// N 体引力求解器 —— 实现。

#include "NBodySimulator.h"

// UE 的 double 版三角函数只在 <Math/Utils.h> 里提供。
// FMath::Sin / Cos / Atan2 只有 float 重载，
// 传 double 会被隐式截断，直接损失 7 位有效数字——
// 在 N 体模拟里这是不可接受的精度损失。
#include "Math/Utils.h"

FNBodySimulator::FNBodySimulator()
{
	MarkEnergyBaseline();
}

///////////////////////////////////////////////////////////////////////////////
// 参数

void FNBodySimulator::SetUnitSystem(ECosmosUnitSystem Unit)
{
	UnitSystem = Unit;
}

void FNBodySimulator::SetSofteningLength(double InSoftening)
{
	SofteningLength = FMath::Max(InSoftening, 1e-12);
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

void FNBodySimulator::ComputeAccelerations()
{
	const int32 N = Bodies.Num();
	Accelerations.SetNumZeroed(N);

	const double G = (UnitSystem == ECosmosUnitSystem::Game)
		? CosmosUnits::GravitationalConstant_Game
		: CosmosUnits::GravitationalConstant_SI;

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

			// 软化：给距离加一个下限，彻底消除 r→0 时的奇点。
			// 没有这一项，两颗星擦肩而过时加速度会趋于无穷大，数值直接溢出。
			const double Soft = Bodies[i].SofteningOverride > 0.0
				? Bodies[i].SofteningOverride
				: SofteningLength;
			R2 += Soft * Soft;

			// 1/r³
			const double InvR = 1.0 / FMath::Sqrt(R2);
			const double InvR3 = InvR * InvR * InvR;

			Sum += Bodies[j].Mass * InvR3 * Delta;
		}

		Accelerations[i] = G * Sum;
	}

	for (int32 i = 0; i < N; ++i)
	{
		Bodies[i].Acceleration = Accelerations[i];
	}
}

///////////////////////////////////////////////////////////////////////////////
// 积分

void FNBodySimulator::StepVelocityVerlet(double Dt)
{
	const int32 N = Bodies.Num();
	if (N == 0)
	{
		return;
	}

	// ── 辛积分的核心：位置半步 → 速度全步 → 位置半步 ──
	// 这样做的关键是保持相空间体积，误差有界不累积。

	// ① 用当前加速度推位置（半步）
	for (int32 i = 0; i < N; ++i)
	{
		Bodies[i].Position += Bodies[i].Velocity * (Dt * 0.5);
	}

	// ② 在新位置重算加速度
	ComputeAccelerations();

	// ③ 用新加速度推速度（全步），再用新速度补完位置（半步）
	for (int32 i = 0; i < N; ++i)
	{
		Bodies[i].Velocity += Bodies[i].Acceleration * Dt;
		Bodies[i].Position += Bodies[i].Velocity * (Dt * 0.5);
	}

	SimulatedTime += Dt;
}

void FNBodySimulator::StepEuler(double Dt)
{
	// 显式欧拉法：一阶方法，误差同向累积。
	// 轨道会在可观时间内螺旋坠入中心天体。这里仅用于对比验证。
	ComputeAccelerations();

	for (int32 i = 0; i < Bodies.Num(); ++i)
	{
		Bodies[i].Velocity += Bodies[i].Acceleration * Dt;
		Bodies[i].Position += Bodies[i].Velocity * Dt;
	}

	SimulatedTime += Dt;
}

void FNBodySimulator::StepExact(double TimeStep)
{
	switch (Integrator)
	{
	case ECosmosIntegrator::VelocityVerlet:
		StepVelocityVerlet(TimeStep);
		break;

	case ECosmosIntegrator::Euler:
		StepEuler(TimeStep);
		break;

	case ECosmosIntegrator::None:
		// 不积分：天体保持初始速度做匀速直线运动
		SimulatedTime += TimeStep;
		break;
	}
}

void FNBodySimulator::Step(float Dt)
{
	if (Dt <= 0.0f)
	{
		return;
	}

	Accumulator += Dt;
	StepExact(Accumulator);
	Accumulator = 0.0;
}

///////////////////////////////////////////////////////////////////////////////
// 诊断

