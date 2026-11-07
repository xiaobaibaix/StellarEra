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
 * HUD 在 UI 模块（Pawns 的上游），Pawn 只能从
 * `GetController<APlayerController>()->GetHUD()` 取到再 Cast 成接口。
 * 拿不到就静默跳过 —— 提示是锦上添花，不该影响控制逻辑。
 */
class STELLARERAPAWNS_API IStellarEraNoticeSink
{
	GENERATED_BODY()

public:
	/** 弹一条提示，几秒后自动消失。重复调用替换上一条，不排队。 */
	virtual void ShowNotice(const FString& Text) = 0;
};
