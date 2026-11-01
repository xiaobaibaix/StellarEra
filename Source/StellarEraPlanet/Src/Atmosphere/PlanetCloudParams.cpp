#include "PlanetCloudParams.h"

namespace PlanetClouds
{
	FCloudLayerRadii ComputeCloudLayerRadiiCm(float SurfaceRadiusCm, float BaseRatio, float TopRatio)
	{
		FCloudLayerRadii Out;
		Out.BaseCm = SurfaceRadiusCm * FMath::Max(BaseRatio, 0.0f);
		Out.TopCm = SurfaceRadiusCm * FMath::Max(TopRatio, 0.0f);
		if (Out.TopCm < Out.BaseCm)
		{
			Out.TopCm = Out.BaseCm;
		}
		return Out;
	}

	float CoverageToNoiseThreshold(float Coverage)
	{
		return 1.0f - FMath::Clamp(Coverage, 0.0f, 1.0f);
	}

} // namespace PlanetClouds
