#pragma once

#include "CoreMinimal.h"

/**
 * 行星大气参数推导。两套规则别混：引擎大气（ASkyAtmosphere，全场一个）用几何缩放
 * （长度 ×s、系数 ×1/s），s = 行星半径km / 6360；壳（每颗一个，自写散射）用观感缩放，
 * 系数按「垂直光学厚度 τ = β×H 与地球相等」反推。两边唯一必须一致的是厚度比。
 * 本文件不含 UObject，可直接单测。
 */
namespace PlanetAtmosphere
{
	// ---- 地球基准，与 USkyAtmosphereComponent 构造函数默认值一致；测试会与引擎对拍。----
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
		 * s = 行星半径km / 6360。同时是有效性信号：半径非正时返回全零，调用方须先查
		 * Scale 再写引擎组件（AtmosphereHeightKm == 0 会让 exp(-h/H) 除零）。
		 */
		float Scale = 0.0f;
	};

	/**
	 * 几何缩放：长度 ×s、系数 ×(1/s)；但 AtmosphereHeightKm 用观感厚度（半径 × 厚度比），
	 * 必须与壳一致，否则穿壳边界会跳变。s 与 1/s 在 τ 里正好抵消。
	 * 当前无调用方（引擎大气已撤），保留以防以后加回来，别当死代码删。
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
	 * 壳的散射：标高按壳半径比例给（观感），系数按「垂直光学厚度等于地球」反推。
	 * τ = β × H 无量纲、决定观感；壳比物理厚度厚约 5.3 倍，β 须相应变薄。
	 * @param ShellRadiusCm 来自 ComputeShellRadiusCm；两个比例默认 0.008 / 0.0015。
	 */
	STELLARERAPLANET_API FShellScatteringParams ComputeShellScatteringParams(
		float ShellRadiusCm, float RayleighScaleHeightRatio, float MieScaleHeightRatio);

} // namespace PlanetAtmosphere
