// Copyright Voxel Plugin SAS. All Rights Reserved.

#include "StarMapPanelLayout.h"

FStarMapPanelRect StarMapPanelLayout::PlacePanelBesideAnchor(
	const FVector2D& AnchorPx,
	float AnchorRadiusPx,
	const FVector2D& PanelSizePx,
	const FVector2D& ViewportSizePx,
	float GapPx,
	float MarginPx)
{
	FStarMapPanelRect Rect;
	Rect.W = float(PanelSizePx.X);
	Rect.H = float(PanelSizePx.Y);

	const float RightX = float(AnchorPx.X) + AnchorRadiusPx + GapPx;
	const float LeftX = float(AnchorPx.X) - AnchorRadiusPx - GapPx - Rect.W;
	const float MaxX = FMath::Max(MarginPx, float(ViewportSizePx.X) - MarginPx - Rect.W);
	const float MaxY = FMath::Max(MarginPx, float(ViewportSizePx.Y) - MarginPx - Rect.H);

	Rect.X = RightX;
	if (RightX + Rect.W > float(ViewportSizePx.X) - MarginPx && LeftX >= MarginPx)
	{
		Rect.X = LeftX;
	}

	Rect.X = FMath::Clamp(Rect.X, MarginPx, MaxX);
	Rect.Y = FMath::Clamp(float(AnchorPx.Y) - Rect.H * 0.5f, MarginPx, MaxY);
	return Rect;
}

void StarMapPanelLayout::ComputeMineralLegendLayout(
	const FVector2D& ViewportSizePx,
	const int32 EntryCount,
	FStarMapPanelRect& OutPanel,
	TArray<FStarMapPanelRect>& OutRowRects)
{
	OutPanel = PlacePanelBesideAnchor(
		FVector2D(MineralLegendLayout::MarginPx, MineralLegendLayout::MarginPx),
		0.f,
		FVector2D(MineralLegendLayout::PanelWidthPx,
			MineralLegendLayout::PanelHeightPx(EntryCount)),
		ViewportSizePx, 0.f, MineralLegendLayout::MarginPx);

	const int32 SlotCount = FMath::Max(EntryCount, 1);
	OutRowRects.Reset();
	OutRowRects.Reserve(SlotCount);

	// 行区起点 = 内边距 + 标题 + 上分隔块（与 DrawMineralLegend 同一处算式）。
	const float RowsTop = OutPanel.Y + MineralLegendLayout::Padding
		+ MineralLegendLayout::TitleLineHeight
		+ MineralLegendLayout::TitleToRowsTopOffset();

	for (int32 SlotIndex = 0; SlotIndex < SlotCount; ++SlotIndex)
	{
		FStarMapPanelRect& Row = OutRowRects.AddDefaulted_GetRef();
		Row.X = OutPanel.X + MineralLegendLayout::Padding;
		Row.Y = RowsTop + SlotIndex * MineralLegendLayout::RowSlotHeight;
		Row.W = OutPanel.W - MineralLegendLayout::Padding * 2.f;
		Row.H = MineralLegendLayout::RowSlotHeight;
	}
}

int32 StarMapPanelLayout::HitTestMineralLegendRow(
	const TArray<FStarMapPanelRect>& RowRects,
	const FVector2D& ScreenPosPx)
{
	for (int32 RowIndex = 0; RowIndex < RowRects.Num(); ++RowIndex)
	{
		const FStarMapPanelRect& Row = RowRects[RowIndex];
		// 上下各缩 HitInsetY：两行之间没有缝，不缩边会让接缝算作命中。
		if (ScreenPosPx.X >= Row.X && ScreenPosPx.X <= Row.X + Row.W
			&& ScreenPosPx.Y >= Row.Y + MineralLegendLayout::HitInsetY
			&& ScreenPosPx.Y <= Row.Y + Row.H - MineralLegendLayout::HitInsetY)
		{
			return RowIndex;
		}
	}
	return INDEX_NONE;
}
