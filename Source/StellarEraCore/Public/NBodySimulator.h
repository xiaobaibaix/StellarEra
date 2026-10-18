// N 体引力求解器 —— 纯数学，不依赖任何 Actor。
//
// 之所以要与引擎彻底解耦，是为了能脱离 UE 写测试：
// 设几个初始条件，跑一百万步，看总能量漂移多少。
// 这是验证积分器写对没有的唯一可靠方法。

#pragma once

#include "CoreMinimal.h"
#include "CosmosTypes.h"

/**
 * 单个天体的完整状态。
 *
 * 位置与速度一律用 FVector3d（双精度）。原因见类注释中的精度说明。
 */
struct FCosmosBodyState
{
	/** 位置。游戏单位 AU 或 SI 单位厘米。 */
	FVector3d Position = FVector3d::ZeroVector;

	/** 速度。游戏单位 AU/年 或 SI 单位 厘米/秒。 */
	FVector3d Velocity = FVector3d::ZeroVector;

	/** 当前加速度。由求解器写入，不要手动设置。 */
	FVector3d Acceleration = FVector3d::ZeroVector;

	/** 质量。游戏单位 M☉ 或 SI 单位 千克。 */
	double Mass = 0.0;

	/**
	 * 半径。**不参与引力，只参与接触**：两个天体的中心距小于半径之和时，它们被
	 * 推开到刚好相切，相互接近的法向速度被抹掉（见 ResolveContacts）。
	 *
	 * 为 0 表示点质量，永不接触 —— 没设半径的天体都是这样（AddBodyToSimulator
	 * 只给 AVoxelPlanetActor 和飞船写半径）。星球取当前朝向下的地形地表高度
	 * （RefreshPlanetContactRadii），飞船取碰撞球半径 300 厘米
	 * （ASpaceshipPawn::GetContactRadiusCm）。飞船的半径**不是 0**，否则它的
	 * **中心点**会被钳在地表上、机身下半截埋进地形里。
	 */
	double Radius = 0.0;

	/** 软化半径。等于 0 时使用求解器的全局设置。 */
	double SofteningOverride = 0.0;

	/**
	 * 外力加速度（AU/年²）。**不是引力** —— 由外部每帧写入，求解器只加不解释。
	 *
	 * 默认零 = 纯引力天体，现有行为一字不变（`x + 0.0` 在 IEEE 下是精确运算）。
	 * 本项目的飞船用它承载推力，见 UCosmosSimulationSubsystem::ApplyControlledBodyForces。
	 */
	FVector3d ExternalAcceleration = FVector3d::ZeroVector;

	/**
	 * 线性阻尼系数（1/年）：a_damp = −k·v。默认 0 = 无阻尼。
	 *
	 * 单位是 1/年 而不是 1/秒，因为求解器的时间单位是模拟年。
	 *
	 * 必须在求解器里**逐子步**算，不能在外部按帧算好一个常量加速度：一帧最多跑
	 * MaxStepsPerFrame = 200 个子步、约 7.5 毫秒世界时间，而飞船的阻尼时间常数是
	 * 10 毫秒 —— 一帧之内速度要变 2 倍以上，按帧首速度算常量阻力等于把阻尼削掉一大截。
	 */
	double LinearDamping = 0.0;

	/** 关联的 Actor。求解器不做任何 UObject 操作。 */
	TWeakObjectPtr<AActor> OwnerActor;
};

/**
 * N 体引力求解器。
 *
 * 实现牛顿万有引力：
 *     a_i = G · Σ_j≠i  m_j · (r_j − r_i) / |r_j − r_i|³
 *
 * 复杂度 O(N²)。太阳系这种量级（N ≤ 50）完全无压力，
 * 真正的瓶颈是数值稳定性而非算力。
 *
 * ── 为什么默认积分器必须是辛的 ───────────────────────────────
 * 显式欧拉法是一阶方法，每步误差同向累积。后果是轨道能量单调流失，
 * 行星越掉越快，最终螺旋坠入恒星。典型表现是跑一小时就崩。
 *
 * Velocity Verlet 保持相空间体积，误差有界不累积，
 * 轨道可以稳定运行数百万年。代价仅是每步多算一次加速度。
 */
class STELLARERACORE_API FNBodySimulator
{
public:
	FNBodySimulator();

	//////////////////////////////////////////////////////////////////////
	// 参数

	/** 单位系统。修改会重算引力常数。 */
	void SetUnitSystem(ECosmosUnitSystem Unit);
	ECosmosUnitSystem GetUnitSystem() const { return UnitSystem; }

