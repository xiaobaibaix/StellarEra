// N 体引力求解器 —— 纯数学核心，不碰任何 UObject 逻辑。
//
// 只作用在 FCosmosBodyState 数组上；唯一的 UObject 是只被比较、不被解引用的
// TWeakObjectPtr<AActor>，因此可脱离 UE 写测试（跑百万步看能量漂移）。

#pragma once

#include "CoreMinimal.h"
#include "CosmosTypes.h"

class AActor;
class UPrimitiveComponent;

/** 单个天体的完整状态。位置与速度一律用 FVector3d（双精度）。 */
struct FCosmosBodyState
{
	/** 位置。游戏单位 AU 或 SI 单位厘米。 */
	FVector3d Position = FVector3d::ZeroVector;

	/** 速度。游戏单位 AU/年 或 SI 单位 厘米/秒。 */
	FVector3d Velocity = FVector3d::ZeroVector;

	/** 当前加速度。由求解器写入，不要手动设置。 */
	FVector3d Acceleration = FVector3d::ZeroVector;

	/**
	 * 纯引力加速度（AU/年²）。Acceleration 是「引力 + 外力 − 阻尼」
	 * 的合力用于积分，这一项单独存供星图画引力箭头，否则推力会搅混方向。
	 * 由 ComputeAccelerations 写入，不要手动设置。
	 */
	FVector3d GravitationalAcceleration = FVector3d::ZeroVector;

	/** 质量。游戏单位 M☉ 或 SI 单位 千克。 */
	double Mass = 0.0;

	/**
	 * 测试粒子：作为引力源质量按 0（不拽别人），自己仍照常被吸引。飞船用它避免把行星
	 * 从轨道上拽动；接触解算仍按真实质量分配让路。
	 */
	bool bTestParticle = false;

	/**
	 * 由外部物理体驱动：求解器只算它受的力，不积分位置/速度、不做接触。
	 * 读回由子系统的 Tick 负责。
	 */
	bool bPhysicsDriven = false;

	/** 是否接受求解器的力。false = 加速度恒为零，按当前速度匀速；作为引力源不受影响。 */
	bool bReceivesForces = true;

	/** 软化半径。等于 0 时使用求解器的全局设置。 */
	double SofteningOverride = 0.0;

	/** 外力加速度（AU/年²），不是引力；默认零 = 纯引力天体。飞船用它承载推力。 */
	FVector3d ExternalAcceleration = FVector3d::ZeroVector;

	/**
	 * 线性阻尼系数（1/年，因为求解器时间单位是模拟年）：a_damp = −k·v。默认 0 = 无阻尼。
	 * 必须逐子步计算：一帧最多 200 步，按帧首速度算常量阻力会把阻尼削掉一大截。
	 */
	double LinearDamping = 0.0;

	/** 刚体：为 true 时求解器积分姿态、接触冲量产生力矩；行星/恒星保持 false，朝向归关卡摆位。 */
	bool bRigidBody = false;

	/** 世界系朝向。求解器是唯一权威，子系统每帧写回 Actor；别处再写姿态就是互相抹。 */
	FQuat4d Orientation = FQuat4d::Identity;

	/** 世界系角速度（rad / 模拟年），与位置速度同一时间单位。 */
	FVector3d AngularVelocity = FVector3d::ZeroVector;

	/** 机体系惯性张量的逆（对角）。存逆不存 I：退化时天然为零 = 转不动，不必除零保护。 */
	FVector3d InverseInertiaLocal = FVector3d::ZeroVector;

	/** 外部力矩产生的角加速度（rad / 模拟年²）；外部每帧写，求解器只加不解释。 */
	FVector3d ExternalAngularAcceleration = FVector3d::ZeroVector;

	/** 角阻尼（1/模拟年），逐子步乘：ω ← ω·(1 − k·dt)。乘法保证 k·dt ≥ 1 时归零而不反号。 */
	double AngularDamping = 0.0;

	/** I⁻¹·τ：世界系力矩 → 世界系角速度增量；非刚体返回零。 */
	FVector3d ApplyInverseInertia(const FVector3d& Torque) const
	{
		if (!bRigidBody)
		{
			return FVector3d::ZeroVector;
		}

		// I⁻¹ 是机体系对角阵：转进机体 → 乘 → 转回世界。
		const FVector3d Local = Orientation.UnrotateVector(Torque) * InverseInertiaLocal;
		return Orientation.RotateVector(Local);
	}

	/** 长方体逆惯量，HalfExtent 是半边长；任一边长或质量非正返回零（转不动），不返回 NaN。 */
	static FVector3d InverseInertiaFromBox(double Mass, const FVector3d& HalfExtent);

	/** 均匀球：I = 2/5·m·r²。 */
	static FVector3d InverseInertiaFromSphere(double Mass, double Radius);

	/** 关联的 Actor。求解器不做任何 UObject 操作。 */
	TWeakObjectPtr<AActor> OwnerActor;

	/**
	 * 物理驱动时承担位移的根组件（代理天体的质量球）；飞船不填，力走 ICosmosBody。
	 * 弱引用，不阻止销毁。
	 */
	TWeakObjectPtr<UPrimitiveComponent> PhysicsBody;
};

