// Copyright Voxel Plugin SAS. All Rights Reserved.

#include "PlanetEditUI.h"

namespace
{
	// 参数用 double：FVector2D 的分量在 UE5 里是 double，直接传给 float 参数
	// 会触发 C4244，而本项目警告即错误。
	double GetLeft(const double ViewportWidth)
	{
		const double TotalWidth = PlanetEditUI::ButtonSize * 2.0 + PlanetEditUI::ButtonGap;
		return FMath::Max(0.0, (ViewportWidth - TotalWidth) * 0.5);
	}

	double GetTop(const double ViewportHeight)
	{
		return FMath::Max(0.0,
			ViewportHeight - PlanetEditUI::ButtonBottomMargin - PlanetEditUI::ButtonSize);
	}
}

void PlanetEditUI::GetBrushButtonRects(
	const FVector2D& ViewportSize,
	FVector2D& OutSphereTopLeft,
	FVector2D& OutCubeTopLeft)
{
	const double Left = GetLeft(ViewportSize.X);
	const double Top = GetTop(ViewportSize.Y);

	OutSphereTopLeft = FVector2D(Left, Top);
	OutCubeTopLeft = FVector2D(Left + ButtonSize + ButtonGap, Top);
}

bool PlanetEditUI::HitTestBrushButton(
	const FVector2D& ViewportSize,
	const FVector2D& ScreenPosition,
	int32& OutButtonIndex)
{
	const double Left = GetLeft(ViewportSize.X);
	const double Top = GetTop(ViewportSize.Y);

	// 半开区间，与 DrawRect 覆盖的 [X, X+Size) 一致：上/左边界算，右/下边界不算。
	if (ScreenPosition.Y < Top || ScreenPosition.Y >= Top + ButtonSize)
	{
		return false;
	}

	if (ScreenPosition.X >= Left && ScreenPosition.X < Left + ButtonSize)
	{
		OutButtonIndex = 0;
		return true;
	}

	const double CubeLeft = Left + ButtonSize + ButtonGap;
	if (ScreenPosition.X >= CubeLeft && ScreenPosition.X < CubeLeft + ButtonSize)
	{
		OutButtonIndex = 1;
		return true;
	}

	return false;
}
