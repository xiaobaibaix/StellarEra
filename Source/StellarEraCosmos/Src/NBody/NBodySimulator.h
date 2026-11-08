// N 体引力求解器 —— 纯数学核心，不碰任何 UObject 逻辑。
//
// 只作用在 FCosmosBodyState 数组上；唯一的 UObject 是只被比较、不被解引用的
// TWeakObjectPtr<AActor>，因此可脱离 UE 写测试（跑百万步看能量漂移）。

#pragma once

#include "CoreMinimal.h"
#include "CosmosTypes.h"

class AActor;

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
	 * 纯引力加速度（AU/年² 或 SI，随 UnitSystem）。Acceleration 是「引力 + 外力 − 阻尼」
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
	 * 半径。不参与引力只参与接触：中心距小于半径之和时推开到相切并抹掉接近法向速度。
	 * 0 = 点质量永不接触。飞船取兜底 300 cm 而不是 0，否则中心点会被钳在地表。
	 * 带 ContactProbes 的天体不用这一项，Radius 应同时置 0，否则球体会抢先触发。
	 */
	double Radius = 0.0;

	/**
	 * 接触探针（**机体系**偏移，求解器单位）。非空时取代 Radius 的球-球接触。
	 * 求解器每个子步按 Orientation 转成世界系 —— 一帧几十个子步、机身还在转，
	 * 存世界系会整帧用上一刻的船身找接触点，落地变成越推越摆的自激振荡。
	 * 空数组 = 按 Radius 做球-球接触。
	 */
	TArray<FVector3d> ContactProbes;

	/** 最远探针到中心的距离。由 SetContactProbes 维护，用于接触前的快速排除。 */
	double ContactProbeRadius = 0.0;

	/**
	 * 逐探针真实地表半径（AU），与 ContactProbes 一一对应；空/数量对不上 = 退回球面接触。
	 * 子系统每帧写一次，求解器只读 —— 一帧最多 200 个子步，体素查询不能放这里。
	 */
	TArray<double> ContactProbeSurfaceRadii;

	/** 上面的半径是对着哪个天体算的。索引对不上就不用逐探针半径（接触是成对的）。 */
	int32 ContactSurfaceSphereIndex = INDEX_NONE;

	/** 写入**机体系**探针并同步 ContactProbeRadius。传空数组即退回球-球接触。 */
	void SetContactProbes(TArray<FVector3d> InProbes)
	{
		ContactProbes = MoveTemp(InProbes);
		ContactProbeRadius = 0.0;
		for (const FVector3d& Probe : ContactProbes)
		{
			ContactProbeRadius = FMath::Max(ContactProbeRadius, Probe.Size());
		}

		// 探针换了，逐探针地表半径一并清掉，让接触退回球面而不是拿旧半径去碰。
		ContactProbeSurfaceRadii.Reset();
		ContactSurfaceSphereIndex = INDEX_NONE;
	}

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

	/** 库仑摩擦系数 μ，切向冲量夹在 ±μ·λ_n；0.6 ≈ 落地不无限滑走，也不像刹车片。 */
	double ContactFriction = 0.6;

	/** 恢复系数 e，默认 0 = 撞上不弹；留着给以后的弹性质点。 */
	double ContactRestitution = 0.0;

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

	/**
	 * 接触时把整条速度设成对方速度（撞地即停），而不是只抹法向分量。只对球-球分支生效：
	 * 带探针的飞船走冲量 + 摩擦，不再置位；行星之间仍保持光滑硬球语义。
	 */
	bool bZeroVelocityOnContact = false;

	/** 关联的 Actor。求解器不做任何 UObject 操作。 */
	TWeakObjectPtr<AActor> OwnerActor;
};

/**
 * 闭合轨道预测结果（相对主导天体坐标）。世界系里主导天体自己在动、绝对路径是摆线，
 * 只有相对坐标才闭合；还原绝对坐标 = RelativePositions + PrimaryPosition。
 */
struct FCosmosClosedOrbitPrediction
{
	/** 相对主导天体的位置（AU）。成功时首尾相接：Last() 与 [0] 逐位相等。 */
	TArray<FVector3d> RelativePositions;

	/** 主导天体在 t=0 的位置（AU）。 */
	FVector3d PrimaryPosition = FVector3d::ZeroVector;

	/** 实测闭合时间（年），不是密切周期 —— 多体扰动下两者可差数倍。 */
	double PeriodYears = 0.0;
};

/**
 * N 体引力求解器：a_i = G · Σ_j≠i m_j·(r_j − r_i)/|r_j − r_i|³，复杂度 O(N²)。
 * 默认 Velocity Verlet 是辛积分器，保持相空间体积、误差有界；显式欧拉误差同向累积，
 * 轨道会螺旋坠入恒星（仅用于对比测试）。
 */
class STELLARERACOSMOS_API FNBodySimulator
{
public:
	FNBodySimulator();

	//////////////////////////////////////////////////////////////////////
	// 参数

	/** 单位系统。修改会重算引力常数。 */
	void SetUnitSystem(ECosmosUnitSystem Unit);
	ECosmosUnitSystem GetUnitSystem() const { return UnitSystem; }

	/**
	 * 1 AU 对应多少世界厘米，本项目最重要的标定值。真值 1.496e13 处 float32 的 ULP 达
	 * 18 公里，比星球还大、位置会抖；本项目按 1 AU = 10 公里（1e6）标定。
	 * 只影响 AU ↔ 世界厘米换算，不改变求解器内部（AU/M☉ 双精度，G = 4π²）与轨道形状。
	 */
	void SetCentimetersPerAu(double InCentimetersPerAu);
	double GetCentimetersPerAu() const { return CentimetersPerAu; }

