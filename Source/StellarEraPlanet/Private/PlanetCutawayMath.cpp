// Copyright Voxel Plugin SAS. All Rights Reserved.

#include "PlanetCutawayMath.h"

namespace
{
	/** 二维点到线段的最短距离平方（UE 只提供 FVector 版本）。 */
	float DistanceSquaredToSegment2D(const FVector2D& Point, const FVector2D& A, const FVector2D& B)
	{
		const FVector2D AB = B - A;
		const double LengthSquared = AB.SizeSquared();
		if (LengthSquared <= 0.0)
		{
			return float(FVector2D::DistSquared(Point, A));
		}
		const double T = FMath::Clamp(
			FVector2D::DotProduct(Point - A, AB) / LengthSquared, 0.0, 1.0);
		return float(FVector2D::DistSquared(Point, A + AB * T));
	}
}

FVector FPlanetCutawayMath::ComputeClipNormal(
	const FVector& PlaneNormal,
	const FVector& CameraPos,
	const FVector& CenterCm)
{
	const FVector Normal = PlaneNormal.GetSafeNormal();

	const FVector ToCamera = CameraPos - CenterCm;
	if (Normal.IsNearlyZero() || ToCamera.IsNearlyZero())
	{
		return Normal;
	}

	return FVector::DotProduct(ToCamera, Normal) >= 0.0 ? Normal : -Normal;
}

void FPlanetCutawayMath::ComputePlaneBasis(
	const FVector& PlaneNormal,
	FVector& OutAxisU,
	FVector& OutAxisV)
{
	const FVector N = PlaneNormal.GetSafeNormal();

	// |N.Z| ≈ 1 时 Up 与 N 平行，cross 会退化，改用 Right 当参考轴。
	const FVector Reference =
		FMath::Abs(N.Z) < 0.99 ? FVector::UpVector : FVector::RightVector;

	OutAxisU = FVector::CrossProduct(Reference, N).GetSafeNormal();
	if (OutAxisU.IsNearlyZero())
	{
		// N 与 Up / Right 同时平行在几何上不可能；这里只保证不返回零向量。
		OutAxisU = FVector::ForwardVector;
	}
	OutAxisV = FVector::CrossProduct(N, OutAxisU);
}

FQuat FPlanetCutawayMath::RotatePlane(
	const FQuat& PlaneOrientation,
	const FVector& Axis,
	float DeltaDegrees)
{
	const FVector UnitAxis = Axis.GetSafeNormal();
	if (UnitAxis.IsNearlyZero() || DeltaDegrees == 0.f)
	{
		return PlaneOrientation;
	}

	const FQuat Delta(UnitAxis, FMath::DegreesToRadians(DeltaDegrees));
	return (Delta * PlaneOrientation).GetNormalized();
}

bool FPlanetCutawayMath::PickSectionRing(
	TArrayView<const FSectionRingScreenPolyline> Rings,
	const FVector2D& MousePx,
	float TolerancePx,
	ESectionRing& OutRing)
{
	// 距离差小于这个值视为并列：先遍历到的环优先（X → Y → Z）。
	constexpr float TieEpsilonPx = 1e-3f;

	float BestDistanceSquared = TNumericLimits<float>::Max();
	ESectionRing BestRing = ESectionRing::X;
	bool bFound = false;

	for (const FSectionRingScreenPolyline& Polyline : Rings)
	{
		const int32 Count = Polyline.Points.Num();
		if (Count < 2)
		{
			continue;
		}

		float RingDistanceSquared = TNumericLimits<float>::Max();
		for (int32 Index = 0; Index < Count; ++Index)
		{
			const FVector2D& A = Polyline.Points[Index];
			const FVector2D& B = Polyline.Points[(Index + 1) % Count];
			RingDistanceSquared = FMath::Min(
				RingDistanceSquared, DistanceSquaredToSegment2D(MousePx, A, B));
		}

		if (!bFound
			|| FMath::Sqrt(RingDistanceSquared) < FMath::Sqrt(BestDistanceSquared) - TieEpsilonPx)
		{
			BestDistanceSquared = RingDistanceSquared;
			BestRing = Polyline.Ring;
			bFound = true;
		}
	}

	OutRing = BestRing;
	return bFound && FMath::Sqrt(BestDistanceSquared) <= TolerancePx;
}

float FPlanetCutawayMath::ComputeOpacityMask(
	const FVector& WorldPos,
	const FVector& Origin,
	const FVector& Normal,
	float InsideRadiusCm,
	bool bActive)
{
	if (!bActive)
	{
		return 1.f;
	}

	const FVector Relative = WorldPos - Origin;
	const FVector UnitNormal = Normal.GetSafeNormal();

	const float Half = FMath::Clamp(
		float(FVector::DotProduct(Relative, UnitNormal)) * 10.f, 0.f, 1.f);
	const float Inside = FMath::Clamp(
		(InsideRadiusCm - float(Relative.Size())) * 0.02f, 0.f, 1.f);

	return 1.f - Half * Inside;
}

FVector FPlanetCutawayMath::RingPoint(ESectionRing Ring, float Radius, float AngleRadians)
{
	const float C = FMath::Cos(AngleRadians);
	const float S = FMath::Sin(AngleRadians);

	switch (Ring)
	{
	case ESectionRing::X: return FVector(0.f, C, S) * Radius;  // YZ 平面
	case ESectionRing::Y: return FVector(C, 0.f, S) * Radius;  // XZ 平面
	default:              return FVector(C, S, 0.f) * Radius;  // XY 平面
	}
}
