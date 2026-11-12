#include "PlanetSnapshot.h"
#include "GeoRng.h"

void FPlanetSnapshot::FinalizeHash()
{
	uint64 H = GeoHash::Combine(GeoHash::Float(float(R0)), GeoHash::Float(DensityScale));
	H = GeoHash::Combine(H, Radial.Hash());
	H = GeoHash::Combine(H, MohoWobble.Hash());
	H = GeoHash::Combine(H, Primitives.Hash());
	H = GeoHash::Combine(H, GeoHash::Float(float(MinSurfaceRadius)));
	H = GeoHash::Combine(H, GeoHash::Float(float(MaxSurfaceRadius)));
	ContentHash = H;
}

SIZE_T FPlanetSnapshot::GetAllocatedSize() const
{
	return Radial.Layers.GetAllocatedSize() + MohoWobble.GetAllocatedSize() + Primitives.GetAllocatedSize();
}
