// TrajectoryPrediction.cpp —— 见头文件。

#include "TrajectoryPrediction.h"
#include "NBodySimulator.h"
#include "NBodyIntegration.h"
#include "CosmosCurveFilter.h"

namespace TrajectoryPrediction
{
	/** 一对天体的引力源质量：测试粒子作为源按 0 计。 */
	static double BodySourceMass(const FCosmosBodyState& Body)
	{
		return Body.bTestParticle ? 0.0 : Body.Mass;
	}

bool TrajectoryPrediction::Predict(const FNBodySimulator& Simulator, int32 BodyIndex, double Horizon,
                                   int32 NumSamples, TArray<FVector3d>& OutPositions)
{
	OutPositions.Reset();

	if (!Simulator.GetBodies().IsValidIndex(BodyIndex) || Horizon <= 0.0 || NumSamples < 2)
	{
		return false;
	}

	// 值类型，复制即得一份完整状态（见头文件注释）。
	FNBodySimulator Copy = Simulator;

	// 副本会继承外力与阻尼，不清零预测线就成了「一直加速」的曲线。这里预测松手后的纯引力轨迹。
	for (int32 Index = 0; Index < Copy.GetNumBodies(); ++Index)
	{
		FCosmosBodyState& CopyBody = Copy.GetBodyMutable(Index);
		CopyBody.ExternalAcceleration = FVector3d::ZeroVector;
		CopyBody.LinearDamping = 0.0;

		// 角方向的输入力矩与角阻尼同样是「松手就没了」，一并清零。
		CopyBody.ExternalAngularAcceleration = FVector3d::ZeroVector;
		CopyBody.AngularDamping = 0.0;

		// 预测副本必须自己积分：物理驱动的体在运行时由 Chaos 推进，副本里没有 Chaos。
		CopyBody.bPhysicsDriven = false;
	}

	// 步长取 Horizon / (NumSamples - 1)，末点恰好落在 Horizon 上。局部量不能叫 Step：
	// 遮蔽成员函数，UE 5.8 把 C4458 当硬错误。
	const double StepSize = Horizon / double(NumSamples - 1);

	OutPositions.Reserve(NumSamples);
	OutPositions.Add(Copy.GetBody(BodyIndex).Position);

	for (int32 SampleIndex = 1; SampleIndex < NumSamples; ++SampleIndex)
	{
		NBodyIntegration::StepVelocityVerlet(Copy, StepSize);
		OutPositions.Add(Copy.GetBody(BodyIndex).Position);
	}

	return true;
}

bool TrajectoryPrediction::PredictClosedOrbit(const FNBodySimulator& Simulator, int32 BodyIndex,
                                              int32 NumSamplesPerOrbit, FCosmosClosedOrbitPrediction& Out,
                                              double ReturnToleranceFraction)
{
	Out.RelativePositions.Reset();
	Out.PrimaryPosition = FVector3d::ZeroVector;
	Out.PeriodYears = 0.0;
	Out.ClosureErrorFraction = 0.0;

	if (!Simulator.GetBodies().IsValidIndex(BodyIndex) || NumSamplesPerOrbit < 2)
	{
		return false;
	}

	const FCosmosBodyState& Body = Simulator.GetBody(BodyIndex);

	// ── 主导天体：引力 M/r² 最大，且必须比本天体重 ──
	// 「更重」不是形式主义：否则重天体绕轻天体会画出一个与实际运动无关的巨环。
	// 用 BodySourceMass：测试粒子不该因质量大就成为别人的主导天体。
	int32 PrimaryIndex = INDEX_NONE;
	double BestPull = 0.0;
	for (int32 Index = 0; Index < Simulator.GetNumBodies(); ++Index)
	{
		if (Index == BodyIndex)
		{
			continue;
		}

		const FCosmosBodyState& Other = Simulator.GetBody(Index);
		const double SourceMass = BodySourceMass(Other);
		if (!(SourceMass > 0.0) || !(SourceMass > Body.Mass))
		{
			continue;
		}

		const double Distance = (Other.Position - Body.Position).Size();
		if (!(Distance > 0.0) || !FMath::IsFinite(Distance))
		{
			continue;
		}

		const double Pull = SourceMass / (Distance * Distance);
		if (Pull > BestPull)
		{
			BestPull = Pull;
			PrimaryIndex = Index;
		}
	}

	// 没有更重的天体（如等质量三星）时退到全系统质心：外侧星正好绕它转，中间星就在它上面、
	// 没有圈可画。只在其余天体总质量比自己重时才退，否则中心星会绕小行星画出巨环。
	const bool bUseBarycenter = PrimaryIndex == INDEX_NONE;
	const auto ReferencePosition = [PrimaryIndex](const FNBodySimulator& Sim) -> FVector3d
	{
		if (PrimaryIndex != INDEX_NONE)
		{
			return Sim.GetBody(PrimaryIndex).Position;
		}
		double Mass = 0.0;
		FVector3d Moment = FVector3d::ZeroVector;
		for (int32 Index = 0; Index < Sim.GetNumBodies(); ++Index)
		{
			const double SourceMass = BodySourceMass(Sim.GetBody(Index));
			Mass += SourceMass;
			Moment += SourceMass * Sim.GetBody(Index).Position;
		}
		return Mass > 0.0 ? Moment / Mass : FVector3d::ZeroVector;
	};

	FVector3d PrimaryPosition = FVector3d::ZeroVector;
	FVector3d PrimaryVelocity = FVector3d::ZeroVector;
	double Mu = 0.0;
	if (!bUseBarycenter)
	{
		PrimaryPosition = Simulator.GetBody(PrimaryIndex).Position;
		PrimaryVelocity = Simulator.GetBody(PrimaryIndex).Velocity;
		Mu = CosmosConstants::GravitationalConstant * (Body.Mass + Simulator.GetBody(PrimaryIndex).Mass);
	}
	else
	{
		double TotalMass = 0.0;
		double OthersMass = 0.0;
		FVector3d Moment = FVector3d::ZeroVector;
		FVector3d Momentum = FVector3d::ZeroVector;
		FVector3d Acceleration = FVector3d::ZeroVector;
		double NearestOther = TNumericLimits<double>::Max();
		for (int32 Index = 0; Index < Simulator.GetNumBodies(); ++Index)
		{
			const FCosmosBodyState& Other = Simulator.GetBody(Index);
			const double SourceMass = BodySourceMass(Other);
			TotalMass += SourceMass;
			Moment += SourceMass * Other.Position;
			Momentum += SourceMass * Other.Velocity;
			if (Index == BodyIndex || !(SourceMass > 0.0))
			{
				continue;
			}
			OthersMass += SourceMass;
			const FVector3d Delta = Other.Position - Body.Position;
			const double Separation = Delta.Size();
			if (Separation > 0.0)
			{
				NearestOther = FMath::Min(NearestOther, Separation);
				Acceleration += CosmosConstants::GravitationalConstant * SourceMass * Delta / (Separation * Separation * Separation);
			}
		}
		if (!(OthersMass > Body.Mass) || !(TotalMass > 0.0))
		{
			return false;
		}
		PrimaryPosition = Moment / TotalMass;
		PrimaryVelocity = Momentum / TotalMass;

		// 质心不是一个点质量：μ 用指向质心的实际引力反推（μ = a_r·r²），否则周期估错数倍、
		// 下面按周期定的搜索窗口转不满一圈。
		const FVector3d Offset = Body.Position - PrimaryPosition;
		const double Radius = Offset.Size();
		if (!(Radius > 1.0e-3 * NearestOther))
		{
			return false;
		}
		Mu = -FVector3d::DotProduct(Acceleration, Offset / Radius) * Radius * Radius;
	}

	const FVector3d RelativePosition = Body.Position - PrimaryPosition;
	const FVector3d RelativeVelocity = Body.Velocity - PrimaryVelocity;
	const double Distance = RelativePosition.Size();
	const double SpeedSquared = RelativeVelocity.SizeSquared();

	if (!(Distance > 0.0) || !(Mu > 0.0)
		|| !FMath::IsFinite(Distance) || !FMath::IsFinite(SpeedSquared) || !FMath::IsFinite(Mu))
	{
		return false;
	}

	// ── 密切轨道：ε < 0 才是椭圆；ε ≥ 0 是抛物线/双曲线，没有「一圈」可闭合 ──
	const double SpecificEnergy = 0.5 * SpeedSquared - Mu / Distance;
	if (!(SpecificEnergy < 0.0) || !FMath::IsFinite(SpecificEnergy))
	{
		return false;
	}

	const double SemiMajorAxis = -Mu / (2.0 * SpecificEnergy);
	if (!(SemiMajorAxis > 0.0) || !FMath::IsFinite(SemiMajorAxis))
	{
		return false;
	}

	// 偏心率。e≈1 是径向/抛物退化（如贴着行星静止的飞船）：会「闭合」成穿过主天体的直线。
	const FVector3d EccentricityVector =
		((SpeedSquared - Mu / Distance) * RelativePosition
			- FVector3d::DotProduct(RelativePosition, RelativeVelocity) * RelativeVelocity) / Mu;
	const double Eccentricity = EccentricityVector.Size();
	if (!(Eccentricity < 0.99) || !FMath::IsFinite(Eccentricity))
	{
		return false;
	}

	const double Period = 2.0 * PI * FMath::Sqrt(
		SemiMajorAxis * SemiMajorAxis * SemiMajorAxis / Mu);
	if (!(Period > 0.0) || !FMath::IsFinite(Period))
	{
		return false;
	}

	// 与 Predict 同口径：复制求解器，清零外力/阻尼，只算松手后的纯引力。
	FNBodySimulator Copy = Simulator;
	for (int32 Index = 0; Index < Copy.GetNumBodies(); ++Index)
	{
		FCosmosBodyState& CopyBody = Copy.GetBodyMutable(Index);
		CopyBody.ExternalAcceleration = FVector3d::ZeroVector;
		CopyBody.LinearDamping = 0.0;

		// 角方向的输入力矩与角阻尼同样是「松手就没了」，一并清零。
		CopyBody.ExternalAngularAcceleration = FVector3d::ZeroVector;
		CopyBody.AngularDamping = 0.0;

		// 预测副本必须自己积分：物理驱动的体在运行时由 Chaos 推进，副本里没有 Chaos。
		CopyBody.bPhysicsDriven = false;
	}

	// ── 用「转满一圈后离起点最近」找闭合时刻，而不是照密切周期截断 ──
	// 密切周期只是 t=0 的局部线性化，多体扰动下差数倍，按它截断会让预测线断成半截。
	// 它只用来定步长；最多积 3 个周期，扫满 2π 后取离起点最近的采样点。只认一圈之后，
	// 否则圆轨道相邻采样点间距就可能小于容差，出发不久即误判。
	const double StepSize = Period / double(NumSamplesPerOrbit - 1);
	const int32 SearchSamples = NumSamplesPerOrbit * 3;
	// 三体扰动下一圈回不到起点 5% 以内是常态（旧值 0.05 让大尺度轨道几乎全判失败）；
	// 放宽后误差由下面的线性分摊吸收，不会在接缝处留折角。
	const double ReturnTolerance = FMath::Max(ReturnToleranceFraction, 0.0) * SemiMajorAxis;
	constexpr double TwoPi = 2.0 * PI;
	// 留一点角度余量：最近点可能落在「跨过 2π」那一采样点之前半个步长。
	constexpr double AngleMargin = 0.1;

	TArray<FVector3d> Samples;
	Samples.Reserve(SearchSamples);

	const FVector3d StartRelative =
		Copy.GetBody(BodyIndex).Position - ReferencePosition(Copy);
	Samples.Add(StartRelative);

	FVector3d PreviousRelative = StartRelative;
	double SweptAngle = 0.0;
	int32 ReturnSample = INDEX_NONE;
	double BestReturnDistance = TNumericLimits<double>::Max();

	for (int32 SampleIndex = 1; SampleIndex < SearchSamples; ++SampleIndex)
	{
		NBodyIntegration::StepVelocityVerlet(Copy, StepSize);

		const FVector3d Relative =
			Copy.GetBody(BodyIndex).Position - ReferencePosition(Copy);
		Samples.Add(Relative);

		// 步长很小，atan2(|a×b|, a·b) 就是这一步扫过的夹角（无符号，顺逆行都成立）。
		SweptAngle += FMath::Atan2(
			FVector3d::CrossProduct(PreviousRelative, Relative).Size(),
			FVector3d::DotProduct(PreviousRelative, Relative));
		PreviousRelative = Relative;

		if (SweptAngle >= TwoPi - AngleMargin)
		{
			const double ReturnDistance = (Relative - StartRelative).Size();
			if (ReturnDistance < BestReturnDistance)
			{
				BestReturnDistance = ReturnDistance;
				ReturnSample = SampleIndex;
			}
		}
	}

	// 转满一圈后都回不到起点附近：不是闭环（被拽走 / 严重进动）。
	if (ReturnSample == INDEX_NONE || BestReturnDistance > ReturnTolerance)
	{
		return false;
	}

	Out.RelativePositions.Append(Samples.GetData(), ReturnSample + 1);
	Out.PrimaryPosition = PrimaryPosition;
	Out.PeriodYears = double(ReturnSample) * StepSize;
	Out.ClosureErrorFraction = BestReturnDistance / SemiMajorAxis;

	// 末点 ≈ 起点：误差按时间摊开后末点与首点逐位相等。
	CosmosCurveFilter::DistributeClosureError(Out.RelativePositions);
	return true;
}
}
