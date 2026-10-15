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

	/** 半径（用于计算碰撞与视觉大小，不参与引力）。 */
	double Radius = 0.0;

	/** 软化半径。等于 0 时使用求解器的全局设置。 */
	double SofteningOverride = 0.0;

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
 * ── 为什么必须用辛积分器 ───────────────────────────────
 * 显式欧拉法是一阶方法，每步误差同向累积。后果是轨道能量单调流失，
 * 行星越掉越快，最终螺旋坠入恒星。典型表现是跑一小时就崩。
 *
 * Velocity Verlet 保持相空间体积，误差有界不累积，
 * 轨道可以稳定运行数百万年。代价仅是每步多算一次加速度。
 */
class FNBodySimulator
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

	/** 清空所有天体。 */
	void Clear() { Bodies.Empty(); Accelerations.SetNumZeroed(0); }

	//////////////////////////////////////////////////////////////////////
	// 轨道求解

	/**
	 * 推进一个时间步。
	 * 内部自动处理累加器，跳过不足一步的余量。
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
	// 由轨道根数推算初始状态

	/**
	 * 根据轨道参数计算初始位置与速度，并写入某个天体。
	 *
	 * 这是填入真实天文数据最省事的方式：只给半长轴和偏心率，
	 * 位置速度由开普勒定律自动推导，保证天体落在正确轨道上。
	 */
	void ApplyOrbitInit(int32 BodyIndex, const FCosmosOrbitInit& Orbit, double ReferenceMassSolar = 1.0);

private:
	/** 计算所有天体的加速度，写入各自Acceleration。 */
	void ComputeAccelerations();

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
