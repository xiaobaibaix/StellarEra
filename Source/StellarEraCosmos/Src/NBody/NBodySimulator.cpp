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
// 时标 —— 只读

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

