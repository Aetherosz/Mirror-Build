#include "MirrorCore.h"

#include "MirrorRules.h"

#include "Components/BoxComponent.h"
#include "FGConstructionMessageInterface.h"
#include "GameFramework/Actor.h"
#include "Hologram/FGHologram.h"

DEFINE_LOG_CATEGORY(LogMirrorBuild);

namespace
{
	/**
	 * UBoxComponent::GetScaledBoxExtent multiplies by the signed scale, and a query made with a negative
	 * extent misses. A box is symmetrical, so flipping it back covers exactly the same space.
	 */
	void KeepBoxExtentsPositive(AActor* Actor, bool bMirrored)
	{
		TInlineComponentArray<UBoxComponent*> Boxes(Actor);
		for (UBoxComponent* Box : Boxes)
		{
			// Flipping back a box with something attached would flip that back too.
			if (Box == Actor->GetRootComponent() || Box->GetNumChildrenComponents() > 0)
			{
				continue;
			}
			FVector Scale = Box->GetRelativeScale3D();
			Scale.Y = bMirrored ? -FMath::Abs(Scale.Y) : FMath::Abs(Scale.Y);
			Box->SetRelativeScale3D(Scale);
		}
	}
}

bool MirrorCore::CanMirror(const UClass* BuildClass)
{
	return MirrorRules::Get(BuildClass).bCanMirror;
}

bool MirrorCore::IsMirrored(const AActor* Actor)
{
	const USceneComponent* Root = Actor ? Actor->GetRootComponent() : nullptr;
	return Root && Root->GetRelativeScale3D().Y < 0.0;
}

void MirrorCore::SetActorMirrored(AActor* Actor, bool bMirrored)
{
	USceneComponent* Root = Actor ? Actor->GetRootComponent() : nullptr;
	if (!Root || IsMirrored(Actor) == bMirrored)
	{
		return;
	}
	FVector Scale = Root->GetRelativeScale3D();
	Scale.Y = -Scale.Y;
	Root->SetRelativeScale3D(Scale);
}

void MirrorCore::SetHologramMirrored(AFGHologram* Hologram, bool bMirrored)
{
	if (!IsValid(Hologram))
	{
		return;
	}
	if (IsMirrored(Hologram) != bMirrored)
	{
		SetActorMirrored(Hologram, bMirrored);
		KeepBoxExtentsPositive(Hologram, bMirrored);
	}
	// Copies in a grid are child holograms with their own transforms. Children of another class, like a
	// platform's track or auto-connected belts, are left alone.
	for (AFGHologram* Child : Hologram->GetHologramChildren())
	{
		if (IsValid(Child) && Child->GetBuildClass() == Hologram->GetBuildClass())
		{
			SetHologramMirrored(Child, bMirrored);
		}
	}
}

void MirrorCore::Toggle(AFGHologram* Hologram)
{
	if (!IsValid(Hologram))
	{
		return;
	}
	SetHologramMirrored(Hologram, !IsMirrored(Hologram));
	if (!MirrorRules::Get(Hologram->GetBuildClass()).bHalfTurn)
	{
		return;
	}

	// The scroll value keeps the half turn through placement updates. The actor rotation shows it
	// straight away, and is the only thing that turns a locked hologram.
	Hologram->SetScrollRotateValue(((Hologram->GetScrollRotateValue() + 180) % 360 + 360) % 360);
	Hologram->AddActorLocalRotation(FRotator(0.0, 180.0, 0.0));
}

void MirrorCore::SerializeMirrorFlag(IFGConstructionMessageInterface* Message, FArchive& Ar)
{
	// static_cast moves the pointer back from the interface to the start of the actor.
	AFGHologram* Hologram = static_cast<AFGHologram*>(Message);
	if (!Hologram || !CanMirror(Hologram->GetBuildClass()))
	{
		return;
	}
	uint8 Flag = IsMirrored(Hologram) ? 1 : 0;
	Ar.SerializeBits(&Flag, 1);
	if (Ar.IsLoading() && !Ar.IsError())
	{
		SetHologramMirrored(Hologram, (Flag & 1) != 0);
	}
}
