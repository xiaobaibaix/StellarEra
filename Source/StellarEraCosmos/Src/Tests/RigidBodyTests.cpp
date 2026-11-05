// Copyright Voxel Plugin SAS. All Rights Reserved.
//
// 求解器刚体姿态的用例：角加速度积分、四元数归一化、角阻尼、非刚体零影响。
//
// 不建 Actor、不碰世界：直接喂 FCosmosBodyState 给求解器，靠 StepExact 驱动。
// 关闭引力截断是为了让「无引力」这件事显式 —— 单天体本来就没有引力项。

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "NBodySimulator.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	/** 一个刚体、无引力、Velocity Verlet 的求解器。返回时 Bodies[0] 即该刚体。 */
	FNBodySimulator MakeSingleRigidBody()
	{
		FNBodySimulator Simulator;
		Simulator.SetUnitSystem(ECosmosUnitSystem::Game);
		Simulator.SetIntegrator(ECosmosIntegrator::VelocityVerlet);
		Simulator.SetGravityCutoff(1.0e-9);

		FCosmosBodyState Body;
		Body.Mass = 1.0;
		Body.bRigidBody = true;
		Body.InverseInertiaLocal = FVector3d(1.0, 1.0, 1.0);
		Simulator.AddBody(Body);

		return Simulator;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraCosmosRigidBodyOrientationTest,
	"StellarEra.Cosmos.RigidBody.Orientation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraCosmosRigidBodyOrientationTest::RunTest(const FString& Parameters)
{
	// ── 1. ω = ∫α dt ──
	{
		FNBodySimulator Simulator = MakeSingleRigidBody();
		Simulator.GetBodyMutable(0).ExternalAngularAcceleration = FVector3d(0.0, 0.0, 2.0);

		const double Dt = 1.0e-3;
		Simulator.StepExact(Dt);

		TestEqual(TEXT("ω_z = α_z·dt"),
			Simulator.GetBody(0).AngularVelocity.Z, 2.0 * Dt, 1.0e-12);
		TestEqual(TEXT("ω_x 仍为零"),
			Simulator.GetBody(0).AngularVelocity.X, 0.0, 1.0e-15);
	}

	// ── 2. 四元数保持单位长度 ──
	{
		FNBodySimulator Simulator = MakeSingleRigidBody();
		Simulator.GetBodyMutable(0).AngularVelocity = FVector3d(0.3, -1.7, 2.9);

		for (int32 Step = 0; Step < 1000; ++Step)
		{
			Simulator.StepExact(1.0e-4);
		}

		const FQuat4d& Q = Simulator.GetBody(0).Orientation;
		const double Norm = FMath::Sqrt(Q.X * Q.X + Q.Y * Q.Y + Q.Z * Q.Z + Q.W * Q.W);
		TestEqual(TEXT("|q| = 1"), Norm, 1.0, 1.0e-12);
		TestTrue(TEXT("确实转了（不是恒等）"), FMath::Abs(Q.Z) > 1.0e-6);
	}

	// ── 3. 角阻尼：ω ← ω·(1 − k·dt) ──
	{
		FNBodySimulator Simulator = MakeSingleRigidBody();
		FCosmosBodyState& State = Simulator.GetBodyMutable(0);
		State.AngularVelocity = FVector3d(1.0, 0.0, 0.0);
		State.AngularDamping = 100.0;

		const double Dt = 1.0e-3;
		Simulator.StepExact(Dt);

		TestEqual(TEXT("ω 按 (1 − k·dt) 衰减"),
			Simulator.GetBody(0).AngularVelocity.X, 1.0 * (1.0 - 100.0 * Dt), 1.0e-12);
	}

	// ── 3b. k·dt ≥ 1 时归零，不反号 ──
	{
		FNBodySimulator Simulator = MakeSingleRigidBody();
		FCosmosBodyState& State = Simulator.GetBodyMutable(0);
		State.AngularVelocity = FVector3d(1.0, 0.0, 0.0);
		State.AngularDamping = 1.0e6;

		Simulator.StepExact(1.0e-3);

		TestTrue(TEXT("严重过阻尼时不反向"), Simulator.GetBody(0).AngularVelocity.IsNearlyZero());
	}

	// ── 4. 非刚体完全不受影响 ──
	{
		FNBodySimulator Simulator = MakeSingleRigidBody();
		FCosmosBodyState& State = Simulator.GetBodyMutable(0);
		State.bRigidBody = false;
		State.AngularVelocity = FVector3d(5.0, 5.0, 5.0);
		State.ExternalAngularAcceleration = FVector3d(5.0, 5.0, 5.0);

		Simulator.StepExact(1.0e-3);

		const FCosmosBodyState& After = Simulator.GetBody(0);
		TestTrue(TEXT("角速度不变"), After.AngularVelocity.Equals(FVector3d(5.0, 5.0, 5.0)));
		TestTrue(TEXT("朝向不变"), After.Orientation.Equals(FQuat4d::Identity));
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
