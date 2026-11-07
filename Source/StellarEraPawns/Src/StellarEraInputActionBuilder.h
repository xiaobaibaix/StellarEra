// Copyright Voxel Plugin SAS. All Rights Reserved.
// 造运行时输入动作与按键映射的小工具，供各 Pawn 的动作表共用。

#pragma once

#include "CoreMinimal.h"
#include "InputActionValue.h"

class UInputAction;
class UInputMappingContext;
class UInputModifier;
struct FKey;

namespace StellarEraInput
{
	/**
	 * 左键「点击」与「拖拽」的分界（像素）。角色编辑点放与星图拾取共用，
	 * 分开放会让两处行为出现查不出来的偏差。
	 */
	constexpr double ClickMoveThresholdPx = 5.0;

	/** 造一个运行时输入动作。ValueType 决定回调里 Get<T>() 的模板参数。 */
	STELLARERAPAWNS_API UInputAction* MakeAction(
		UObject* Outer,
		const TCHAR* Name,
		EInputActionValueType ValueType);

	/** 把一个按键映射到动作，并按顺序挂上修饰器。 */
	STELLARERAPAWNS_API void MapKey(
		UInputMappingContext* Context,
		UInputAction* Action,
		const FKey& Key,
		const TArray<UInputModifier*>& Modifiers = TArray<UInputModifier*>());

	STELLARERAPAWNS_API UInputModifier* MakeNegate(UObject* Outer);

	/** Swizzle(YXZ) 把按键的 X 轴值搬到 Y 轴，用于把 WASD 合成一个二维向量。 */
	STELLARERAPAWNS_API UInputModifier* MakeSwizzleYXZ(UObject* Outer);
}
