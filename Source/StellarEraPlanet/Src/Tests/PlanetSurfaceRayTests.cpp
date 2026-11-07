// Copyright Voxel Plugin SAS. All Rights Reserved.
// 行星表面解析射线：纯数学，无 UObject、无世界。

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "PlanetSurfaceRay.h"

#if WITH_DEV_AUTOMATION_TESTS

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

// 变半径（5 + 3·Z）：钉「迭代真的按新半径重算了距离」。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraSurfaceRayVaryingRadiusTest,
	"StellarEra.Planet.SurfaceRay.VaryingRadius",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraSurfaceRayVaryingRadiusTest::RunTest(const FString& Parameters)
{
	const FVector Origin(0.0, 0.0, -10.0);
	const FVector Direction(0.0, 0.0, 1.0);

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

	TestFalse(TEXT("球在背后"),
		PlanetSurfaceRay::SolveAnalyticHit(
			FVector(0.0, 0.0, 10.0), Forward, 5.0, 3, ConstantRadius, Distance));
	// 失败时不动 Out 参数：调用方可能拿它和上一次的结果比较。
	TestEqual(TEXT("失败时 Out 距离保持 123"), Distance, 123.0);
	return true;
}

// RaySphereFirstHit 自身边界；星图轨道遮挡计算也直接用它，故与 SolveAnalyticHit 分开测。
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

// 中段打空时保留上一个距离而不是整体失败 —— RaycastSurfaceCm 依赖的契约。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraSurfaceRayMidIterationMissTest,
	"StellarEra.Planet.SurfaceRay.MidIterationMiss",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraSurfaceRayMidIterationMissTest::RunTest(const FString& Parameters)
{
	const FVector Origin(0.0, 10.0, -10.0);
	const FVector Direction(0.0, 0.0, 1.0);

	double Distance = -1.0;
	const bool bHit = PlanetSurfaceRay::SolveAnalyticHit(
		Origin, Direction, /*BaseRadiusCm=*/10.0, /*Iterations=*/3,
		[](const FVector&) { return 5.0; }, Distance);

	TestTrue(TEXT("中段打空仍算命中"), bHit);
	TestTrue(TEXT("保留基准距离 10"), FMath::IsNearlyEqual(Distance, 10.0, 1e-6));
	return true;
}

// 远根：与 FirstHit 唯一差别是「起点在球内也给正数」；体素射线行进拿它当几何终点。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraSurfaceRaySphereFarHitTest,
	"StellarEra.Planet.SurfaceRay.SphereFarHit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraSurfaceRaySphereFarHitTest::RunTest(const FString& Parameters)
{
	const FVector Forward(0.0, 0.0, 1.0);
	const FVector Center = FVector::ZeroVector;

	const double Entry = PlanetSurfaceRay::RaySphereFirstHit(
		FVector(0.0, 0.0, -10.0), Forward, Center, 5.0);
	const double Exit = PlanetSurfaceRay::RaySphereFarHit(
		FVector(0.0, 0.0, -10.0), Forward, Center, 5.0);
	TestTrue(TEXT("穿出点 15"), FMath::IsNearlyEqual(Exit, 15.0, 1e-6));
	TestTrue(TEXT("弦长 10 = 远根 − 近根"), FMath::IsNearlyEqual(Exit - Entry, 10.0, 1e-6));

	TestTrue(TEXT("起点在球内时近根仍是 −1"),
		PlanetSurfaceRay::RaySphereFirstHit(
			Center, Forward, Center, 5.0) < 0.0);
	TestTrue(TEXT("起点在球内时远根 = 5"),
		FMath::IsNearlyEqual(
			PlanetSurfaceRay::RaySphereFarHit(Center, Forward, Center, 5.0), 5.0, 1e-6));

	// 相切：两个根重合，仍算命中。
	TestTrue(TEXT("相切远根 = 10"),
		FMath::IsNearlyEqual(
			PlanetSurfaceRay::RaySphereFarHit(
				FVector(0.0, 5.0, -10.0), Forward, Center, 5.0), 10.0, 1e-6));

	// 失败面与近根一致。
	TestTrue(TEXT("擦不到 → −1"),
		PlanetSurfaceRay::RaySphereFarHit(
			FVector(0.0, 10.0, -10.0), Forward, Center, 5.0) < 0.0);
	TestTrue(TEXT("球在背后 → −1"),
		PlanetSurfaceRay::RaySphereFarHit(
			FVector(0.0, 0.0, 10.0), Forward, Center, 5.0) < 0.0);
	TestTrue(TEXT("半径 0 → −1"),
		PlanetSurfaceRay::RaySphereFarHit(
			FVector(0.0, 0.0, -10.0), Forward, Center, 0.0) < 0.0);
	TestTrue(TEXT("方向 0 → −1"),
		PlanetSurfaceRay::RaySphereFarHit(
			FVector(0.0, 0.0, -10.0), FVector::ZeroVector, Center, 5.0) < 0.0);
	return true;
}

