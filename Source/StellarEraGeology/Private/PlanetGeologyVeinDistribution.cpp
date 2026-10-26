// Copyright Voxel Plugin SAS. All Rights Reserved.

#include "PlanetGeologyVeinDistribution.h"

namespace
{
	/** 一段脉（归一化空间）：胶囊 A→B，半径 r。 */
	struct FVeinSegment
	{
		FVector A = FVector::ZeroVector;
		FVector B = FVector::ZeroVector;
		float Radius = 0.f;
		FBox Bounds = FBox(ForceInit);
		float BoundingRadius = 0.f;
	};

	/**
	 * 从 Point 出发沿 Direction 走 TargetLength，把每一段写进 Segments。
	 *
	 * 随机流顺序固定：扰动轴 → 扰动角 → （新点出球则停）→ 半径抖动。
	 * 停止条件是「新点离球心超过 1 − 0.5r」：露头靠胶囊自身探出，不额外外推。
	 */
	void GrowChain(TArray<FVeinSegment>& Segments, TArray<FVector>& OutPoints,
		FRandomStream& Rng, FVector Point, FVector Direction, const double TargetLength,
		const double InVeinRadius, const double InRadiusJitter, const double SegmentLength,
		const double DirectionJitterDeg)
	{
		OutPoints.Reset();
		OutPoints.Add(Point);

		double Traveled = 0.0;
		while (Traveled < TargetLength)
		{
			const FVector RandomVector = Rng.GetUnitVector();
			const double Angle = FMath::DegreesToRadians(
				DirectionJitterDeg * (2.0 * double(Rng.FRand()) - 1.0));
			FVector Axis = FVector::CrossProduct(Direction, RandomVector).GetSafeNormal();
			if (Axis.IsNearlyZero())
			{
				Axis = FVector::CrossProduct(Direction, FVector::ZAxisVector).GetSafeNormal();
			}

			FVector NewDirection = Axis.IsNearlyZero()
				? Direction
				: Direction.RotateAngleAxisRad(Angle, Axis);
			NewDirection = NewDirection.GetSafeNormal();
			if (NewDirection.IsNearlyZero())
			{
				NewDirection = Direction;
			}

			const FVector NewPoint = Point + NewDirection * float(SegmentLength);
			if (NewPoint.Size() > 1.0 - 0.5 * InVeinRadius)
			{
				break;
			}

			const double RadiusScale =
				1.0 + InRadiusJitter * (2.0 * double(Rng.FRand()) - 1.0);

			FVeinSegment& Segment = Segments.AddDefaulted_GetRef();
			Segment.A = Point;
			Segment.B = NewPoint;
			Segment.Radius = float(FMath::Clamp(InVeinRadius * RadiusScale, 0.002, 0.05));
			Segment.Bounds = FBox(
				Segment.A.ComponentMin(Segment.B) - FVector(Segment.Radius),
				Segment.A.ComponentMax(Segment.B) + FVector(Segment.Radius));
			Segment.BoundingRadius =
				float((NewPoint - Point).Size() * 0.5) + Segment.Radius;

			OutPoints.Add(NewPoint);
			Point = NewPoint;
			Direction = NewDirection;
			Traveled += SegmentLength;
		}
	}

	struct FVeinRuntime final : public FPlanetGeologyDistributionRuntime
	{
		TArray<FVeinSegment> Segments;
		TMap<FIntVector, TArray<int32>> Index;
		double CellSize = 0.0;
		int32 IndexReferences = 0;
		FPlanetGeologyDistributionNoise Noise;

		float EvaluateSegment(const FVeinSegment& Segment, const FVector& P) const
		{
			const FVector Delta = Segment.B - Segment.A;
			const double LengthSquared = Delta.SizeSquared();
			const double RadiusSquared =
				double(Segment.Radius) * double(Segment.Radius);
			if (LengthSquared <= 0.0 || RadiusSquared <= 0.0)
			{
				return 0.f;
			}

			const double T = FMath::Clamp(
				double(FVector::DotProduct(P - Segment.A, Delta)) / LengthSquared, 0.0, 1.0);
			const FVector Closest = Segment.A + Delta * float(T);
			const double DistanceSquared = (P - Closest).SizeSquared();
			if (DistanceSquared >= RadiusSquared)
			{
				return 0.f;
			}
			return float(1.0 - DistanceSquared / RadiusSquared);
		}

		float EvaluateNormalized(const FVector& P) const override
		{
			if (Segments.Num() == 0 || CellSize <= 0.0)
			{
				return 0.f;
			}

			float Best = 0.f;
			if (const TArray<int32>* Refs = Index.Find(PlanetGeologyToCell(P, CellSize)))
			{
				for (const int32 SegmentIndex : *Refs)
				{
					Best = FMath::Max(Best, EvaluateSegment(Segments[SegmentIndex], P));
					if (Best >= 1.f)
					{
						break;
					}
				}
			}
			if (Best <= 0.f)
			{
				return 0.f;
			}
			return Best * Noise.EvaluateVein(P);
		}

