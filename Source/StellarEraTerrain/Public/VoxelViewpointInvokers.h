// 视点门控：只有当前被本地控制器 Possess 的那个 Pawn 的体素调用器参与 LOD。
//
// 为什么需要它：本项目用 UAlwaysLocalVoxelInvokerComponent 覆写了 IsLocalInvoker
// （见 AlwaysLocalVoxelInvokerComponent.h），代价是所有调用器永久参与 —— 进星图后
// 行星 3 个 + 飞船 3 个 + 角色 1 个 + 星图 3 个同时算，而星图里根本看不见飞船和角色。
// 这里补一层显式开关：换视点时由 AStellarEraPlayerController::OnPossess 统一驱动。
//
// 行星自己的 UVoxelSimpleInvokerComponent 不实现本接口，因此永远不受影响。

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "VoxelViewpointInvokers.generated.h"

class UVoxelInvokerComponentBase;

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
class STELLARERATERRAIN_API IVoxelViewpointInvokers
{
	GENERATED_BODY()

public:
	virtual void SetViewpointInvokersActive(bool bActive) = 0;
};

namespace VoxelViewpointInvokers
{
	/**
	 * 幂等地把调用器切到 bEnabled。
	 *
	 * 插件的 EnableInvoker / DisableInvoker 在状态已经对时会往 Message Log 写
	 * "Invoker already enabled/disabled"（VoxelInvokerComponent.cpp:41 / :57）。
	 * 换视点是低频事件但会反复传同一个值，所以必须先比对再动手。
	 * 未注册的组件直接跳过 —— 注册状态由 OnRegister / OnUnregister 管。
	 */
	STELLARERATERRAIN_API void SetEnabled(UVoxelInvokerComponentBase* Invoker, bool bEnabled);
}
