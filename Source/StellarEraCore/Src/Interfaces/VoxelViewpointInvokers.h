// 视点门控：只有当前被本地控制器 Possess 的那个 Pawn 的体素调用器参与 LOD。
//
// 为什么需要它：本项目用 UAlwaysLocalVoxelInvokerComponent 覆写了 IsLocalInvoker
// （见 AlwaysLocalVoxelInvokerComponent.h），代价是所有调用器永久参与 —— 进星图后
// 行星 3 个 + 飞船 3 个 + 角色 1 个 + 星图 3 个同时算，而星图里根本看不见飞船和角色。
// 这里补一层显式开关：换视点时由 AStellarEraPlayerController::OnPossess 统一驱动。
//
// 行星自己的 UVoxelSimpleInvokerComponent 不实现本接口，因此永远不受影响。
//
// 位置说明：接口住在 Core（L0）。实现它的是 StellarEraPawns 里的三个 Pawn，
// 使用它的是 StellarEra 主模块的控制器，两方都不该为这一个头互相依赖，
// 所以放在所有人共同的下游。配套的 VoxelViewpointInvokers::SetEnabled 需要
// Voxel 插件类型，留在 StellarEraPlanet/Public/Terrain/VoxelViewpointInvokerUtils.h。

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "VoxelViewpointInvokers.generated.h"

UINTERFACE(MinimalAPI, meta = (CannotImplementInterfaceInBlueprint))
class UVoxelViewpointInvokers : public UInterface
{
	GENERATED_BODY()
};

/**
 * 由「持有体素调用器的 Pawn」实现。
 *
 * 契约：
 *   1. bActive = true 表示本 Pawn 是当前视点；
 *   2. 实现必须幂等 —— 重复传同一个值不改变状态，也不触发插件的
 *      "Invoker already enabled/disabled"（先用 VoxelViewpointInvokers::SetEnabled）；
 *   3. 实现必须记住最后一次的值，并在 BeginPlay 里按它重新应用 ——
 *      组件在 OnRegister 中自动启用（VoxelInvokerComponent.cpp:83），早于 Possess。
 */
class STELLARERACORE_API IVoxelViewpointInvokers
{
	GENERATED_BODY()

public:
	virtual void SetViewpointInvokersActive(bool bActive) = 0;
};
