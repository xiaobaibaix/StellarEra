// Copyright Voxel Plugin SAS. All Rights Reserved.

#include "PlanetGeologyDistribution.h"

#include "FastNoise/VoxelFastNoise.inl"
#include "PlanetGeologyPreset.h"
#include "PlanetGeologyTypes.h"

const TArray<FPlanetGeologyPod>& FPlanetGeologyDistributionRuntime::GetSpherePods() const
{
	static const TArray<FPlanetGeologyPod> Empty;
	return Empty;
}

void FPlanetGeologyDistributionNoise::Init(const int32 InSeed, const float InFeatureRadius,
	const float InShapeDetail, const int32 InOctaves, const float InLacunarity,
	const float InGain, const float InVeinThreshold)
{
	FeatureRadius = FMath::Max(InFeatureRadius, 0.f);
	ShapeDetail = FMath::Max(InShapeDetail, 1.f);
	Octaves = FMath::Clamp(InOctaves, 1, 6);
	VeinThreshold = FMath::Clamp(InVeinThreshold, 0.f, 0.95f);

	Noise.SetSeed(InSeed);
	Noise.SetFractalLacunarity(FMath::Max(InLacunarity, 1.f));
	// FractalBounding 只算一次；采样时的 octaves 必须与此处完全一致。
	Noise.SetFractalOctavesAndGain(Octaves, FMath::Clamp(InGain, 0.f, 1.f));
}

float FPlanetGeologyDistributionNoise::EvaluateAt(const FVector& NoiseSpacePoint) const
{
	const double RawNoise = double(Noise.GetPerlinFractal_3D(
		v_flt(double(NoiseSpacePoint.X)),
		v_flt(double(NoiseSpacePoint.Y)),
		v_flt(double(NoiseSpacePoint.Z)),
		v_flt(1.f), Octaves));

	// Perlin 输出 [-1,1]，映射到 [0,1] 才是「含量」。
	return float(FMath::Clamp(0.5 * (RawNoise + 1.0), 0.0, 1.0));
}

float FPlanetGeologyDistributionNoise::EvaluateVein(const FVector& P) const
{
	if (FeatureRadius <= 0.f)
	{
		return 0.f;
	}

	// 频率折叠进坐标：ShapeDetail 是「每特征直径的周期数」，直径 = 2r。
	const double Scale = double(ShapeDetail) / (2.0 * double(FeatureRadius));
	const double Noise01 = double(EvaluateAt(P * float(Scale)));
	if (VeinThreshold <= 0.0)
	{
		return float(Noise01);
	}
	return float(FMath::Clamp(
		(Noise01 - double(VeinThreshold)) / (1.0 - double(VeinThreshold)), 0.0, 1.0));
}

FVector SamplePlanetGeologyPlacement(FRandomStream& Rng, const float InDepthBias,
	const float InDepthCenter, const float InDepthSpread)
{
	const double DepthBias = FMath::Clamp(double(InDepthBias), 0.0, 0.9);
	const double DepthCenter = FMath::Clamp(double(InDepthCenter), 0.0, 1.0);
	const double DepthSpread = FMath::Max(double(InDepthSpread), 0.01);

	// 球面均匀方向；径向距离另抽，两者独立。
	const FVector Direction = Rng.GetUnitVector();

	// ∛U 让**体积**均匀（近地表体积大，基元自然更多）。
	double RadiusRatio = FMath::Pow(double(Rng.FRand()), 1.0 / 3.0);

	// 作者参数里的 DepthCenter 是「深度」（0 = 地表，1 = 球心），与半径比例
	// 互为补角 —— 带权时必须先换算成 1 - r 再比。
	if (DepthBias > 0.0)
	{
		for (int32 Attempt = 0; Attempt < 64; ++Attempt)
		{
			const double CandidateRadius = FMath::Pow(double(Rng.FRand()), 1.0 / 3.0);
			const double CandidateDepth = 1.0 - CandidateRadius;
			const double Offset = (CandidateDepth - DepthCenter) / DepthSpread;
			const double Weight =
				(1.0 - DepthBias) + DepthBias * FMath::Exp(-Offset * Offset);
			RadiusRatio = CandidateRadius;
			if (double(Rng.FRand()) < Weight)
			{
				break;
			}
		}
	}

	return Direction * float(RadiusRatio);
}

FIntVector PlanetGeologyToCell(const FVector& Point, const double CellSize)
{
	return FIntVector(
		FMath::FloorToInt(Point.X / CellSize),
		FMath::FloorToInt(Point.Y / CellSize),
		FMath::FloorToInt(Point.Z / CellSize));
}

void PlanetGeologyIndexBounds(const FBox& Bounds, const double CellSize,
	TMap<FIntVector, TArray<int32>>& Index, const int32 PrimitiveIndex,
	int32& InOutReferenceCount)
{
	const FIntVector MinCell = PlanetGeologyToCell(Bounds.Min, CellSize);
	const FIntVector MaxCell = PlanetGeologyToCell(Bounds.Max, CellSize);
	for (int32 X = MinCell.X; X <= MaxCell.X; ++X)
	{
		for (int32 Y = MinCell.Y; Y <= MaxCell.Y; ++Y)
		{
			for (int32 Z = MinCell.Z; Z <= MaxCell.Z; ++Z)
			{
				Index.FindOrAdd(FIntVector(X, Y, Z)).Add(PrimitiveIndex);
				++InOutReferenceCount;
			}
		}
	}
}

int32 UPlanetGeologyDistribution::ResolveSeed(const FName ElementId,
	const int32 DistributionIndex) const
{
	if (bSeedExplicit || Seed != 0)
	{
		return Seed;
	}
	const uint32 Mixed = uint32(GetTypeHash(ElementId))
		^ (uint32(DistributionIndex) * 0x9E3779B1u)
		^ uint32(GetTypeHash(GetClass()->GetFName()));
	return int32(Mixed & 0x7fffffffu);
}

#if WITH_EDITOR
void UPlanetGeologyDistribution::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	if (UPlanetGeologyPreset* Preset = GetTypedOuter<UPlanetGeologyPreset>())
	{
		// 漏了这条会同时坏两件事：观测预览不自动重建、地形缓存键的 Revision 不变。
		Preset->NotifyContentChanged();
	}
}
#endif
