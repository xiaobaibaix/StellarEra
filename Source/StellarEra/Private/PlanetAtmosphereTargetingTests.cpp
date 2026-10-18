#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "PlanetAtmosphereTargeting.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraAtmosphereTargetingTest,
	"StellarEra.Atmosphere.Targeting",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FStellarEraAtmosphereTargetingTest::RunTest(const FString& Parameters)
{
	// 三颗行星：原点一颗半径 75000，X 轴 5e6 一颗，Y 轴 1e7 一颗
	TArray<FVector> Centers;
	Centers.Add(FVector(0.0, 0.0, 0.0));
	Centers.Add(FVector(5.0e6, 0.0, 0.0));
	Centers.Add(FVector(0.0, 1.0e7, 0.0));

	TArray<float> Radii;
	Radii.Add(75000.0f);
	Radii.Add(75000.0f);
	Radii.Add(75000.0f);

	// 相机在原点附近（第一颗的内部）
	TestEqual(TEXT("camera at origin picks planet 0"),
		PlanetAtmosphereTargeting::FindNearestPlanet(Centers, Radii, FVector(0.0, 0.0, 0.0)), 0);

	// 相机贴近第二颗的表面：到球心 5e6 + 80000，表面距离 80000
	TestEqual(TEXT("camera near planet 1 surface picks planet 1"),
		PlanetAtmosphereTargeting::FindNearestPlanet(Centers, Radii,
			FVector(5.0e6 + 80000.0, 0.0, 0.0)), 1);

	// 相机在第二颗球心负 X 侧也应该选第二颗 —— 不能只看坐标正负
	TestEqual(TEXT("camera on -X side of planet 1 still picks planet 1"),
		PlanetAtmosphereTargeting::FindNearestPlanet(Centers, Radii,
			FVector(5.0e6 - 90000.0, 0.0, 0.0)), 1);

	// 相机在两颗行星之间，更靠近第三颗
	TestEqual(TEXT("camera closer to planet 2 picks planet 2"),
		PlanetAtmosphereTargeting::FindNearestPlanet(Centers, Radii,
			FVector(0.0, 9.0e6, 0.0)), 2);

	// 用"表面距离"而不是"球心距离"：给第三颗一个巨大的半径，
	// 使它的表面比第二颗更近，尽管球心更远。
	Radii[2] = 4.0e6f;
	// 相机到第三颗球心 9e6，表面距离 5e6；到第二颗球心约 9.05e6，表面距离约 8.97e6
	TestEqual(TEXT("surface distance wins over center distance"),
		PlanetAtmosphereTargeting::FindNearestPlanet(Centers, Radii, FVector(0.0, 9.0e6, 0.0)), 2);

	// 空数组
	TestEqual(TEXT("empty centers returns INDEX_NONE"),
		PlanetAtmosphereTargeting::FindNearestPlanet(TArray<FVector>(), TArray<float>(), FVector::ZeroVector),
		INDEX_NONE);

	// 长度不匹配 —— 防御性返回
	TArray<FVector> OneCenter;
	OneCenter.Add(FVector::ZeroVector);
	TestEqual(TEXT("mismatched array sizes returns INDEX_NONE"),
		PlanetAtmosphereTargeting::FindNearestPlanet(OneCenter, TArray<float>(), FVector::ZeroVector),
		INDEX_NONE);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
