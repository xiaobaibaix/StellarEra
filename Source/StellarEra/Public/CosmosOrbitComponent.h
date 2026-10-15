// Copyright Voxel Plugin SAS. All Rights Reserved.
// 把任意 Actor 变成 N 体系统里的一个天体。

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "CosmosTypes.h"
#include "CosmosOrbitComponent.generated.h"

/**
 * 天体组件：挂在任意 Actor 上，它就参与 N 体引力。
 *
 * 刻意做成「组件」而不是「Actor 基类」：
 *   - 恒星不需要新类 —— 一个 StaticMeshActor 挂上它就是天体，不必为它写一个
 *     只有网格的恒星 Actor；
 *   - 行星也不需要 —— AVoxelPlanetActor 挂上它就是天体，不必让体素星球去继承
 *     一个 N 体的基类。
 *
 * 组件本身不做任何事：它只存参数。注册、推进、写回位置全在
 * UCosmosSimulationSubsystem 里 —— N 体是耦合系统（A 受 B 的力，B 也受 A 的力），
 * 必须有唯一的编排者，各天体分别 tick 会读到彼此的中间状态。
 */
UCLASS(ClassGroup = (Cosmos), meta = (BlueprintSpawnableComponent))
class UCosmosOrbitComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UCosmosOrbitComponent();

	/**
	 * 质量，单位 M☉（太阳质量）。默认值是地球质量。
	 *
	 * 行星之间的引力相对恒星可以忽略，但质量仍是决定「谁绕谁」的唯一依据 ——
	 * 中心天体必须是这里面最大的那个，否则行星会直接飞出去。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cosmos|Body")
	double MassSolar = 3.0034893e-6;

	/**
	 * 轨道初始条件。
	 *
	 * 用「半长轴 + 偏心率」而不是直接填位置速度：这样天体保证落在正确的轨道上。
	 * 直接填位置和速度，一旦两者不匹配就会飞向恒星或者逃逸。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cosmos|Orbit")
	FCosmosOrbitInit Orbit;

	/**
	 * 中心天体的质量，单位 M☉。**只用于推导初始速度**，不参与积分。
	 *
	 * 积分用的是各天体自己的 MassSolar；这个值只在开局算「给定半长轴该飞多快」
	 * 时用一次。填错只会让开局速度不对，不会改变后续的引力。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cosmos|Orbit")
	double CentralMassSolar = 1.0;

	/**
	 * 是否用上面的轨道根数覆盖 Actor 摆放的位置与速度。
	 *
	 * 为真：开局位置由轨道根数算出，Actor 摆在关卡哪里都无所谓。
	 * 为假：位置保持关卡里摆的样子，速度为零 —— 中心恒星用这个。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cosmos|Orbit")
	bool bApplyOrbitInit = true;

	/**
	 * 是否每帧把求解器算出的位置写回所属 Actor。
	 *
	 * **与 bApplyOrbitInit 是两件事，不要合并。** 参考项目只有一个 bSimulated
	 * 同时管这两件事，结果是中心恒星的位置在求解器里漂、在 Actor 上不动，两边
	 * 分叉。这里拆开：
	 *   - 恒星：bApplyOrbitInit = false、bSimulated = true —— 位置保持摆放的样子，
	 *     但仍由求解器写回，两边永远一致；
	 *   - 行星：两个都 true；
	 *   - 纯装饰的远处天体：两个都 false。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cosmos|Body")
	bool bSimulated = true;

	/** 求解器里的索引。运行时由子系统填写，不要手改。 */
	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "Cosmos")
	int32 SimulatorIndex = INDEX_NONE;
};
