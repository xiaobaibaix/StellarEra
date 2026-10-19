// Copyright Voxel Plugin SAS. All Rights Reserved.
//
// N 体求解器「预测」与「接触」功能的自动化测试。
//
// 这几组断言挡的是同一类错误：功能看起来像那么回事、其实是错的。
// 全是纯数学 —— 不依赖世界、不依赖编辑器，这是本项目唯一能自动验证的地方。
//
// 跑法：用 UnrealEditor-Cmd 跑自动化测试 StellarEra.Core（五个用例：
// StellarEra.Core.NBodyPrediction、StellarEra.Core.NBodyContact、
// StellarEra.Core.NBodyExternalForce、StellarEra.Core.NBodyTestParticle、
// StellarEra.Core.Units）。

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
	"StellarEra.Core.NBodyPrediction",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraNBodyPredictionTest::RunTest(const FString& Parameters)
{
	const double G = CosmosUnits::GravitationalConstant_Game;

	// ── 1. 配对周期公式：钉住具体数值 ──
	// 下表的期望值是 2π√( r³ / (G·(m_A+m_B)) ) 的**精确输出**：行星质量 3e-6 是
	// **算进去的**，不是忽略的。不要把它们「化简」回 √(r³/m_中心) —— 那样更符合
	// 直觉，却会放过「公式漏掉行星质量」这类错误，而正是这类错误会污染
	// FinalizeRegistration 里取最短周期的那一步。写成小数值而不是再写一遍公式，
	// 是为了让「G 改了」或「漏了/多了个 2π」真的会让测试红。
	//
	// 前两行（等质量 1+1）带一个 ~2e-8 的偏移，来源是 UE 的 PI 宏是 float 字面量：
	// 公式里的 2.0*PI 是「float 提升成 double」的 6.2831854820251465，不是 2π 的
	// 双精度值。手算对不出这个偏移，别照着双精度 π 去「修正」它们。
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

	// ── 2. 最短时标取的是「配对最小值」 ──
	// 恒星 + 近行星(0.5AU) + 远行星(4AU)。恒星与近行星那一对最短。
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
	// 这条挡的是最危险的 bug：预测把真实模拟带跑偏。那种错在 PIE 里表现为
	// 「轨道慢慢变形」，很容易被当成积分器精度问题查半天。
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
	"StellarEra.Core.NBodyContact",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraNBodyContactTest::RunTest(const FString& Parameters)
{
	// ── 1. 接触的纯几何：一步、且这一步里积分器什么都不做 ──
	// dt = 0 让 StepExact 只剩 ResolveContacts 有作用 —— 引力推进量是零，接触是
	// 唯一的变量。这样期望值能按公式手算出来钉死，而不是「跑跑看差不多」。
	//
	// 两颗质量不等的天体迎面对撞，且**总动量不为零**（3×0.05 + 1×(−0.01) = 0.14）。
	// 这一条同时挡两种写错的接触：
	//   · 只推一颗的 —— 位移必须按 0.25 / 0.75 分（轻的让得多）；
	//   · 抹掉各自**绝对**法向速度的 —— 那会把总动量吃成 0，而不是保持 0.14。
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

		// 半径之和 0.1，初始间距 0.08 → 重叠 0.02。让路比例 = 对方质量 / 总质量：
		//   Heavy 让 1/4 = 0.25： −0.04 − 0.02×0.25 = −0.045
		//   Light 让 3/4 = 0.75： +0.04 + 0.02×0.75 = +0.055
		TestEqual(TEXT("重的一方少让路"),
			Simulator.GetBody(0).Position.X, -0.045, 1.0e-15);
		TestEqual(TEXT("轻的一方多让路"),
			Simulator.GetBody(1).Position.X, 0.055, 1.0e-15);
		TestEqual(TEXT("推开后正好相切（间距 = 半径之和）"),
			(Simulator.GetBody(1).Position - Simulator.GetBody(0).Position).Size(),
			0.1, 1.0e-15);

		// 相对法向速度被抹平 → 两者沿 X 的速度变得**相等**，而**不是都变成 0**。
		// 相等值由动量守恒定：0.14 / (3+1) = 0.035。
		TestEqual(TEXT("接触后两者法向速度相等"),
			Simulator.GetBody(0).Velocity.X, Simulator.GetBody(1).Velocity.X, 1.0e-15);
		TestEqual(TEXT("相等值 = 总动量 / 总质量 = 0.035"),
			Simulator.GetBody(0).Velocity.X, 0.035, 1.0e-15);

		TestTrue(TEXT("接触不改变总动量"),
			(Simulator.GetTotalMomentum() - MomentumBefore).Size() < 1.0e-15);

		// 切向分量必须原样留着（接触面是光滑的）。
		//
		// 初态要**重新摆成迎面接近**：位置与 X 向速度都复位，否则相对法向速度
		// 已经是 0，接触那一支根本不执行，这条断言就变成恒真的了。Y 分量是这里
		// 唯一新增的东西，接触解算不该碰它。
		Simulator.GetBodyMutable(0).Position = FVector3d(-0.04, 0.0, 0.0);
		Simulator.GetBodyMutable(1).Position = FVector3d(0.04, 0.0, 0.0);
		Simulator.GetBodyMutable(0).Velocity = FVector3d(0.05, 0.007, 0.0);
		Simulator.GetBodyMutable(1).Velocity = FVector3d(-0.01, -0.003, 0.0);

		Simulator.StepExact(0.0);

		TestEqual(TEXT("切向速度不被接触改动（重）"),
			Simulator.GetBody(0).Velocity.Y, 0.007, 1.0e-15);
		TestEqual(TEXT("切向速度不被接触改动（轻）"),
			Simulator.GetBody(1).Velocity.Y, -0.003, 1.0e-15);

		// 同时确认法向确实被处理了 —— 否则上面两条可能只是因为整个接触分支
		// 没执行而通过。
		TestEqual(TEXT("同一步里法向速度仍被抹平"),
			Simulator.GetBody(0).Velocity.X, Simulator.GetBody(1).Velocity.X, 1.0e-15);
	}

	// ── 2. 自由落体落到地表并停住 ──
	// 质量与半径都取本关卡 A 的实际值（0.873237 M☉ / 0.075 AU），飞船取
	// AddBodyToSimulator 里实际填的（质量 0、半径 0 —— 它不是体素星球）。
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

		// 全程一次都没穿透。位置钳制不留余量、且每步都在 StepExact 末尾执行，
		// 所以**任何一步结束时**的距离都不该小于半径。留的 1e-12 是浮点舍入，
		// 不是穿透余量 —— 把它放大到 1e-3 就会放过「每步往下陷一点」那类错。
		TestTrue(TEXT("全程没有穿透地表"),
			DeepestBelowSurface < 1.0e-12);

		// 停住而不是弹跳：法向速度被抹掉。这一条不是恒真的 —— 每步重力都会给
		// 飞船加上 a·dt ≈ 6.1e-3 AU/年 的接近速度，是接触解算把它抹回去的。
		TestTrue(TEXT("法向速度归零（停住、不回弹）"),
			FMath::Abs(FinalShip.Velocity.X) < 1.0e-12);

		// 质量 0 的飞船推不动行星。
		TestEqual(TEXT("行星没有被飞船推走"),
			Simulator.GetBody(0).Position.X, 0.0, 1.0e-15);
	}

	// ── 3. 对照组：把半径去掉，同一套初态就会穿过去 ──
	// 这是「接触项确实是拦住它的那一项」的反证。没有它，第 2 组断言有可能被
	// 别的东西（比如软化长度）意外满足，而接触项其实没生效。
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
	// 飞船带切向速度斜撞向行星。默认的光滑硬球会保留切向分量（沿地表滑），
	// 但飞船开了 bZeroVelocityOnContact，所以接触后它的速度应**完全等于**行星
	// 的速度（相对速度归零），切向分量一并被抹掉。
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
	"StellarEra.Core.NBodyExternalForce",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraNBodyExternalForceTest::RunTest(const FString& Parameters)
{
	// ── 1. 常力下的抛物线是**精确**的 ──
	//
	// Velocity Verlet 对「加速度与位置、速度都无关」的力是精确解，不是近似解：
	// 每一步的 x += v·dt/2 → 算 a → v += a·dt → x += v·dt/2 串起来正好等于
	// x₀ + v₀t + ½at²，与步长无关。所以这里可以用 1e-9 这种量级的容差 ——
	// 留出来的余量只装浮点累加误差（1000 次加法，量级 1e-13）。
	//
	// 这条钉住的是「外力真的进了积分器」。写错成「每帧只加一次」「忘了乘 dt」
	// 「加到了位置上而不是加速度上」都会让末点偏出十万八千里。
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
	//
	// 阻尼项在 StepVelocityVerlet 里用的是**踢之前**的速度（步骤②重算加速度时
	// 速度还没更新），所以它是显式欧拉：v_{n+1} = v_n·(1 − k·dt)，闭式解
	// v_N = v₀·(1 − k·dt)^N，恒小于解析解 v₀·e^(−k·t)。显式欧拉是一阶的，
	// 所以步长减半、误差也减半 —— 这正是下面那条「一阶收敛」断言钉的东西。
	//
	// 为什么要钉「一阶」而不只钉数值：只钉数值的话，把阻尼写成用**踢之后**的
	// 速度（隐式欧拉，误差反号）照样能过。阶数对了，说明实现方式就是设计里那一种。
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
		// 期望比值 ≈ 0.5（一阶），放到 0.6 留余量 —— 挡得住「零阶」（比值 1）。
		// **挡不住二阶**：0.25 照样小于 0.6。二阶由上面那行下限
		// ErrCoarse > 1e-8 挡（本配置下二阶格式的误差约 1e-9，落在下限之下），
		// 所以那个看着多余的下限是二阶的唯一鉴别力，别删。
		TestTrue(TEXT("阻尼：步长减半，误差减半（一阶收敛）"), ErrFine < ErrCoarse * 0.6);
	}

	// ── 3. 终端速度 v → A/k ──
	//
	// 这是飞船赖以工作的那一条：阻尼模型下按 W 会收敛到「推力 × τ」，而 τ = 1/k。
	// 离散系统的不动点**恰好**是 A/k（解 v = A·dt + v·(1 − k·dt) 即得），
	// 所以长跑之后应该贴在 A/k 上。写成 A/(k·dt) 或 A·k 都会在这里红。
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
	//
	// PredictTrajectory 是 `FNBodySimulator Copy = *this;` 再往前跑。值拷贝会把
	// ExternalAcceleration / LinearDamping 一起带过去，星图上就会画出一条「一直
	// 加速」的线 —— 那不是玩家要的信息（预测的是**松手之后**的纯引力轨迹）。
	//
	// 用「同一初态、一份设外力一份不设」的对照跑逐点比对，比「主求解器没被改动」
	// 更严：后者只挡得住「忘了用副本」，挡不住「副本继承了外力」。
	{
		auto MakePair = [](bool bForced)
		{
			FNBodySimulator Simulator = MakeCircularTwoBody(0.718, 1.0);
			if (bForced)
			{
				// 施在**下标 1** 那个天体身上（也就是下面预测的那个）—— 一旦副本
				// 继承了外力，预测线会被推得肉眼可见（0.608 年里偏 ½×5×0.608² ≈ 0.92 AU）。
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

		// 逐点比，只累加「不一样」的个数 —— 64 次 Printf 会淹掉日志。
		// 两份求解器由同一个函数、同一串运算造出来，浮点结果必须逐位相同。
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
	"StellarEra.Core.NBodyTestParticle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraNBodyTestParticleTest::RunTest(const FString& Parameters)
{
	const double G = CosmosUnits::GravitationalConstant_Game;

	// 两体系统：中心天体 + 一颗候选测试粒子，距离 1 AU、质量都是 1 M☉。
	// 质量取这么大是为了让「粒子仍然施力」时的后果一眼可见 —— 若它还在拽中心
	// 天体，0.04 年里中心天体就移动约 0.03 AU，不可能被容差吃掉。
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

	// ── 1. 加速度：粒子仍被吸引，中心天体不受它吸引 ──
	// 这条挡的是「把测试粒子的引力整条关掉」—— 那会让粒子也不动，功能看起来
	// 一样是「不拽别人」，但飞船就不受行星引力了。
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

		// 同一段积分在对照组里必须把中心天体拉开 —— 否则上面那条「不动」可能只是
		// 因为两体问题本身就不动，而测试粒子标志根本没参与。
		TestTrue(TEXT("对照：普通粒子把中心天体拉离原点"),
			Control.GetBody(0).Position.X > 1.0e-3);
	}

	// ── 3. 单向不进入能量诊断：不贡献幻影势能 ──
	// 两个天体都静止，动能项为 0。测试粒子作为引力源质量 0，于是势能项也为 0：
	// 总能量必须是 0，而不是普通两体的 −G·M·m/r。
	{
		FNBodySimulator Simulator = MakePair(true);
		TestEqual(TEXT("总能量不含测试粒子的幻影势能"),
			Simulator.GetTotalEnergy(), 0.0, 1.0e-15);

		FNBodySimulator Control = MakePair(false);
		TestEqual(TEXT("对照：普通两体能量 = −G·M·m/r"),
			Control.GetTotalEnergy(), -G, 1.0e-6);
	}

	// ── 4. 预测副本继承标志 ──
	// PredictTrajectory 是值拷贝，bTestParticle 会跟着过去；这里钉住「中心天体的
	// 预测线不被测试粒子拉动」，同时钉住预测本身仍在推进（粒子确实下落）。
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
// 单位换算
//
// 这些断言挡的是「换算写反/写错一个量级」：solve 和编辑器边界都靠它们。
// 容差取到能分辨最后几位有效数字，但不会被浮点噪声误伤。

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraUnitsTest,
	"StellarEra.Core.Units",
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

#endif // WITH_DEV_AUTOMATION_TESTS