// 行进范围：钉「不再有 128 体素深度上限」；步数只由包围球弦长与体素尺寸决定。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraSurfaceRayMarchStepRangeTest,
	"StellarEra.Planet.SurfaceRay.MarchStepRange",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraSurfaceRayMarchStepRangeTest::RunTest(const FString& Parameters)
{
	const FVector Forward(0.0, 0.0, 1.0);

	// 基准：半径 1000 cm（包围球 1250）、相机在 10R 外、体素 1 cm ⇒ 弦长 2250 体素。
	{
		const PlanetSurfaceRay::FMarchStepRange Range =
			PlanetSurfaceRay::ComputeMarchStepRange(
				FVector(0.0, 0.0, -10000.0), Forward,
				/*AnalyticDistanceCm=*/9000.0, /*BoundingRadiusCm=*/1250.0,
				/*VoxelSizeCm=*/1.0);
		TestEqual(TEXT("往里能走完整条弦 2250 步"), Range.InwardMaxSteps, 2250);
		TestEqual(TEXT("往外只到包围球近根 250 步"), Range.BackwardMaxSteps, 250);
		TestTrue(TEXT("远超旧的 128 固定窗口"), Range.InwardMaxSteps > 128);
	}

	{
		const PlanetSurfaceRay::FMarchStepRange Range =
			PlanetSurfaceRay::ComputeMarchStepRange(
				FVector(0.0, 0.0, -10000000.0), Forward,
				/*AnalyticDistanceCm=*/9000000.0,
				/*BoundingRadiusCm=*/1250000.0, /*VoxelSizeCm=*/1.0);
		TestEqual(TEXT("半径放大 1000 倍 → 2250000 步"),
			Range.InwardMaxSteps, 2250000);
	}

	{
		const PlanetSurfaceRay::FMarchStepRange Range =
			PlanetSurfaceRay::ComputeMarchStepRange(
				FVector(0.0, 0.0, -10000.0), Forward,
				9000.0, 1250.0, /*VoxelSizeCm=*/100.0);
		TestEqual(TEXT("体素 100 cm → 23 步"), Range.InwardMaxSteps, 23);
	}

	{
		const PlanetSurfaceRay::FMarchStepRange Range =
			PlanetSurfaceRay::ComputeMarchStepRange(
				FVector(0.0, 0.0, -1100.0), Forward,
				/*AnalyticDistanceCm=*/100.0, /*BoundingRadiusCm=*/1250.0,
				/*VoxelSizeCm=*/1.0);
		TestEqual(TEXT("朝外到射线原点 100 步"), Range.BackwardMaxSteps, 100);
		TestEqual(TEXT("朝里仍是整条弦 2250 步"), Range.InwardMaxSteps, 2250);
	}

	// 射线擦不到包围球：退化成「解析距离 + 包围半径」，不静默失效。
	{
		const PlanetSurfaceRay::FMarchStepRange Range =
			PlanetSurfaceRay::ComputeMarchStepRange(
				FVector(0.0, 5000.0, -10000.0), Forward,
				/*AnalyticDistanceCm=*/9000.0, /*BoundingRadiusCm=*/1250.0,
				/*VoxelSizeCm=*/1.0);
		TestEqual(TEXT("退化终点 = 9000 + 1250"), Range.InwardMaxSteps, 1250);
		TestEqual(TEXT("退化时朝外走到原点 9000 步"), Range.BackwardMaxSteps, 9000);
	}

	// 体素尺寸为 0 也不能除零：步数是有限的且至少 1。
	{
		const PlanetSurfaceRay::FMarchStepRange Range =
			PlanetSurfaceRay::ComputeMarchStepRange(
				FVector(0.0, 0.0, -10000.0), Forward,
				9000.0, 1250.0, /*VoxelSizeCm=*/0.0);
		TestTrue(TEXT("体素 0 不除零"), Range.InwardMaxSteps >= 1);
		TestTrue(TEXT("体素 0 朝外也有限"), Range.BackwardMaxSteps >= 1);
	}
	return true;
}

