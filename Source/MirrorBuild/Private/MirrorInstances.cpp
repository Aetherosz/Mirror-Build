#include "MirrorInstances.h"

#include "AbstractInstanceManager.h"
#include "Buildables/FGBuildable.h"
#include "FGColoredInstanceMeshProxy.h"
#include "InstanceData.h"
#include "MirrorCore.h"
#include "Patching/NativeHookManager.h"

/*
 * Many machines (the Foundry's base, the Coal Generator, the Blender) aren't components at all:
 * the game draws them as lightweight instances shared by every building of that kind. A mirrored
 * instance comes out dark, the same lighting fault as instanced components. So on a mirrored
 * building each instance is hidden and an ordinary mesh part, drawn on its own, stands in for it.
 * Hiding only moves the visible copy away: the collision, the handle and the game's code that
 * talks to it are untouched. What the game later does to the instance is passed on to the stand-in.
 */
namespace
{
	const FName StandInTag(TEXT("MirrorBuildStandIn"));

	/** Instance handle -> the mesh part drawn in its place. */
	TMap<const FInstanceOwnershipHandle*, TWeakObjectPtr<UFGColoredInstanceMeshProxy>> StandIns;

	/** Set while this file hides an instance itself, so the hide isn't passed on to the stand-in. */
	bool bHidingForStandIn = false;

	UFGColoredInstanceMeshProxy* FindStandIn(const FInstanceOwnershipHandle* Handle)
	{
		const TWeakObjectPtr<UFGColoredInstanceMeshProxy>* Found = StandIns.Find(Handle);
		UFGColoredInstanceMeshProxy* Mesh = Found ? Found->Get() : nullptr;
		// Handles are freed and their memory reused, so the owner has to match as well.
		return Mesh && Mesh->GetOwner() == Handle->GetOwner() ? Mesh : nullptr;
	}

	/** When the game sets the instances up again (a new skin), the old stand-in goes. */
	void RemoveOldStandIn(AActor* Actor, const UStaticMesh* Mesh, const FTransform& Where)
	{
		TInlineComponentArray<UFGColoredInstanceMeshProxy*> Meshes(Actor);
		for (UFGColoredInstanceMeshProxy* Old : Meshes)
		{
			if (Old->ComponentHasTag(StandInTag) && Old->GetStaticMesh() == Mesh
				&& Old->GetComponentTransform().Equals(Where, 0.1))
			{
				Old->DestroyComponent();
			}
		}
	}

	void StandIn(AActor* Owner, const FTransform& ActorTransform, const FInstanceData& Instance,
		const FInstanceOwnerHandlePtr& Handle, bool bHidden)
	{
		USceneComponent* Root = Owner->GetRootComponent();
		if (!Root || !Instance.StaticMesh || !Handle.IsValid() || !Handle->IsInstanced())
		{
			return;
		}
		// Same sum the instance manager does for the instance itself.
		const FTransform Where = Instance.RelativeTransform * ActorTransform;
		RemoveOldStandIn(Owner, Instance.StaticMesh, Where);

		UFGColoredInstanceMeshProxy* Mesh = NewObject<UFGColoredInstanceMeshProxy>(Owner);
		Mesh->mBlockInstancing = true;
		Mesh->ComponentTags.Add(StandInTag);
		Mesh->SetMobility(Root->Mobility);
		Mesh->SetStaticMesh(Instance.StaticMesh);
		for (int32 Slot = 0; Slot < Instance.OverridenMaterials.Num(); ++Slot)
		{
			if (Instance.OverridenMaterials[Slot])
			{
				Mesh->SetMaterial(Slot, Instance.OverridenMaterials[Slot]);
			}
		}
		Mesh->SetCastShadow(Instance.bCastShadows);
		// The hidden instance keeps its collision, so the stand-in only needs to be seen.
		Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Mesh->SetNumCustomDataFloats(Instance.NumCustomDataFloats);
		Mesh->SetHiddenInGame(bHidden);
		Mesh->SetupAttachment(Root);
		Mesh->SetWorldTransform(Where);
		Mesh->RegisterComponent();
		Owner->AddInstanceComponent(Mesh);
		for (int32 Float = 0; Float < Instance.DefaultPerInstanceCustomData.Num(); ++Float)
		{
			Mesh->SetCustomPrimitiveDataFloat(Float, Instance.DefaultPerInstanceCustomData[Float]);
		}
		if (AFGBuildable* Building = Cast<AFGBuildable>(Owner))
		{
			Mesh->SetCustomizationData(Building->GetCustomizationData_Native());
		}

		TGuardValue<bool> Guard(bHidingForStandIn, true);
		Handle->HideInstance();
		StandIns.Add(Handle.Get(), Mesh);
		UE_LOG(LogMirrorBuild, Display, TEXT("%s: %s drawn as a plain mesh, not a lightweight instance%s"),
			*Owner->GetName(), *Instance.StaticMesh->GetName(), bHidden ? TEXT(" (hidden until built)") : TEXT(""));
	}
}

void MirrorInstances::Register()
{
	// Every lightweight instance, a building's own and the ones it adds later, comes through here.
	SUBSCRIBE_METHOD_AFTER(AAbstractInstanceManager::SetInstanced,
		[](AAbstractInstanceManager*, AActor* Owner, const FTransform& ActorTransform, const FInstanceData& Instance,
			FInstanceOwnerHandlePtr& Handle, bool bInitializeHidden)
		{
			if (Owner && MirrorCore::IsMirrored(Owner) && MirrorCore::CanMirror(Owner->GetClass()))
			{
				StandIn(Owner, ActorTransform, Instance, Handle, bInitializeHidden);
			}
		});

	// The build animation unhides the instances at the end. Show the stand-in and keep the instance hidden.
	SUBSCRIBE_METHOD(FInstanceOwnershipHandle::UnHideInstance,
		[](auto& Scope, FInstanceOwnershipHandle* Self, bool)
		{
			if (UFGColoredInstanceMeshProxy* Mesh = FindStandIn(Self))
			{
				Mesh->SetHiddenInGame(false);
				Scope.Cancel();
			}
		});

	SUBSCRIBE_METHOD_AFTER(FInstanceOwnershipHandle::HideInstance,
		[](FInstanceOwnershipHandle* Self, bool)
		{
			if (!bHidingForStandIn)
			{
				if (UFGColoredInstanceMeshProxy* Mesh = FindStandIn(Self))
				{
					Mesh->SetHiddenInGame(true);
				}
			}
		});

	// Paint, power lights and the like arrive as instance data.
	SUBSCRIBE_METHOD_AFTER(FInstanceOwnershipHandle::SetPrimitiveDataByArray,
		[](const FInstanceOwnershipHandle* Self, const TArray<float>& Data, bool)
		{
			if (UFGColoredInstanceMeshProxy* Mesh = FindStandIn(Self))
			{
				for (int32 Index = 0; Index < Data.Num(); ++Index)
				{
					Mesh->SetCustomPrimitiveDataFloat(Index, Data[Index]);
				}
			}
		});

	SUBSCRIBE_METHOD_AFTER(FInstanceOwnershipHandle::SetPrimitiveDataByID,
		[](const FInstanceOwnershipHandle* Self, float Value, const int32 Index, bool)
		{
			if (UFGColoredInstanceMeshProxy* Mesh = FindStandIn(Self))
			{
				Mesh->SetCustomPrimitiveDataFloat(Index, Value);
			}
		});
}
