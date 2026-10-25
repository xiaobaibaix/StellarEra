// Copyright Voxel Plugin SAS. All Rights Reserved.

#include "PlanetSurfaceRay.h"

namespace PlanetSurfaceRay
{
	double RaySphereFirstHit(
		const FVector& RayOrigin,
		const FVector& UnitDir,
		const FVector& SphereCenter,
		const double SphereRadiusCm)
	{
		if (SphereRadiusCm <= 0.0)
		{
			return -1.0;
		}

		const FVector Direction = UnitDir.GetSafeNormal();
		if (Direction.IsNearlyZero())
		{
			return -1.0;
		}

		// 射线 P(t) = Origin + Direction × t。把球心投到射线上：投影长度 Projection
		// 对应最近点，垂距平方 PerpSq = |ToCenter|² − Projection²。
		const FVector ToCenter = SphereCenter - RayOrigin;
		const double Projection = ToCenter.Dot(Direction);
		const double PerpSq = ToCenter.SizeSquared() - Projection * Projection;
		const double RadiusSq = SphereRadiusCm * SphereRadiusCm;
		if (PerpSq > RadiusSq)
		{
			return -1.0; // 射线擦不到球
		}

		// 半弦长：Entry 是穿入点，Exit 是穿出点。
		const double HalfChord = FMath::Sqrt(RadiusSq - PerpSq);
		const double Entry = Projection - HalfChord;
		const double Exit = Projection + HalfChord;

		// Exit ≤ 0：整个球在射线背后；Entry ≤ 0 < Exit：起点在球内。
		// 两种都不给约束 —— 返回 −1，避免调用方把缩放锁死在 0。
		return (Exit > 0.0 && Entry > 0.0) ? Entry : -1.0;
	}

	bool SolveAnalyticHit(
		const FVector& LocalOrigin,
		const FVector& LocalDir,
		const double BaseRadiusCm,
		const int32 Iterations,
		const TFunctionRef<double(const FVector& UnitDirection)> RadiusAtDirection,
		double& OutDistanceCm)
	{
		if (BaseRadiusCm <= 0.0)
		{
			return false; // 半径为 0 的球根本挡不住射线
		}

		const FVector Direction = LocalDir.GetSafeNormal();
		if (Direction.IsNearlyZero())
		{
			return false;
		}

		// 起点在基准球内：没有「穿入点」。调用方永远在地表外，走到这里说明参数错了。
		if (LocalOrigin.SizeSquared() < BaseRadiusCm * BaseRadiusCm)
		{
			return false;
		}

		double DistanceCm = RaySphereFirstHit(
			LocalOrigin, Direction, FVector::ZeroVector, BaseRadiusCm);
		if (DistanceCm < 0.0)
		{
			return false;
		}

		for (int32 Iteration = 0; Iteration < Iterations; ++Iteration)
		{
			const FVector Hit = LocalOrigin + Direction * DistanceCm;
			const FVector HitDirection = Hit.GetSafeNormal();
			if (HitDirection.IsNearlyZero())
			{
				break;
			}

			const double LocalRadius = RadiusAtDirection(HitDirection);
			if (LocalRadius <= 0.0)
			{
				break;
			}

			const double NextDistance = RaySphereFirstHit(
				LocalOrigin, Direction, FVector::ZeroVector, LocalRadius);
			if (NextDistance < 0.0)
			{
				// 这个方向的半径让射线打空（地形凹得太深）：保留上一次的距离。
				break;
			}

			const double Change = FMath::Abs(NextDistance - DistanceCm);
			DistanceCm = NextDistance;
			if (Change < 0.001) // 0.01 毫米，已经远小于体素
			{
				break;
			}
		}

		OutDistanceCm = DistanceCm;
		return true;
	}
}
