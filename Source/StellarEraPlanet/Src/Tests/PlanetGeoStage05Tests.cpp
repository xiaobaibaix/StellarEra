// 阶段 5：温度（纬度 / 高程 / 大陆度）与风带（纬度三分段）。降水 / 群系在后续任务。
#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "GeoRng.h"
#include "PlanetBakePipeline.h"
#include "Stage02Plates.h"
#include "Stage03Terrain.h"
#include "Stage05Climate.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	/** 直接跑 2→3→5：气候只读高程与大陆度，不需要阶段 4。 */
	FPlanetBakeFields Stage05TestPlanet(const FPlanetRecipeConfig& C, FPlanetBakeReport& R)
	{
		FPlanetBakeFields F;
		FPlanetBakeContext C2{ C, F, FGeoRng::Derive(C.Seed, 2, 0), R }; FStage02Plates().Bake(C2);
		FPlanetBakeContext C3{ C, F, FGeoRng::Derive(C.Seed, 3, 0), R }; FStage03Terrain().Bake(C3);
		FPlanetBakeContext C5{ C, F, FGeoRng::Derive(C.Seed, 5, 0), R }; FStage05Climate().Bake(C5);
		return F;
	}

	float Stage05LatDeg(const FVector3f& Dir)
	{
		return FMath::RadiansToDegrees(FMath::Asin(FMath::Clamp(Dir.Z, -1.f, 1.f)));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGeoStage05TemperatureTest, "StellarEra.Planet.Geo.Stage05.Temperature",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGeoStage05TemperatureTest::RunTest(const FString& Parameters)
{
	FPlanetRecipeConfig C;
	C.FaceRes = 256;
	FPlanetBakeReport R;
	const FPlanetBakeFields F = Stage05TestPlanet(C, R);
	if (!TestFalse(FString::Printf(TEXT("无错误（%s）"), *FString::Join(R.Errors, TEXT(" | "))), R.HasErrors())) { return false; }
	if (!TestEqual(TEXT("温度场分辨率 = ClimateRes"), F.Temperature.Res, C.Climate.ClimateRes)) { return false; }

	double EqSum = 0.0, PoleSum = 0.0;
	int32 EqN = 0, PoleN = 0;
	for (int32 Face = 0; Face < 6; ++Face)
		for (int32 Y = 0; Y < F.Temperature.Res; ++Y)
			for (int32 X = 0; X < F.Temperature.Res; ++X)
			{
				const float Lat = Stage05LatDeg(F.Temperature.TexelDir(Face, X, Y));
				const float T = F.Temperature.At(Face, X, Y);
				if (FMath::Abs(Lat) <= 5.f) { EqSum += T; ++EqN; }
				else if (FMath::Abs(Lat) >= 85.f) { PoleSum += T; ++PoleN; }
			}
	const double EqMean = EqN > 0 ? EqSum / EqN : 0.0, PoleMean = PoleN > 0 ? PoleSum / PoleN : 0.0;
	AddInfo(FString::Printf(TEXT("赤道带（|lat|≤5°）温度均值 %.4f，极区（|lat|≥85°）%.4f"), EqMean, PoleMean));
	TestTrue(TEXT("赤道 > 0.8"), EqMean > 0.8);
	TestTrue(TEXT("极区 < 0.2"), PoleMean < 0.2);

	// 高程递减：合成一颗大陆度恒定的星球，高程只随经度分两档，保证同纬度带两组样本可比。
	FPlanetBakeFields G;
	G.Elevation.Init(128);
	G.Elevation.Fill([R0 = float(C.Radius)](const FVector3f& Dir) { return Dir.X > 0.f ? 0.05f * R0 : 0.f; });
	G.ContinentalMask.Init(128);
	G.ContinentalMask.Fill([](const FVector3f&) { return 0.5f; });
	FPlanetBakeReport GR;
	FPlanetBakeContext G5{ C, G, FGeoRng::Derive(C.Seed, 5, 0), GR };
	FStage05Climate().Bake(G5);
	if (!TestFalse(FString::Printf(TEXT("合成星球无错误（%s）"), *FString::Join(GR.Errors, TEXT(" | "))), GR.HasErrors())) { return false; }

	double HiSum = 0.0, LoSum = 0.0;
	int32 HiN = 0, LoN = 0;
	for (int32 Face = 0; Face < 6; ++Face)
		for (int32 Y = 0; Y < G.Temperature.Res; ++Y)
			for (int32 X = 0; X < G.Temperature.Res; ++X)
			{
				const float AbsLat = FMath::Abs(Stage05LatDeg(G.Temperature.TexelDir(Face, X, Y)));
				if (AbsLat < 30.f || AbsLat > 60.f) { continue; }
				const float T = G.Temperature.At(Face, X, Y);
				if (G.Elevation.Sample(G.Temperature.TexelDir(Face, X, Y)) > 0.f) { HiSum += T; ++HiN; }
				else { LoSum += T; ++LoN; }
			}
	const double HiMean = HiN > 0 ? HiSum / HiN : 0.0, LoMean = LoN > 0 ? LoSum / LoN : 0.0;
	AddInfo(FString::Printf(TEXT("同纬度带高地均值 %.4f，低地均值 %.4f"), HiMean, LoMean));
	TestTrue(TEXT("两组样本都非空"), HiN > 0 && LoN > 0);
	TestTrue(TEXT("同纬度高地比低地冷 > 0.15"), HiMean < LoMean - 0.15);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGeoStage05WindTest, "StellarEra.Planet.Geo.Stage05.Wind",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGeoStage05WindTest::RunTest(const FString& Parameters)
{
	FPlanetRecipeConfig C;
	C.FaceRes = 256;
	FPlanetBakeReport R;
	const FPlanetBakeFields F = Stage05TestPlanet(C, R);
	if (!TestFalse(FString::Printf(TEXT("无错误（%s）"), *FString::Join(R.Errors, TEXT(" | "))), R.HasErrors())) { return false; }
	if (!TestEqual(TEXT("风场分辨率 = ClimateRes"), F.WindDir.Res, C.Climate.ClimateRes)) { return false; }

	// 方位角：0 = 北，90 = 东。信风与极地东风都向西（270°），靠纬度区分带。
	const float Lats[] = { 5.f, 20.f, 40.f, 55.f, 70.f, 85.f };
	const float Wants[] = { 270.f, 270.f, 90.f, 90.f, 270.f, 270.f };
	for (int32 i = 0; i < UE_ARRAY_COUNT(Lats); ++i)
	{
		const float Lat = FMath::DegreesToRadians(Lats[i]);
		const FVector3f Dir(FMath::Cos(Lat), 0.f, FMath::Sin(Lat));
		const float Az = F.WindDir.Sample(Dir);
		const float Delta = FMath::Abs(FMath::FindDeltaAngleDegrees(Az, Wants[i]));
		TestTrue(FString::Printf(TEXT("lat %.0f° 吹向 %.0f°（实测 %.1f°）"), Lats[i], Wants[i], Az), Delta <= 2.f);
	}
	return true;
}

#endif
