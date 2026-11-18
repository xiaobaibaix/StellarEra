// 3A / 3B / 7 的验收：盖层出现在低地、断层与变质级非空、风化壳湿热厚干旱薄、
// 次生富集不超出宿主的水平范围。
#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "GeoSurfaceDepositRules.h"
#include "GeoLithology.h"
#include "GeoSoil.h"
#include "PlanetBakePipeline.h"
#include "PlanetMaterialEval.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGeoStage07WeatheringTest, "StellarEra.Planet.Geo.Stage07.Weathering",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGeoStage07WeatheringTest::RunTest(const FString& Parameters)
{
	FPlanetRecipeConfig C;
	C.FaceRes = 256;
	FPlanetBakeReport R;
	const TSharedPtr<const FPlanetSnapshot> Snap = FPlanetBakePipeline::MakeDefault().Bake(C, R);
	if (!TestTrue(FString::Printf(TEXT("烘焙成功（%s）"), *FString::Join(R.Errors, TEXT(" | "))), Snap.IsValid())) { return false; }
	TestTrue(TEXT("风化壳场已生成"), Snap->WeatheringDepth.Res > 0);
	TestTrue(TEXT("土壤类型场已生成"), Snap->SoilType.Res > 0);

	// 气候场不进快照，只能按土壤类型分组比厚度：红土（湿热）应该比干旱壳厚。
	float WetSum = 0.f, DrySum = 0.f;
	int32 Wet = 0, Dry = 0;
	FGeoRng Rng = FGeoRng::Derive(31, 0, 0);
	for (int32 i = 0; i < 2048; ++i)
	{
		const float Z = Rng.Range(-1.f, 1.f), Phi = Rng.Range(0.f, 2.f * PI), S = FMath::Sqrt(1.f - Z * Z);
		const FVector3f Dir(S * FMath::Cos(Phi), S * FMath::Sin(Phi), Z);
		const uint8 Soil = uint8(FMath::RoundToInt(Snap->SoilType.SampleNearest(Dir)));
		const float D = Snap->WeatheringDepth.Sample(Dir);
		if (Soil == GeoSoil::Laterite) { WetSum += D; ++Wet; }
		if (Soil == GeoSoil::Arid) { DrySum += D; ++Dry; }
	}
	const float WetAvg = Wet > 0 ? WetSum / Wet : 0.f;
	const float DryAvg = Dry > 0 ? DrySum / Dry : 0.f;
	AddInfo(FString::Printf(TEXT("红土 %d 处均值 %.2f，干旱壳 %d 处均值 %.2f"), Wet, WetAvg, Dry, DryAvg));
	TestTrue(TEXT("有红土分布"), Wet > 0);
	TestTrue(TEXT("红土处风化壳比干旱壳厚"), Dry > 0 && WetAvg > DryAvg);

	// 关掉整段 → 场为空，后续阶段不报错。
	FPlanetRecipeConfig Off = C;
	Off.Weathering.bWeathering = false;
	FPlanetBakeReport R2;
	const TSharedPtr<const FPlanetSnapshot> S2 = FPlanetBakePipeline::MakeDefault().Bake(Off, R2);
	TestTrue(TEXT("关风化仍能烘焙"), S2.IsValid());
	TestEqual(TEXT("关风化 → 场为空"), S2.IsValid() ? S2->WeatheringDepth.Res : -1, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGeoStrataTest, "StellarEra.Planet.Geo.Stage03A.Sediments",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGeoStrataTest::RunTest(const FString& Parameters)
{
	FPlanetRecipeConfig C;
	C.FaceRes = 256;
	FPlanetBakeReport R;
	const TSharedPtr<const FPlanetSnapshot> Snap = FPlanetBakePipeline::MakeDefault().Bake(C, R);
	if (!TestTrue(FString::Printf(TEXT("烘焙成功（%s）"), *FString::Join(R.Errors, TEXT(" | "))), Snap.IsValid())) { return false; }
	TestTrue(TEXT("盖层场已生成"), Snap->StrataThickness.Res > 0);

	// 盖层只在陆地上，且低于最大厚度。
	FGeoRng Rng = FGeoRng::Derive(32, 0, 0);
	int32 Covered = 0, OverThick = 0, OceanCovered = 0;
	float MaxSeen = 0.f;
	for (int32 i = 0; i < 2048; ++i)
	{
		const float Z = Rng.Range(-1.f, 1.f), Phi = Rng.Range(0.f, 2.f * PI), S = FMath::Sqrt(1.f - Z * Z);
		const FVector3f Dir(S * FMath::Cos(Phi), S * FMath::Sin(Phi), Z);
		const float Th = Snap->StrataThickness.Sample(Dir);
		MaxSeen = FMath::Max(MaxSeen, Th);
		Covered += Th >= 1.f ? 1 : 0;
		OverThick += Th > float(C.Strata.MaxThicknessRatio * C.Radius) + 1.f ? 1 : 0;
		if (Snap->Elevation.Res > 0 && Snap->Elevation.Sample(Dir) < Snap->CrustSplitElevation - 400.f)
		{
			OceanCovered += Th >= 1.f ? 1 : 0;
		}
	}
	AddInfo(FString::Printf(TEXT("2048 个方向里 %d 处有盖层，最厚 %.1f 体素"), Covered, MaxSeen));
	TestTrue(TEXT("低地上确实盖了层"), Covered > 0);
	TestEqual(TEXT("厚度不超过配置上限"), OverThick, 0);
	// 陆架本来就在水下、也算陆壳，允许有盖层；深海必须没有。
	TestEqual(TEXT("深海没有陆相盖层"), OceanCovered, 0);

	// 盖层内的地表岩性来自盖层，不再是陆壳。
	int32 Sediment = 0;
	for (int32 i = 0; i < 2048; ++i)
	{
		const float Z = Rng.Range(-1.f, 1.f), Phi = Rng.Range(0.f, 2.f * PI), S = FMath::Sqrt(1.f - Z * Z);
		const FVector3f Dir(S * FMath::Cos(Phi), S * FMath::Sin(Phi), Z);
		if (Snap->StrataThickness.Sample(Dir) < 2.f) { continue; }
		const double Rr = Snap->R0 + double(Snap->Elevation.Sample(Dir)) - 1.0;
		const uint8 L = PlanetMaterialEval::Sample(*Snap, FVector3d(Dir) * Rr).Lithology;
		Sediment += (L == GeoLithology::Sandstone || L == GeoLithology::Shale || L == GeoLithology::Limestone) ? 1 : 0;
	}
	TestTrue(TEXT("盖层内的地表采样得到沉积岩"), Sediment > 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGeoTectonicsTest, "StellarEra.Planet.Geo.Stage03B.Tectonics",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGeoTectonicsTest::RunTest(const FString& Parameters)
{
	FPlanetRecipeConfig C;
	C.FaceRes = 256;
	FPlanetBakeReport R;
	const TSharedPtr<const FPlanetSnapshot> Snap = FPlanetBakePipeline::MakeDefault().Bake(C, R);
	if (!TestTrue(FString::Printf(TEXT("烘焙成功（%s）"), *FString::Join(R.Errors, TEXT(" | "))), Snap.IsValid())) { return false; }
	TestTrue(TEXT("断层场已生成"), Snap->FaultMask.Res > 0);
	TestTrue(TEXT("变质级场已生成"), Snap->MetamorphicGrade.Res > 0);

	int32 FaultCells = 0, HighGrade = 0;
	for (int32 Face = 0; Face < 6; ++Face)
	{
		for (int32 Y = 0; Y < Snap->FaultMask.Res; Y += 4)
		{
			for (int32 X = 0; X < Snap->FaultMask.Res; X += 4)
			{
				FaultCells += Snap->FaultMask.At(Face, X, Y) > 0.05f ? 1 : 0;
				HighGrade += Snap->MetamorphicGrade.At(Face, X, Y) > 0.3f ? 1 : 0;
			}
		}
	}
	AddInfo(FString::Printf(TEXT("断层格 %d，高变质格 %d"), FaultCells, HighGrade));
	TestTrue(TEXT("有断层带"), FaultCells > 0);
	TestTrue(TEXT("有高变质区"), HighGrade > 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGeoSupergeneTest, "StellarEra.Planet.Geo.Stage07.Supergene",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGeoSupergeneTest::RunTest(const FString& Parameters)
{
	// 直接喂一个宿主矿体，验证富集毯只在它正上方、且水平范围不出宿主。
	FPlanetRecipeConfig C;
	C.Radius = 40000.0;
	C.Minerals.Add({ TEXT("斑岩铜"), FColor::Orange });
	FPlanetBakeFields F;
	F.Elevation.Init(64);
	F.Elevation.Fill([](const FVector3f&) { return 0.f; });
	FGeoPrimitive Host = FGeoPrimitive::MakeBlob(FVector3f(0.f, 0.f, 39500.f), FVector3f(300.f, 200.f, 400.f), 0.1f);
	Host.MineralId = 1;
	F.Primitives.Add(Host);

	FGeoSupergeneRule Rule;
	Rule.HostMineral = TEXT("斑岩铜");
	Rule.DepthBelowSurface = 20.f;
	Rule.ThicknessVoxels = 12.f;
	TArray<FString> Errors;
	FGeoScatterContext Ctx{ C, F, FGeoRng::Derive(1, 8, 0), Errors };
	FGeoScatterOutput Out;
	Rule.Scatter(Ctx, Out);
	TestEqual(TEXT("每个宿主矿体上一条富集毯"), Out.Bodies.Num(), 1);
	if (Out.Bodies.Num() == 1)
	{
		const FGeoPrimitive& Band = Out.Bodies[0];
		TestTrue(TEXT("富集毯在宿主上方"), Band.Center.Z > Host.Center.Z + Host.Radii.Z);
		TestTrue(TEXT("富集毯仍在地表之下"), Band.Center.Size() < C.Radius);
		TestTrue(TEXT("水平范围不超出宿主的放大范围"), Band.Radii.X <= Host.Radii.X * Rule.HostMargin + 1.f
			&& Band.Radii.Y <= Host.Radii.Y * Rule.HostMargin + 1.f);
	}
	return true;
}

#endif