// 径向步进：起点空朝里找实、起点实朝外找空，边界取两格中点；GetVoxelSurfaceRadiusCmAtDirection 靠它。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraSurfaceRayBoundaryStepTest,
	"StellarEra.Planet.SurfaceRay.BoundaryStep",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraSurfaceRayBoundaryStepTest::RunTest(const FString& Parameters)
{
	{
		const auto SampleEmpty = [](const int32 Step)
		{
			return Step > -3;
		};

		double BoundaryStep = 0.0;
		TestTrue(TEXT("起点空、往里找到实"),
			PlanetSurfaceRay::FindSurfaceBoundaryStep(
				/*MaxForwardSteps=*/8, /*MaxBackwardSteps=*/8,
				SampleEmpty, BoundaryStep));
		TestEqual(TEXT("边界 = −2.5"), BoundaryStep, -2.5, 1.0e-12);
	}

	{
		const auto SampleEmpty = [](const int32 Step)
		{
			return Step >= 4;
		};

		double BoundaryStep = 0.0;
		TestTrue(TEXT("起点实、往外找到空"),
			PlanetSurfaceRay::FindSurfaceBoundaryStep(8, 8, SampleEmpty, BoundaryStep));
		TestEqual(TEXT("边界 = +3.5"), BoundaryStep, 3.5, 1.0e-12);
	}

	// 预算走完没翻转（挖穿）返回 false 且不写 Out —— 调用方靠它退回解析半径。
	{
		const auto SampleEmpty = [](const int32 Step)
		{
			return Step > -1000;
		};

		double BoundaryStep = -123.0;
		TestFalse(TEXT("全空到预算尽头：找不到翻转"),
			PlanetSurfaceRay::FindSurfaceBoundaryStep(8, 8, SampleEmpty, BoundaryStep));
		TestEqual(TEXT("失败时不写 Out 参数"), BoundaryStep, -123.0, 1.0e-12);
	}

	// 预算 = 1 的两个方向：正好在最后一格翻转 → 成功；下一格才翻转 → 失败。
	{
		double BoundaryStep = 0.0;
		TestTrue(TEXT("预算 1、第 1 格就翻转"),
			PlanetSurfaceRay::FindSurfaceBoundaryStep(
				8, 1, [](const int32 Step) { return Step > -1; }, BoundaryStep));
		TestEqual(TEXT("边界 = −0.5"), BoundaryStep, -0.5, 1.0e-12);

		BoundaryStep = 0.0;
		TestFalse(TEXT("预算 1、第 2 格才翻转 → 失败"),
			PlanetSurfaceRay::FindSurfaceBoundaryStep(
				8, 1, [](const int32 Step) { return Step > -2; }, BoundaryStep));
	}

	// 正向预算为 1 的同一条规则（起点实）。
	{
		double BoundaryStep = 0.0;
		TestTrue(TEXT("起点实、正向预算 1、第 1 格就翻转"),
			PlanetSurfaceRay::FindSurfaceBoundaryStep(
				1, 8, [](const int32 Step) { return Step >= 1; }, BoundaryStep));
		TestEqual(TEXT("边界 = +0.5"), BoundaryStep, 0.5, 1.0e-12);
	}

	return true;
}

#endif
