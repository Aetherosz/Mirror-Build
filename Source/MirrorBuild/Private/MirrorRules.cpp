#include "MirrorRules.h"

#include "Buildables/FGBuildableFactory.h"
#include "Buildables/FGBuildablePipeHyperAttachment.h"
#include "Buildables/FGBuildablePipelineAttachment.h"
#include "Buildables/FGBuildablePortalBase.h"
#include "Buildables/FGBuildableResourceSink.h"
#include "Buildables/FGBuildableSpaceElevator.h"
#include "Buildables/FGBuildableTrainPlatform.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "FGFactoryConnectionComponent.h"
#include "FGPipeConnectionComponent.h"
#include "MirrorCore.h"

namespace
{
	struct FPortTotals
	{
		int32 Num = 0;
		double AlongX = 0.0;
		double AlongY = 0.0;

		void Add(const USceneComponent* Component, const FTransform& Place)
		{
			const UFGFactoryConnectionComponent* Belt = Cast<UFGFactoryConnectionComponent>(Component);
			// Not the Base class: hypertube entrances are no input or output.
			const UFGPipeConnectionComponent* Pipe = Cast<UFGPipeConnectionComponent>(Component);
			if ((Belt && Belt->GetDirection() != EFactoryConnectionDirection::FCD_SNAP_ONLY)
				|| (Pipe && Pipe->GetPipeConnectionType() != EPipeConnectionType::PCT_SNAP_ONLY))
			{
				const FVector Normal = Place.Rotator().Vector();
				++Num;
				AlongX += FMath::Abs(Normal.X);
				AlongY += FMath::Abs(Normal.Y);
			}
		}
	};

	/** Blueprint components only exist as templates until spawned, so they're read from the class. */
	void AddNode(UBlueprintGeneratedClass* Leaf, const USCS_Node* Node, const FTransform& Parent, FPortTotals& Totals)
	{
		if (!Node)
		{
			return;
		}
		FTransform Place = Parent;
		if (const USceneComponent* Component = Cast<USceneComponent>(Node->GetActualComponentTemplate(Leaf)))
		{
			Place = Component->GetRelativeTransform() * Parent;
			Totals.Add(Component, Place);
		}
		for (const USCS_Node* Child : Node->GetChildNodes())
		{
			AddNode(Leaf, Child, Place, Totals);
		}
	}

	FMirrorRule Work(UClass* Class)
	{
		FMirrorRule Rule;

		// AFGBuildableFactory leaves out belts, lifts, splitters, mergers, pipes, poles, walls and decor.
		// Pumps, valves and junctions are factories too, but they're logistics.
		// Special buildings and train platforms have ports but no use for a mirror.
		if (!Class->IsChildOf<AFGBuildableFactory>() || Class->IsChildOf<AFGBuildablePipelineAttachment>()
			|| Class->IsChildOf<AFGBuildablePipeHyperAttachment>() || Class->IsChildOf<AFGBuildableSpaceElevator>()
			|| Class->IsChildOf<AFGBuildableTrainPlatform>()
			|| Class->IsChildOf<AFGBuildableResourceSink>() || Class->IsChildOf<AFGBuildablePortalBase>()
			// The gift tree by name: its C++ class is a general one that modded machines use too.
			|| Class->GetFName() == TEXT("Build_TreeGiftProducer_C"))
		{
			return Rule;
		}

		FPortTotals Totals;
		TInlineComponentArray<USceneComponent*> Native(Cast<AActor>(Class->GetDefaultObject()));
		for (const USceneComponent* Component : Native)
		{
			Totals.Add(Component, Component->GetRelativeTransform());
		}

		UBlueprintGeneratedClass* Leaf = Cast<UBlueprintGeneratedClass>(Class);
		for (UClass* Current = Class; Current && Leaf; Current = Current->GetSuperClass())
		{
			const UBlueprintGeneratedClass* Generated = Cast<UBlueprintGeneratedClass>(Current);
			if (!Generated || !Generated->SimpleConstructionScript)
			{
				continue;
			}
			for (const USCS_Node* Node : Generated->SimpleConstructionScript->GetRootNodes())
			{
				AddNode(Leaf, Node, FTransform::Identity, Totals);
			}
		}

		Rule.NumPorts = Totals.Num;
		Rule.bCanMirror = Totals.Num > 0;
		Rule.bHalfTurn = Rule.bCanMirror && Totals.AlongY >= Totals.AlongX;
		return Rule;
	}
}

FMirrorRule MirrorRules::Get(const UClass* BuildClass)
{
	if (!BuildClass)
	{
		return FMirrorRule();
	}
	check(IsInGameThread());

	static TMap<TWeakObjectPtr<const UClass>, FMirrorRule> Cache;
	if (const FMirrorRule* Known = Cache.Find(BuildClass))
	{
		return *Known;
	}
	const FMirrorRule Rule = Work(const_cast<UClass*>(BuildClass));
	Cache.Add(BuildClass, Rule);
	if (Rule.bCanMirror)
	{
		UE_LOG(LogMirrorBuild, Display, TEXT("Can mirror %s: ports=%d halfturn=%d"),
			*BuildClass->GetName(), Rule.NumPorts, Rule.bHalfTurn ? 1 : 0);
	}
	return Rule;
}
