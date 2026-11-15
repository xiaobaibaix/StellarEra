// 阶段 4c-2：碳酸岩岩性与放置。跑真实 2→3→4 烘焙，验证裂谷里的碳酸岩体。
#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "GeoLithology.h"
#include "GeoPrimitive.h"
#include "GeoRng.h"
#include "PlanetBakePipeline.h"
#include "PlanetRecipeConfig.h"
#include "Stage02Plates.h"
#include "Stage03Terrain.h"
#include "Stage04Magmatism.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	// 跑阶段 2→3→4（阶段 4 内含 4b 放置）：与生产管线同一入口，避免测试自己拼一个近似的场。
	void Stage04c2TestBake(const FPlanetRecipeConfig& C, FPlanetBakeFields& F, FPlanetBakeReport& R)
	{
		FPlanetBakeContext C2{ C, F, FGeoRng::Derive(C.Seed, 2, 0), R }; FStage02Plates().Bake(C2);
		FPlanetBakeContext C3{ C, F, FGeoRng::Derive(C.Seed, 3, 0), R }; FStage03Terrain().Bake(C3);
		FPlanetBakeContext C4{ C, F, FGeoRng::Derive(C.Seed, 4, 0), R }; FStage04Magmatism().Bake(C4);
	}
	int32 Stage04c2LithoCount(const TArray<FGeoPrimitive>& P, uint8 Litho)
	{
		int32 N = 0; for (const FGeoPrimitive& Q : P) { N += Q.LithologyId == Litho ? 1 : 0; } return N;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGeoStage04c2CarbonatiteTest, "StellarEra.Planet.Geo.Stage04c2.Carbonatite",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGeoStage04c2CarbonatiteTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("岩性表多出碳酸岩一位"), int32(GeoLithology::Count), 16);
	TestEqual(TEXT("碳酸岩显示名"), FString(GeoLithology::DisplayName(GeoLithology::Carbonatite)), FString(TEXT("碳酸岩")));
	TestEqual(TEXT("碳酸岩调试色"), uint32(GeoLithology::DebugColor(GeoLithology::Carbonatite).ToPackedARGB()),
		uint32(FColor(200, 180, 210).ToPackedARGB()));

	FPlanetRecipeConfig C; C.FaceRes = 512;
	FPlanetBakeFields F; FPlanetBakeReport R;
	Stage04c2TestBake(C, F, R);
	TestFalse(TEXT("无错误"), R.HasErrors());

	const int32 NC = Stage04c2LithoCount(F.Primitives, GeoLithology::Carbonatite);
	AddInfo(FString::Printf(TEXT("碳酸岩 %d 个"), NC));
	TestTrue(TEXT("有碳酸岩"), NC > 0);

	// 放置只从裂谷 texel 出发：中心方向最近 texel 必须仍是 6（大陆裂谷），且陆上。
	int32 BadRift = 0, BadElev = 0, NotBlob = 0;
	for (const FGeoPrimitive& P : F.Primitives)
	{
		if (P.LithologyId != GeoLithology::Carbonatite) { continue; }
		const FVector3f Dir = P.Center.GetSafeNormal();
		BadRift += F.ReliefType.SampleNearest(Dir) == 6.f ? 0 : 1;
		BadElev += F.Elevation.Sample(Dir) > 0.f ? 0 : 1;
		NotBlob += P.Kind == EGeoPrimitiveKind::Blob ? 0 : 1;
	}
	AddInfo(FString::Printf(TEXT("越界：裂谷 %d / 海面 %d / 非 Blob %d"), BadRift, BadElev, NotBlob));
	TestEqual(TEXT("碳酸岩都在大陆裂谷（ReliefType==6）"), BadRift, 0);
	TestEqual(TEXT("碳酸岩都在海平面以上"), BadElev, 0);
	TestEqual(TEXT("碳酸岩都是 Blob"), NotBlob, 0);

	// 尺度不变：半径减半后数量不变（候选按方向去重，与 R 无关）。
	FPlanetRecipeConfig Half = C; Half.Radius = C.Radius * 0.5;
	FPlanetBakeFields Fh; FPlanetBakeReport Rh;
	Stage04c2TestBake(Half, Fh, Rh);
	TestEqual(TEXT("半径减半碳酸岩数量不变"), Stage04c2LithoCount(Fh.Primitives, GeoLithology::Carbonatite), NC);
	return true;
}

#endif
