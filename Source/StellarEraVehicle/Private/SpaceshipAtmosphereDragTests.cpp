// Copyright Voxel Plugin SAS. All Rights Reserved.
//
// 大气阻力的验收测试。
//
// 只测纯函数：密度剖面与二次阻力公式。真正挂到行星上的那条路径需要
// AVoxelPlanetActor（构造即建体素世界），不适合在单元测试里生成；那部分在
// UniverseMap PIE 里验证 —— HUD 会直接印出当前密度与阻力减速度。

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "SpaceshipPawn.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraAtmosphereDensityTest,
	"StellarEra.Vehicle.AtmosphereDensityProfile",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraAtmosphereDensityTest::RunTest(const FString& Parameters)
{
	// 取本关卡大行星的实际比例尺：表面半径 2 AU = 2e6 cm，壳厚 5%，标高 1.5%。
	constexpr double SurfaceRadiusCm = 2.0e6;
	constexpr double ScaleHeightRatio = 0.015;
	constexpr double ShellHeightRatio = 0.05;
	const double ScaleHeightCm = SurfaceRadiusCm * ScaleHeightRatio;
	const double ShellHeightCm = SurfaceRadiusCm * ShellHeightRatio;

	auto Density = [=](double AltitudeCm)
	{
		return ASpaceshipPawn::ComputeAtmosphereDensityRatio(
			AltitudeCm, SurfaceRadiusCm, ScaleHeightRatio, ShellHeightRatio);
	};

	TestTrue(TEXT("海平面密度比 = 1"),
		FMath::IsNearlyEqual(Density(0.0), 1.f, 1.0e-5f));
	TestTrue(TEXT("地面以下按海平面处理（不出现 ρ > 1）"),
		FMath::IsNearlyEqual(Density(-10000.0), 1.f, 1.0e-5f));
	TestTrue(TEXT("一个标高处 = 1/e"),
		FMath::IsNearlyEqual(Density(ScaleHeightCm), 0.3678794f, 1.0e-4f));
	TestTrue(TEXT("壳顶 = 0"),
		FMath::IsNearlyEqual(Density(ShellHeightCm), 0.f));
	TestTrue(TEXT("壳外 = 0"),
		FMath::IsNearlyEqual(Density(ShellHeightCm + 1.0), 0.f));

	// 参数非法时宁可不加阻力，也不能除零 / exp 溢出。
	TestTrue(TEXT("表面半径非法 = 0"),
		FMath::IsNearlyEqual(ASpaceshipPawn::ComputeAtmosphereDensityRatio(
			0.0, 0.0, ScaleHeightRatio, ShellHeightRatio), 0.f));
	TestTrue(TEXT("标高比非法 = 0"),
		FMath::IsNearlyEqual(ASpaceshipPawn::ComputeAtmosphereDensityRatio(
			0.0, SurfaceRadiusCm, 0.0, ShellHeightRatio), 0.f));
	TestTrue(TEXT("壳厚比非法 = 0"),
		FMath::IsNearlyEqual(ASpaceshipPawn::ComputeAtmosphereDensityRatio(
			0.0, SurfaceRadiusCm, ScaleHeightRatio, 0.0), 0.f));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraAtmosphereDragTest,
	"StellarEra.Vehicle.AtmosphereDragAcceleration",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraAtmosphereDragTest::RunTest(const FString& Parameters)
{
	constexpr double DragKmPerS2 = 10.0;

	// 1 km/s 向下 = 1e5 cm/s；海平面、系数 10 km/s² → 减速度 1e6 cm/s² 向上。
	const FVector Down(0.0, 0.0, -1.0e5);
	const FVector Reference =
		ASpaceshipPawn::ComputeAtmosphereDragAccelerationWorldCmPerS2(
			Down, 1.0, DragKmPerS2);
	TestTrue(TEXT("方向与速度严格相反"),
		Reference.Equals(FVector(0.0, 0.0, 1.0e6), 1.0));

	// 二次阻力：速度翻倍 → 阻力四倍。
	const FVector Fast =
		ASpaceshipPawn::ComputeAtmosphereDragAccelerationWorldCmPerS2(
			Down * 2.0, 1.0, DragKmPerS2);
	TestTrue(TEXT("速度翻倍阻力四倍"),
		Fast.Equals(FVector(0.0, 0.0, 4.0e6), 4.0));

	// 密度折半 → 阻力折半。
	const FVector Thin =
		ASpaceshipPawn::ComputeAtmosphereDragAccelerationWorldCmPerS2(
			Down, 0.5, DragKmPerS2);
	TestTrue(TEXT("密度折半阻力折半"),
		Thin.Equals(FVector(0.0, 0.0, 5.0e5), 1.0));

	// 任一输入为零都必须是零向量：真空里不能凭空出现减速。
	TestTrue(TEXT("速度为零 = 无阻力"),
		ASpaceshipPawn::ComputeAtmosphereDragAccelerationWorldCmPerS2(
			FVector::ZeroVector, 1.0, DragKmPerS2).IsNearlyZero());
	TestTrue(TEXT("密度为零 = 无阻力"),
		ASpaceshipPawn::ComputeAtmosphereDragAccelerationWorldCmPerS2(
			Down, 0.0, DragKmPerS2).IsNearlyZero());
	TestTrue(TEXT("系数为零 = 无阻力"),
		ASpaceshipPawn::ComputeAtmosphereDragAccelerationWorldCmPerS2(
			Down, 1.0, 0.0).IsNearlyZero());

	return true;
}

#endif
