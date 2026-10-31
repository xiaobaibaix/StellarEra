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

	double RaySphereFarHit(
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

		// 与 RaySphereFirstHit 同一套推导，只取 Exit 这一根。
		const FVector ToCenter = SphereCenter - RayOrigin;
		const double Projection = ToCenter.Dot(Direction);
		const double PerpSq = ToCenter.SizeSquared() - Projection * Projection;
		const double RadiusSq = SphereRadiusCm * SphereRadiusCm;
		if (PerpSq > RadiusSq)
		{
			return -1.0; // 射线擦不到球
		}

		const double Exit = Projection + FMath::Sqrt(RadiusSq - PerpSq);
		// 只有 Exit ≤ 0（整个球在背后）才算失败；起点在球内时 Entry < 0 < Exit，
		// 照样给出尽头 —— 这正是与 FirstHit 的区别。
		return Exit > 0.0 ? Exit : -1.0;
	}

	FMarchStepRange ComputeMarchStepRange(
		const FVector& RayOrigin,
		const FVector& UnitDir,
		const double AnalyticDistanceCm,
		const double BoundingRadiusCm,
		const double VoxelSizeCm)
	{
		FMarchStepRange Range;
		const double StepCm = FMath::Max(VoxelSizeCm, 1.0e-4);

		// 往里：走到包围球在行星**另一侧**的穿出点，也就是整条弦。
		double FarRootCm = RaySphereFarHit(
			RayOrigin, UnitDir, FVector::ZeroVector, BoundingRadiusCm);
		if (FarRootCm < 0.0)
		{
			// 解析命中点必然落在包围球内，理论上走不到这里。真走到了也给一个有限终点。
			FarRootCm = AnalyticDistanceCm + FMath::Max(BoundingRadiusCm, 0.0);
		}

		// 往外：走到包围球近根；起点在球内（近根为 −1）时退到射线原点。
		const double NearRootCm = RaySphereFirstHit(
			RayOrigin, UnitDir, FVector::ZeroVector, BoundingRadiusCm);
		const double BackwardLimitCm = FMath::Max(NearRootCm, 0.0);

		Range.InwardMaxSteps = FMath::Max(1,
			FMath::CeilToInt((FarRootCm - AnalyticDistanceCm) / StepCm));
		Range.BackwardMaxSteps = FMath::Max(1,
			FMath::CeilToInt((AnalyticDistanceCm - BackwardLimitCm) / StepCm));
		return Range;
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
