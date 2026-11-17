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
	int32 BadMaskWithoutId = 0, BadIdOutside = 0, Endorheic = 0;
	for (int32 Face = 0; Face < 6; ++Face)
	for (int32 Y = 0; Y < Res; ++Y)
	for (int32 X = 0; X < Res; ++X)
	{
		const float M = F.BasinMask.At(Face, X, Y);
		const float Id = F.BasinId.At(Face, X, Y);
		// 湖盆（1）与内流盆地（2）都算洼地：BasinId 按洼地编号，湖盆不因有出口就没有编号。
		if (M > 0.f) { Endorheic += M == 2.f ? 1 : 0; BadMaskWithoutId += Id < 0.f ? 1 : 0; }
		else { BadIdOutside += Id >= 0.f ? 1 : 0; }
	}
	AddInfo(FString::Printf(TEXT("内流格 %d"), Endorheic));
	TestTrue(TEXT("有内流盆地格（否则整套断言空转）"), Endorheic > 0);
	TestEqual(TEXT("洼地格子都有编号"), BadMaskWithoutId, 0);
	TestEqual(TEXT("非洼地格编号为 -1"), BadIdOutside, 0);
	return true;
}

#endif
