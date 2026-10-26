// Copyright Voxel Plugin SAS. All Rights Reserved.

#include "PlanetGeologyLensDistribution.h"

namespace
{
	/** 一片透镜（归一化空间）：中心 + 局部→世界的旋转 + 三轴半长。 */
	struct FLensPrimitive
	{
		FVector Center = FVector::ZeroVector;
		FQuat Orientation = FQuat::Identity;
		FVector HalfExtent = FVector::ZeroVector; // A, B, C
		FBox Bounds = FBox(ForceInit);
		float BoundingRadius = 0.f;
	};

	struct FLensRuntime final : public FPlanetGeologyDistributionRuntime
	{
		TArray<FLensPrimitive> Primitives;
		TMap<FIntVector, TArray<int32>> Index;
		double CellSize = 0.0;
		int32 IndexReferences = 0;
		double WarpAmplitude = 0.0;
		double WarpScale = 2.0;
		FPlanetGeologyDistributionNoise Noise;

		/** 单片在 P 处的强度：非负的 1 − Σ(Local/h)²。 */
		float EvaluatePrimitive(const FLensPrimitive& Lens, const FVector& P) const
		{
			if (Lens.HalfExtent.X <= 0.f || Lens.HalfExtent.Y <= 0.f
				|| Lens.HalfExtent.Z <= 0.f)
			{
				return 0.f;
			}

			FVector Local = Lens.Orientation.UnrotateVector(P - Lens.Center);
			if (WarpAmplitude > 0.0)
			{
				// 只扭曲径向：薄的一轴动起来才像片层被揉皱，横向扭曲会把整片拽走。
				const double Thickness = double(Lens.HalfExtent.Z);
				const double Frequency = WarpScale / Thickness;
				const float Warp = Noise.EvaluateAt(Local * float(Frequency));
				Local.Z += float(WarpAmplitude * Thickness * (2.0 * double(Warp) - 1.0));
			}

			const double Q =
				FMath::Square(double(Local.X) / double(Lens.HalfExtent.X))
				+ FMath::Square(double(Local.Y) / double(Lens.HalfExtent.Y))
				+ FMath::Square(double(Local.Z) / double(Lens.HalfExtent.Z));
			return float(FMath::Max(0.0, 1.0 - Q));
		}

		float EvaluateNormalized(const FVector& P) const override
		{
			if (Primitives.Num() == 0 || CellSize <= 0.0)
			{
				return 0.f;
			}

			float Best = 0.f;
			if (const TArray<int32>* Refs = Index.Find(PlanetGeologyToCell(P, CellSize)))
			{
				for (const int32 PrimitiveIndex : *Refs)
				{
					Best = FMath::Max(Best, EvaluatePrimitive(Primitives[PrimitiveIndex], P));
					if (Best >= 1.f)
					{
						break;
					}
				}
			}
			if (Best <= 0.f)
			{
				return 0.f; // 不在任何片里：一次 Perlin 都不求
			}
			return Best * Noise.EvaluateVein(P);
		}

		float EvaluateBruteForce(const FVector& P) const override
		{
			float Best = 0.f;
			for (const FLensPrimitive& Lens : Primitives)
			{
				Best = FMath::Max(Best, EvaluatePrimitive(Lens, P));
			}
			if (Best <= 0.f)
			{
				return 0.f;
			}
			return Best * Noise.EvaluateVein(P);
		}

		void EnumerateRegions(TArray<FPlanetGeologyRegion>& Out) const override
		{
			for (const FLensPrimitive& Lens : Primitives)
			{
				FPlanetGeologyRegion& Region = Out.AddDefaulted_GetRef();
				Region.Bounds = Lens.Bounds;
				Region.CharacteristicRadius = Lens.HalfExtent.Z;
			}
		}

