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
	 * 两个按钮的左上角（像素），先球体后正方体。视口窄到放不下时整体贴左边（X 夹到 0），
	 * 此时右侧按钮可能超出视口右边缘。
	 */
	STELLARERAPLANET_API void GetBrushButtonRects(const FVector2D& ViewportSize,
		FVector2D& OutSphereTopLeft, FVector2D& OutCubeTopLeft);

	/**
	 * 命中返回 true 并写下标：0 = 球体，1 = 正方体。半开区间：上/左边界算命中，右/下不算
	 * （与 DrawRect 的 [X, X+Size) 一致），两按钮之间的空隙不算。OutButtonIndex 只在 true 时写。
	 * 返回 int32 而非 EPlanetBrushShape：命中判定是纯几何，调用方各自映射。
	 */
	STELLARERAPLANET_API bool HitTestBrushButton(const FVector2D& ViewportSize, const FVector2D& ScreenPosition,
		int32& OutButtonIndex);
}