/**
 * N 体引力模型：a_i = G · Σ_j≠i m_j·(r_j − r_i)/|r_j − r_i|³，复杂度 O(N²)。
 * **只负责受力**：位置/速度积分在 NBodyIntegration，运行时由子系统驱动。
 */
class STELLARERACOSMOS_API FNBodySimulator
{
public:
	FNBodySimulator();

	//////////////////////////////////////////////////////////////////////
	// 参数

	/**
	 * 1 AU 对应多少世界厘米，本项目最重要的标定值。真值 1.496e13 处 float32 的 ULP 达
	 * 18 公里，比星球还大、位置会抖；本项目按 1 AU = 10 公里（1e6）标定。
	 * 只影响 AU ↔ 世界厘米换算，不改变求解器内部（AU/M☉ 双精度，G = 4π²）与轨道形状。
	 */
	void SetCentimetersPerAu(double InCentimetersPerAu);
	double GetCentimetersPerAu() const { return CentimetersPerAu; }

	/**
	 * 计算所有天体的合力加速度（引力 + 外力 − 阻尼），写入各自 Acceleration。
	 * 本类只负责受力；位置/速度积分由 NBodyIntegration 与子系统负责。
	 */
	void ComputeAccelerations();

	/** 推进模拟时钟（年）。由积分器每步调用。 */
	void AdvanceSimulatedTime(double Dt) { SimulatedTime += Dt; }

	/**
	 * 软化长度，防止近距离引力趋于无穷；游戏单位下取轨道半长轴 1e-4 量级。
	 * 太大会削弱真实引力，太小则失去保护作用。
	 */
	void SetSofteningLength(double InSoftening);
	double GetSofteningLength() const { return SofteningLength; }

	/** 引力截断半径：距离超过此值的天体互不吸引（减少无效计算）。 */
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
	 * 清空天体，并复位模拟时间、时间余量与能量基线 —— 只清 Bodies 会让后两个读数失真。
	 */
	void Clear()
	{
		Bodies.Empty();
		Accelerations.SetNumZeroed(0);
		SimulatedTime = 0.0;
		MarkEnergyBaseline();
	}

	//////////////////////////////////////////////////////////////////////
	// 轨道求解

	/** 累计已模拟的天文时间（年）。 */
	double GetSimulatedTime() const { return SimulatedTime; }

	//////////////////////////////////////////////////////////////////////
	// 诊断 —— 用于验证积分器是否正确

	/** 系统总能量。辛积分器下应基本恒定。 */
	double GetTotalEnergy() const;

	/** 系统总动量。无外力时应恒定。 */
	FVector3d GetTotalMomentum() const;

	/** 系统总角动量（相对质心）。 */
	FVector3d GetTotalAngularMomentum() const;

	/** 相对初始状态的总能量漂移比例；百万步后应在 1e-6 量级，1e-2 以上说明积分器有问题。 */
	double GetEnergyDriftRatio() const;

	/** 记录当前能量作为基准，供GetEnergyDriftRatio 比较。 */
	void MarkEnergyBaseline();

	//////////////////////////////////////////////////////////////////////
	// 坐标转换

	/** 游戏单位位置（AU）转 UE 世界坐标（厘米）。 */
	FVector ToWorldLocation(const FVector3d& GameUnitPosition) const;

	/** UE 世界坐标（厘米）转游戏单位位置（AU）。 */
	FVector3d ToGameUnitPosition(const FVector& WorldLocation) const;

	//////////////////////////////////////////////////////////////////////
	// 预测 —— 只读，不改本对象

	/**
	 * 一对天体绕转周期 T = 2π√(r³/(G(m_i+m_j)))，r 取当前间距。项目里 G = 4π²，
	 * 化简成 √(r³/(m_i+m_j))，看着偏小的周期不是写错。
	 * G 由调用方传入以保持纯函数；间距或质量之和非正时返回 0。
	 */
	static double PairOrbitalPeriod(const FCosmosBodyState& A, const FCosmosBodyState& B, double G);

	/** 天体 i 参与的最短动力学时标 = min over j≠i 的 PairOrbitalPeriod。无解返回 0。 */
	double EstimateOrbitalPeriod(int32 BodyIndex) const;

private:
	/** 当前单位制下的引力常数。ComputeAccelerations / GetTotalEnergy / 预测三处共用。 */
	double GetG() const;

	/**
	 * 引力源质量：测试粒子返回 0。Mass 本身仍用于接触解算与惯性诊断。
	 */
	static double GravitationalSourceMass(const FCosmosBodyState& Body)
	{
		return Body.bTestParticle ? 0.0 : Body.Mass;
	}

	TArray<FCosmosBodyState> Bodies;

	/** 加速度缓存，避免每步重新分配。 */
	TArray<FVector3d> Accelerations;

	/** 1 AU 对应多少世界厘米。默认 1e6 = 1 AU 10 公里，理由见 SetCentimetersPerAu。 */
	double CentimetersPerAu = 1.0e6;

	double SofteningLength = 0.0001;
	double GravityCutoff = 0.0;   ///< 0 表示不截断

	double SimulatedTime = 0.0;
	double EnergyBaseline = 0.0;
	bool bEnergyBaselineValid = false;
};