	/**
	 * 1 AU 对应多少世界厘米。**本项目最重要的一个标定值。**
	 *
	 * 真实值是 1.496e13，但那样 float32 在该处的 ULP 会到 18 公里 —— 比本项目的
	 * 星球（半径 1 公里）还大 18 倍，位置会剧烈抖动，星球直接看不出在哪。
	 * 所以本项目的世界按「1 AU = 10 公里」标定：1e6 厘米处 ULP 是 0.12 厘米，
	 * 远小于 100 厘米的体素边长，体素渲染不会抖。
	 *
	 * 只影响 AU ↔ 世界厘米的换算，**不影响求解器内部**：位置、速度、质量全程是
	 * AU / AU每年 / M☉ 的双精度，G 仍然是 4π²。改这个值不改变轨道形状，
	 * 只改变「轨道画在世界里多大」。
	 */
	void SetCentimetersPerAu(double InCentimetersPerAu);
	double GetCentimetersPerAu() const { return CentimetersPerAu; }

	/** 积分器类型。 */
	void SetIntegrator(ECosmosIntegrator InIntegrator) { Integrator = InIntegrator; }
	ECosmosIntegrator GetIntegrator() const { return Integrator; }

	/**
	 * 软化长度。防止两体近距离时引力趋向无穷大导致数值溢出。
	 * 等价于给每个天体加一个很小的额外半径。
	 *
	 * 取值建议：游戏单位下取轨道半长轴的 1e-4 量级。
	 * 太大会削弱真实引力，太小则失去保护作用。
	 */
	void SetSofteningLength(double InSoftening);
	double GetSofteningLength() const { return SofteningLength; }

	/**
	 * 超出最大轨道的引力截断半径。
	 * 距离超过此值的天体被视为不受彼此吸引。
	 * 这是广袤宇宙中常用的近似，能显著减少无效计算。
	 */
	void SetGravityCutoff(double InCutoff) { GravityCutoff = InCutoff; }
	double GetGravityCutoff() const { return GravityCutoff; }

	//////////////////////////////////////////////////////////////////////
	// 天体管理

	int32 GetNumBodies() const { return Bodies.Num(); }

	const TArray<FCosmosBodyState>& GetBodies() const { return Bodies; }

	/** 添加天体，返回其索引。 */
	int32 AddBody(const FCosmosBodyState& Body);

	/** 按索引取天体引用。可能因数组重排而失效，取完立即用。 */
	FCosmosBodyState& GetBodyMutable(int32 Index) { return Bodies[Index]; }
	const FCosmosBodyState& GetBody(int32 Index) const { return Bodies[Index]; }

	/** 按 Actor 反查索引，找不到返回 INDEX_NONE。 */
	int32 FindBodyByActor(const AActor* Actor) const;

	/**
	 * 清空所有天体，并把模拟时间、时间余量与能量基线一并复位。
	 *
	 * 三者都要清：只清 Bodies 的话，GetSimulatedTime() 会带着旧值继续累加，
	 * GetEnergyDriftRatio() 会拿清空前的基线去比，两个读数都会失去意义。
	 */
	void Clear()
	{
		Bodies.Empty();
		Accelerations.SetNumZeroed(0);
		Accumulator = 0.0;
		SimulatedTime = 0.0;
		MarkEnergyBaseline();
	}

	//////////////////////////////////////////////////////////////////////
	// 轨道求解

	/**
	 * 推进一个时间步。
	 *
	 * **本函数是变步长积分器，会破坏辛性质。**
	 * 实现是 Accumulator += Dt; StepExact(Accumulator); Accumulator = 0.0;——
	 * 累加器加完立刻被清零，所以它实际上等价于 StepExact(Dt)，
	 * GetPendingTime() 恒为 0。每帧传入的 Dt 一旦不同，就等同于变步长积分，
	 * Velocity Verlet「误差有界不累积」的前提不再成立。
	 *
	 * 需要真正的辛积分（误差有界、轨道可长期稳定）时，请**直接调 StepExact**
	 * 并自行保证步长固定；本函数只适合不在乎长期精度的场合。
	 */
	void Step(float Dt);

	/** 强制推进恰好 TimeStep 的一步（不管累加器）。 */
	void StepExact(double TimeStep);

	/** 已挂起的不足一步的时间余量。 */
	double GetPendingTime() const { return Accumulator; }

	/** 重置时间余量。 */
	void ResetAccumulator() { Accumulator = 0.0; }

	/** 累计已模拟的天文时间，单位与 UnitSystem 一致。 */
	double GetSimulatedTime() const { return SimulatedTime; }

	//////////////////////////////////////////////////////////////////////
	// 诊断 —— 用于验证积分器是否正确

	/** 系统总能量。辛积分器下应基本恒定。 */
	double GetTotalEnergy() const;

	/** 系统总动量。无外力时应恒定。 */
	FVector3d GetTotalMomentum() const;

	/** 系统总角动量（相对质心）。 */
	FVector3d GetTotalAngularMomentum() const;

	/**
	 * 相对初始状态的总能量漂移比例。
	 * 跑一百万步后应该在 1e-6 量级；若达到 1e-2 以上说明积分器有问题。
	 */
	double GetEnergyDriftRatio() const;

	/** 记录当前能量作为基准，供GetEnergyDriftRatio 比较。 */
	void MarkEnergyBaseline();

	//////////////////////////////////////////////////////////////////////
	// 坐标转换

