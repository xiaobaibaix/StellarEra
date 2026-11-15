// 阶段 4c-1：火山竖向矿柱。跑真实 2→3→4 烘焙再手动跑规则，验证三种矿与火山的空间关系。
#include "CoreMinimal.h"
#include "UObject/UObjectGlobals.h"
#include "Misc/AutomationTest.h"
#include "GeoField2D.h"
#include "GeoLithology.h"
#include "GeoMineral.h"
#include "GeoMineralCatalog.h"
#include "GeoMineralRule.h"
#include "GeoMineralRules.h"
#include "GeoPrimitiveIndex.h"
#include "GeoRng.h"
#include "GeoVolcano.h"
#include "PlanetBakeTypes.h"
#include "PlanetMaterialEval.h"
#include "PlanetRecipeConfig.h"
#include "PlanetSnapshot.h"
#include "Stage02Plates.h"
#include "Stage03Terrain.h"
#include "Stage04Magmatism.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	// 单条规则按管线同样的随机流跑一次撒矿，但不盖章：验「规则吐 0、管线盖号」的分工。
	void Stage04cScatterOne(const FPlanetRecipeConfig& C, const FPlanetBakeFields& F, TArray<FString>& Errors,
		const TSharedPtr<const IGeoMineralRule>& Rule, FGeoScatterOutput& Out)
	{
		// 随机流与 FPlanetBakePipeline::Bake 逐位一致，测试量的就是生产撒出来的矿体。
		const uint64 Purpose = GeoHash::Combine(GeoHash::Combine(GeoHash::Str("scatter"), Rule->HashParams()), uint64(Rule->SeedOffset));
		FGeoScatterContext SC{ C, F, FGeoRng::Derive(C.Seed, Rule->GetStage(), Purpose), Errors };
		Rule->Scatter(SC, Out);
	}

	// 跑真实 2→3→4 烘焙再按管线的方式撒矿：测试要拿到撒矿后的场才验得了矿体位置。
	void Stage04cTestBake(const FPlanetRecipeConfig& C, FPlanetBakeFields& F, FPlanetBakeReport& R)
	{
		FPlanetBakeContext C2{ C, F, FGeoRng::Derive(C.Seed, 2, 0), R }; FStage02Plates().Bake(C2);
		FPlanetBakeContext C3{ C, F, FGeoRng::Derive(C.Seed, 3, 0), R }; FStage03Terrain().Bake(C3);
		FPlanetBakeContext C4{ C, F, FGeoRng::Derive(C.Seed, 4, 0), R }; FStage04Magmatism().Bake(C4);
		for (const TSharedPtr<const IGeoMineralRule>& Rule : C.MineralRules)
		{
			if (!Rule) { continue; }
			FGeoScatterOutput Out;
			Stage04cScatterOne(C, F, R.Errors, Rule, Out);
			for (FGeoPrimitive& B : Out.Bodies) { B.MineralId = Rule->MineralId; F.Primitives.Add(B); }
			for (FGeoMineralOverlay& O : Out.Overlays) { O.MineralId = Rule->MineralId; F.MineralOverlays.Add(MoveTemp(O)); }
		}
	}

	// TArray<FGeoPrimitive> 没有 Hash：借快照索引的哈希，它逐字段混入（含 Center/Radii/Rotation）。
	uint64 Stage04cPrimHash(const TArray<FGeoPrimitive>& P)
	{
		FGeoPrimitiveIndex I; I.Build(P); return I.Hash();
	}

	int32 Stage04cBodyCount(const TArray<FGeoPrimitive>& P, uint8 MineralId)
	{
		int32 N = 0; for (const FGeoPrimitive& B : P) { N += B.MineralId == MineralId ? 1 : 0; } return N;
	}

	int32 Stage04cOverlayCount(const TArray<FGeoMineralOverlay>& O, uint8 MineralId)
	{
		int32 N = 0; for (const FGeoMineralOverlay& L : O) { N += L.MineralId == MineralId ? 1 : 0; } return N;
	}

	// 材质求值核只吃快照：把烘焙出来的场搬进去，测试才能沿矿柱逐点读矿种。
	TSharedPtr<FPlanetSnapshot> Stage04cSnap(const FPlanetRecipeConfig& C, const FPlanetBakeFields& F)
	{
		TSharedPtr<FPlanetSnapshot> S = MakeShared<FPlanetSnapshot>();
		S->R0 = C.Radius;
		S->Radial = F.Radial;
		S->MohoWobble = F.MohoWobble;
		S->Primitives.Build(F.Primitives);
		S->Elevation = F.Elevation;
		S->MineralOverlays = F.MineralOverlays;
		S->Volcanoes = F.Volcanoes;
		S->Minerals = C.Minerals;
		S->CrustSplitElevation = F.CrustSplitElevation;
		S->MinSurfaceRadius = C.Radius + double(F.MinElevation);
		S->MaxSurfaceRadius = C.Radius + double(F.MaxElevation);
		return S;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGeoStage04cSulfurTest, "StellarEra.Planet.Geo.Stage04c.Sulfur",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGeoStage04cSulfurTest::RunTest(const FString& Parameters)
{
	FPlanetRecipeConfig C; C.FaceRes = 512;
	TSharedPtr<FGeoSulfurRule> Rule = MakeShared<FGeoSulfurRule>();
	Rule->MineralId = 1;
	C.MineralRules = { Rule };
	FPlanetBakeFields F; FPlanetBakeReport R;
	Stage04cTestBake(C, F, R);
	TestFalse(FString::Printf(TEXT("无错误（%s）"), *FString::Join(R.Errors, TEXT(" | "))), R.HasErrors());

	if (!TestEqual(TEXT("硫磺出一张覆层"), F.MineralOverlays.Num(), 1)) { return false; }
	const FGeoMineralOverlay& O = F.MineralOverlays[0];
	// 规则本身不盖章（契约：管线统一盖章）：直接跑一次原始 Scatter 验它吐 0，
	// 再确认管线式拷贝把它盖成规则编号——否则这行只是复述 helper 的赋值。
	FGeoScatterOutput Raw;
	Stage04cScatterOne(C, F, R.Errors, Rule, Raw);
	if (!TestEqual(TEXT("原始撒矿吐出一张覆层"), Raw.Overlays.Num(), 1)) { return false; }
	TestEqual(TEXT("规则本身不填矿物编号（留 0 给管线）"), int32(Raw.Overlays[0].MineralId), 0);
	TestEqual(TEXT("管线式拷贝把覆层盖成规则编号"), int32(O.MineralId), int32(Rule->MineralId));
	TestEqual(TEXT("遮罩分辨率 = max(128, FaceRes/2)"), O.Mask.Res, FMath::Max(128, C.FaceRes / 2));
	TestTrue(FString::Printf(TEXT("结壳厚度 %.1f 体素在 10–200 之间"), O.DepthVoxels), O.DepthVoxels >= 10.f && O.DepthVoxels <= 200.f);

	// 结壳半径 = max(火口半径, 下限)：火口小的（层状 / 盾状）靠下限撑住，否则 512 遮罩上不足一个 texel。
	TArray<TPair<FVector3f, float>> Craters;
	for (const FGeoVolcano& V : F.Volcanoes)
	{
		if (V.bActive && V.CraterRadius > 0.f) { Craters.Emplace(V.Dir, FMath::Max(V.CraterRadius, Rule->P.RadiusRatio)); }
	}
	AddInfo(FString::Printf(TEXT("火山 %d 座（活跃带火口 %d 座），遮罩 %d²，覆层厚 %.1f 体素"),
		F.Volcanoes.Num(), Craters.Num(), O.Mask.Res, O.DepthVoxels));
	if (!TestTrue(TEXT("有活跃带火口的火山"), Craters.Num() > 0)) { return false; }

	int32 NonZero = 0, OutOfRange = 0;
	float WorstRatio = 0.f;
	for (int32 Face = 0; Face < 6; ++Face)
	for (int32 Y = 0; Y < O.Mask.Res; ++Y)
	for (int32 X = 0; X < O.Mask.Res; ++X)
	{
		if (O.Mask.At(Face, X, Y) <= 0.f) { continue; }
		++NonZero;
		const FVector3f Dir = O.Mask.TexelDir(Face, X, Y);
		float Chord = TNumericLimits<float>::Max(), Radius = 0.f;
		for (const TPair<FVector3f, float>& Cr : Craters)
		{
			const float D = (Cr.Key - Dir).Size();
			if (D < Chord) { Chord = D; Radius = Cr.Value; }
		}
		WorstRatio = FMath::Max(WorstRatio, Chord / Radius);
		OutOfRange += Chord > 1.2f * Radius + 1e-4f ? 1 : 0;
	}
	AddInfo(FString::Printf(TEXT("遮罩非零 %d 个 texel，最远处 %.4f × 结壳半径"), NonZero, WorstRatio));
	TestTrue(TEXT("遮罩确实有覆盖"), NonZero > 0);
	TestEqual(TEXT("遮罩非零处都在 1.2 × 结壳半径内"), OutOfRange, 0);

	int32 CenterMiss = 0;
	for (const TPair<FVector3f, float>& Cr : Craters) { CenterMiss += O.Mask.SampleNearest(Cr.Key) > 0.5f ? 0 : 1; }
	TestEqual(TEXT("每座活跃火山中心都被结壳覆盖"), CenterMiss, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGeoStage04cEpithermalTest, "StellarEra.Planet.Geo.Stage04c.Epithermal",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGeoStage04cEpithermalTest::RunTest(const FString& Parameters)
{
	FPlanetRecipeConfig C; C.FaceRes = 512;
	TSharedPtr<FGeoEpithermalRule> Rule = MakeShared<FGeoEpithermalRule>();
	Rule->MineralId = 1;
	C.MineralRules = { Rule };
	FPlanetBakeFields F; FPlanetBakeReport R;
	Stage04cTestBake(C, F, R);
	TestFalse(FString::Printf(TEXT("无错误（%s）"), *FString::Join(R.Errors, TEXT(" | "))), R.HasErrors());

	// 脉只长在活跃且带火口的火山下：与硫磺结壳同一批，矿柱的三层才叠得起来。
	TArray<FVector3f> Actives;
	for (const FGeoVolcano& V : F.Volcanoes)
	{
		if (V.bActive && V.CraterRadius > 0.f) { Actives.Add(V.Dir); }
	}
	if (!TestTrue(TEXT("有活跃带火口的火山"), Actives.Num() > 0)) { return false; }

	int32 Veins = 0;
	for (const FGeoPrimitive& B : F.Primitives) { Veins += B.MineralId == Rule->MineralId ? 1 : 0; }
	AddInfo(FString::Printf(TEXT("活火山 %d 座，脉体 %d 条（每座 %d 条）"), Actives.Num(), Veins, Rule->P.VeinCount));
	TestEqual(TEXT("每座活火山都有 VeinCount 条脉"), Veins, Actives.Num() * Rule->P.VeinCount);

	const TSharedPtr<FPlanetSnapshot> S = Stage04cSnap(C, F);
	const double R0 = C.Radius;
	int32 Miss = 0, Shallow = 0, Deep = 0;
	for (const FVector3f& Dir : Actives)
	{
		const double Surf = R0 + double(F.Elevation.Sample(Dir));
		const auto HitAt = [&](double Ratio)
		{
			return PlanetMaterialEval::Sample(*S, FVector3d(Dir) * (Surf - Ratio * R0)).Mineral == Rule->MineralId;
		};
		bool bHit = false;
		for (float Dp = Rule->P.DepthMinRatio; Dp <= Rule->P.DepthMaxRatio + 1e-6f && !bHit; Dp += 0.0005f) { bHit = HitAt(double(Dp)); }
		Miss += bHit ? 0 : 1;
		Shallow += HitAt(0.002) ? 1 : 0;
		Deep += HitAt(0.02) ? 1 : 0;
	}
	AddInfo(FString::Printf(TEXT("正下方命中 %d/%d；0.002R 命中 %d 座；0.02R 命中 %d 座"),
		Actives.Num() - Miss, Actives.Num(), Shallow, Deep));
	TestEqual(TEXT("每座活火山正下方 0.004–0.012R 内至少命中一次"), Miss, 0);
	TestEqual(TEXT("0.002R 处不命中（脉顶之上）"), Shallow, 0);
	TestEqual(TEXT("0.02R 处不命中（脉底之下）"), Deep, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGeoStage04cPorphyryTest, "StellarEra.Planet.Geo.Stage04c.Porphyry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGeoStage04cPorphyryTest::RunTest(const FString& Parameters)
{
	FPlanetRecipeConfig C; C.FaceRes = 512;
	TSharedPtr<FGeoPorphyryRule> Rule = MakeShared<FGeoPorphyryRule>();
	Rule->MineralId = 1;
	C.MineralRules = { Rule };
	FPlanetBakeFields F; FPlanetBakeReport R;
	Stage04cTestBake(C, F, R);
	TestFalse(FString::Printf(TEXT("无错误（%s）"), *FString::Join(R.Errors, TEXT(" | "))), R.HasErrors());

	TArray<const FGeoPrimitive*> Stocks;
	for (const FGeoPrimitive& P : F.Primitives)
	{
		if (P.LithologyId == GeoLithology::Diorite) { Stocks.Add(&P); }
	}
	if (!TestTrue(TEXT("有闪长岩岩株"), Stocks.Num() > 0)) { return false; }

	TArray<const FGeoPrimitive*> Bodies;
	for (const FGeoPrimitive& P : F.Primitives)
	{
		if (P.MineralId == Rule->MineralId) { Bodies.Add(&P); }
	}
	TestEqual(TEXT("每座岩株 PerStock 个浸染体"), Bodies.Num(), Stocks.Num() * Rule->P.PerStock);

	const double R0 = C.Radius;
	int32 Far = 0, Swallow = 0, AboveGround = 0;
	float WorstRatio = 0.f;
	for (const FGeoPrimitive* Body : Bodies)
	{
		const FVector3f Dir = Body->Center.GetSafeNormal();
		const double Surf = R0 + double(F.Elevation.Sample(Dir));
		const double BodyTop = double(Body->Center.Size()) + double(Body->Radii.GetMax());
		AboveGround += BodyTop < Surf - 1.0 ? 0 : 1;

		const FGeoPrimitive* Host = nullptr;
		double Best = TNumericLimits<double>::Max();
		for (const FGeoPrimitive* St : Stocks)
		{
			const double D = double((St->Center - Body->Center).Size());
			if (D < Best) { Best = D; Host = St; }
		}
		const double StockRadius = double(Host->Radii.GetMax());
		WorstRatio = FMath::Max(WorstRatio, float(Best / StockRadius));
		Far += Best <= 1.5 * StockRadius ? 0 : 1;
		// 顶面必须压在岩株顶面之下：否则浸染体会把整个岩株包住，岩株顶不再出露。
		const double StockTop = double(Host->Center.Size()) + double(Host->Radii.GetMax());
		Swallow += BodyTop < StockTop - 1.0 ? 0 : 1;
	}
	AddInfo(FString::Printf(TEXT("岩株 %d 座，浸染体 %d 个，最远心距 %.2f × 岩株半径"),
		Stocks.Num(), Bodies.Num(), WorstRatio));
	TestEqual(TEXT("每个浸染体都在宿主岩株 1.5 倍半径内"), Far, 0);
	TestEqual(TEXT("浸染体顶面都压在岩株顶面之下"), Swallow, 0);
	TestEqual(TEXT("浸染体顶面都在地表以下"), AboveGround, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGeoStage04cColumnTest, "StellarEra.Planet.Geo.Stage04c.Column",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGeoStage04cColumnTest::RunTest(const FString& Parameters)
{
	FPlanetRecipeConfig C; C.FaceRes = 512;
	TSharedPtr<FGeoSulfurRule> Sulfur = MakeShared<FGeoSulfurRule>(); Sulfur->MineralId = 1;
	TSharedPtr<FGeoEpithermalRule> Epithermal = MakeShared<FGeoEpithermalRule>(); Epithermal->MineralId = 2;
	TSharedPtr<FGeoPorphyryRule> Porphyry = MakeShared<FGeoPorphyryRule>(); Porphyry->MineralId = 3;
	C.MineralRules = { Sulfur, Epithermal, Porphyry };
	FPlanetBakeFields F; FPlanetBakeReport R;
	Stage04cTestBake(C, F, R);
	TestFalse(FString::Printf(TEXT("无错误（%s）"), *FString::Join(R.Errors, TEXT(" | "))), R.HasErrors());

	const TSharedPtr<FPlanetSnapshot> S = Stage04cSnap(C, F);
	const double R0 = C.Radius;
	int32 Checked = 0, Violations = 0;
	FString FirstColumn;

	// 层状火山才有岩株，斑岩铜那一段才不会缺。
	for (const FGeoVolcano& V : F.Volcanoes)
	{
		if (!V.bActive || V.Kind != EGeoVolcanoKind::Strato) { continue; }
		++Checked;
		const double Surf = R0 + double(F.Elevation.Sample(V.Dir));
		TArray<uint8> Order;      // 首次出现的矿种顺序
		TArray<FString> Bands;    // 每段的深度区间，失败时报出来
		uint8 Prev = 0; double Start = 0.0;
		for (float Dp = 0.f; Dp <= 0.035f + 1e-6f; Dp += 0.0005f)
		{
			const uint8 M = PlanetMaterialEval::Sample(*S, FVector3d(V.Dir) * (Surf - double(Dp) * R0)).Mineral;
			// 0 只是两层之间的空隙；同一矿种被零隔开仍算同一段，矿种真的变了才记新的一段。
			if (M == Prev) { continue; }
			if (Prev != 0) { Bands.Add(FString::Printf(TEXT("%d@%.4f–%.4f"), Prev, Start, double(Dp))); }
			Prev = M;
			Start = double(Dp);
			if (M != 0 && (Order.IsEmpty() || Order.Last() != M)) { Order.Add(M); }
		}
		if (Prev != 0) { Bands.Add(FString::Printf(TEXT("%d@%.4f–0.0350"), Prev, Start)); }

		const bool bOk = Order.Num() == 3 && Order[0] == 1 && Order[1] == 2 && Order[2] == 3;
		if (FirstColumn.IsEmpty()) { FirstColumn = FString::Join(Bands, TEXT(" → ")); }
		if (!bOk)
		{
			++Violations;
			FString Ids;
			for (const uint8 M : Order) { Ids += FString::Printf(TEXT("%s%d"), Ids.IsEmpty() ? TEXT("") : TEXT("→"), int32(M)); }
			AddError(FString::Printf(TEXT("矿柱顺序 %s（期望 1→2→3），实测 %s"), *Ids, *FString::Join(Bands, TEXT(" → "))));
		}
	}
	AddInfo(FString::Printf(TEXT("活跃层状火山 %d 座，顺序违规 %d 座；首座实测 %s"), Checked, Violations, *FirstColumn));
	TestTrue(TEXT("有活跃层状火山"), Checked > 0);
	TestEqual(TEXT("每座矿柱都是 硫磺→金银→斑岩铜，三段非空且不交叉"), Violations, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGeoStage04cScaleInvarianceTest, "StellarEra.Planet.Geo.Stage04c.ScaleInvariance",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGeoStage04cScaleInvarianceTest::RunTest(const FString& Parameters)
{
	FPlanetRecipeConfig Full; Full.FaceRes = 512;
	TSharedPtr<FGeoSulfurRule> Sulfur = MakeShared<FGeoSulfurRule>(); Sulfur->MineralId = 1;
	TSharedPtr<FGeoEpithermalRule> Epithermal = MakeShared<FGeoEpithermalRule>(); Epithermal->MineralId = 2;
	TSharedPtr<FGeoPorphyryRule> Porphyry = MakeShared<FGeoPorphyryRule>(); Porphyry->MineralId = 3;
	Full.MineralRules = { Sulfur, Epithermal, Porphyry };
	// 规则是共享只读参数：两次烘焙只有半径不同，所有比值都按 R 缩放。
	FPlanetRecipeConfig Half = Full; Half.Radius = Full.Radius * 0.5;

	FPlanetBakeFields Ff, Fh; FPlanetBakeReport Rf, Rh;
	Stage04cTestBake(Full, Ff, Rf);
	Stage04cTestBake(Half, Fh, Rh);
	TestFalse(FString::Printf(TEXT("全尺度无错误（%s）"), *FString::Join(Rf.Errors, TEXT(" | "))), Rf.HasErrors());
	TestFalse(FString::Printf(TEXT("半尺度无错误（%s）"), *FString::Join(Rh.Errors, TEXT(" | "))), Rh.HasErrors());

	const int32 OvlF = Stage04cOverlayCount(Ff.MineralOverlays, 1);
	const int32 VeinF = Stage04cBodyCount(Ff.Primitives, 2);
	const int32 PorphF = Stage04cBodyCount(Ff.Primitives, 3);
	TestTrue(TEXT("全尺度有硫磺结壳"), OvlF > 0);
	TestTrue(TEXT("全尺度有金银脉"), VeinF > 0);
	TestTrue(TEXT("全尺度有斑岩铜体"), PorphF > 0);
	TestEqual(TEXT("半径减半后硫磺结壳数量不变"), Stage04cOverlayCount(Fh.MineralOverlays, 1), OvlF);
	TestEqual(TEXT("半径减半后金银脉数量不变"), Stage04cBodyCount(Fh.Primitives, 2), VeinF);
	TestEqual(TEXT("半径减半后斑岩铜体数量不变"), Stage04cBodyCount(Fh.Primitives, 3), PorphF);

	// 脉体尺寸按 R 缩放：最大脉半径应减半（相对误差 1e-3；绝对值对 ~160 体素太紧）。
	float VeinMaxF = 0.f, VeinMaxH = 0.f;
	for (const FGeoPrimitive& B : Ff.Primitives) { if (B.MineralId == 2) { VeinMaxF = FMath::Max(VeinMaxF, B.Radii.GetMax()); } }
	for (const FGeoPrimitive& B : Fh.Primitives) { if (B.MineralId == 2) { VeinMaxH = FMath::Max(VeinMaxH, B.Radii.GetMax()); } }
	TestTrue(FString::Printf(TEXT("最大脉半径减半（%.4f → %.4f）"), VeinMaxF, VeinMaxH),
		FMath::Abs(VeinMaxF * 0.5f - VeinMaxH) <= FMath::Abs(VeinMaxF * 0.5f) * 1e-3f);

	// 斑岩铜体径向深度 = 地表半径 − 中心半径，同样按 R 缩放；按序号配对（随机流与半径无关）。
	TArray<const FGeoPrimitive*> PorphFList, PorphHList;
	for (const FGeoPrimitive& B : Ff.Primitives) { if (B.MineralId == 3) { PorphFList.Add(&B); } }
	for (const FGeoPrimitive& B : Fh.Primitives) { if (B.MineralId == 3) { PorphHList.Add(&B); } }
	if (!TestEqual(TEXT("两次烘焙斑岩铜体数量一致，可按序配对"), PorphHList.Num(), PorphFList.Num())) { return false; }
	int32 DepthMiss = 0;
	for (int32 i = 0; i < PorphFList.Num(); ++i)
	{
		const FVector3f DirF = PorphFList[i]->Center.GetSafeNormal();
		const FVector3f DirH = PorphHList[i]->Center.GetSafeNormal();
		const double SurfF = Full.Radius + double(Ff.Elevation.Sample(DirF));
		const double SurfH = Half.Radius + double(Fh.Elevation.Sample(DirH));
		const double DepthF = SurfF - double(PorphFList[i]->Center.Size());
		const double DepthH = SurfH - double(PorphHList[i]->Center.Size());
		// 深度 ~ (0.03+Shift)·R ≈ 480 体素，float 尾差 1e-3 量级；用相对 1e-3。
		DepthMiss += FMath::Abs(DepthF * 0.5 - DepthH) <= FMath::Abs(DepthF * 0.5) * 1e-3 ? 0 : 1;
	}
	AddInfo(FString::Printf(TEXT("硫磺 %d/%d，脉 %d/%d，斑岩铜 %d/%d，最大脉半径 %.4f/%.4f"),
		OvlF, Stage04cOverlayCount(Fh.MineralOverlays, 1), VeinF, Stage04cBodyCount(Fh.Primitives, 2),
		PorphF, Stage04cBodyCount(Fh.Primitives, 3), VeinMaxF, VeinMaxH));
	TestEqual(TEXT("斑岩铜体径向深度随半径减半"), DepthMiss, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGeoStage04cSwitchesTest, "StellarEra.Planet.Geo.Stage04c.Switches",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGeoStage04cSwitchesTest::RunTest(const FString& Parameters)
{
	FPlanetRecipeConfig C; C.FaceRes = 512;
	// 确定性对拍要强制单线程：字段用途见 FPlanetRecipeConfig::bSingleThreadBake。
	C.bSingleThreadBake = true;
	TSharedPtr<FGeoSulfurRule> Sulfur = MakeShared<FGeoSulfurRule>(); Sulfur->MineralId = 1;
	TSharedPtr<FGeoEpithermalRule> Epithermal = MakeShared<FGeoEpithermalRule>(); Epithermal->MineralId = 2;
	TSharedPtr<FGeoPorphyryRule> Porphyry = MakeShared<FGeoPorphyryRule>(); Porphyry->MineralId = 3;

	auto Bake = [&](bool bS, bool bE, bool bP, FPlanetBakeFields& F, FPlanetBakeReport& R)
	{
		C.MineralRules.Empty();
		if (bS) { C.MineralRules.Add(Sulfur); }
		if (bE) { C.MineralRules.Add(Epithermal); }
		if (bP) { C.MineralRules.Add(Porphyry); }
		Stage04cTestBake(C, F, R);
	};

	// 全开：三类矿都非零。
	{
		FPlanetBakeFields F; FPlanetBakeReport R;
		Bake(true, true, true, F, R);
		TestFalse(FString::Printf(TEXT("全开无错误（%s）"), *FString::Join(R.Errors, TEXT(" | "))), R.HasErrors());
		TestTrue(TEXT("全开有硫磺结壳"), Stage04cOverlayCount(F.MineralOverlays, 1) > 0);
		TestTrue(TEXT("全开有金银脉"), Stage04cBodyCount(F.Primitives, 2) > 0);
		TestTrue(TEXT("全开有斑岩铜体"), Stage04cBodyCount(F.Primitives, 3) > 0);
	}

	// 各自关掉：该矿种的覆层与矿体都为 0，其它两类不受影响。
	{
		FPlanetBakeFields F; FPlanetBakeReport R;
		Bake(false, true, true, F, R);
		TestEqual(TEXT("关掉硫磺后没有硫磺覆层"), Stage04cOverlayCount(F.MineralOverlays, 1), 0);
		TestEqual(TEXT("关掉硫磺后没有硫磺矿体"), Stage04cBodyCount(F.Primitives, 1), 0);
		TestTrue(TEXT("关掉硫磺不影响金银脉"), Stage04cBodyCount(F.Primitives, 2) > 0);
		TestTrue(TEXT("关掉硫磺不影响斑岩铜"), Stage04cBodyCount(F.Primitives, 3) > 0);
	}
	{
		FPlanetBakeFields F; FPlanetBakeReport R;
		Bake(true, false, true, F, R);
		TestEqual(TEXT("关掉金银后没有金银覆层"), Stage04cOverlayCount(F.MineralOverlays, 2), 0);
		TestEqual(TEXT("关掉金银后没有金银矿体"), Stage04cBodyCount(F.Primitives, 2), 0);
		TestTrue(TEXT("关掉金银不影响斑岩铜"), Stage04cBodyCount(F.Primitives, 3) > 0);
	}
	{
		FPlanetBakeFields F; FPlanetBakeReport R;
		Bake(true, true, false, F, R);
		TestEqual(TEXT("关掉斑岩铜后没有斑岩铜覆层"), Stage04cOverlayCount(F.MineralOverlays, 3), 0);
		TestEqual(TEXT("关掉斑岩铜后没有斑岩铜矿体"), Stage04cBodyCount(F.Primitives, 3), 0);
		TestTrue(TEXT("关掉斑岩铜不影响硫磺"), Stage04cOverlayCount(F.MineralOverlays, 1) > 0);
	}

	// 确定性：同配方两次烘焙，原语哈希与覆层哈希一致。
	{
		FPlanetBakeFields F1, F2; FPlanetBakeReport R1, R2;
		Bake(true, true, true, F1, R1);
		Bake(true, true, true, F2, R2);
		TestEqual(TEXT("两次烘焙原语哈希一致"), int64(Stage04cPrimHash(F1.Primitives)), int64(Stage04cPrimHash(F2.Primitives)));
		uint64 H1 = 0, H2 = 0;
		for (const FGeoMineralOverlay& O : F1.MineralOverlays) { H1 = GeoHash::Combine(H1, O.Hash()); }
		for (const FGeoMineralOverlay& O : F2.MineralOverlays) { H2 = GeoHash::Combine(H2, O.Hash()); }
		TestEqual(TEXT("两次烘焙覆层哈希一致"), int64(H1), int64(H2));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGeoStage04cCatalogTest, "StellarEra.Planet.Geo.Stage04c.Catalog",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGeoStage04cCatalogTest::RunTest(const FString& Parameters)
{
	// 配方路径：撒矿器只按名字引用目录，编号 = 下标 + 1（PlanetRecipe.cpp ToConfig）。
	const TCHAR* Ids[] = { TEXT("BIF"), TEXT("VMS"), TEXT("MnNodule"), TEXT("Chromite"),
		TEXT("Sulfur"), TEXT("EpithermalAuAg"), TEXT("PorphyryCu") };
	UGeoMineralCatalog* Cat = NewObject<UGeoMineralCatalog>();
	for (const TCHAR* Id : Ids)
	{
		FGeoMineralDef D;
		D.Id = FName(Id);
		D.DisplayName = Id;
		Cat->Minerals.Add(D);
	}
	FPlanetRecipeConfig C;
	for (const FGeoMineralDef& D : Cat->Minerals)
	{
		C.Minerals.Add({ D.DisplayName, D.DebugColor.ToFColorSRGB() });
	}
	TestEqual(TEXT("目录七个矿种都进了调色板"), C.Minerals.Num(), 7);

	// 三个新矿种必须能在目录里解析出非零编号，否则撒矿器会被配方整条跳过。
	const TCHAR* NewNames[] = { TEXT("Sulfur"), TEXT("EpithermalAuAg"), TEXT("PorphyryCu") };
	for (const TCHAR* Name : NewNames)
	{
		const int32 Index = Cat->FindIndex(FName(Name));
		const uint8 MineralId = Index == INDEX_NONE ? 0 : uint8(Index + 1);
		TestTrue(FString::Printf(TEXT("「%s」能解析出非零矿物编号"), Name), MineralId != 0);
		TestTrue(FString::Printf(TEXT("「%s」的编号落在调色板范围内"), Name),
			C.Minerals.IsValidIndex(Index) && !C.Minerals[Index].Name.IsEmpty());
	}
	return true;
}

#endif
