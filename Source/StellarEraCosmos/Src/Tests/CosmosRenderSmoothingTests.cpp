// Copyright Voxel Plugin SAS. All Rights Reserved.
//
// 步长下限与写回插值的自动化测试：纯数学，不建世界、不启动模拟。

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "CosmosSimulationSubsystem.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraCosmosTimeStepFloorTest,
	"StellarEra.Cosmos.TimeStepFloor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraCosmosTimeStepFloorTest::RunTest(const FString& Parameters)
{
	constexpr double TimeScale = 0.00333;
	constexpr int32 StepsPerOrbit = 20000;
	constexpr int32 MinStepsPerSecond = 120;

	// 远处配置：最短周期 6.32 年 → 精度步长 3.16e-4 年，只有 10.5 步/秒（视频里的一顿一顿）。
	{
		const double Step = UCosmosSimulationSubsystem::ComputeBaseTimeStep(
			6.324555, StepsPerOrbit, TimeScale, MinStepsPerSecond);

		TestEqual(TEXT("粗步长被下限夹到 1/120 秒"), Step, TimeScale / 120.0, 1.0e-12);
		TestTrue(TEXT("步/秒 ≥ MinStepsPerSecond"),
			TimeScale / Step >= double(MinStepsPerSecond) - 1.0e-9);
		TestTrue(TEXT("60 FPS 每帧至少两步"),
			TimeScale / 60.0 >= Step * 1.999999);
	}

	// 近处配置：最短周期 0.210164 年 → 1.05e-5 年已经比下限细，绝不能反过来放宽。
	{
		const double Step = UCosmosSimulationSubsystem::ComputeBaseTimeStep(
			0.210164, StepsPerOrbit, TimeScale, MinStepsPerSecond);

		TestEqual(TEXT("精度优先于下限"), Step, 0.210164 / 20000.0, 1.0e-12);
	}

	// 单天体 / 退化配对：必须回一个正的有限步长，否则 Tick 里除以 0。
	{
		const double Candidates[] = { 0.0, -1.0, TNumericLimits<double>::Max() };
		for (const double Period : Candidates)
		{
			const double Step = UCosmosSimulationSubsystem::ComputeBaseTimeStep(
				Period, StepsPerOrbit, TimeScale, MinStepsPerSecond);

			TestTrue(TEXT("退化为正且有限"), FMath::IsFinite(Step) && Step > 0.0);
			TestTrue(TEXT("退化仍守下限"), TimeScale / Step >= double(MinStepsPerSecond) - 1.0e-9);
		}

		const double Step = UCosmosSimulationSubsystem::ComputeBaseTimeStep(
			6.324555, 0, TimeScale, MinStepsPerSecond);
		TestTrue(TEXT("StepsPerOrbit=0 不产生 0 步长"), FMath::IsFinite(Step) && Step > 0.0);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraCosmosRenderInterpolatePoseTest,
	"StellarEra.Cosmos.RenderInterpolatePose",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraCosmosRenderInterpolatePoseTest::RunTest(const FString& Parameters)
{
	const FVector3d P0(1.0, 2.0, 3.0);
	const FVector3d P1(4.0, 6.0, 8.0);
	const FQuat4d Q0(0.0, 0.0, 0.0, 1.0);
	const FQuat4d Q1(FVector3d::UnitZ(), FMath::DegreesToRadians(60.0));

	FVector3d Position;
	FQuat4d Orientation;

	// 端点必须逐位命中：alpha 0/1 时插值不能引入任何偏移。
	UCosmosSimulationSubsystem::InterpolateRenderPose(P0, Q0, P1, Q1, 0.0, Position, Orientation);
	TestTrue(TEXT("alpha=0 位置等于 Prev"), Position.Equals(P0, 0.0));
	TestTrue(TEXT("alpha=0 朝向等于 Prev"), Orientation.Equals(Q0, 0.0));

	UCosmosSimulationSubsystem::InterpolateRenderPose(P0, Q0, P1, Q1, 1.0, Position, Orientation);
	TestTrue(TEXT("alpha=1 位置等于 Cur"), Position.Equals(P1, 0.0));
	TestTrue(TEXT("alpha=1 朝向等于 Cur"), Orientation.Equals(Q1, 0.0));

	// 越界 alpha 一律夹回 [0,1]。
	UCosmosSimulationSubsystem::InterpolateRenderPose(P0, Q0, P1, Q1, 2.0, Position, Orientation);
	TestTrue(TEXT("alpha>1 夹到 Cur"), Position.Equals(P1, 0.0));
	UCosmosSimulationSubsystem::InterpolateRenderPose(P0, Q0, P1, Q1, -1.0, Position, Orientation);
	TestTrue(TEXT("alpha<0 夹到 Prev"), Position.Equals(P0, 0.0));

	// 中点位置是线性插值，朝向走短弧：60° 的一半是 30°。
	UCosmosSimulationSubsystem::InterpolateRenderPose(P0, Q0, P1, Q1, 0.5, Position, Orientation);
	TestTrue(TEXT("中点位置"), Position.Equals(FMath::Lerp(P0, P1, 0.5), 1.0e-12));
	{
		const FVector3d Probe = Orientation.RotateVector(FVector3d::UnitX());
		const FVector3d Expected(FMath::Cos(FMath::DegreesToRadians(30.0)),
		                         FMath::Sin(FMath::DegreesToRadians(30.0)), 0.0);
		TestTrue(TEXT("中点朝向 30°"), Probe.Equals(Expected, 1.0e-9));
	}

	// 300° 的短弧是 −60°，不是 +150°：slerp 必须自己处理负点积。
	{
		const FQuat4d QWide(FVector3d::UnitZ(), FMath::DegreesToRadians(300.0));
		UCosmosSimulationSubsystem::InterpolateRenderPose(P0, Q0, P1, QWide, 0.5, Position, Orientation);

		const FVector3d Probe = Orientation.RotateVector(FVector3d::UnitX());
		const FVector3d Expected(FMath::Cos(FMath::DegreesToRadians(-30.0)),
		                         FMath::Sin(FMath::DegreesToRadians(-30.0)), 0.0);
		TestTrue(TEXT("短弧中点 −30°"), Probe.Equals(Expected, 1.0e-9));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraCosmosRenderCadenceTest,
	"StellarEra.Cosmos.RenderCadence",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraCosmosRenderCadenceTest::RunTest(const FString& Parameters)
{
	// 复刻 Tick 的推进/写回节奏，喂一个刻意粗的步长（远处配置：10.5 步/秒）。
	constexpr double TimeScale = 0.00333;
	constexpr double DeltaSeconds = 1.0 / 60.0;
	constexpr double Step = 3.162278e-4;
	constexpr int32 NumFrames = 200;

	const FVector3d Velocity(1337.0, 0.0, 0.0);
	const double ExpectedAdvance = DeltaSeconds * TimeScale * Velocity.Size();
	const FQuat4d Identity(0.0, 0.0, 0.0, 1.0);

	double Accumulator = 0.0;
	FVector3d Cur = FVector3d::ZeroVector;
	FVector3d Prev = Cur;
	bool bHasPrev = false;
	bool bRecording = false;
	bool bHasReference = false;
	int32 ZeroRawAdvances = 0;
	TArray<double> Advances;
	FVector3d Rendered = Cur;
	FVector3d RawRendered = Cur;

	for (int32 Frame = 0; Frame < NumFrames; ++Frame)
	{
		Accumulator += DeltaSeconds * TimeScale;
		const int32 NumSteps = FMath::FloorToInt(Accumulator / Step);

		if (NumSteps > 0)
		{
			for (int32 StepIndex = 0; StepIndex < NumSteps; ++StepIndex)
			{
				if (StepIndex + 1 == NumSteps)
				{
					Prev = Cur;
					bHasPrev = true;
				}

				Cur = Cur + Velocity * Step;
			}

			Accumulator -= double(NumSteps) * Step;
			bRecording = true;
		}

		const double Alpha = bHasPrev ? FMath::Clamp(Accumulator / Step, 0.0, 1.0) : 1.0;
		FVector3d RenderPosition;
		FQuat4d RenderOrientation;
		UCosmosSimulationSubsystem::InterpolateRenderPose(
			Prev, Identity, Cur, Identity, Alpha, RenderPosition, RenderOrientation);

		if (bRecording)
		{
			// 首个推进帧只是建立基准（此前没有 Prev 可插值），它的位移不算采样。
			if (bHasReference)
			{
				Advances.Add((RenderPosition - Rendered).Size());
			}

			bHasReference = true;

			if ((Cur - RawRendered).IsNearlyZero())
			{
				++ZeroRawAdvances;
			}
		}

		Rendered = RenderPosition;
		RawRendered = Cur;
	}

	TestTrue(TEXT("用例样本足够"), Advances.Num() > 100);

	// 先证明这个步长下「不插值」确实是坏的：绝大多数帧原地不动。
	TestTrue(TEXT("不插值时过半帧零位移"), ZeroRawAdvances > Advances.Num() / 2);

	int32 ZeroAdvances = 0;
	double WorstRelativeError = 0.0;
	for (const double Advance : Advances)
	{
		if (Advance <= 0.0)
		{
			++ZeroAdvances;
		}

		WorstRelativeError = FMath::Max(
			WorstRelativeError, FMath::Abs(Advance - ExpectedAdvance) / ExpectedAdvance);
	}

	TestEqual(TEXT("插值后没有整帧不动"), ZeroAdvances, 0);
	TestTrue(TEXT("逐帧位移均匀（误差 < 2%）"), WorstRelativeError < 0.02);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
