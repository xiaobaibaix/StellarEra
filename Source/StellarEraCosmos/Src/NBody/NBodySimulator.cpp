// N 体引力求解器 —— 实现。

#include "NBodySimulator.h"
#include "NBodyIntegration.h"
#include "CosmosCurveFilter.h"

// TWeakObjectPtr<AActor> 的成员函数实例化需要 AActor 完整定义，显式引入以免换 PCH 策略后编不过。
#include "GameFramework/Actor.h"

FNBodySimulator::FNBodySimulator()
{
	MarkEnergyBaseline();
}

///////////////////////////////////////////////////////////////////////////////
// 参数

void FNBodySimulator::SetSofteningLength(double InSoftening)
{
	SofteningLength = FMath::Max(InSoftening, 1e-12);
}

void FNBodySimulator::SetCentimetersPerAu(double InCentimetersPerAu)
{
	// 给一个下限：0 会让所有天体坍缩到原点，负数更无意义。
	CentimetersPerAu = FMath::Max(InCentimetersPerAu, 1.0);
}

///////////////////////////////////////////////////////////////////////////////
// 天体管理

int32 FNBodySimulator::AddBody(const FCosmosBodyState& Body)
{
	const int32 Index = Bodies.Add(Body);
	Accelerations.Add(FVector3d::ZeroVector);
	ComputeAccelerations();
	return Index;
}

int32 FNBodySimulator::FindBodyByActor(const AActor* Actor) const
{
	for (int32 i = 0; i < Bodies.Num(); ++i)
	{
		if (Bodies[i].OwnerActor.Get() == Actor)
		{
			return i;
		}
	}
	return INDEX_NONE;
}

///////////////////////////////////////////////////////////////////////////////
// 引力计算

double FNBodySimulator::GetG() const
{
	return CosmosConstants::GravitationalConstant;
}

void FNBodySimulator::ComputeAccelerations()
{
	const int32 N = Bodies.Num();
	Accelerations.SetNumZeroed(N);

	const double G = GetG();

	const double CutoffSq = (GravityCutoff > 0.0) ? GravityCutoff * GravityCutoff : 0.0;

	for (int32 i = 0; i < N; ++i)
	{
		FVector3d Sum = FVector3d::ZeroVector;

		for (int32 j = 0; j < N; ++j)
		{
			if (i == j)
			{
				continue;
			}

			const FVector3d Delta = Bodies[j].Position - Bodies[i].Position;
			double R2 = Delta.SizeSquared();

			if (CutoffSq > 0.0 && R2 > CutoffSq)
			{
				continue;
			}

			// 软化：给距离加下限，彻底消除 r→0 的奇点，否则擦肩而过时数值溢出。
			const double Soft = Bodies[i].SofteningOverride > 0.0
				? Bodies[i].SofteningOverride
				: SofteningLength;
			R2 += Soft * Soft;

			// 1/r³
			const double InvR = 1.0 / FMath::Sqrt(R2);
			const double InvR3 = InvR * InvR * InvR;

			// 用引力源质量而非 Mass：测试粒子作为源不吸引别人。
			Sum += GravitationalSourceMass(Bodies[j]) * InvR3 * Delta;
		}

		// 外力与阻尼逐子步求值（显式欧拉，dt 逐关卡派生、不可硬编码）。引力单独存一份
		// 供星图箭头读，不和外力/阻尼混，否则推力会搅混方向。
		if (!Bodies[i].bReceivesForces)
		{
			// 不受力：加速度恒为零，位置按当前速度匀速。作为引力源仍参与上面的 Sum。
			Bodies[i].GravitationalAcceleration = FVector3d::ZeroVector;
			Accelerations[i] = FVector3d::ZeroVector;
			continue;
		}

		Bodies[i].GravitationalAcceleration = G * Sum;
		Accelerations[i] = G * Sum
			+ Bodies[i].ExternalAcceleration
			- Bodies[i].LinearDamping * Bodies[i].Velocity;
	}

	for (int32 i = 0; i < N; ++i)
	{
		Bodies[i].Acceleration = Accelerations[i];
	}
}


///////////////////////////////////////////////////////////////////////////////
// 诊断

