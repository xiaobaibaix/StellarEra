// 世界边长取错会把整个立方体切到 MaxLOD（旧行星踩过）；这里钉死与 AVoxelPlanetActor 同一口径。
#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "GeoTestPlanetActor.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGeoTestPlanetTest, "StellarEra.Planet.Geo.TestPlanet",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGeoTestPlanetTest::RunTest(const FString& Parameters)
{
	const AGeoTestPlanetActor* Cdo = GetDefault<AGeoTestPlanetActor>();
	TestFalse(TEXT("不自动建世界（由 StartBake 建）"), Cdo->bCreateWorldAutomatically);
	TestFalse(TEXT("不自建全局池"), Cdo->bCreateGlobalPool);
	TestTrue(TEXT("RGB 材质配置"), Cdo->MaterialConfig == EVoxelMaterialConfig::RGB);
	TestEqual(TEXT("R=40000 → 世界边长 131072"), AGeoTestPlanetActor::ComputeWorldSize(40000.0), 131072);
	TestEqual(TEXT("R=1000 → 4096"), AGeoTestPlanetActor::ComputeWorldSize(1000.0), 4096);
	TestTrue(TEXT("R=0 不返回 0"), AGeoTestPlanetActor::ComputeWorldSize(0.0) > 0);
	TestTrue(TEXT("巨大 R 不回绕成非正"), AGeoTestPlanetActor::ComputeWorldSize(1e12) > 0);
	return true;
}

#endif
