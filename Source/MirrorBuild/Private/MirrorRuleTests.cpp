#include "CoreMinimal.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "AssetRegistry/AssetRegistryModule.h"
#include "Buildables/FGBuildable.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "FGFactoryConnectionComponent.h"
#include "FGPipeConnectionComponent.h"
#include "Hologram/FGBuildableHologram.h"
#include "Misc/AutomationTest.h"
#include "MirrorCore.h"
#include "MirrorRules.h"

namespace
{
	/** Every vanilla building that should mirror. Anything else that does is a failure. */
	const TCHAR* const ExpectedMirrorable[] = {
		// Production
		TEXT("Build_SmelterMk1"), TEXT("Build_FoundryMk1"), TEXT("Build_ConstructorMk1"), TEXT("Build_AssemblerMk1"),
		TEXT("Build_ManufacturerMk1"), TEXT("Build_OilRefinery"), TEXT("Build_Packager"), TEXT("Build_Blender"),
		TEXT("Build_Converter"), TEXT("Build_HadronCollider"), TEXT("Build_QuantumEncoder"),
		// Extraction
		TEXT("Build_MinerMk1"), TEXT("Build_MinerMk2"), TEXT("Build_MinerMk3"), TEXT("Build_OilPump"),
		TEXT("Build_WaterPump"), TEXT("Build_FrackingExtractor"),
		// Power
		TEXT("Build_GeneratorBiomass_Automated"), TEXT("Build_GeneratorCoal"), TEXT("Build_GeneratorFuel"),
		TEXT("Build_GeneratorNuclear"), TEXT("Build_AlienPowerBuilding"),
		// Storage
		TEXT("Build_StorageContainerMk1"), TEXT("Build_StorageContainerMk2"), TEXT("Build_CentralStorage"),
		TEXT("Build_PipeStorageTank"), TEXT("Build_IndustrialTank"),
		// Stations
		TEXT("Build_TruckStation"), TEXT("Build_FluidTruckStation"), TEXT("Build_DroneStation"),
	};

	/** The ones with their ports on an X end. */
	const TCHAR* const NoHalfTurn[] = {TEXT("Build_DroneStation"), TEXT("Build_FrackingExtractor")};

	TArray<UClass*> AllBuildables()
	{
		IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
		Registry.SearchAllAssets(true);
		TArray<FAssetData> Assets;
		Registry.GetAssetsByPath(TEXT("/Game/FactoryGame"), Assets, true);

		TArray<UClass*> Classes;
		for (const FAssetData& Asset : Assets)
		{
			const FString Name = Asset.AssetName.ToString();
			if (!Name.StartsWith(TEXT("Build_")) || Name.EndsWith(TEXT("_C")))
			{
				continue;
			}
			UClass* Class = LoadObject<UClass>(nullptr, *FString::Printf(TEXT("%s.%s_C"), *Asset.PackageName.ToString(), *Name), nullptr, LOAD_NoWarn | LOAD_Quiet);
			if (Class && Class->IsChildOf(AFGBuildable::StaticClass()))
			{
				Classes.AddUnique(Class);
			}
		}
		return Classes;
	}

