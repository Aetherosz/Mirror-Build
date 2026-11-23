#include "CoreMinimal.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "FGBuildEffectActor.h"
#include "Materials/MaterialInterface.h"
#include "Misc/AutomationTest.h"
#include "MirrorLabels.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMirrorLabelsTest, "MirrorBuild.Labels",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMirrorLabelsTest::RunTest(const FString&)
{
	const int32 Changed = MirrorLabels::Apply();
	AddInfo(FString::Printf(TEXT("%d mesh slots changed"), Changed));
	for (const TCHAR* Name : {TEXT("Assembler"), TEXT("Biomass"), TEXT("Coal"), TEXT("Constructor"),
		TEXT("Foundry"), TEXT("Logo"), TEXT("Packager"), TEXT("Smelter")})
	{
		UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr,
			*FString::Printf(TEXT("/MirrorBuild/Materials/M_Labels_%s.M_Labels_%s"), Name, Name));
		if (!TestNotNull(FString(Name) + TEXT(" material"), Material))
		{
			continue;
		}
		UFGBuildEffectMaterialSwapAssetData* Data = Cast<UFGBuildEffectMaterialSwapAssetData>(
			Material->GetAssetUserDataOfClass(UFGBuildEffectMaterialSwapAssetData::StaticClass()));
		TestTrue(FString(Name) + TEXT(" has a build effect"), Data && Data->mBuildEffect);
	}
	return true;
}

#endif