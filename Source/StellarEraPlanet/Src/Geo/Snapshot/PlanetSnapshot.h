// 冻结产物：不可变、无 UObject，以 TSharedPtr<const> 交给体素线程。只放求值期真的要读的字段。
#pragma once
#include "CoreMinimal.h"
#include "GeoField2D.h"
#include "GeoPrimitiveIndex.h"
#include "GeoProfile.h"

struct STELLARERAPLANET_API FPlanetSnapshot
{
	/** 海平面半径（体素），也是 M1 的地表。 */
	double R0 = 0.0;
	/** 密度值 = 距离差 / DensityScale；1 表示「1 体素对应 1 个单位」。 */
	float DensityScale = 1.f;
	FGeoProfile Radial;
	/** 莫霍面等界面的起伏，∈ [-1,1]，乘各层 WobbleRatio 得到半径比例。 */
	FGeoField2D MohoWobble;
	FGeoPrimitiveIndex Primitives;
	/** 地表半径的保守上下界，值域与早退都以它为准。 */
	double MinSurfaceRadius = 0.0;
	double MaxSurfaceRadius = 0.0;
	uint64 ContentHash = 0;

	void FinalizeHash();
	SIZE_T GetAllocatedSize() const;
};
