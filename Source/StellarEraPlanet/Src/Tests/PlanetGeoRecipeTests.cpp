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

	// 阶段 4 的 26 项默认值与 FPlanetVolcanoParams 逐项对齐：ToConfig 里 RHS 接错字段照样能编译，
	// 默认值不同才会露馅（5 开关 + 3 int + 18 float）。
	{
		UPlanetRecipe* Rv = NewObject<UPlanetRecipe>();
		const FPlanetVolcanoParams VD;
		const FPlanetVolcanoParams VC = Rv->ToConfig(40000.0).Volcano;
		TestEqual(TEXT("默认 bArcVolcanoes"), int32(VC.bArcVolcanoes), int32(VD.bArcVolcanoes));
		TestEqual(TEXT("默认 bHotspots"), int32(VC.bHotspots), int32(VD.bHotspots));
		TestEqual(TEXT("默认 bFloodBasalt"), int32(VC.bFloodBasalt), int32(VD.bFloodBasalt));
		TestEqual(TEXT("默认 bCalderas"), int32(VC.bCalderas), int32(VD.bCalderas));
		TestEqual(TEXT("默认 bCinderCones"), int32(VC.bCinderCones), int32(VD.bCinderCones));
		TestEqual(TEXT("默认 HotspotCount"), VC.HotspotCount, VD.HotspotCount);
		TestEqual(TEXT("默认 HotspotChainLength"), VC.HotspotChainLength, VD.HotspotChainLength);
		TestEqual(TEXT("默认 CinderPerVolcano"), VC.CinderPerVolcano, VD.CinderPerVolcano);
		TestEqual(TEXT("默认 StratoBaseRatio"), VC.StratoBaseRatio, VD.StratoBaseRatio, 1e-6f);
		TestEqual(TEXT("默认 StratoHeightRatio"), VC.StratoHeightRatio, VD.StratoHeightRatio, 1e-6f);
		TestEqual(TEXT("默认 StratoCraterRatio"), VC.StratoCraterRatio, VD.StratoCraterRatio, 1e-6f);
		TestEqual(TEXT("默认 CraterDepthRatio"), VC.CraterDepthRatio, VD.CraterDepthRatio, 1e-6f);
		TestEqual(TEXT("默认 ArcSpacingRatio"), VC.ArcSpacingRatio, VD.ArcSpacingRatio, 1e-6f);
		TestEqual(TEXT("默认 ArcOffsetRatio"), VC.ArcOffsetRatio, VD.ArcOffsetRatio, 1e-6f);
		TestEqual(TEXT("默认 ArcActiveFraction"), VC.ArcActiveFraction, VD.ArcActiveFraction, 1e-6f);
		TestEqual(TEXT("默认 ShieldBaseRatio"), VC.ShieldBaseRatio, VD.ShieldBaseRatio, 1e-6f);
		TestEqual(TEXT("默认 ShieldHeightRatio"), VC.ShieldHeightRatio, VD.ShieldHeightRatio, 1e-6f);
		TestEqual(TEXT("默认 HotspotSpacingRatio"), VC.HotspotSpacingRatio, VD.HotspotSpacingRatio, 1e-6f);
		TestEqual(TEXT("默认 FloodBasaltChance"), VC.FloodBasaltChance, VD.FloodBasaltChance, 1e-6f);
		TestEqual(TEXT("默认 FloodBasaltSizeRatio"), VC.FloodBasaltSizeRatio, VD.FloodBasaltSizeRatio, 1e-6f);
		TestEqual(TEXT("默认 FloodBasaltHeightRatio"), VC.FloodBasaltHeightRatio, VD.FloodBasaltHeightRatio, 1e-6f);
		TestEqual(TEXT("默认 CalderaFraction"), VC.CalderaFraction, VD.CalderaFraction, 1e-6f);
		TestEqual(TEXT("默认 CalderaRadiusRatio"), VC.CalderaRadiusRatio, VD.CalderaRadiusRatio, 1e-6f);
		TestEqual(TEXT("默认 CalderaDepthRatio"), VC.CalderaDepthRatio, VD.CalderaDepthRatio, 1e-6f);
		TestEqual(TEXT("默认 CinderBaseRatio"), VC.CinderBaseRatio, VD.CinderBaseRatio, 1e-6f);
		TestEqual(TEXT("默认 CinderHeightRatio"), VC.CinderHeightRatio, VD.CinderHeightRatio, 1e-6f);
	}

	{
		UPlanetRecipe* Rp = NewObject<UPlanetRecipe>();
		Rp->ArcSpacingRatio = 0.05f; Rp->bHotspots = false; Rp->HotspotChainLength = 3;
		const FPlanetRecipeConfig Cv = Rp->ToConfig(40000.0);
		TestEqual(TEXT("ArcSpacingRatio 透传"), Cv.Volcano.ArcSpacingRatio, 0.05f);
		TestFalse(TEXT("bHotspots 透传"), Cv.Volcano.bHotspots);
		TestEqual(TEXT("HotspotChainLength 透传"), Cv.Volcano.HotspotChainLength, 3);
	}

	// 阶段 4b 的 35 项默认值与 FPlanetIntrusionParams 逐项对齐：ToConfig 里 RHS 接错字段照样能编译，
	// 默认值不同才会露馅（7 开关 + 4 int + 24 float）。
	{
		UPlanetRecipe* Ri = NewObject<UPlanetRecipe>();
		const FPlanetIntrusionParams ID;
		const FPlanetIntrusionParams IC = Ri->ToConfig(40000.0).Intrusion;
		TestEqual(TEXT("默认 bBatholiths"), int32(IC.bBatholiths), int32(ID.bBatholiths));
		TestEqual(TEXT("默认 bStocks"), int32(IC.bStocks), int32(ID.bStocks));
		TestEqual(TEXT("默认 bDikes"), int32(IC.bDikes), int32(ID.bDikes));
		TestEqual(TEXT("默认 bSills"), int32(IC.bSills), int32(ID.bSills));
		TestEqual(TEXT("默认 bLayeredIntrusions"), int32(IC.bLayeredIntrusions), int32(ID.bLayeredIntrusions));
		TestEqual(TEXT("默认 bKimberlites"), int32(IC.bKimberlites), int32(ID.bKimberlites));
		TestEqual(TEXT("默认 bAlteration"), int32(IC.bAlteration), int32(ID.bAlteration));
		TestEqual(TEXT("默认 BatholithMinVolcanoes"), IC.BatholithMinVolcanoes, ID.BatholithMinVolcanoes);
		TestEqual(TEXT("默认 DikePerRift"), IC.DikePerRift, ID.DikePerRift);
		TestEqual(TEXT("默认 LayeredCount"), IC.LayeredCount, ID.LayeredCount);
		TestEqual(TEXT("默认 KimberliteMax"), IC.KimberliteMax, ID.KimberliteMax);
		TestEqual(TEXT("默认 BatholithLongRatio"), IC.BatholithLongRatio, ID.BatholithLongRatio, 1e-6f);
		TestEqual(TEXT("默认 BatholithShortRatio"), IC.BatholithShortRatio, ID.BatholithShortRatio, 1e-6f);
		TestEqual(TEXT("默认 BatholithThickRatio"), IC.BatholithThickRatio, ID.BatholithThickRatio, 1e-6f);
		TestEqual(TEXT("默认 BatholithTopDepthRatio"), IC.BatholithTopDepthRatio, ID.BatholithTopDepthRatio, 1e-6f);
		TestEqual(TEXT("默认 StockRadiusRatio"), IC.StockRadiusRatio, ID.StockRadiusRatio, 1e-6f);
		TestEqual(TEXT("默认 StockTopDepthRatio"), IC.StockTopDepthRatio, ID.StockTopDepthRatio, 1e-6f);
		TestEqual(TEXT("默认 DikeThicknessRatio"), IC.DikeThicknessRatio, ID.DikeThicknessRatio, 1e-6f);
		TestEqual(TEXT("默认 DikeLengthRatio"), IC.DikeLengthRatio, ID.DikeLengthRatio, 1e-6f);
		TestEqual(TEXT("默认 DikeTopDepthRatio"), IC.DikeTopDepthRatio, ID.DikeTopDepthRatio, 1e-6f);
		TestEqual(TEXT("默认 SillRadiusRatio"), IC.SillRadiusRatio, ID.SillRadiusRatio, 1e-6f);
		TestEqual(TEXT("默认 SillThicknessRatio"), IC.SillThicknessRatio, ID.SillThicknessRatio, 1e-6f);
		TestEqual(TEXT("默认 SillTopDepthRatio"), IC.SillTopDepthRatio, ID.SillTopDepthRatio, 1e-6f);
		TestEqual(TEXT("默认 CratonSillDensity"), IC.CratonSillDensity, ID.CratonSillDensity, 1e-6f);
		TestEqual(TEXT("默认 LayeredRadiusRatio"), IC.LayeredRadiusRatio, ID.LayeredRadiusRatio, 1e-6f);
		TestEqual(TEXT("默认 LayeredThicknessRatio"), IC.LayeredThicknessRatio, ID.LayeredThicknessRatio, 1e-6f);
		TestEqual(TEXT("默认 LayeredGapRatio"), IC.LayeredGapRatio, ID.LayeredGapRatio, 1e-6f);
		TestEqual(TEXT("默认 LayeredTopDepthRatio"), IC.LayeredTopDepthRatio, ID.LayeredTopDepthRatio, 1e-6f);
		TestEqual(TEXT("默认 KimberliteRadiusRatio"), IC.KimberliteRadiusRatio, ID.KimberliteRadiusRatio, 1e-6f);
		TestEqual(TEXT("默认 KimberliteHalfHeightRatio"), IC.KimberliteHalfHeightRatio, ID.KimberliteHalfHeightRatio, 1e-6f);
		TestEqual(TEXT("默认 KimberliteTopDepthRatio"), IC.KimberliteTopDepthRatio, ID.KimberliteTopDepthRatio, 1e-6f);
		TestEqual(TEXT("默认 AlterHaloScale"), IC.AlterHaloScale, ID.AlterHaloScale, 1e-6f);
		TestEqual(TEXT("默认 AlterKeratiticBand"), IC.AlterKeratiticBand, ID.AlterKeratiticBand, 1e-6f);
		TestEqual(TEXT("默认 AlterSericiticBand"), IC.AlterSericiticBand, ID.AlterSericiticBand, 1e-6f);
		TestEqual(TEXT("默认 AlterArgillicBand"), IC.AlterArgillicBand, ID.AlterArgillicBand, 1e-6f);
	}

	// 非默认值逐项透传：默认值对比抓不到「RHS 接成同类型字段」，这里每项给互不相同的值。
	{
		UPlanetRecipe* Ri = NewObject<UPlanetRecipe>();
		Ri->bBatholiths = false; Ri->bStocks = false; Ri->bDikes = false; Ri->bSills = false;
		Ri->bLayeredIntrusions = false; Ri->bKimberlites = false; Ri->bAlteration = false;
		Ri->BatholithMinVolcanoes = 5; Ri->DikePerRift = 3; Ri->LayeredCount = 6; Ri->KimberliteMax = 9;
		Ri->BatholithLongRatio = 0.081f; Ri->BatholithShortRatio = 0.041f; Ri->BatholithThickRatio = 0.031f;
		Ri->BatholithTopDepthRatio = 0.051f; Ri->StockRadiusRatio = 0.0081f; Ri->StockTopDepthRatio = 0.031f;
		Ri->DikeThicknessRatio = 0.0021f; Ri->DikeLengthRatio = 0.021f; Ri->DikeTopDepthRatio = 0.021f;
		Ri->SillRadiusRatio = 0.051f; Ri->SillThicknessRatio = 0.0021f; Ri->SillTopDepthRatio = 0.021f;
		Ri->CratonSillDensity = 0.41f; Ri->LayeredRadiusRatio = 0.031f; Ri->LayeredThicknessRatio = 0.0026f;
		Ri->LayeredGapRatio = 0.0041f; Ri->LayeredTopDepthRatio = 0.061f; Ri->KimberliteRadiusRatio = 0.0021f;
		Ri->KimberliteHalfHeightRatio = 0.011f; Ri->KimberliteTopDepthRatio = 0.0041f; Ri->AlterHaloScale = 2.3f;
		Ri->AlterKeratiticBand = 0.31f; Ri->AlterSericiticBand = 0.56f; Ri->AlterArgillicBand = 0.81f;
		const FPlanetIntrusionParams IC = Ri->ToConfig(40000.0).Intrusion;
		TestFalse(TEXT("bBatholiths 透传"), IC.bBatholiths);
		TestFalse(TEXT("bStocks 透传"), IC.bStocks);
		TestFalse(TEXT("bDikes 透传"), IC.bDikes);
		TestFalse(TEXT("bSills 透传"), IC.bSills);
		TestFalse(TEXT("bLayeredIntrusions 透传"), IC.bLayeredIntrusions);
		TestFalse(TEXT("bKimberlites 透传"), IC.bKimberlites);
		TestFalse(TEXT("bAlteration 透传"), IC.bAlteration);
		TestEqual(TEXT("BatholithMinVolcanoes 透传"), IC.BatholithMinVolcanoes, 5);
		TestEqual(TEXT("DikePerRift 透传"), IC.DikePerRift, 3);
		TestEqual(TEXT("LayeredCount 透传"), IC.LayeredCount, 6);
		TestEqual(TEXT("KimberliteMax 透传"), IC.KimberliteMax, 9);
		TestEqual(TEXT("BatholithLongRatio 透传"), IC.BatholithLongRatio, 0.081f, 1e-6f);
		TestEqual(TEXT("BatholithShortRatio 透传"), IC.BatholithShortRatio, 0.041f, 1e-6f);
		TestEqual(TEXT("BatholithThickRatio 透传"), IC.BatholithThickRatio, 0.031f, 1e-6f);
		TestEqual(TEXT("BatholithTopDepthRatio 透传"), IC.BatholithTopDepthRatio, 0.051f, 1e-6f);
		TestEqual(TEXT("StockRadiusRatio 透传"), IC.StockRadiusRatio, 0.0081f, 1e-6f);
		TestEqual(TEXT("StockTopDepthRatio 透传"), IC.StockTopDepthRatio, 0.031f, 1e-6f);
		TestEqual(TEXT("DikeThicknessRatio 透传"), IC.DikeThicknessRatio, 0.0021f, 1e-6f);
		TestEqual(TEXT("DikeLengthRatio 透传"), IC.DikeLengthRatio, 0.021f, 1e-6f);
		TestEqual(TEXT("DikeTopDepthRatio 透传"), IC.DikeTopDepthRatio, 0.021f, 1e-6f);
		TestEqual(TEXT("SillRadiusRatio 透传"), IC.SillRadiusRatio, 0.051f, 1e-6f);
		TestEqual(TEXT("SillThicknessRatio 透传"), IC.SillThicknessRatio, 0.0021f, 1e-6f);
		TestEqual(TEXT("SillTopDepthRatio 透传"), IC.SillTopDepthRatio, 0.021f, 1e-6f);
		TestEqual(TEXT("CratonSillDensity 透传"), IC.CratonSillDensity, 0.41f, 1e-6f);
		TestEqual(TEXT("LayeredRadiusRatio 透传"), IC.LayeredRadiusRatio, 0.031f, 1e-6f);
		TestEqual(TEXT("LayeredThicknessRatio 透传"), IC.LayeredThicknessRatio, 0.0026f, 1e-6f);
		TestEqual(TEXT("LayeredGapRatio 透传"), IC.LayeredGapRatio, 0.0041f, 1e-6f);
		TestEqual(TEXT("LayeredTopDepthRatio 透传"), IC.LayeredTopDepthRatio, 0.061f, 1e-6f);
		TestEqual(TEXT("KimberliteRadiusRatio 透传"), IC.KimberliteRadiusRatio, 0.0021f, 1e-6f);
		TestEqual(TEXT("KimberliteHalfHeightRatio 透传"), IC.KimberliteHalfHeightRatio, 0.011f, 1e-6f);
		TestEqual(TEXT("KimberliteTopDepthRatio 透传"), IC.KimberliteTopDepthRatio, 0.0041f, 1e-6f);
		TestEqual(TEXT("AlterHaloScale 透传"), IC.AlterHaloScale, 2.3f, 1e-6f);
		TestEqual(TEXT("AlterKeratiticBand 透传"), IC.AlterKeratiticBand, 0.31f, 1e-6f);
		TestEqual(TEXT("AlterSericiticBand 透传"), IC.AlterSericiticBand, 0.56f, 1e-6f);
		TestEqual(TEXT("AlterArgillicBand 透传"), IC.AlterArgillicBand, 0.81f, 1e-6f);
	}

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
	Probe = C2; Probe.Plates.SpeedMax = 11.f;              TestTrue(TEXT("PlateSpeedMax 进哈希"), Probe.Hash() != Base);
	Probe = C2; Probe.Terrain.SpeedWidthEffect = 0.61f;    TestTrue(TEXT("SpeedWidthEffect 进哈希"), Probe.Hash() != Base);
	Probe = C2; Probe.Terrain.MidOceanRidgeHeightRatio = 0.0061f; TestTrue(TEXT("洋中脊高度进哈希"), Probe.Hash() != Base);
	Probe = C2; Probe.Terrain.RiftDepthRatio = 0.0041f;    TestTrue(TEXT("裂谷深度进哈希"), Probe.Hash() != Base);
	Probe = C2; Probe.Terrain.OldOrogenHeightRatio = 0.0081f; TestTrue(TEXT("古老造山带进哈希"), Probe.Hash() != Base);
	Probe = C2; Probe.Terrain.OldOrogenWidthRatio = 0.046f;   TestTrue(TEXT("古老造山带宽度进哈希"), Probe.Hash() != Base);
	Probe = C2; Probe.Terrain.OldOrogenGapFraction = 0.31f;   TestTrue(TEXT("古老造山带断接进哈希"), Probe.Hash() != Base);
	Probe = C2; Probe.Terrain.TrenchSedimentFill = 0.71f;   TestTrue(TEXT("TrenchSedimentFill 进哈希"), Probe.Hash() != Base);
	Probe = C2; Probe.Terrain.TrenchSlump = 0.51f;         TestTrue(TEXT("TrenchSlump 进哈希"), Probe.Hash() != Base);
	Probe = C2; Probe.Terrain.MountainHeightRatio = 0.071f;  TestTrue(TEXT("MountainHeightRatio 进哈希"), Probe.Hash() != Base);
	Probe = C2; Probe.Terrain.ErosionIterations = 10;      TestTrue(TEXT("ErosionIterations 进哈希"), Probe.Hash() != Base);
	Probe = C2; Probe.Continents.DetailOctaves = 6;         TestTrue(TEXT("DetailOctaves 进哈希"), Probe.Hash() != Base);
	Probe = C2; Probe.Continents.AbyssRatio = 0.8f;         TestTrue(TEXT("AbyssRatio 进哈希"), Probe.Hash() != Base);
	Probe = C2; Probe.Volcano.StratoHeightRatio = 0.0121f;  TestTrue(TEXT("层状火山高进哈希"), Probe.Hash() != Base);
	Probe = C2; Probe.Volcano.HotspotCount = 5;              TestTrue(TEXT("热点数进哈希"), Probe.Hash() != Base);
	Probe = C2; Probe.Volcano.bCalderas = false;             TestTrue(TEXT("破火山口开关进哈希"), Probe.Hash() != Base);
	Probe = C2; Probe.Volcano.CinderHeightRatio = 0.0021f;   TestTrue(TEXT("渣锥高进哈希"), Probe.Hash() != Base);
	Probe = C2; Probe.Intrusion.BatholithLongRatio = 0.081f;  TestTrue(TEXT("岩基长轴进哈希"), Probe.Hash() != Base);
	Probe = C2; Probe.Intrusion.KimberliteMax = 9;            TestTrue(TEXT("岩管上限进哈希"), Probe.Hash() != Base);
	Probe = C2; Probe.Intrusion.bAlteration = false;          TestTrue(TEXT("蚀变开关进哈希"), Probe.Hash() != Base);
	return true;
}

#endif
