// Copyright Voxel Plugin SAS. All Rights Reserved.
// N 体引力系统的唯一驱动。

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "NBodySimulator.h"
#include "CosmosSimulationSubsystem.generated.h"

class UCosmosOrbitComponent;

/**
 * N 体引力系统的驱动。
 *
 * 为什么是 WorldSubsystem 而不是 GameMode：
 *   - 一个关卡只能有一个 GameMode，本项目已有的 AStellarEraGameMode 负责开局登船，
 *     不该被 N 体挤掉；
 *   - N 体是耦合系统（A 受 B 的力，B 也受 A 的力），必须有唯一的编排者。
 *     WorldSubsystem 的单例由引擎保证，不需要在关卡里摆一个驱动 Actor。
 *
 * 每帧顺序（全在一次 Tick 内完成，避免天体之间差一帧）：
 *   0. 刷新行星接触半径 + 把受控天体（飞船）的推力/阻尼喂给求解器
 *   1. 推进求解器若干步
 *   2. 把求解器位置与速度写回天体、并把位移带给骑在上面的 Pawn
 *   3. 诊断
 */
UCLASS(Config = Game)
class STELLARERACOSMOS_API UCosmosSimulationSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	//~ Begin USubsystem Interface
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	//~ End USubsystem Interface

	//~ Begin FTickableGameObject Interface
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	//~ End FTickableGameObject Interface

	/** 由 UCosmosOrbitComponent::BeginPlay 调用。 */
	void RegisterBody(UCosmosOrbitComponent* Component);

	/** 由 UCosmosOrbitComponent::EndPlay 调用。 */
	void UnregisterBody(UCosmosOrbitComponent* Component);

	/** 把当前诊断打到日志。每 DiagnosticInterval 秒自动调一次。 */
	void LogDiagnostics() const;

	//////////////////////////////////////////////////////////////////////////
	// 只读查询 —— 供轨迹渲染层用，渲染层因此不必碰求解器

	/**
	 * 预测某个天体未来 HorizonYears 年的轨迹，输出**世界坐标（厘米）**。
	 * AU → 厘米 的换算在这里做。
	 *
	 * 返回 false（并清空 OutWorldPositions）当：索引无效、窗口非正、采样点少于 2。
	 */
	bool PredictTrajectoryWorld(int32 SimulatorIndex, double HorizonYears, int32 NumSamples,
	                            TArray<FVector>& OutWorldPositions) const;

	/**
	 * 预测天体相对主导天体的**闭合轨道**，输出世界坐标（厘米）。
	 *
	 * 与 PredictTrajectoryWorld 的区别：那个是绝对世界坐标里的有限一段；这个在
	 * 主导天体参考系里积到相对位置转满一圈、回到起点附近，首尾相接（输出末点与
	 * 首点相同），用于把理论上可闭合的轨道画成闭环。若密切轨道非椭圆、或转满一圈
	 * 仍回不到起点附近，返回 false 并清空输出 —— 调用方应退回 PredictTrajectoryWorld
	 * 的有限窗口。
	 */
	bool PredictClosedOrbitWorld(int32 SimulatorIndex, int32 NumSamplesPerOrbit,
	                             TArray<FVector>& OutWorldPositions) const;

	/** 已模拟的天文时间（年）。拖尾按模拟时间采样，不按帧 —— 否则覆盖时长会随帧率漂移。 */
	double GetSimulatedTimeYears() const;

	/** 天体 i 参与的最短动力学时标（年）。算不出（单天体 / 退化配对）返回 0。 */
	double GetOrbitalPeriodYears(int32 SimulatorIndex) const;

	/**
	 * 全系统最短的那条配对周期（年），即所有 (i, j) 配对里最短的一个。
	 * FinalizeRegistration 还没跑、或一对都算不出时返回 0。
	 *
	 * 与 GetOrbitalPeriodYears(i) 的区别：那个是「天体 i 参与的」最短，逐天体不同；
	 * 这个是全系统的下界，**只有一个值**。它是自动积分步长的分母
	 * （BaseTimeStep = 该值 / StepsPerOrbit）。
	 *
	 * 预测窗口**不**读它：窗口上限按每个天体自身的周期算
	 * （见 UCosmosTrajectoryComponent::MaxAutoPredictionPeriods），否则一艘快
	 * 飞船会把全系统所有天体的窗口一起压短。
	 */
	double GetShortestPeriodYears() const { return ShortestPeriodYears; }

	/**
	 * 天体 i **当前**的游戏单位速度（AU/年），即求解器里那一刻的值。
	 *
	 * 与 `UCosmosOrbitComponent::InitialVelocityKmPerSecond` 的区别：后者是配置的
	 * **开局**速度（单位 km/s），运行期不变；这个返回的是求解器单位 AU/年、
	 * 每帧都在变的状态。速度箭头读的是这个，不是那个 ——
	 * 所以箭头才会跟着天体走、随实际速度变长变短。
	 *
	 * 下标无效（含 INDEX_NONE —— 轨迹组件注册还没轮到的那几帧）、或求解器里还没有
	 * 这颗天体时返回零向量。**下标校验必须在这里做**：求解器的 `GetBody` 内部是裸的
	 * `Bodies[Index]`，越界不报错、直接是 UB。
	 *
	 * 单位是游戏单位而不是世界单位：箭头长度按「速度大小 × VelocityArrowScaleCm」
	 * 画，而那个系数的定义就是「每 1 AU/年 画多少厘米」，所以这里不做换算。
	 */
	FVector3d GetBodyVelocityGameUnits(int32 SimulatorIndex) const;

	/**
	 * 天体 i **当前**的纯引力加速度（游戏单位 AU/年²），即求解器里那一刻的值。
	 *
	 * 只含万有引力（`G·Σ m_j·Δ/r³`），**不含**飞船的推力与阻尼 —— 见
	 * FCosmosBodyState::GravitationalAcceleration 的注释。星图里的「引力合力箭头」
	 * 读它：方向就是天体此刻被拉向哪，长度正比于引力大小。
	 *
	 * 下标无效（含 INDEX_NONE）、或求解器里还没有这颗天体时返回零向量。
	 * 下标校验在这里做，理由同 GetBodyVelocityGameUnits。
	 *
	 * 单位是游戏单位（AU/年²）而不是世界单位：箭头长度按「大小 ×
	 * GravityArrowScaleCm」画，系数定义与速度箭头同族（示意比例，非物理世界长度）。
	 */
	FVector3d GetBodyGravityGameUnits(int32 SimulatorIndex) const;

	//////////////////////////////////////////////////////////////////////////
	// 星图模式
	//
	// 选中状态放这里而不是放星图 Pawn 上：本子系统已经是天体的全局注册表
	// （Bodies），而且是**唯一**知道全部天体的地方。轨迹组件每帧单向读它决定
	// 高亮，没有回写、没有生命周期问题。

	/** 选中一颗天体。传 nullptr 取消选中。存弱引用，天体被销毁后自动失效。 */
	void SetSelectedBody(AActor* Body);

	/** 当前选中的天体。未选中、或它已被销毁时返回 nullptr。 */
	AActor* GetSelectedBody() const;

	/**
	 * 把所有已注册天体的 Actor 写进 Out（先 Reset）。
	 *
	 * 跳过两种：组件本身失效的，和拿不到 Owner 的（组件的 Outer 不是 Actor）。
	 * 拾取遍历用它，所以**顺序必须是确定的** —— 拾取在等距时取先出现的。
	 * 实际顺序是 `Bodies` 的当前顺序：`FinalizeRegistration`（第一次 `Tick` 时跑）
	 * 会按质量降序排一次，质量大的在前；此后注册的追加到末尾。
	 * **不是注册顺序**，但确定。
	 */
	void GetRegisteredBodies(TArray<AActor*>& Out) const;

	/** 星图模式是否激活。轨迹组件的显示门控读它。 */
	bool IsStarMapActive() const { return bStarMapActive; }

	/**
	 * 应由 AStellarEraPlayerController::ToggleStarMap 的进入/退出分支调用。
	 *
	 * **不放星图 Pawn 的 BeginPlay/EndPlay** —— 那个 Pawn 是懒创建并复用的，
	 * BeginPlay 一辈子只跑一次，第二次进入时标志不会被重新设上。
	 */
	void SetStarMapActive(bool bActive) { bStarMapActive = bActive; }

	//////////////////////////////////////////////////////////////////////////
	// 标定
	//
	// 这些都是 UPROPERTY(Config)：可以在 Config/DefaultGame.ini 里覆盖，改完重启
	// 编辑器生效，不必重新编译。风险表里那几个「调一下试试」都靠这里。

	/**
	 * 1 AU 对应多少世界厘米。默认 1e6，即 1 AU = 10 公里。
	 *
	 * 真值 1.496e13 下 float32 的 ULP 有 18 公里，比半径 1 公里的星球还大 18 倍，
	 * 体素世界会抖成一团。若实测仍有抖动，把它降到 3e5（1 AU = 3 公里）——
	 * 精度更宽松，代价是整个星系缩到三分之一。
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Cosmos|Scale",
		meta = (DisplayName = "世界标定（厘米/AU）"))
	double CentimetersPerAu = 1.0e6;

	/** 模拟年 / 真实秒。默认 0.00333，即公转一圈约 300 秒。 */
	UPROPERTY(Config, EditAnywhere, Category = "Cosmos|Time",
		meta = (DisplayName = "时间流速（模拟年/真实秒）"))
	double TimeScale = 0.00333;

	/**
	 * 最短的一条「天体间周期」每圈积分多少步，用来推自动步长。
	 *
	 * 周期不是手填的，而是由关卡里的实际间距反推：对每一对天体 (i, j)，
	 * T = 2π√(r³ / (G·(m_i + m_j)))，取其中最短的一条。见 FinalizeRegistration。
	 *
	 * 参考项目写死「最短周期 / 1000」，在本尺度下要 18 帧才算一步，肉眼可见卡顿。
	 * 20000 步/圈给出约每帧一步。
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Cosmos|Time")
	int32 StepsPerOrbit = 20000;

	/**
	 * 单帧积分步数上限。**这才是真正约束单帧开销的那一项** ——
	 * Tick 里单帧最多推进 max(该值 − 1, 1) × 步长 个模拟年：取 1 时是一个步长，
	 * 取 200 时是 199 个步长。
	 *
	 * 下限取 1：填 0 会让推进上限退化成「至少一步」，于是每帧都超限、
	 * TimeScale 每帧乘 0.9 掉下去，几帧内就把模拟冻住。
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Cosmos|Time", meta = (ClampMin = "1"))
	int32 MaxStepsPerFrame = 200;

	/**
	 * 引力软化长度，单位 AU。把点质量换成半径 ε 的 Plummer 球：加速度按
	 * a = G·m·Δ/(r² + ε²)^(3/2) 算，于是 r→0 时不再趋于无穷。
	 * 太小失去保护作用，太大会削弱真实引力。
	 *
	 * ── 为什么默认是 0.02 而不是 1e-5 ──────────────────────────────
	 * 1e-5 在近距离交会时等于没有。实测用的是 UniverseMap **当时**的三体初态：
	 * 100 M☉ × 3 共线在 X 轴上，位置 5 / 0 / −4.1767 AU，外侧两颗初速 ∓20 AU/年，
	 * 系统最短周期 0.603580 年。开局漂移 3.9e-07 一路健康，到 t ≈ 0.84 模拟年
	 * （TimeScale = 0.00333，即约 4.2 分钟 PIE）突然变成 +547，此后天体被甩到
	 * 4000 AU 外。该次运行里两体的**最小间距是 0.013 AU** —— 1e-5 的软化在这个
	 * 距离上什么也挡不住：加速度量级 G·M/r² ≈ 2.3e7 AU/年²，乘一个步长
	 * dt = 3.0e-5 年就是 Δv 量级 700 AU/年，而系统的速度尺度才 20 AU/年。
	 *
	 * ⚠ **那个初态后来被改掉了**：外侧那颗从 −4.1767 挪到 −5（对称的 5 / 0 / −5，
	 * 之后又改过一次标定（质量 100 → 0.873237、初速改成圆速度），现在是三颗圆轨道天体。
	 * 这些版本里两体的最近距离都是 **AU 量级**，远大于 0.02、更远大于 1e-5 ——
	 * 两者在它上面跑到 t = 10 年的漂移没有区别。
	 * 所以这一项现在**不是**在修当前关卡的 bug，而是把「近距离交会」这个失败面拆掉：
	 * 爆炸的机制（一步之内 Δv 超过系统的速度尺度）是 ε 与步长的性质，与具体初态
	 * 无关，换一个有关卡初态还会再撞上。
	 *
	 * 下面这张表是在**上面那个旧初态**上扫的（离线复刻引擎的积分器：同款
	 * Velocity Verlet、同款势能、同款步长 P/20000），跑到 t = 10 模拟年：
	 *
	 *     软化 ε |  漂移@2.0   漂移@5.0  漂移@10.0 | 最小间距 | 结论
	 *      0.0125 | -1.3e-08   +1.5e+02    +1.5e+02 |   0.0052 | t=2 看着好，t=5 炸了
	 *       0.015 | -1.3e-07   +5.8e-07    +1.4e-05 |   0.0514 | 撑住了，但漂移在往上爬
	 *        0.02 | +3.2e-08   +1.9e-07    +5.5e-08 |   0.1252 | 撑住，漂移压在 1e-7
	 *        0.03 | +2.9e-03   +2.4e-03    +6.0e-03 |   0.0261 | 撑住但交会没解开，见下
	 *        0.05 | +2.9e-08   +1.7e-08    +4.4e-06 |   0.0598 | 撑住，漂移在往上爬
	 *       0.075 | +1.3e-08   +1.5e-07    +1.1e-07 |   0.0666 | 撑住，漂移压在 1e-7
	 *
	 * 只有 0.02 与 0.075 把漂移按在 1e-7 撑到 t = 10 年。取 0.02：软化把 r 处的力按
	 * (1 + (ε/r)²)^(−3/2) 削掉，ε 小 3.75 倍则削弱程度差 3.75³ ≈ 53 倍 ——
	 * 1 AU 处 0.02 削掉 0.06%，0.075 削掉 0.84%。
	 *
	 * 0.02 仍在天体半径（本关卡 0.075 AU）**以下**，这是有意的：软化长度是「点质量
	 * 模型从这里往下就不成立」的尺度，而两颗 0.075 AU 的球擦到 0.15 AU 以内本来
	 * 就已经互相穿透了 —— 软化不该去修饰一个本就没有物理意义的位置。
	 *
	 * ⚠ 0.03 那行是反例，别照抄成「更大的值更稳」。它的 1e-3 漂移不是更稳，是**没解**：
	 * 那次交会最近到 0.026 AU，加速度量级 G·m/(r²+ε²) ≈ 2.5e6 AU/年²，乘一个步长
	 * dt = 3.0e-5 年，Δv 量级 75 AU/年 —— 而整个系统的速度尺度才 20 AU/年。一步之内
	 * 速度翻几倍，辛积分器不再守恒，只是没炸而已。
	 *
	 * ⚠ 这一项治的是**数值爆炸**，不是**混沌**。上面那张表是在旧初态上扫的，那个初态本身就是
	 * 散射问题：跑到 t = 10 年的过程中仍有一颗被甩到远大于系统尺度的地方。那是能量守恒
	 * （漂移 1e-7）之下的真实动力学，不是 bug。想让关卡里是个安分的天体系统，
	 * 得改关卡的初态 —— 现在那一版已经改成一组圆轨道了。
	 *
	 * 这是 UPROPERTY(Config)：某个关卡想用别的值，在 Config/DefaultGame.ini 的
	 * [/Script/StellarEra.CosmosSimulationSubsystem] 段覆盖即可，不必重编。
	 * 逐天体的覆盖走 FCosmosBodyState::SofteningOverride（该天体单独生效）。
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Cosmos|Physics",
		meta = (DisplayName = "引力软化长度（AU）"))
	double SofteningLength = 0.02;

	/**
	 * 携带判定：Pawn 到球心的距离 < 地表半径 × 该值，就把星球本帧的位移加给 Pawn。
	 *
	 * 默认 2.0 —— 半径 1 公里的星球携带 2 公里内的一切。站在地面上、跳跃中、
	 * 飞船悬停在 100 米高度都覆盖得到；在 50 公里外自由飞行时不会被推着走。
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Cosmos|Physics")
	double RideRadiusScale = 2.0;

	/** 诊断日志间隔（秒）。设 0 关闭。 */
	UPROPERTY(Config, EditAnywhere, Category = "Cosmos|Debug")
	float DiagnosticInterval = 5.f;

