// 阶段 4c-1：火山竖向矿柱。跑真实 2→3→4 烘焙再手动跑规则，验证三种矿与火山的空间关系。
#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "GeoField2D.h"
#include "GeoLithology.h"
#include "GeoMineral.h"
#include "GeoMineralRule.h"
#include "GeoMineralRules.h"
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
	// 跑真实 2→3→4 烘焙再按管线的方式撒矿：测试要拿到撒矿后的场才验得了矿体位置。
	void Stage04cTestBake(const FPlanetRecipeConfig& C, FPlanetBakeFields& F, FPlanetBakeReport& R)
	{
		FPlanetBakeContext C2{ C, F, FGeoRng::Derive(C.Seed, 2, 0), R }; FStage02Plates().Bake(C2);
		FPlanetBakeContext C3{ C, F, FGeoRng::Derive(C.Seed, 3, 0), R }; FStage03Terrain().Bake(C3);
		FPlanetBakeContext C4{ C, F, FGeoRng::Derive(C.Seed, 4, 0), R }; FStage04Magmatism().Bake(C4);
		for (const TSharedPtr<const IGeoMineralRule>& Rule : C.MineralRules)
		{
			if (!Rule) { continue; }
			// 随机流与 FPlanetBakePipeline::Bake 逐位一致，测试量的就是生产撒出来的矿体。
			const uint64 Purpose = GeoHash::Combine(GeoHash::Combine(GeoHash::Str("scatter"), Rule->HashParams()), uint64(Rule->SeedOffset));
			FGeoScatterContext SC{ C, F, FGeoRng::Derive(C.Seed, Rule->GetStage(), Purpose), R.Errors };
			FGeoScatterOutput Out;
			Rule->Scatter(SC, Out);
			for (FGeoPrimitive& B : Out.Bodies) { B.MineralId = Rule->MineralId; F.Primitives.Add(B); }
			for (FGeoMineralOverlay& O : Out.Overlays) { O.MineralId = Rule->MineralId; F.MineralOverlays.Add(MoveTemp(O)); }
		}
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
	TestEqual(TEXT("覆层盖上矿物编号"), int32(O.MineralId), int32(Rule->MineralId));
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

#endif
