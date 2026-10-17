// Copyright Voxel Plugin SAS. All Rights Reserved.
//
// N 体求解器「预测」功能的自动化测试。
//
// 这几组断言挡的是同一类错误：预测看起来像那么回事、其实是错的。
// 全是纯数学 —— 不依赖世界、不依赖编辑器，这是本项目唯一能自动验证的地方。
//
// 跑法见 plan 的 Task 1 Step 4。

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

#endif // WITH_DEV_AUTOMATION_TESTS