	/** 积分器类型。 */
	void SetIntegrator(ECosmosIntegrator InIntegrator) { Integrator = InIntegrator; }
	ECosmosIntegrator GetIntegrator() const { return Integrator; }

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
		Accumulator = 0.0;
		SimulatedTime = 0.0;
		MarkEnergyBaseline();
	}

	//////////////////////////////////////////////////////////////////////
	// 轨道求解

	/**
	 * 推进一个时间步。实现是 Accumulator += Dt 后立刻清零，等价于 StepExact(Dt) ——
	 * 每帧 Dt 不同即变步长，破坏辛性质。需要长期精度请直接调 StepExact 并保证步长固定。
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

	/** 相对初始状态的总能量漂移比例；百万步后应在 1e-6 量级，1e-2 以上说明积分器有问题。 */
	double GetEnergyDriftRatio() const;

	/** 记录当前能量作为基准，供GetEnergyDriftRatio 比较。 */
	void MarkEnergyBaseline();

	//////////////////////////////////////////////////////////////////////
	// 坐标转换

	/**
	 * 渲染原点：世界坐标不再等于「求解器绝对位置 × 厘米」。行星 15 km/s，相机绑在行星上
	 * 每帧平移 1.8 km，Lumen 表面缓存与 VSM page table 这类按世界坐标取键的缓存帧帧换
	 * key，阴影边于是逐帧在两种量化态之间翻。设了原点后世界静止，物理仍是绝对量。
	 * InOriginGameUnits 是求解器单位的当前锚点位置，InOriginWorld 是它对应的世界位置。
	 * 默认两者皆零 = 旧行为（绝对坐标）。
	 */
	void SetRenderOrigin(const FVector3d& InOriginGameUnits, const FVector& InOriginWorld);

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
	 * 一对天体绕转周期 T = 2π√(r³/(G(m_i+m_j)))，r 取当前间距。项目里 G = 4π²，
	 * 化简成 √(r³/(m_i+m_j))，看着偏小的周期不是写错。
	 * G 由调用方传入以保持纯函数；间距或质量之和非正时返回 0。
	 */
	static double PairOrbitalPeriod(const FCosmosBodyState& A, const FCosmosBodyState& B, double G);

	/** 天体 i 参与的最短动力学时标 = min over j≠i 的 PairOrbitalPeriod。无解返回 0。 */
	double EstimateOrbitalPeriod(int32 BodyIndex) const;

	/**
	 * 预测天体未来 Horizon 年的轨迹（AU），含首尾共 NumSamples 点，末点恰好落在 Horizon。
	 * 内部复制求解器再推进，不改本对象。索引无效 / Horizon ≤ 0 / NumSamples < 2 时返回 false。
	 */
	bool PredictTrajectory(int32 BodyIndex, double Horizon, int32 NumSamples,
	                       TArray<FVector3d>& OutPositions) const;

	/**
	 * 预测天体相对主导天体（引力 M/r² 最大且更重的那颗）的闭合轨道（一整圈）。
	 * 密切周期只用来定步长，最多积 3 个周期，取扫满 2π 后离起点最近的点为闭合时刻 ——
	 * 多体扰动下实测一圈可与密切周期差数倍，按密切周期截断会把闭环画成半截。
	 * 非椭圆 / e ≥ 0.99 / 找不到主导天体 / 回不到起点附近时返回 false；成功时首尾逐位相等。
	 */
	bool PredictClosedOrbit(int32 BodyIndex, int32 NumSamplesPerOrbit,
	                        FCosmosClosedOrbitPrediction& Out) const;

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

	/** 计算所有天体的加速度，写入各自Acceleration。 */
	void ComputeAccelerations();

	/**
	 * 解算所有天体对的接触：重叠推开到相切，抹掉接近的法向速度。在积分之后调用，
	 * 三种积分器与预测路径共用一套约束；半径全为 0 的天体对永不接触。
	 */
	void ResolveContacts();

	/**
	 * 探针接触：收集穿透的探针（最多 MaxContactPointsPerPair 个，留最深的），逐点顺序冲量
	 * 求解（法向 + 累计库仑摩擦），再沿穿透量加权的平均法线推出最深那份穿透。
	 */
	void ResolveProbeContact(int32 ProbeIndex, int32 SphereIndex);

	/**
	 * 刚体姿态积分：ω += α·dt，ω 乘角阻尼，q ← exp(½·ω·dt)⊗q。
	 * 放 StepExact 里积分器之后、ResolveContacts 之前，预测轨迹自动拿到同样的姿态行为。
	 */
	void IntegrateRigidBodyOrientations(double TimeStep);

	/**
	 * 接触时两者各让多少位移，按质量分配、两者之和恒为 1。质量 ≤ 0 视为测试粒子：它让
	 * 全部、对方一点都不让 —— 取倒数会除零，必须显式分情形。
	 * 飞船虽是测试粒子，这里仍按真实质量比让路（相对行星等于让全部）。
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

	FVector3d RenderOriginGameUnits = FVector3d::ZeroVector;
	FVector RenderOriginWorld = FVector::ZeroVector;

	double Accumulator = 0.0;
	double SimulatedTime = 0.0;
	double EnergyBaseline = 0.0;
	bool bEnergyBaselineValid = false;
};
