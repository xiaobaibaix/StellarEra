#include "PlanetSnapshot.h"
#include "GeoRng.h"

namespace
{
	// float 哈希在 R=40000 处分辨率约 0.004 体素，半径略改会撞哈希；按 double 位型混。
	uint64 HashDouble(double V)
	{
		uint64 Bits = 0;
		FMemory::Memcpy(&Bits, &V, sizeof(Bits));
		return GeoHash::Mix64(Bits);
	}
}

void FPlanetSnapshot::FinalizeHash()
{
	uint64 H = GeoHash::Combine(HashDouble(R0), GeoHash::Float(DensityScale));
	H = GeoHash::Combine(H, Radial.Hash());
	H = GeoHash::Combine(H, MohoWobble.Hash());
	H = GeoHash::Combine(H, Primitives.Hash());
	H = GeoHash::Combine(H, Elevation.Hash());
	H = GeoHash::Combine(H, GeoHash::Float(CrustSplitElevation));
	H = GeoHash::Combine(H, HashDouble(MinSurfaceRadius));
	H = GeoHash::Combine(H, HashDouble(MaxSurfaceRadius));
	ContentHash = H;
}

SIZE_T FPlanetSnapshot::GetAllocatedSize() const
{
	return Radial.Layers.GetAllocatedSize() + MohoWobble.GetAllocatedSize() + Primitives.GetAllocatedSize()
		+ Elevation.GetAllocatedSize();
}
