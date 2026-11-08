// 视点门控：换视点时由控制器 OnPossess 关掉非视点 Pawn 的体素调用器。
// UAlwaysLocalVoxelInvokerComponent 让调用器永久参与，进星图后飞船那三档 LOD 环
// 会在看不见的地方照算；角色是例外，见下面 SetViewpointInvokersActive 的契约。
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
 * 视点；实现必须幂等（走 VoxelViewpointInvokers::SetEnabled）；必须在 BeginPlay
 * 重新应用一次 —— 组件在 OnRegister 就自动启用，早于 Possess。
 * 例外：APlanetCharacter 忽略 bActive，开合只看登船 —— 人脚下那圈碰撞块不能回收。
 */
class STELLARERAPLANET_API IVoxelViewpointInvokers
{
	GENERATED_BODY()

public:
	virtual void SetViewpointInvokersActive(bool bActive) = 0;
};
