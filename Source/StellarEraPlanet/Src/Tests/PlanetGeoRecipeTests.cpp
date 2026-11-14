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
	const FPlanetRecipeConfig Default = Recipe->ToConfig(40000.0);
	TestEqual(TEXT("半径来自调用方"), Default.Radius, 40000.0);

	Recipe->Seed = 77; Recipe->FaceRes = 512;
	Recipe->CrustRatio = 0.2f; Recipe->UpperMantleRatio = 0.3f; Recipe->TransitionRatio = 0.35f;
	Recipe->MantleOuterRatio = 0.5f; Recipe->OuterCoreRatio = 0.75f; Recipe->MohoWobbleRatio = 0.02f; Recipe->WobbleOctaves = 4;
	const FPlanetRecipeConfig C = Recipe->ToConfig(1234.0);
	TestEqual(TEXT("Radius"), C.Radius, 1234.0);
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

	// 阶段 2 暴露项逐项落到 Config；用互不相同的量级，赋值错位会在这里暴露。
	UPlanetRecipe* R2 = NewObject<UPlanetRecipe>();
	const FPlanetContinentParams KD;
	const FPlanetRecipeConfig D2 = R2->ToConfig(40000.0);
	TestEqual(TEXT("默认 PlateCount 7"), D2.Plates.PlateCount, 7);
	TestEqual(TEXT("默认 FrameDegree"), D2.Continents.FrameDegree, KD.FrameDegree);
	TestEqual(TEXT("默认 LandFrac"), D2.Continents.LandFrac, KD.LandFrac);
	TestEqual(TEXT("默认 CoastRoughness"), D2.Continents.CoastRoughness, KD.CoastRoughness);
	TestEqual(TEXT("默认 ShelfFrac"), D2.Continents.ShelfFrac, KD.ShelfFrac);
	TestEqual(TEXT("默认 LandHeightRatio"), D2.Continents.LandHeightRatio, KD.LandHeightRatio);
	TestEqual(TEXT("默认 OceanDepthRatio"), D2.Continents.OceanDepthRatio, KD.OceanDepthRatio);
	TestEqual(TEXT("未暴露 DetailFrequency 走默认"), D2.Continents.DetailFrequency, KD.DetailFrequency);

	R2->PlateCount = 11; R2->FrameDegree = 6; R2->LandFrac = 0.42f; R2->CoastRoughness = 0.8f;
	R2->ShelfFrac = 0.21f; R2->LandHeightRatio = 0.0077f; R2->OceanDepthRatio = 0.013f; R2->MountainHeightRatio = 0.07f; R2->ErosionIterations = 9;
	const FPlanetRecipeConfig C2 = R2->ToConfig(40000.0);
	TestEqual(TEXT("PlateCount"), C2.Plates.PlateCount, 11);
	TestEqual(TEXT("FrameDegree"), C2.Continents.FrameDegree, 6);
	TestEqual(TEXT("LandFrac"), C2.Continents.LandFrac, 0.42f);
	TestEqual(TEXT("CoastRoughness"), C2.Continents.CoastRoughness, 0.8f);
	TestEqual(TEXT("ShelfFrac"), C2.Continents.ShelfFrac, 0.21f);
	TestEqual(TEXT("LandHeightRatio"), C2.Continents.LandHeightRatio, 0.0077f);
	TestEqual(TEXT("OceanDepthRatio"), C2.Continents.OceanDepthRatio, 0.013f);
	TestEqual(TEXT("MountainHeightRatio"), C2.Terrain.MountainHeightRatio, 0.07f);
	TestEqual(TEXT("ErosionIterations"), C2.Terrain.ErosionIterations, 9);

	// 每项都必须进 Hash()：漏一项则改它快照哈希不变、存档校验失效。
	const uint64 Base = C2.Hash();
	FPlanetRecipeConfig Probe;
	Probe = C2; Probe.Plates.PlateCount = 12;               TestTrue(TEXT("PlateCount 进哈希"), Probe.Hash() != Base);
	Probe = C2; Probe.Continents.FrameDegree = 7;           TestTrue(TEXT("FrameDegree 进哈希"), Probe.Hash() != Base);
	Probe = C2; Probe.Continents.LandFrac = 0.43f;          TestTrue(TEXT("LandFrac 进哈希"), Probe.Hash() != Base);
	Probe = C2; Probe.Continents.CoastRoughness = 0.81f;    TestTrue(TEXT("CoastRoughness 进哈希"), Probe.Hash() != Base);
	Probe = C2; Probe.Continents.ShelfFrac = 0.22f;         TestTrue(TEXT("ShelfFrac 进哈希"), Probe.Hash() != Base);
	Probe = C2; Probe.Continents.LandHeightRatio = 0.0078f;  TestTrue(TEXT("LandHeightRatio 进哈希"), Probe.Hash() != Base);
	Probe = C2; Probe.Continents.OceanDepthRatio = 0.0131f;  TestTrue(TEXT("OceanDepthRatio 进哈希"), Probe.Hash() != Base);
	Probe = C2; Probe.Plates.WarpAmp = 0.51f;              TestTrue(TEXT("BoundaryWarp 进哈希"), Probe.Hash() != Base);
	Probe = C2; Probe.Plates.DetailWarpAmp = 0.09f;        TestTrue(TEXT("BoundaryDetail 进哈希"), Probe.Hash() != Base);
	Probe = C2; Probe.Terrain.MountainHeightRatio = 0.071f;  TestTrue(TEXT("MountainHeightRatio 进哈希"), Probe.Hash() != Base);
	Probe = C2; Probe.Terrain.ErosionIterations = 10;      TestTrue(TEXT("ErosionIterations 进哈希"), Probe.Hash() != Base);
	Probe = C2; Probe.Continents.DetailOctaves = 6;         TestTrue(TEXT("DetailOctaves 进哈希"), Probe.Hash() != Base);
	Probe = C2; Probe.Continents.AbyssRatio = 0.8f;         TestTrue(TEXT("AbyssRatio 进哈希"), Probe.Hash() != Base);
	return true;
}

#endif
