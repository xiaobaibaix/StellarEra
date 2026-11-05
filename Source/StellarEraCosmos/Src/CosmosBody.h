// Copyright Voxel Plugin SAS. All Rights Reserved.
// 「被 N 体求解器驱动的一个天体」—— Cosmos 对受控实体的全部要求。

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "CosmosBody.generated.h"

class AVoxelPlanetActor;

UINTERFACE(MinimalAPI, meta = (CannotImplementInterfaceInBlueprint))
class UCosmosBody : public UInterface
{
	GENERATED_BODY()
};

/**
 * 受控天体（目前只有飞船）对模拟层暴露的接口。
 *
 * 为什么需要它：`UCosmosSimulationSubsystem` 与 `UCosmosTrajectoryComponent` 各有
 * 几处 `Cast<ASpaceshipPawn>`，把 Cosmos 钉死在 Pawns 上。那笔依赖反过来挡住了
 * 星图相机搬家（相机要 Cosmos，Pawns 里一放就成环）。接口抽出来后 Cosmos 只认
 * 本接口，Pawns 反而可以放心依赖 Cosmos。
 *
 * 声明在 Cosmos（消费方）、实现在 Pawns（`ASpaceshipPawn`）——依赖方向与
 * `IStarMapUIInputs`、`IStellarEraNoticeSink` 相反，因为这次消费方在低层。
 *
 * 全部方法与 `ASpaceshipPawn` 的现有公开方法逐字对应，实现方只需加 `override`。
 */
class STELLARERACOSMOS_API ICosmosBody
{
	GENERATED_BODY()

public:
	// ── 接触 ──

	/** 凸包探针不可用时的兜底接触半径（厘米）。 */
	virtual float GetContactRadiusCm() const = 0;

	/** 船体局部空间的接触探针；为空时求解器退回上面的球。 */
	virtual const TArray<FVector>& GetContactProbesLocalCm() const = 0;

	// ── 受力 ──

	/** 大气阻力要按「相对参考行星」的速度算，参考行星由这里给。 */
	virtual AVoxelPlanetActor* GetPlanetActor() const = 0;

	/** 子系统每帧推进来的参考行星速度（世界空间，厘米/秒）。 */
	virtual void SetReferencePlanetVelocity(const FVector& InVelocity) = 0;

	/**
	 * 玩家输入产生的推力加速度（世界空间，厘米/秒²）。
	 *
	 * 带 DeltaSeconds：飞船的制动要按「本帧满推力能改变多少速度」收敛。
	 */
	virtual FVector GetThrustAccelerationWorldCmPerS2(float DeltaSeconds) const = 0;

	/** 阻尼时间常数（秒）；<= 0 表示不加阻尼。 */
	virtual float GetDampingTime() const = 0;

	// ── 状态回写 ──

	/** 求解器把本帧速度写回来（世界空间，厘米/秒）。 */
	virtual void SetCosmosVelocity(const FVector& InVelocity) = 0;

	/** 调试视角是否打开（B 键）。合力箭头只在它打开时画。 */
	virtual bool IsDebugViewMode() const = 0;
};