double FNBodySimulator::GetTotalEnergy() const
{
	const double G = GetG();

	double Kinetic = 0.0;
	double Potential = 0.0;

	for (int32 i = 0; i < Bodies.Num(); ++i)
	{
		Kinetic += 0.5 * Bodies[i].Mass * Bodies[i].Velocity.SizeSquared();
	}

	for (int32 i = 0; i < Bodies.Num(); ++i)
	{
		for (int32 j = i + 1; j < Bodies.Num(); ++j)
		{
			double R2 = (Bodies[j].Position - Bodies[i].Position).SizeSquared();
			const double Soft = Bodies[i].SofteningOverride > 0.0
				? Bodies[i].SofteningOverride
				: SofteningLength;
			R2 += Soft * Soft;

			// 势能也用引力源质量，与 ComputeAccelerations 同口径，否则测试粒子会贡献不存在的势能。
			Potential -= G * GravitationalSourceMass(Bodies[i])
				* GravitationalSourceMass(Bodies[j]) / FMath::Sqrt(R2);
		}
	}

	return Kinetic + Potential;
}

FVector3d FNBodySimulator::GetTotalMomentum() const
{
	FVector3d Sum = FVector3d::ZeroVector;
	for (const FCosmosBodyState& B : Bodies)
	{
		Sum += B.Mass * B.Velocity;
	}
	return Sum;
}

FVector3d FNBodySimulator::GetTotalAngularMomentum() const
{
	// 先算质心，角动量必须相对质心才有意义
	FVector3d CenterOfMass = FVector3d::ZeroVector;
	double TotalMass = 0.0;
	for (const FCosmosBodyState& B : Bodies)
	{
		CenterOfMass += B.Mass * B.Position;
		TotalMass += B.Mass;
	}
	if (TotalMass <= 0.0)
	{
		return FVector3d::ZeroVector;
	}
	CenterOfMass /= TotalMass;

	FVector3d Sum = FVector3d::ZeroVector;
	for (const FCosmosBodyState& B : Bodies)
	{
		Sum += B.Mass * FVector3d::CrossProduct(B.Position - CenterOfMass, B.Velocity);
	}
	return Sum;
}

void FNBodySimulator::MarkEnergyBaseline()
{
	EnergyBaseline = GetTotalEnergy();
	bEnergyBaselineValid = true;
}

double FNBodySimulator::GetEnergyDriftRatio() const
{
	if (!bEnergyBaselineValid || FMath::IsNearlyEqual(EnergyBaseline, 0.0))
	{
		return 0.0;
	}
	return (GetTotalEnergy() - EnergyBaseline) / FMath::Abs(EnergyBaseline);
}

///////////////////////////////////////////////////////////////////////////////
// 坐标转换

FVector FNBodySimulator::ToWorldLocation(const FVector3d& GameUnitPosition) const
{
	return FVector(GameUnitPosition) * CentimetersPerAu;
}

FVector3d FNBodySimulator::ToGameUnitPosition(const FVector& WorldLocation) const
{
	return FVector3d(WorldLocation) / CentimetersPerAu;
}

///////////////////////////////////////////////////////////////////////////////
// 预测 —— 只读

double FNBodySimulator::PairOrbitalPeriod(const FCosmosBodyState& A, const FCosmosBodyState& B, double G)
{
	const double Separation = (B.Position - A.Position).Size();
	const double Mu = G * (A.Mass + B.Mass);

	// 间距或总质量为零时这一对没有意义，返回 0 让调用方跳过。
	if (Separation <= 0.0 || Mu <= 0.0)
	{
		return 0.0;
	}

	return 2.0 * PI * FMath::Sqrt(Separation * Separation * Separation / Mu);
}

double FNBodySimulator::EstimateOrbitalPeriod(int32 BodyIndex) const
{
	if (!Bodies.IsValidIndex(BodyIndex))
	{
		return 0.0;
	}

	const double G = GetG();
	double Shortest = 0.0;

	for (int32 j = 0; j < Bodies.Num(); ++j)
	{
		if (j == BodyIndex)
		{
			continue;
		}

		const double Period = PairOrbitalPeriod(Bodies[BodyIndex], Bodies[j], G);
		if (Period > 0.0 && (Shortest <= 0.0 || Period < Shortest))
		{
			Shortest = Period;
		}
	}

	return Shortest;
}

