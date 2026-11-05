// Copyright Voxel Plugin SAS. All Rights Reserved.
// Pawn 想给玩家弹一条提示，但看不到 UI 模块时用的接口。

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "StellarEraNoticeSink.generated.h"

UINTERFACE(MinimalAPI, meta = (CannotImplementInterfaceInBlueprint))
class UStellarEraNoticeSink : public UInterface
{
	GENERATED_BODY()
};

/**
 * 一条转瞬即逝的屏幕提示（"巡航：开" / "飞船调试视角：关"）。
 *
 * 为什么需要它：有些提示的唯一触发点是 Pawn 自己的输入回调 —— 巡航和调试视角
 * 都是飞船的状态，切换本身不带任何画面变化，不提示的话玩家分不清按没按到。
 * 但弹提示的 AStellarEraHUD 在 StellarEraUI，而本模块是它的上游（UI → Pawns），
 * Pawn 不能 include HUD，否则成环。
 *
 * 所以声明在本模块（消费方在 Pawns、实现在 UI），由 Pawn 通过
 * `GetController<APlayerController>()->GetHUD()` 取到 HUD 再 Cast 成接口。
 *
 * 有 Canvas 的 HUD 才算数：拿不到就静默跳过，提示是锦上添花，不该让控制逻辑变复杂。
 */
class STELLARERAPAWNS_API IStellarEraNoticeSink
{
	GENERATED_BODY()

public:
	/** 弹一条提示，几秒后自动消失。重复调用替换上一条，不排队。 */
	virtual void ShowNotice(const FString& Text) = 0;
};
