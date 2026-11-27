#include "MirrorBlueprints.h"

#include "Buildables/FGBuildable.h"
#include "FGBlueprintSubsystem.h"
#include "FGFactoryBlueprintTypes.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "MirrorCore.h"
#include "Patching/NativeHookManager.h"

namespace
{
	// Named so the commas in the template don't split the SUBSCRIBE_METHOD macro arguments.
	using FPostSerializeCallback = TFunction<void(AFGBuildable*, int32)>;
	using FPreBeginPlayCallback = TFunction<void()>;

	/** Buildings are metres apart, so a few centimetres is plenty for float round trips. */
	constexpr double MatchTolerance = 5.0;

	struct FMirroredEntry
	{
		FString ClassPath;
		/** Location relative to the blueprint origin, ignoring its scale. */
		FVector Location = FVector::ZeroVector;
		bool bUsed = false;
	};

	FString SidecarPath(AFGBlueprintSubsystem* Subsystem, const FString& BlueprintName)
	{
		return FPaths::Combine(Subsystem->GetSessionBlueprintPath(),
			AFGBlueprintSubsystem::SanitizeBlueprintFileName(BlueprintName) + TEXT(".sbpmirror"));
	}

	void WriteSidecar(AFGBlueprintSubsystem* Subsystem, const FBlueprintRecord& Record, const FTransform& Origin,
		const TArray<AFGBuildable*>& Buildables)
	{
		if (!Subsystem || !Record.IsValid())
		{
			return;
		}
		TArray<FString> Lines;
		for (const AFGBuildable* Building : Buildables)
		{
			if (IsValid(Building) && MirrorCore::IsMirrored(Building) && MirrorCore::CanMirror(Building->GetClass()))
			{
				const FVector Local = Origin.InverseTransformPositionNoScale(Building->GetActorLocation());
				Lines.Add(FString::Printf(TEXT("%s\t%.3f\t%.3f\t%.3f"),
					*Building->GetClass()->GetPathName(), Local.X, Local.Y, Local.Z));
			}
		}
		const FString Path = SidecarPath(Subsystem, Record.BlueprintName);
		if (Lines.IsEmpty())
		{
			// Saved again without mirrored buildings: an old list must not apply to the new one.
			IFileManager::Get().Delete(*Path, false, false, true);
			return;
		}
		const bool bSaved = FFileHelper::SaveStringArrayToFile(Lines, *Path);
		UE_LOG(LogMirrorBuild, Display, TEXT("Blueprint %s: %d mirrored building(s) %s %s"),
			*Record.BlueprintName, Lines.Num(), bSaved ? TEXT("written to") : TEXT("FAILED to write to"), *Path);
	}

	TArray<FMirroredEntry> ReadSidecar(AFGBlueprintSubsystem* Subsystem, const FString& BlueprintName)
	{
		TArray<FMirroredEntry> Entries;
		TArray<FString> Lines;
		if (!Subsystem || BlueprintName.IsEmpty()
			|| !FFileHelper::LoadFileToStringArray(Lines, *SidecarPath(Subsystem, BlueprintName)))
		{
			return Entries;
		}
		for (const FString& Line : Lines)
		{
			TArray<FString> Parts;
			if (Line.ParseIntoArray(Parts, TEXT("\t")) == 4)
			{
				FMirroredEntry& Entry = Entries.AddDefaulted_GetRef();
				Entry.ClassPath = Parts[0];
				Entry.Location = FVector(FCString::Atod(*Parts[1]), FCString::Atod(*Parts[2]), FCString::Atod(*Parts[3]));
			}
		}
		return Entries;
	}

	/** One blueprint load: where it's placed and which of its buildings were mirrored. */
	struct FLoadContext
	{
		FString BlueprintName;
		FTransform Origin;
		TArray<FMirroredEntry> Entries;
		int32 NumMirrored = 0;
	};

	/**
	 * A mirror that came through as a negative X scale is the same shape as a Y mirror turned half
	 * way round, but X turns the ports inward (see MirrorCore.h). Put it back the way the mod builds it.
	 */
	bool FixXMirror(AFGBuildable* Building)
	{
		const FTransform Current = Building->GetActorTransform();
		FVector Scale = Current.GetScale3D();
		if (Scale.X >= 0.0)
		{
			return false;
		}
		Scale.X = FMath::Abs(Scale.X);
		Scale.Y = -FMath::Abs(Scale.Y);
		const FQuat Rotation = Current.GetRotation() * FQuat(FVector::UpVector, PI);
		Building->SetActorTransform(FTransform(Rotation, Current.GetLocation(), Scale));
		return true;
	}

