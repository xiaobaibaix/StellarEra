#pragma once

#include "CoreMinimal.h"

/**
 * 引擎原生 SkyAtmosphere 全场景只能有一套（FScene 只有单个
 * FSkyAtmosphereRenderSceneInfo*），所以它逐帧被搬到"离相机最近"的那颗行星。
 *
 * "最近"用**表面距离** |相机 - 球心| - 半径，不是球心距离 ——
 * 否则大行星会把相机贴在自己表面时仍被判定为"更远"。
 */
namespace PlanetAtmosphereTargeting
{
	/**
	 * @param Centers        各行星的球心世界坐标
	 * @param SurfaceRadiiCm 各行星的表面半径（cm），与 Centers 一一对应
	 * @param CameraPos      相机世界坐标
	 * @return 表面距离最小的下标；数组为空或长度不匹配时返回 INDEX_NONE
	 */
	STELLARERA_API int32 FindNearestPlanet(const TArray<FVector>& Centers,
		const TArray<float>& SurfaceRadiiCm, const FVector& CameraPos);

} // namespace PlanetAtmosphereTargeting
