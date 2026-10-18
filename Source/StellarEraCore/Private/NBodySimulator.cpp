// N 体引力求解器 —— 实现。

#include "NBodySimulator.h"

// double 版三角函数（Sin / Cos / Atan2 / Sqrt）由 CoreMinimal.h 经
// HAL/PlatformMath.h → GenericPlatformMath.h 提供，FMath 已有 double 重载，
// 传 double 不会被截断成 float，因此这里不需要任何额外的数学头文件。

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
	return (UnitSystem == ECosmosUnitSystem::Game)
		? CosmosUnits::GravitationalConstant_Game
		: CosmosUnits::GravitationalConstant_SI;
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

		// 外力与阻尼都是**逐子步**求值的：阻尼那一项用的是本步此刻的 Velocity，
		// 而 StepVelocityVerlet 在步骤②重算加速度时速度还没被踢 —— 所以阻尼是
		// 显式欧拉，每步放大因子 1 − k·dt。稳定性要求 k·dt < 2。
		//
		// 判据只写公式，不代具体步长：dt 是逐关卡派生的（最短周期 / StepsPerOrbit，
		// 另有钳制与默认值兜底），换关卡、甚至同关卡改初始条件都会变。飞船的
		// k = 3.0e4 /年，本关卡 dt 在 1e-7 年量级，于是 k·dt 在 1e-3 量级 ——
		// 远小于上限 2，余量两到三个数量级。
		//
		// 引力单独存一份（GravitationalAcceleration），不和外力/阻尼混 —— 星图里
		// 的「引力合力箭头」读它，才能画出纯引力方向、不被推力搅混。
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

///////////////////////////////////////////////////////////////////////////////
// 接触

void FNBodySimulator::ComputeContactShares(double MassI, double MassJ,
                                           double& OutShareI, double& OutShareJ)
{
	// 「谁轻谁让路」：位移按对方质量占两者之和的比例分。
	//
	// 质量 ≤ 0 的是测试粒子：它让全部、对方一点都不让。**不能写成
	// m_j/(m_i+m_j)** —— 那样在 m_i = 0 时会让对方承担 100%（正好反了），
	// 两个都 ≤ 0 时还是 0/0。
	//
	// 本项目的飞船**不是**测试粒子（MassSolar = 1e-6）：它按质量比让路，
	// 但 1e-6 相对行星的 0.873237 等于让全部，行为上仍是「行星纹丝不动」。
	const bool bRealI = MassI > 0.0;
	const bool bRealJ = MassJ > 0.0;

	if (bRealI && bRealJ)
	{
		const double Total = MassI + MassJ;
		OutShareI = MassJ / Total;
		OutShareJ = MassI / Total;
	}
	else if (bRealI)
	{
		OutShareI = 0.0;
		OutShareJ = 1.0;
	}
	else if (bRealJ)
	{
		OutShareI = 1.0;
		OutShareJ = 0.0;
	}
	else
	{
		// 两个都是测试粒子：没有质量可以分配，对半分。
		OutShareI = 0.5;
		OutShareJ = 0.5;
	}
}

void FNBodySimulator::ResolveContacts()
{
	const int32 N = Bodies.Num();

	for (int32 i = 0; i < N; ++i)
	{
		for (int32 j = i + 1; j < N; ++j)
		{
			const double ContactDistance = Bodies[i].Radius + Bodies[j].Radius;
			if (ContactDistance <= 0.0)
			{
				// 两个都是点质量。本项目的恒星、以及所有没设半径的天体走这一支，
				// 所以接触项对它们是完全无操作的。
				continue;
			}

			const FVector3d Delta = Bodies[j].Position - Bodies[i].Position;
			const double DistanceSq = Delta.SizeSquared();
			if (DistanceSq >= ContactDistance * ContactDistance)
			{
				continue;
			}

			const double Distance = FMath::Sqrt(DistanceSq);

			// 完全重合时 Delta 没有方向可言，任取一个轴。这里只需要「有个方向」
			// 把它们分开 —— 下一次解算看到的就已经是真实的分离方向了。
			const FVector3d Normal = Distance > 1e-12
				? Delta / Distance
				: FVector3d(1.0, 0.0, 0.0);

			double ShareI = 0.0;
			double ShareJ = 0.0;
			ComputeContactShares(Bodies[i].Mass, Bodies[j].Mass, ShareI, ShareJ);

			// ① 位置：推开到刚好相切。**不留穿透余量** —— 余量会让天体每步往下
			//    陷一点点，长期看就是「地表在漏」。
			const double Overlap = ContactDistance - Distance;
			Bodies[i].Position -= Normal * (Overlap * ShareI);
			Bodies[j].Position += Normal * (Overlap * ShareJ);

			// ② 速度：默认只抹掉「相互接近」的法向分量，切向留着 —— 接触面是
			//    光滑的，天体沿地表滑。抹的是**相对**速度而不是各自的速度：后者会把
			//    整个系统的总动量吃掉，而总动量是这个求解器自己的诊断量之一。
			//
			//    若某一方带 bZeroVelocityOnContact（飞船），则走「撞上即停」的
			//    特例：把它整条速度设成对方的速度（相对速度归零，随行星一起动），
			//    不再保留切向滑行分量。
			const bool bZeroI = Bodies[i].bZeroVelocityOnContact;
			const bool bZeroJ = Bodies[j].bZeroVelocityOnContact;

			// 只有**一方**要求清零才走「撞上即停」特例。双方都要求清零是误配
			// （没有可参照的速度），退回默认的法向抹除 —— 实际项目里只有飞船
			// 会开这个开关，永远只有一方为 true，此分支不会误入。
			if (bZeroI != bZeroJ)
			{
				// 把带开关的那一方整条速度钉到对方上（相对速度归零，随行星一起动）。
				if (bZeroI)
				{
					Bodies[i].Velocity = Bodies[j].Velocity;
				}
				else
				{
					Bodies[j].Velocity = Bodies[i].Velocity;
				}
				continue;
			}

			const double Approach = FVector3d::DotProduct(
				Bodies[j].Velocity - Bodies[i].Velocity, Normal);

			if (Approach < 0.0)
			{
				Bodies[i].Velocity += Normal * (Approach * ShareI);
				Bodies[j].Velocity -= Normal * (Approach * ShareJ);
			}
		}
	}
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

	// 约束放在积分**之后**：这样三种积分器共用一处，Step 与 PredictTrajectory
	// 也就自动拿到同样的接触行为（预测跑的是同一个 StepExact 的副本）。
	//
	// 位置被钳过之后，缓存里的 Acceleration 对应的是钳制前的位置，下一步的
	// 第一个半步会用这个带一步滞后的值 —— 这是 Velocity Verlet 本来就有的性质，
	// 不是接触引入的。
	ResolveContacts();
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
	const double G = GetG();

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
		return FVector(GameUnitPosition) * CentimetersPerAu;
	}
	return FVector(GameUnitPosition);
}

