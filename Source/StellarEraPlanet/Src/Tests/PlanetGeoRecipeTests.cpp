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

	// 阶段 2 参数必须进哈希，否则改了大陆形状快照哈希不变、存档校验失效。
	FPlanetRecipeConfig PlateA, PlateB = PlateA;
	PlateB.Plates.LandTableVoxels = PlateA.Plates.LandTableVoxels + 10.f;
	TestTrue(TEXT("陆台高进哈希"), PlateA.Hash() != PlateB.Hash());
	FPlanetRecipeConfig PlateC = PlateA;
	PlateC.Plates.AlphaMax = 3.f;
	TestTrue(TEXT("α 搜索区间进哈希"), PlateA.Hash() != PlateC.Hash());
	TestEqual(TEXT("默认板块数 7"), PlateA.Plates.PlateCount, 7);

	// 阶段 2 六项要逐项落到 C.Plates；用互不相同的量级，赋值错位会在这里暴露。
	UPlanetRecipe* PlateRecipe = NewObject<UPlanetRecipe>();
	const FPlanetPlateParams PlateDefaults;
	const FPlanetRecipeConfig PlateDefaultCfg = PlateRecipe->ToConfig();
	TestEqual(TEXT("默认 PlateCount"), PlateDefaultCfg.Plates.PlateCount, PlateDefaults.PlateCount);
	TestEqual(TEXT("默认 TargetLandFrac"), PlateDefaultCfg.Plates.TargetLandFrac, PlateDefaults.TargetLandFrac);
	TestEqual(TEXT("默认 LandTableVoxels"), PlateDefaultCfg.Plates.LandTableVoxels, PlateDefaults.LandTableVoxels);
	TestEqual(TEXT("默认 OceanBasinVoxels"), PlateDefaultCfg.Plates.OceanBasinVoxels, PlateDefaults.OceanBasinVoxels);
	TestEqual(TEXT("默认 StyleAmpVoxels"), PlateDefaultCfg.Plates.StyleAmpVoxels, PlateDefaults.StyleAmpVoxels);
	TestEqual(TEXT("默认 SlopeWidthVoxels"), PlateDefaultCfg.Plates.SlopeWidthVoxels, PlateDefaults.SlopeWidthVoxels);
	TestEqual(TEXT("未暴露 WarpAmp 走默认"), PlateDefaultCfg.Plates.WarpAmp, PlateDefaults.WarpAmp);
	TestEqual(TEXT("未暴露 SigmaRatio 走默认"), PlateDefaultCfg.Plates.SigmaRatio, PlateDefaults.SigmaRatio);

	PlateRecipe->PlateCount = 11;
	PlateRecipe->TargetLandFrac = 0.42f;
	PlateRecipe->LandTableVoxels = 310.f;
	PlateRecipe->OceanBasinVoxels = 520.f;
	PlateRecipe->StyleAmpVoxels = 95.f;
	PlateRecipe->SlopeWidthVoxels = 175.f;
	const FPlanetRecipeConfig PlateCfg = PlateRecipe->ToConfig();
	TestEqual(TEXT("PlateCount"), PlateCfg.Plates.PlateCount, 11);
	TestEqual(TEXT("TargetLandFrac"), PlateCfg.Plates.TargetLandFrac, 0.42f);
	TestEqual(TEXT("LandTableVoxels"), PlateCfg.Plates.LandTableVoxels, 310.f);
	TestEqual(TEXT("OceanBasinVoxels"), PlateCfg.Plates.OceanBasinVoxels, 520.f);
	TestEqual(TEXT("StyleAmpVoxels"), PlateCfg.Plates.StyleAmpVoxels, 95.f);
	TestEqual(TEXT("SlopeWidthVoxels"), PlateCfg.Plates.SlopeWidthVoxels, 175.f);

	// 六项都必须进 Hash()：漏一项则改它快照哈希不变、存档校验失效。
	const uint64 BaseHash = PlateCfg.Hash();
	FPlanetRecipeConfig HashProbe = PlateCfg;
	HashProbe.Plates.PlateCount = 12;
	TestTrue(TEXT("PlateCount 进哈希"), HashProbe.Hash() != BaseHash);
	HashProbe = PlateCfg; HashProbe.Plates.TargetLandFrac = 0.43f;
	TestTrue(TEXT("TargetLandFrac 进哈希"), HashProbe.Hash() != BaseHash);
	HashProbe = PlateCfg; HashProbe.Plates.LandTableVoxels = 311.f;
	TestTrue(TEXT("LandTableVoxels 进哈希"), HashProbe.Hash() != BaseHash);
	HashProbe = PlateCfg; HashProbe.Plates.OceanBasinVoxels = 521.f;
	TestTrue(TEXT("OceanBasinVoxels 进哈希"), HashProbe.Hash() != BaseHash);
	HashProbe = PlateCfg; HashProbe.Plates.StyleAmpVoxels = 96.f;
	TestTrue(TEXT("StyleAmpVoxels 进哈希"), HashProbe.Hash() != BaseHash);
	HashProbe = PlateCfg; HashProbe.Plates.SlopeWidthVoxels = 176.f;
	TestTrue(TEXT("SlopeWidthVoxels 进哈希"), HashProbe.Hash() != BaseHash);
	return true;
}

#endif
