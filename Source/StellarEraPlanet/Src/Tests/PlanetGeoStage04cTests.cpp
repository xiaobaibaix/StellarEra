// 阶段 4c-1：火山竖向矿柱。跑真实 2→3→4 烘焙再手动跑规则，验证三种矿与火山的空间关系。
#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "GeoField2D.h"
#include "GeoMineral.h"
#include "GeoMineralRule.h"
#include "GeoMineralRules.h"
#include "GeoRng.h"
#include "GeoVolcano.h"
#include "PlanetBakeTypes.h"
#include "PlanetRecipeConfig.h"
#include "Stage02Plates.h"
#include "Stage03Terrain.h"
#include "Stage04Magmatism.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	// 与生产管线同一入口跑阶段 2→3→4，再按规则的 Stage 手动撒矿：测试要拿到撒矿后的场才验得了位置。
	FGeoField2D Stage04cTestBake(const FPlanetRecipeConfig& C, FPlanetBakeFields& F, FPlanetBakeReport& R)
	{
		FPlanetBakeContext C2{ C, F, FGeoRng::Derive(C.Seed, 2, 0), R }; FStage02Plates().Bake(C2);
		FPlanetBakeContext C3{ C, F, FGeoRng::Derive(C.Seed, 3, 0), R }; FStage03Terrain().Bake(C3);
		FPlanetBakeContext C4{ C, F, FGeoRng::Derive(C.Seed, 4, 0), R }; FStage04Magmatism().Bake(C4);
		for (const TSharedPtr<const IGeoMineralRule>& Rule : C.MineralRules)
		{
			if (!Rule) { continue; }
			FGeoRng Rng = FGeoRng::Derive(C.Seed, Rule->GetStage(), Rule->Hash());
			FGeoScatterContext SC{ C, F, Rng, R.Errors };
			FGeoScatterOutput Out;
			Rule->Scatter(SC, Out);
			for (FGeoPrimitive& B : Out.Bodies) { B.MineralId = Rule->MineralId; F.Primitives.Add(B); }
			for (FGeoMineralOverlay& O : Out.Overlays) { O.MineralId = Rule->MineralId; F.MineralOverlays.Add(MoveTemp(O)); }
		}
		return F.Elevation;
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

#endif
