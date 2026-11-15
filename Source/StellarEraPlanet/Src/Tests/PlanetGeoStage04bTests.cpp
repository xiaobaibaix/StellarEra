// 阶段 4b：合成原语直喂材质求值，不跑烘焙 —— 只验岩性 / 蚀变圈在单次查询里怎么落地。
#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "GeoLithology.h"
#include "GeoPrimitive.h"
#include "PlanetMaterialEval.h"
#include "PlanetSnapshot.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	TSharedPtr<FPlanetSnapshot> Stage04bSnap(const TArray<FGeoPrimitive>& Prims)
	{
		TSharedPtr<FPlanetSnapshot> S = MakeShared<FPlanetSnapshot>();
		S->R0 = 40000.0;
		S->Radial.Layers = { { 1.f, GeoLithology::Crust, 0.f } };
		S->Primitives.Build(Prims);
		return S;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGeoStage04bMaterialTest, "StellarEra.Planet.Geo.Stage04b.Material",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGeoStage04bMaterialTest::RunTest(const FString& Parameters)
{
	const FVector3f Center(0.f, 0.f, float(40000.0 * 0.95));
	FGeoPrimitive Rock = FGeoPrimitive::MakeBlob(Center, FVector3f(500.f), 0.02f);
	Rock.LithologyId = GeoLithology::Granite;
	const TSharedPtr<FPlanetSnapshot> S = Stage04bSnap({ Rock });
	TestEqual(TEXT("岩体内岩性覆盖为花岗岩"), int32(PlanetMaterialEval::Sample(*S, FVector3d(Center)).Lithology), int32(GeoLithology::Granite));
	// 剖面层写的是未拆分的地壳，Sample 会按海拔拆成陆壳 / 洋壳：地表以上无高程数据，落陆壳。
	TestEqual(TEXT("岩体外恢复地壳"), int32(PlanetMaterialEval::Sample(*S, FVector3d(Center + FVector3f(900.f, 0.f, 0.f))).Lithology), int32(GeoLithology::ContinentalCrust));

	// 蚀变四圈：同一晕上按归一化距离取 4 点。
	FGeoPrimitive Halo = FGeoPrimitive::MakeBlob(Center, FVector3f(1000.f), 0.02f);
	Halo.ClueId = 1; Halo.ClueStrength = 15;
	const TSharedPtr<FPlanetSnapshot> H = Stage04bSnap({ Halo });
	const float Fract[4] = { 0.15f, 0.42f, 0.68f, 0.9f };
	const uint8 Want[4] = { GeoClue::Keratitic, GeoClue::Sericitic, GeoClue::Argillic, GeoClue::Propylitic };
	int32 Order = 0, Mono = 0; uint8 Prev = 255;
	for (int32 k = 0; k < 4; ++k)
	{
		const FGeoSample Sm = PlanetMaterialEval::Sample(*H, FVector3d(Center + FVector3f(Fract[k] * 1000.f, 0.f, 0.f)));
		Order += Sm.Clue == Want[k] ? 1 : 0;
		Mono += Sm.ClueStrength < Prev ? 1 : 0;
		Prev = Sm.ClueStrength;
	}
	TestEqual(TEXT("四圈顺序：钾化→绢英岩化→泥化→青磐岩化"), Order, 4);
	TestEqual(TEXT("强度由内到外单调递减"), Mono, 4);

	// 矿优先：矿体与岩体重叠时两者都写。
	FGeoPrimitive Ore = FGeoPrimitive::MakeBlob(Center, FVector3f(300.f), 0.02f);
	Ore.MineralId = 7; Ore.Grade = 200;
	const TSharedPtr<FPlanetSnapshot> M = Stage04bSnap({ Rock, Ore });
	const FGeoSample Ms = PlanetMaterialEval::Sample(*M, FVector3d(Center));
	TestEqual(TEXT("矿仍被写出"), int32(Ms.Mineral), 7);
	TestEqual(TEXT("岩性同时被覆盖"), int32(Ms.Lithology), int32(GeoLithology::Granite));
	return true;
}

#endif