bool FNBodySimulator::PredictTrajectory(int32 BodyIndex, double Horizon, int32 NumSamples,
                                        TArray<FVector3d>& OutPositions) const
{
	OutPositions.Reset();

	if (!Bodies.IsValidIndex(BodyIndex) || Horizon <= 0.0 || NumSamples < 2)
	{
		return false;
	}

	// 值类型，复制即得一份完整状态（见头文件注释）。
	FNBodySimulator Copy = *this;

	// 副本会继承外力与阻尼，不清零预测线就成了「一直加速」的曲线。这里预测松手后的纯引力轨迹。
	for (FCosmosBodyState& Body : Copy.Bodies)
	{
		Body.ExternalAcceleration = FVector3d::ZeroVector;
		Body.LinearDamping = 0.0;

		// 角方向的输入力矩与角阻尼同样是「松手就没了」，一并清零。
		Body.ExternalAngularAcceleration = FVector3d::ZeroVector;
		Body.AngularDamping = 0.0;
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

bool FNBodySimulator::PredictClosedOrbit(int32 BodyIndex, int32 NumSamplesPerOrbit,
                                         FCosmosClosedOrbitPrediction& Out,
                                         double ReturnToleranceFraction) const
{
	Out.RelativePositions.Reset();
	Out.PrimaryPosition = FVector3d::ZeroVector;
	Out.PeriodYears = 0.0;
	Out.ClosureErrorFraction = 0.0;

	if (!Bodies.IsValidIndex(BodyIndex) || NumSamplesPerOrbit < 2)
	{
		return false;
	}

	const FCosmosBodyState& Body = Bodies[BodyIndex];

	// ── 主导天体：引力 M/r² 最大，且必须比本天体重 ──
	// 「更重」不是形式主义：否则重天体绕轻天体会画出一个与实际运动无关的巨环。
	// 用 GravitationalSourceMass：测试粒子不该因质量大就成为别人的主导天体。
	int32 PrimaryIndex = INDEX_NONE;
	double BestPull = 0.0;
	for (int32 Index = 0; Index < Bodies.Num(); ++Index)
	{
		if (Index == BodyIndex)
		{
			continue;
		}

		const FCosmosBodyState& Other = Bodies[Index];
		const double SourceMass = GravitationalSourceMass(Other);
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
			const double SourceMass = GravitationalSourceMass(Sim.GetBody(Index));
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
		PrimaryPosition = Bodies[PrimaryIndex].Position;
		PrimaryVelocity = Bodies[PrimaryIndex].Velocity;
		Mu = GetG() * (Body.Mass + Bodies[PrimaryIndex].Mass);
	}
	else
	{
		double TotalMass = 0.0;
		double OthersMass = 0.0;
		FVector3d Moment = FVector3d::ZeroVector;
		FVector3d Momentum = FVector3d::ZeroVector;
		FVector3d Acceleration = FVector3d::ZeroVector;
		double NearestOther = TNumericLimits<double>::Max();
		for (int32 Index = 0; Index < Bodies.Num(); ++Index)
		{
			const FCosmosBodyState& Other = Bodies[Index];
			const double SourceMass = GravitationalSourceMass(Other);
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
				Acceleration += GetG() * SourceMass * Delta / (Separation * Separation * Separation);
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

	// 与 PredictTrajectory 同口径：复制求解器，清零外力/阻尼，只算松手后的纯引力。
	FNBodySimulator Copy = *this;
	for (FCosmosBodyState& CopyBody : Copy.Bodies)
	{
		CopyBody.ExternalAcceleration = FVector3d::ZeroVector;
		CopyBody.LinearDamping = 0.0;

		// 角方向的输入力矩与角阻尼同样是「松手就没了」，一并清零。
		CopyBody.ExternalAngularAcceleration = FVector3d::ZeroVector;
		CopyBody.AngularDamping = 0.0;
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

///////////////////////////////////////////////////////////////////////////////
// 刚体姿态

FVector3d FCosmosBodyState::InverseInertiaFromBox(double Mass, const FVector3d& HalfExtent)
{
	const FVector3d Edge = HalfExtent * 2.0;
	if (Mass <= 0.0 || Edge.X <= 0.0 || Edge.Y <= 0.0 || Edge.Z <= 0.0)
	{
		return FVector3d::ZeroVector;
	}

	const double TwelveOverMass = 12.0 / Mass;
	return FVector3d(
		TwelveOverMass / (Edge.Y * Edge.Y + Edge.Z * Edge.Z),
		TwelveOverMass / (Edge.X * Edge.X + Edge.Z * Edge.Z),
		TwelveOverMass / (Edge.X * Edge.X + Edge.Y * Edge.Y));
}

FVector3d FCosmosBodyState::InverseInertiaFromSphere(double Mass, double Radius)
{
	const double Inertia = 0.4 * Mass * Radius * Radius;
	return Inertia > 0.0 ? FVector3d(1.0 / Inertia) : FVector3d::ZeroVector;
}

