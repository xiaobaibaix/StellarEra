// Copyright Voxel Plugin SAS. All Rights Reserved.
//
// N 体求解器「预测」与「接触」的自动化测试：纯数学，不依赖世界/编辑器。用例都是
// StellarEra.Cosmos.*，跑法见 CLAUDE.md。

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "NBodySimulator.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	/** 造一个「大质量中心 + 一颗圆轨道行星」的两体系统。 */
	FNBodySimulator MakeCircularTwoBody(double OrbitRadiusAu, double CentralMassSolar)
	{
		FNBodySimulator Simulator;
		Simulator.SetUnitSystem(ECosmosUnitSystem::Game);
		Simulator.SetIntegrator(ECosmosIntegrator::VelocityVerlet);
		Simulator.SetSofteningLength(1.0e-5);
		Simulator.SetCentimetersPerAu(1.0e6);

		FCosmosBodyState Central;
		Central.Mass = CentralMassSolar;
		Central.Position = FVector3d::ZeroVector;
		Simulator.AddBody(Central);

		// 圆轨道速度 v = √(G·M/r)。G = 4π² 时也就是 2π√(M/r)。
		const double Speed = FMath::Sqrt(
			CosmosUnits::GravitationalConstant_Game * CentralMassSolar / OrbitRadiusAu);

		FCosmosBodyState Planet;
		Planet.Mass = 3.0034893e-6;   // 地球质量
		Planet.Position = FVector3d(OrbitRadiusAu, 0.0, 0.0);
		Planet.Velocity = FVector3d(0.0, Speed, 0.0);
		Simulator.AddBody(Planet);

		Simulator.MarkEnergyBaseline();
		return Simulator;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraNBodyPredictionTest,
	"StellarEra.Cosmos.NBodyPrediction",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraNBodyPredictionTest::RunTest(const FString& Parameters)
{
	const double G = CosmosUnits::GravitationalConstant_Game;

	// ── 1. 配对周期公式：钉住具体数值 ──
	// 期望值是公式的**精确输出**，行星质量 3e-6 算进去了；别「化简」回 √(r³/m_中心)，
	// 那会放过「公式漏掉行星质量」这类错误。
	// 前两行有 ~2e-8 偏移：UE 的 PI 宏是 float 字面量（6.2831854820251465），不是双精度 2π，别照着后者「修正」。
	{
		FCosmosBodyState A;
		A.Mass = 1.0;
		A.Position = FVector3d::ZeroVector;

		FCosmosBodyState B;
		B.Mass = 1.0;
		B.Position = FVector3d(1.0, 0.0, 0.0);
		TestEqual(TEXT("周期 1AU/两倍太阳质量"),
			FNBodySimulator::PairOrbitalPeriod(A, B, G), 0.70710678, 1.0e-6);

		B.Position = FVector3d(4.0, 0.0, 0.0);
		TestEqual(TEXT("周期 4AU/两倍太阳质量"),
			FNBodySimulator::PairOrbitalPeriod(A, B, G), 5.65685425, 1.0e-6);

		// 本关卡的真实几何：0.718 AU 绕 1 M☉（行星质量 3e-6 算进去了）。
		B.Mass = 3.0034893e-6;
		B.Position = FVector3d(0.718, 0.0, 0.0);
		TestEqual(TEXT("周期 0.718AU/1太阳质量（本关卡）"),
			FNBodySimulator::PairOrbitalPeriod(A, B, G), 0.60839555, 1.0e-6);

		// 退化情形必须返回 0（而不是 inf / nan）。
		B.Position = A.Position;
		TestEqual(TEXT("间距为零返回 0"),
			FNBodySimulator::PairOrbitalPeriod(A, B, G), 0.0);

		B.Position = FVector3d(1.0, 0.0, 0.0);
		B.Mass = 0.0;
		A.Mass = 0.0;
		TestEqual(TEXT("总质量为零返回 0"),
			FNBodySimulator::PairOrbitalPeriod(A, B, G), 0.0);
	}

	// ── 2. 最短时标取「配对最小值」：恒星 + 近行星(0.5AU) + 远行星(4AU)，恒-近最短 ──
	{
		FNBodySimulator Simulator;
		Simulator.SetUnitSystem(ECosmosUnitSystem::Game);

		FCosmosBodyState Star;
		Star.Mass = 1.0;
		Simulator.AddBody(Star);

		FCosmosBodyState NearPlanet;
		NearPlanet.Mass = 3.0034893e-6;
		NearPlanet.Position = FVector3d(0.5, 0.0, 0.0);
		Simulator.AddBody(NearPlanet);

		FCosmosBodyState FarPlanet;
		FarPlanet.Mass = 3.0034893e-6;
		FarPlanet.Position = FVector3d(4.0, 0.0, 0.0);
		Simulator.AddBody(FarPlanet);

		// √(r³/(1+m_行星))，r 取 0.5 与 4。行星质量在这两行里同样算进去了。
		TestEqual(TEXT("恒星的最短时标 = 与近行星那一对"),
			Simulator.EstimateOrbitalPeriod(0), 0.35355287, 1.0e-6);
		TestEqual(TEXT("近行星的最短时标 = 与恒星那一对"),
			Simulator.EstimateOrbitalPeriod(1), 0.35355287, 1.0e-6);
		TestEqual(TEXT("远行星的最短时标 = 与恒星那一对"),
			Simulator.EstimateOrbitalPeriod(2), 7.99998821, 1.0e-6);

		// 只有一颗天体时无解。
		FNBodySimulator Single;
		Single.SetUnitSystem(ECosmosUnitSystem::Game);
		Single.AddBody(Star);
		TestEqual(TEXT("单天体系统的时标为 0"),
			Single.EstimateOrbitalPeriod(0), 0.0);
	}

	// ── 3. 预测一圈之后闭合 ──
	{
		FNBodySimulator Simulator = MakeCircularTwoBody(0.718, 1.0);
		const double Period = Simulator.EstimateOrbitalPeriod(1);
		TestTrue(TEXT("估算周期为正"), Period > 0.0);

		TArray<FVector3d> Predicted;
		TestTrue(TEXT("预测调用成功"),
			Simulator.PredictTrajectory(1, Period, 256, Predicted));
		TestEqual(TEXT("预测点数 = 采样数"), Predicted.Num(), 256);

		const double ClosureError = (Predicted.Last() - Predicted[0]).Size();
		TestTrue(TEXT("一圈之后末点回到首点附近（< 1% 轨道半径）"),
			ClosureError < 0.01 * 0.718);
	}

	// ── 4. 预测不得改动被预测的求解器 ──
	// 最危险的 bug：预测把真实模拟带跑偏，PIE 里表现为「轨道慢慢变形」，易被当成积分器精度问题。
	{
		FNBodySimulator Simulator = MakeCircularTwoBody(0.718, 1.0);

		TArray<FVector3d> PositionsBefore;
		TArray<FVector3d> VelocitiesBefore;
		for (int32 i = 0; i < Simulator.GetNumBodies(); ++i)
		{
			PositionsBefore.Add(Simulator.GetBody(i).Position);
			VelocitiesBefore.Add(Simulator.GetBody(i).Velocity);
		}
		const double TimeBefore = Simulator.GetSimulatedTime();
		const double DriftBefore = Simulator.GetEnergyDriftRatio();
		const int32 CountBefore = Simulator.GetNumBodies();

		TArray<FVector3d> Predicted;
		Simulator.PredictTrajectory(1, 0.608, 64, Predicted);

		TestEqual(TEXT("预测后天体数不变"), Simulator.GetNumBodies(), CountBefore);
		TestTrue(TEXT("预测后模拟时间逐位不变"),
			Simulator.GetSimulatedTime() == TimeBefore);
		TestTrue(TEXT("预测后能量漂移逐位不变"),
			Simulator.GetEnergyDriftRatio() == DriftBefore);

		for (int32 i = 0; i < Simulator.GetNumBodies(); ++i)
		{
			const FCosmosBodyState& State = Simulator.GetBody(i);
			TestTrue(*FString::Printf(TEXT("预测后天体 %d 位置逐位不变"), i),
				State.Position == PositionsBefore[i]);
			TestTrue(*FString::Printf(TEXT("预测后天体 %d 速度逐位不变"), i),
				State.Velocity == VelocitiesBefore[i]);
		}
	}

	// ── 5. 非法入参走失败分支 ──
	{
		FNBodySimulator Simulator = MakeCircularTwoBody(0.718, 1.0);
		TArray<FVector3d> Predicted;

		Predicted.Add(FVector3d(1.0, 2.0, 3.0));
		TestFalse(TEXT("索引越界返回 false"), Simulator.PredictTrajectory(99, 1.0, 16, Predicted));
		TestEqual(TEXT("失败时清空输出"), Predicted.Num(), 0);

		TestFalse(TEXT("窗口为零返回 false"), Simulator.PredictTrajectory(1, 0.0, 16, Predicted));
		TestFalse(TEXT("采样点不足返回 false"), Simulator.PredictTrajectory(1, 1.0, 1, Predicted));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraNBodyContactTest,
	"StellarEra.Cosmos.NBodyContact",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraNBodyContactTest::RunTest(const FString& Parameters)
{
	// ── 1. 接触的纯几何：dt = 0，StepExact 只剩 ResolveContacts，期望值可按公式钉死 ──
	// 总动量不为零（3×0.05 + 1×(−0.01) = 0.14），同时挡两种写错：只推一颗的；
	// 抹掉各自**绝对**法向速度的（那会把总动量吃成 0）。
	{
		FNBodySimulator Simulator;
		Simulator.SetUnitSystem(ECosmosUnitSystem::Game);
		Simulator.SetIntegrator(ECosmosIntegrator::VelocityVerlet);
		Simulator.SetSofteningLength(1.0e-5);

		FCosmosBodyState Heavy;
		Heavy.Mass = 3.0;
		Heavy.Radius = 0.05;
		Heavy.Position = FVector3d(-0.04, 0.0, 0.0);
		Heavy.Velocity = FVector3d(0.05, 0.0, 0.0);
		Simulator.AddBody(Heavy);

		FCosmosBodyState Light;
		Light.Mass = 1.0;
		Light.Radius = 0.05;
		Light.Position = FVector3d(0.04, 0.0, 0.0);
		Light.Velocity = FVector3d(-0.01, 0.0, 0.0);
		Simulator.AddBody(Light);

		const FVector3d MomentumBefore = Simulator.GetTotalMomentum();

		Simulator.StepExact(0.0);

		// 半径和 0.1、间距 0.08 → 重叠 0.02；让路比例 = 对方质量/总质量：Heavy 让 1/4 → −0.045，Light 让 3/4 → +0.055。
		TestEqual(TEXT("重的一方少让路"),
			Simulator.GetBody(0).Position.X, -0.045, 1.0e-15);
		TestEqual(TEXT("轻的一方多让路"),
			Simulator.GetBody(1).Position.X, 0.055, 1.0e-15);
		TestEqual(TEXT("推开后正好相切（间距 = 半径之和）"),
			(Simulator.GetBody(1).Position - Simulator.GetBody(0).Position).Size(),
			0.1, 1.0e-15);

		// 相对法向速度被抹平 → 沿 X 速度**相等**（不是都变 0），由动量守恒定：0.14/4 = 0.035。
		TestEqual(TEXT("接触后两者法向速度相等"),
			Simulator.GetBody(0).Velocity.X, Simulator.GetBody(1).Velocity.X, 1.0e-15);
		TestEqual(TEXT("相等值 = 总动量 / 总质量 = 0.035"),
			Simulator.GetBody(0).Velocity.X, 0.035, 1.0e-15);

		TestTrue(TEXT("接触不改变总动量"),
			(Simulator.GetTotalMomentum() - MomentumBefore).Size() < 1.0e-15);

		// 切向分量必须原样留着。重摆成迎面接近：否则相对法向速度已是 0、接触分支不执行、断言恒真。
		Simulator.GetBodyMutable(0).Position = FVector3d(-0.04, 0.0, 0.0);
		Simulator.GetBodyMutable(1).Position = FVector3d(0.04, 0.0, 0.0);
		Simulator.GetBodyMutable(0).Velocity = FVector3d(0.05, 0.007, 0.0);
		Simulator.GetBodyMutable(1).Velocity = FVector3d(-0.01, -0.003, 0.0);

		Simulator.StepExact(0.0);

		TestEqual(TEXT("切向速度不被接触改动（重）"),
			Simulator.GetBody(0).Velocity.Y, 0.007, 1.0e-15);
		TestEqual(TEXT("切向速度不被接触改动（轻）"),
			Simulator.GetBody(1).Velocity.Y, -0.003, 1.0e-15);

		// 同时确认法向确实被处理了，否则上面两条可能只是分支没执行而通过。
		TestEqual(TEXT("同一步里法向速度仍被抹平"),
			Simulator.GetBody(0).Velocity.X, Simulator.GetBody(1).Velocity.X, 1.0e-15);
	}

	// ── 2. 自由落体落到地表并停住。取本关卡实际值（0.873237 M☉ / 0.075 AU；飞船质量 0） ──
	{
		FNBodySimulator Simulator;
		Simulator.SetUnitSystem(ECosmosUnitSystem::Game);
		Simulator.SetIntegrator(ECosmosIntegrator::VelocityVerlet);
		Simulator.SetSofteningLength(1.0e-5);
		Simulator.SetCentimetersPerAu(1.0e6);

		FCosmosBodyState Planet;
		Planet.Mass = 0.873237;
		Planet.Radius = 0.075;
		Simulator.AddBody(Planet);

		FCosmosBodyState Ship;
		Ship.Mass = 0.0;
		Ship.Radius = 0.0;
		Ship.Position = FVector3d(0.076, 0.0, 0.0);   // 地表上方 0.001 AU
		Simulator.AddBody(Ship);

		const double Step = 1.0e-6;
		const int32 NumSteps = 5000;                  // 0.005 年，自由落体只需约 6e-4 年
		double DeepestBelowSurface = 0.0;

		for (int32 StepIndex = 0; StepIndex < NumSteps; ++StepIndex)
		{
			Simulator.StepExact(Step);

			const double Distance = Simulator.GetBody(1).Position.Size();
			DeepestBelowSurface = FMath::Max(DeepestBelowSurface, 0.075 - Distance);
		}

		const FCosmosBodyState& FinalShip = Simulator.GetBody(1);

		TestTrue(TEXT("落到地表：距离收敛到半径"),
			FMath::Abs(FinalShip.Position.Size() - 0.075) < 1.0e-12);

		// 全程没穿透：钳制不留余量、每步末尾执行。1e-12 是浮点舍入不是穿透余量 —— 放大到 1e-3 会放过「每步下陷一点」。
		TestTrue(TEXT("全程没有穿透地表"),
			DeepestBelowSurface < 1.0e-12);

		// 停住而非弹跳：每步重力都给 a·dt ≈ 6.1e-3 AU/年 的接近速度，是接触解算抹回去的。
		TestTrue(TEXT("法向速度归零（停住、不回弹）"),
			FMath::Abs(FinalShip.Velocity.X) < 1.0e-12);

		// 质量 0 的飞船推不动行星。
		TestEqual(TEXT("行星没有被飞船推走"),
			Simulator.GetBody(0).Position.X, 0.0, 1.0e-15);
	}

	// ── 3. 对照组：去掉半径，同一初态就会穿过去。反证「接触项确实是拦住它的那一项」 ──
	{
		FNBodySimulator Simulator;
		Simulator.SetUnitSystem(ECosmosUnitSystem::Game);
		Simulator.SetIntegrator(ECosmosIntegrator::VelocityVerlet);
		Simulator.SetSofteningLength(1.0e-5);

		FCosmosBodyState Planet;
		Planet.Mass = 0.873237;
		Planet.Radius = 0.0;                          // ← 与第 2 组唯一的差别
		Simulator.AddBody(Planet);

		FCosmosBodyState Ship;
		Ship.Mass = 0.0;
		Ship.Radius = 0.0;
		Ship.Position = FVector3d(0.076, 0.0, 0.0);
		Simulator.AddBody(Ship);

		double MinDistance = TNumericLimits<double>::Max();
		for (int32 StepIndex = 0; StepIndex < 5000; ++StepIndex)
		{
			Simulator.StepExact(1.0e-6);
			MinDistance = FMath::Min(MinDistance, Simulator.GetBody(1).Position.Size());
		}

		TestTrue(TEXT("没有半径时飞船穿过地表（对照）"), MinDistance < 0.070);
	}

	// ── 4. 飞船撞地即停：bZeroVelocityOnContact 把整条速度钉到行星上 ──
	// 默认光滑硬球保留切向（沿地表滑）；开了这一项后整条速度等于行星速度，切向一并被抹掉。
	{
		FNBodySimulator Simulator;
		Simulator.SetUnitSystem(ECosmosUnitSystem::Game);
		Simulator.SetIntegrator(ECosmosIntegrator::VelocityVerlet);
		Simulator.SetSofteningLength(1.0e-5);

		FCosmosBodyState Planet;
		Planet.Mass = 0.873237;
		Planet.Radius = 0.075;
		Planet.Velocity = FVector3d(0.0, 0.02, 0.0);   // 行星自己在动
		Simulator.AddBody(Planet);

		FCosmosBodyState Ship;
		Ship.Mass = 0.0;
		Ship.Radius = 0.0;
		Ship.Position = FVector3d(0.074, 0.0, 0.0);     // 地表内（< 0.075），立刻接触
		// 既有法向接近（X），也有切向（Y）—— 斜撞，看切向是否也被清掉。
		Ship.Velocity = FVector3d(-0.05, 0.03, 0.0);
		Ship.bZeroVelocityOnContact = true;
		Simulator.AddBody(Ship);

		Simulator.StepExact(0.0);

		const FCosmosBodyState& FinalShip = Simulator.GetBody(1);

		// 整条速度都被钉到行星速度上：X、Y 都等于行星，切向滑行分量不复存在。
		TestEqual(TEXT("撞地后飞船速度 = 行星速度（法向 X）"),
			FinalShip.Velocity.X, 0.0, 1.0e-15);
		TestEqual(TEXT("撞地后飞船速度 = 行星速度（切向 Y 一并清零）"),
			FinalShip.Velocity.Y, 0.02, 1.0e-15);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraNBodyExternalForceTest,
	"StellarEra.Cosmos.NBodyExternalForce",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraNBodyExternalForceTest::RunTest(const FString& Parameters)
{
	// ── 1. 常力下的抛物线是**精确**的 ──
	// Velocity Verlet 对「加速度与位置、速度无关」的力是精确解（x₀ + v₀t + ½at²，与步长无关），
	// 1e-9 容差只装浮点累加误差。这条钉住「外力真的进了积分器」：每帧只加一次、忘乘 dt 都会偏出十万八千里。
	{
		FNBodySimulator Simulator;
		Simulator.SetUnitSystem(ECosmosUnitSystem::Game);
		Simulator.SetIntegrator(ECosmosIntegrator::VelocityVerlet);
		Simulator.SetSofteningLength(1.0e-5);

		FCosmosBodyState Body;            // 单天体 = 没有引力，只剩外力
		Body.Mass = 0.0;
		Body.Position = FVector3d::ZeroVector;
		Body.Velocity = FVector3d(1.0, 0.0, 0.0);
		Body.ExternalAcceleration = FVector3d(0.0, -2.0, 0.0);
		Simulator.AddBody(Body);

		const double Dt = 0.01;
		const int32 NumSteps = 1000;      // t = 10 年
		for (int32 Step = 0; Step < NumSteps; ++Step)
		{
			Simulator.StepExact(Dt);
		}

		const FVector3d FinalPosition = Simulator.GetBody(0).Position;
		const FVector3d FinalVelocity = Simulator.GetBody(0).Velocity;

		// x = 0 + 1×10 = 10；y = 0 − ½×2×10² = −100；z 不动
		TestEqual(TEXT("常力：末位置 X"), FinalPosition.X, 10.0, 1.0e-9);
		TestEqual(TEXT("常力：末位置 Y"), FinalPosition.Y, -100.0, 1.0e-9);
		TestEqual(TEXT("常力：末位置 Z"), FinalPosition.Z, 0.0, 1.0e-9);

		// vx 不变 = 1；vy = 0 − 2×10 = −20
		TestEqual(TEXT("常力：末速度 X"), FinalVelocity.X, 1.0, 1.0e-9);
		TestEqual(TEXT("常力：末速度 Y"), FinalVelocity.Y, -20.0, 1.0e-9);
	}

	// ── 2. 阻尼是指数衰减，且误差随步长**线性**下降 ──
	// 阻尼项用**踢之前**的速度 → 显式欧拉，v_N = v₀(1−k·dt)^N，恒小于解析解。钉「一阶」
	// 而不只钉数值，是为了挡住「改用踢之后的速度」（隐式欧拉，误差反号，数值照样能过）。
	{
		const double K = 2.0;             // 1/年
		const double V0 = 1.0;
		const double Exact = V0 * FMath::Exp(-K * 1.0);   // t = 1 年

		// 单天体 + 只有阻尼，没有引力也没有外力。
		auto RunDecay = [](double Dt, int32 NumSteps, double K, double V0) -> double
		{
			FNBodySimulator Simulator;
			Simulator.SetUnitSystem(ECosmosUnitSystem::Game);
			Simulator.SetIntegrator(ECosmosIntegrator::VelocityVerlet);

			FCosmosBodyState Body;
			Body.Mass = 0.0;
			Body.Velocity = FVector3d(V0, 0.0, 0.0);
			Body.LinearDamping = K;
			Simulator.AddBody(Body);

			for (int32 Step = 0; Step < NumSteps; ++Step)
			{
				Simulator.StepExact(Dt);
			}
			return Simulator.GetBody(0).Velocity.X;
		};

		const double Coarse = RunDecay(1.0e-4, 10000, K, V0);   // 10000 × 1e-4 = 1 年
		const double Fine = RunDecay(5.0e-5, 20000, K, V0);     // 20000 × 5e-5 = 1 年

		const double ErrCoarse = FMath::Abs(Coarse - Exact);
		const double ErrFine = FMath::Abs(Fine - Exact);

		TestTrue(TEXT("阻尼：粗步长解低于解析解（显式欧拉的符号）"), Coarse < Exact);
		TestTrue(TEXT("阻尼：误差在 1e-4 以内"), ErrCoarse < 1.0e-4);
		TestTrue(TEXT("阻尼：误差不为零（真的在积分，不是抄了解析解）"), ErrCoarse > 1.0e-8);
		// 期望比值 ≈ 0.5（一阶），0.6 留余量，挡得住零阶（比值 1）但挡不住二阶
		// （0.25 < 0.6）。二阶由上面 ErrCoarse > 1e-8 那条挡（二阶误差约 1e-9），别删。
		TestTrue(TEXT("阻尼：步长减半，误差减半（一阶收敛）"), ErrFine < ErrCoarse * 0.6);
	}

	// ── 3. 终端速度 v → A/k ──
	// 阻尼下按 W 会收敛到「推力 × τ」，τ = 1/k。离散系统的不动点**恰好**是 A/k；
	// 写成 A/(k·dt) 或 A·k 都会红。
	{
		const double A = 2.0;
		const double K = 4.0;
		const double Terminal = A / K;    // 0.5

		FNBodySimulator Simulator;
		Simulator.SetUnitSystem(ECosmosUnitSystem::Game);
		Simulator.SetIntegrator(ECosmosIntegrator::VelocityVerlet);

		FCosmosBodyState Body;            // 从静止起，只有常力 + 阻尼
		Body.Mass = 0.0;
		Body.ExternalAcceleration = FVector3d(A, 0.0, 0.0);
		Body.LinearDamping = K;
		Simulator.AddBody(Body);

		const double Dt = 1.0e-3;         // k·dt = 4e-3，稳定性余量三个数量级
		for (int32 Step = 0; Step < 5000; ++Step)   // t = 5 年 ≫ 1/k = 0.25 年
		{
			Simulator.StepExact(Dt);
		}

		const double Final = Simulator.GetBody(0).Velocity.X;

		TestTrue(TEXT("终端速度：从下方逼近 A/k"), Final < Terminal);
		TestEqual(TEXT("终端速度 = A/k"), Final, Terminal, 1.0e-6);
	}

	// ── 4. 预测副本不继承外力 ──
	// 值拷贝会把 ExternalAcceleration / LinearDamping 带过去，星图就画出「一直加速」的线。
	// 用「一份设外力一份不设」逐点比对，比「主求解器没被改动」更严。
	{
		auto MakePair = [](bool bForced)
		{
			FNBodySimulator Simulator = MakeCircularTwoBody(0.718, 1.0);
			if (bForced)
			{
				// 施在下标 1（下面预测的那个）—— 副本若继承外力，0.608 年里预测线会偏 ≈ 0.92 AU。
				Simulator.GetBodyMutable(1).ExternalAcceleration = FVector3d(0.0, 5.0, 0.0);
				Simulator.GetBodyMutable(1).LinearDamping = 3.0;
			}
			return Simulator;
		};

		FNBodySimulator Forced = MakePair(true);
		FNBodySimulator Control = MakePair(false);

		TArray<FVector3d> ForcedPoints;
		TArray<FVector3d> ControlPoints;
		TestTrue(TEXT("预测调用成功（带外力）"),
			Forced.PredictTrajectory(1, 0.608, 64, ForcedPoints));
		TestTrue(TEXT("预测调用成功（无外力对照）"),
			Control.PredictTrajectory(1, 0.608, 64, ControlPoints));

		TestEqual(TEXT("预测点数一致"), ForcedPoints.Num(), ControlPoints.Num());

		// 逐点比、只数「不一样」的个数（64 次 Printf 会淹日志）；同一函数造出的两份必须逐位相同。
		const int32 Count = FMath::Min(ForcedPoints.Num(), ControlPoints.Num());
		int32 Mismatched = 0;
		for (int32 Index = 0; Index < Count; ++Index)
		{
			if (ForcedPoints[Index] != ControlPoints[Index])
			{
				++Mismatched;
			}
		}
		TestEqual(TEXT("预测点逐位相同：外力没有渗进预测副本"), Mismatched, 0);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraNBodyTestParticleTest,
	"StellarEra.Cosmos.NBodyTestParticle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraNBodyTestParticleTest::RunTest(const FString& Parameters)
{
	const double G = CosmosUnits::GravitationalConstant_Game;

	// 两体：中心天体 + 候选测试粒子，距离 1 AU、质量各 1 M☉ —— 质量取大是让「粒子仍在施力」可见。
	auto MakePair = [](bool bTestParticle)
	{
		FNBodySimulator Simulator;
		Simulator.SetUnitSystem(ECosmosUnitSystem::Game);
		Simulator.SetIntegrator(ECosmosIntegrator::VelocityVerlet);
		Simulator.SetSofteningLength(1.0e-5);

		FCosmosBodyState Central;
		Central.Mass = 1.0;
		Simulator.AddBody(Central);

		FCosmosBodyState Particle;
		Particle.Mass = 1.0;
		Particle.Position = FVector3d(1.0, 0.0, 0.0);
		Particle.bTestParticle = bTestParticle;
		Simulator.AddBody(Particle);

		return Simulator;
	};

	// ── 1. 粒子仍被吸引，中心天体不受它吸引。挡「把测试粒子的引力整条关掉」 ──
	{
		FNBodySimulator Simulator = MakePair(true);

		TestEqual(TEXT("测试粒子仍被中心天体吸引（−G·M/r²）"),
			Simulator.GetBody(1).Acceleration.X, -G, 1.0e-6);
		TestTrue(TEXT("测试粒子的加速度确实不为零"),
			FMath::Abs(Simulator.GetBody(1).Acceleration.X) > 1.0);
		TestTrue(TEXT("中心天体完全不受测试粒子吸引（逐位为零）"),
			Simulator.GetBody(0).Acceleration == FVector3d::ZeroVector);

		// 对照：同一初态、不标测试粒子时中心天体确实被拉向 +X。
		FNBodySimulator Control = MakePair(false);
		TestEqual(TEXT("对照：普通粒子会吸引中心天体"),
			Control.GetBody(0).Acceleration.X, G, 1.0e-6);
	}

	// ── 2. 积分 0.04 年：中心天体逐位不动，测试粒子照常下落 ──
	{
		FNBodySimulator Simulator = MakePair(true);
		FNBodySimulator Control = MakePair(false);

		for (int32 Step = 0; Step < 400; ++Step)
		{
			Simulator.StepExact(1.0e-4);
			Control.StepExact(1.0e-4);
		}

		TestTrue(TEXT("0.04 年后中心天体位置逐位仍在原点"),
			Simulator.GetBody(0).Position == FVector3d::ZeroVector);
		TestTrue(TEXT("测试粒子照常掉向中心天体"),
			Simulator.GetBody(1).Position.X < 0.99);

		// 对照组必须把中心天体拉开，否则上面那条「不动」可能只是两体问题本身不动。
		TestTrue(TEXT("对照：普通粒子把中心天体拉离原点"),
			Control.GetBody(0).Position.X > 1.0e-3);
	}

	// ── 3. 单向不进入能量诊断：两天体静止、测试粒子引力源质量为 0 → 总能量必须是 0，不是 −G·M·m/r ──
	{
		FNBodySimulator Simulator = MakePair(true);
		TestEqual(TEXT("总能量不含测试粒子的幻影势能"),
			Simulator.GetTotalEnergy(), 0.0, 1.0e-15);

		FNBodySimulator Control = MakePair(false);
		TestEqual(TEXT("对照：普通两体能量 = −G·M·m/r"),
			Control.GetTotalEnergy(), -G, 1.0e-6);
	}

	// ── 4. 预测副本继承 bTestParticle：中心天体预测线不被拉、粒子确实在动 ──
	{
		FNBodySimulator Simulator = MakePair(true);
		FNBodySimulator Control = MakePair(false);

		TArray<FVector3d> PredictedCentral;
		TArray<FVector3d> PredictedCentralControl;
		TArray<FVector3d> PredictedParticle;

		TestTrue(TEXT("预测中心天体成功"),
			Simulator.PredictTrajectory(0, 0.1, 32, PredictedCentral));
		TestTrue(TEXT("预测中心天体成功（对照）"),
			Control.PredictTrajectory(0, 0.1, 32, PredictedCentralControl));
		TestTrue(TEXT("预测测试粒子成功"),
			Simulator.PredictTrajectory(1, 0.1, 32, PredictedParticle));

		bool bCentralStayedAtOrigin = true;
		for (const FVector3d& Point : PredictedCentral)
		{
			if (Point != FVector3d::ZeroVector)
			{
				bCentralStayedAtOrigin = false;
				break;
			}
		}
		TestTrue(TEXT("预测线里中心天体始终在原点"), bCentralStayedAtOrigin);
		TestTrue(TEXT("对照：预测线里中心天体被拽走"),
			PredictedCentralControl.Last().SizeSquared() > 0.0);
		TestTrue(TEXT("预测线里测试粒子确实在动"),
			(PredictedParticle.Last() - PredictedParticle[0]).SizeSquared() > 0.0);
	}

	return true;
}

///////////////////////////////////////////////////////////////////////////////
// 单位换算：挡「换算写反/写错一个量级」，solve 与编辑器边界都靠它们。

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraUnitsTest,
	"StellarEra.Cosmos.Units",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraUnitsTest::RunTest(const FString& Parameters)
{
	constexpr double KmsPerAuPerYear = 4.740470463533348;
	constexpr double AuPerYearPerKms = 0.21094952656969873;

	// ── 1. 物理速度：AU/年 ↔ km/s ──
	TestEqual(TEXT("1 AU/年 = 4.740470463533348 km/s"),
		CosmosUnits::KilometerPerSecondPerAuPerYear, KmsPerAuPerYear, 1.0e-12);
	TestEqual(TEXT("1 km/s = 0.21094952656969873 AU/年"),
		CosmosUnits::AuPerYearPerKilometerPerSecond, AuPerYearPerKms, 1.0e-15);

	const FVector3d EarthVelocity =
		CosmosUnits::AuPerYearToKilometerPerSecond(FVector3d(0.0, 2.0 * UE_DOUBLE_PI, 0.0));
	TestEqual(TEXT("地球圆轨道 2π AU/年 = 29.785254365591534 km/s"),
		EarthVelocity.Y, 29.785254365591534, 1.0e-12);

	const FVector3d PlanetVelocity =
		CosmosUnits::AuPerYearToKilometerPerSecond(FVector3d(0.0, 50.0, 0.0));
	TestEqual(TEXT("50 AU/年 = 237.0235231766674 km/s"),
		PlanetVelocity.Y, 237.0235231766674, 1.0e-12);

	const FVector3d BackToSolver = CosmosUnits::KilometerPerSecondToAuPerYear(PlanetVelocity);
	TestEqual(TEXT("km/s 往返 AU/年：X"), BackToSolver.X, 0.0, 1.0e-12);
	TestEqual(TEXT("km/s 往返 AU/年：Y"), BackToSolver.Y, 50.0, 1.0e-12);

	// ── 2. 质量：M⊕ ↔ M☉ ──
	TestEqual(TEXT("1 M⊕ = 3.0034893e-6 M☉"),
		CosmosUnits::EarthMassToSolarMass(1.0), 3.0034893e-6, 1.0e-20);
	TestEqual(TEXT("1 M☉ = 332946.0837433315 M⊕"),
		CosmosUnits::SolarMassToEarthMass(1.0), 332946.0837433315, 1.0e-9);
	TestEqual(TEXT("行星 0.873237 M☉ = 290740.8393297756 M⊕"),
		CosmosUnits::SolarMassToEarthMass(0.873237), 290740.8393297756, 1.0e-9);
	TestEqual(TEXT("中心 10000 M☉ = 3329460837.433315 M⊕"),
		CosmosUnits::SolarMassToEarthMass(10000.0), 3329460837.433315, 1.0e-3);
	TestEqual(TEXT("飞船 1e-6 M☉ = 0.33294608374333146 M⊕"),
		CosmosUnits::SolarMassToEarthMass(1.0e-6), 0.33294608374333146, 1.0e-15);

	// ── 3. 世界速度：AU/年 ↔ 厘米/秒（默认标定 1e6 / 0.00333） ──
	TestEqual(TEXT("1 AU/年 = 3330 厘米/秒"),
		CosmosUnits::GameVelocityToWorldCmPerSecond(1.0, 1.0e6, 0.00333), 3330.0, 1.0e-9);
	TestEqual(TEXT("50 AU/年 = 166500 厘米/秒"),
		CosmosUnits::GameVelocityToWorldCmPerSecond(50.0, 1.0e6, 0.00333), 166500.0, 1.0e-9);
	TestEqual(TEXT("世界速度往返"),
		CosmosUnits::WorldCmPerSecondToGameVelocity(166500.0, 1.0e6, 0.00333), 50.0, 1.0e-12);
	TestEqual(TEXT("标定值无效时速度换算返回 0，而不是 inf/nan"),
		CosmosUnits::WorldCmPerSecondToGameVelocity(1.0, 0.0, 0.0), 0.0, 0.0);

	// ── 4. 世界加速度：AU/年² ↔ 厘米/秒² ──
	TestEqual(TEXT("1 AU/年² = 11.0889 厘米/秒²"),
		CosmosUnits::GameAccelerationToWorldCmPerS2(1.0, 1.0e6, 0.00333), 11.0889, 1.0e-9);
	TestEqual(TEXT("1.2e7 厘米/秒² = 1082163.2443254064 AU/年²"),
		CosmosUnits::WorldCmPerS2ToGameAcceleration(1.2e7, 1.0e6, 0.00333),
		1082163.2443254064, 1.0e-6);
	TestEqual(TEXT("标定值无效时加速度换算返回 0"),
		CosmosUnits::WorldCmPerS2ToGameAcceleration(1.0, 0.0, 0.0), 0.0, 0.0);

	// ── 5. 向量重载与标量同口径 ──
	const FVector3d WorldVelocity = CosmosUnits::GameVelocityToWorldCmPerSecond(
		FVector3d(0.0, 50.0, 0.0), 1.0e6, 0.00333);
	TestEqual(TEXT("向量速度换算：Y"), WorldVelocity.Y, 166500.0, 1.0e-9);

	const FVector3d GameAcceleration = CosmosUnits::WorldCmPerS2ToGameAcceleration(
		FVector3d(0.0, 0.0, 1.2e7), 1.0e6, 0.00333);
	TestEqual(TEXT("向量加速度换算：Z"),
		GameAcceleration.Z, 1082163.2443254064, 1.0e-6);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraNBodyProbeContactTest,
	"StellarEra.Cosmos.NBodyProbeContact",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraNBodyProbeContactTest::RunTest(const FString& Parameters)
{
	// 几何：球体半径 0.1 在原点（质量 3），探针体质量 1 在 x=0.15。偏移 -0.06 穿 0.01，-0.08 穿 0.03，深者胜。
	{
		FNBodySimulator Simulator;
		Simulator.SetUnitSystem(ECosmosUnitSystem::Game);
		Simulator.SetIntegrator(ECosmosIntegrator::VelocityVerlet);
		Simulator.SetSofteningLength(1.0e-5);

		FCosmosBodyState Sphere;
		Sphere.Mass = 3.0;
		Sphere.Radius = 0.1;
		Sphere.Position = FVector3d::ZeroVector;
		Simulator.AddBody(Sphere);

		FCosmosBodyState ProbeBody;
		ProbeBody.Mass = 1.0;
		ProbeBody.Position = FVector3d(0.15, 0.0, 0.0);
		TArray<FVector3d> Probes;
		Probes.Add(FVector3d(-0.06, 0.0, 0.0));
		Probes.Add(FVector3d(-0.08, 0.0, 0.0));
		ProbeBody.SetContactProbes(MoveTemp(Probes));
		Simulator.AddBody(ProbeBody);

		TestEqual(TEXT("ContactProbeRadius = 0.08"),
			Simulator.GetBody(1).ContactProbeRadius, 0.08, 1.0e-15);

		Simulator.StepExact(0.0);

		// 穿透 0.03，让路比例：探针体 3/4、球体 1/4。
		TestEqual(TEXT("探针体让 3/4 的穿透量"),
			Simulator.GetBody(1).Position.X, 0.1725, 1.0e-15);
		TestEqual(TEXT("球体反向让 1/4"),
			Simulator.GetBody(0).Position.X, -0.0075, 1.0e-15);
		TestEqual(TEXT("最深探针恰好落在球面上"),
			(Simulator.GetBody(1).Position + FVector3d(-0.08, 0.0, 0.0)
				- Simulator.GetBody(0).Position).Size(), 0.1, 1.0e-15);
	}

	// 空探针 = 退回球-球：半径 0 的点质量即便中心距 0.15 > 0.1 也不会接触。
	{
		FNBodySimulator Simulator;
		Simulator.SetUnitSystem(ECosmosUnitSystem::Game);
		Simulator.SetIntegrator(ECosmosIntegrator::VelocityVerlet);
		Simulator.SetSofteningLength(1.0e-5);

		FCosmosBodyState Sphere;
		Sphere.Mass = 3.0;
		Sphere.Radius = 0.1;
		Simulator.AddBody(Sphere);

		FCosmosBodyState PointMass;
		PointMass.Mass = 1.0;
		PointMass.Position = FVector3d(0.15, 0.0, 0.0);
		Simulator.AddBody(PointMass);

		Simulator.StepExact(0.0);

		TestEqual(TEXT("无探针、半径为 0：不接触"),
			Simulator.GetBody(1).Position.X, 0.15, 1.0e-15);
		TestEqual(TEXT("球体也不动"), Simulator.GetBody(0).Position.X, 0.0, 1.0e-15);
	}

	// 探针接触走冲量求解：法向相对速度被抹掉，切向由库仑摩擦削。
	// bZeroVelocityOnContact 只作用于球-球分支，探针分支不再消费它 ——
	// 这里仍然置位，就是为了钉住「置了也不影响探针路径」。
	{
		FNBodySimulator Simulator;
		Simulator.SetUnitSystem(ECosmosUnitSystem::Game);
		Simulator.SetIntegrator(ECosmosIntegrator::VelocityVerlet);
		Simulator.SetSofteningLength(1.0e-5);

		FCosmosBodyState Sphere;
		Sphere.Mass = 3.0;
		Sphere.Radius = 0.1;
		Sphere.Velocity = FVector3d(0.0, 0.002, 0.0);
		Simulator.AddBody(Sphere);

		FCosmosBodyState ProbeBody;
		ProbeBody.Mass = 1.0;
		ProbeBody.Position = FVector3d(0.15, 0.0, 0.0);
		ProbeBody.Velocity = FVector3d(-0.01, 0.0, 0.0);
		ProbeBody.bZeroVelocityOnContact = true;
		TArray<FVector3d> Probes;
		Probes.Add(FVector3d(-0.06, 0.0, 0.0));
		ProbeBody.SetContactProbes(MoveTemp(Probes));
		Simulator.AddBody(ProbeBody);

		Simulator.StepExact(0.0);

		// 法向相对速度 −0.01，有效质量 1/(1/1 + 1/3) = 0.75 → λ_n = 0.0075，
		// 两侧法向相对速度归零。切向相对 0.002，μλ_n = 0.6×0.0075 = 0.0045，
		// 大于所需的 0.002×0.75 = 0.0015，于是切向也被完全削平。
		//
		// 相对速度整体归零 ⇒ 两者速度都等于质心速度：
		//     (m₁v₁ + m₂v₂) / (m₁ + m₂)
		//   = (1·(−0.01, 0, 0) + 3·(0, 0.002, 0)) / 4
		//   = (−0.0025, 0.0015, 0)
		const FVector3d Relative =
			Simulator.GetBody(1).Velocity - Simulator.GetBody(0).Velocity;
		TestEqual(TEXT("法向相对速度归零"), Relative.X, 0.0, 1.0e-15);
		TestEqual(TEXT("切向相对速度被摩擦削光（μλ_n 够大）"), Relative.Y, 0.0, 1.0e-15);

		TestEqual(TEXT("探针体速度 X = 球体速度 X"),
			Simulator.GetBody(1).Velocity.X, -0.0025, 1.0e-15);
		TestEqual(TEXT("探针体速度 Y = 球体速度 Y"),
			Simulator.GetBody(1).Velocity.Y, 0.0015, 1.0e-15);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraNBodyProbeOrientationTest,
	"StellarEra.Cosmos.ProbeContactUsesOrientation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraNBodyProbeOrientationTest::RunTest(const FString& Parameters)
{
	// 探针存**机体系**：机身绕 +Z 转 90° 后，本地 +Y 的探针必须落到世界 −X 一侧。
	// 存世界系的旧实现会把它当成 (0,0.08,0) 直接加到船心上（距球心 0.17，无接触），
	// 于是同一个姿态下接触点整体错开 90°，落地时正是靠这个错位自激成每帧 ±8° 的摆动。
	FNBodySimulator Simulator;
	Simulator.SetUnitSystem(ECosmosUnitSystem::Game);
	Simulator.SetIntegrator(ECosmosIntegrator::VelocityVerlet);
	Simulator.SetSofteningLength(1.0e-5);

	FCosmosBodyState Sphere;
	Sphere.Mass = 3.0;
	Sphere.Radius = 0.1;
	Simulator.AddBody(Sphere);

	FCosmosBodyState ProbeBody;
	ProbeBody.Mass = 1.0;
	ProbeBody.Position = FVector3d(0.15, 0.0, 0.0);
	ProbeBody.bRigidBody = true;
	ProbeBody.Orientation = FQuat4d(FVector3d(0.0, 0.0, 1.0), HALF_PI);
	TArray<FVector3d> Probes;
	Probes.Add(FVector3d(0.0, 0.08, 0.0));
	ProbeBody.SetContactProbes(MoveTemp(Probes));
	Simulator.AddBody(ProbeBody);

	Simulator.StepExact(0.0);

	// 旋转后世界偏移 (−0.08,0,0) → 探针在 (0.07,0,0)，穿透 0.03，探针体让 3/4。
	TestEqual(TEXT("按朝向旋转后接触成立：探针体被推开 3/4 穿透"),
		Simulator.GetBody(1).Position.X, 0.1725, 1.0e-9);
	TestEqual(TEXT("球体反向让 1/4"),
		Simulator.GetBody(0).Position.X, -0.0075, 1.0e-9);
	TestEqual(TEXT("旋转后的探针恰好落在球面上"),
		(Simulator.GetBody(1).Position + FVector3d(-0.08, 0.0, 0.0)
			- Simulator.GetBody(0).Position).Size(), 0.1, 1.0e-9);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraNBodyShipRestTest,
	"StellarEra.Cosmos.ShipRestStability",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraNBodyShipRestTest::RunTest(const FString& Parameters)
{
	// 落地不该自激：探针存世界系（不随子步转向）时同一个场景末段是 2.55 rad/s ≈ 18°/帧、
	// 机身最后横躺 87°，正是 PIE 里「落到地上一直抖」；按朝向旋转后收敛到 0.1°/帧。
	FNBodySimulator Simulator;
	Simulator.SetUnitSystem(ECosmosUnitSystem::Game);
	Simulator.SetIntegrator(ECosmosIntegrator::VelocityVerlet);
	Simulator.SetSofteningLength(1.0e-5);

	FCosmosBodyState Planet;
	Planet.Mass = 250.0;
	Planet.Radius = 2.0;
	Simulator.AddBody(Planet);

	FCosmosBodyState Ship;
	Ship.Mass = 3.00349e-9;
	Ship.bTestParticle = true;
	Ship.bRigidBody = true;
	Ship.AngularDamping = 1051.0;   // 3.5 1/s ÷ TimeScale 0.00333
	Ship.ContactFriction = 0.6;

	const FVector3d Half(0.00275, 0.00413, 0.001);
	Ship.InverseInertiaLocal = FCosmosBodyState::InverseInertiaFromBox(Ship.Mass, Half);

	// 船底压进地面 8% 半长、再绕 Y 倾 2°，让 8 个角点的接触深度不对称。
	Ship.Position = FVector3d(Planet.Radius + Half.X * 0.92, 0.0, 0.0);
	Ship.Orientation = FQuat4d(FVector3d(0.0, 1.0, 0.0), FMath::DegreesToRadians(2.0));

	TArray<FVector3d> Probes;
	for (const double SignX : {-1.0, 1.0})
	{
		for (const double SignY : {-1.0, 1.0})
		{
			for (const double SignZ : {-1.0, 1.0})
			{
				Probes.Add(FVector3d(SignX * Half.X, SignY * Half.Y, SignZ * Half.Z));
			}
		}
	}
	Ship.SetContactProbes(MoveTemp(Probes));
	Simulator.AddBody(Ship);

	const double Dt = 1.188142e-5;
	const double TimeScale = 0.00333;
	const int32 Steps = 4000;
	const int32 Window = 800;

	double MaxOmegaLate = 0.0;
	double SumOmegaLate = 0.0;
	double MinRadius = TNumericLimits<double>::Max();
	double MaxRadius = 0.0;
	for (int32 Step = 0; Step < Steps; ++Step)
	{
		Simulator.StepExact(Dt);

		const FCosmosBodyState& Body = Simulator.GetBody(1);
		const double OmegaRadPerSec = Body.AngularVelocity.Size() * TimeScale;
		if (Step >= Steps - Window)
		{
			MaxOmegaLate = FMath::Max(MaxOmegaLate, OmegaRadPerSec);
			SumOmegaLate += OmegaRadPerSec;
			const double Radius = Body.Position.Size();
			MinRadius = FMath::Min(MinRadius, Radius);
			MaxRadius = FMath::Max(MaxRadius, Radius);
		}
	}

	const FCosmosBodyState& ShipNow = Simulator.GetBody(1);
	const FVector3d Up = ShipNow.Position.GetSafeNormal();
	const FVector3d BodyX = ShipNow.Orientation.RotateVector(FVector3d(1.0, 0.0, 0.0));
	const double TiltDeg = FMath::RadiansToDegrees(
		FMath::Acos(FMath::Clamp(BodyX.Dot(Up), -1.0, 1.0)));

	AddInfo(FString::Printf(
		TEXT("[诊断] 末段角速度 最大 %.3f 平均 %.3f rad/s（0.125 s 帧 = %.1f 度）"),
		MaxOmegaLate, SumOmegaLate / Window, MaxOmegaLate * 0.125 * 180.0 / PI));
	AddInfo(FString::Printf(
		TEXT("[诊断] 末段径向 %.6f~%.6f AU（地面 %.6f），振幅 %.3f cm"),
		MinRadius, MaxRadius, Planet.Radius, (MaxRadius - MinRadius) * 1.0e6));
	AddInfo(FString::Printf(
		TEXT("[诊断] 终态倾角 %.2f 度，法向速度 %.3f m/s，切向速度 %.3f m/s"),
		TiltDeg,
		FMath::Abs(ShipNow.Velocity.Dot(Up)) * 4.74047e5 / 100.0,
		(ShipNow.Velocity - Up * ShipNow.Velocity.Dot(Up)).Size() * 4.74047e5 / 100.0));

	// 阈值离实测留了 4~25 倍余量：要的是「没有自激」，不是把当前数值钉死。
	TestTrue(TEXT("落地后角速度收敛（旧实现 2.55 rad/s ≈ 18°/帧）"), MaxOmegaLate < 0.05);
	TestTrue(TEXT("落地后机身摆平，倾角 < 5 度（旧实现横躺 87°）"), TiltDeg < 5.0);
	TestTrue(TEXT("落地后径向振幅 < 2 cm"), (MaxRadius - MinRadius) * 1.0e6 < 2.0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraNBodyPredictionContactTest,
	"StellarEra.Cosmos.NBodyPredictionContact",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraNBodyPredictionContactTest::RunTest(const FString& Parameters)
{
	// 复现编辑器预览「预测线飞出去」：中心星半径 2 AU，飞船悬在 2.054 AU、初速零。
	// 不给半径时飞船穿过星体扎进软化核、被踢成直线；给半径 + 撞上即停就停在表面。
	// 预览（DrawInEditor）以前不传半径 —— 这条锁的是「预览必须把半径传进来」。
	FNBodySimulator Simulator;
	Simulator.SetUnitSystem(ECosmosUnitSystem::Game);
	Simulator.SetIntegrator(ECosmosIntegrator::VelocityVerlet);
	Simulator.SetSofteningLength(1.0e-4);
	Simulator.SetCentimetersPerAu(1.0e6);

	FCosmosBodyState Star;
	Star.Mass = 10000.0;
	Star.Radius = 2.0;
	Simulator.AddBody(Star);

	FCosmosBodyState Ship;
	Ship.Mass = 1.0e-6;
	Ship.Position = FVector3d(0.0, 0.0, 2.054);
	Ship.Radius = 300.0 / 1.0e6;   // GetContactRadiusCm() / CentimetersPerAu
	Ship.bZeroVelocityOnContact = true;
	Simulator.AddBody(Ship);

	constexpr double Horizon = 0.0162624;   // 0.5 × 飞船与中心星那一对的周期
	TArray<FVector3d> Predicted;
	TestTrue(TEXT("预测调用成功"),
		Simulator.PredictTrajectory(1, Horizon, 256, Predicted));
	TestEqual(TEXT("预测点数 = 采样数"), Predicted.Num(), 256);

	double MaxAbsZ = 0.0;
	bool bAllFinite = true;
	for (const FVector3d& Point : Predicted)
	{
		bAllFinite &= FMath::IsFinite(Point.X) && FMath::IsFinite(Point.Y)
			&& FMath::IsFinite(Point.Z);
		MaxAbsZ = FMath::Max(MaxAbsZ, FMath::Abs(Point.Z));
	}
	TestTrue(TEXT("预测点全部有限"), bAllFinite);
	TestTrue(TEXT("整条预测线留在中心星附近（不飞出去）"), MaxAbsZ < 2.1);

	const double FinalRadius = Predicted.Last().Size();
	TestTrue(TEXT("末点停在中心星表面"), FinalRadius <= 2.0 + 1.0e-3);
	TestTrue(TEXT("末点没有被推离表面"), FinalRadius >= 2.0 - 1.0e-6);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraClosedOrbitPredictionTest,
	"StellarEra.Cosmos.ClosedOrbitPrediction",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraClosedOrbitPredictionTest::RunTest(const FString& Parameters)
{
	// ── 1. 圆轨道：相对主导天体跑满一圈，首尾逐位相接 ──
	{
		FNBodySimulator Simulator = MakeCircularTwoBody(0.718, 1.0);

		FCosmosClosedOrbitPrediction Closed;
		TestTrue(TEXT("圆轨道能闭合"),
			Simulator.PredictClosedOrbit(1, 256, Closed));
		// 回到起点的那个采样点本身被吸到首点上，不再另补一个尾点（那一小段就是旧版接缝的折角）。
		TestEqual(TEXT("点数 = 一圈采样数（末点即回到起点那一采样）"), Closed.RelativePositions.Num(), 256);

		// 首尾是同一个点（逐位相等，不是「接近」）。
		TestTrue(TEXT("末点与首点逐位相等"),
			Closed.RelativePositions.Last() == Closed.RelativePositions[0]);

		// 中心星在原点且不动，主导天体位置就是零。
		TestTrue(TEXT("主导天体位置 = 原点"),
			Closed.PrimaryPosition == FVector3d::ZeroVector);

		// 密切周期应与既有的配对周期估算一致。
		TestEqual(TEXT("周期 = 估算周期"),
			Closed.PeriodYears, Simulator.EstimateOrbitalPeriod(1), 1.0e-3);

		// 圆轨道半径不变。容差 1% 是积分误差不是物理效应（见 NBodyPrediction 第 3 节）。
		double MinRadius = TNumericLimits<double>::Max();
		double MaxRadius = 0.0;
		for (const FVector3d& Point : Closed.RelativePositions)
		{
			const double Radius = Point.Size();
			MinRadius = FMath::Min(MinRadius, Radius);
			MaxRadius = FMath::Max(MaxRadius, Radius);
		}
		TestTrue(TEXT("全程半径贴近 0.718 AU（下限）"),
			MinRadius > 0.99 * 0.718);
		TestTrue(TEXT("全程半径贴近 0.718 AU（上限）"),
			MaxRadius < 1.01 * 0.718);
	}

	// ── 2. 双曲线：ε ≥ 0，没有一圈可闭合 ──
	{
		FNBodySimulator Simulator = MakeCircularTwoBody(0.718, 1.0);
		Simulator.GetBodyMutable(1).Velocity *= 1.5;

		FCosmosClosedOrbitPrediction Closed;
		TestFalse(TEXT("双曲线不闭合"), Simulator.PredictClosedOrbit(1, 64, Closed));
		TestEqual(TEXT("失败时清空点集"), Closed.RelativePositions.Num(), 0);
		TestEqual(TEXT("失败时周期为 0"), Closed.PeriodYears, 0.0);
	}

	// ── 3. 径向退化（速度为零）：e = 1，不是闭环 ──
	{
		FNBodySimulator Simulator = MakeCircularTwoBody(0.718, 1.0);
		Simulator.GetBodyMutable(1).Velocity = FVector3d::ZeroVector;

		FCosmosClosedOrbitPrediction Closed;
		TestFalse(TEXT("径向退化不闭合"), Simulator.PredictClosedOrbit(1, 64, Closed));
	}

	// ── 4. 中心星本身：没有更重的主导天体 ──
	{
		FNBodySimulator Simulator = MakeCircularTwoBody(0.718, 1.0);

		FCosmosClosedOrbitPrediction Closed;
		TestFalse(TEXT("中心星不会被行星闭环"), Simulator.PredictClosedOrbit(0, 64, Closed));
	}

	// ── 5. 非法入参 ──
	{
		FNBodySimulator Simulator = MakeCircularTwoBody(0.718, 1.0);

		FCosmosClosedOrbitPrediction Closed;
		TestFalse(TEXT("索引越界返回 false"), Simulator.PredictClosedOrbit(99, 64, Closed));
		TestFalse(TEXT("采样点不足返回 false"), Simulator.PredictClosedOrbit(1, 1, Closed));
	}

	// ── 6. 三体系统：密切周期 ≠ 真实一圈，仍必须闭合 ──
	// 复刻 UniverseMap 构型：1e4 M☉ 中心星 + 两颗 2500 M☉ 行星分居 ±50 AU、处 e≈0.75 椭圆
	// 远拱点。真实一圈约 1.55 年，密切周期只有 1.37 年；旧实现按密切周期截断会断成半截。
	{
		FNBodySimulator Simulator;
		Simulator.SetUnitSystem(ECosmosUnitSystem::Game);
		Simulator.SetIntegrator(ECosmosIntegrator::VelocityVerlet);
		Simulator.SetSofteningLength(1.0e-5);
		Simulator.SetCentimetersPerAu(1.0e6);

		FCosmosBodyState Star;
		Star.Mass = 1.0e4;
		Simulator.AddBody(Star);

		FCosmosBodyState PlanetA;
		PlanetA.Mass = 2500.0;
		PlanetA.Position = FVector3d(-50.0, 0.0, 0.0);
		PlanetA.Velocity = FVector3d(0.0, 50.0, 0.0);
		Simulator.AddBody(PlanetA);

		FCosmosBodyState PlanetB;
		PlanetB.Mass = 2500.0;
		PlanetB.Position = FVector3d(50.0, 0.0, 0.0);
		PlanetB.Velocity = FVector3d(0.0, -50.0, 0.0);
		Simulator.AddBody(PlanetB);

		// 不能用 EstimateOrbitalPeriod：它把当前距离当圆轨道半径（3.16 年），而这里是
		// e≈0.75 的椭圆（a≈28.6 AU），密切周期 1.37 年。混用会把回归判据写反。
		const FCosmosBodyState& Tracked = Simulator.GetBody(1);
		const double Mu = CosmosUnits::GravitationalConstant_Game * (2500.0 + 1.0e4);
		const double TrackedDistance = Tracked.Position.Size();
		const double SpecificEnergy = 0.5 * Tracked.Velocity.SizeSquared()
			- Mu / TrackedDistance;
		const double TrackedSemiMajorAxis = -Mu / (2.0 * SpecificEnergy);
		const double OsculatingPeriod = 2.0 * PI * FMath::Sqrt(
			TrackedSemiMajorAxis * TrackedSemiMajorAxis * TrackedSemiMajorAxis / Mu);
		TestTrue(TEXT("三体构型的密切周期约为 1.37 年"),
			OsculatingPeriod > 1.2 && OsculatingPeriod < 1.5);

		for (int32 PlanetIndex = 1; PlanetIndex <= 2; ++PlanetIndex)
		{
			FCosmosClosedOrbitPrediction Closed;
			TestTrue(TEXT("三体构型仍能闭合成环"),
				Simulator.PredictClosedOrbit(PlanetIndex, 512, Closed));

			TestTrue(TEXT("首尾逐位相接"),
				Closed.RelativePositions.Num() >= 2
					&& Closed.RelativePositions.Last() == Closed.RelativePositions[0]);

			// 实测闭合时间必须明显长于密切周期：这正是旧实现失败的地方。
			TestTrue(TEXT("闭合时刻按实测（≈1.55 年），不是密切周期"),
				Closed.PeriodYears > 1.05 * OsculatingPeriod);
			TestTrue(TEXT("实测闭合时刻在 1.55 年附近"),
				Closed.PeriodYears > 1.45 && Closed.PeriodYears < 1.65);

			// 轨道形状：远拱点 ≈ 50 AU，近拱点 ≈ 7 AU。
			double MinRadius = TNumericLimits<double>::Max();
			double MaxRadius = 0.0;
			for (const FVector3d& Point : Closed.RelativePositions)
			{
				const double Radius = Point.Size();
				MinRadius = FMath::Min(MinRadius, Radius);
				MaxRadius = FMath::Max(MaxRadius, Radius);
			}
			TestTrue(TEXT("远拱点 ≈ 50 AU"), MaxRadius > 49.0 && MaxRadius < 51.0);
			TestTrue(TEXT("近拱点 ≈ 7 AU"), MinRadius > 6.0 && MinRadius < 9.0);
		}
	}

	return true;
}

// 逐探针地表半径：飞船停在哪由「那个探针方向上的真实地表」决定，而不是行星解析球的
// 半径 —— 挖出来的坑就靠这条落下去。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraNBodyProbeSurfaceRadiiTest,
	"StellarEra.Cosmos.NBodyProbeSurfaceRadii",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraNBodyProbeSurfaceRadiiTest::RunTest(const FString& Parameters)
{
	// 统一几何：球体（行星）质量 3、半径 0.1 在原点；探针体（飞船）质量 1，探针在本地 (−0.06, 0, 0)。

	const auto MakeSimulator = []()
	{
		FNBodySimulator Simulator;
		Simulator.SetUnitSystem(ECosmosUnitSystem::Game);
		Simulator.SetIntegrator(ECosmosIntegrator::VelocityVerlet);
		Simulator.SetSofteningLength(1.0e-5);

		FCosmosBodyState Planet;
		Planet.Mass = 3.0;
		Planet.Radius = 0.1;
		Planet.Position = FVector3d::ZeroVector;
		Simulator.AddBody(Planet);
		return Simulator;
	};

	// 逐探针半径必须在 SetContactProbes **之后**写：那个函数会清空它（防止探针重建后留下旧半径）。
	const auto MakeShip = [](const double ShipX)
	{
		FCosmosBodyState Ship;
		Ship.Mass = 1.0;
		Ship.Position = FVector3d(ShipX, 0.0, 0.0);
		Ship.SetContactProbes({FVector3d(-0.06, 0.0, 0.0)});
		return Ship;
	};

	// ── 1. 坑：逐探针半径 0.05 比球半径 0.1 低，船落到 0.05 的面上 ──
	// 船心 0.09 + 探针 (−0.06) = 0.03，低于 0.05 → 穿透 0.02。
	// 让路比例：船 3/4、行星 1/4。
	{
		FNBodySimulator Simulator = MakeSimulator();

		FCosmosBodyState Ship = MakeShip(0.09);
		Ship.ContactProbeSurfaceRadii.Add(0.05);
		Ship.ContactSurfaceSphereIndex = 0;
		Simulator.AddBody(Ship);

		Simulator.StepExact(0.0);

		TestEqual(TEXT("船心让 3/4：0.09 + 0.015 = 0.105"),
			Simulator.GetBody(1).Position.X, 0.105, 1.0e-15);
		TestEqual(TEXT("行星反向让 1/4"),
			Simulator.GetBody(0).Position.X, -0.005, 1.0e-15);
		TestEqual(TEXT("探针恰好落在 0.05 的地表上"),
			(Simulator.GetBody(1).Position + FVector3d(-0.06, 0.0, 0.0)
				- Simulator.GetBody(0).Position).Size(), 0.05, 1.0e-15);
	}

	// ── 2. 同一个坑，船悬在 0.12：探针落在 0.06 高于 0.05 → 不接触（「停在空气上」的反向断言） ──
	{
		FNBodySimulator Simulator = MakeSimulator();

		FCosmosBodyState Ship = MakeShip(0.12);
		Ship.ContactProbeSurfaceRadii.Add(0.05);
		Ship.ContactSurfaceSphereIndex = 0;
		Simulator.AddBody(Ship);

		Simulator.StepExact(0.0);

		TestEqual(TEXT("没碰到 0.05 的地表就不该被推开"),
			Simulator.GetBody(1).Position.X, 0.12, 1.0e-15);

		// 对照组：同样摆位不给逐探针半径，必须被 0.1 球面拦住 —— 否则上一条恒真。
		FNBodySimulator Control = MakeSimulator();
		Control.AddBody(MakeShip(0.12));
		Control.StepExact(0.0);
		TestTrue(TEXT("对照组：没有逐探针半径时会被球面拦住"),
			Control.GetBody(1).Position.X > 0.12 + 1.0e-6);
	}

	// ── 3. 索引对不上 → 退回球半径（0.1）──
	{
		FNBodySimulator Simulator = MakeSimulator();

		FCosmosBodyState Ship = MakeShip(0.09);
		Ship.ContactProbeSurfaceRadii.Add(0.05);
		Ship.ContactSurfaceSphereIndex = 99;
		Simulator.AddBody(Ship);

		Simulator.StepExact(0.0);

		// 半径按 0.1：穿透 0.1 − 0.03 = 0.07 → 船心 0.09 + 0.07×0.75 = 0.1425。
		TestEqual(TEXT("索引不匹配：按球半径算"),
			Simulator.GetBody(1).Position.X, 0.1425, 1.0e-15);
	}

	// ── 4. 数量对不上 → 退回球半径 ──
	{
		FNBodySimulator Simulator = MakeSimulator();

		FCosmosBodyState Ship = MakeShip(0.09);
		Ship.ContactProbeSurfaceRadii.Add(0.05);
		Ship.ContactProbeSurfaceRadii.Add(0.05);   // 探针只有 1 个
		Ship.ContactSurfaceSphereIndex = 0;
		Simulator.AddBody(Ship);

		Simulator.StepExact(0.0);

		TestEqual(TEXT("数量不匹配：按球半径算"),
			Simulator.GetBody(1).Position.X, 0.1425, 1.0e-15);
	}

	// ── 5. 抬高地形：逐探针半径**大于**球半径时，快速排除不能误杀（只按球半径排除会整条漏掉） ──
	{
		FNBodySimulator Simulator = MakeSimulator();

		FCosmosBodyState Ship;
		Ship.Mass = 1.0;
		Ship.Position = FVector3d(0.15, 0.0, 0.0);
		Ship.SetContactProbes({FVector3d::ZeroVector});
		Ship.ContactProbeSurfaceRadii.Add(0.2);
		Ship.ContactSurfaceSphereIndex = 0;
		Simulator.AddBody(Ship);

		Simulator.StepExact(0.0);

		// 穿透 0.2 − 0.15 = 0.05 → 船心 0.15 + 0.05×0.75 = 0.1875。
		TestEqual(TEXT("抬高地表：0.15 + 0.0375"),
			Simulator.GetBody(1).Position.X, 0.1875, 1.0e-15);
		TestEqual(TEXT("探针落在 0.2 的地表上"),
			(Simulator.GetBody(1).Position - Simulator.GetBody(0).Position).Size(),
			0.2, 1.0e-15);
	}

	// ── 6. 三个探针各带半径：船心 0.15 → 距球心 0.09/0.07/0.13；半径 {0.1,0.06,0.06} → 只有第一个真穿透 ──
	{
		FNBodySimulator Simulator = MakeSimulator();

		FCosmosBodyState Ship;
		Ship.Mass = 1.0;
		Ship.Position = FVector3d(0.15, 0.0, 0.0);
		Ship.SetContactProbes({
			FVector3d(-0.06, 0.0, 0.0),
			FVector3d(-0.08, 0.0, 0.0),
			FVector3d(-0.02, 0.0, 0.0)});
		Ship.ContactProbeSurfaceRadii.Append({0.1, 0.06, 0.06});
		Ship.ContactSurfaceSphereIndex = 0;
		Simulator.AddBody(Ship);

		Simulator.StepExact(0.0);

		TestEqual(TEXT("最深的探针决定位移：0.15 + 0.0075"),
			Simulator.GetBody(1).Position.X, 0.1575, 1.0e-15);
	}

	// ── 7. 逐探针半径与冲量求解共存：法向相对速度抹掉，切向受 μλ_n 限制 ──
	//
	// bZeroVelocityOnContact 在探针分支已不再消费（只留球-球分支），这里仍置位
	// 只是为了钉住这一点：结果必须由冲量公式给出，而不是「撞上即停」。
	{
		FNBodySimulator Simulator = MakeSimulator();

		FCosmosBodyState Ship = MakeShip(0.09);
		Ship.Velocity = FVector3d(-0.01, 0.002, 0.0);
		Ship.bZeroVelocityOnContact = true;
		Ship.ContactProbeSurfaceRadii.Add(0.05);
		Ship.ContactSurfaceSphereIndex = 0;
		Simulator.AddBody(Ship);

		Simulator.StepExact(0.0);

		// 法向 −0.01 → λ_n = 0.01×0.75 = 0.0075；切向 0.002 需要 0.0015 的冲量，
		// 小于 μλ_n = 0.6×0.0075 = 0.0045，所以切向也被削光。行星质量 3，
		// 它的速度反向分走 λ/3。
		//
		// 相对速度整体归零 ⇒ 两者速度都等于质心速度：
		//     (m₁v₁ + m₂v₂) / (m₁ + m₂)
		//   = (1·(−0.01, 0.002, 0) + 3·(0, 0, 0)) / 4
		//   = (−0.0025, 0.0005, 0)
		const FVector3d Relative =
			Simulator.GetBody(1).Velocity - Simulator.GetBody(0).Velocity;
		TestEqual(TEXT("法向相对速度归零"), Relative.X, 0.0, 1.0e-15);
		TestEqual(TEXT("切向相对速度被摩擦削光"), Relative.Y, 0.0, 1.0e-15);

		TestEqual(TEXT("飞船速度 X = −0.0025"),
			Simulator.GetBody(1).Velocity.X, -0.0025, 1.0e-15);
		TestEqual(TEXT("飞船速度 Y = 0.0005"),
			Simulator.GetBody(1).Velocity.Y, 0.0005, 1.0e-15);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraRenderOriginTest,
	"StellarEra.Cosmos.RenderOrigin",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * 渲染原点只改「求解器坐标 → 世界坐标」这一层：锚点钉在关卡摆放处不动，其它天体按相对位置展开。
 * 这条是阴影逐帧闪烁的回归门 —— 原点一旦失效，世界又会以每帧公里级平移。
 */
bool FStellarEraRenderOriginTest::RunTest(const FString& Parameters)
{
	constexpr double Cm = 1.0e6;
	FNBodySimulator Simulator;
	Simulator.SetUnitSystem(ECosmosUnitSystem::Game);
	Simulator.SetCentimetersPerAu(Cm);

	const FVector3d Anchor = FVector3d(1.2e-4, -3.4e-5, 5.6e-6);
	const FVector AnchorWorld(120.0, -34.0, 5.6);

	// 没设原点：仍是绝对坐标，旧行为不动。
	TestEqual(TEXT("未设原点 = 绝对坐标（X）"),
		Simulator.ToWorldLocation(Anchor).X, Anchor.X * Cm, 1.0e-3);

	Simulator.SetRenderOrigin(Anchor, AnchorWorld);

	TestEqual(TEXT("锚点世界位置 = 关卡摆放（X）"),
		Simulator.ToWorldLocation(Anchor).X, AnchorWorld.X, 1.0e-3);

	// 锚点前进 1e-5 AU，世界坐标应保持在摆放处（这正是「世界静止」的定义）。
	const FVector3d Advanced = Anchor + FVector3d(1.0e-5, 0.0, 0.0);
	Simulator.SetRenderOrigin(Advanced, AnchorWorld);
	TestEqual(TEXT("锚点前进后世界位置不变（X）"),
		Simulator.ToWorldLocation(Advanced).X, AnchorWorld.X, 1.0e-3);

	// 其它天体只跟相对位置走：偏移 2e-5 AU → 20 厘米。
	TestEqual(TEXT("其它天体随相对位置平移"),
		Simulator.ToWorldLocation(Advanced + FVector3d(2.0e-5, 0.0, 0.0)).X,
		AnchorWorld.X + 20.0, 1.0e-2);

	// 注册路径要靠往返换算：写完再读回来必须是同一个求解器坐标。
	const FVector3d Probe = Advanced + FVector3d(-7.0e-5, 4.0e-5, 1.0e-5);
	const FVector3d RoundTrip = Simulator.ToGameUnitPosition(Simulator.ToWorldLocation(Probe));
	TestEqual(TEXT("往返换算（X）"), RoundTrip.X, Probe.X, 1.0e-9);
	TestEqual(TEXT("往返换算（Y）"), RoundTrip.Y, Probe.Y, 1.0e-9);
	TestEqual(TEXT("往返换算（Z）"), RoundTrip.Z, Probe.Z, 1.0e-9);

	return true;
}

// 等质量欧拉共线三星（UniverseMap 的构型）：谁都不比谁重，旧实现找不到主导天体、
// 两颗外侧星只能退回半圈有限路径。现在退到「其余天体质心」；中间星就在质心上，不能画巨环。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraClosedOrbitEqualMassTest,
	"StellarEra.Cosmos.NBodyPrediction.ClosedOrbitEqualMass",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraClosedOrbitEqualMassTest::RunTest(const FString& Parameters)
{
	FNBodySimulator Simulator;
	Simulator.SetUnitSystem(ECosmosUnitSystem::Game);
	Simulator.SetIntegrator(ECosmosIntegrator::VelocityVerlet);
	Simulator.SetSofteningLength(1.0e-5);
	Simulator.SetCentimetersPerAu(1.0e6);

	constexpr double Mass = 1.0e5;
	constexpr double Separation = 12.0;
	// 外侧星受力 Gm/d² + Gm/(2d)² = 5Gm/(4d²)，圆速度 √(5Gm/(4d))。
	const double Speed = FMath::Sqrt(5.0 * CosmosUnits::GravitationalConstant_Game * Mass / (4.0 * Separation));

	FCosmosBodyState Middle;
	Middle.Mass = Mass;
	Simulator.AddBody(Middle);

	FCosmosBodyState Left;
	Left.Mass = Mass;
	Left.Position = FVector3d(-Separation, 0.0, 0.0);
	Left.Velocity = FVector3d(0.0, -Speed, 0.0);
	Simulator.AddBody(Left);

	FCosmosBodyState Right;
	Right.Mass = Mass;
	Right.Position = FVector3d(Separation, 0.0, 0.0);
	Right.Velocity = FVector3d(0.0, Speed, 0.0);
	Simulator.AddBody(Right);

	for (const int32 Outer : { 1, 2 })
	{
		FCosmosClosedOrbitPrediction Closed;
		if (!TestTrue(FString::Printf(TEXT("外侧星 %d 能闭合"), Outer), Simulator.PredictClosedOrbit(Outer, 256, Closed)))
		{
			continue;
		}
		TestTrue(TEXT("参考点是全系统质心（中间星所在的原点）"), Closed.PrimaryPosition.Equals(FVector3d::ZeroVector, 1.0e-9));

		// 外侧星绕质心的半径就是 d：画出的圈与真实运动是同一个圆。
		double MaxDeviation = 0.0;
		for (const FVector3d& Point : Closed.RelativePositions)
		{
			MaxDeviation = FMath::Max(MaxDeviation, FMath::Abs(Point.Size() - Separation));
		}
		TestTrue(FString::Printf(TEXT("半径贴近 d（最大偏差 %.4f AU）"), MaxDeviation), MaxDeviation < 0.05 * Separation);
	}

	FCosmosClosedOrbitPrediction MiddleClosed;
	TestFalse(TEXT("中间星在质心上，不画巨环"), Simulator.PredictClosedOrbit(0, 256, MiddleClosed));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
