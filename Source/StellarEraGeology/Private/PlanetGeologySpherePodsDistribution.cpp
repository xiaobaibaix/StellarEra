// Copyright Voxel Plugin SAS. All Rights Reserved.

#include "PlanetGeologySpherePodsDistribution.h"

#include "PlanetGeologyTypes.h"

namespace
{
	/** 球内二次衰减（1 - d²/r²）：中心 1，表面 0，囊外 0。与旧实现逐字相同。 */
	float PodInfluence(const FPlanetGeologyPod& Pod, const FVector& Point)
	{
		const float RadiusSquared = Pod.Radius * Pod.Radius;
		if (RadiusSquared <= 0.f)
		{
			return 0.f;
		}
		const float DistanceSquared = (Point - Pod.Center).SizeSquared();
		if (DistanceSquared >= RadiusSquared)
		{
			return 0.f;
		}
		return 1.f - DistanceSquared / RadiusSquared;
	}

	struct FSpherePodsRuntime final : public FPlanetGeologyDistributionRuntime
	{
		TArray<FPlanetGeologyPod> Pods;
		TMap<FIntVector, TArray<int32>> Index;
		double CellSize = 0.0;
		int32 IndexReferences = 0;
		FPlanetGeologyDistributionNoise Noise;

		float EvaluateNormalized(const FVector& P) const override
		{
			if (Pods.Num() == 0 || CellSize <= 0.0)
			{
				return 0.f;
			}

			float PodFactor = 0.f;
			if (const TArray<int32>* Refs = Index.Find(PlanetGeologyToCell(P, CellSize)))
			{
				for (const int32 PodIndex : *Refs)
				{
					PodFactor = FMath::Max(PodFactor, PodInfluence(Pods[PodIndex], P));
					if (PodFactor >= 1.f)
					{
						break;
					}
				}
			}
			if (PodFactor <= 0.f)
			{
				return 0.f; // 不在任何矿囊里：一次 Perlin 都不求
			}
			return PodFactor * Noise.EvaluateVein(P);
		}

		float EvaluateBruteForce(const FVector& P) const override
		{
			float PodFactor = 0.f;
			for (const FPlanetGeologyPod& Pod : Pods)
			{
				PodFactor = FMath::Max(PodFactor, PodInfluence(Pod, P));
			}
			if (PodFactor <= 0.f)
			{
				return 0.f;
			}
			return PodFactor * Noise.EvaluateVein(P);
		}

		void EnumerateRegions(TArray<FPlanetGeologyRegion>& Out) const override
		{
			for (const FPlanetGeologyPod& Pod : Pods)
			{
				FPlanetGeologyRegion& Region = Out.AddDefaulted_GetRef();
				Region.Bounds = FBox(Pod.Center - FVector(Pod.Radius),
					Pod.Center + FVector(Pod.Radius));
				Region.CharacteristicRadius = Pod.Radius;
			}
		}

		bool IsEmpty() const override { return Pods.Num() == 0; }
		int32 NumPrimitives() const override { return Pods.Num(); }
		const TCHAR* GetDebugName() const override { return TEXT("SpherePods"); }

		FString GetDebugSummary() const override
		{
			const double AverageCandidates = Index.Num() > 0
				? double(IndexReferences) / double(Index.Num())
				: 0.0;
			return FString::Printf(TEXT("格边=%.4f 平均每格候选=%.2f"), CellSize, AverageCandidates);
		}

		const TArray<FPlanetGeologyPod>& GetSpherePods() const override { return Pods; }
	};

	TSharedPtr<const FPlanetGeologyDistributionRuntime> MakeRuntime(
		const int32 InPodCount, const float InPodRadius, const float InPodRadiusJitter,
		const float InPodDepthBias, const float InPodDepthCenter, const float InPodDepthSpread,
		const int32 InSeed, const float InShapeDetail, const int32 InOctaves,
		const float InLacunarity, const float InGain, const float InVeinThreshold)
	{
		const int32 PodCount = FMath::Clamp(InPodCount, 0, 512);
		const float NominalRadius = FMath::Clamp(InPodRadius, 0.001f, 0.25f);

		TSharedPtr<FSpherePodsRuntime> Runtime = MakeShared<FSpherePodsRuntime>();
		Runtime->Noise.Init(InSeed, NominalRadius, InShapeDetail, InOctaves,
			InLacunarity, InGain, InVeinThreshold);
		if (PodCount == 0)
		{
			return Runtime;
		}

		FRandomStream Rng(InSeed);
		const double Jitter = FMath::Clamp(double(InPodRadiusJitter), 0.0, 1.0);
		Runtime->Pods.Reserve(PodCount);

		float MaxRadius = 0.f;
		for (int32 PodIndex = 0; PodIndex < PodCount; ++PodIndex)
		{
			// 随机流顺序必须与旧 PlacePods 逐位一致：先抖动半径，再方向，再径向距离。
			// 调换任何两步，旧行星的矿囊位置就全变了。
			const double RadiusJitter = 1.0 + Jitter * (2.0 * double(Rng.FRand()) - 1.0);
			const double Radius = FMath::Clamp(double(NominalRadius) * RadiusJitter, 1e-3, 0.25);

			FPlanetGeologyPod& Pod = Runtime->Pods.AddDefaulted_GetRef();
			Pod.Radius = float(Radius);
			Pod.Center = SamplePlanetGeologyPlacement(
				Rng, InPodDepthBias, InPodDepthCenter, InPodDepthSpread);

			MaxRadius = FMath::Max(MaxRadius, Pod.Radius);
		}

		Runtime->CellSize = double(MaxRadius);
		for (int32 PodIndex = 0; PodIndex < Runtime->Pods.Num(); ++PodIndex)
		{
			const FPlanetGeologyPod& Pod = Runtime->Pods[PodIndex];
			PlanetGeologyIndexBounds(
				FBox(Pod.Center - FVector(Pod.Radius), Pod.Center + FVector(Pod.Radius)),
				Runtime->CellSize, Runtime->Index, PodIndex, Runtime->IndexReferences);
		}

		return Runtime;
	}
}

TSharedPtr<const FPlanetGeologyDistributionRuntime>
UPlanetGeologySpherePodsDistribution::BuildRuntime(
	const FPlanetGeologyDistributionBuildContext& Context) const
{
	return MakeRuntime(PodCount, PodRadius, PodRadiusJitter, PodDepthBias, PodDepthCenter,
		PodDepthSpread, ResolveSeed(Context.ElementId, Context.DistributionIndex),
		ShapeDetail, Octaves, Lacunarity, Gain, VeinThreshold);
}

TSharedPtr<const FPlanetGeologyDistributionRuntime>
UPlanetGeologySpherePodsDistribution::MakeRuntimeFromLegacyParams(
	const int32 InPodCount, const float InPodRadius, const float InPodRadiusJitter,
	const float InPodDepthBias, const float InPodDepthCenter, const float InPodDepthSpread,
	const int32 InSeed, const float InShapeDetail, const int32 InOctaves,
	const float InLacunarity, const float InGain, const float InVeinThreshold)
{
	return MakeRuntime(InPodCount, InPodRadius, InPodRadiusJitter, InPodDepthBias,
		InPodDepthCenter, InPodDepthSpread, InSeed, InShapeDetail, InOctaves,
		InLacunarity, InGain, InVeinThreshold);
}