		bool IsEmpty() const override { return Primitives.Num() == 0; }
		int32 NumPrimitives() const override { return Primitives.Num(); }
		const TCHAR* GetDebugName() const override { return TEXT("Lens"); }

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
UPlanetGeologyLensDistribution::BuildRuntime(
	const FPlanetGeologyDistributionBuildContext& Context) const
{
	const int32 InSeamCount = FMath::Clamp(SeamCount, 0, 256);
	const double InSizeJitter = FMath::Clamp(double(SizeJitter), 0.0, 1.0);
	const double TiltDegrees = FMath::Clamp(double(TiltJitterDeg), 0.0, 45.0);

	// 种子只解析一次：噪声与随机流必须是同一个种子，否则调参会得到「噪声和
	// 位置各自随机」的鬼影。
	const int32 ResolvedSeed = ResolveSeed(Context.ElementId, Context.DistributionIndex);

	TSharedPtr<FLensRuntime> Runtime = MakeShared<FLensRuntime>();
	Runtime->WarpAmplitude = FMath::Clamp(double(WarpAmplitude), 0.0, 2.0);
	Runtime->WarpScale = FMath::Clamp(double(WarpScale), 0.5, 8.0);
	Runtime->Noise.Init(ResolvedSeed,
		FMath::Clamp(SeamThickness, 0.0005f, 0.05f),
		ShapeDetail, Octaves, Lacunarity, Gain, VeinThreshold);
	if (InSeamCount == 0)
	{
		return Runtime;
	}

	FRandomStream Rng(ResolvedSeed);
	Runtime->Primitives.Reserve(InSeamCount);
	float MaxBoundingRadius = 0.f;

	for (int32 SeamIndex = 0; SeamIndex < InSeamCount; ++SeamIndex)
	{
		// 随机流顺序固定：方向+半径 → 切向随机向量 → 倾斜角 → A/B/C 抖动。
		const FVector Center = SamplePlanetGeologyPlacement(
			Rng, DepthBias, DepthCenter, DepthSpread);
		const FVector LocalZ = Center.IsNearlyZero() ? FVector::ZAxisVector
			: Center.GetSafeNormal();

		FVector LocalX = FVector::CrossProduct(LocalZ, Rng.GetUnitVector()).GetSafeNormal();
		if (LocalX.IsNearlyZero())
		{
			LocalX = FVector::CrossProduct(LocalZ, FVector::XAxisVector).GetSafeNormal();
		}
		const FQuat Base = FRotationMatrix::MakeFromXZ(LocalX, LocalZ).ToQuat();
		const FVector LocalY = FVector::CrossProduct(LocalZ, LocalX);

		const double TiltRadians =
			FMath::DegreesToRadians(TiltDegrees * (2.0 * double(Rng.FRand()) - 1.0));
		const FQuat Orientation = FQuat(LocalY, TiltRadians) * Base;

		auto Jittered = [&Rng, InSizeJitter](const double BaseValue)
		{
			return BaseValue * (1.0 + InSizeJitter * (2.0 * double(Rng.FRand()) - 1.0));
		};

		FLensPrimitive& Lens = Runtime->Primitives.AddDefaulted_GetRef();
		Lens.Center = Center;
		Lens.Orientation = Orientation;
		Lens.HalfExtent = FVector(
			float(FMath::Clamp(Jittered(double(SeamLength)), 0.01, 0.5)),
			float(FMath::Clamp(Jittered(double(SeamWidth)), 0.01, 0.5)),
			float(FMath::Clamp(Jittered(double(SeamThickness)), 0.0005, 0.05)));

		// 有向椭球的 AABB：沿世界轴 i 的半长 = Σ_j |R_ij| × h_j。
		//
		// 厚度用**扭曲放大后**的值：径向域扭曲最多把点沿局部 Z 挪
		// WarpAmplitude × 厚度，按原始厚度算包围盒会让索引漏候选、区域也盖不住
		// 实际矿体（WarpAmplitude 非 0 时表现为矿体被切掉一角）。
		const double EffectiveHalfThickness =
			double(Lens.HalfExtent.Z) * (1.0 + Runtime->WarpAmplitude);
		const FMatrix Rot = Orientation.ToMatrix();
		const FVector Extent(
			FMath::Abs(Rot.M[0][0]) * Lens.HalfExtent.X
				+ FMath::Abs(Rot.M[0][1]) * Lens.HalfExtent.Y
				+ FMath::Abs(Rot.M[0][2]) * EffectiveHalfThickness,
			FMath::Abs(Rot.M[1][0]) * Lens.HalfExtent.X
				+ FMath::Abs(Rot.M[1][1]) * Lens.HalfExtent.Y
				+ FMath::Abs(Rot.M[1][2]) * EffectiveHalfThickness,
			FMath::Abs(Rot.M[2][0]) * Lens.HalfExtent.X
				+ FMath::Abs(Rot.M[2][1]) * Lens.HalfExtent.Y
				+ FMath::Abs(Rot.M[2][2]) * EffectiveHalfThickness);
		Lens.Bounds = FBox(Lens.Center - Extent, Lens.Center + Extent);
		Lens.BoundingRadius = float(Extent.GetMax());
		MaxBoundingRadius = FMath::Max(MaxBoundingRadius, Lens.BoundingRadius);
	}

	Runtime->CellSize = double(MaxBoundingRadius);
	for (int32 PrimitiveIndex = 0; PrimitiveIndex < Runtime->Primitives.Num(); ++PrimitiveIndex)
	{
		PlanetGeologyIndexBounds(Runtime->Primitives[PrimitiveIndex].Bounds,
			Runtime->CellSize, Runtime->Index, PrimitiveIndex, Runtime->IndexReferences);
	}
	return Runtime;
}
