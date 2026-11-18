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

