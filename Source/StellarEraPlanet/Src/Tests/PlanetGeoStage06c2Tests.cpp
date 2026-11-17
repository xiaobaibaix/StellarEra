// 阶段 6c-2：盆地沉积与煤。
#include "CoreMinimal.h"
#include "HAL/PlatformTime.h"
#include "Misc/AutomationTest.h"
#include "GeoField2D.h"
#include "GeoMineralRule.h"
#include "GeoMineralRules.h"
#include "GeoMineralScatterer.h"
#include "GeoPrimitive.h"
#include "GeoRng.h"
#include "GeoVolcano.h"
#include "PlanetBakePipeline.h"
#include "PlanetBakeTypes.h"
#include "PlanetRecipeConfig.h"
#include "Stage02Plates.h"
#include "Stage03Terrain.h"
#include "Stage04Magmatism.h"
#include "Stage05Climate.h"
#include "Stage06Erosion.h"
#include "Stage06bExposure.h"

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
		Out.F.BasinMask.Init(Res); Out.F.BasinId.Init(Res); Out.F.FlowAccum.Init(Res);
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

	/** 某编号盆地的最大角半径：同编号格子里离中心最远的那个。 */
	float Stage06c2BasinIdRad(const FPlanetBakeFields& F, int32 Id, const FVector3f& Center)
	{
		float MinDot = 1.f;
		const int32 Res = F.BasinMask.Res;
		for (int32 Face = 0; Face < 6; ++Face)
		for (int32 Y = 0; Y < Res; ++Y)
		for (int32 X = 0; X < Res; ++X)
		{
			if (F.BasinMask.At(Face, X, Y) != 2.f || int32(F.BasinId.At(Face, X, Y)) != Id) { continue; }
			MinDot = FMath::Min(MinDot, FVector3f::DotProduct(F.BasinMask.TexelDir(Face, X, Y), Center));
		}
		return FMath::Acos(FMath::Clamp(MinDot, -1.f, 1.f));
	}

	float Stage06c2BasinRad(const FStage06c2Basin& B)
	{
		return Stage06c2BasinIdRad(B.F, 0, B.Center);
	}

	/** 两个内流盆地：A 在面 0 (2,2)、B 在面 1 (4,4)，各 3×3 格、独立编号。 */
	void Stage06c2TwoBasins(FPlanetBakeFields& F, FVector3f& OutA, FVector3f& OutB)
	{
		const int32 Res = 8;
		F.BasinMask.Init(Res); F.BasinId.Init(Res); F.FlowAccum.Init(Res);
		F.Precipitation.Init(Res); F.Elevation.Init(Res);
		for (int32 Face = 0; Face < 6; ++Face)
		for (int32 Y = 0; Y < Res; ++Y)
		for (int32 X = 0; X < Res; ++X)
		{
			const bool bA = Face == 0 && FMath::Abs(X - 2) <= 1 && FMath::Abs(Y - 2) <= 1;
			const bool bB = Face == 1 && FMath::Abs(X - 4) <= 1 && FMath::Abs(Y - 4) <= 1;
			F.BasinMask.At(Face, X, Y) = (bA || bB) ? 2.f : 0.f;
			F.BasinId.At(Face, X, Y) = bA ? 0.f : (bB ? 1.f : -1.f);
			F.Precipitation.At(Face, X, Y) = 0.1f;
			F.Elevation.At(Face, X, Y) = 50.f;
		}
		OutA = F.BasinMask.TexelDir(0, 2, 2);
		OutB = F.BasinMask.TexelDir(1, 4, 4);
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGeoStage06c2RingOrientationTest, "StellarEra.Planet.Geo.Stage06c2.BasinDeposit.RingOrientation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGeoStage06c2RingOrientationTest::RunTest(const FString& Parameters)
{
	const FStage06c2Basin B = Stage06c2MakeBasin(0.1f);
	FGeoBasinDepositRule Rule;
	// 环带全在盆心 5° 之外，沿用盆心标架时每片都会歪。
	Rule.P.RingInner = 0.5f;
	Rule.P.RingOuter = 1.f;
	FGeoScatterOutput Out;
	Stage06c2Scatter(B, Rule, Out);
	if (!TestTrue(TEXT("有片才谈朝向"), Out.Bodies.Num() > 0)) { return false; }

	const float CosLimit = FMath::Cos(FMath::DegreesToRadians(5.f));
	int32 Bad = 0;
	for (const FGeoPrimitive& Body : Out.Bodies)
	{
		const FVector3f Normal = Body.Rotation.RotateVector(FVector3f::ZAxisVector).GetSafeNormal();
		const float Dot = FVector3f::DotProduct(Normal, Body.Center.GetSafeNormal());
		Bad += Dot < CosLimit ? 1 : 0;
	}
	TestEqual(TEXT("每片的局部 Z 轴与自身地表法线夹角 ≤ 5°"), Bad, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGeoStage06c2TwoBasinsTest, "StellarEra.Planet.Geo.Stage06c2.BasinDeposit.TwoBasins",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGeoStage06c2TwoBasinsTest::RunTest(const FString& Parameters)
{
	FPlanetBakeFields F;
	FVector3f CenterA, CenterB;
	Stage06c2TwoBasins(F, CenterA, CenterB);
	FPlanetRecipeConfig C; C.Radius = 1000.0;

	FGeoBasinDepositRule Rule;
	Rule.P.BodiesPerBasin = 16;
	TArray<FString> Errors;
	FGeoScatterContext SC{ C, F, FGeoRng::Derive(C.Seed, Rule.GetStage(), 0), Errors };
	FGeoScatterOutput Out;
	Rule.Scatter(SC, Out);

	const float RadA = Stage06c2BasinIdRad(F, 0, CenterA);
	const float RadB = Stage06c2BasinIdRad(F, 1, CenterB);
	// 两盆地相隔约 90°，半径之和远小于它，下面按角半径判归属不会有歧义。
	const float Sep = FMath::Acos(FMath::Clamp(FVector3f::DotProduct(CenterA, CenterB), -1.f, 1.f));
	TestTrue(TEXT("两盆地中心足够分开"), Sep > 2.f * FMath::Max(RadA, RadB));

	int32 CountA = 0, CountB = 0, BadOwner = 0, BadNear = 0;
	for (const FGeoPrimitive& Body : Out.Bodies)
	{
		const FVector3f Dir = Body.Center.GetSafeNormal();
		const float AngleA = FMath::Acos(FMath::Clamp(FVector3f::DotProduct(Dir, CenterA), -1.f, 1.f));
		const float AngleB = FMath::Acos(FMath::Clamp(FVector3f::DotProduct(Dir, CenterB), -1.f, 1.f));
		const bool bInA = AngleA <= RadA + 1e-3f, bInB = AngleB <= RadB + 1e-3f;
		if (bInA == bInB) { ++BadOwner; continue; }
		if (bInA) { ++CountA; BadNear += AngleA >= AngleB ? 1 : 0; }
		else { ++CountB; BadNear += AngleB >= AngleA ? 1 : 0; }
	}
	TestEqual(TEXT("总片数 = 2 × BodiesPerBasin"), Out.Bodies.Num(), 2 * Rule.P.BodiesPerBasin);
	TestEqual(TEXT("盆地 A 恰得 BodiesPerBasin 片"), CountA, Rule.P.BodiesPerBasin);
	TestEqual(TEXT("盆地 B 恰得 BodiesPerBasin 片"), CountB, Rule.P.BodiesPerBasin);
	TestEqual(TEXT("每片只落在一个盆地的角半径内"), BadOwner, 0);
	TestEqual(TEXT("每片离自己盆地中心更近"), BadNear, 0);
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGeoStage06c2RecipeBasinTest, "StellarEra.Planet.Geo.Stage06c2.RecipeBasin",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGeoStage06c2RecipeBasinTest::RunTest(const FString& Parameters)
{
	UGeoBasinDepositScatterer* S = NewObject<UGeoBasinDepositScatterer>();
	TestEqual(TEXT("默认矿物是 EvaporiteCarbonate"), S->Mineral, FName(TEXT("EvaporiteCarbonate")));

	// 全字段非默认：抄错任何一项都会露出来。
	S->MaxPrecip = 0.35f;
	S->MinBasinCells = 9;
	S->RingInner = 0.25f;
	S->RingOuter = 0.8f;
	S->BodiesPerBasin = 40;
	S->RadiusRatio = 0.006f;
	S->ThicknessVoxels = 3.5f;
	S->bRequireVolcano = true;
	S->VolcanoRadiusRatio = 0.08f;
	S->Hard = 0.6f;
	const TSharedPtr<IGeoMineralRule> Rule = S->MakeRule();
	if (!TestNotNull(TEXT("撒矿器产出规则"), Rule.Get())) { return false; }
	const FGeoBasinDepositRule* B = static_cast<const FGeoBasinDepositRule*>(Rule.Get());
	TestEqual(TEXT("阶段号 7"), int32(Rule->GetStage()), 7);
	TestEqual(TEXT("规则名 BasinDeposit"), FString(Rule->GetRuleName()), FString(TEXT("BasinDeposit")));
	TestEqual(TEXT("MaxPrecip"), B->P.MaxPrecip, 0.35f);
	TestEqual(TEXT("MinBasinCells"), B->P.MinBasinCells, 9);
	TestEqual(TEXT("RingInner"), B->P.RingInner, 0.25f);
	TestEqual(TEXT("RingOuter"), B->P.RingOuter, 0.8f);
	TestEqual(TEXT("BodiesPerBasin"), B->P.BodiesPerBasin, 40);
	TestEqual(TEXT("RadiusRatio"), B->P.RadiusRatio, 0.006f);
	TestEqual(TEXT("ThicknessVoxels"), B->P.ThicknessVoxels, 3.5f);
	TestEqual(TEXT("bRequireVolcano"), B->P.bRequireVolcano, true);
	TestEqual(TEXT("VolcanoRadiusRatio"), B->P.VolcanoRadiusRatio, 0.08f);
	TestEqual(TEXT("Hard"), B->P.Hard, 0.6f);

	// 哈希探针：10 项各翻一项，HashParams 必变；翻完立刻恢复。
	UGeoBasinDepositScatterer* H = NewObject<UGeoBasinDepositScatterer>();
	auto HashNow = [H]() {
		const TSharedPtr<IGeoMineralRule> R = H->MakeRule();
		return R ? R->HashParams() : uint64(0);
	};
	const uint64 H0 = HashNow();
	if (!TestTrue(TEXT("默认参数哈希非零"), H0 != 0)) { return false; }
	H->MaxPrecip = 0.3f;           TestTrue(TEXT("MaxPrecip 进哈希"), HashNow() != H0); H->MaxPrecip = 0.2f;
	H->MinBasinCells = 7;          TestTrue(TEXT("MinBasinCells 进哈希"), HashNow() != H0); H->MinBasinCells = 6;
	H->RingInner = 0.1f;           TestTrue(TEXT("RingInner 进哈希"), HashNow() != H0); H->RingInner = 0.f;
	H->RingOuter = 0.9f;           TestTrue(TEXT("RingOuter 进哈希"), HashNow() != H0); H->RingOuter = 1.f;
	H->BodiesPerBasin = 25;        TestTrue(TEXT("BodiesPerBasin 进哈希"), HashNow() != H0); H->BodiesPerBasin = 24;
	H->RadiusRatio = 0.005f;       TestTrue(TEXT("RadiusRatio 进哈希"), HashNow() != H0); H->RadiusRatio = 0.004f;
	H->ThicknessVoxels = 3.f;      TestTrue(TEXT("ThicknessVoxels 进哈希"), HashNow() != H0); H->ThicknessVoxels = 2.f;
	H->bRequireVolcano = true;     TestTrue(TEXT("bRequireVolcano 进哈希"), HashNow() != H0); H->bRequireVolcano = false;
	H->VolcanoRadiusRatio = 0.06f; TestTrue(TEXT("VolcanoRadiusRatio 进哈希"), HashNow() != H0); H->VolcanoRadiusRatio = 0.05f;
	H->Hard = 0.4f;                TestTrue(TEXT("Hard 进哈希"), HashNow() != H0); H->Hard = 0.25f;
	TestEqual(TEXT("翻完恢复原哈希"), HashNow(), H0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGeoStage06c2RecipeCoalTest, "StellarEra.Planet.Geo.Stage06c2.RecipeCoal",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGeoStage06c2RecipeCoalTest::RunTest(const FString& Parameters)
{
	UGeoCoalScatterer* S = NewObject<UGeoCoalScatterer>();
	TestEqual(TEXT("默认矿物是 Coal"), S->Mineral, FName(TEXT("Coal")));

	// 全字段非默认：抄错任何一项都会露出来。
	S->MinPrecip = 0.75f;
	S->MaxElevRatio = 0.006f;
	S->MaxSlope = 0.05f;
	S->MaxContinental = 0.4f;
	S->PatchCount = 120;
	S->RadiusRatio = 0.005f;
	S->ThicknessVoxels = 3.f;
	S->DepthVoxels = 2.f;
	S->Hard = 0.35f;
	const TSharedPtr<IGeoMineralRule> Rule = S->MakeRule();
	if (!TestNotNull(TEXT("撒矿器产出规则"), Rule.Get())) { return false; }
	const FGeoCoalRule* C = static_cast<const FGeoCoalRule*>(Rule.Get());
	TestEqual(TEXT("阶段号 7"), int32(Rule->GetStage()), 7);
	TestEqual(TEXT("规则名 Coal"), FString(Rule->GetRuleName()), FString(TEXT("Coal")));
	TestEqual(TEXT("MinPrecip"), C->P.MinPrecip, 0.75f);
	TestEqual(TEXT("MaxElevRatio"), C->P.MaxElevRatio, 0.006f);
	TestEqual(TEXT("MaxSlope"), C->P.MaxSlope, 0.05f);
	TestEqual(TEXT("MaxContinental"), C->P.MaxContinental, 0.4f);
	TestEqual(TEXT("PatchCount"), C->P.PatchCount, 120);
	TestEqual(TEXT("RadiusRatio"), C->P.RadiusRatio, 0.005f);
	TestEqual(TEXT("ThicknessVoxels"), C->P.ThicknessVoxels, 3.f);
	TestEqual(TEXT("DepthVoxels"), C->P.DepthVoxels, 2.f);
	TestEqual(TEXT("Hard"), C->P.Hard, 0.35f);

	// 哈希探针：9 项各翻一项，HashParams 必变。
	UGeoCoalScatterer* H = NewObject<UGeoCoalScatterer>();
	auto HashNow = [H]() {
		const TSharedPtr<IGeoMineralRule> R = H->MakeRule();
		return R ? R->HashParams() : uint64(0);
	};
	const uint64 H0 = HashNow();
	if (!TestTrue(TEXT("默认参数哈希非零"), H0 != 0)) { return false; }
	H->MinPrecip = 0.7f;        TestTrue(TEXT("MinPrecip 进哈希"), HashNow() != H0); H->MinPrecip = 0.6f;
	H->MaxElevRatio = 0.005f;   TestTrue(TEXT("MaxElevRatio 进哈希"), HashNow() != H0); H->MaxElevRatio = 0.004f;
	H->MaxSlope = 0.03f;        TestTrue(TEXT("MaxSlope 进哈希"), HashNow() != H0); H->MaxSlope = 0.04f;
	H->MaxContinental = 0.4f;   TestTrue(TEXT("MaxContinental 进哈希"), HashNow() != H0); H->MaxContinental = 0.65f;
	H->PatchCount = 301;        TestTrue(TEXT("PatchCount 进哈希"), HashNow() != H0); H->PatchCount = 300;
	H->RadiusRatio = 0.005f;    TestTrue(TEXT("RadiusRatio 进哈希"), HashNow() != H0); H->RadiusRatio = 0.004f;
	H->ThicknessVoxels = 3.f;   TestTrue(TEXT("ThicknessVoxels 进哈希"), HashNow() != H0); H->ThicknessVoxels = 2.f;
	H->DepthVoxels = 2.f;       TestTrue(TEXT("DepthVoxels 进哈希"), HashNow() != H0); H->DepthVoxels = 1.f;
	H->Hard = 0.3f;             TestTrue(TEXT("Hard 进哈希"), HashNow() != H0); H->Hard = 0.2f;
	TestEqual(TEXT("翻完恢复原哈希"), HashNow(), H0);
	return true;
}

namespace
{
	struct FStage06c2CoalField
	{
		FPlanetRecipeConfig C;
		FPlanetBakeFields F;
	};

	/**
	 * Res=8、面 0 上 6×6 格低地，其余 50；降水 / 大陆度全场统一。
	 * R=2000：默认 MaxElevRatio=0.004 折合 8 体素，5 体素的低地才过高度闸。
	 */
	FStage06c2CoalField Stage06c2MakeCoal(float Precip, float LowElev, bool bSteep)
	{
		FStage06c2CoalField Out;
		Out.C.Radius = 2000.0;
		const int32 Res = 8;
		Out.F.Precipitation.Init(Res); Out.F.Elevation.Init(Res); Out.F.ContinentalMask.Init(Res);
		for (int32 Face = 0; Face < 6; ++Face)
		for (int32 Y = 0; Y < Res; ++Y)
		for (int32 X = 0; X < Res; ++X)
		{
			const bool bLow = Face == 0 && X >= 1 && X <= 6 && Y >= 1 && Y <= 6;
			// 陡坡用列交替：整格内线性变化，不存在行交替那种斜率恰好为零的鞍点。
			const float Low = bSteep ? ((X % 2) ? 50.f : -50.f) : LowElev;
			Out.F.Precipitation.At(Face, X, Y) = Precip;
			Out.F.ContinentalMask.At(Face, X, Y) = 0.2f;
			Out.F.Elevation.At(Face, X, Y) = bLow ? Low : 50.f;
		}
		return Out;
	}

	void Stage06c2ScatterCoal(const FStage06c2CoalField& Field, const IGeoMineralRule& Rule, FGeoScatterOutput& Out)
	{
		TArray<FString> Errors;
		FGeoScatterContext SC{ Field.C, Field.F, FGeoRng::Derive(Field.C.Seed, Rule.GetStage(), 0), Errors };
		Rule.Scatter(SC, Out);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGeoStage06c2CoalConditionsTest, "StellarEra.Planet.Geo.Stage06c2.CoalConditions",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGeoStage06c2CoalConditionsTest::RunTest(const FString& Parameters)
{
	FGeoCoalRule Rule;
	const FStage06c2CoalField Wet = Stage06c2MakeCoal(0.8f, 5.f, false);
	FGeoScatterOutput Out;
	Stage06c2ScatterCoal(Wet, Rule, Out);
	if (!TestTrue(TEXT("湿热低平近海：有片"), Out.Bodies.Num() > 0)) { return false; }
	const double MaxElev = double(Rule.P.MaxElevRatio) * Wet.C.Radius;
	int32 Bad = 0;
	for (const FGeoPrimitive& Body : Out.Bodies)
	{
		const float Elev = Wet.F.Elevation.Sample(Body.Center.GetSafeNormal());
		Bad += (Elev <= 0.f || double(Elev) > MaxElev) ? 1 : 0;
	}
	TestEqual(TEXT("每片方向高程 ∈ (0, MaxElevRatio·R]"), Bad, 0);

	FGeoScatterOutput Dry, High, Steep;
	Stage06c2ScatterCoal(Stage06c2MakeCoal(0.3f, 5.f, false), Rule, Dry);
	Stage06c2ScatterCoal(Stage06c2MakeCoal(0.8f, 100.f, false), Rule, High);
	Stage06c2ScatterCoal(Stage06c2MakeCoal(0.8f, 5.f, true), Rule, Steep);
	TestEqual(TEXT("降水 0.3 < MinPrecip：0 片"), Dry.Bodies.Num(), 0);
	TestEqual(TEXT("低地高程 100 > MaxElevRatio·R：0 片"), High.Bodies.Num(), 0);
	TestEqual(TEXT("低地内陡坡：0 片"), Steep.Bodies.Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGeoStage06c2CoalDepthTest, "StellarEra.Planet.Geo.Stage06c2.CoalDepth",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGeoStage06c2CoalDepthTest::RunTest(const FString& Parameters)
{
	FGeoCoalRule Rule;
	const FStage06c2CoalField Wet = Stage06c2MakeCoal(0.8f, 5.f, false);
	FGeoScatterOutput Out;
	Stage06c2ScatterCoal(Wet, Rule, Out);
	if (!TestTrue(TEXT("有片才谈深度"), Out.Bodies.Num() > 0)) { return false; }

	int32 Bad = 0;
	for (const FGeoPrimitive& Body : Out.Bodies)
	{
		const FVector3f Dir = Body.Center.GetSafeNormal();
		const double Expected = Wet.C.Radius + double(Wet.F.Elevation.Sample(Dir))
			- double(Rule.P.DepthVoxels) - 0.5 * double(Rule.P.ThicknessVoxels);
		Bad += FMath::Abs(double(Body.Center.Size()) - Expected) > 0.6 ? 1 : 0;
	}
	TestEqual(TEXT("中心半径 = R + Elev - Depth - Thickness/2（±0.6 体素）"), Bad, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGeoStage06c2CoalContinentalTest, "StellarEra.Planet.Geo.Stage06c2.CoalContinental",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGeoStage06c2CoalContinentalTest::RunTest(const FString& Parameters)
{
	FGeoCoalRule Rule;
	FGeoScatterOutput Oceanic;
	Stage06c2ScatterCoal(Stage06c2MakeCoal(0.8f, 5.f, false), Rule, Oceanic);
	TestTrue(TEXT("大陆度 0.2 ≤ MaxContinental=0.65：有片"), Oceanic.Bodies.Num() > 0);

	FStage06c2CoalField Continental = Stage06c2MakeCoal(0.8f, 5.f, false);
	Continental.F.ContinentalMask.Fill([](const FVector3f&) { return 0.9f; }, true);
	FGeoScatterOutput Rejected;
	Stage06c2ScatterCoal(Continental, Rule, Rejected);
	TestEqual(TEXT("大陆度 0.9 > MaxContinental=0.65：0 片"), Rejected.Bodies.Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGeoStage06c2CoalOrientationTest, "StellarEra.Planet.Geo.Stage06c2.CoalOrientation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGeoStage06c2CoalOrientationTest::RunTest(const FString& Parameters)
{
	FGeoCoalRule Rule;
	FGeoScatterOutput Out;
	Stage06c2ScatterCoal(Stage06c2MakeCoal(0.8f, 5.f, false), Rule, Out);
	if (!TestTrue(TEXT("有片才谈朝向"), Out.Bodies.Num() > 0)) { return false; }

	const float CosLimit = FMath::Cos(FMath::DegreesToRadians(5.f));
	int32 Bad = 0;
	for (const FGeoPrimitive& Body : Out.Bodies)
	{
		const FVector3f Normal = Body.Rotation.RotateVector(FVector3f::ZAxisVector).GetSafeNormal();
		Bad += FVector3f::DotProduct(Normal, Body.Center.GetSafeNormal()) < CosLimit ? 1 : 0;
	}
	TestEqual(TEXT("每片局部 Z 轴与自身中心方向夹角 ≤ 5°"), Bad, 0);
	return true;
}

namespace
{
	/**
	 * 按管线的执行方式手动跑一个阶段：Bake 完立刻跑挂在该阶段的撒矿器。
	 * 派生随机流逐位照抄 FPlanetBakePipeline::Bake，测的就是生产那条路径。
	 */
	void Stage06c2BakeOne(const FPlanetRecipeConfig& C, FPlanetBakeFields& F, FPlanetBakeReport& R,
		const IPlanetStage& Stage)
	{
		FPlanetBakeContext Ctx{ C, F, FGeoRng::Derive(C.Seed, Stage.GetStageIndex(), 0), R };
		Stage.Bake(Ctx);
		for (const TSharedPtr<const IGeoMineralRule>& Rule : C.MineralRules)
		{
			if (!Rule || Rule->GetStage() != Stage.GetStageIndex()) { continue; }
			const uint64 Purpose = GeoHash::Combine(GeoHash::Combine(GeoHash::Str("scatter"), Rule->HashParams()),
				uint64(Rule->SeedOffset));
			FGeoScatterContext SC{ C, F, FGeoRng::Derive(C.Seed, Stage.GetStageIndex(), Purpose), R.Errors };
			FGeoScatterOutput Out;
			Rule->Scatter(SC, Out);
			for (FGeoPrimitive& B : Out.Bodies) { B.MineralId = Rule->MineralId; F.Primitives.Add(B); }
			for (FGeoMineralOverlay& O : Out.Overlays) { O.MineralId = Rule->MineralId; F.MineralOverlays.Add(MoveTemp(O)); }
		}
	}

	/**
	 * 2→7 全手动烘焙（含阶段 7 后的撒矿器），结束时场还活着，测试才数得到 BasinMask。
	 * 阶段 1 只写圈层剖面，2–7 的代码都不读它，所以从 2 起。OutStageIndices 交回实际跑过的阶段号。
	 */
	void Stage06c2BakeToExposure(const FPlanetRecipeConfig& C, FPlanetBakeFields& F, FPlanetBakeReport& R,
		TArray<uint32>& OutStageIndices)
	{
		OutStageIndices.Reset();
		const FStage02Plates S2; Stage06c2BakeOne(C, F, R, S2); OutStageIndices.Add(S2.GetStageIndex());
		const FStage03Terrain S3; Stage06c2BakeOne(C, F, R, S3); OutStageIndices.Add(S3.GetStageIndex());
		const FStage04Magmatism S4; Stage06c2BakeOne(C, F, R, S4); OutStageIndices.Add(S4.GetStageIndex());
		const FStage05Climate S5; Stage06c2BakeOne(C, F, R, S5); OutStageIndices.Add(S5.GetStageIndex());
		const FStage06Erosion S6; Stage06c2BakeOne(C, F, R, S6); OutStageIndices.Add(S6.GetStageIndex());
		const FStage06bExposure S7; Stage06c2BakeOne(C, F, R, S7); OutStageIndices.Add(S7.GetStageIndex());
	}

	/** 两条阶段 7 规则：盆地环带蒸发岩（编号 30）与湿热低地煤（编号 31）。 */
	FPlanetRecipeConfig Stage06c2MarginRecipe(TSharedPtr<const IGeoMineralRule>* OutBasin = nullptr)
	{
		FPlanetRecipeConfig C; C.FaceRes = 512; C.bSingleThreadBake = true;
		TSharedPtr<FGeoBasinDepositRule> Basin = MakeShared<FGeoBasinDepositRule>();
		Basin->MineralId = 30;
		Basin->P.RingOuter = 0.6f;
		TSharedPtr<FGeoCoalRule> Coal = MakeShared<FGeoCoalRule>();
		Coal->MineralId = 31;
		C.MineralRules = { Basin, Coal };
		if (OutBasin) { *OutBasin = Basin; }
		return C;
	}

	int32 Stage06c2CountBodies(const TArray<FGeoPrimitive>& P, uint8 MineralId)
	{
		int32 N = 0;
		for (const FGeoPrimitive& B : P) { N += B.MineralId == MineralId ? 1 : 0; }
		return N;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGeoStage06c2PipelineTest, "StellarEra.Planet.Geo.Stage06c2.Pipeline",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGeoStage06c2PipelineTest::RunTest(const FString& Parameters)
{
	TSharedPtr<const IGeoMineralRule> BasinOwner;
	const FPlanetRecipeConfig C = Stage06c2MarginRecipe(&BasinOwner);

	const double T0 = FPlatformTime::Seconds();
	FPlanetBakeReport R;
	const TSharedPtr<const FPlanetSnapshot> Snap = FPlanetBakePipeline::MakeDefault().Bake(C, R);
	const double Seconds = FPlatformTime::Seconds() - T0;
	if (!TestFalse(FString::Printf(TEXT("整管线无错误（%s）"), *FString::Join(R.Errors, TEXT(" | "))), R.HasErrors())) { return false; }
	if (!TestNotNull(TEXT("产出快照"), Snap.Get())) { return false; }

	const TArray<FGeoPrimitive>& Prims = Snap->Primitives.GetPrimitives();
	const int32 Evaporites = Stage06c2CountBodies(Prims, 30);
	const int32 Coals = Stage06c2CountBodies(Prims, 31);

	// 快照里没有 BasinMask：同配方再手动跑一遍 2→7，留住场才数得了内流盆地格。
	FPlanetBakeFields F; FPlanetBakeReport ManualR; TArray<uint32> UsedStages;
	const double T1 = FPlatformTime::Seconds();
	Stage06c2BakeToExposure(C, F, ManualR, UsedStages);
	const double ManualSeconds = FPlatformTime::Seconds() - T1;
	if (!TestFalse(FString::Printf(TEXT("手动烘焙无错误（%s）"), *FString::Join(ManualR.Errors, TEXT(" | "))), ManualR.HasErrors())) { return false; }

	// 阶段 1 只写圈层剖面，手动链从 2 起；其余必须与默认管线逐项对齐，否则管线加了阶段而这里没跟上。
	// 管线必须落成具名对象：Sorted() 交出的裸指针指进它的 Stages，临时管线一析构就悬垂。
	FPlanetBakePipeline Pipeline = FPlanetBakePipeline::MakeDefault();
	TArray<uint32> PipelineStages;
	for (const IPlanetStage* S : Pipeline.Sorted()) { PipelineStages.Add(S->GetStageIndex()); }
	const bool bSkipsOnlyStage1 = PipelineStages.Num() > 0 && PipelineStages[0] == 1;
	if (bSkipsOnlyStage1) { PipelineStages.RemoveAt(0); }
	TestTrue(TEXT("默认管线以阶段 1（圈层剖面）开头，手动链只跳过它"), bSkipsOnlyStage1);
	int32 StageMismatch = 0;
	for (int32 i = 0; i < UsedStages.Num() || i < PipelineStages.Num(); ++i)
	{
		StageMismatch += (UsedStages.IsValidIndex(i) && PipelineStages.IsValidIndex(i) && UsedStages[i] == PipelineStages[i]) ? 0 : 1;
	}
	TestEqual(TEXT("手动链与默认管线逐阶段号一致（管线新增阶段必须同步到这里）"), StageMismatch, 0);

	// 空场会让下面的「无内流盆地」跳过分支静默通过；先把分辨率钉死。
	if (!TestEqual(TEXT("BasinMask 分辨率 = max(64, FaceRes/4)"), F.BasinMask.Res, FMath::Max(64, C.FaceRes / 4))) { return false; }
	const int32 Res = F.BasinMask.Res;
	int32 Endorheic = 0;
	for (int32 Face = 0; Face < 6; ++Face)
	for (int32 Y = 0; Y < Res; ++Y)
	for (int32 X = 0; X < Res; ++X)
	{
		Endorheic += F.BasinMask.At(Face, X, Y) == 2.f ? 1 : 0;
	}
	const int32 ManualEvaporites = Stage06c2CountBodies(F.Primitives, 30);
	const int32 ManualCoals = Stage06c2CountBodies(F.Primitives, 31);

	AddInfo(FString::Printf(TEXT("FaceRes %d 整管线 %.2f s，原语 %d（蒸发岩 %d，煤 %d）；手动链 %.2f s，内流盆地格 %d（蒸发岩 %d，煤 %d）"),
		C.FaceRes, Seconds, Prims.Num(), Evaporites, Coals, ManualSeconds, Endorheic, ManualEvaporites, ManualCoals));
	TestTrue(TEXT("有煤产出"), Coals > 0);
	if (Endorheic > 0)
	{
		TestTrue(TEXT("有内流盆地格：盆地蒸发岩产出 > 0"), Evaporites > 0);
	}
	else
	{
		// 规范 §9：本种子没有内流盆地时蒸发岩为 0 是合法结果，不判失败。
		AddInfo(TEXT("本种子没有内流盆地格，蒸发岩按规范 §9 跳过（不判失败）"));
	}
	TestEqual(TEXT("手动链煤片数与管线一致"), ManualCoals, Coals);
	TestEqual(TEXT("手动链蒸发岩片数与管线一致"), ManualEvaporites, Evaporites);
	TestTrue(TEXT("盆地沉积规则阶段号是 7"), BasinOwner.IsValid() && BasinOwner->GetStage() == 7);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGeoStage06c2DeterminismTest, "StellarEra.Planet.Geo.Stage06c2.Determinism",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGeoStage06c2DeterminismTest::RunTest(const FString& Parameters)
{
	FPlanetRecipeConfig C = Stage06c2MarginRecipe();
	// 三次烘焙只翻 bSingleThreadBake，其余字段原样：哈希不同只可能是并行度改变结果。
	auto Bake = [&C](bool bSingle, double& OutSeconds)
	{
		C.bSingleThreadBake = bSingle;
		FPlanetBakeReport R;
		const double T0 = FPlatformTime::Seconds();
		TSharedPtr<const FPlanetSnapshot> Out = FPlanetBakePipeline::MakeDefault().Bake(C, R);
		OutSeconds = FPlatformTime::Seconds() - T0;
		return R.HasErrors() ? TSharedPtr<const FPlanetSnapshot>() : Out;
	};

	double T1 = 0.0, T2 = 0.0, T3 = 0.0;
	const TSharedPtr<const FPlanetSnapshot> S1 = Bake(true, T1);
	const TSharedPtr<const FPlanetSnapshot> S2 = Bake(true, T2);
	const TSharedPtr<const FPlanetSnapshot> S3 = Bake(false, T3);
	if (!TestTrue(TEXT("单线程两次烘焙都成功"), S1.IsValid() && S2.IsValid())) { return false; }
	if (!TestTrue(TEXT("多线程烘焙成功"), S3.IsValid())) { return false; }

	const uint64 H1 = S1->Primitives.Hash();
	const TArray<FGeoPrimitive>& Prims = S1->Primitives.GetPrimitives();
	AddInfo(FString::Printf(TEXT("原语 %d（蒸发岩 %d，煤 %d）哈希 %016llx；单线程 %.2f/%.2f s，多线程 %.2f s"),
		Prims.Num(), Stage06c2CountBodies(Prims, 30), Stage06c2CountBodies(Prims, 31),
		static_cast<unsigned long long>(H1), T1, T2, T3));
	TestTrue(TEXT("煤非空，哈希对比不是空转"), Stage06c2CountBodies(Prims, 31) > 0);
	TestTrue(TEXT("哈希非零"), H1 != 0);
	TestEqual(TEXT("同配方两次烘焙原语哈希一致"), int64(S2->Primitives.Hash()), int64(H1));
	TestEqual(TEXT("单线程与多线程原语哈希一致"), int64(S3->Primitives.Hash()), int64(H1));
	return true;
}

#endif
