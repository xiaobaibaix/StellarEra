// Copyright Voxel Plugin SAS. All Rights Reserved.

#include "PlanetGeologyDisseminatedDistribution.h"

namespace
{
	struct FGrain
	{
		FVector Center = FVector::ZeroVector;
		float Radius = 0.f;
		FBox Bounds = FBox(ForceInit);
	};

	struct FDisseminatedRuntime final : public FPlanetGeologyDistributionRuntime
	{
		TArray<FGrain> Grains;
		TMap<FIntVector, TArray<int32>> Index;
		double CellSize = 0.0;
		int32 IndexReferences = 0;
		FPlanetGeologyDistributionNoise Noise;

		/** 与囊状同一个二次衰减：中心 1，表面 0。 */
		static float GrainInfluence(const FGrain& Grain, const FVector& P)
		{
			const float RadiusSquared = Grain.Radius * Grain.Radius;
			if (RadiusSquared <= 0.f)
			{
				return 0.f;
			}
			const float DistanceSquared = (P - Grain.Center).SizeSquared();
			if (DistanceSquared >= RadiusSquared)
			{
				return 0.f;
			}
			return 1.f - DistanceSquared / RadiusSquared;
		}

		float EvaluateNormalized(const FVector& P) const override
		{
			if (Grains.Num() == 0 || CellSize <= 0.0)
			{
				return 0.f;
			}

			float Best = 0.f;
			if (const TArray<int32>* Refs = Index.Find(PlanetGeologyToCell(P, CellSize)))
			{
				for (const int32 GrainIndex : *Refs)
				{
					Best = FMath::Max(Best, GrainInfluence(Grains[GrainIndex], P));
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
			for (const FGrain& Grain : Grains)
			{
				Best = FMath::Max(Best, GrainInfluence(Grain, P));
			}
			if (Best <= 0.f)
			{
				return 0.f;
			}
			return Best * Noise.EvaluateVein(P);
		}

		void EnumerateRegions(TArray<FPlanetGeologyRegion>& Out) const override
		{
			for (const FGrain& Grain : Grains)
			{
				FPlanetGeologyRegion& Region = Out.AddDefaulted_GetRef();
				Region.Bounds = Grain.Bounds;
				Region.CharacteristicRadius = Grain.Radius;
			}
		}

		bool IsEmpty() const override { return Grains.Num() == 0; }
		int32 NumPrimitives() const override { return Grains.Num(); }
		const TCHAR* GetDebugName() const override { return TEXT("Disseminated"); }

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
UPlanetGeologyDisseminatedDistribution::BuildRuntime(
	const FPlanetGeologyDistributionBuildContext& Context) const
{
	const int32 ResolvedSeed = ResolveSeed(Context.ElementId, Context.DistributionIndex);

	const int32 InGrainCount = FMath::Clamp(GrainCount, 0, 8192);
	const double InGrainRadius = FMath::Clamp(double(GrainRadius), 0.0005, 0.02);
	const double InRadiusJitter = FMath::Clamp(double(RadiusJitter), 0.0, 1.0);

	TSharedPtr<FDisseminatedRuntime> Runtime = MakeShared<FDisseminatedRuntime>();
	Runtime->Noise.Init(ResolvedSeed, float(InGrainRadius), ShapeDetail, Octaves,
		Lacunarity, Gain, VeinThreshold);
	if (InGrainCount == 0)
	{
		return Runtime;
	}

	FRandomStream Rng(ResolvedSeed);
	Runtime->Grains.Reserve(InGrainCount);
	float MaxRadius = 0.f;

	for (int32 GrainIndex = 0; GrainIndex < InGrainCount; ++GrainIndex)
	{
		const FVector Center = SamplePlanetGeologyPlacement(
			Rng, DepthBias, DepthCenter, DepthSpread);
		const double RadiusScale = 1.0 + InRadiusJitter * (2.0 * double(Rng.FRand()) - 1.0);

		FGrain& Grain = Runtime->Grains.AddDefaulted_GetRef();
		Grain.Center = Center;
		Grain.Radius = float(FMath::Clamp(InGrainRadius * RadiusScale, 0.0005, 0.02));
		Grain.Bounds = FBox(Center - FVector(Grain.Radius), Center + FVector(Grain.Radius));
		MaxRadius = FMath::Max(MaxRadius, Grain.Radius);
	}

	Runtime->CellSize = double(MaxRadius);
	for (int32 GrainIndex = 0; GrainIndex < Runtime->Grains.Num(); ++GrainIndex)
	{
		PlanetGeologyIndexBounds(Runtime->Grains[GrainIndex].Bounds,
			Runtime->CellSize, Runtime->Index, GrainIndex, Runtime->IndexReferences);
	}
	return Runtime;
}
