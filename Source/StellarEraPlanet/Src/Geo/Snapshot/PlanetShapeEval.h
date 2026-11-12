// 密度求值核：体素线程每次挖掘调用 ~10⁹ 次，只许 inline 算术，不许虚函数、分配、日志。
#pragma once
#include "CoreMinimal.h"
#include "PlanetSnapshot.h"

namespace PlanetShapeEval
{
	FORCEINLINE double SurfaceRadius(const FPlanetSnapshot& S, const FVector3f& /*Dir*/)
	{
		return S.R0;   // M3 起叠加 Elevation
	}

	FORCEINLINE float Value(const FPlanetSnapshot& S, const FVector3d& P)
	{
		const double D = P.Size();
		const FVector3f Dir = D > 1e-6 ? FVector3f(P / D) : FVector3f::UpVector;
		return float((D - SurfaceRadius(S, Dir)) / double(S.DensityScale));
	}

	/** 盒内到球心的最近 / 最远距离分别配地表上 / 下界，得到保守区间。 */
	FORCEINLINE FGeoRange ValueRange(const FPlanetSnapshot& S, const FVector3d& BoxMin, const FVector3d& BoxMax)
	{
		const FVector3d Near(FMath::Clamp(0.0, BoxMin.X, BoxMax.X), FMath::Clamp(0.0, BoxMin.Y, BoxMax.Y), FMath::Clamp(0.0, BoxMin.Z, BoxMax.Z));
		const FVector3d Far(FMath::Max(FMath::Abs(BoxMin.X), FMath::Abs(BoxMax.X)),
			FMath::Max(FMath::Abs(BoxMin.Y), FMath::Abs(BoxMax.Y)), FMath::Max(FMath::Abs(BoxMin.Z), FMath::Abs(BoxMax.Z)));
		const double Scale = double(S.DensityScale);
		return { float((Near.Size() - S.MaxSurfaceRadius) / Scale), float((Far.Size() - S.MinSurfaceRadius) / Scale) };
	}
}