FVector3d FNBodySimulator::ToGameUnitPosition(const FVector& WorldLocation) const
{
	if (UnitSystem == ECosmosUnitSystem::Game)
	{
		return FVector3d(WorldLocation) / CentimetersPerAu;
	}
	return FVector3d(WorldLocation);
}

// ── 以下三个换算目前无人调用 ──
// 本项目只用到位置换算（ToWorldLocation / ToGameUnitPosition）。速度、秒、距离
// 三个换算在参考项目里也是死代码。保留是为了让搬运保持完整、将来要加拖尾或
// 速度显示时直接用；改动此处时不要以为有调用方在依赖它。
//
// 用之前先看清它们是**实时**换算：1 AU/年 → 1e6 厘米 / 3.15576e7 秒 ≈ 0.032 厘米/秒，
// 而游戏里 1 模拟年只花 1/TimeScale 真实秒（当前 TimeScale ≈ 0.00333，约 300 秒），
// 同一个速度按游戏时钟算是 ≈ 3330 厘米/秒。要在屏幕上显示速度，得把这里的实时值再乘
// YearToSecond · TimeScale ≈ 1.05e5，而不是乘 1/TimeScale —— 后者是模拟时间的压缩比，
// 换算成「每真实秒」还要再过一道 YearToSecond。否则地球的 209 米/秒会显示成 0.2 厘米/秒。
FVector FNBodySimulator::ToWorldVelocity(const FVector3d& GameUnitVelocity) const
{
	if (UnitSystem == ECosmosUnitSystem::Game)
	{
		// AU/年 → cm/s
		return FVector(GameUnitVelocity) * (CentimetersPerAu / CosmosUnits::YearToSecond);
	}
	return FVector(GameUnitVelocity);
}

double FNBodySimulator::ToWorldSeconds(double GameUnitYears) const
{
	return (UnitSystem == ECosmosUnitSystem::Game) ? GameUnitYears * CosmosUnits::YearToSecond : GameUnitYears;
}

double FNBodySimulator::ToWorldCentimeter(double GameUnitAu) const
{
	return (UnitSystem == ECosmosUnitSystem::Game) ? GameUnitAu * CentimetersPerAu : GameUnitAu;
}

///////////////////////////////////////////////////////////////////////////////
// 预测 —— 只读

double FNBodySimulator::PairOrbitalPeriod(const FCosmosBodyState& A, const FCosmosBodyState& B, double G)
{
	const double Separation = (B.Position - A.Position).Size();
	const double Mu = G * (A.Mass + B.Mass);

	// 与 FinalizeRegistration 里原来那段判定完全一致：间距为零或总质量为零时
	// 这一对没有意义，返回 0 让调用方跳过。
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

	// 副本会把 ExternalAcceleration / LinearDamping 一起继承过去，于是星图上的
	// 预测线画成一条「一直加速」的曲线。这里清零 —— 预测的是**松手之后会去哪**
	// 的纯引力轨迹，那才是玩家要的信息。
	//
	// 想要「一直按着 W 会去哪」是另一个特性，应该显式做，不该靠静默继承。
	for (FCosmosBodyState& Body : Copy.Bodies)
	{
		Body.ExternalAcceleration = FVector3d::ZeroVector;
		Body.LinearDamping = 0.0;
	}

	// 步长取 Horizon / (NumSamples - 1)，于是末点**恰好**落在 Horizon 上，
	// 而不是 Horizon 差一步的位置。
	//
	// 局部量叫 StepSize 而不是 Step：后者会遮蔽成员函数 FNBodySimulator::Step，
	// 而 UE 5.8 把 C4458 当作硬错误。
	const double StepSize = Horizon / double(NumSamples - 1);

	OutPositions.Reserve(NumSamples);
	OutPositions.Add(Copy.GetBody(BodyIndex).Position);

	for (int32 SampleIndex = 1; SampleIndex < NumSamples; ++SampleIndex)
	{
		Copy.StepExact(StepSize);
		OutPositions.Add(Copy.GetBody(BodyIndex).Position);
	}

	return true;
}
