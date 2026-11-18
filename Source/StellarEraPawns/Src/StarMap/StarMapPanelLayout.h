// Copyright Voxel Plugin SAS. All Rights Reserved.
// 星图面板的布局几何：贴边放置、矿物图例的固定布局与命中。纯函数，不碰 Canvas。

#pragma once

#include "CoreMinimal.h"

/** 面板矩形（Canvas 像素）。 */
struct FStarMapPanelRect
{
	float X = 0.f;
	float Y = 0.f;
	float W = 0.f;
	float H = 0.f;
};


/** 星图面板的布局几何：绘制在 UI、命中在星图输入组件，两边调同一份。 */
namespace StarMapPanelLayout
{
	/**
	 * 把面板放在锚点右侧；右侧放不下翻到左侧，最后整体夹进视口。
	 * 鼠标锚点传 AnchorRadiusPx = 0，天体锚点传屏幕轮廓半径。
	 */
	STELLARERAPAWNS_API FStarMapPanelRect PlacePanelBesideAnchor(
		const FVector2D& AnchorPx,
		float AnchorRadiusPx,
		const FVector2D& PanelSizePx,
		const FVector2D& ViewportSizePx,
		float GapPx,
		float MarginPx);

}
