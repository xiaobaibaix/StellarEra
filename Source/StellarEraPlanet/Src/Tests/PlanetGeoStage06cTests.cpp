// 阶段 6c-1：水系拓扑导出与砂矿规则。
#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "GeoField2D.h"
#include "GeoMineralRule.h"
#include "GeoMineralRules.h"
#include "GeoPrimitive.h"
#include "GeoRng.h"
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
	FPlanetBakeFields Stage06cUpToHydro(const FPlanetRecipeConfig& C, FPlanetBakeReport& R)
	{
		FPlanetBakeFields F;
		FPlanetBakeContext C2{ C, F, FGeoRng::Derive(C.Seed, 2, 0), R }; FStage02Plates().Bake(C2);
		FPlanetBakeContext C3{ C, F, FGeoRng::Derive(C.Seed, 3, 0), R }; FStage03Terrain().Bake(C3);
		FPlanetBakeContext C4{ C, F, FGeoRng::Derive(C.Seed, 4, 0), R }; FStage04Magmatism().Bake(C4);
		FPlanetBakeContext C5{ C, F, FGeoRng::Derive(C.Seed, 5, 0), R }; FStage05Climate().Bake(C5);
		FPlanetBakeContext C6{ C, F, FGeoRng::Derive(C.Seed, 6, 0), R }; FStage06Erosion().Bake(C6);
		return F;
	}

	/** 编号布局与阶段 6 一致：(Face * Res + Y) * Res + X。 */
	int32 Stage06cCellIndex(const FGeoField2D& F, int32 Face, int32 X, int32 Y)
	{
		return (Face * F.Res + Y) * F.Res + X;
	}

	void Stage06cCellFaceXY(const FGeoField2D& F, int32 Idx, int32& Face, int32& X, int32& Y)
	{
		const int32 Res = F.Res;
		Face = Idx / (Res * Res);
		Y = (Idx / Res) % Res;
		X = Idx % Res;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGeoStage06cFlowNextTest, "StellarEra.Planet.Geo.Stage06c.FlowNext",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGeoStage06cFlowNextTest::RunTest(const FString& Parameters)
{
	FPlanetRecipeConfig C; C.FaceRes = 256;
	FPlanetBakeReport R;
	const FPlanetBakeFields F = Stage06cUpToHydro(C, R);
	if (!TestFalse(FString::Printf(TEXT("无错误（%s）"), *FString::Join(R.Errors, TEXT(" | "))), R.HasErrors())) { return false; }
	if (!TestEqual(TEXT("FlowNext 与 FlowAccum 同分辨率"), F.FlowNext.Res, F.FlowAccum.Res)) { return false; }

	const int32 Res = F.FlowNext.Res, N = 6 * Res * Res;
	int32 NonZero = 0, BadIndex = 0, Cycles = 0, OceanWithDown = 0;
	for (int32 Idx = 0; Idx < N; ++Idx)
	{
		int32 Face, X, Y; Stage06cCellFaceXY(F.FlowNext, Idx, Face, X, Y);
		const int32 Next = int32(F.FlowNext.At(Face, X, Y)) - 1;
		const bool bLand = F.Elevation.Sample(F.FlowNext.TexelDir(Face, X, Y)) > 0.f;
		if (Next >= 0) { ++NonZero; }
		BadIndex += Next >= N ? 1 : 0;
		OceanWithDown += (!bLand && Next >= 0) ? 1 : 0;
		if (Next < 0 || Next >= N) { continue; }
		// 沿下游走，超过 N 步必成环（D8 不允许环）。
		int32 Cur = Next, Steps = 0;
		while (Cur >= 0 && Cur < N && Steps <= N)
		{
			int32 F2, X2, Y2; Stage06cCellFaceXY(F.FlowNext, Cur, F2, X2, Y2);
			Cur = int32(F.FlowNext.At(F2, X2, Y2)) - 1;
			++Steps;
		}
		Cycles += (Steps > N) ? 1 : 0;
	}
	AddInfo(FString::Printf(TEXT("格 %d，有下游 %d，海洋有下游 %d"), N, NonZero, OceanWithDown));
	TestEqual(TEXT("下游格编号在范围内"), BadIndex, 0);
	TestEqual(TEXT("顺流必在 N 步内终止（无环）"), Cycles, 0);
	TestTrue(TEXT("有河道格带下游"), NonZero > 0);
	return true;
}

namespace
{
	// 方向 → 格子编号：取整与 FGeoField2D::SampleNearest 一致，测试与规则才读同一格。
	int32 Stage06cCellAtDir(const FGeoField2D& Field, const FVector3f& Dir)
	{
		int32 Face; float U, V;
		FGeoField2D::DirToFace(Dir, Face, U, V);
		const int32 R = Field.Res;
		const int32 X = FMath::Clamp(int32((U + 1.f) * 0.5f * float(R)), 0, R - 1);
		const int32 Y = FMath::Clamp(int32((V + 1.f) * 0.5f * float(R)), 0, R - 1);
		return Stage06cCellIndex(Field, Face, X, Y);
	}

	// Res=16、面 0 的 Y=8 一行格子串成一条河：0..Land-1 陆上，其余海洋。
	struct FStage06cChain
	{
		FPlanetRecipeConfig C;
		FPlanetBakeFields F;
	};

	FStage06cChain Stage06cMakeChain(int32 LandCells)
	{
		FStage06cChain Out;
		Out.C.Radius = 1000.0;
		const int32 Res = 16, Y = 8;
		Out.F.FlowAccum.Init(Res);
		Out.F.FlowNext.Init(Res);
		Out.F.Elevation.Init(Res);
		const int32 Land = FMath::Clamp(LandCells, 1, Res - 1);
		for (int32 X = 0; X < Res; ++X)
		{
			const bool bLand = X < Land;
			Out.F.Elevation.At(0, X, Y) = bLand ? 100.f : -100.f;
			Out.F.FlowAccum.At(0, X, Y) = bLand ? 0.001f : 0.f;
			Out.F.FlowNext.At(0, X, Y) = (X + 1 < Res) ? float(((Y * Res) + (X + 1)) + 1) : 0.f;
		}
		for (int32 X = Land; X < Res; ++X)
		{
			for (int32 Y2 = 0; Y2 < Res; ++Y2)
			{
				Out.F.Elevation.At(0, X, Y2) = -100.f;
				Out.F.FlowAccum.At(0, X, Y2) = 0.f;
			}
		}
		// 源矿：上游第 3 个格子，出露满。
		const FVector3f Dir = Out.F.FlowAccum.TexelDir(0, 2, Y);
		FGeoPrimitive Src = FGeoPrimitive::MakeBlob(Dir * 1010.f, FVector3f(20.f, 20.f, 20.f), 0.1f);
		Src.MineralId = 5;
		Src.Exposure = 255;
		Out.F.Primitives.Add(Src);
		return Out;
	}

	void Stage06cScatter(const FPlanetRecipeConfig& C, const FPlanetBakeFields& F, const IGeoMineralRule& Rule,
		FGeoScatterOutput& Out)
	{
		TArray<FString> Errors;
		FGeoScatterContext SC{ C, F, FGeoRng::Derive(C.Seed, Rule.GetStage(), 0), Errors };
		Rule.Scatter(SC, Out);
	}

	/** 每片中心落在哪一格：中心是 Dir 的正倍数，方向反查即可。 */
	int32 Stage06cBodyCell(const FGeoField2D& Field, const FGeoPrimitive& Body)
	{
		return Stage06cCellAtDir(Field, Body.Center.GetSafeNormal());
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGeoStage06cPlacerNoSourceTest, "StellarEra.Planet.Geo.Stage06c.Placer.NoSource",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGeoStage06cPlacerNoSourceTest::RunTest(const FString& Parameters)
{
	const FStage06cChain Chain = Stage06cMakeChain(12);
	FGeoPlacerRule Rule;
	FGeoScatterOutput Out;
	Stage06cScatter(Chain.C, Chain.F, Rule, Out);
	TestEqual(TEXT("源矿列表为空：0 片"), Out.Bodies.Num(), 0);

	Rule.P.SourceMineralIds.Add(7);   // 场上只有矿物 5
	FGeoScatterOutput Out2;
	Stage06cScatter(Chain.C, Chain.F, Rule, Out2);
	TestEqual(TEXT("场上没有该矿：0 片"), Out2.Bodies.Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGeoStage06cPlacerFollowFlowTest, "StellarEra.Planet.Geo.Stage06c.Placer.FollowFlow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGeoStage06cPlacerFollowFlowTest::RunTest(const FString& Parameters)
{
	const FStage06cChain Chain = Stage06cMakeChain(12);
	FGeoPlacerRule Rule;
	Rule.P.SourceMineralIds.Add(5);
	Rule.P.StepStride = 1;
	FGeoScatterOutput Out;
	Stage06cScatter(Chain.C, Chain.F, Rule, Out);
	if (!TestTrue(TEXT("顺流撒到砂矿"), Out.Bodies.Num() > 0)) { return false; }

	int32 Bad = 0;
	for (const FGeoPrimitive& Body : Out.Bodies)
	{
		int32 Face, X, Y;
		Stage06cCellFaceXY(Chain.F.FlowAccum, Stage06cBodyCell(Chain.F.FlowAccum, Body), Face, X, Y);
		if (Face != 0 || Y != 8) { ++Bad; continue; }
		if (Chain.F.Elevation.At(Face, X, Y) <= 0.f) { ++Bad; continue; }
		if (Chain.F.FlowAccum.At(Face, X, Y) < Chain.C.Hydro.RiverThreshold) { ++Bad; }
	}
	TestEqual(TEXT("每片都在河格（面 0 / Y=8 / 陆地 / 汇流过阈值）"), Bad, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGeoStage06cPlacerGradeDecayTest, "StellarEra.Planet.Geo.Stage06c.Placer.GradeDecay",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGeoStage06cPlacerGradeDecayTest::RunTest(const FString& Parameters)
{
	const FStage06cChain Chain = Stage06cMakeChain(12);
	FGeoPlacerRule Rule;
	Rule.P.SourceMineralIds.Add(5);
	Rule.P.StepStride = 1;
	Rule.GradeMin = 40;
	Rule.GradeMax = 230;
	FGeoScatterOutput Out;
	Stage06cScatter(Chain.C, Chain.F, Rule, Out);
	if (!TestTrue(TEXT("至少两片才谈得上衰减"), Out.Bodies.Num() >= 2)) { return false; }

	bool bMonotonic = true;
	for (int32 i = 1; i < Out.Bodies.Num(); ++i)
	{
		bMonotonic &= Out.Bodies[i].Grade <= Out.Bodies[i - 1].Grade;
	}
	TestTrue(TEXT("品位沿流程单调不增"), bMonotonic);
	TestTrue(TEXT("首片 ≥ 200"), Out.Bodies[0].Grade >= 200);
	TestTrue(TEXT("末片 < 首片且 ≥ 40"), Out.Bodies.Last().Grade < Out.Bodies[0].Grade && Out.Bodies.Last().Grade >= 40);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGeoStage06cPlacerMouthTest, "StellarEra.Planet.Geo.Stage06c.Placer.Mouth",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGeoStage06cPlacerMouthTest::RunTest(const FString& Parameters)
{
	const FStage06cChain Chain = Stage06cMakeChain(12);
	FGeoPlacerRule Plain;
	Plain.P.SourceMineralIds.Add(5);
	FGeoScatterOutput PlainOut;
	Stage06cScatter(Chain.C, Chain.F, Plain, PlainOut);
	TestTrue(TEXT("非嘴模式片数 > 3"), PlainOut.Bodies.Num() > 3);

	FGeoPlacerRule Mouth;
	Mouth.P.SourceMineralIds.Add(5);
	Mouth.P.bMouthOnly = true;
	Mouth.P.MouthCells = 3;
	FGeoScatterOutput MouthOut;
	Stage06cScatter(Chain.C, Chain.F, Mouth, MouthOut);
	TestEqual(TEXT("嘴模式恰好 3 片"), MouthOut.Bodies.Num(), 3);

	int32 Bad = 0;
	for (const FGeoPrimitive& Body : MouthOut.Bodies)
	{
		int32 Face, X, Y;
		Stage06cCellFaceXY(Chain.F.FlowAccum, Stage06cBodyCell(Chain.F.FlowAccum, Body), Face, X, Y);
		if (Face != 0 || Y != 8 || X < 12 - 3) { ++Bad; }
	}
	TestEqual(TEXT("三片都在入海前最后 3 格"), Bad, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGeoStage06cPlacerExposureTest, "StellarEra.Planet.Geo.Stage06c.Placer.Exposure",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGeoStage06cPlacerExposureTest::RunTest(const FString& Parameters)
{
	FStage06cChain Chain = Stage06cMakeChain(12);
	Chain.F.Primitives[0].Exposure = 0;
	FGeoPlacerRule Rule;
	Rule.P.SourceMineralIds.Add(5);
	Rule.P.MinSourceExposure = 0.5f;
	FGeoScatterOutput Out;
	Stage06cScatter(Chain.C, Chain.F, Rule, Out);
	TestEqual(TEXT("源矿出露低于阈值：0 片"), Out.Bodies.Num(), 0);

	Rule.P.MinSourceExposure = 0.f;
	FGeoScatterOutput Out2;
	Stage06cScatter(Chain.C, Chain.F, Rule, Out2);
	TestTrue(TEXT("阈值 0 时同一条链有片"), Out2.Bodies.Num() > 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGeoStage06cPlacerShapeTest, "StellarEra.Planet.Geo.Stage06c.Placer.Shape",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGeoStage06cPlacerShapeTest::RunTest(const FString& Parameters)
{
	const FStage06cChain Chain = Stage06cMakeChain(12);
	FGeoPlacerRule Rule;
	Rule.P.SourceMineralIds.Add(5);
	FGeoScatterOutput Out;
	Stage06cScatter(Chain.C, Chain.F, Rule, Out);
	if (!TestTrue(TEXT("有片才验形状"), Out.Bodies.Num() > 0)) { return false; }

	int32 BadKind = 0, BadThickness = 0;
	for (const FGeoPrimitive& Body : Out.Bodies)
	{
		BadKind += Body.Kind != EGeoPrimitiveKind::OrientedBlob ? 1 : 0;
		BadThickness += Body.Radii.Z > 0.5f * Rule.P.ThicknessVoxels + 0.01f ? 1 : 0;
	}
	TestEqual(TEXT("全部 OrientedBlob"), BadKind, 0);
	TestEqual(TEXT("厚度不超过 ThicknessVoxels/2"), BadThickness, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGeoStage06cFlowNextMatchTest, "StellarEra.Planet.Geo.Stage06c.FlowNextMatch",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGeoStage06cFlowNextMatchTest::RunTest(const FString& Parameters)
{
	FPlanetRecipeConfig C; C.FaceRes = 256;
	// 刻谷会就地削低 Elevation；关掉它，重采样才还是 SolveFlow 当初的输入，逐格对拍才成立。
	C.Hydro.bRivers = false;
	FPlanetBakeReport R;
	const FPlanetBakeFields F = Stage06cUpToHydro(C, R);
	if (!TestFalse(FString::Printf(TEXT("无错误（%s）"), *FString::Join(R.Errors, TEXT(" | "))), R.HasErrors())) { return false; }

	// 与 Stage06Erosion::Bake 的宏观采样逐行一致。
	const int32 Res = FMath::Max(64, C.FaceRes / 4);
	if (!TestEqual(TEXT("FlowNext 分辨率与阶段 6 一致"), F.FlowNext.Res, Res)) { return false; }
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

	int32 Mismatch = 0, LandMismatch = 0, Ocean = 0, LandWithDown = 0;
	for (int32 i = 0; i < N; ++i)
	{
		int32 Face, X, Y; Stage06cCellFaceXY(F.FlowNext, i, Face, X, Y);
		const bool bMatch = F.FlowNext.At(Face, X, Y) == float(Flow.Down[i] + 1);
		if (E[i] <= 0.f) { ++Ocean; }
		else if (Flow.Down[i] >= 0) { ++LandWithDown; }
		if (!bMatch)
		{
			++Mismatch;
			LandMismatch += E[i] > 0.f ? 1 : 0;
		}
	}
	AddInfo(FString::Printf(TEXT("海洋格 %d，有下游的陆格 %d，失配 %d（其中陆格 %d）"), Ocean, LandWithDown, Mismatch, LandMismatch));
	TestEqual(TEXT("陆格 FlowNext 与重算的 Down+1 逐格一致"), LandMismatch, 0);
	TestEqual(TEXT("全部格一致（Down=-1 落成零值）"), Mismatch, 0);
	return true;
}

#endif
