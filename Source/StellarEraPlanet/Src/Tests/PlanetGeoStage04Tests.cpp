// 阶段 4 骨架：空阶段接进管线后，火山点表要进快照、配方哈希要对火山参数敏感。
#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "GeoContinentField.h"
#include "GeoPlates.h"
#include "GeoRng.h"
#include "PlanetBakePipeline.h"
#include "Stage02Plates.h"
#include "Stage03Terrain.h"
#include "Stage04Magmatism.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	// 跑阶段 2→3→4；返回阶段 3 之后、阶段 4 之前的高程。
	FGeoField2D Stage04TestBake(const FPlanetRecipeConfig& C, FPlanetBakeFields& F, FPlanetBakeReport& R, bool bRunStage4 = true)
	{
		FPlanetBakeContext C2{ C, F, FGeoRng::Derive(C.Seed, 2, 0), R }; FStage02Plates().Bake(C2);
		FPlanetBakeContext C3{ C, F, FGeoRng::Derive(C.Seed, 3, 0), R }; FStage03Terrain().Bake(C3);
		const FGeoField2D Before = F.Elevation;
		if (bRunStage4) { FPlanetBakeContext C4{ C, F, FGeoRng::Derive(C.Seed, 4, 0), R }; FStage04Magmatism().Bake(C4); }
		return Before;
	}
	int32 Stage04Count(const FPlanetBakeFields& F, EGeoVolcanoKind K)
	{
		int32 N = 0; for (const FGeoVolcano& V : F.Volcanoes) { N += V.Kind == K ? 1 : 0; } return N;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGeoStage04PipelineTest, "StellarEra.Planet.Geo.Stage04.Pipeline",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGeoStage04PipelineTest::RunTest(const FString& Parameters)
{
	FPlanetRecipeConfig C;
	C.FaceRes = 256;
	FPlanetBakeReport R;
	const TSharedPtr<const FPlanetSnapshot> Snap = FPlanetBakePipeline::MakeDefault().Bake(C, R);
	TestTrue(FString::Printf(TEXT("烘焙成功（%s）"), *FString::Join(R.Errors, TEXT(" | "))), Snap.IsValid());
	TestTrue(TEXT("阶段 4 在管线里"), R.StageSeconds.ContainsByPredicate([](const TPair<FName, double>& P) { return P.Key == TEXT("Stage04Magmatism"); }));
	return true;
}

#endif
