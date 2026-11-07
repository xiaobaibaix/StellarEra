// Copyright Voxel Plugin SAS. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "NBodySimulator.h"
#include "CosmosSimulationSubsystem.generated.h"

class UCosmosOrbitComponent;

/**
 * N 体引力的唯一驱动（WorldSubsystem 单例，不占 GameMode 槽位）。每帧按固定顺序走完，
 * 避免天体之间差一帧：刷接触形状 → 推进（推进前才读外力）→ 写回（刚体含朝向）→ 诊断。
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

	/** 预测未来 HorizonYears 年的轨迹（世界坐标厘米）。索引无效 / 窗口非正 / 采样 < 2 时返回 false 并清空输出。 */
	bool PredictTrajectoryWorld(int32 SimulatorIndex, double HorizonYears, int32 NumSamples,
	                            TArray<FVector>& OutWorldPositions) const;

	/** 预测相对主导天体的闭合轨道（世界坐标厘米）。非椭圆或回不到起点返回 false，调用方退回 PredictTrajectoryWorld。 */
	bool PredictClosedOrbitWorld(int32 SimulatorIndex, int32 NumSamplesPerOrbit,
	                             TArray<FVector>& OutWorldPositions) const;

	/** 已模拟的天文时间（年）。拖尾按模拟时间采样，不按帧 —— 否则覆盖时长会随帧率漂移。 */
	double GetSimulatedTimeYears() const;

	/** 天体 i 参与的最短动力学时标（年）。算不出（单天体 / 退化配对）返回 0。 */
	double GetOrbitalPeriodYears(int32 SimulatorIndex) const;

	/**
	 * 全系统最短配对周期（年），自动步长的分母。预测窗口不读它 —— 上限按每个天体
	 * 自身周期算，否则快飞船会压短所有天体。
	 */
	double GetShortestPeriodYears() const { return ShortestPeriodYears; }

	/**
	 * 天体当前速度（AU/年，求解器单位），与配置的 InitialVelocityKmPerSecond 不同。
	 * 下标无效返回零向量 —— GetBody 越界是 UB，校验必须在这里做。
	 */
	FVector3d GetBodyVelocityGameUnits(int32 SimulatorIndex) const;

	/** 纯引力加速度（AU/年²），不含推力与阻尼；下标无效返回零向量。星图「引力合力箭头」读它。 */
	FVector3d GetBodyGravityGameUnits(int32 SimulatorIndex) const;

	//////////////////////////////////////////////////////////////////////////
	// 星图模式
	//
	// 选中状态放这里而不是星图 Pawn：子系统是唯一知道全部天体的地方，轨迹组件每帧单向读它决定高亮。

	/** 选中一颗天体。传 nullptr 取消选中。存弱引用，天体被销毁后自动失效。 */
	void SetSelectedBody(AActor* Body);

	/** 当前选中的天体。未选中、或它已被销毁时返回 nullptr。 */
	AActor* GetSelectedBody() const;

	/**
	 * 所有已注册天体的 Actor 写进 Out（先 Reset），跳过失效组件。顺序不等于注册顺序：
	 * FinalizeRegistration 按质量降序排一次（拾取等距时取先出现的）。
	 */
	void GetRegisteredBodies(TArray<AActor*>& Out) const;

	/** 星图模式是否激活。轨迹组件的显示门控读它。 */
	bool IsStarMapActive() const { return bStarMapActive; }

	/**
	 * 由 AStarMapCameraPawn::EnterStarMap / ExitStarMap 调用。不能放 Pawn 的
	 * BeginPlay/EndPlay —— 那个 Pawn 懒创建复用，BeginPlay 只跑一次。
	 */
	void SetStarMapActive(bool bActive) { bStarMapActive = bActive; }

	//////////////////////////////////////////////////////////////////////////
	// 标定（都是 UPROPERTY(Config)，改 DefaultGame.ini 后重启编辑器生效，不必重编）

	/**
	 * 1 AU 对应多少世界厘米。默认 1e6（1 AU = 10 公里）：真值 1.496e13 处 float32 的
	 * ULP 有 18 公里，体素世界会抖。
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Cosmos|Scale",
		meta = (DisplayName = "世界标定（厘米/AU）"))
	double CentimetersPerAu = 1.0e6;

	/** 模拟年 / 真实秒。默认 0.00333，即公转一圈约 300 秒。 */
	UPROPERTY(Config, EditAnywhere, Category = "Cosmos|Time",
		meta = (DisplayName = "时间流速（模拟年/真实秒）"))
	double TimeScale = 0.00333;

	/**
	 * 最短配对周期每圈积分多少步，用来推自动步长。20000 步/圈约每帧一步；
	 * 1000 步/圈在本尺度下 18 帧一步，肉眼可见卡顿。
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Cosmos|Time")
	int32 StepsPerOrbit = 20000;

	/**
	 * 单帧积分步数上限：单帧最多推进 max(该值 − 1, 1) × 步长。
	 * 必须 ≥ 1：填 0 会让 TimeScale 每帧乘 0.9 掉下去，几帧内冻住。
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Cosmos|Time", meta = (ClampMin = "1"))
	int32 MaxStepsPerFrame = 200;

	/** 单帧逐探针体素查询上限。按「地表 1 体素」去重，正常个位数；超了退回解析半径。 */
	static constexpr int32 MaxContactSurfaceQueriesPerFrame = 32;

	/**
	 * 引力软化长度（AU）：a = G·m·Δ/(r² + ε²)^(3/2)，避免近距离交会时数值爆炸。
	 * 它治数值爆炸不治混沌，想让系统安分得改关卡初态。逐天体覆盖走 FCosmosBodyState::SofteningOverride。
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Cosmos|Physics",
		meta = (DisplayName = "引力软化长度（AU）"))
	double SofteningLength = 0.02;

	/** 携带判定：Pawn 到球心距离 < 地表半径 × 该值就把星球位移加给它（2.0 = 1 公里星球携带 2 公里内的一切）。 */
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
	 * 把每颗体素星球的接触半径刷成当前朝向的地表半径。每帧刷是因为地形起伏 ±80 米，
	 * 用平均球半径会让隆起处埋进去、洼地处浮空。
	 */
	void RefreshPlanetContactRadii();

	/**
	 * 把飞船的接触探针（Actor 本地厘米）写进求解器，并记下各方向的真实地表半径。
	 * 探针来自 Hull 的 Simple Collision 凸包；没有则退回 300 cm 球。体素查询必须在
	 * 推进前做一次并缓存 —— 一帧最多 200 个子步，绝不能在里面查体素。
	 */
	void RefreshShipContactProbes();

	/**
	 * 在**真正会推进**的帧里、推进之前，把受控天体（飞船）想施加的力写进求解器。
	 * 零子步帧不调用：读角加速度会清掉飞船攒下的待发冲量，而那一帧没有 StepExact 消费它。
	 *
	 * DeltaSeconds 传**本帧实际消费的真实秒**（StepsToRun·BaseTimeStep/TimeScale），不是引擎帧时长：
	 * 鼠标/横滚按「本帧 Δω = target − current」折算，拿帧时长当除数会随帧率放大。
	 */
	void ApplyControlledBodyForces(float DeltaSeconds);

	/** AU/年 → 世界速度（厘米/秒）：v × CentimetersPerAu × TimeScale（默认 3330）。 */
	FVector ToWorldVelocityScaled(const FVector3d& GameUnitVelocity) const;

	/**
	 * 把 BodyActor 本帧位移施加给附近（< 地表半径 × RideRadiusScale）的 Pawn，不是全局平移。
	 * 判定球重叠时同一 Pawn 会被推两次 → 只有最近的 AVoxelPlanetActor 才携带它。
	 */
	void CarryRiders(AActor* BodyActor, const FVector& Delta);

	TArray<TObjectPtr<UCosmosOrbitComponent>> Bodies;

	/** 星图模式下选中的天体。弱引用 —— 天体被销毁后自动变 nullptr。 */
	TWeakObjectPtr<AActor> SelectedBody;

	/** 星图模式是否激活。 */
	bool bStarMapActive = false;

	/** 上一次 ApplyControlledBodyForces 是否有天体受外力/阻尼：此时能量漂移不再是积分误差，别去查积分器。 */
	bool bHasForcedBodies = false;

	FNBodySimulator Simulator;

	/** 自动推导出的积分步长，单位模拟年。 */
	double BaseTimeStep = 5.0e-5;

	/** 全系统最短配对周期（年），FinalizeRegistration 算一次；与 BaseTimeStep 同源。 */
	double ShortestPeriodYears = 0.0;

	/** 跨帧累积的待推进时间（模拟年）。固定步长是辛积分器的前提，而每帧年数不是步长的整数倍，余量必须存下来。 */
	double AdvanceAccumulator = 0.0;

	float DiagnosticTimer = 0.f;
	bool bInitialized = false;
};
