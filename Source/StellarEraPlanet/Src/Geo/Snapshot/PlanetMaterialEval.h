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
		const float E = S.Elevation.Res > 0 ? S.Elevation.Sample(Dir) : 0.f;
		FGeoSample Out;
		Out.Lithology = S.Radial.LithologyAt(DepthRatio, Wobble);
		// 地壳不分深度，按海平面分陆壳 / 洋壳：两套成矿背景的判据是海拔不是厚度。
		if (Out.Lithology == GeoLithology::Crust)
		{
			Out.Lithology = E < S.CrustSplitElevation ? GeoLithology::OceanicCrust : GeoLithology::ContinentalCrust;
		}

		// 矿体：取强度最高的那个；> 0.5 才算在矿体里，软边外的浸染留给以后的品位渐变。
		if (!S.Primitives.IsEmpty())
		{
			const FVector3f Pf(P);
			float Best = 0.5f;
			const TArray<FGeoPrimitive>& Prims = S.Primitives.GetPrimitives();
			S.Primitives.QueryPoint(Pf, [&](int32 i)
			{
				const FGeoPrimitive& Pr = Prims[i];
				if (Pr.MineralId == 0) { return; }
				const float V = GeoPrimitiveEvaluate(Pr, Pf);
				if (V > Best) { Best = V; Out.Mineral = Pr.MineralId; Out.Grade = Pr.Grade; }
			});
		}
		// 覆层：地表下一薄层。下限放到地表上方 2 体素：网格顶点可能落在地表外一点点。
		if (Out.Mineral == 0 && S.MineralOverlays.Num() > 0)
		{
			const double Below = S.R0 + double(E) - D;
			for (const FGeoMineralOverlay& O : S.MineralOverlays)
			{
				if (Below >= -2.0 && Below <= double(O.DepthVoxels) && O.Mask.Sample(Dir) > 0.5f)
				{
					Out.Mineral = O.MineralId;
					Out.Grade = O.Grade;
					break;
				}
			}
		}
		return Out;
	}
}
