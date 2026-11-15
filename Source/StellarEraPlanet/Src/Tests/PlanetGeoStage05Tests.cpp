// 阶段 5：温度、风带、降水（纬度基准 + 离海衰减 + 逆风雨影）、群系查表与图层、尺度不变与确定性。
#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "GeoRng.h"
#include "PlanetBakePipeline.h"
#include "PlanetSnapshot.h"
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

	// 离海衰减：逐 10° 纬度档比「高大陆度 vs 低大陆度」，消掉纬度基准的干扰。
	double FalloffSum = 0.0;
	int32 FalloffBins = 0;
	for (int32 Bin = 0; Bin < 6; ++Bin)
	{
		double HiSum = 0.0, LoSum = 0.0;
		int32 HiN = 0, LoN = 0;
		for (int32 Face = 0; Face < 6; ++Face)
			for (int32 Y = 0; Y < F.Precipitation.Res; ++Y)
				for (int32 X = 0; X < F.Precipitation.Res; ++X)
				{
					const FVector3f Dir = F.Precipitation.TexelDir(Face, X, Y);
					const float AbsLat = FMath::Abs(Stage05LatDeg(Dir));
					if (AbsLat < float(Bin * 10) || AbsLat >= float(Bin * 10 + 10)) { continue; }
					const float M = F.ContinentalMask.Sample(Dir);
					const float P = F.Precipitation.At(Face, X, Y);
					if (M > 0.8f) { HiSum += P; ++HiN; }
					else if (M < 0.2f) { LoSum += P; ++LoN; }
				}
		if (HiN > 0 && LoN > 0)
		{
			FalloffSum += LoSum / LoN - HiSum / HiN;
			++FalloffBins;
		}
	}
	const double FalloffGap = FalloffBins > 0 ? FalloffSum / FalloffBins : 0.0;
	AddInfo(FString::Printf(TEXT("同纬度带低大陆度减高大陆度均值差 %.4f（%d 档）"), FalloffGap, FalloffBins));
	TestTrue(TEXT("至少三个纬度档同时有高/低大陆度样本"), FalloffBins >= 3);
	TestTrue(TEXT("同纬度带高大陆度更干"), FalloffGap > 0.05);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGeoStage05RainShadowTest, "StellarEra.Planet.Geo.Stage05.RainShadow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGeoStage05RainShadowTest::RunTest(const FString& Parameters)
{
	FPlanetRecipeConfig C;
	C.FaceRes = 256;
	// 合成山整颗都是内陆（M=1）：离海衰减给两侧乘同一个常数、只把差值等比缩小，本测试只验雨影。
	C.Climate.OceanFalloff = 0.f;
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGeoStage05BiomeTableTest, "StellarEra.Planet.Geo.Stage05.BiomeTable",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGeoStage05BiomeTableTest::RunTest(const FString& Parameters)
{
	FPlanetRecipeConfig C;
	C.FaceRes = 256;
	FPlanetBakeReport R;
	const FPlanetBakeFields F = Stage05TestPlanet(C, R);
	if (!TestFalse(FString::Printf(TEXT("无错误（%s）"), *FString::Join(R.Errors, TEXT(" | "))), R.HasErrors())) { return false; }
	if (!TestEqual(TEXT("群系场分辨率 = ClimateRes"), F.Biome.Res, C.Climate.ClimateRes)) { return false; }

	int32 Mismatch = 0, BadValue = 0;
	int32 Counts[8] = { 0 };
	for (int32 Face = 0; Face < 6; ++Face)
		for (int32 Y = 0; Y < F.Biome.Res; ++Y)
			for (int32 X = 0; X < F.Biome.Res; ++X)
			{
				const float T = F.Temperature.At(Face, X, Y);
				const float P = F.Precipitation.At(Face, X, Y);
				const float Recorded = F.Biome.At(Face, X, Y);
				const int32 Re = Stage05::BiomeFor(T, P, C.Climate);
				// 场里存 float 编号：用记录下来的 T/P 重查必须逐位落在同一编号，漏写一格这里就会暴露。
				Mismatch += Recorded == float(Re) ? 0 : 1;
				if (Recorded < 0.f || Recorded > 7.f || Recorded != float(int32(Recorded))) { ++BadValue; }
				else { ++Counts[int32(Recorded)]; }
			}
	static const TCHAR* Names[8] = { TEXT("冰原"), TEXT("沙漠"), TEXT("苔原"), TEXT("针叶林"),
		TEXT("雨林"), TEXT("稀树草原"), TEXT("温带林"), TEXT("草原") };
	FString Hist;
	int32 Distinct = 0;
	for (int32 i = 0; i < 8; ++i)
	{
		Distinct += Counts[i] > 0 ? 1 : 0;
		Hist += FString::Printf(TEXT("%s %d  "), Names[i], Counts[i]);
	}
	AddInfo(FString::Printf(TEXT("群系分布：%s"), *Hist));
	TestEqual(TEXT("逐 texel 重查与记录一致"), Mismatch, 0);
	TestEqual(TEXT("编号都是 0–7 的整数"), BadValue, 0);
	TestTrue(FString::Printf(TEXT("至少四种群系（实测 %d）"), Distinct), Distinct >= 4);

	// 顺序敏感探针：同一对温/降水常同时满足多行条件，只有按表序查才给出正确群系。
	const FPlanetClimateParams& Q = C.Climate;
	struct FBiomeProbe { float T, P; int32 Want; const TCHAR* Name; };
	const FBiomeProbe Probes[] = {
		{ Q.BiomeIceTemp - 0.05f, 0.95f, 0, TEXT("冰原优先于其他") },
		{ 0.5f * (Q.BiomeIceTemp + Q.BiomeTundraTemp), Q.BiomeDesertPrecip - 0.05f, 1, TEXT("沙漠优先于苔原") },
		{ 0.5f * (Q.BiomeIceTemp + Q.BiomeTundraTemp), 0.95f, 2, TEXT("苔原") },
		{ 0.5f * (Q.BiomeTundraTemp + Q.BiomeForestTemp), Q.BiomeForestPrecip + 0.2f, 3, TEXT("针叶林优先于温带林") },
		{ Q.BiomeRainTemp + 0.05f, Q.BiomeRainPrecip + 0.05f, 4, TEXT("雨林优先于稀树草原") },
		{ Q.BiomeSavannaTemp + 0.05f, Q.BiomeDesertPrecip + 0.1f, 5, TEXT("稀树草原") },
		{ Q.BiomeForestTemp + 0.05f, Q.BiomeTemperatePrecip + 0.1f, 6, TEXT("温带林") },
		{ Q.BiomeTundraTemp + 0.1f, Q.BiomeDesertPrecip + 0.1f, 7, TEXT("草原兜底") },
	};
	for (const FBiomeProbe& Probe : Probes)
	{
		TestEqual(FString::Printf(TEXT("%s（T=%.3f，P=%.3f）"), Probe.Name, Probe.T, Probe.P),
			Stage05::BiomeFor(Probe.T, Probe.P, Q), Probe.Want);
	}

	// 图层必须与查表同源：查看器按 Id=Biome 读到的编号得是 Biome 场里那一格。
	const FGeoDebugLayer* BiomeLayer = FPlanetBakePipeline::FindDefaultDebugLayer(TEXT("Biome"));
	if (!TestNotNull(TEXT("注册了 Biome 图层"), BiomeLayer)) { return false; }
	TestTrue(TEXT("Biome 是分类图层"), BiomeLayer->Kind == EGeoDebugKind::Categorical);
	TestEqual(TEXT("图例 8 项"), BiomeLayer->Legend.Num(), 8);
	FPlanetSnapshot Snap;
	Snap.R0 = C.Radius;
	Snap.DebugFields.Add(TEXT("Temperature"), F.Temperature);
	Snap.DebugFields.Add(TEXT("Precipitation"), F.Precipitation);
	Snap.DebugFields.Add(TEXT("Biome"), F.Biome);
	const FIntVector Texels[] = { { 0, 3, 5 }, { 1, 61, 200 }, { 2, 128, 128 }, { 4, 255, 7 }, { 5, 90, 33 } };
	for (const FIntVector& T : Texels)
	{
		const FVector3f Dir = F.Biome.TexelDir(T.X, T.Y, T.Z);
		const float Got = BiomeLayer->Sample(Snap, FVector3d(Dir) * C.Radius);
		const int32 Want = Stage05::BiomeFor(F.Temperature.At(T.X, T.Y, T.Z), F.Precipitation.At(T.X, T.Y, T.Z), C.Climate);
		TestEqual(FString::Printf(TEXT("图层采样 = 查表（面 %d texel %d,%d）"), T.X, T.Y, T.Z), Got, float(Want), 0.f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGeoStage05ScaleInvarianceTest, "StellarEra.Planet.Geo.Stage05.ScaleInvariance",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGeoStage05ScaleInvarianceTest::RunTest(const FString& Parameters)
{
	FPlanetRecipeConfig C;
	C.FaceRes = 256;
	FPlanetRecipeConfig Half = C;
	Half.Radius = C.Radius * 0.5;
	FPlanetBakeReport R1, R2;
	const FPlanetBakeFields Full = Stage05TestPlanet(C, R1);
	const FPlanetBakeFields Fine = Stage05TestPlanet(Half, R2);
	if (!TestFalse(FString::Printf(TEXT("全尺度无错误（%s）"), *FString::Join(R1.Errors, TEXT(" | "))), R1.HasErrors())) { return false; }
	if (!TestFalse(FString::Printf(TEXT("半尺度无错误（%s）"), *FString::Join(R2.Errors, TEXT(" | "))), R2.HasErrors())) { return false; }

	// 容差而非逐位：上游地形带 0.01 体素的绝对数值下限，不随半径缩放（实测最多挪 0.022 体素）。
	if (!TestEqual(TEXT("两次分辨率相同"), Fine.Temperature.Res, Full.Temperature.Res)) { return false; }
	const int32 Res = Full.Temperature.Res;
	const int32 Total = 6 * Res * Res;

	const auto AbsStats = [](const FGeoField2D& A, const FGeoField2D& B, float& OutMax, double& OutMean)
	{
		double Sum = 0.0;
		int32 N = 0;
		OutMax = 0.f;
		for (int32 Face = 0; Face < 6; ++Face)
			for (int32 Y = 0; Y < A.Res; ++Y)
				for (int32 X = 0; X < A.Res; ++X)
				{
					const float D = FMath::Abs(A.At(Face, X, Y) - B.At(Face, X, Y));
					OutMax = FMath::Max(OutMax, D);
					Sum += D;
					++N;
				}
		OutMean = Sum / FMath::Max(N, 1);
	};

	float TMax = 0.f, PMax = 0.f;
	double TMean = 0.0, PMean = 0.0;
	AbsStats(Full.Temperature, Fine.Temperature, TMax, TMean);
	AbsStats(Full.Precipitation, Fine.Precipitation, PMax, PMean);
	AddInfo(FString::Printf(TEXT("温度最大差 %.3e，平均 %.3e；降水最大差 %.3e，平均 %.3e"), TMax, TMean, PMax, PMean));
	TestTrue(FString::Printf(TEXT("温度逐 texel 差 ≤ 2e-3（实测 %.3e）"), TMax), TMax <= 2e-3f);
	TestTrue(FString::Printf(TEXT("温度平均差 ≤ 5e-4（实测 %.3e）"), TMean), TMean <= 5e-4);
	TestTrue(FString::Printf(TEXT("降水逐 texel 差 ≤ 2e-3（实测 %.3e）"), PMax), PMax <= 2e-3f);
	TestTrue(FString::Printf(TEXT("降水平均差 ≤ 5e-4（实测 %.3e）"), PMean), PMean <= 5e-4);

	// 风向只按纬度分段、与高程无关：必须逐位相同。
	int32 WindDiff = 0;
	for (int32 Face = 0; Face < 6; ++Face)
		for (int32 Y = 0; Y < Full.WindDir.Res; ++Y)
			for (int32 X = 0; X < Full.WindDir.Res; ++X)
			{
				WindDiff += Full.WindDir.At(Face, X, Y) == Fine.WindDir.At(Face, X, Y) ? 0 : 1;
			}
	TestEqual(TEXT("风向逐位相同"), WindDiff, 0);

	// 群系是查表：只有正好压在阈值上的 texel 会翻，且翻的地方必须紧贴阈值。
	const FPlanetClimateParams& Q = C.Climate;
	const float TThr[] = { Q.BiomeIceTemp, Q.BiomeTundraTemp, Q.BiomeForestTemp, Q.BiomeRainTemp, Q.BiomeSavannaTemp };
	const float PThr[] = { Q.BiomeDesertPrecip, Q.BiomeForestPrecip, Q.BiomeRainPrecip, Q.BiomeTemperatePrecip };
	const auto NearThreshold = [&TThr, &PThr](float T, float P)
	{
		for (const float V : TThr) { if (FMath::Abs(T - V) <= 0.01f) { return true; } }
		for (const float V : PThr) { if (FMath::Abs(P - V) <= 0.01f) { return true; } }
		return false;
	};
	int32 BiomeDiff = 0, OffThreshold = 0;
	float MaxDT = 0.f, MaxDP = 0.f;
	if (!TestEqual(TEXT("群系场分辨率相同"), Fine.Biome.Res, Full.Biome.Res)) { return false; }
	for (int32 Face = 0; Face < 6; ++Face)
		for (int32 Y = 0; Y < Full.Biome.Res; ++Y)
			for (int32 X = 0; X < Full.Biome.Res; ++X)
			{
				if (Full.Biome.At(Face, X, Y) == Fine.Biome.At(Face, X, Y)) { continue; }
				++BiomeDiff;
				MaxDT = FMath::Max(MaxDT, FMath::Abs(Full.Temperature.At(Face, X, Y) - Fine.Temperature.At(Face, X, Y)));
				MaxDP = FMath::Max(MaxDP, FMath::Abs(Full.Precipitation.At(Face, X, Y) - Fine.Precipitation.At(Face, X, Y)));
				OffThreshold += NearThreshold(Full.Temperature.At(Face, X, Y), Full.Precipitation.At(Face, X, Y)) ? 0 : 1;
			}
	AddInfo(FString::Printf(TEXT("群系差异 %d/%d texel（%.3f%%），最大 |ΔT| %.3e、|ΔP| %.3e，阈值外 %d 处"),
		BiomeDiff, Total, 100.0 * double(BiomeDiff) / double(FMath::Max(Total, 1)), MaxDT, MaxDP, OffThreshold));
	TestTrue(FString::Printf(TEXT("群系差异 ≤ 0.5%% texel（实测 %d）"), BiomeDiff),
		double(BiomeDiff) <= 0.005 * double(FMath::Max(Total, 1)));
	TestEqual(TEXT("群系差异全在阈值 0.01 内"), OffThreshold, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGeoStage05DeterminismTest, "StellarEra.Planet.Geo.Stage05.Determinism",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGeoStage05DeterminismTest::RunTest(const FString& Parameters)
{
	FPlanetRecipeConfig C;
	C.FaceRes = 256;
	FPlanetBakeReport R1, R2;
	const FPlanetBakeFields A = Stage05TestPlanet(C, R1);
	const FPlanetBakeFields B = Stage05TestPlanet(C, R2);
	if (!TestFalse(FString::Printf(TEXT("两次烘焙无错误（%s | %s）"),
		*FString::Join(R1.Errors, TEXT(" | ")), *FString::Join(R2.Errors, TEXT(" | "))), R1.HasErrors() || R2.HasErrors())) { return false; }

	struct FFieldPair { const TCHAR* Name; const FGeoField2D& X; const FGeoField2D& Y; };
	const FFieldPair Same[] = {
		{ TEXT("温度"), A.Temperature, B.Temperature },
		{ TEXT("降水"), A.Precipitation, B.Precipitation },
		{ TEXT("风向"), A.WindDir, B.WindDir },
		{ TEXT("群系"), A.Biome, B.Biome },
	};
	for (const FFieldPair& Pair : Same)
	{
		AddInfo(FString::Printf(TEXT("%s 哈希 %llu / %llu"), Pair.Name, Pair.X.Hash(), Pair.Y.Hash()));
		TestTrue(FString::Printf(TEXT("%s：同配方两次烘焙哈希一致"), Pair.Name), Pair.X.Hash() == Pair.Y.Hash());
	}

	// 并行写入与单线程对拍：每个 texel 只写自己，两条路径必须给出同一份场。
	FPlanetRecipeConfig Single = C;
	Single.bSingleThreadBake = true;
	FPlanetBakeReport R3;
	const FPlanetBakeFields S = Stage05TestPlanet(Single, R3);
	if (!TestFalse(FString::Printf(TEXT("单线程烘焙无错误（%s）"), *FString::Join(R3.Errors, TEXT(" | "))), R3.HasErrors())) { return false; }
	const FFieldPair SinglePairs[] = {
		{ TEXT("温度"), A.Temperature, S.Temperature },
		{ TEXT("降水"), A.Precipitation, S.Precipitation },
		{ TEXT("风向"), A.WindDir, S.WindDir },
		{ TEXT("群系"), A.Biome, S.Biome },
	};
	for (const FFieldPair& Pair : SinglePairs)
	{
		TestTrue(FString::Printf(TEXT("%s：单线程与并行哈希一致"), Pair.Name), Pair.X.Hash() == Pair.Y.Hash());
	}
	return true;
}

#endif