	void Apply(FLoadContext& Context, AFGBuildable* Building)
	{
		if (!IsValid(Building) || !MirrorCore::CanMirror(Building->GetClass()))
		{
			return;
		}
		bool bChanged = FixXMirror(Building);
		if (!bChanged && !MirrorCore::IsMirrored(Building))
		{
			const FString ClassPath = Building->GetClass()->GetPathName();
			const FVector Local = Context.Origin.InverseTransformPositionNoScale(Building->GetActorLocation());
			for (FMirroredEntry& Entry : Context.Entries)
			{
				if (!Entry.bUsed && Entry.ClassPath == ClassPath && FVector::Dist(Entry.Location, Local) <= MatchTolerance)
				{
					Entry.bUsed = true;
					MirrorCore::SetActorMirrored(Building, true);
					bChanged = true;
					break;
				}
			}
		}
		if (bChanged)
		{
			++Context.NumMirrored;
			UE_LOG(LogMirrorBuild, Display, TEXT("Blueprint %s: mirrored %s in %s%s"), *Context.BlueprintName,
				*Building->GetName(), *GetNameSafe(Building->GetWorld()),
				Building->HasActorBegunPlay() ? TEXT(" (after it started: may look dark or miss its ports)") : TEXT(""));
		}
	}
}

void MirrorBlueprints::Register()
{
	SUBSCRIBE_METHOD(AFGBlueprintSubsystem::WriteBlueprintToArchive,
		[](auto&, AFGBlueprintSubsystem* Self, const FBlueprintRecord& Record, const FTransform& Origin,
			const TArray<AFGBuildable*>& Buildables, FIntVector)
		{
			WriteSidecar(Self, Record, Origin, Buildables);
		});

	// Every load goes through here: the hologram preview, the placement itself and the designer.
	// Each building is mirrored right after the game reads it, and once more before they all start,
	// in case the game skipped the per-building call.
	// The lambda is in brackets because its capture lists have commas the macro would split on.
	SUBSCRIBE_METHOD(AFGBlueprintSubsystem::LoadStoredBlueprint,
		([](auto& Scope, AFGBlueprintSubsystem* Self, UFGBlueprintDescriptor* Desc, const FTransform& Origin,
			TArray<AFGBuildable*>& OutSpawned, bool bUseBlueprintWorld, AFGBuildableBlueprintDesigner* Designer,
			APawn* Instigator, AFGBlueprintProxy* Proxy, const FPostSerializeCallback& PostSerialize,
			const FPreBeginPlayCallback& PreBeginPlay)
		{
			TSharedRef<FLoadContext> Context = MakeShared<FLoadContext>();
			Context->BlueprintName = Desc ? Desc->GetBlueprintNameAsString() : FString();
			Context->Origin = Origin;
			Context->Entries = ReadSidecar(Self, Context->BlueprintName);

			FPostSerializeCallback WrappedPostSerialize =
				[Context, Original = PostSerialize](AFGBuildable* Building, int32 Index)
				{
					if (Original)
					{
						Original(Building, Index);
					}
					Apply(*Context, Building);
				};
			FPreBeginPlayCallback WrappedPreBeginPlay =
				// The load fills OutSpawned and calls this before it returns, so the reference is still good.
				[Context, Original = PreBeginPlay, &OutSpawned]()
				{
					for (AFGBuildable* Building : OutSpawned)
					{
						Apply(*Context, Building);
					}
					if (Original)
					{
						Original();
					}
				};

			Scope(Self, Desc, Origin, OutSpawned, bUseBlueprintWorld, Designer, Instigator, Proxy,
				WrappedPostSerialize, WrappedPreBeginPlay);

			// Last chance if neither call came. Late, but better than back to front.
			for (AFGBuildable* Building : OutSpawned)
			{
				Apply(*Context, Building);
			}
			int32 NumUnmatched = 0;
			for (const FMirroredEntry& Entry : Context->Entries)
			{
				NumUnmatched += Entry.bUsed ? 0 : 1;
			}
			if (Context->NumMirrored > 0 || NumUnmatched > 0)
			{
				UE_LOG(LogMirrorBuild, Display, TEXT("Blueprint %s loaded in %s: %d mirrored, %d listed but not found"),
					*Context->BlueprintName, bUseBlueprintWorld ? TEXT("the preview world") : TEXT("the game"),
					Context->NumMirrored, NumUnmatched);
			}
		}));

	UE_LOG(LogMirrorBuild, Display, TEXT("Mirror Build: blueprint hooks installed"));
}
