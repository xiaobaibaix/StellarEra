// Copyright Voxel Plugin SAS. All Rights Reserved.
// 编辑模式两个笔刷按钮的布局与命中。纯函数，不碰 Canvas。

#pragma once

#include "CoreMinimal.h"

namespace PlanetEditUI
{
	/** 按钮边长（像素）。 */
	constexpr float ButtonSize = 48.f;

	/** 两个按钮之间的间距（像素）。 */
	constexpr float ButtonGap = 8.f;

	/** 按钮底边距屏幕底部的距离（像素）。 */
	constexpr float ButtonBottomMargin = 40.f;

	/** 按钮底边到提示文字的间距（像素）。 */
	constexpr float HintGap = 8.f;

	/**
	 * 两个按钮的左上角（像素）。顺序：先球体、后正方体。
	 * 视口窄到放不下时整体贴左边（X 夹到 0），不会产生负坐标；
	 * 此时右侧按钮可能超出视口右边缘。
	 */
	STELLARERAUI_API void GetBrushButtonRects(const FVector2D& ViewportSize,
		FVector2D& OutSphereTopLeft, FVector2D& OutCubeTopLeft);

	/**
	 * 命中返回 true 并写下标：0 = 球体，1 = 正方体。
	 * **半开区间**：上/左边界算命中，右/下边界不算（与 DrawRect 覆盖的 [X, X+Size)
	 * 一致）；两个按钮之间的空隙不算。
	 * OutButtonIndex 只在返回 true 时写入，返回 false 时保持原值。
	 *
	 * 返回 int32 而不是 EPlanetBrushShape：本头是 StellarEraUI 的公开头，
	 * 而该枚举属于 StellarEraPlanetEdit。调用方在 .cpp 里映射。
	 */
	STELLARERAUI_API bool HitTestBrushButton(const FVector2D& ViewportSize, const FVector2D& ScreenPosition,
		int32& OutButtonIndex);
}
