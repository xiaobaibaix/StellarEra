#pragma once

#include "CoreMinimal.h"

/**
 * 行星体积云的纯数学部分。密度计算在材质 HLSL 里，这里只放必须与 C++ 侧算得
 * 一模一样的云层半径与覆盖率阈值。不含 UObject，可直接单测。
 */
namespace PlanetClouds
{
	/**
	 * 同时参与着色的太阳槽位数。与 Shaders/PlanetClouds.ush 的 PLANET_MAX_SUNS 是两份
	 * 字面量，测试钉住一致；改它要同步 .ush、组件参数数组、材质 Code 实参，并把
	 * PLANET_CLOUDS_VERSION +1 后重跑 BuildAtmosphereMaterial。想省 GPU 就调小。
	 */
	constexpr int32 MaxSuns = 4;

	/** 云层的内外半径（cm），以行星中心为原点。 */
	struct FCloudLayerRadii
	{
		float BaseCm = 0.0f;
		float TopCm = 0.0f;
	};

	/**
	 * 云层半径 = 表面半径 × (1 + 比例)，以**行星中心**为原点的绝对半径。少乘 (1 + …)
	 * 云层会落到地面以下、密度恒 0。比例按 0 兜底，Top 钳到不小于 Base（否则除法变号）。
	 */
	STELLARERAPLANET_API FCloudLayerRadii ComputeCloudLayerRadiiCm(
		float SurfaceRadiusCm, float BaseRatio, float TopRatio);

	/** 覆盖率 0 时的噪声阈值，即"完全没有云"要越过的门槛。 */
	extern const float CloudNoiseThresholdAtZeroCoverage;

	/** 覆盖率 1 时的噪声阈值，即"处处是云"的门槛。 */
	extern const float CloudNoiseThresholdAtFullCoverage;

	/**
	 * 覆盖率 → 噪声阈值（0 → 0.63，1 → 0.36，钳到 0..1）。不要改成教科书的
	 * `1 - Coverage`：CloudFbm 是钟形分布（均值 0.497、σ 0.050），1 - C 在 0.45 时
	 * 只有三分之一方向穿过阈值且样本很淡，画面是灰雾加暗斑而非云。HLSL 侧用同一式子。
	 */
	STELLARERAPLANET_API float CoverageToNoiseThreshold(float Coverage);

} // namespace PlanetClouds
