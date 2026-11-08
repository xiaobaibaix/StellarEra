// 预测速度的参考系来源。插件的 UVoxelInvokerWithPredictionComponent 直接拿
// GetOwner()->GetVelocity() 外推，那是**绝对**速度；行星以 km/s 公转时，落地的飞船
// 预测点也会被甩到船头一两百米外。由知道自己在哪个参考系里动的 Pawn 覆写。

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "VoxelInvokerPredictionReference.generated.h"

UINTERFACE(MinimalAPI, meta = (CannotImplementInterfaceInBlueprint))
class UVoxelInvokerPredictionReference : public UInterface
{
	GENERATED_BODY()
};

/**
 * 契约：返回**世界厘米/秒**、某参考系（通常是脚下那颗行星）下的速度。调用器用它
 * 乘 PredictionTime 得预测点。不实现则退回 Actor 的绝对速度（插件原行为）。
 * 实现者是 StellarEraPawns 的 ASpaceshipPawn，体素这边不认识它。
 */
class STELLARERAPLANET_API IVoxelInvokerPredictionReference
{
	GENERATED_BODY()

public:
	virtual FVector GetPredictionReferenceVelocity() const = 0;
};
