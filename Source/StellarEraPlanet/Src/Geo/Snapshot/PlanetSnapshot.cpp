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
	for (const FGeoMineralOverlay& O : MineralOverlays) { H = GeoHash::Combine(H, O.Hash()); }
	H = GeoHash::Combine(H, Elevation.Hash());
	for (const FGeoVolcano& V : Volcanoes) { H = GeoHash::Combine(H, V.Hash()); }
	H = GeoHash::Combine(H, GeoHash::Float(CrustSplitElevation));
	// 全部原语的最深点：按包围球算，宁大勿小，避免剔掉真实数据。
	PrimitiveDepthMaxRatio = 0.f;
	for (const FGeoPrimitive& P : Primitives.GetPrimitives())
	{
		const double Deepest = double(P.Center.Size()) - double(P.Radii.GetMax());
		PrimitiveDepthMaxRatio = FMath::Max(PrimitiveDepthMaxRatio, float((R0 - Deepest) / FMath::Max(R0, 1.0)));
	}
	for (const float B : AlterBands) { H = GeoHash::Combine(H, GeoHash::Float(B)); }
	H = GeoHash::Combine(H, HashDouble(MinSurfaceRadius));
	H = GeoHash::Combine(H, HashDouble(MaxSurfaceRadius));
	ContentHash = H;
}

SIZE_T FPlanetSnapshot::GetAllocatedSize() const
{
	return Radial.Layers.GetAllocatedSize() + MohoWobble.GetAllocatedSize() + Primitives.GetAllocatedSize()
		+ Elevation.GetAllocatedSize() + Volcanoes.GetAllocatedSize() + MineralOverlaysSize();
}

SIZE_T FPlanetSnapshot::MineralOverlaysSize() const
{
	SIZE_T Size = MineralOverlays.GetAllocatedSize();
	for (const FGeoMineralOverlay& O : MineralOverlays) { Size += O.Mask.GetAllocatedSize(); }
	return Size;
}
