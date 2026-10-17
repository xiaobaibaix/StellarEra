// 宇宙游戏模式 —— 持有 N 体求解器，统一驱动所有天体。
//
// 职责：
//   1. BeginPlay 时扫描关卡，把所有 ACosmosBodyActor 注册进求解器
//   2. Tick 推进模拟，再把结果同步回各个 Actor
//   3. 暴露诊断数据供 HUD 或调试面板使用
//
// 为什么求解器放在 GameMode 而不是各个 Actor 自己算：
// N 体是耦合系统，A 受 B 的力、B 也受 A 的力。
// 如果每个 Actor 各自算，会得到互相矛盾的解。

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "CosmosTypes.h"
#include "CosmosGameMode.generated.h"

class FNBodySimulator;
class ACosmosBodyActor;
class ACosmosCameraPawn;

/**
 * 宇宙游戏模式。
 *
 * 关卡里拖入若干 ACosmosBodyActor，设好参数，PIE 即可运行。
 */
UCLASS(Blueprintable)
class VOXELCOSMOS_API ACosmosGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ACosmosGameMode();

	//~ Begin AActor Interface
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	//~ End AActor Interface

	//////////////////////////////////////////////////////////////////////
	// 模拟参数

	/**
	 * 时间流速倍率。
	 * 1.0 = 真实时间（土星公转要 30 年，显然没法玩）。
	 * 1000.0 = 1 秒等于 1000 秒。
	 *
	 * 典型取值：
	 *   观察行星轨道：100 ~ 1000
	 *   看外太阳系：10000
	 *   看星系尺度：1000000 以上（此时应降低精度需求或关闭模拟）
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category = "Cosmos|Simulation", meta = (ClampMin = "0.0"))
	float TimeScale = 100.f;

	/** 单位系统。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category = "Cosmos|Simulation")
	ECosmosUnitSystem UnitSystem = ECosmosUnitSystem::Game;

	/** 积分器类型。默认辛积分。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category = "Cosmos|Simulation")
	ECosmosIntegrator Integrator = ECosmosIntegrator::VelocityVerlet;

	/**
	 * 基础时间步长，单位与 UnitSystem 一致（游戏单位下是「年」）。
	 * 相对于最短轨道周期要足够小，否则轨道会失真。
	 *
	 * 游戏单位下的经验值：1e-5 ~ 1e-4 年（约 5 ~ 50 分钟）。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category = "Cosmos|Simulation", meta = (ClampMin = "1e-12"))
	double BaseTimeStep = 1e-5;

	/** 引力软化长度。防止近距离数值爆炸。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category = "Cosmos|Simulation", meta = (ClampMin = "1e-9"))
	double SofteningLength = 1e-5;

	/** 引力截断半径（游戏单位 AU）。0 = 不截断。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category = "Cosmos|Simulation", meta = (ClampMin = "0.0"))
	double GravityCutoffAu = 0.0;

	/**
	 * 单帧最大积分步数。
	 * 防止 TimeScale 过大时一帧内要算几千步导致卡死。
	 * 超限会自动降TimeScale 并在日志里提示。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category = "Cosmos|Simulation", meta = (ClampMin = "1"))
	int32 MaxStepsPerFrame = 2000;

	/** 是否自动在 BeginPlay 时根据天体分布估算一个合适的步长。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category = "Cosmos|Simulation")
	bool bAutoTuneTimeStep = true;

	//////////////////////////////////////////////////////////////////////
	// 诊断

	/** 总能量漂移比例。辛积分器下应长期维持在 1e-6 量级。 */
	UFUNCTION(BlueprintPure, Category = "Cosmos|Debug")
	double GetEnergyDriftRatio() const;

	/** 当前实际积分步长（考虑自动调优后）。 */
	UFUNCTION(BlueprintPure, Category = "Cosmos|Debug")
	double GetEffectiveTimeStep() const;

	/** 已模拟的天文时间（游戏单位：年）。 */
	UFUNCTION(BlueprintPure, Category = "Cosmos|Debug")
	double GetSimulatedYears() const;

	/** 求解器引用。 */
	UFUNCTION(BlueprintPure, Category = "Cosmos")
	FNBodySimulator& GetSimulator() { return Simulator; }

	/** 注册进模拟的天体列表。 */
	UFUNCTION(BlueprintPure, Category = "Cosmos")
	const TArray<ACosmosBodyActor*>& GetBodies() const { return Bodies; }

	/** 打印一份诊断报告到日志。 */
	UFUNCTION(BlueprintCallable, Category = "Cosmos|Debug")
	void LogDiagnostics() const;

protected:
	/** 扫描关卡收集天体。 */
	void CollectBodies();

	/** 相机用来聚焦的天体列表。 */
	void RegisterFocusTargets(ACosmosCameraPawn* CameraPawn) const;

	/** 根据最短轨道周期估算合适的步长。 */
	void AutoTuneTimeStep();

	FNBodySimulator Simulator;

	UPROPERTY()
	TArray<ACosmosBodyActor*> Bodies;

	/**
	 * 首帧标记。
	 * UE 的BeginPlay 顺序是 GameMode 先于关卡 Actor，
	 * 所以父子关系建立、体素星球查找都要等到 Actor 自己的 BeginPlay 之后。
	 * 这个标记用来把「停用体素星球」延后到首帧 Tick。
	 */
	bool bFirstFrameDone = false;
};
