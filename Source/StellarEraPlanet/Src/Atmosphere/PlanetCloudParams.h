#pragma once

#include "CoreMinimal.h"

/**
 * 行星体积云的纯数学部分。
 *
 * 与 PlanetAtmosphereParams 一样不含 UObject，可被任意模块直接单测。
 * 真正的密度计算在材质 HLSL 里（PlanetAtmosphereMaterialBuilder.cpp），
 * 这里只放"必须和 C++ 侧算得一模一样"的那两件事：云层半径、覆盖率阈值。
 */
namespace PlanetClouds
{
	/** 云层的内外半径（cm），以行星中心为原点。 */
	struct FCloudLayerRadii
	{
		float BaseCm = 0.0f;
		float TopCm = 0.0f;
	};

	/**
	 * 云层半径 = 表面半径 × 比例。
	 *
	 * 比例按 0 兜底（负比例没有物理意义），Top 被钳到不小于 Base ——
	 * 比例写反时得到的是零厚度云层而不是负厚度，后者会让 HLSL 里的
	 * 除法变号，云会长到地面以下。
	 */
	STELLARERAPLANET_API FCloudLayerRadii ComputeCloudLayerRadiiCm(
		float SurfaceRadiusCm, float BaseRatio, float TopRatio);

	/**
	 * 覆盖率 → 噪声阈值，公式是 `1 - Coverage`（钳到 0..1）。
	 *
	 * HLSL 里用的就是这个式子，两边必须一致：不一致的症状是
	 * "组件上调了覆盖率，画面上看不出变化"。
	 */
	STELLARERAPLANET_API float CoverageToNoiseThreshold(float Coverage);

} // namespace PlanetClouds
