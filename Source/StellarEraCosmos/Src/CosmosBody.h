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
 * 受控天体（目前只有飞船）对模拟层暴露的接口。抽出来是因为 Cosmos 若直接
 * Cast<ASpaceshipPawn> 就被钉死在 Pawns 上，反过来挡住星图相机搬家（成环）。
 * 声明在 Cosmos（消费方）、实现在 Pawns（ASpaceshipPawn），依赖方向与
 * IStellarEraNoticeSink 相反 —— 这次消费方在低层。
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

	/** 玩家输入产生的推力加速度（世界空间，厘米/秒²）；带 DeltaSeconds 是为了制动按帧长收敛。 */
	virtual FVector GetThrustAccelerationWorldCmPerS2(float DeltaSeconds) const = 0;

	/** 阻尼时间常数（秒）；<= 0 表示不加阻尼。 */
	virtual float GetDampingTime() const = 0;

	// ── 状态回写 ──

	/** 求解器把本帧速度写回来（世界空间，厘米/秒）。 */
	virtual void SetCosmosVelocity(const FVector& InVelocity) = 0;

	/** 调试视角是否打开（B 键）。合力箭头只在它打开时画。 */
	virtual bool IsDebugViewMode() const = 0;
};
