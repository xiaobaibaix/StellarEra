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
			// 退化的叉乘必须换一个与 LocalZ **不平行**的轴：写死 XAxis 在
			// LocalZ ∥ XAxis 时同样得到零向量。取 LocalZ 分量绝对值最小的世界轴，
			// 它与 LocalZ 的夹角最接近 90°，叉乘不会退化。
			const FVector::FReal AbsX = FMath::Abs(LocalZ.X);
			const FVector::FReal AbsY = FMath::Abs(LocalZ.Y);
			const FVector::FReal AbsZ = FMath::Abs(LocalZ.Z);
			const FVector FallbackAxis = (AbsX <= AbsY && AbsX <= AbsZ) ? FVector::XAxisVector
				: (AbsY <= AbsZ ? FVector::YAxisVector : FVector::ZAxisVector);
			LocalX = FVector::CrossProduct(LocalZ, FallbackAxis).GetSafeNormal();
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

		// 有向包围盒：把局部盒的 8 个角点转到世界再取 FBox。
		//
		// 不要手推 `Σ_j |M_ij| h_j` 那种下标：`FMatrix` 是行向量约定，行列含义极易写反，
		// 写反会得到一个**不保守**的盒子（某些朝向下比真实包围盒小十几倍），索引随之漏格，
		// 真实矿体被切掉 —— 而按包围盒采样的测试根本发现不了。转 8 个角点没有下标可写错。
		//
		// 厚度用**扭曲放大后**的值：径向域扭曲最多把点沿局部 Z 挪
		// WarpAmplitude × 厚度，按原始厚度算包围盒会让索引漏候选、区域也盖不住
		// 实际矿体（WarpAmplitude 非 0 时表现为矿体被切掉一角）。
		const double EffectiveHalfThickness =
			double(Lens.HalfExtent.Z) * (1.0 + Runtime->WarpAmplitude);
		FBox OrientedBounds(ForceInit);
		for (int32 CornerIndex = 0; CornerIndex < 8; ++CornerIndex)
		{
			const FVector LocalCorner(
				(CornerIndex & 1) ? double(Lens.HalfExtent.X) : -double(Lens.HalfExtent.X),
				(CornerIndex & 2) ? double(Lens.HalfExtent.Y) : -double(Lens.HalfExtent.Y),
				(CornerIndex & 4) ? EffectiveHalfThickness : -EffectiveHalfThickness);
			OrientedBounds += Lens.Center + Orientation.RotateVector(LocalCorner);
		}
		Lens.Bounds = OrientedBounds;
		const FVector Extent = OrientedBounds.GetExtent();
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
