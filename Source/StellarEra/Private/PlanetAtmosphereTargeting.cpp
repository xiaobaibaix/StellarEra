#include "PlanetAtmosphereTargeting.h"

namespace PlanetAtmosphereTargeting
{

int32 FindNearestPlanet(const TArray<FVector>& Centers, const TArray<float>& SurfaceRadiiCm,
	const FVector& CameraPos)
{
	if (Centers.Num() == 0 || Centers.Num() != SurfaceRadiiCm.Num())
	{
		return INDEX_NONE;
	}

	int32 BestIndex = INDEX_NONE;
	double BestSurfaceDistance = 0.0;

	for (int32 Index = 0; Index < Centers.Num(); ++Index)
	{
		const double SurfaceDistance =
			FVector::Dist(CameraPos, Centers[Index]) - static_cast<double>(SurfaceRadiiCm[Index]);

		if (BestIndex == INDEX_NONE || SurfaceDistance < BestSurfaceDistance)
		{
			BestIndex = Index;
			BestSurfaceDistance = SurfaceDistance;
		}
	}

	return BestIndex;
}

} // namespace PlanetAtmosphereTargeting
