// Copyright Voxel Plugin SAS. All Rights Reserved.
//
// 飞船角加速度换算的用例。被测函数是纯函数，不建 Actor、不依赖关卡。
//
// 断言三件事：
//   1. 鼠标冲量（机体本地，度/秒）→ 世界系角加速度（rad/s²），除以 dt；
//   2. 横滚是持续角加速度，到 MaxRollRate 时衰减到 0（与旧版「到顶就停」一致）；
//   3. dt 为 0 时返回零向量，不产生 inf。

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Spaceship/SpaceshipPawn.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraShipAngularInputTest,
	"StellarEra.Spaceship.AngularInput",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraShipAngularInputTest::RunTest(const FString& Parameters)
{
	const FQuat Identity = FQuat::Identity;
	const FVector NoAngularVelocity = FVector::ZeroVector;
	const float Dt = 0.02f;

	// ── 1. 鼠标冲量 → α = 冲量 / dt ──
	{
		const FVector Alpha = ASpaceshipPawn::ComputeAngularAccelerationWorldRadPerS2(
			Identity, FVector(0.f, 10.f, 20.f), NoAngularVelocity,
			/*RollInput=*/0.f, /*RollAlpha=*/1500.f, /*MaxRollRate=*/120.f, Dt);

		// FVector 的分量在 UE5 是 double，TestEqual 的 float/double 两组重载
		// 会因「参数 1 选 double 版、参数 2/3 选 float 版」而 C2666，故显式收窄。
		TestEqual(TEXT("α_y = pitch/dt"),
			float(Alpha.Y), float(FMath::DegreesToRadians(10.0)) / Dt, 1.0e-5f);
		TestEqual(TEXT("α_z = yaw/dt"),
			float(Alpha.Z), float(FMath::DegreesToRadians(20.0)) / Dt, 1.0e-5f);
		TestEqual(TEXT("没滚就不给 α_x"), float(Alpha.X), 0.f, 1.0e-6f);
	}

	// ── 2. 横滚到顶：衰减到 0 ──
	{
		const FVector AtMaxRate(FMath::DegreesToRadians(120.f), 0.f, 0.f);
		const FVector Alpha = ASpaceshipPawn::ComputeAngularAccelerationWorldRadPerS2(
			Identity, FVector::ZeroVector, AtMaxRate,
			/*RollInput=*/1.f, 1500.f, 120.f, Dt);

		TestEqual(TEXT("到 MaxRollRate 后角加速度为 0"), float(Alpha.X), 0.f, 1.0e-6f);
	}

	// ── 3. 半速：衰减一半 ──
	{
		const FVector HalfRate(FMath::DegreesToRadians(60.f), 0.f, 0.f);
		const FVector Alpha = ASpaceshipPawn::ComputeAngularAccelerationWorldRadPerS2(
			Identity, FVector::ZeroVector, HalfRate, 1.f, 1500.f, 120.f, Dt);

		TestEqual(TEXT("半速时是额定角加速度的一半"),
			float(Alpha.X), float(FMath::DegreesToRadians(750.0)), 1.0e-3f);
	}

	// ── 4. 反向滚同样有效（对称） ──
	{
		const FVector Alpha = ASpaceshipPawn::ComputeAngularAccelerationWorldRadPerS2(
			Identity, FVector::ZeroVector, NoAngularVelocity, -1.f, 1500.f, 120.f, Dt);

		TestEqual(TEXT("A 键给出负角加速度"),
			float(Alpha.X), -float(FMath::DegreesToRadians(1500.0)), 1.0e-3f);
	}

	// ── 5. dt = 0：零，不产生 inf ──
	{
		const FVector Alpha = ASpaceshipPawn::ComputeAngularAccelerationWorldRadPerS2(
			Identity, FVector(0.f, 10.f, 0.f), NoAngularVelocity, 0.f, 1500.f, 120.f, 0.f);

		TestTrue(TEXT("dt=0 返回零"), Alpha.IsNearlyZero());
	}

	// ── 6. 机身绕世界 Z 转 90°：本地 pitch 变成世界 −X ──
	{
		const FQuat Yaw90(FVector::UpVector, FMath::DegreesToRadians(90.f));
		const FVector Alpha = ASpaceshipPawn::ComputeAngularAccelerationWorldRadPerS2(
			Yaw90, FVector(0.f, 10.f, 0.f), NoAngularVelocity, 0.f, 1500.f, 120.f, Dt);

		// 本地 +Y 在 90° 偏航后指向世界 −X。
		TestTrue(TEXT("本地 Y 已转到世界 −X"), Alpha.X < -0.1f);
		TestEqual(TEXT("世界 Y 分量为零"), float(Alpha.Y), 0.f, 1.0e-5f);
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