		float EvaluateBruteForce(const FVector& P) const override
		{
			float Best = 0.f;
			for (const FVeinSegment& Segment : Segments)
			{
				Best = FMath::Max(Best, EvaluateSegment(Segment, P));
			}
			if (Best <= 0.f)
			{
				return 0.f;
			}
			return Best * Noise.EvaluateVein(P);
		}

		void EnumerateRegions(TArray<FPlanetGeologyRegion>& Out) const override
		{
			for (const FVeinSegment& Segment : Segments)
			{
				FPlanetGeologyRegion& Region = Out.AddDefaulted_GetRef();
				Region.Bounds = Segment.Bounds;
				Region.CharacteristicRadius = Segment.Radius;
			}
		}

		bool IsEmpty() const override { return Segments.Num() == 0; }
		int32 NumPrimitives() const override { return Segments.Num(); }
		const TCHAR* GetDebugName() const override { return TEXT("Vein"); }

		FString GetDebugSummary() const override
		{
			const double AverageCandidates = Index.Num() > 0
				? double(IndexReferences) / double(Index.Num())
				: 0.0;
			return FString::Printf(TEXT("格边=%.4f 平均每格候选=%.2f"), CellSize, AverageCandidates);
		}
	};
}

TSharedPtr<const FPlanetGeologyDistributionRuntime>
UPlanetGeologyVeinDistribution::BuildRuntime(
	const FPlanetGeologyDistributionBuildContext& Context) const
{
	const int32 ResolvedSeed = ResolveSeed(Context.ElementId, Context.DistributionIndex);

	const int32 InVeinCount = FMath::Clamp(VeinCount, 0, 64);
	const double InVeinRadius = FMath::Clamp(double(VeinRadius), 0.002, 0.05);
	const double SegmentLength =
		InVeinRadius * FMath::Clamp(double(SegmentLengthRatio), 1.0, 4.0);
	const double TargetLength = FMath::Clamp(double(VeinLength), 0.05, 1.0);
	const double InDirectionJitter = FMath::Clamp(double(DirectionJitterDeg), 0.0, 45.0);
	const int32 InBranchCount = FMath::Clamp(BranchCount, 0, 8);
	const double InRadiusJitter = FMath::Clamp(double(RadiusJitter), 0.0, 1.0);

	TSharedPtr<FVeinRuntime> Runtime = MakeShared<FVeinRuntime>();
	Runtime->Noise.Init(ResolvedSeed, float(InVeinRadius), ShapeDetail, Octaves,
		Lacunarity, Gain, VeinThreshold);
	if (InVeinCount == 0)
	{
		return Runtime;
	}

	FRandomStream Rng(ResolvedSeed);
	for (int32 VeinIndex = 0; VeinIndex < InVeinCount; ++VeinIndex)
	{
		const FVector Start = SamplePlanetGeologyPlacement(
			Rng, DepthBias, DepthCenter, DepthSpread);
		const FVector InitialDirection = Rng.GetUnitVector();

		TArray<FVector> ChainPoints;
		GrowChain(Runtime->Segments, ChainPoints, Rng, Start, InitialDirection,
			TargetLength, InVeinRadius, InRadiusJitter, SegmentLength, InDirectionJitter);

		for (int32 BranchIndex = 0; BranchIndex < InBranchCount; ++BranchIndex)
		{
			if (ChainPoints.Num() < 2)
			{
				break;
			}
			const int32 StartIndex = Rng.RandRange(0, ChainPoints.Num() - 1);
			const FVector BranchDirection = Rng.GetUnitVector();

			TArray<FVector> BranchPoints;
			GrowChain(Runtime->Segments, BranchPoints, Rng, ChainPoints[StartIndex],
				BranchDirection, TargetLength * 0.5, InVeinRadius, InRadiusJitter,
				SegmentLength, InDirectionJitter);
		}
	}

	float MaxBoundingRadius = 0.f;
	for (const FVeinSegment& Segment : Runtime->Segments)
	{
		MaxBoundingRadius = FMath::Max(MaxBoundingRadius, Segment.BoundingRadius);
	}
	Runtime->CellSize = double(MaxBoundingRadius);
	for (int32 SegmentIndex = 0; SegmentIndex < Runtime->Segments.Num(); ++SegmentIndex)
	{
		PlanetGeologyIndexBounds(Runtime->Segments[SegmentIndex].Bounds,
			Runtime->CellSize, Runtime->Index, SegmentIndex, Runtime->IndexReferences);
	}
	return Runtime;
}
