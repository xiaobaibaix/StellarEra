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
		// 海拔归一化到 0..255：材质按 UV1.y 查高程色带。区间退化时给海平面值。
		{
			const double Span = S.MaxSurfaceRadius - S.MinSurfaceRadius;
			const double T = Span > 1.0 ? (double(E) - (S.MinSurfaceRadius - S.R0)) / Span : 0.5;
			Out.Elevation = uint8(FMath::Clamp(FMath::RoundToInt(T * 255.0), 0, 255));
		}
		Out.Lithology = S.Radial.LithologyAt(DepthRatio, Wobble);
		// 地壳不分深度，按海平面分陆壳 / 洋壳：两套成矿背景的判据是海拔不是厚度。
		if (Out.Lithology == GeoLithology::Crust)
		{
			Out.Lithology = E < S.CrustSplitElevation ? GeoLithology::OceanicCrust : GeoLithology::ContinentalCrust;
		}

		// 3A 沉积盖层：只改最上面这层皮。放在矿体查询之前，穿层的矿脉仍然优先。
		if (S.StrataType.Res > 0)
		{
			const double Below = S.R0 + double(E) - D;
			const float Cover = S.StrataThickness.Sample(Dir);
			if (Cover > 0.f && Below <= double(Cover))
			{
				const uint8 T = uint8(FMath::Clamp(FMath::RoundToInt(S.StrataType.SampleNearest(Dir)), 0, 255));
				if (T != 0) { Out.Lithology = T; }
			}
		}
		// 3B：石灰岩埋进造山带够深就是大理岩。
		if (S.MetamorphicGrade.Res > 0 && Out.Lithology == GeoLithology::Limestone
			&& S.MetamorphicGrade.Sample(Dir) > S.MarbleThreshold)
		{
			Out.Lithology = GeoLithology::Marble;
		}

		// 矿体现在同时携带岩性与蚀变：一次查询按 矿 > 岩性 > 蚀变 的优先级落地，量级不同不互斥。
		if (!S.Primitives.IsEmpty())
		{
			const FVector3f Pf(P);
			float BestMineral = 0.5f, BestLitho = TNumericLimits<float>::Max();
			uint8 LithoId = 0, ClueId = 0, ClueStr = 0;
			const TArray<FGeoPrimitive>& Prims = S.Primitives.GetPrimitives();
			S.Primitives.QueryPoint(Pf, [&](int32 i)
			{
				const FGeoPrimitive& Pr = Prims[i];
				const float D = GeoPrimitiveDistance(Pr, Pf);
				if (D >= 1.f) { return; }
				if (Pr.MineralId != 0)
				{
					const float V = GeoBlobFalloff(D, Pr.Hard);
					if (V > BestMineral) { BestMineral = V; Out.Mineral = Pr.MineralId; Out.Grade = Pr.Grade; }
				}
				if (Pr.LithologyId != 0 && D < BestLitho) { BestLitho = D; LithoId = Pr.LithologyId; }
				if (Pr.ClueId != 0)
				{
					// 晕原语只带「有一圈蚀变」，具体是哪一圈由归一化距离决定，强度按圈递减。
					const int32 Band = D < S.AlterBands[0] ? 0 : D < S.AlterBands[1] ? 1 : D < S.AlterBands[2] ? 2 : 3;
					static const uint8 BandId[4] = { GeoClue::Keratitic, GeoClue::Sericitic, GeoClue::Argillic, GeoClue::Propylitic };
					static const uint8 BandStr[4] = { 15, 11, 7, 3 };
					const uint8 Str = uint8(FMath::Max(1, FMath::RoundToInt(float(BandStr[Band]) * float(Pr.ClueStrength) / 15.f)));
					if (Str > ClueStr) { ClueStr = Str; ClueId = BandId[Band]; }
				}
			});
			if (LithoId != 0) { Out.Lithology = LithoId; }
			Out.Clue = ClueId;
			Out.ClueStrength = ClueStr;
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
