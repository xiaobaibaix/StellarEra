// 材质求值核：只在被网格化的表面上调用，返回语义样本；编码进体素材质由适配器调 GeoMaterialCodec。
#pragma once
#include "CoreMinimal.h"
#include "GeoMaterialCodec.h"
#include "PlanetSnapshot.h"

namespace PlanetMaterialEval
{
	FORCEINLINE FGeoSample Sample(const FPlanetSnapshot& S, const FVector3d& P)
	{
		const double D = P.Size();
		const FVector3f Dir = D > 1e-6 ? FVector3f(P / D) : FVector3f::UpVector;
		const float DepthRatio = float((S.R0 - D) / S.R0);
		const float Wobble = S.MohoWobble.Res > 0 ? S.MohoWobble.Sample(Dir) : 0.f;
		FGeoSample Out;
		Out.Lithology = S.Radial.LithologyAt(DepthRatio, Wobble);
		// 地壳不分深度，按海平面分陆壳 / 洋壳：两套成矿背景的判据是海拔不是厚度。
		if (Out.Lithology == GeoLithology::Crust)
		{
			const float E = S.Elevation.Res > 0 ? S.Elevation.Sample(Dir) : 0.f;
			Out.Lithology = E < S.CrustSplitElevation ? GeoLithology::OceanicCrust : GeoLithology::ContinentalCrust;
		}
		return Out;
	}
}
