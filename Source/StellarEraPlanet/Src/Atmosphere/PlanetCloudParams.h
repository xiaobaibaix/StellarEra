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
	/**
	 * 同时参与着色的太阳槽位数。
	 *
	 * 与 `Shaders/PlanetClouds.ush` 的 `#define PLANET_MAX_SUNS 4` 是**两份字面量**，
	 * 靠测试钉住（`PlanetAtmosphereMaterialTests` 断言 .ush 文本里写的是 4）。
	 * 两者不一致的症状是「第 4 颗星在材质里根本不存在」。
	 *
	 * 改这个值要同时动四处，少一处就编译失败或测试变红：
	 *   1. 这里；
	 *   2. `Shaders/PlanetClouds.ush` 的 `PLANET_MAX_SUNS`；
	 *   3. `PlanetAtmosphereComponent.cpp` 顶部 `ParamSunDirections` /
	 *      `ParamSunRadiances` 的 4 项初始化列表；
	 *   4. `Source/StellarEraPlanet/Src/Tests/PlanetAtmosphereMaterialTests.cpp`
	 *      里三处对 4 的断言（`MaxSuns`、`.ush` 文本、`SunDirection3` 参数存在）。
	 *
	 * 降级旋钮：想省 GPU 就按下调小（关卡正好 3 颗）。
	 */
	constexpr int32 MaxSuns = 4;

	/** 云层的内外半径（cm），以行星中心为原点。 */
	struct FCloudLayerRadii
	{
		float BaseCm = 0.0f;
		float TopCm = 0.0f;
	};

	/**
	 * 云层半径 = 表面半径 × (1 + 比例)，即以**行星中心**为原点的绝对半径。
	 *
	 * 比例是高度占表面半径的份额，与 ComputeShellRadiusCm 同一约定。
	 * 少乘这个 (1 + …) 会让云层落到地面以下，HLSL 里密度恒 0。
	 * 比例按 0 兜底（负比例没有物理意义），Top 被钳到不小于 Base ——
	 * 比例写反时得到的是零厚度云层而不是负厚度，后者会让 HLSL 里的
	 * 除法变号。
	 */
	STELLARERAPLANET_API FCloudLayerRadii ComputeCloudLayerRadiiCm(
		float SurfaceRadiusCm, float BaseRatio, float TopRatio);

	/** 覆盖率 0 时的噪声阈值，即"完全没有云"要越过的门槛。 */
	extern const float CloudNoiseThresholdAtZeroCoverage;

	/** 覆盖率 1 时的噪声阈值，即"处处是云"的门槛。 */
	extern const float CloudNoiseThresholdAtFullCoverage;

	/**
	 * 覆盖率 → 噪声阈值，在噪声的实际取值区间上线性映射
	 * （Coverage 0 → 0.63，1 → 0.36，钳到 0..1）。
	 *
	 * **不要改成教科书的 `1 - Coverage`。** 那个式子假设 w 在 [0,1] 上均匀，
	 * 而 CloudFbm 是若干八度相加，分布是钟形：实测均值 0.497、标准差 0.050、
	 * 取值区间 [0.243, 0.631]。用 `1 - Coverage` 时 Coverage=0.45 落在均值
	 * +1σ，只有三分之一的方向穿过阈值，且穿过的样本平均密度只有 0.07 ——
	 * 画面上是一层灰雾加暗斑，而不是云。
	 *
	 * HLSL 里用的就是这个式子（PlanetClouds.ush 的 CLOUD_THRESHOLD_AT_*），
	 * 两边必须一致：不一致的症状是"组件上调了覆盖率，画面上看不出变化"。
	 */
	STELLARERAPLANET_API float CoverageToNoiseThreshold(float Coverage);

} // namespace PlanetClouds
