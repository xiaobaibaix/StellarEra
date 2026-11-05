// Copyright Voxel Plugin SAS. All Rights Reserved.

#include "PlanetShadowMask.h"

namespace PlanetShadow
{
	float ComputeLightMask(
		const FVector& WorldPosition,
		const FVector& DirectionToSun,
		const FVector& PlanetCenter,
		double PlanetRadiusCm,
		double PenumbraCm)
	{
		const FVector ToCenter = PlanetCenter - WorldPosition;
		const double TowardSun = FVector::DotProduct(ToCenter, DirectionToSun);

		// 球心在射线背后（或射线与球心连线垂直）：行星挡不到太阳。
		// 这一支不能省：太阳在天顶时 ToCenter 与 DirectionToSun 反向，
		// 光算 Perp 会得到 0（射线穿过球心）而把大白天判成深夜。
		if (TowardSun <= 0.0)
		{
			return 1.0f;
		}

		const double PerpSq = FMath::Max(
			ToCenter.SizeSquared() - TowardSun * TowardSun, 0.0);
		const double Perp = FMath::Sqrt(PerpSq);
		const double Edge = FMath::Max(PenumbraCm, 1.0);

		return static_cast<float>(FMath::Clamp((Perp - PlanetRadiusCm) / Edge, 0.0, 1.0));
	}
}
