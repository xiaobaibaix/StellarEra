// 径向剖面：深度比例 → 岩性。层按 BottomDepthRatio 升序，最后一层底界为 1（球心）。
#pragma once
#include "CoreMinimal.h"
#include "GeoRng.h"

struct FGeoProfileLayer
{
	float BottomDepthRatio = 1.f;
	uint8 LithologyId = 0;
	/** 本层底界随方向的起伏（占 R）。必须小于相邻层厚度的一半，否则层序翻转。 */
	float WobbleRatio = 0.f;
};

struct FGeoProfile
{
	TArray<FGeoProfileLayer> Layers;

	FORCEINLINE uint8 LithologyAt(float DepthRatio, float Wobble) const
	{
		for (const FGeoProfileLayer& L : Layers)
		{
			if (DepthRatio < L.BottomDepthRatio + Wobble * L.WobbleRatio) { return L.LithologyId; }
		}
		return Layers.Num() > 0 ? Layers.Last().LithologyId : 0;
	}

	uint64 Hash() const
	{
		uint64 H = uint64(Layers.Num());
		for (const FGeoProfileLayer& L : Layers)
		{
			H = GeoHash::Combine(H, GeoHash::Float(L.BottomDepthRatio));
			H = GeoHash::Combine(H, uint64(L.LithologyId));
			H = GeoHash::Combine(H, GeoHash::Float(L.WobbleRatio));
		}
		return H;
	}
};