	/** 游戏单位位置（AU）转 UE 世界坐标（厘米）。 */
	FVector ToWorldLocation(const FVector3d& GameUnitPosition) const;

	/** UE 世界坐标（厘米）转游戏单位位置（AU）。 */
	FVector3d ToGameUnitPosition(const FVector& WorldLocation) const;

	/** 游戏单位速度（AU/年）转 UE 世界速度（厘米/秒）。 */
	FVector ToWorldVelocity(const FVector3d& GameUnitVelocity) const;

	/** 时间：游戏单位年转 UE 世界秒。 */
	double ToWorldSeconds(double GameUnitYears) const;

	/** 距离：游戏单位 AU 转 UE 世界厘米。 */
	double ToWorldCentimeter(double GameUnitAu) const;

	//////////////////////////////////////////////////////////////////////
	// 预测 —— 只读，不改本对象

	/**
	 * 一对天体相互绕转的周期：T = 2π√( r³ / (G·(m_i + m_j)) )，r 取当前间距。
	 *
	 * 注意项目里 G = 4π²，所以 2π 与 G 抵消，游戏单位下这个式子化简成
	 * √(r³/(m_i+m_j))。看着「偏小」的周期（0.718 AU 绕 1 M☉ → 0.608 年）
	 * 就是这么来的，不是写错。
	 *
	 * G 由调用方传入而不是从 UnitSystem 推：这样它是纯函数，能脱离求解器
	 * 单独测试。间距或质量之和非正时返回 0，表示「这一对没有有限的绕转周期」。
	 */
	static double PairOrbitalPeriod(const FCosmosBodyState& A, const FCosmosBodyState& B, double G);

	/** 天体 i 参与的最短动力学时标 = min over j≠i 的 PairOrbitalPeriod。无解返回 0。 */
	double EstimateOrbitalPeriod(int32 BodyIndex) const;

	/**
	 * 预测天体 i 未来 Horizon 年的轨迹，输出 AU 坐标序列。
	 *
	 * 含首尾共 NumSamples 个点：首点 = 当前位置，末点 = 恰好 Horizon 处。
	 * **不修改本对象** —— 内部复制一份求解器再推进副本。之所以这样是安全的：
	 * 积分器的全部状态就是每颗星的 (Position, Velocity)，复制求解器即复制状态。
	 *
	 * 返回 false（并清空 OutPositions）当：索引无效、Horizon ≤ 0、NumSamples < 2。
	 */
	bool PredictTrajectory(int32 BodyIndex, double Horizon, int32 NumSamples,
	                       TArray<FVector3d>& OutPositions) const;

private:
	/** 当前单位制下的引力常数。ComputeAccelerations / GetTotalEnergy / 预测三处共用。 */
	double GetG() const;

	/** 计算所有天体的加速度，写入各自Acceleration。 */
	void ComputeAccelerations();

	/**
	 * 解算所有天体对的接触：重叠的推开到相切，相互接近的法向速度抹掉。
	 *
	 * 在 StepExact 的积分之后调用，所以三种积分器、以及 Step 与 PredictTrajectory
	 * 走的是同一套约束（预测跑的是同一个 StepExact 的副本）。
	 *
	 * 半径全为 0 的天体对永不接触 —— 现有的能量漂移测试造的都是点质量，
	 * 因此不受这一项影响。
	 */
	void ResolveContacts();

	/**
	 * 接触时两个天体各让多少位移，按「谁轻谁让路」分配，两者之和恒为 1。
	 *
	 * 质量 ≤ 0 视为测试粒子：它让全部，对方一点都不让。取质量倒数会除零，
	 * 所以这里必须显式分情形，不能写成 m_j/(m_i+m_j)。
	 *
	 * 本项目的飞船**不是**测试粒子（MassSolar = 1e-6）：它按质量比让路，
	 * 但 1e-6 相对行星的 0.873237 等于让全部，行为上仍是「行星纹丝不动」。
	 */
	static void ComputeContactShares(double MassI, double MassJ,
	                                 double& OutShareI, double& OutShareJ);

	/** 用给定加速度做一次 Velocity Verlet 半步推进。 */
	void StepVelocityVerlet(double Dt);

	/** 显式欧拉法，仅用于对比测试。 */
	void StepEuler(double Dt);

	TArray<FCosmosBodyState> Bodies;

	/** 加速度缓存，避免每步重新分配。 */
	TArray<FVector3d> Accelerations;

	ECosmosUnitSystem UnitSystem = ECosmosUnitSystem::Game;
	ECosmosIntegrator Integrator = ECosmosIntegrator::VelocityVerlet;

	/** 1 AU 对应多少世界厘米。默认 1e6 = 1 AU 10 公里，理由见 SetCentimetersPerAu。 */
	double CentimetersPerAu = 1.0e6;

	double SofteningLength = 0.0001;
	double GravityCutoff = 0.0;   ///< 0 表示不截断
	double Accumulator = 0.0;
	double SimulatedTime = 0.0;
	double EnergyBaseline = 0.0;
	bool bEnergyBaselineValid = false;
};
