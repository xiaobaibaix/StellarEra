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
		return Out;
	}
}
