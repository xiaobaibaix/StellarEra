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

#endif
