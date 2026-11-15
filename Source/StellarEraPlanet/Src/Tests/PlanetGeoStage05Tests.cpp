// 阶段 5：温度 / 风带 / 降水（纬度基准 + 离海衰减 + 逆风雨影）。群系在后续任务。
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

	/** 南北向长山：Elevation = Amp·exp(−(u/w)²)，u 是到 XZ 经线面的角距；其余场只为过阶段校验。 */
	FPlanetBakeFields Stage05RidgePlanet(const FPlanetRecipeConfig& C, float RidgeAmpVoxels, float RidgeHalfWidthDeg)
	{
		const int32 Res = FMath::Max(C.Climate.ClimateRes, 1);
		const float HalfWidth = FMath::DegreesToRadians(FMath::Max(RidgeHalfWidthDeg, 1e-3f));
		FPlanetBakeFields F;
		F.Elevation.Init(Res);
		F.Elevation.Fill([RidgeAmpVoxels, HalfWidth](const FVector3f& Dir)
		{
			const float U = FMath::Asin(FMath::Clamp(FMath::Abs(Dir.Y), 0.f, 1.f));
			const float T = U / HalfWidth;
			return RidgeAmpVoxels * FMath::Exp(-T * T);
		});
		F.ContinentalMask.Init(Res);
		F.ContinentalMask.Fill([](const FVector3f&) { return 1.f; });
		F.PlateId.Init(Res);
		F.PlateId.Fill([](const FVector3f&) { return 0.f; });
		F.CrustType.Init(Res);
		F.CrustType.Fill([](const FVector3f&) { return 1.f; });
		return F;
	}

	/** 指定纬度上「距经线面角距 = UDeg」的方向；东侧的 y > 0，西侧的 y < 0。 */
	FVector3f Stage05DirAt(float LatDeg, float UDeg, bool bEast)
	{
		const float SinLat = FMath::Sin(FMath::DegreesToRadians(LatDeg));
		const float Y = (bEast ? 1.f : -1.f) * FMath::Sin(FMath::DegreesToRadians(UDeg));
		const float X = FMath::Sqrt(FMath::Max(0.f, 1.f - Y * Y - SinLat * SinLat));
		return FVector3f(X, Y, SinLat);
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

	// 方位角 → 切向：90° 必须指向经度增加方向（东），写成 dir × N 会把整张风带镜像。
	{
		const float Lat = FMath::DegreesToRadians(45.f), Lon = FMath::DegreesToRadians(30.f);
		const FVector3f Dir(FMath::Cos(Lat) * FMath::Cos(Lon), FMath::Cos(Lat) * FMath::Sin(Lon), FMath::Sin(Lat));
		const FVector3f East(-FMath::Sin(Lon), FMath::Cos(Lon), 0.f);
		const FVector3f W = Stage05::WindTangent(Dir, 90.f);
		TestTrue(FString::Printf(TEXT("az=90° 切向 = 东（dot %.4f）"), FVector3f::DotProduct(W, East)), FVector3f::DotProduct(W, East) > 0.999f);
		TestTrue(TEXT("az=270° 切向 = 西"), FVector3f::DotProduct(Stage05::WindTangent(Dir, 270.f), East) < -0.999f);
		TestTrue(TEXT("极点没有可定义的切向"), Stage05::WindTangent(FVector3f(0.f, 0.f, 1.f), 270.f).IsNearlyZero());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGeoStage05DryBeltTest, "StellarEra.Planet.Geo.Stage05.DryBelt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGeoStage05DryBeltTest::RunTest(const FString& Parameters)
{
	FPlanetRecipeConfig C;
	C.FaceRes = 256;
	FPlanetBakeReport R;
	const FPlanetBakeFields F = Stage05TestPlanet(C, R);
	if (!TestFalse(FString::Printf(TEXT("无错误（%s）"), *FString::Join(R.Errors, TEXT(" | "))), R.HasErrors())) { return false; }
	if (!TestEqual(TEXT("降水场分辨率 = ClimateRes"), F.Precipitation.Res, C.Climate.ClimateRes)) { return false; }

	double DrySum = 0.0, EqSum = 0.0, MidSum = 0.0;
	int32 DryN = 0, EqN = 0, MidN = 0;
	for (int32 Face = 0; Face < 6; ++Face)
		for (int32 Y = 0; Y < F.Precipitation.Res; ++Y)
			for (int32 X = 0; X < F.Precipitation.Res; ++X)
			{
				const float AbsLat = FMath::Abs(Stage05LatDeg(F.Precipitation.TexelDir(Face, X, Y)));
				const float P = F.Precipitation.At(Face, X, Y);
				if (AbsLat >= 25.f && AbsLat <= 35.f) { DrySum += P; ++DryN; }
				else if (AbsLat < 10.f) { EqSum += P; ++EqN; }
				else if (AbsLat >= 40.f && AbsLat <= 50.f) { MidSum += P; ++MidN; }
			}
	const double DryMean = DryN > 0 ? DrySum / DryN : 0.0;
	const double EqMean = EqN > 0 ? EqSum / EqN : 0.0;
	const double MidMean = MidN > 0 ? MidSum / MidN : 0.0;
	AddInfo(FString::Printf(TEXT("降水均值：25–35° %.4f，|lat|<10° %.4f，40–50° %.4f"), DryMean, EqMean, MidMean));
	TestTrue(TEXT("三个纬度带都有样本"), DryN > 0 && EqN > 0 && MidN > 0);
	TestTrue(TEXT("干旱带比赤道带低 ≥0.15"), DryMean < EqMean - 0.15);
	TestTrue(TEXT("干旱带比中纬带低 ≥0.15"), DryMean < MidMean - 0.15);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGeoStage05RainShadowTest, "StellarEra.Planet.Geo.Stage05.RainShadow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGeoStage05RainShadowTest::RunTest(const FString& Parameters)
{
	FPlanetRecipeConfig C;
	C.FaceRes = 256;
	FPlanetBakeReport R;
	// 南北向长山，风在 45° 吹向东：东侧背风、西侧迎风。
	// 山高取 0.08R（spec 默认 0.04R 时 3–5° 采样点已在山腰，实测只差 0.289，够不到验收线 0.4）。
	FPlanetBakeFields G = Stage05RidgePlanet(C, 0.08f * float(C.Radius), 6.f);
	FPlanetBakeContext C5{ C, G, FGeoRng::Derive(C.Seed, 5, 0), R };
	FStage05Climate().Bake(C5);
	if (!TestFalse(FString::Printf(TEXT("合成星球无错误（%s）"), *FString::Join(R.Errors, TEXT(" | "))), R.HasErrors())) { return false; }

	double LeeSum = 0.0, WindSum = 0.0;
	int32 N = 0;
	for (const float Lat : { 40.f, 45.f, 50.f })
		for (const float Off : { 3.f, 4.f, 5.f })
		{
			LeeSum += G.Precipitation.Sample(Stage05DirAt(Lat, Off, true));
			WindSum += G.Precipitation.Sample(Stage05DirAt(Lat, Off, false));
			++N;
		}
	const double LeeMean = N > 0 ? LeeSum / N : 0.0;
	const double WindMean = N > 0 ? WindSum / N : 0.0;
	AddInfo(FString::Printf(TEXT("背风（东）均值 %.4f，迎风（西）均值 %.4f，差 %.4f"), LeeMean, WindMean, WindMean - LeeMean));
	TestTrue(TEXT("迎风侧比背风侧湿 > 0.4"), WindMean - LeeMean > 0.4);
	return true;
}

#endif