double FNBodySimulator::GetTotalEnergy() const
{
	const double G = (UnitSystem == ECosmosUnitSystem::Game)
		? CosmosUnits::GravitationalConstant_Game
		: CosmosUnits::GravitationalConstant_SI;

	double Kinetic = 0.0;
	double Potential = 0.0;

	for (int32 i = 0; i < Bodies.Num(); ++i)
	{
		// 动能 ½mv²
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

			// 势能 −Gm_im_j / r
			Potential -= G * Bodies[i].Mass * Bodies[j].Mass / FMath::Sqrt(R2);
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
	if (UnitSystem == ECosmosUnitSystem::Game)
	{
		return FVector(GameUnitPosition) * CosmosUnits::AuToCentimeter;
	}
	return FVector(GameUnitPosition);
}

FVector3d FNBodySimulator::ToGameUnitPosition(const FVector& WorldLocation) const
{
	if (UnitSystem == ECosmosUnitSystem::Game)
	{
		return FVector3d(WorldLocation) / CosmosUnits::AuToCentimeter;
	}
	return FVector3d(WorldLocation);
}

FVector FNBodySimulator::ToWorldVelocity(const FVector3d& GameUnitVelocity) const
{
	if (UnitSystem == ECosmosUnitSystem::Game)
	{
		// AU/年 → cm/s
		return FVector(GameUnitVelocity) * (CosmosUnits::AuToCentimeter / CosmosUnits::YearToSecond);
	}
	return FVector(GameUnitVelocity);
}

double FNBodySimulator::ToWorldSeconds(double GameUnitYears) const
{
	return (UnitSystem == ECosmosUnitSystem::Game) ? GameUnitYears * CosmosUnits::YearToSecond : GameUnitYears;
}

double FNBodySimulator::ToWorldCentimeter(double GameUnitAu) const
{
	return (UnitSystem == ECosmosUnitSystem::Game) ? GameUnitAu * CosmosUnits::AuToCentimeter : GameUnitAu;
}

///////////////////////////////////////////////////////////////////////////////
// 轨道根数 → 初始状态

void FNBodySimulator::ApplyOrbitInit(int32 BodyIndex, const FCosmosOrbitInit& Orbit, double ReferenceMassSolar)
{
	if (!Bodies.IsValidIndex(BodyIndex))
	{
		return;
	}

	const double G = (UnitSystem == ECosmosUnitSystem::Game)
		? CosmosUnits::GravitationalConstant_Game
		: CosmosUnits::GravitationalConstant_SI;

	const double a = FMath::Max(Orbit.SemiMajorAxis, 1e-9);
	const double e = FMath::Clamp(Orbit.Eccentricity, 0.0, 0.99);
	const double M = ReferenceMassSolar;   // 中心天体质量（游戏单位即 M☉）

	// ── 1. 由偏近点角求真近点角 ──
	// 初始相位均匀分布时，直接用偏近点角当相位即可，
	// 这样「PhaseDegrees = 0」对应近日点，视觉上最直观。
	//
	// 注意：三角函数靠 <Math/Utils.h> 提供 double 重载，
	// 且角度转弧度用显式的 (PI/180) 而非 FMath::DegreesToRadians
	//（后者返回 float，会截断 double）。
	const double PhaseRad = Orbit.PhaseDegrees * (PI / 180.0);
	const double EccentricAnomaly = PhaseRad;

	// 真近点角
	const double TrueAnomaly = 2.0 * FMath::Atan2(
		FMath::Sqrt(1.0 + e) * FMath::Sin(EccentricAnomaly * 0.5),
		FMath::Sqrt(1.0 - e) * FMath::Cos(EccentricAnomaly * 0.5));

	// ── 2. 轨道平面内的位置与速度 ──
	// 近日点距离 r_p = a(1−e)，远日点 r_a = a(1+e)
	const double R = a * (1.0 - e * FMath::Cos(EccentricAnomaly));
	const double PX = R * FMath::Cos(TrueAnomaly);
	const double PY = R * FMath::Sin(TrueAnomaly);

	// 圆轨道速度 √(GM/r)；椭圆轨道需乘以 √(1+e)/(1+e·cosE)
	const double BaseSpeed = FMath::Sqrt(G * M / FMath::Max(R, 1e-9));
	const double SpeedFactor = (e < 1e-6)
		? 1.0
		: FMath::Sqrt((1.0 + e) / FMath::Max(1.0 + e * FMath::Cos(EccentricAnomaly), 1e-9));

	// 速度方向垂直于半径（在椭圆上具体是切向）
	const double VX = -BaseSpeed * SpeedFactor * FMath::Sin(TrueAnomaly);
	const double VY = BaseSpeed * SpeedFactor * FMath::Cos(TrueAnomaly);

	// ── 3. 应用轨道倾角与升交点经度 ──
	const double Inc = Orbit.InclinationDegrees * (PI / 180.0);
	const double Node = Orbit.AscendingNodeDegrees * (PI / 180.0);

	// 绕 X 轴倾倒 i，再绕 Z 轴旋转 Ω
	const auto RotateInPlane = [Inc, Node](const FVector3d& V)
	{
		const double Cy = FMath::Cos(Inc), Sy = FMath::Sin(Inc);
		const FVector3d Tilted(V.X, V.Y * Cy - V.Z * Sy, V.Y * Sy + V.Z * Cy);
		const double Cz = FMath::Cos(Node), Sz = FMath::Sin(Node);
		return FVector3d(
			Tilted.X * Cz - Tilted.Y * Sz,
			Tilted.X * Sz + Tilted.Y * Cz,
			Tilted.Z);
	};

	FCosmosBodyState& Body = Bodies[BodyIndex];
	Body.Position = RotateInPlane(FVector3d(PX, PY, 0.0));

	// 用户显式指定速度时优先用用户的
	if (Orbit.bHasVelocityOverride())
	{
		Body.Velocity = FVector3d(Orbit.OverrideVelocity);
	}
	else
	{
		Body.Velocity = RotateInPlane(FVector3d(VX, VY, 0.0));
	}
}
