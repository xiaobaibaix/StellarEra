#include "PlanetCloudParams.h"

namespace PlanetClouds
{
	FCloudLayerRadii ComputeCloudLayerRadiiCm(float SurfaceRadiusCm, float BaseRatio, float TopRatio)
	{
		FCloudLayerRadii Out;
		// 比例是高度占表面半径的份额，不是绝对半径的倍数 —— 与 ComputeShellRadiusCm 的
		// (1 + 比例) 同一约定。漏掉 +1 云层就落在地面以下，密度恒 0，画面上完全看不见云。
		Out.BaseCm = SurfaceRadiusCm * (1.0f + FMath::Max(BaseRatio, 0.0f));
		Out.TopCm = SurfaceRadiusCm * (1.0f + FMath::Max(TopRatio, 0.0f));
		if (Out.TopCm < Out.BaseCm)
		{
			Out.TopCm = Out.BaseCm;
		}
		return Out;
	}

	const float CloudNoiseThresholdAtZeroCoverage = 0.63f;
	const float CloudNoiseThresholdAtFullCoverage = 0.36f;

	float CoverageToNoiseThreshold(float Coverage)
	{
		const float C = FMath::Clamp(Coverage, 0.0f, 1.0f);
		return FMath::Lerp(CloudNoiseThresholdAtZeroCoverage, CloudNoiseThresholdAtFullCoverage, C);
	}

} // namespace PlanetClouds
