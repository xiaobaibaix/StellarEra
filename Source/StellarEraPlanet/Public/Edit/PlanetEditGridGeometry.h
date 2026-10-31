// Copyright Voxel Plugin SAS. All Rights Reserved.
// 表面网格的几何生成与背面剔除：纯函数，不碰体素数据。

#pragma once

#include "CoreMinimal.h"

/**
 * 一条经纬网格线。
 *
 * Directions 是单位球方向（行星体素空间；世界与体素空间只差等比缩放，方向可直接共用）。
 * bClosed = true 表示首尾相接（纬线）；经线是开放的折线。
 */
struct FPlanetEditGridLine
{
	TArray<FVector> Directions;
	bool bClosed = false;
};

namespace PlanetEditGrid
{
	/**
	 * 按步长生成经纬线。步长夹到 [1, 90] 度。
	 *
	 * 经线：固定经度，纬度从 -90° 到 +90°（含两极），每条 LatCount+1 个点。
	 * 纬线：跳过两极（在极点会退化成重复点），每条 LonCount 个点并闭合。
	 * 数量：经线 LonCount 条、纬线 LatCount-1 条。
	 */
	STELLARERAPLANET_API void BuildLines(float StepDegrees, TArray<FPlanetEditGridLine>& OutLines);

	/** 顶点方向与「球心 → 相机」方向同向（点积 > 0）即可见。 */
	STELLARERAPLANET_API bool IsDirectionVisible(const FVector& UnitDirection, const FVector& CameraDirection);

	/** 线段只要有一端可见就整段提交；两端都在背面才剔除。 */
	STELLARERAPLANET_API bool IsSegmentVisible(const FVector& UnitDirectionA, const FVector& UnitDirectionB,
		const FVector& CameraDirection);
}
