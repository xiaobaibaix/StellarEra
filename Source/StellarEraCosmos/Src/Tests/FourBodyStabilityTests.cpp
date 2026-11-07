// 三恒星 + 一行星：跑 10 模拟年，能量漂移与逃逸都要在阈值内 ——
// 这是本次改动唯一的「会不会跑飞」守门测试，参数一改就该重跑。

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "NBodySimulator.h"
#include "CosmosTypes.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	/** 三恒星 + 一行星的定稿初态。位置 AU、速度 AU/年、质量 M☉。 */
	void AddFourBodySystem(FNBodySimulator& Simulator)
	{
		auto Add = [&Simulator](const FVector& Position, const FVector& Velocity, double MassSolar)
		{
			FCosmosBodyState Body;
			Body.Position = Position;
			Body.Velocity = Velocity;
			Body.Mass = MassSolar;
			Simulator.AddBody(Body);
		};

		const double StarMass = 1.0e5;
		const double PlanetMass = 250.0;

		Add(FVector(0.0, 0.0, 0.0),    FVector(-0.5386, 0.0, 0.0), StarMass);
		Add(FVector(-200.0, 0.0, 0.0), FVector(0.0, 157.19, 0.0),  StarMass);
		Add(FVector(200.0, 0.0, 0.0),  FVector(0.0, -157.19, 0.0), StarMass);
		Add(FVector(0.0, 100.0, 0.0),  FVector(215.42, 0.0, 0.0),  PlanetMass);
	}

	FNBodySimulator MakeFourBodySimulator()
	{
		FNBodySimulator Simulator;
		Simulator.SetUnitSystem(ECosmosUnitSystem::Game);
		Simulator.SetIntegrator(ECosmosIntegrator::VelocityVerlet);
		Simulator.SetSofteningLength(1.0e-4);
		Simulator.SetCentimetersPerAu(1.0e6);
		AddFourBodySystem(Simulator);
		return Simulator;
	}
} // namespace

// 10 模拟年 ≈ 50 分钟真实时间（TimeScale 0.00333，1 模拟年 ≈ 300 秒）。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraFourBodyStabilityTest,
	"StellarEra.Cosmos.Simulator.FourBodyStability",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FStellarEraFourBodyStabilityTest::RunTest(const FString& Parameters)
{
	FNBodySimulator Simulator = MakeFourBodySimulator();
	Simulator.MarkEnergyBaseline();

	constexpr double TotalYears = 10.0;
	constexpr double StepYears = 1.0e-3;
	const int32 StepCount = FMath::RoundToInt(TotalYears / StepYears);

	for (int32 Index = 0; Index < StepCount; ++Index)
	{
		Simulator.StepExact(StepYears);
	}

	const double Drift = Simulator.GetEnergyDriftRatio();
	TestTrue(FString::Printf(TEXT("10 模拟年能量漂移 %.3e 应 < 1e-4"), Drift), Drift < 1.0e-4);

	// 没有天体被甩出去：最远的天体应在 500 AU 以内（初态最远 200 AU）。
	for (int32 Index = 0; Index < 4; ++Index)
	{
		const double Radius = Simulator.GetBody(Index).Position.Size();
		TestTrue(FString::Printf(TEXT("天体 %d 的轨道半径 %.1f AU 应 < 500"), Index, Radius),
			Radius < 500.0);
	}

	return true;
}

// 初速表本身也是事实：钉住定稿数值，防止有人按二体直觉改回去（二体公式给 140.5 AU/年，低 11%）。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraFourBodyVelocitiesTest,
	"StellarEra.Cosmos.Simulator.FourBodyInitialVelocities",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FStellarEraFourBodyVelocitiesTest::RunTest(const FString& Parameters)
{
	FNBodySimulator Simulator = MakeFourBodySimulator();

	const double ExpectedAuPerYear[4] = { 0.5386, 157.19, 157.19, 215.42 };
	for (int32 Index = 0; Index < 4; ++Index)
	{
		const double Speed = Simulator.GetBody(Index).Velocity.Size();
		TestEqual(FString::Printf(TEXT("天体 %d 的初速"), Index), Speed,
			ExpectedAuPerYear[Index], 0.01);
	}

	// 总动量应为零：系统质心不会漂。判据必须用**相对**容差 —— 初速表 0.5386 只写到
	// 四位小数（精确值 250×215.42/1e5 = 0.53855），乘 1e5 M☉ 后的残差是取整误差，不是物理。
	FVector Momentum = FVector::ZeroVector;
	double MomentumScale = 0.0;
	for (int32 Index = 0; Index < 4; ++Index)
	{
		const FCosmosBodyState& Body = Simulator.GetBody(Index);
		const FVector BodyMomentum = Body.Velocity * Body.Mass;
		Momentum += BodyMomentum;
		MomentumScale = FMath::Max(MomentumScale, BodyMomentum.Size());
	}

	const double MomentumRatio = Momentum.Size() / MomentumScale;
	TestTrue(FString::Printf(TEXT("总动量 %.3e 相对单项 %.3e 的比值 %.2e 应 < 1e-3"),
			Momentum.Size(), MomentumScale, MomentumRatio),
		MomentumRatio < 1.0e-3);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