	template <int32 Num>
	bool IsListed(const TCHAR* const (&List)[Num], const FString& Name)
	{
		for (const TCHAR* Entry : List)
		{
			if (Name == Entry)
			{
				return true;
			}
		}
		return false;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMirrorRulesTest, "MirrorBuild.Rules",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMirrorRulesTest::RunTest(const FString&)
{
	const TArray<UClass*> Classes = AllBuildables();
	int32 Mirrorable = 0;
	for (const UClass* Class : Classes)
	{
		const FString Name = Class->GetName().LeftChop(2);
		const FMirrorRule Rule = MirrorRules::Get(Class);
		const bool bExpected = IsListed(ExpectedMirrorable, Name);
		TestEqual(Name + TEXT(" can mirror"), Rule.bCanMirror, bExpected);
		if (bExpected)
		{
			TestEqual(Name + TEXT(" half turn"), Rule.bHalfTurn, !IsListed(NoHalfTurn, Name));
		}
		Mirrorable += Rule.bCanMirror ? 1 : 0;
	}
	TestEqual(TEXT("all expected buildings found"), Mirrorable, static_cast<int32>(UE_ARRAY_COUNT(ExpectedMirrorable)));
	AddInfo(FString::Printf(TEXT("%d of %d buildables can mirror"), Mirrorable, Classes.Num()));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMirrorEveryBuildingTest, "MirrorBuild.EveryBuilding",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMirrorEveryBuildingTest::RunTest(const FString&)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	ON_SCOPE_EXIT
	{
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
	};

	const FProperty* BuildClass = AFGHologram::StaticClass()->FindPropertyByName(TEXT("mBuildClass"));
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	int32 Buildings = 0;
	int32 Ports = 0;
	for (UClass* Class : AllBuildables())
	{
		const FMirrorRule Rule = MirrorRules::Get(Class);
		if (!Rule.bCanMirror)
		{
			continue;
		}
		++Buildings;
		const FString Name = Class->GetName();
		const FTransform Place(FRotator(0, 90, 0), FVector(Buildings * 20000.0, 5000.0, 100.0));

		// The real key press, on a hologram standing where the building will be.
		AFGBuildableHologram* Hologram = World->SpawnActor<AFGBuildableHologram>();
		if (!TestNotNull(Name + TEXT(" hologram"), Hologram))
		{
			continue;
		}
		if (!Hologram->GetRootComponent())
		{
			USceneComponent* Root = NewObject<USceneComponent>(Hologram);
			Hologram->SetRootComponent(Root);
			Root->RegisterComponent();
		}
		Hologram->GetRootComponent()->SetMobility(EComponentMobility::Movable);
		*BuildClass->ContainerPtrToValuePtr<TSubclassOf<AActor>>(Hologram) = Class;
		Hologram->SetActorLocationAndRotation(Place.GetLocation(), Place.Rotator());
		MirrorCore::Toggle(Hologram);
		const FTransform MirrorPlace = Hologram->GetActorTransform();

		const AActor* Plain = World->SpawnActor<AActor>(Class, Place, Params);
		const AActor* Mirror = World->SpawnActor<AActor>(Class, MirrorPlace, Params);
		if (!TestNotNull(Name + TEXT(" plain"), Plain) || !TestNotNull(Name + TEXT(" mirrored"), Mirror))
		{
			continue;
		}
		TestTrue(Name + TEXT(" spawn keeps the mirror"), MirrorCore::IsMirrored(Mirror));

		const FVector Reflect = Rule.bHalfTurn ? FVector(-1, 1, 1) : FVector(1, -1, 1);
		TInlineComponentArray<USceneComponent*> PlainParts(Plain);
		TInlineComponentArray<USceneComponent*> MirrorParts(Mirror);
		int32 Found = 0;
		for (const USceneComponent* A : PlainParts)
		{
			if (!A->IsA<UFGFactoryConnectionComponent>() && !A->IsA<UFGPipeConnectionComponent>())
			{
				continue;
			}
			const FVector Where = Place.InverseTransformPosition(A->GetComponentLocation()) * Reflect;
			const FVector Facing = Place.InverseTransformVectorNoScale(A->GetComponentRotation().Vector()) * Reflect;
			for (const USceneComponent* B : MirrorParts)
			{
				if (B->GetFName() == A->GetFName())
				{
					++Found;
					TestTrue(Name + TEXT(" ") + A->GetName() + TEXT(" position"),
						B->GetComponentLocation().Equals(Place.TransformPosition(Where), 0.1));
					TestTrue(Name + TEXT(" ") + A->GetName() + TEXT(" normal"),
						B->GetComponentRotation().Vector().Equals(Place.TransformVectorNoScale(Facing), 0.001));
				}
			}
		}
		TestEqual(Name + TEXT(" ports found"), Found, Rule.NumPorts);
		Ports += Found;

		MirrorCore::Toggle(Hologram);
		TestFalse(Name + TEXT(" second press unmirrors"), MirrorCore::IsMirrored(Hologram));
		TestTrue(Name + TEXT(" second press puts it back"), Hologram->GetActorTransform().Equals(Place, 0.01));
	}
	AddInfo(FString::Printf(TEXT("%d buildings, %d ports checked"), Buildings, Ports));
	return true;
}

#endif
