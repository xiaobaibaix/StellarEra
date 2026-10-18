#pragma once

#include "CoreMinimal.h"

/**
 * 行星大气的参数推导。
 *
 * 两条互相独立的规则，别混：
 *
 *  - 引擎大气（ASkyAtmosphere，全场一个）：用几何缩放模型。
 *    长度 ×s、系数 ×(1/s) 时辐射传输方程完全不变，所以这是"物理正确"的缩放。
 *    s = 行星半径km / 6360。
 *
 *  - 壳（每颗行星一个，自写散射）：用观感缩放。
 *    壳的厚度是观感参数（比物理厚度厚约 5.3 倍，否则从太空看是一条看不见的线），
 *    所以系数不能跟着几何走，而是让垂直光学厚度 τ = β × H 与地球相等。
 *
 * 两边唯一必须一致的是**厚度比**，否则玩家穿过壳边界会看到跳变。
 *
 * 本文件不含 UObject，可被任意模块直接单测。
 */
namespace PlanetAtmosphere
{
	// ---- 地球基准。与 USkyAtmosphereComponent 构造函数的默认值一致，
	// ---- 出处 Engine/Private/Components/SkyAtmosphereComponent.cpp:96-118。
	// ---- PlanetAtmosphereParamsTests.cpp 里有一条测试把它们与引擎的默认值对拍，
	// ---- 引擎改默认值会让那条测试变红。
	constexpr float EarthBottomRadiusKm = 6360.0f;
	constexpr float EarthAtmosphereHeightKm = 60.0f;
	constexpr float EarthRayleighScatteringScale = 0.033100f;
	constexpr float EarthMieScatteringScale = 0.003996f;
	constexpr float EarthMieAbsorptionScale = 0.000444f;
	constexpr float EarthRayleighScaleHeightKm = 8.0f;
	constexpr float EarthMieScaleHeightKm = 1.2f;

	/** 引擎把 RayleightScatteringRaw 按 B 通道归一化后存进 RayleighScattering。 */
	inline const FVector3f EarthRayleighScatteringColor(0.175287f, 0.409607f, 1.0f);

	/** 壳半径（cm）= 表面半径 × (1 + 厚度比)。 */
	STELLARERAPLANET_API float ComputeShellRadiusCm(float SurfaceRadiusCm, float AtmosphereHeightRatio);

	/** 引擎原生大气按行星半径缩放后的参数。长度单位 km，系数单位 1/km。 */
	struct FSkyAtmosphereScaleParams
	{
		float BottomRadiusKm = 0.0f;
		float AtmosphereHeightKm = 0.0f;
		float RayleighScatteringScale = 0.0f;
		float MieScatteringScale = 0.0f;
		float MieAbsorptionScale = 0.0f;
		float RayleighExponentialDistributionKm = 0.0f;
		float MieExponentialDistributionKm = 0.0f;
		/**
		 * s = 行星半径km / 6360。
		 *
		 * 同时是**有效性的唯一信号**：半径非正时 ComputeSkyAtmosphereScaleParams
		 * 返回全零结构体，此时 Scale == 0。调用方必须先查它再写引擎组件 ——
		 * AtmosphereHeightKm == 0 会让引擎的 exp(-h/H) 除零。
		 */
		float Scale = 0.0f;
	};

	/**
	 * 几何缩放：长度 ×s、系数 ×(1/s)。
	 *
	 * 但 AtmosphereHeightKm **不**用几何缩放，而是 = 行星半径km × 厚度比。
	 * 这是刻意的：引擎大气的厚度必须与壳一致（都是观感厚度），否则玩家穿过
	 * 壳边界时会看到跳变。该厚度比是地球的 5.3 倍，但 τ = β × H 并不因此改变
	 * —— s 与 1/s 在 τ 里正好抵消（比值 1.0006），无需补偿，AtmosphereCoefficientScale 留在 1.0。
	 */
	STELLARERAPLANET_API FSkyAtmosphereScaleParams ComputeSkyAtmosphereScaleParams(
		float SurfaceRadiusKm, float AtmosphereHeightRatio);

	/** 壳的散射参数。系数单位 1/cm，长度单位 cm。 */
	struct FShellScatteringParams
	{
		FVector3f RayleighBeta = FVector3f::ZeroVector;
		/** 米氏散射 + 吸收的总消光系数（吸收那 11% 折进来一起当散射用，单次散射模型下可忽略）。 */
		FVector3f MieBeta = FVector3f::ZeroVector;
		float RayleighScaleHeightCm = 0.0f;
		float MieScaleHeightCm = 0.0f;
	};

	/**
	 * 壳的散射：标高按壳半径的比例给（观感），系数按"垂直光学厚度等于地球"反推。
	 *
	 * τ = β × H 是无量纲的，也是真正决定观感的量。壳比物理厚度厚约 5.3 倍，
	 * 所以 β 必须相应变薄，观感才对。
	 *
	 * @param ShellRadiusCm             壳半径（cm），来自 ComputeShellRadiusCm
	 * @param RayleighScaleHeightRatio  瑞利标高 / 壳半径，默认 0.015
	 * @param MieScaleHeightRatio       米氏标高 / 壳半径，默认 0.003
	 */
	STELLARERAPLANET_API FShellScatteringParams ComputeShellScatteringParams(
		float ShellRadiusCm, float RayleighScaleHeightRatio, float MieScaleHeightRatio);

} // namespace PlanetAtmosphere