private:
	/** 首次 Tick 时做一次：配置求解器、注册所有天体、推步长、记能量基准。 */
	void FinalizeRegistration();

	/** 把一颗天体注册进求解器。 */
	void AddBodyToSimulator(UCosmosOrbitComponent* Component);

	/**
	 * 把每颗体素星球的接触半径刷成**当前朝向**下的地形地表半径。
	 *
	 * 为什么每帧刷：FCosmosBodyState::Radius 是球面的，而体素星球的地表是起伏的
	 * —— 本关卡基准半径 750 米，起伏 ±80 米。拿平均球半径去解接触，隆起处会把
	 * 天体埋进去、洼地处会浮在空中，差多少全看落点。
	 *
	 * 方向取「星球中心 → 最近的那个天体」：只有最近的天体才可能进到接触距离内，
	 * 所以只需要这一个方向的地表高度，每颗星每帧一次地形采样。
	 *
	 * 求解器仍然只读 Radius，既不知道地形也不知道 Actor —— 这是引擎侧对求解器的
	 * 单向喂数据，不破坏它的引擎无关性。
	 */
	void RefreshPlanetContactRadii();

	/**
	 * 把飞船的接触探针（Actor 本地厘米）转成求解器坐标写进天体状态。
	 *
	 * 探针来自 Hull 的 Simple Collision 凸包，取代 300 cm 的球体接触。
	 * 没有探针（网格没导碰撞）时退回 300 cm 球 —— Radius 回到旧值。
	 * 与 RefreshPlanetContactRadii 一样，必须在推进之前调用。
	 */
	void RefreshShipContactProbes();

	/**
	 * 每帧推进之前，把**受控天体**（飞船）想施加的力写进求解器。
	 *
	 * 方向只能是「子系统下来读飞船」：飞船模块不能依赖 StellarEra，
	 * 飞船不能反过来调本子系统（见 ASpaceshipPawn 的类注释）。
	 *
	 * DeltaSeconds 是本帧的帧时长，原样转给飞船 —— 它的制动要按「本帧满推力能改变
	 * 多少速度」收敛（见 ASpaceshipPawn::GetThrustAccelerationWorldCmPerS2），必须知道这一帧
	 * 有多长。由本处传入而不是让飞船在自己 Tick 里抄一份：两个 Tick 谁先谁后不保证，
	 * 抄的那份在帧率突变的那一帧会让制动过冲。
	 *
	 * 只认 ASpaceshipPawn —— 现在只有一艘船，Cast 就是全部；等出现第二个受控天体
	 * （第二艘船、导弹）再抽接口，那时抽出来的形状也才看得清。
	 */
	void ApplyControlledBodyForces(float DeltaSeconds);

	/**
	 * 游戏单位速度（AU/年）→ **玩家感知**的世界速度（厘米/秒）。
	 *
	 * 即「飞船在真实一秒里走过多少厘米」。1 模拟年只花 1/TimeScale 真实秒，所以
	 * v_world = v_game × CentimetersPerAu × TimeScale（默认 3330）。
	 *
	 * ⚠ 与 FNBodySimulator::ToWorldVelocity 差 YearToSecond · TimeScale ≈ 1.05e5 倍
	 * —— 那个是「1 模拟年 = 1 真实年」的实时值，屏幕上的数不是它。
	 */
	FVector ToWorldVelocityScaled(const FVector3d& GameUnitVelocity) const;

	/**
	 * 把 BodyActor 本帧的位移施加给骑在它上面的 Pawn。
	 *
	 * 只在星球附近生效（判定半径 = 地表半径 × RideRadiusScale），不是全局平移：
	 * 在 50 公里外自由飞行的飞船不该被行星拖着走。
	 *
	 * 两颗星的判定球可能重叠，重叠区里的 Pawn 若被两颗星各推一次，位移会叠加成
	 * 两倍（本关卡足以让飞船卡在边界上横向扫走、飞不到另一颗星）。所以再加一条
	 * 「最近者独占」：只有离 Pawn 最近的那颗 AVoxelPlanetActor 才携带它。
	 * 不是 AVoxelPlanetActor 的天体不是携带者，不参与比较。
	 */
	void CarryRiders(AActor* BodyActor, const FVector& Delta);

	TArray<TObjectPtr<UCosmosOrbitComponent>> Bodies;

	/** 星图模式下选中的天体。弱引用 —— 天体被销毁后自动变 nullptr。 */
	TWeakObjectPtr<AActor> SelectedBody;

	/** 星图模式是否激活。 */
	bool bStarMapActive = false;

	/**
	 * 上一次 ApplyControlledBodyForces 里是否有天体被外力或阻尼作用。
	 *
	 * 有受控天体时，总能量与总动量这两项都不再守恒，这个漂移值于是不再是积分
	 * 误差：推力做功把它推正，阻尼与接触耗散把它拉负（接触抹的是相对法向速度，
	 * 只吃能量、不动总动量）。加一句提醒，免得看日志的人去查积分器。
	 */
	bool bHasForcedBodies = false;

	FNBodySimulator Simulator;

	/** 自动推导出的积分步长，单位模拟年。 */
	double BaseTimeStep = 5.0e-5;

	/**
	 * 全系统最短的配对周期（年），FinalizeRegistration 里算一次。
	 *
	 * 与 BaseTimeStep 同源（后者 = 它 / StepsPerOrbit），所以一起存 —— 两者都基于
	 * **开局**的间距，之后不再变。预测窗口的上限读它，见 GetShortestPeriodYears。
	 */
	double ShortestPeriodYears = 0.0;

	/**
	 * 跨帧累积的待推进时间（模拟年）。
	 *
	 * 步长固定是辛积分器的前提（变步长会让能量漂移失去上界），而每帧要推进的
	 * 年数不是步长的整数倍，所以余量必须存下来。见 Tick 里的详细说明。
	 */
	double AdvanceAccumulator = 0.0;

	float DiagnosticTimer = 0.f;
	bool bInitialized = false;
};
