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

	// ── 7. 接触点超过 16 个：边收集边截断，但**最深**那个必须留下 ──
	{
		// 19 个等深的环状探针 + 1 个更深的正中探针（排在最后）。
		// 环上探针：世界中 (±2cosθ, 2sinθ, 4)，距球心 sqrt(20) → 穿透 10 − sqrt(20) ≈ 5.5279。
		// 正中探针：世界中 (0,0,4)，距球心 4 → 穿透 6。
		// 只留「先遇到的 16 个」的话位移由 5.5279 决定；保留最深者的实现给出 6。
		TArray<FVector3d> Probes;
		for (int32 Index = 0; Index < 19; ++Index)
		{
			const double Angle = 2.0 * UE_PI * Index / 19.0;
			Probes.Add(FVector3d(2.0 * FMath::Cos(Angle), 2.0 * FMath::Sin(Angle), 0.0));
		}
		Probes.Add(FVector3d(0.0, 0.0, 0.0));

		const FVector3d Start(0.0, 0.0, 4.0);
		FNBodySimulator Simulator = MakeProbeScene(10.0, Start, Probes);

		Simulator.StepExact(1.0e-5);

		// 位置修正的位移模长恒为「最深穿透 × 探针侧让路比例」，与推出方向无关。
		// 探针质量 1、球质量 1e6 → 比例 = 1e6/(1e6+1)。
		const double Share = 1.0e6 / (1.0e6 + 1.0);
		const double Moved = (Simulator.GetBody(1).Position - Start).Size();
		TestEqual(TEXT("截断后仍推掉最深那份穿透（6 × 份额）"), Moved, 6.0 * Share, 1.0e-3);
	}

	// ── 8. NaN 探针被跳过，不污染任何状态 ──
	{
		// `<=` 对 NaN 恒为 false，不显式判 IsFinite 的话它会被当成有效接触。
		TArray<FVector3d> Probes;
		Probes.Add(FVector3d(FMath::Sqrt(-1.0), 0.0, 0.0));   // sqrt(−1) → NaN
		Probes.Add(FVector3d(0.0, 0.0, 1.0));

		const FVector3d Start(0.0, 0.0, 4.0);
		FNBodySimulator Simulator = MakeProbeScene(10.0, Start, Probes);

		Simulator.StepExact(1.0e-5);

		const FCosmosBodyState& Body = Simulator.GetBody(1);
		TestTrue(TEXT("位置没有 NaN"), !Body.Position.ContainsNaN());
		TestTrue(TEXT("速度没有 NaN"), !Body.Velocity.ContainsNaN());
		TestTrue(TEXT("角速度没有 NaN"), !Body.AngularVelocity.ContainsNaN());

		// 有效探针 (0,0,1) 在世界 (0,0,5)：距球心 5 → 穿透 5，照常推 5×份额。
		const double Share = 1.0e6 / (1.0e6 + 1.0);
		TestEqual(TEXT("有效探针照常解算"),
			Body.Position.Z, 4.0 + 5.0 * Share, 1.0e-3);
	}

	// ── 9. 非对称多接触：推出方向按穿透量加权，不是简单平均 ──
	{
		// A：世界中 (0,0,4)，距球心 4 → 穿透 6，法线 (0,0,1)。
		// B：世界中 (5,0,4)，距球心 sqrt(41) → 穿透 10 − sqrt(41)，法线 (5,0,4)/sqrt(41)。
		// 推出方向 = normalize(6·nA + d_B·nB)。
		// 未加权的平均是 normalize(nA + nB)，X 分量 0.4330 vs 0.3224，差 0.11；
		// 乘上 6 之后位移相差约 0.66，远超这里的容差。
		const FVector3d Start(0.0, 0.0, 4.0);
		FNBodySimulator Simulator = MakeProbeScene(10.0, Start,
			{FVector3d(0.0, 0.0, 0.0), FVector3d(5.0, 0.0, 0.0)});

		Simulator.StepExact(1.0e-5);

		const double DistanceB = FMath::Sqrt(41.0);
		const double DepthB = 10.0 - DistanceB;
		const FVector3d NormalB(5.0 / DistanceB, 0.0, 4.0 / DistanceB);
		const FVector3d Push =
			(FVector3d(0.0, 0.0, 6.0) + NormalB * DepthB).GetSafeNormal();

		const double Share = 1.0e6 / (1.0e6 + 1.0);
		const FVector3d Expected = Start + Push * (6.0 * Share);

		const FVector3d Actual = Simulator.GetBody(1).Position;
		TestEqual(TEXT("X：按穿透量加权"), Actual.X, Expected.X, 1.0e-6);
		TestEqual(TEXT("Z：按穿透量加权"), Actual.Z, Expected.Z, 1.0e-6);
		TestEqual(TEXT("Y 仍为零（两法线都在 XZ 面内）"), Actual.Y, 0.0, 1.0e-12);
	}

	// ── 10. 两个接触点同时滑动：切向冲量**累计**夹在摩擦锥里 ──
	{
		// 两个探针在机身 ±X 两侧、法线几乎同向朝 +Z；机身带很大的切向速度沿 +Y 滑。
		// 摩擦每轮都想再削一截，但切向速度远大于 μλ_n，锥一直满着：
		// 按「本轮增量」夹的话 4 轮累计可达 4×μλ_n，按「累计量」夹则总量 ≤ μλ_n。
		const FVector3d Start(0.0, 0.0, 9.9);
		FNBodySimulator Simulator = MakeProbeScene(10.0, Start,
			{FVector3d(0.3, 0.0, 0.05), FVector3d(-0.3, 0.0, 0.05)});
		Simulator.GetBodyMutable(1).ContactFriction = 0.1;
		Simulator.GetBodyMutable(1).Velocity = FVector3d(0.0, 1.0, -1.0);

		Simulator.StepExact(1.0e-5);

		const FCosmosBodyState& Body = Simulator.GetBody(1);

		// 两个接触的法线都没有 Y 分量、X 分量两侧抵消，所以法向冲量总量可以从
		// 线动量反推：Δv_z = Σλ_n · n_z / m，探针质量 1。
		const FVector3d ProbeWorld(Start + FVector3d(0.3, 0.0, 0.05));
		const double NormalZ = ProbeWorld.Z / ProbeWorld.Size();
		const double NormalImpulseTotal = (Body.Velocity.Z - (-1.0)) / NormalZ;

		// 切向同理：所有摩擦冲量都在 ±Y 上（切向速度、力矩都保持在这个对称面上），
		// 所以 Δv_y 就是切向冲量总量（质量 1，标量）。
		const double TangentialImpulse = FMath::Abs(Body.Velocity.Y - 1.0);

		TestTrue(TEXT("摩擦确实生效（下面的上界不是恒真）"),
			TangentialImpulse > 0.5 * 0.1 * NormalImpulseTotal);
		TestTrue(TEXT("切向冲量总量 ≤ μ·Σλ_n（累计裁剪，不是每轮各夹一次）"),
			TangentialImpulse <= 0.1 * NormalImpulseTotal * (1.0 + 1.0e-4) + 1.0e-12);
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
