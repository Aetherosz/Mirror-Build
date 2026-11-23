#include "MirrorHooks.h"

#include "Engine/World.h"
#include "FGColoredInstanceMeshProxy.h"
#include "FGConstructionMessageInterface.h"
#include "FGFactoryConnectionComponent.h"
#include "FGPipeConnectionComponent.h"
#include "Hologram/FGBuildableHologram.h"
#include "MirrorCore.h"
#include "Patching/NativeHookManager.h"

namespace
{
	/** The class being built from a mirrored hologram, while its Construct runs. */
	const UClass* ConstructingMirrored = nullptr;

	/**
	 * The game draws many machines as instances of one mesh, and the engine lights a mirrored
	 * instance from the wrong side, so it comes out dark. Machines that already opt out of
	 * instancing, like the Refinery, look right mirrored, so a mirrored one opts out too.
	 * Runs before BeginPlay, where the instancing happens. Holds for loaded saves as well.
	 */
	void DrawOnItsOwn(AActor* Actor)
	{
		int32 Changed = 0;
		TInlineComponentArray<UFGColoredInstanceMeshProxy*> Proxies(Actor);
		for (UFGColoredInstanceMeshProxy* Proxy : Proxies)
		{
			if (!Proxy->mBlockInstancing)
			{
				Proxy->mBlockInstancing = true;
				// It skipped its render state while it expected to be instanced.
				Proxy->RecreateRenderState_Concurrent();
				++Changed;
			}
		}
		if (Changed)
		{
			UE_LOG(LogMirrorBuild, Display, TEXT("%s: %d mesh(es) drawn on their own, not instanced"), *Actor->GetName(), Changed);
		}
	}

	/**
	 * Runs after the spawn transform is applied and before BeginPlay, which is where meshes get
	 * instanced and connections register. Only does anything if Construct left the scale out.
	 */
	void OnComponentsRegistered(AActor* Actor)
	{
		if (ConstructingMirrored && Actor && Actor->GetClass() == ConstructingMirrored
			&& !Actor->IsActorInitialized() && !MirrorCore::IsMirrored(Actor))
		{
			MirrorCore::SetActorMirrored(Actor, true);
			UE_LOG(LogMirrorBuild, Display, TEXT("%s: spawn dropped the scale, put back before BeginPlay"), *Actor->GetName());
		}
		if (Actor && !Actor->IsActorInitialized() && MirrorCore::IsMirrored(Actor) && MirrorCore::CanMirror(Actor->GetClass()))
		{
			DrawOnItsOwn(Actor);
		}
	}

	/** Shows in the log whether a save load or a blueprint kept the scale. */
	void OnActorSpawned(AActor* Actor)
	{
		if (!Actor)
		{
			return;
		}
		// Every mirrored spawn, plus what comes out of a blueprint. Not every machine in a big save.
		const bool bMirrored = MirrorCore::IsMirrored(Actor);
		const bool bBlueprintWorld = GetNameSafe(Actor->GetWorld()).Contains(TEXT("Blueprint"));
		if ((bMirrored || bBlueprintWorld) && MirrorCore::CanMirror(Actor->GetClass()))
		{
			UE_LOG(LogMirrorBuild, Display, TEXT("Spawned %s in %s: scale=%s rotation=%s"),
				*Actor->GetName(), *GetNameSafe(Actor->GetWorld()),
				*Actor->GetActorScale3D().ToCompactString(), *Actor->GetActorRotation().ToCompactString());
		}
	}

	void OnWorldInitialized(UWorld* World, const UWorld::InitializationValues)
	{
		if (!World)
		{
			return;
		}
		World->AddOnPostRegisterAllActorComponentsHandler(
			FOnPostRegisterAllActorComponents::FDelegate::CreateStatic(&OnComponentsRegistered));
		World->AddOnActorSpawnedHandler(FOnActorSpawned::FDelegate::CreateStatic(&OnActorSpawned));
	}

	template <typename ComponentType>
	void ComparePorts(const AActor* Built, const TArray<ComponentType*>& HologramPorts, const TCHAR* Kind)
	{
		TInlineComponentArray<ComponentType*> BuiltPorts(Built);
		int32 Matched = 0;
		for (const ComponentType* Port : BuiltPorts)
		{
			const FVector Location = Port->GetComponentLocation();
			const FVector Normal = Port->GetConnectorNormal();
			for (const ComponentType* Preview : HologramPorts)
			{
				if (Preview && Location.Equals(Preview->GetComponentLocation(), 1.0)
					&& FVector::DotProduct(Normal, Preview->GetConnectorNormal()) > 0.99)
				{
					++Matched;
					break;
				}
			}
			UE_LOG(LogMirrorBuild, Display, TEXT("  %s %s: at=%s normal=%s"),
				Kind, *Port->GetName(), *Location.ToCompactString(), *Normal.ToCompactString());
		}
		UE_LOG(LogMirrorBuild, Display, TEXT("  %s ports same as hologram: %d of %d (hologram has %d)"),
			Kind, Matched, BuiltPorts.Num(), HologramPorts.Num());
	}

	void ReportBuilt(const AFGBuildableHologram* Hologram, const AActor* Built)
	{
		UE_LOG(LogMirrorBuild, Display, TEXT("Built mirrored %s: hologram=%s built=%s"), *GetNameSafe(Built),
			*Hologram->GetActorTransform().ToString(), Built ? *Built->GetActorTransform().ToString() : TEXT("none"));
		if (Built)
		{
			ComparePorts(Built, Hologram->GetCachedFactoryConnectionComponents(), TEXT("belt"));
			ComparePorts(Built, Hologram->GetCachedPipeConnectionComponents(), TEXT("pipe"));
		}
	}
}

void FMirrorHooks::Register()
{
	FWorldDelegates::OnPostWorldInitialization.AddStatic(&OnWorldInitialized);

	AFGBuildableHologram* Sample = GetMutableDefault<AFGBuildableHologram>();

	SUBSCRIBE_METHOD_VIRTUAL(AFGBuildableHologram::Construct, Sample,
		[](auto& Scope, AFGBuildableHologram* Self, TArray<AActor*>& OutChildren, FNetConstructionID Id)
		{
			const bool bMirrored = MirrorCore::CanMirror(Self->GetBuildClass()) && MirrorCore::IsMirrored(Self);
			if (bMirrored)
			{
				MirrorCore::SetHologramMirrored(Self, true);
			}
			TGuardValue<const UClass*> Guard(ConstructingMirrored, bMirrored ? Self->GetBuildClass().Get() : nullptr);
			Scope(Self, OutChildren, Id);
			if (bMirrored)
			{
				ReportBuilt(Self, Scope.GetResult());
			}
		});

	// The override in AFGBuildableHologram is protected, so it's named through the base class and found
	// through the sample object. The method belongs to the interface, so Self is the interface pointer
	// whatever its type says.
	SUBSCRIBE_METHOD_VIRTUAL_AFTER(AFGHologram::SerializeConstructMessage, Sample,
		[](AFGHologram* Self, FArchive& Ar, FNetConstructionID)
		{
			MirrorCore::SerializeMirrorFlag(reinterpret_cast<IFGConstructionMessageInterface*>(Self), Ar);
		});

	UE_LOG(LogMirrorBuild, Display, TEXT("Mirror Build prototype: hooks installed"));
}
