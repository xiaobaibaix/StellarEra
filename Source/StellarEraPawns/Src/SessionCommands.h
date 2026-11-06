// Copyright Voxel Plugin SAS. All Rights Reserved.
// Pawn 想让控制器做一件「跨 Pawn 的会话动作」，但看不到 Game 模块时用的接口。

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "SessionCommands.generated.h"

UINTERFACE(MinimalAPI, meta = (CannotImplementInterfaceInBlueprint))
class UStellarEraSessionCommands : public UInterface
{
	GENERATED_BODY()
};

/**
 * 控制器级的会话命令：进 / 出星图、登船 / 离船。
 *
 * 为什么需要它：这三个键（M、E）现在由各 Pawn 的输入组件绑 —— M 装在角色、
 * 飞船与星图相机上（相机也要它，否则进图后按 M 退不出来），E 装在角色与飞船上。
 * 但真正改 Possess、生成角色、切星图模式的实现住在 AStellarEraPlayerController
 * （Game 模块），Pawn 所在模块是它的上游，不能 include 控制器。
 *
 * 所以接口声明在本模块、实现落在控制器上，Pawn 通过
 * `Cast<IStellarEraSessionCommands>(Pawn->GetController())` 取到。
 */
class STELLARERAPAWNS_API IStellarEraSessionCommands
{
	GENERATED_BODY()

public:
	/**
	 * M：进入 / 退出星图。
	 *
	 * 方向由控制器按「当前 Possess 的是不是星图相机」判定，调用方不用区分。
	 */
	virtual void ToggleStarMap() = 0;

	/** E：角色登船 / 飞船离船。不在角色或飞船上时无动作。 */
	virtual void InteractWithShip() = 0;
};
