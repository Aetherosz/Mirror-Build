#include "MirrorLabels.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "FGBuildEffectActor.h"
#include "Materials/MaterialInterface.h"
#include "MirrorCore.h"

namespace
{
	struct FLabelledMesh
	{
		const TCHAR* Mesh;
		const TCHAR* Material;
		/** Only where the building's own component sets the decal material over the mesh's. */
		const TCHAR* Building = nullptr;
	};

	/*
	 * The vanilla meshes with writing on them. The materials are made by Tools/make_label_materials.py.
	 * They look like the game's decal material until the instance being drawn is mirrored,
	 * so there's nothing to set per building and nothing is saved.
	 */
	const FLabelledMesh LabelledMeshes[] = {
		{TEXT("AssemblerMk1/Mesh/AssemblerMk1_static"), TEXT("M_Labels_Assembler")},
		{TEXT("ConstructorMk1/Mesh/ConstructorMk1_static"), TEXT("M_Labels_Constructor")},
		{TEXT("FoundryMk1/Mesh/FoundryMk1_static"), TEXT("M_Labels_Foundry")},
		{TEXT("GeneratorBiomass/Mesh/GeneratorBiomass_static"), TEXT("M_Labels_Biomass")},
		{TEXT("GeneratorCoal/Mesh/GeneratorCoal_static"), TEXT("M_Labels_Coal")},
		{TEXT("Packager/Mesh/SM_Packager_01"), TEXT("M_Labels_Packager")},
		{TEXT("SmelterMk1/Mesh/SmelterMk1_static"), TEXT("M_Labels_Smelter"), TEXT("SmelterMk1/Build_SmelterMk1")},
		{TEXT("Blender/Mesh/SM_Blender_01"), TEXT("M_Labels_Logo")},
		{TEXT("Converter/Mesh/SM_Converter_01"), TEXT("M_Labels_Logo")},
		{TEXT("MinerMK1/Mesh/MinerMk1_static"), TEXT("M_Labels_Logo")},
		{TEXT("MinerMk2/Mesh/MinerMk2_static"), TEXT("M_Labels_Logo")},
		{TEXT("MinerMk3/Mesh/Miner_Mk3_Static"), TEXT("M_Labels_Logo")},
		{TEXT("OilRefinery/Mesh/SM_OilRefinery_01"), TEXT("M_Labels_Logo")},
		{TEXT("QuantumEncoder/Mesh/SM_QuantumEncoder_01"), TEXT("M_Labels_Logo")},
		{TEXT("TruckStation/Mesh/Truckstation_static"), TEXT("M_Labels_Logo")},
		{TEXT("TruckStation/Mesh/SM_TruckStation_Fluid_01"), TEXT("M_Labels_Logo")},
		{TEXT("WaterPump/Mesh/SM_WaterPump_01"), TEXT("M_Labels_Logo")},
	};

	const FName DecalMaterial(TEXT("DecalColor_Masked"));

	FString GamePath(const TCHAR* Short)
	{
		const FString Package = FString(TEXT("/Game/FactoryGame/Buildable/Factory/")) + Short;
		return Package + TEXT(".") + FPaths::GetBaseFilename(Package);
	}

	int32 ReplaceOverrides(const TCHAR* Building, const UStaticMesh* Mesh, UMaterialInterface* Material)
	{
		UBlueprintGeneratedClass* Class = LoadObject<UBlueprintGeneratedClass>(nullptr, *(GamePath(Building) + TEXT("_C")));
		if (!Class || !Class->SimpleConstructionScript)
		{
			return 0;
		}
		Class->AddToRoot();
		int32 Changed = 0;
		for (const USCS_Node* Node : Class->SimpleConstructionScript->GetAllNodes())
		{
			UStaticMeshComponent* Component = Node ? Cast<UStaticMeshComponent>(Node->GetActualComponentTemplate(Class)) : nullptr;
			if (!Component || Component->GetStaticMesh() != Mesh)
			{
				continue;
			}
			for (auto& Override : Component->OverrideMaterials)
			{
				if (Override && Override->GetFName() == DecalMaterial)
				{
					Override = Material;
					++Changed;
				}
			}
		}
		return Changed;
	}

	/** The build and dismantle animation looks for its variant of a material in the material's user data. */
	void AddBuildEffect(UMaterialInterface* Material)
	{
		if (Material->GetAssetUserDataOfClass(UFGBuildEffectMaterialSwapAssetData::StaticClass()))
		{
			return;
		}
		UMaterialInterface* Effect = LoadObject<UMaterialInterface>(nullptr,
			TEXT("/Game/FactoryGame/Buildable/-Shared/Material/DecalColor_Masked_BuildEffect.DecalColor_Masked_BuildEffect"));
		if (Effect)
		{
			UFGBuildEffectMaterialSwapAssetData* Data = NewObject<UFGBuildEffectMaterialSwapAssetData>(Material, NAME_None, RF_Transient);
			Data->mBuildEffect = Effect;
			Material->AddAssetUserData(Data);
		}
	}

	void ApplyInFirstGameWorld(UWorld* World, const UWorld::InitializationValues)
	{
		static bool bDone = false;
		if (bDone || !World || !World->IsGameWorld())
		{
			return;
		}
		bDone = true;
		MirrorLabels::Apply();
	}
}

int32 MirrorLabels::Apply()
{
	int32 Total = 0;
	for (const FLabelledMesh& Entry : LabelledMeshes)
	{
		UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *GamePath(Entry.Mesh));
		UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr,
			*FString::Printf(TEXT("/MirrorBuild/Materials/%s.%s"), Entry.Material, Entry.Material));
		if (!Mesh || !Material)
		{
			UE_LOG(LogMirrorBuild, Warning, TEXT("Readable text: %s not found, left as it is"), Mesh ? Entry.Material : Entry.Mesh);
			continue;
		}
		// Unloading would bring the game's material back.
		Mesh->AddToRoot();
		Material->AddToRoot();
		AddBuildEffect(Material);

		int32 Changed = 0;
		for (FStaticMaterial& Slot : Mesh->GetStaticMaterials())
		{
			if (Slot.MaterialInterface && Slot.MaterialInterface->GetFName() == DecalMaterial)
			{
				Slot.MaterialInterface = Material;
				++Changed;
			}
		}
		if (Entry.Building)
		{
			Changed += ReplaceOverrides(Entry.Building, Mesh, Material);
		}
		UE_LOG(LogMirrorBuild, Display, TEXT("Readable text: %s slots=%d material=%s"), *Mesh->GetName(), Changed, Entry.Material);
		Total += Changed;
	}
	return Total;
}

void MirrorLabels::Register()
{
	// A dedicated server draws nothing.
	if (!IsRunningDedicatedServer())
	{
		FWorldDelegates::OnPostWorldInitialization.AddStatic(&ApplyInFirstGameWorld);
	}
}
