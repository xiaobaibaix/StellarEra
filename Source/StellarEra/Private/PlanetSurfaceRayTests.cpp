// Copyright Voxel Plugin SAS. All Rights Reserved.
// 行星表面解析射线：纯数学，无 UObject、无世界。

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "PlanetSurfaceRay.h"

// 定半径：就是一个普通球面求交。起于球心正下方 10 cm、朝 +Z 打半径 5 的球，
// 穿入点在 z = −5，所以距离 5。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraSurfaceRayConstantRadiusTest,
	"StellarEra.Planet.SurfaceRay.ConstantRadius",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraSurfaceRayConstantRadiusTest::RunTest(const FString& Parameters)
{
	const FVector Origin(0.0, 0.0, -10.0);
	const FVector Direction(0.0, 0.0, 1.0);

	double Distance = -1.0;
	const bool bHit = PlanetSurfaceRay::SolveAnalyticHit(
		Origin, Direction, /*BaseRadiusCm=*/5.0, /*Iterations=*/3,
		[](const FVector&) { return 5.0; }, Distance);

	TestTrue(TEXT("定半径命中"), bHit);
	TestTrue(TEXT("距离 ≈ 5"), FMath::IsNearlyEqual(Distance, 5.0, 1e-6));
	return true;
}

// 方向相关半径：朝 −Z 看时半径只有 2（5 + 3 × (−1)），射线从 z = −10 打到
// z = −2，距离正好 8。这一项钉的是「迭代真的按新半径重算了距离」。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraSurfaceRayVaryingRadiusTest,
	"StellarEra.Planet.SurfaceRay.VaryingRadius",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraSurfaceRayVaryingRadiusTest::RunTest(const FString& Parameters)
{
	const FVector Origin(0.0, 0.0, -10.0);
	const FVector Direction(0.0, 0.0, 1.0);

	// 极轴方向鼓 3 cm。命中点方向恒为 (0,0,−1)，所以两次迭代后半径稳定在 2。
	const auto RadiusAtDirection = [](const FVector& UnitDirection)
	{
		return 5.0 + 3.0 * UnitDirection.Z;
	};

	double Distance = -1.0;
	const bool bHit = PlanetSurfaceRay::SolveAnalyticHit(
		Origin, Direction, /*BaseRadiusCm=*/5.0, /*Iterations=*/3,
		RadiusAtDirection, Distance);

	TestTrue(TEXT("变半径命中"), bHit);
	TestTrue(TEXT("距离 ≈ 8"), FMath::IsNearlyEqual(Distance, 8.0, 1e-6));
	return true;
}

// 拒绝路径：擦不到、起点在球内、半径 0、方向 0、整个球在背后。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraSurfaceRayRejectsTest,
	"StellarEra.Planet.SurfaceRay.Rejects",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraSurfaceRayRejectsTest::RunTest(const FString& Parameters)
{
	const FVector Forward(0.0, 0.0, 1.0);
	const auto ConstantRadius = [](const FVector&) { return 5.0; };

	double Distance = 123.0;
	TestFalse(TEXT("擦不到球"),
		PlanetSurfaceRay::SolveAnalyticHit(
			FVector(0.0, 10.0, -10.0), Forward, 5.0, 3, ConstantRadius, Distance));
	TestFalse(TEXT("起点在球内"),
		PlanetSurfaceRay::SolveAnalyticHit(
			FVector::ZeroVector, Forward, 5.0, 3, ConstantRadius, Distance));
	TestFalse(TEXT("半径为 0"),
		PlanetSurfaceRay::SolveAnalyticHit(
			FVector(0.0, 0.0, -10.0), Forward, 0.0, 3, ConstantRadius, Distance));
	TestFalse(TEXT("方向为 0"),
		PlanetSurfaceRay::SolveAnalyticHit(
			FVector(0.0, 0.0, -10.0), FVector::ZeroVector, 5.0, 3, ConstantRadius, Distance));

	// 球在射线背后：从 z = +10 往 +Z 打，球心在原点。
	TestFalse(TEXT("球在背后"),
		PlanetSurfaceRay::SolveAnalyticHit(
			FVector(0.0, 0.0, 10.0), Forward, 5.0, 3, ConstantRadius, Distance));
	return true;
}

// RaySphereFirstHit 自身的边界：与 SolveAnalyticHit 分开测，因为星图的
// 轨道遮挡计算也直接用它。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraSurfaceRaySphereHitTest,
	"StellarEra.Planet.SurfaceRay.SphereHit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraSurfaceRaySphereHitTest::RunTest(const FString& Parameters)
{
	const FVector Forward(0.0, 0.0, 1.0);
	const FVector Center = FVector::ZeroVector;

	TestTrue(TEXT("穿入点 5"),
		FMath::IsNearlyEqual(
			PlanetSurfaceRay::RaySphereFirstHit(
				FVector(0.0, 0.0, -10.0), Forward, Center, 5.0),
			5.0, 1e-6));
	TestTrue(TEXT("擦不到 → −1"),
		PlanetSurfaceRay::RaySphereFirstHit(
			FVector(0.0, 10.0, -10.0), Forward, Center, 5.0) < 0.0);
	TestTrue(TEXT("球在背后 → −1"),
		PlanetSurfaceRay::RaySphereFirstHit(
			FVector(0.0, 0.0, 10.0), Forward, Center, 5.0) < 0.0);
	TestTrue(TEXT("半径 0 → −1"),
		PlanetSurfaceRay::RaySphereFirstHit(
			FVector(0.0, 0.0, -10.0), Forward, Center, 0.0) < 0.0);
	return true;
}
