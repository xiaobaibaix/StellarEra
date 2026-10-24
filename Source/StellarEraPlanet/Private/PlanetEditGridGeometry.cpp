// Copyright Voxel Plugin SAS. All Rights Reserved.

#include "PlanetEditGridGeometry.h"

namespace
{
	FVector MakeDirection(const float LatDeg, const float LonDeg)
	{
		const float LatRad = FMath::DegreesToRadians(LatDeg);
		const float LonRad = FMath::DegreesToRadians(LonDeg);
		const float CosLat = FMath::Cos(LatRad);
		return FVector(
			CosLat * FMath::Cos(LonRad),
			CosLat * FMath::Sin(LonRad),
			FMath::Sin(LatRad));
	}
}

void PlanetEditGrid::BuildLines(const float StepDegrees, TArray<FPlanetEditGridLine>& OutLines)
{
	OutLines.Reset();

	const float Step = FMath::Clamp(StepDegrees, 1.f, 90.f);

	// 用 RoundToInt 而不是 floor：360/90 = 4 必须得到 4 条经线。
	// 实际步长重新均分，保证正好绕一圈。
	const int32 LonCount = FMath::Max(1, FMath::RoundToInt(360.f / Step));
	const int32 LatCount = FMath::Max(1, FMath::RoundToInt(180.f / Step));
	const float LonStep = 360.f / LonCount;
	const float LatStep = 180.f / LatCount;

	// ── 经线 ──
	for (int32 LonIndex = 0; LonIndex < LonCount; ++LonIndex)
	{
		const float LonDeg = -180.f + LonIndex * LonStep;

		FPlanetEditGridLine Line;
		Line.bClosed = false;
		Line.Directions.Reserve(LatCount + 1);
		for (int32 LatIndex = 0; LatIndex <= LatCount; ++LatIndex)
		{
			Line.Directions.Add(MakeDirection(-90.f + LatIndex * LatStep, LonDeg));
		}
		OutLines.Add(MoveTemp(Line));
	}

	// ── 纬线 ──
	// 跳过两极：极点处的纬线所有点重合，画出来是一条零长度线段。
	for (int32 LatIndex = 1; LatIndex < LatCount; ++LatIndex)
	{
		const float LatDeg = -90.f + LatIndex * LatStep;

		FPlanetEditGridLine Line;
		Line.bClosed = true;
		Line.Directions.Reserve(LonCount);
		for (int32 LonIndex = 0; LonIndex < LonCount; ++LonIndex)
		{
			Line.Directions.Add(MakeDirection(LatDeg, -180.f + LonIndex * LonStep));
		}
		OutLines.Add(MoveTemp(Line));
	}
}

bool PlanetEditGrid::IsDirectionVisible(
	const FVector& UnitDirection,
	const FVector& CameraDirection)
{
	// 严格大于 0：正好切于屏幕边缘的方向不画，避免接缝处一条线闪。
	return FVector::DotProduct(UnitDirection, CameraDirection) > 0.0;
}

bool PlanetEditGrid::IsSegmentVisible(
	const FVector& UnitDirectionA,
	const FVector& UnitDirectionB,
	const FVector& CameraDirection)
{
	return IsDirectionVisible(UnitDirectionA, CameraDirection) ||
		IsDirectionVisible(UnitDirectionB, CameraDirection);
}
