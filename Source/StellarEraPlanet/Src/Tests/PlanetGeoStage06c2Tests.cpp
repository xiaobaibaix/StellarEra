// 阶段 6c-2：盆地沉积与煤。
#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "GeoField2D.h"
#include "GeoMineralRule.h"
#include "GeoMineralRules.h"
#include "GeoPrimitive.h"
#include "GeoRng.h"
#include "GeoVolcano.h"
#include "PlanetBakeTypes.h"
#include "PlanetRecipeConfig.h"
#include "Stage02Plates.h"
#include "Stage03Terrain.h"
#include "Stage04Magmatism.h"
#include "Stage05Climate.h"
#include "Stage06Erosion.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	FPlanetBakeFields Stage06c2UpToHydro(const FPlanetRecipeConfig& C, FPlanetBakeReport& R)
	{
		FPlanetBakeFields F;
		FPlanetBakeContext C2{ C, F, FGeoRng::Derive(C.Seed, 2, 0), R }; FStage02Plates().Bake(C2);
		FPlanetBakeContext C3{ C, F, FGeoRng::Derive(C.Seed, 3, 0), R }; FStage03Terrain().Bake(C3);
		FPlanetBakeContext C4{ C, F, FGeoRng::Derive(C.Seed, 4, 0), R }; FStage04Magmatism().Bake(C4);
		FPlanetBakeContext C5{ C, F, FGeoRng::Derive(C.Seed, 5, 0), R }; FStage05Climate().Bake(C5);
		FPlanetBakeContext C6{ C, F, FGeoRng::Derive(C.Seed, 6, 0), R }; FStage06Erosion().Bake(C6);
		return F;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGeoStage06c2BasinIdTest, "StellarEra.Planet.Geo.Stage06c2.BasinId",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGeoStage06c2BasinIdTest::RunTest(const FString& Parameters)
{
	FPlanetRecipeConfig C; C.FaceRes = 256;
	FPlanetBakeReport R;
	const FPlanetBakeFields F = Stage06c2UpToHydro(C, R);
	if (!TestFalse(FString::Printf(TEXT("无错误（%s）"), *FString::Join(R.Errors, TEXT(" | "))), R.HasErrors())) { return false; }
	if (!TestEqual(TEXT("BasinId 与 BasinMask 同分辨率"), F.BasinId.Res, F.BasinMask.Res)) { return false; }
	const int32 Res = F.BasinId.Res;
	int32 BadMaskWithoutId = 0, BadIdOutside = 0, Endorheic = 0, Lakes = 0;
	for (int32 Face = 0; Face < 6; ++Face)
	for (int32 Y = 0; Y < Res; ++Y)
	for (int32 X = 0; X < Res; ++X)
	{
		const float M = F.BasinMask.At(Face, X, Y);
		const float Id = F.BasinId.At(Face, X, Y);
		// 湖盆（1）与内流盆地（2）都算洼地：BasinId 按洼地编号，湖盆不因有出口就没有编号。
		if (M > 0.f) { Endorheic += M == 2.f ? 1 : 0; Lakes += M == 1.f ? 1 : 0; BadMaskWithoutId += Id < 0.f ? 1 : 0; }
		else { BadIdOutside += Id >= 0.f ? 1 : 0; }
	}
	AddInfo(FString::Printf(TEXT("内流格 %d，湖盆格 %d"), Endorheic, Lakes));
	TestTrue(TEXT("有内流盆地格（否则整套断言空转）"), Endorheic > 0);
	TestTrue(TEXT("有湖盆格（湖盆同样带编号，否则编号覆盖只有一半）"), Lakes > 0);
	TestEqual(TEXT("洼地格子都有编号"), BadMaskWithoutId, 0);
	TestEqual(TEXT("非洼地格编号为 -1"), BadIdOutside, 0);
	return true;
}

namespace
{
	// Res=8、面 0 上以 (4,4) 为中心、半径 1 格的内流盆地；降水整体可调。
	struct FStage06c2Basin
	{
		FPlanetRecipeConfig C;
		FPlanetBakeFields F;
		FVector3f Center;
	};

	FStage06c2Basin Stage06c2MakeBasin(float Precip)
	{
		FStage06c2Basin Out;
		Out.C.Radius = 1000.0;
		const int32 Res = 8;
		Out.F.BasinMask.Init(Res); Out.F.BasinId.Init(Res);
		Out.F.Precipitation.Init(Res); Out.F.Elevation.Init(Res);
		Out.F.ContinentalMask.Init(Res);
		for (int32 Face = 0; Face < 6; ++Face)
		for (int32 Y = 0; Y < Res; ++Y)
		for (int32 X = 0; X < Res; ++X)
		{
			const bool bIn = Face == 0 && FMath::Abs(X - 4) <= 1 && FMath::Abs(Y - 4) <= 1;
			Out.F.BasinMask.At(Face, X, Y) = bIn ? 2.f : 0.f;
			Out.F.BasinId.At(Face, X, Y) = bIn ? 0.f : -1.f;
			Out.F.Precipitation.At(Face, X, Y) = Precip;
			Out.F.Elevation.At(Face, X, Y) = 50.f;
			Out.F.ContinentalMask.At(Face, X, Y) = 0.9f;
		}
		Out.Center = Out.F.BasinMask.TexelDir(0, 4, 4);
		return Out;
	}

	/** 盆地 0 的最大角半径：同编号格子里离中心最远的那个。 */
	float Stage06c2BasinRad(const FStage06c2Basin& B)
	{
		float MinDot = 1.f;
		const int32 Res = B.F.BasinMask.Res;
		for (int32 Face = 0; Face < 6; ++Face)
		for (int32 Y = 0; Y < Res; ++Y)
		for (int32 X = 0; X < Res; ++X)
		{
			if (B.F.BasinMask.At(Face, X, Y) != 2.f || int32(B.F.BasinId.At(Face, X, Y)) != 0) { continue; }
			MinDot = FMath::Min(MinDot, FVector3f::DotProduct(B.F.BasinMask.TexelDir(Face, X, Y), B.Center));
		}
		return FMath::Acos(FMath::Clamp(MinDot, -1.f, 1.f));
	}

	void Stage06c2Scatter(const FStage06c2Basin& B, const IGeoMineralRule& Rule, FGeoScatterOutput& Out)
	{
		TArray<FString> Errors;
		FGeoScatterContext SC{ B.C, B.F, FGeoRng::Derive(B.C.Seed, Rule.GetStage(), 0), Errors };
		Rule.Scatter(SC, Out);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGeoStage06c2BasinDepositRingBandTest, "StellarEra.Planet.Geo.Stage06c2.BasinDeposit.RingBand",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGeoStage06c2BasinDepositRingBandTest::RunTest(const FString& Parameters)
{
	const FStage06c2Basin B = Stage06c2MakeBasin(0.1f);
	FGeoBasinDepositRule Rule;
	Rule.P.RingInner = 0.3f;
	Rule.P.RingOuter = 0.6f;
	Rule.P.BodiesPerBasin = 32;
	FGeoScatterOutput Out;
	Stage06c2Scatter(B, Rule, Out);
	if (!TestEqual(TEXT("32 片"), Out.Bodies.Num(), 32)) { return false; }

	const float Rad = Stage06c2BasinRad(B);
	int32 Bad = 0;
	for (const FGeoPrimitive& Body : Out.Bodies)
	{
		const float Angle = FMath::Acos(FMath::Clamp(FVector3f::DotProduct(Body.Center.GetSafeNormal(), B.Center), -1.f, 1.f));
		Bad += (Angle / Rad < 0.25f || Angle / Rad > 0.65f) ? 1 : 0;
	}
	TestEqual(TEXT("每片与盆心的夹角 / 盆半径 ∈ [0.25, 0.65]"), Bad, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGeoStage06c2BasinDepositRingPrecipTest, "StellarEra.Planet.Geo.Stage06c2.BasinDeposit.RingPrecip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGeoStage06c2BasinDepositRingPrecipTest::RunTest(const FString& Parameters)
{
	FGeoBasinDepositRule Rule;
	FGeoScatterOutput Wet, Dry;
	Stage06c2Scatter(Stage06c2MakeBasin(0.5f), Rule, Wet);
	Stage06c2Scatter(Stage06c2MakeBasin(0.1f), Rule, Dry);
	TestEqual(TEXT("Precip=0.5 > MaxPrecip=0.2：0 片"), Wet.Bodies.Num(), 0);
	TestTrue(TEXT("Precip=0.1：有片"), Dry.Bodies.Num() > 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGeoStage06c2BasinDepositRingVolcanoTest, "StellarEra.Planet.Geo.Stage06c2.BasinDeposit.RingVolcano",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGeoStage06c2BasinDepositRingVolcanoTest::RunTest(const FString& Parameters)
{
	FGeoBasinDepositRule Rule;
	Rule.P.bRequireVolcano = true;
	FStage06c2Basin B = Stage06c2MakeBasin(0.1f);
	FGeoScatterOutput Empty;
	Stage06c2Scatter(B, Rule, Empty);
	TestEqual(TEXT("没有活火山：0 片"), Empty.Bodies.Num(), 0);

	FGeoVolcano V;
	V.Dir = B.Center;
	V.bActive = true;
	B.F.Volcanoes.Add(V);
	FGeoScatterOutput Near;
	Stage06c2Scatter(B, Rule, Near);
	TestTrue(TEXT("盆心附近有活火山：有片"), Near.Bodies.Num() > 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGeoStage06c2BasinDepositRingShapeTest, "StellarEra.Planet.Geo.Stage06c2.BasinDeposit.RingShape",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGeoStage06c2BasinDepositRingShapeTest::RunTest(const FString& Parameters)
{
	const FStage06c2Basin B = Stage06c2MakeBasin(0.1f);
	FGeoBasinDepositRule Rule;
	FGeoScatterOutput Out;
	Stage06c2Scatter(B, Rule, Out);
	if (!TestTrue(TEXT("有片才谈形状"), Out.Bodies.Num() > 0)) { return false; }

	const float HalfThick = FMath::Max(0.5f * Rule.P.ThicknessVoxels, 0.5f);
	const double ExpectedRadius = B.C.Radius + 50.0 - 0.5 * double(Rule.P.ThicknessVoxels);
	int32 BadKind = 0, BadThick = 0, BadCenter = 0;
	for (const FGeoPrimitive& Body : Out.Bodies)
	{
		BadKind += Body.Kind != EGeoPrimitiveKind::OrientedBlob ? 1 : 0;
		BadThick += FMath::Abs(Body.Radii.Z - HalfThick) > 0.01f ? 1 : 0;
		BadCenter += FMath::Abs(double(Body.Center.Size()) - ExpectedRadius) > 0.5 ? 1 : 0;
	}
	TestEqual(TEXT("全部 OrientedBlob"), BadKind, 0);
	TestEqual(TEXT("厚度恰为 max(ThicknessVoxels/2, 0.5)（2 → 1）"), BadThick, 0);
	TestEqual(TEXT("中心半径 = R + Elev - ThicknessVoxels/2（1049）"), BadCenter, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGeoStage06c2BasinDepositHashTest, "StellarEra.Planet.Geo.Stage06c2.BasinDeposit.Hash",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGeoStage06c2BasinDepositHashTest::RunTest(const FString& Parameters)
{
	FGeoBasinDepositRule Base;
	const uint64 H = Base.HashParams();
	const auto Differs = [&Base, H](auto Mutate)
	{
		FGeoBasinDepositRule R = Base;
		Mutate(R.P);
		return R.HashParams() != H;
	};
	TestTrue(TEXT("MaxPrecip"), Differs([](FGeoBasinDepositParams& P) { P.MaxPrecip += 0.1f; }));
	TestTrue(TEXT("MinBasinCells"), Differs([](FGeoBasinDepositParams& P) { P.MinBasinCells += 1; }));
	TestTrue(TEXT("RingInner"), Differs([](FGeoBasinDepositParams& P) { P.RingInner += 0.1f; }));
	TestTrue(TEXT("RingOuter"), Differs([](FGeoBasinDepositParams& P) { P.RingOuter -= 0.1f; }));
	TestTrue(TEXT("BodiesPerBasin"), Differs([](FGeoBasinDepositParams& P) { P.BodiesPerBasin += 1; }));
	TestTrue(TEXT("RadiusRatio"), Differs([](FGeoBasinDepositParams& P) { P.RadiusRatio *= 2.f; }));
	TestTrue(TEXT("ThicknessVoxels"), Differs([](FGeoBasinDepositParams& P) { P.ThicknessVoxels += 1.f; }));
	TestTrue(TEXT("bRequireVolcano"), Differs([](FGeoBasinDepositParams& P) { P.bRequireVolcano = true; }));
	TestTrue(TEXT("VolcanoRadiusRatio"), Differs([](FGeoBasinDepositParams& P) { P.VolcanoRadiusRatio *= 2.f; }));
	TestTrue(TEXT("Hard"), Differs([](FGeoBasinDepositParams& P) { P.Hard += 0.1f; }));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGeoStage06c2BasinIdMatchTest, "StellarEra.Planet.Geo.Stage06c2.BasinIdMatch",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGeoStage06c2BasinIdMatchTest::RunTest(const FString& Parameters)
{
	FPlanetRecipeConfig C; C.FaceRes = 256;
	// 刻谷会就地削低 Elevation；关掉它，重采样才还是 SolveFlow 当初的输入，逐格对拍才成立。
	C.Hydro.bRivers = false;
	FPlanetBakeReport R;
	const FPlanetBakeFields F = Stage06c2UpToHydro(C, R);
	if (!TestFalse(FString::Printf(TEXT("无错误（%s）"), *FString::Join(R.Errors, TEXT(" | "))), R.HasErrors())) { return false; }

	// 与 Stage06Erosion::Bake 的宏观采样逐行一致。
	const int32 Res = FMath::Max(64, C.FaceRes / 4);
	if (!TestEqual(TEXT("BasinId 分辨率与阶段 6 一致"), F.BasinId.Res, Res)) { return false; }
	Stage06::FHydroGrid Grid;
	Stage06::BuildGrid(Res, Grid);
	const int32 N = Grid.Num();
	TArray<float> E, Pr;
	E.SetNumUninitialized(N); Pr.SetNumUninitialized(N);
	for (int32 i = 0; i < N; ++i)
	{
		E[i] = F.Elevation.Sample(Grid.Dir[i]);
		Pr[i] = F.Precipitation.Sample(Grid.Dir[i]);
	}
	Stage06::FHydroFlow Flow;
	Stage06::SolveFlow(Grid, E, Pr, float(C.Radius), C.Hydro.FlatSlopeRatio, C.Hydro.EndorheicPrecip, Flow);

	int32 Mismatch = 0;
	for (int32 i = 0; i < N; ++i)
	{
		const int32 Face = i / (Res * Res), Y = (i / Res) % Res, X = i % Res;
		if (F.BasinId.At(Face, X, Y) != float(Flow.BasinId[i])) { ++Mismatch; }
	}
	AddInfo(FString::Printf(TEXT("洼地数 %d，逐格不符 %d"), Flow.Exits.Num(), Mismatch));
	TestEqual(TEXT("BasinId 与重新求解的 SolveFlow 逐格一致"), Mismatch, 0);
	return true;
}

#endif
