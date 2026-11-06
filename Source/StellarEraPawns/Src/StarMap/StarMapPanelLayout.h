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

/**
 * 矿物观察图例的固定布局。
 *
 * 宽度与字体无关：输入回调（鼠标命中）里拿不到 AHUD::Canvas，量不了字，
 * 所以宽度写死、高度只随行数变。绘制与命中各自调一次，几何完全一致。
 *
 * 住在 Pawns 而不是 UI：绘制在 UI（HUD）、命中在星图相机的输入组件（Pawns），
 * 两边共用同一份几何。UI 依赖 Pawns，方向合法；反过来则不行。
 */
namespace MineralLegendLayout
{
	constexpr float Padding = 10.f;
	constexpr float SeparatorSpacing = 6.f;
	constexpr float SeparatorThickness = 1.f;
	/** 每行占的高度。行与行之间没有缝，命中判定直接用这个矩形。 */
	constexpr float RowSlotHeight = 24.f;
	/** 标题 / 提示行高：固定值，不量字体。 */
	constexpr float TitleLineHeight = 22.f;
	constexpr float HintLineHeight = 22.f;
	/** 面板宽度：按最长的一行提示（约 34 个汉字宽）留出的固定值。 */
	constexpr float PanelWidthPx = 460.f;
	constexpr float MarginPx = 8.f;
	/** 命中判定的上下缩进：点在两行接缝上时不误命中相邻行。 */
	constexpr float HitInsetY = 2.f;

	/**
	 * 标题文字下沿 → 第一行顶部：上下各留一次间距，中间夹一条分隔线。
	 * 面板高度预算与 ComputeMineralLegendLayout 的行起点共用这一处算式。
	 */
	FORCEINLINE float TitleToRowsTopOffset()
	{
		return SeparatorSpacing + SeparatorThickness + SeparatorSpacing;
	}

	/**
	 * 最后一行下沿 → 提示文字顶部：与标题段同构（间距 / 分隔线 / 间距）。
	 * 面板高度预算与绘制时的提示区块共用这一处算式。
	 */
	FORCEINLINE float RowsBottomToHintTopOffset()
	{
		return SeparatorSpacing + SeparatorThickness + SeparatorSpacing;
	}

	/**
	 * 面板高度（夹进视口之前）。EntryCount = 0 时仍留一行占位。
	 *
	 * 两段分隔块都要计入：只算 SeparatorSpacing + SeparatorThickness 会短 12 px，
	 * 缺口全挤在「最后一行 ↔ 提示行」之间，提示分隔线会画进最后一行。
	 */
	FORCEINLINE float PanelHeightPx(int32 EntryCount)
	{
		const int32 Slots = EntryCount > 0 ? EntryCount : 1;
		return Padding * 2.f + TitleLineHeight + RowSlotHeight * Slots + HintLineHeight
			+ TitleToRowsTopOffset() + RowsBottomToHintTopOffset();
	}

	/** 提示文字顶部的 Y：贴面板底，只留 Padding。 */
	FORCEINLINE float HintTextTopY(const FStarMapPanelRect& Panel)
	{
		return Panel.Y + Panel.H - Padding - HintLineHeight;
	}

	/** 提示分隔线的 Y：位于提示文字上方的一个间距块之后（与标题段同构）。 */
	FORCEINLINE float HintSeparatorY(const FStarMapPanelRect& Panel)
	{
		return HintTextTopY(Panel) - SeparatorSpacing - SeparatorThickness;
	}
}

/**
 * 星图面板的布局几何。绘制在 UI、命中在星图输入组件，两边调同一份。
 */
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

	/** 算面板与每一行的矩形。EntryCount = 0 时 OutRowRects 里仍有一行（占位行）。 */
	STELLARERAPAWNS_API void ComputeMineralLegendLayout(
		const FVector2D& ViewportSizePx,
		int32 EntryCount,
		FStarMapPanelRect& OutPanel,
		TArray<FStarMapPanelRect>& OutRowRects);

	/** 屏幕坐标命中哪一行；没命中返回 INDEX_NONE。 */
	STELLARERAPAWNS_API int32 HitTestMineralLegendRow(
		const TArray<FStarMapPanelRect>& RowRects,
		const FVector2D& ScreenPosPx);
}
