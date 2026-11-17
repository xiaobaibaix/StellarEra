// 阶段 6c-1：水系拓扑导出与砂矿规则。
#include "CoreMinimal.h"
#include "HAL/PlatformTime.h"
#include "Misc/AutomationTest.h"
#include "GeoField2D.h"
#include "GeoMineralCatalog.h"
#include "GeoMineralRule.h"
#include "GeoMineralRules.h"
#include "GeoMineralScatterer.h"
#include "PlanetRecipe.h"
#include "GeoPrimitive.h"
#include "GeoRng.h"
#include "PlanetBakePipeline.h"
#include "PlanetBakeTypes.h"
#include "PlanetRecipeConfig.h"
#include "PlanetStage.h"
#include "Stage02Plates.h"
#include "Stage03Terrain.h"
#include "Stage04Magmatism.h"
#include "Stage05Climate.h"
#include "Stage06Erosion.h"
#include "Stage06bExposure.h"

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

	// 每源片数上限：链上有效格远多于 2，超了必然截到 2 片。
	FGeoPlacerRule Capped;
	Capped.P.SourceMineralIds.Add(5);
	Capped.P.StepStride = 1;
	Capped.P.MaxBodiesPerSource = 2;
	FGeoScatterOutput CappedOut;
	Stage06cScatter(Chain.C, Chain.F, Capped, CappedOut);
	TestEqual(TEXT("MaxBodiesPerSource=2 时恰好 2 片"), CappedOut.Bodies.Num(), 2);
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
	const FStage06cChain Chain = Stage06cMakeChain(16);   // Res=16：上限 15 个陆格
	const int32 Land = Chain.F.FlowAccum.Res - 1;         // 与 Stage06cMakeChain 的 Clamp 一致
	FGeoPlacerRule Plain;
	Plain.P.SourceMineralIds.Add(5);
	FGeoScatterOutput PlainOut;
	Stage06cScatter(Chain.C, Chain.F, Plain, PlainOut);
	TestTrue(TEXT("非嘴模式片数 > 3"), PlainOut.Bodies.Num() > 3);

	FGeoPlacerRule Mouth;
	Mouth.P.SourceMineralIds.Add(5);
	Mouth.P.bMouthOnly = true;
	Mouth.P.MouthCells = 5;
	FGeoScatterOutput MouthOut;
	Stage06cScatter(Chain.C, Chain.F, Mouth, MouthOut);
	TestEqual(TEXT("嘴模式恰好 5 片"), MouthOut.Bodies.Num(), 5);

	int32 Bad = 0;
	for (const FGeoPrimitive& Body : MouthOut.Bodies)
	{
		int32 Face, X, Y;
		Stage06cCellFaceXY(Chain.F.FlowAccum, Stage06cBodyCell(Chain.F.FlowAccum, Body), Face, X, Y);
		if (Face != 0 || Y != 8 || X < Land - 5) { ++Bad; }
	}
	TestEqual(TEXT("五片都在入海前最后 5 格"), Bad, 0);
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
	Rule.P.ThicknessVoxels = 4.f;   // 非默认值：厚度若被写死成 0.5，这里会露馅
	FGeoScatterOutput Out;
	Stage06cScatter(Chain.C, Chain.F, Rule, Out);
	if (!TestTrue(TEXT("有片才验形状"), Out.Bodies.Num() > 0)) { return false; }

	const float ExpectedHalfThick = FMath::Max(0.5f * Rule.P.ThicknessVoxels, 0.5f);   // 4 体素 → 2
	int32 BadKind = 0, BadThickness = 0;
	for (const FGeoPrimitive& Body : Out.Bodies)
	{
		BadKind += Body.Kind != EGeoPrimitiveKind::OrientedBlob ? 1 : 0;
		BadThickness += FMath::Abs(Body.Radii.Z - ExpectedHalfThick) > 0.01f ? 1 : 0;
	}
	TestEqual(TEXT("全部 OrientedBlob"), BadKind, 0);
	TestEqual(TEXT("厚度恰为 max(ThicknessVoxels/2, 0.5)（4 → 2）"), BadThickness, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGeoStage06cPlacerHashTest, "StellarEra.Planet.Geo.Stage06c.Placer.Hash",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGeoStage06cPlacerHashTest::RunTest(const FString& Parameters)
{
	FGeoPlacerRule Off;
	Off.P.SourceMineralIds.Add(5);
	FGeoPlacerRule OnMouth = Off;
	OnMouth.P.bMouthOnly = true;   // 默认 MouthCells=3：旧位移写法下这个翻转不改哈希
	TestTrue(TEXT("bMouthOnly 翻转改变哈希"), Off.HashParams() != OnMouth.HashParams());

	FGeoPlacerRule Mouth1 = Off;
	Mouth1.P.bMouthOnly = true;
	Mouth1.P.MouthCells = 1;
	FGeoPlacerRule Mouth3 = Off;   // bMouthOnly=false, MouthCells=3（默认）
	TestTrue(TEXT("(MouthCells=1, 嘴模式) 与 (MouthCells=3, 普通) 哈希不同"),
		Mouth1.HashParams() != Mouth3.HashParams());
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
	TestTrue(TEXT("有下游的陆格非零（否则对拍空转）"), LandWithDown > 0);
	TestEqual(TEXT("陆格 FlowNext 与重算的 Down+1 逐格一致"), LandMismatch, 0);
	TestEqual(TEXT("全部格一致（Down=-1 落成零值）"), Mismatch, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGeoStage06cPlacerRecipeTest, "StellarEra.Planet.Geo.Stage06c.Recipe",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGeoStage06cPlacerRecipeTest::RunTest(const FString& Parameters)
{
	UGeoMineralCatalog* Cat = NewObject<UGeoMineralCatalog>();
	for (const TCHAR* Id : { TEXT("EpithermalAuAg"), TEXT("PorphyryCu") })
	{
		FGeoMineralDef D; D.Id = FName(Id); D.DisplayName = Id; Cat->Minerals.Add(D);
	}
	UPlanetRecipe* Recipe = NewObject<UPlanetRecipe>();
	Recipe->MineralCatalog = Cat;

	// 解析路径会对缺名 / 缺目录打警告；不登记的话测试会被判成「意外警告」。
	AddExpectedError(TEXT("不在矿物目录里"), EAutomationExpectedErrorFlags::Contains, 1);
	AddExpectedError(TEXT("没有矿物目录"), EAutomationExpectedErrorFlags::Contains, 1);

	UGeoPlacerScatterer* S = NewObject<UGeoPlacerScatterer>(Recipe);
	S->Sources = { TEXT("EpithermalAuAg"), TEXT("PorphyryCu"), TEXT("Missing") };
	const TSharedPtr<IGeoMineralRule> Rule = S->MakeRule();
	if (!TestNotNull(TEXT("撒矿器产出规则"), Rule.Get())) { return false; }
	TestEqual(TEXT("砂矿规则的阶段号 7"), int32(Rule->GetStage()), 7);
	const FGeoPlacerRule* Placer = static_cast<const FGeoPlacerRule*>(Rule.Get());
	TestEqual(TEXT("两个名字解析成功、缺的跳过"), Placer->P.SourceMineralIds.Num(), 2);
	if (Placer->P.SourceMineralIds.Num() == 2)
	{
		TestEqual(TEXT("编号 1"), int32(Placer->P.SourceMineralIds[0]), 1);
		TestEqual(TEXT("编号 2"), int32(Placer->P.SourceMineralIds[1]), 2);
	}
	TestEqual(TEXT("默认矿物是 AuPlacer"), S->Mineral, FName(TEXT("AuPlacer")));

	// 没有配方外层级：源矿解析不了，但规则照出，整条烘焙不该因此失败。
	UGeoPlacerScatterer* Orphan = NewObject<UGeoPlacerScatterer>();
	Orphan->Sources = { TEXT("EpithermalAuAg") };
	const TSharedPtr<IGeoMineralRule> OrphanRule = Orphan->MakeRule();
	if (TestNotNull(TEXT("无目录也产出规则"), OrphanRule.Get()))
	{
		TestEqual(TEXT("无目录：源矿列表为空"),
			static_cast<const FGeoPlacerRule*>(OrphanRule.Get())->P.SourceMineralIds.Num(), 0);
	}

	// 属性 → 参数逐项拷贝：全部给非默认值，抄错哪一项都会露出来。
	UGeoPlacerScatterer* Full = NewObject<UGeoPlacerScatterer>(Recipe);
	Full->Sources = { TEXT("PorphyryCu") };
	Full->MinSourceExposure = 0.42f;
	Full->MaxSteps = 55;
	Full->StepStride = 5;
	Full->MaxBodiesPerSource = 17;
	Full->MinFlowAccumRatio = 3.5f;
	Full->HalfWidthRatio = 0.0035f;
	Full->ThicknessVoxels = 2.5f;
	Full->DecayPerVoxel = 0.0004f;
	Full->bMouthOnly = true;
	Full->MouthCells = 7;
	Full->Hard = 0.7f;
	const TSharedPtr<IGeoMineralRule> FullRuleOwner = Full->MakeRule();
	const FGeoPlacerRule* FullRule = static_cast<const FGeoPlacerRule*>(FullRuleOwner.Get());
	if (!TestNotNull(TEXT("全非默认参数产出规则"), FullRule)) { return false; }
	const FGeoPlacerParams& FP = FullRule->P;
	TestEqual(TEXT("源矿只解析出一项"), FP.SourceMineralIds.Num(), 1);
	if (FP.SourceMineralIds.Num() == 1)
	{
		TestEqual(TEXT("源矿编号 = 目录下标 + 1（PorphyryCu = 2）"), int32(FP.SourceMineralIds[0]), 2);
	}
	TestEqual(TEXT("出露阈值"), FP.MinSourceExposure, 0.42f);
	TestEqual(TEXT("追踪步数"), FP.MaxSteps, 55);
	TestEqual(TEXT("步进"), FP.StepStride, 5);
	TestEqual(TEXT("每源片数上限"), FP.MaxBodiesPerSource, 17);
	TestEqual(TEXT("汇流倍数"), FP.MinFlowAccumRatio, 3.5f);
	TestEqual(TEXT("半宽"), FP.HalfWidthRatio, 0.0035f);
	TestEqual(TEXT("厚度"), FP.ThicknessVoxels, 2.5f);
	TestEqual(TEXT("品位衰减"), FP.DecayPerVoxel, 0.0004f);
	TestEqual(TEXT("嘴模式"), FP.bMouthOnly, true);
	TestEqual(TEXT("嘴格数"), FP.MouthCells, 7);
	TestEqual(TEXT("软边"), FP.Hard, 0.7f);

	// 哈希探针：12 个参数各翻一项，HashParams 必变；翻完立刻恢复，下一条从默认值出发。
	UGeoPlacerScatterer* H = NewObject<UGeoPlacerScatterer>(Recipe);
	H->Sources = { TEXT("EpithermalAuAg"), TEXT("PorphyryCu") };
	auto HashNow = [H]() {
		const TSharedPtr<IGeoMineralRule> R = H->MakeRule();
		return R ? R->HashParams() : uint64(0);
	};
	const uint64 H0 = HashNow();
	if (!TestTrue(TEXT("默认参数哈希非零"), H0 != 0)) { return false; }
	H->Sources.Pop();        TestTrue(TEXT("Sources 进哈希"), HashNow() != H0); H->Sources.Add(TEXT("PorphyryCu"));
	H->MinSourceExposure = 0.5f;   TestTrue(TEXT("MinSourceExposure 进哈希"), HashNow() != H0); H->MinSourceExposure = 0.f;
	H->MaxSteps = 97;              TestTrue(TEXT("MaxSteps 进哈希"), HashNow() != H0); H->MaxSteps = 96;
	H->StepStride = 3;             TestTrue(TEXT("StepStride 进哈希"), HashNow() != H0); H->StepStride = 2;
	H->MaxBodiesPerSource = 25;    TestTrue(TEXT("MaxBodiesPerSource 进哈希"), HashNow() != H0); H->MaxBodiesPerSource = 24;
	H->MinFlowAccumRatio = 2.f;    TestTrue(TEXT("MinFlowAccumRatio 进哈希"), HashNow() != H0); H->MinFlowAccumRatio = 1.f;
	H->HalfWidthRatio = 0.003f;    TestTrue(TEXT("HalfWidthRatio 进哈希"), HashNow() != H0); H->HalfWidthRatio = 0.0015f;
	H->ThicknessVoxels = 2.f;      TestTrue(TEXT("ThicknessVoxels 进哈希"), HashNow() != H0); H->ThicknessVoxels = 1.f;
	H->DecayPerVoxel = 0.00016f;   TestTrue(TEXT("DecayPerVoxel 进哈希"), HashNow() != H0); H->DecayPerVoxel = 0.00008f;
	H->bMouthOnly = true;          TestTrue(TEXT("bMouthOnly 进哈希"), HashNow() != H0); H->bMouthOnly = false;
	H->MouthCells = 4;             TestTrue(TEXT("MouthCells 进哈希"), HashNow() != H0); H->MouthCells = 3;
	H->Hard = 0.5f;                TestTrue(TEXT("Hard 进哈希"), HashNow() != H0); H->Hard = 0.3f;
	TestEqual(TEXT("翻完恢复原哈希"), HashNow(), H0);
	return true;
}

namespace
{
	/**
	 * 按管线的执行方式手动跑一个阶段：Bake 完立刻跑挂在该阶段的撒矿器。
	 * 阶段号与随机流逐位照抄 FPlanetBakePipeline::Bake，测试量的就是生产那条路径。
	 */
	void Stage06cBakeOne(const FPlanetRecipeConfig& C, FPlanetBakeFields& F, FPlanetBakeReport& R,
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
	 * 2→7 全手动烘焙（含各阶段的撒矿器），结束时场还活着，测试才读得到 FlowAccum / FlowNext / Elevation。
	 * 跳过的阶段 1 只写圈层剖面；OutStageIndices 交回实际跑过的阶段号，供测试与默认管线对表。
	 */
	void Stage06cBakeToExposure(const FPlanetRecipeConfig& C, FPlanetBakeFields& F, FPlanetBakeReport& R,
		TArray<uint32>& OutStageIndices)
	{
		OutStageIndices.Reset();
		const FStage02Plates S2; Stage06cBakeOne(C, F, R, S2); OutStageIndices.Add(S2.GetStageIndex());
		const FStage03Terrain S3; Stage06cBakeOne(C, F, R, S3); OutStageIndices.Add(S3.GetStageIndex());
		const FStage04Magmatism S4; Stage06cBakeOne(C, F, R, S4); OutStageIndices.Add(S4.GetStageIndex());
		const FStage05Climate S5; Stage06cBakeOne(C, F, R, S5); OutStageIndices.Add(S5.GetStageIndex());
		const FStage06Erosion S6; Stage06cBakeOne(C, F, R, S6); OutStageIndices.Add(S6.GetStageIndex());
		const FStage06bExposure S7; Stage06cBakeOne(C, F, R, S7); OutStageIndices.Add(S7.GetStageIndex());
	}

	/** 砂金配方：火山金银脉（编号 6）当源矿，砂矿跟着它顺流沉积。 */
	FPlanetRecipeConfig Stage06cPlacerRecipe(TSharedPtr<const IGeoMineralRule>* OutPlacer = nullptr)
	{
		FPlanetRecipeConfig C; C.FaceRes = 512; C.bSingleThreadBake = true;
		TSharedPtr<FGeoEpithermalRule> Gold = MakeShared<FGeoEpithermalRule>(); Gold->MineralId = 6;
		TSharedPtr<FGeoPlacerRule> Placer = MakeShared<FGeoPlacerRule>();
		Placer->MineralId = 20;
		Placer->P.SourceMineralIds = { 6 };
		Placer->P.StepStride = 1;
		C.MineralRules = { Gold, Placer };
		if (OutPlacer) { *OutPlacer = Placer; }
		return C;
	}

	int32 Stage06cCountBodies(const TArray<FGeoPrimitive>& P, uint8 MineralId)
	{
		int32 N = 0;
		for (const FGeoPrimitive& B : P) { N += B.MineralId == MineralId ? 1 : 0; }
		return N;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGeoStage06cPipelineTest, "StellarEra.Planet.Geo.Stage06c.Pipeline",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGeoStage06cPipelineTest::RunTest(const FString& Parameters)
{
	TSharedPtr<const IGeoMineralRule> Placer;
	const FPlanetRecipeConfig C = Stage06cPlacerRecipe(&Placer);

	const double T0 = FPlatformTime::Seconds();
	FPlanetBakeReport R;
	const TSharedPtr<const FPlanetSnapshot> Snap = FPlanetBakePipeline::MakeDefault().Bake(C, R);
	const double Seconds = FPlatformTime::Seconds() - T0;
	if (!TestFalse(FString::Printf(TEXT("整管线无错误（%s）"), *FString::Join(R.Errors, TEXT(" | "))), R.HasErrors())) { return false; }
	if (!TestNotNull(TEXT("产出快照"), Snap.Get())) { return false; }

	int32 Placers = 0, Above = 0, Below = 0, Touched = 0;
	for (const FGeoPrimitive& B : Snap->Primitives.GetPrimitives())
	{
		if (B.MineralId != 20) { continue; }
		++Placers;
		const FVector3f Dir = B.Center.GetSafeNormal();
		const double Surf = double(Snap->R0) + double(Snap->Elevation.Sample(Dir));
		Above += double(B.Center.Size()) > Surf + 1.0 ? 1 : 0;
		Below += double(B.Center.Size()) < Surf - 4.0 ? 1 : 0;
		// 位置证明不了「没被出露结算碰过」：砂矿本来就在地表，结算的贴顶夹取会把它放回原处。
		// 结算只要跑过就写非零比例（夹取那条路写满），所以 Exposure 仍是 0 才是真正没被碰过的证据。
		Touched += B.Exposure != 0 ? 1 : 0;
	}
	AddInfo(FString::Printf(TEXT("FaceRes %d 整管线 %.2f s，原语 %d（砂金 %d 片，高于地表 %d，埋深超 4 体素 %d，被出露结算改写 %d）"),
		C.FaceRes, Seconds, Snap->Primitives.GetPrimitives().Num(), Placers, Above, Below, Touched));
	TestTrue(TEXT("有砂金产出"), Placers > 0);
	TestEqual(TEXT("砂金不高于地表 1 体素"), Above, 0);
	TestEqual(TEXT("砂金不埋深 4 体素以上"), Below, 0);
	TestEqual(TEXT("砂金未被出露结算改写（Exposure 恒 0，说明规则跑在阶段 7 之后）"), Touched, 0);
	TestTrue(TEXT("沉积规则阶段号是 7"), Placer.IsValid() && Placer->GetStage() == 7);
	TestTrue(TEXT("默认管线含阶段 7"), FPlanetBakePipeline::MakeDefault().Sorted().ContainsByPredicate(
		[](const IPlanetStage* S) { return S->GetStageIndex() == 7; }));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGeoStage06cPlacerOnRiversTest, "StellarEra.Planet.Geo.Stage06c.PlacerOnRivers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGeoStage06cPlacerOnRiversTest::RunTest(const FString& Parameters)
{
	// 手动烘焙才留得住 FlowAccum / FlowNext：管线只交出快照。
	TSharedPtr<const IGeoMineralRule> PlacerOwner;
	const FPlanetRecipeConfig C = Stage06cPlacerRecipe(&PlacerOwner);
	const FGeoPlacerRule* PlacerRule = static_cast<const FGeoPlacerRule*>(PlacerOwner.Get());
	if (!TestNotNull(TEXT("拿到砂矿规则"), PlacerRule)) { return false; }

	FPlanetBakeFields F; FPlanetBakeReport R; TArray<uint32> UsedStages;
	Stage06cBakeToExposure(C, F, R, UsedStages);
	if (!TestFalse(FString::Printf(TEXT("手动烘焙无错误（%s）"), *FString::Join(R.Errors, TEXT(" | "))), R.HasErrors())) { return false; }
	if (!TestEqual(TEXT("FlowAccum 分辨率 = max(64, FaceRes/4)"), F.FlowAccum.Res, FMath::Max(64, C.FaceRes / 4))) { return false; }

	// 阶段 1 只写圈层剖面（2–7 的代码都不读它），手动链从 2 起；其余必须与默认管线逐项对齐，
	// 否则管线加了阶段而这里没跟上，测试会悄悄测一条过时的链路。
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
	AddInfo(FString::Printf(TEXT("手动链阶段 [%s]，默认管线去掉阶段 1 后 [%s]"),
		*FString::JoinBy(UsedStages, TEXT(","), [](uint32 V) { return FString::FromInt(int32(V)); }),
		*FString::JoinBy(PipelineStages, TEXT(","), [](uint32 V) { return FString::FromInt(int32(V)); })));
	TestEqual(TEXT("手动链与默认管线逐阶段号一致（管线新增阶段必须同步到这里）"), StageMismatch, 0);

	// 诊断：源矿本身在不在可顺流的陆格上——砂金为 0 时先看这里，而不是猜规则。
	int32 Sources = 0, SourcesOnFlow = 0;
	for (const FGeoPrimitive& Src : F.Primitives)
	{
		if (Src.MineralId != 6) { continue; }
		++Sources;
		const int32 Cell = Stage06cBodyCell(F.FlowAccum, Src);
		int32 Face, X, Y; Stage06cCellFaceXY(F.FlowAccum, Cell, Face, X, Y);
		const bool bLand = F.Elevation.Sample(F.FlowAccum.TexelDir(Face, X, Y)) > 0.f;
		SourcesOnFlow += (bLand && int32(F.FlowNext.At(Face, X, Y)) - 1 >= 0) ? 1 : 0;
	}

	// 门槛走规则自己的参数（MinFlowAccumRatio × RiverThreshold），调参后这条断言仍然合法。
	const double MinAcc = double(PlacerRule->P.MinFlowAccumRatio) * double(FMath::Max(C.Hydro.RiverThreshold, 1e-9f));
	int32 Placers = 0, Ocean = 0, LowAccum = 0, OffCell = 0;
	for (const FGeoPrimitive& B : F.Primitives)
	{
		if (B.MineralId != 20) { continue; }
		++Placers;
		const int32 Cell = Stage06cBodyCell(F.FlowAccum, B);
		int32 Face, X, Y; Stage06cCellFaceXY(F.FlowAccum, Cell, Face, X, Y);
		const FVector3f Dir = F.FlowAccum.TexelDir(Face, X, Y);
		// 片心 = 格方向 × 半径，所以方向反查必须是同一格；点积兜住「隔壁格也过其余断言」的漏判。
		OffCell += FVector3f::DotProduct(B.Center.GetSafeNormal(), Dir) < 0.999f ? 1 : 0;
		Ocean += F.Elevation.Sample(Dir) <= 0.f ? 1 : 0;
		LowAccum += double(F.FlowAccum.At(Face, X, Y)) < MinAcc ? 1 : 0;
	}
	AddInfo(FString::Printf(TEXT("源矿 %d 条（其中 %d 条在可顺流的陆格），砂金 %d 片（海洋 %d，汇流 < %.6f 的 %d，片心出格 %d）"),
		Sources, SourcesOnFlow, Placers, Ocean, MinAcc, LowAccum, OffCell));
	TestTrue(TEXT("源矿（火山脉）进了管线"), Sources > 0);
	TestTrue(TEXT("有砂金产出"), Placers > 0);
	TestEqual(TEXT("砂金都落在陆上格"), Ocean, 0);
	TestEqual(TEXT("砂金都在汇流达标的河道格"), LowAccum, 0);
	TestEqual(TEXT("砂金片心都落在它记录的那一格"), OffCell, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGeoStage06cDeterminismTest, "StellarEra.Planet.Geo.Stage06c.Determinism",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGeoStage06cDeterminismTest::RunTest(const FString& Parameters)
{
	FPlanetRecipeConfig C = Stage06cPlacerRecipe();
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
	AddInfo(FString::Printf(TEXT("原语 %d（砂金 %d）哈希 %016llx；单线程 %.2f/%.2f s，多线程 %.2f s"),
		S1->Primitives.GetPrimitives().Num(), Stage06cCountBodies(S1->Primitives.GetPrimitives(), 20),
		static_cast<unsigned long long>(H1), T1, T2, T3));
	TestTrue(TEXT("砂金非空，哈希对比不是空转"), Stage06cCountBodies(S1->Primitives.GetPrimitives(), 20) > 0);
	TestTrue(TEXT("哈希非零"), H1 != 0);
	TestEqual(TEXT("同配方两次烘焙原语哈希一致"), int64(S2->Primitives.Hash()), int64(H1));
	TestEqual(TEXT("单线程与多线程原语哈希一致"), int64(S3->Primitives.Hash()), int64(H1));
	return true;
}

#endif
