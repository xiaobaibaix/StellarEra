#include "PlanetAtmosphereParams.h"

namespace PlanetAtmosphere
{

float ComputeShellRadiusCm(float SurfaceRadiusCm, float AtmosphereHeightRatio)
{
	return SurfaceRadiusCm * (1.0f + AtmosphereHeightRatio);
}

FSkyAtmosphereScaleParams ComputeSkyAtmosphereScaleParams(
	float SurfaceRadiusKm, float AtmosphereHeightRatio)
{
	FSkyAtmosphereScaleParams Out;

	// 几何缩放因子。行星半径 0.75 km 时 s = 1/8480。
	const float Scale = SurfaceRadiusKm / EarthBottomRadiusKm;
	if (Scale <= 0.0f)
	{
		// 半径非正无法缩放。返回全零，调用方应据此跳过写入。
		return Out;
	}
	const float InverseScale = 1.0f / Scale;

	Out.Scale = Scale;

	// 长度：半径直接用行星的；厚度用观感厚度比（必须与壳一致，见头文件说明）。
	Out.BottomRadiusKm = SurfaceRadiusKm;
	Out.AtmosphereHeightKm = SurfaceRadiusKm * AtmosphereHeightRatio;

	// 系数：×(1/s)。
	Out.RayleighScatteringScale = EarthRayleighScatteringScale * InverseScale;
	Out.MieScatteringScale = EarthMieScatteringScale * InverseScale;
	Out.MieAbsorptionScale = EarthMieAbsorptionScale * InverseScale;

	// 标高也是长度：×s。
	Out.RayleighExponentialDistributionKm = EarthRayleighScaleHeightKm * Scale;
	Out.MieExponentialDistributionKm = EarthMieScaleHeightKm * Scale;

	return Out;
}

FShellScatteringParams ComputeShellScatteringParams(
	float ShellRadiusCm, float RayleighScaleHeightRatio, float MieScaleHeightRatio)
{
	FShellScatteringParams Out;

	if (ShellRadiusCm <= 0.0f || RayleighScaleHeightRatio <= 0.0f || MieScaleHeightRatio <= 0.0f)
	{
		return Out;
	}

	Out.RayleighScaleHeightCm = ShellRadiusCm * RayleighScaleHeightRatio;
	Out.MieScaleHeightCm = ShellRadiusCm * MieScaleHeightRatio;

	// 地球的垂直光学厚度，无量纲。
	const float EarthTauRayleigh = EarthRayleighScatteringScale * EarthRayleighScaleHeightKm;
	const float EarthTauMie =
		(EarthMieScatteringScale + EarthMieAbsorptionScale) * EarthMieScaleHeightKm;

	// 反推系数：τ = β × H → β = τ / H。
	// 地球的 RayleighScatteringScale 是 B 通道的原始值，所以这里得到的是 B 通道系数，
	// 再乘归一化颜色得到 RGB。
	const float RayleighBetaB = EarthTauRayleigh / Out.RayleighScaleHeightCm;
	Out.RayleighBeta = EarthRayleighScatteringColor * RayleighBetaB;

	const float MieBetaScalar = EarthTauMie / Out.MieScaleHeightCm;
	Out.MieBeta = FVector3f(MieBetaScalar, MieBetaScalar, MieBetaScalar);

	return Out;
}

} // namespace PlanetAtmosphere
