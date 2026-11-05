// Copyright Voxel Plugin SAS. All Rights Reserved.
//
// 探针接触求解的用例：力矩、摩擦、多接触点不发散、靠外力静止在球面上。
//
// 场景统一是「静止大球 + 一个带探针的小刚体」，关掉引力 —— 接触与引力混在一起
// 会分不清是哪一项把物体推开的。球质量取 1e6 让它的反冲可忽略，断言就能用解析值。

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "NBodySimulator.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	/** 球在原点、半径 SphereRadius；刚体居中放在 BodyCenter，探针为本地坐标。 */
	FNBodySimulator MakeProbeScene(
		double SphereRadius,
		const FVector3d& BodyCenter,
		const TArray<FVector3d>& Probes,
		double BodyMass = 1.0)
	{
		FNBodySimulator Simulator;
		Simulator.SetUnitSystem(ECosmosUnitSystem::Game);
		Simulator.SetIntegrator(ECosmosIntegrator::VelocityVerlet);
		Simulator.SetGravityCutoff(1.0e-9);

		FCosmosBodyState Sphere;
		Sphere.Mass = 1.0e6;
		Sphere.Position = FVector3d::ZeroVector;
		Sphere.Radius = SphereRadius;
		Simulator.AddBody(Sphere);

		FCosmosBodyState Body;
		Body.Mass = BodyMass;
		Body.Position = BodyCenter;
		Body.bRigidBody = true;
		Body.InverseInertiaLocal = FVector3d(1.0, 1.0, 1.0);
		Body.SetContactProbes(Probes);
		Body.Radius = 0.0;
		Simulator.AddBody(Body);

		return Simulator;
	}

	/** 最深的穿透量（<= 0 表示没接触）。探针按刚体当前位置算。 */
	double MaxPenetration(const FNBodySimulator& Simulator)
	{
		const FCosmosBodyState& Body = Simulator.GetBody(1);
		const FCosmosBodyState& Sphere = Simulator.GetBody(0);

		double Max = 0.0;
		for (const FVector3d& Probe : Body.ContactProbes)
		{
			const double Distance = (Body.Position + Probe - Sphere.Position).Size();
			Max = FMath::Max(Max, Sphere.Radius - Distance);
		}
		return Max;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraCosmosRigidBodyContactTest,
	"StellarEra.Cosmos.RigidBody.Contact",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraCosmosRigidBodyContactTest::RunTest(const FString& Parameters)
{
	// ── 1. 惯性张量公式 ──
	{
		// 质量 12、边长 (4, 8, 12)：Ixx = 12/12·(8² + 12²) = 208。
		const FVector3d Inv = FCosmosBodyState::InverseInertiaFromBox(12.0, FVector3d(2.0, 4.0, 6.0));
		TestEqual(TEXT("1/Ixx"), Inv.X, 1.0 / 208.0, 1.0e-15);
		TestEqual(TEXT("1/Iyy"), Inv.Y, 1.0 / (12.0 / 12.0 * (16.0 + 144.0)), 1.0e-15);
		TestEqual(TEXT("1/Izz"), Inv.Z, 1.0 / (12.0 / 12.0 * (16.0 + 64.0)), 1.0e-15);
		TestTrue(TEXT("退化（零质量）→ 零"),
			FCosmosBodyState::InverseInertiaFromBox(0.0, FVector3d(1.0)).IsZero());

		// 球：I = 2/5·m·r² = 0.4·5·4 = 8。
		const FVector3d InvSphere = FCosmosBodyState::InverseInertiaFromSphere(5.0, 2.0);
		TestEqual(TEXT("球 1/I"), InvSphere.X, 0.125, 1.0e-15);
	}

	// ── 2. 偏心探针产生力矩：r × n 非零 → 角速度沿 +X、−Y 方向 ──
	{
		// 球半径 10、刚体中心 (0,0,4)、探针 (5,5,0)。
		// 探针世界位置 (5,5,4)，距球心 8.124，穿透 1.876；
		// 力臂 r = (5,5,0)，r × n = (2.462, −2.462, 0)。
		FNBodySimulator Simulator = MakeProbeScene(
			10.0, FVector3d(0.0, 0.0, 4.0), {FVector3d(5.0, 5.0, 0.0)});
		Simulator.GetBodyMutable(1).Velocity = FVector3d(0.0, 0.0, -1.0);

		Simulator.StepExact(1.0e-5);

		const FVector3d Omega = Simulator.GetBody(1).AngularVelocity;
		TestTrue(TEXT("ω_x > 0"), Omega.X > 0.05);
		TestTrue(TEXT("ω_y < 0"), Omega.Y < -0.05);
		TestEqual(TEXT("ω_z 为零"), Omega.Z, 0.0, 1.0e-12);
	}

	// ── 3. 正中探针不产生力矩 ──
	{
		FNBodySimulator Simulator = MakeProbeScene(
			10.0, FVector3d(0.0, 0.0, 4.0), {FVector3d(0.0, 0.0, 1.0)});
		Simulator.GetBodyMutable(1).Velocity = FVector3d(0.0, 0.0, -1.0);

		Simulator.StepExact(1.0e-5);

		TestTrue(TEXT("力臂与法线共线 → ω 为零"),
			Simulator.GetBody(1).AngularVelocity.IsNearlyZero(1.0e-12));
	}

	// ── 4. 摩擦削掉切向速度，并把它变成角速度 ──
	{
		FNBodySimulator Simulator = MakeProbeScene(
			10.0, FVector3d(0.0, 0.0, 4.0), {FVector3d(0.0, 0.0, 1.0)});
		Simulator.GetBodyMutable(1).ContactFriction = 0.6;
		Simulator.GetBodyMutable(1).Velocity = FVector3d(1.0, 0.0, -1.0);

		Simulator.StepExact(1.0e-5);

		const FCosmosBodyState& Body = Simulator.GetBody(1);
		TestTrue(TEXT("切向速度被摩擦削掉"), FMath::Abs(Body.Velocity.X) < 0.99);
		TestEqual(TEXT("法向速度归零"), Body.Velocity.Z, 0.0, 1.0e-6);
		TestTrue(TEXT("摩擦产生角速度"), FMath::Abs(Body.AngularVelocity.Y) > 0.01);
	}

	// ── 5. 四个对称探针：收敛到「四个都落在球面上」，不越推越远 ──
	{
		FNBodySimulator Simulator = MakeProbeScene(10.0, FVector3d(0.0, 0.0, 4.0),
			{FVector3d(1.0, 0.0, 0.0), FVector3d(-1.0, 0.0, 0.0),
			 FVector3d(0.0, 1.0, 0.0), FVector3d(0.0, -1.0, 0.0)});

		for (int32 Step = 0; Step < 200; ++Step)
		{
			Simulator.StepExact(1.0e-5);
		}

		// 对称解：球心到每个探针都是 10，即 sqrt(1 + z²) = 10 → z = 9.9499。
		// 只沿一个方向猛推的话这里会冲到 4 倍穿透，z 明显大于该值。
		const double Z = Simulator.GetBody(1).Position.Z;
		TestEqual(TEXT("稳定在 sqrt(100−1)"), Z, 9.9499, 0.01);
		TestTrue(TEXT("穿透收敛掉"), MaxPenetration(Simulator) < 1.0e-3);
	}

	// ── 6. 持续外力下沉 → 静止在球面上，不抖不陷 ──
	{
		FNBodySimulator Simulator = MakeProbeScene(
			10.0, FVector3d(0.0, 0.0, 4.0), {FVector3d(0.0, 0.0, 1.0)});

		for (int32 Step = 0; Step < 200; ++Step)
		{
			Simulator.GetBodyMutable(1).ExternalAcceleration = FVector3d(0.0, 0.0, -1.0);
			Simulator.StepExact(1.0e-5);
		}

		const FCosmosBodyState& Body = Simulator.GetBody(1);
		TestEqual(TEXT("停在球面上（z = 9）"), Body.Position.Z, 9.0, 0.01);
		TestTrue(TEXT("没有残余穿透"), MaxPenetration(Simulator) < 1.0e-3);
		TestTrue(TEXT("没有攒出角速度"), Body.AngularVelocity.IsNearlyZero(1.0e-9));
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
