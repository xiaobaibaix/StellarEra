// 视点门控：只有当前被本地控制器 Possess 的那个 Pawn 的体素调用器参与 LOD。
// UAlwaysLocalVoxelInvokerComponent 让所有调用器永久参与，进星图后飞船与角色
// 的也照算而根本看不见；这里补一层显式开关，换视点时由控制器 OnPossess 驱动。
// 行星自己的 UVoxelSimpleInvokerComponent 不实现本接口，永远不受影响。
// 实现者是 Pawns 的三个 Pawn 与 UI 的星图相机；SetEnabled 见 InvokerUtils.h。

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
 * 由「持有体素调用器的 Pawn」实现。契约：bActive = true 表示本 Pawn 是当前
 * 视点；实现必须幂等（走 VoxelViewpointInvokers::SetEnabled）；必须记住最后一次
 * 的值并在 BeginPlay 重新应用 —— 组件在 OnRegister 就自动启用，早于 Possess。
 */
class STELLARERAPLANET_API IVoxelViewpointInvokers
{
	GENERATED_BODY()

public:
	virtual void SetViewpointInvokersActive(bool bActive) = 0;
};
