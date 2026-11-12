// 资产 → POD 是烘焙期唯一碰 UObject 的地方：字段漏拷就是「面板上改了没效果」。
#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "PlanetRecipe.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGeoRecipeTest, "StellarEra.Planet.Geo.Recipe",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGeoRecipeTest::RunTest(const FString& Parameters)
{
	UPlanetRecipe* Recipe = NewObject<UPlanetRecipe>();
	const FPlanetRecipeConfig Default = Recipe->ToConfig();
	TestEqual(TEXT("默认半径 40000"), Default.Radius, 40000.0);
	TestEqual(TEXT("默认体素 50 cm"), Default.VoxelSizeCm, 50.f);

	Recipe->Radius = 1234.f; Recipe->VoxelSizeCm = 75.f; Recipe->Seed = 77; Recipe->FaceRes = 512;
	Recipe->CrustRatio = 0.2f; Recipe->UpperMantleRatio = 0.3f; Recipe->TransitionRatio = 0.35f;
	Recipe->MantleOuterRatio = 0.5f; Recipe->OuterCoreRatio = 0.75f; Recipe->MohoWobbleRatio = 0.02f; Recipe->WobbleOctaves = 4;
	const FPlanetRecipeConfig C = Recipe->ToConfig();
	TestEqual(TEXT("Radius"), C.Radius, 1234.0);
	TestEqual(TEXT("VoxelSizeCm"), C.VoxelSizeCm, 75.f);
	TestEqual(TEXT("Seed"), C.Seed, uint64(77));
	TestEqual(TEXT("FaceRes"), C.FaceRes, 512);
	TestEqual(TEXT("CrustRatio"), C.Sphere.CrustRatio, 0.2f);
	TestEqual(TEXT("UpperMantleRatio"), C.Sphere.UpperMantleRatio, 0.3f);
	TestEqual(TEXT("TransitionRatio"), C.Sphere.TransitionRatio, 0.35f);
	TestEqual(TEXT("MantleOuterRatio"), C.Sphere.MantleOuterRatio, 0.5f);
	TestEqual(TEXT("OuterCoreRatio"), C.Sphere.OuterCoreRatio, 0.75f);
	TestEqual(TEXT("MohoWobbleRatio"), C.Sphere.MohoWobbleRatio, 0.02f);
	TestEqual(TEXT("WobbleOctaves"), C.Sphere.WobbleOctaves, 4);
	TestTrue(TEXT("改了字段配方哈希就变"), C.Hash() != Default.Hash());
	return true;
}

#endif
