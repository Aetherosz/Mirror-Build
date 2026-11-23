#include "CoreMinimal.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "FGConstructionMessageInterface.h"
#include "FGFactoryConnectionComponent.h"
#include "FGPipeConnectionComponent.h"
#include "Hologram/FGBuildableHologram.h"
#include "Misc/AutomationTest.h"
#include "MirrorCore.h"
#include "Patching/NativeHookManager.h"
#include "Serialization/BitReader.h"
#include "Serialization/BitWriter.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"

// These run against the engine and the starter project's assets. The game's own code is stubbed in the
// editor, so nothing here says that placing, saving or loading works in the game.

namespace
{
	const TCHAR* const RefineryPath = TEXT("/Game/FactoryGame/Buildable/Factory/OilRefinery/Build_OilRefinery.Build_OilRefinery_C");

	struct FTestWorld
	{
		UWorld* World = nullptr;

		FTestWorld()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false);
			GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
		}

		~FTestWorld()
		{
			GEngine->DestroyWorldContext(World);
			World->DestroyWorld(false);
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMirrorTransformTest, "MirrorBuild.Transform",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMirrorTransformTest::RunTest(const FString&)
{
	const FTransform Port(FRotator(0, 90, 0), FVector(-200, 1000, 100));
	const FTransform SidePort(FRotator(0, 30, 0), FVector(500, -300, 175));
	for (const FRotator& Rotation : {FRotator(0, 0, 0), FRotator(0, 45, 0), FRotator(0, 90, 0), FRotator(0, -137, 0), FRotator(20, 70, 10)})
	{
		const FTransform Normal(Rotation, FVector(120000, -56000, 3400));
		const FTransform Mirrored(Rotation.Quaternion() * FRotator(0, 180, 0).Quaternion(), Normal.GetLocation(), FVector(1, -1, 1));
		for (const FTransform& Local : {Port, SidePort})
		{
			const FVector Where = Local.GetLocation();
			const FVector Facing = Local.GetRotation().GetAxisX();
			const FTransform World = Local * Mirrored;
			TestTrue(TEXT("port is at the left/right reflected place"),
				World.GetLocation().Equals(Normal.TransformPosition(FVector(-Where.X, Where.Y, Where.Z)), 0.1));
			TestTrue(TEXT("connector normal is the reflected normal"),
				World.Rotator().Vector().Equals(Normal.TransformVectorNoScale(FVector(-Facing.X, Facing.Y, Facing.Z)), 0.001));
		}

		TArray<uint8> Bytes;
		FTransform3f Saved(Mirrored);
		FMemoryWriter Writer(Bytes);
		Writer << Saved;
		FTransform3f Loaded;
		FMemoryReader Reader(Bytes);
		Reader << Loaded;
		TestTrue(TEXT("save header's transform type keeps the mirror"), FTransform(Loaded).GetScale3D().Equals(FVector(1, -1, 1), 0.001));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMirrorActorTest, "MirrorBuild.Actor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMirrorActorTest::RunTest(const FString&)
{
	FTestWorld Test;
	AActor* Actor = Test.World->SpawnActor<AActor>();
	USceneComponent* Root = NewObject<USceneComponent>(Actor);
	Actor->SetRootComponent(Root);
	Root->RegisterComponent();

	UBoxComponent* Box = NewObject<UBoxComponent>(Actor);
	Box->SetupAttachment(Root);
	Box->SetBoxExtent(FVector(100, 200, 50));
	Box->RegisterComponent();

	UStaticMeshComponent* Mesh = NewObject<UStaticMeshComponent>(Actor);
	Mesh->SetupAttachment(Root);
	Mesh->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
	Mesh->SetRelativeLocation(FVector(0, 600, 0));
	Mesh->SetCollisionProfileName(TEXT("BlockAll"));
	Mesh->RegisterComponent();

	MirrorCore::SetActorMirrored(Actor, true);
	TestTrue(TEXT("mirrored"), MirrorCore::IsMirrored(Actor));
	Actor->SetActorLocationAndRotation(FVector(1000, 2000, 300), FRotator(0, 90, 0));
	TestTrue(TEXT("moving and rotating keeps the mirror"), MirrorCore::IsMirrored(Actor));
	TestTrue(TEXT("uncorrected box extent is negative"), Box->GetScaledBoxExtent().Y < 0);
	Box->SetRelativeScale3D(FVector(1, -1, 1));
	TestTrue(TEXT("corrected box extent is positive"), Box->GetScaledBoxExtent().Equals(FVector(100, 200, 50), 0.01));

	const FVector Expected = Mesh->GetComponentLocation();
	const FVector Other = Actor->GetActorLocation() * 2.0 - Expected;
	FHitResult Hit;
	TestTrue(TEXT("collision is on the mirrored side"), Mesh->LineTraceComponent(
		Hit, Expected + FVector(0, 0, 500), Expected - FVector(0, 0, 500), FCollisionQueryParams()));
	TestFalse(TEXT("no collision left on the old side"), Mesh->LineTraceComponent(
		Hit, Other + FVector(0, 0, 500), Other - FVector(0, 0, 500), FCollisionQueryParams()));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMirrorMessageTest, "MirrorBuild.Message",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMirrorMessageTest::RunTest(const FString&)
{
	UClass* Refinery = LoadClass<AActor>(nullptr, RefineryPath);
	if (!TestNotNull(TEXT("Refinery class"), Refinery))
	{
		return false;
	}
	FTestWorld Test;
	AFGBuildableHologram* Sender = Test.World->SpawnActor<AFGBuildableHologram>();
	AFGBuildableHologram* Receiver = Test.World->SpawnActor<AFGBuildableHologram>();
	if (!TestNotNull(TEXT("sender"), Sender) || !TestNotNull(TEXT("receiver"), Receiver))
	{
		return false;
	}
	const FProperty* BuildClass = AFGHologram::StaticClass()->FindPropertyByName(TEXT("mBuildClass"));
	for (AFGBuildableHologram* Hologram : {Sender, Receiver})
	{
		// The stubbed constructor may not make a root.
		if (!Hologram->GetRootComponent())
		{
			USceneComponent* Root = NewObject<USceneComponent>(Hologram);
			Hologram->SetRootComponent(Root);
			Root->RegisterComponent();
		}
		*BuildClass->ContainerPtrToValuePtr<TSubclassOf<AActor>>(Hologram) = Refinery;
	}

	const FMemberFunctionPointer Pointer = ConvertFunctionPointer(&AFGHologram::SerializeConstructMessage);
	uint8* HookSelf = reinterpret_cast<uint8*>(Sender) + Pointer.ThisAdjustment;
	TestTrue(TEXT("the hook gets an adjusted pointer"), Pointer.ThisAdjustment != 0);
	if (!TestTrue(TEXT("and it is the interface pointer"),
		reinterpret_cast<IFGConstructionMessageInterface*>(HookSelf) == static_cast<IFGConstructionMessageInterface*>(Sender)))
	{
		return false;
	}

	for (const bool bMirrored : {true, false})
	{
		MirrorCore::SetHologramMirrored(Sender, bMirrored);
		FBitWriter Writer(0, true);
		MirrorCore::SerializeMirrorFlag(reinterpret_cast<IFGConstructionMessageInterface*>(HookSelf), Writer);
		TestEqual(TEXT("one bit written"), Writer.GetNumBits(), int64(1));
		FBitReader Reader(Writer.GetData(), Writer.GetNumBits());
		MirrorCore::SerializeMirrorFlag(static_cast<IFGConstructionMessageInterface*>(Receiver), Reader);
		TestEqual(TEXT("receiver matches sender"), MirrorCore::IsMirrored(Receiver), bMirrored);
		TestTrue(TEXT("one bit read"), Reader.AtEnd() && !Reader.IsError());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMirrorRefineryTest, "MirrorBuild.Refinery",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMirrorRefineryTest::RunTest(const FString&)
{
	UClass* Refinery = LoadClass<AActor>(nullptr, RefineryPath);
	if (!TestNotNull(TEXT("Refinery class"), Refinery))
	{
		return false;
	}
	for (const TCHAR* Name : {TEXT("mHologramClass"), TEXT("mClearanceData")})
	{
		if (const FProperty* Property = Refinery->FindPropertyByName(Name))
		{
			FString Value;
			Property->ExportTextItem_InContainer(Value, Refinery->GetDefaultObject(), nullptr, nullptr, PPF_None);
			AddInfo(FString::Printf(TEXT("%s = %s"), Name, *Value));
		}
	}

	FTestWorld Test;
	const FTransform Place(FRotator(0, 90, 0), FVector(5000, 5000, 100));
	const FTransform MirrorPlace(FRotator(0, 270, 0), Place.GetLocation(), FVector(1, -1, 1));
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AActor* Plain = Test.World->SpawnActor<AActor>(Refinery, Place, Params);
	AActor* Mirror = Test.World->SpawnActor<AActor>(Refinery, MirrorPlace, Params);
	if (!TestNotNull(TEXT("plain"), Plain) || !TestNotNull(TEXT("mirrored"), Mirror))
	{
		return false;
	}
	TestTrue(TEXT("spawn keeps the mirror"), MirrorCore::IsMirrored(Mirror));

	TInlineComponentArray<USceneComponent*> PlainParts(Plain);
	TInlineComponentArray<USceneComponent*> MirrorParts(Mirror);
	int32 Ports = 0;
	for (const USceneComponent* A : PlainParts)
	{
		if (!A->IsA<UFGFactoryConnectionComponent>() && !A->IsA<UFGPipeConnectionComponent>())
		{
			continue;
		}
		++Ports;
		const FVector Where = Place.InverseTransformPosition(A->GetComponentLocation());
		const FVector Facing = Place.InverseTransformVectorNoScale(A->GetComponentRotation().Vector());
		AddInfo(FString::Printf(TEXT("%s at %s facing %s"), *A->GetName(), *Where.ToCompactString(), *Facing.ToCompactString()));

		bool bFound = false;
		for (const USceneComponent* B : MirrorParts)
		{
			if (B->GetFName() != A->GetFName())
			{
				continue;
			}
			bFound = true;
			TestTrue(A->GetName() + TEXT(" position mirrored"), B->GetComponentLocation().Equals(
				Place.TransformPosition(FVector(-Where.X, Where.Y, Where.Z)), 0.1));
			TestTrue(A->GetName() + TEXT(" normal mirrored"), B->GetComponentRotation().Vector().Equals(
				Place.TransformVectorNoScale(FVector(-Facing.X, Facing.Y, Facing.Z)), 0.001));
		}
		TestTrue(A->GetName() + TEXT(" exists on both"), bFound);
	}
	TestTrue(TEXT("Refinery asset has ports to test"), Ports > 0);
	AddInfo(FString::Printf(TEXT("%d ports checked"), Ports));
	return true;
}

#endif
