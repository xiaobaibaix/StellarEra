// Copyright Voxel Plugin SAS. All Rights Reserved.

#include "PlanetMineralBlocks.h"

#include "PlanetGeologyField.h"
#include "Math/RandomStream.h"

namespace
{
	/** 球心附近方向退化：半径 5% 以内不采样。 */
	constexpr double MinDirectionalRadiusRatio = 0.05;
}

void PlanetMineralBlocks::Build(const FPlanetGeologyField& Field, const FBuildParams& Params,
	FBuildResult& Out)
{
	Out.BlocksByElement.Reset();
	Out.SampleCount = 0;

	const double RadiusCm = double(Params.SurfaceRadiusCm);
	const double StepCm = RadiusCm * double(Params.SpacingRatio);
	if (RadiusCm <= 0.0 || Params.VoxelSizeCm <= 0.0 || StepCm <= 0.0)
	{
		return;
	}

	if (Field.IsEmpty())
	{
		return;
	}

	Out.BlocksByElement.SetNum(Field.NumElements());

	const double ColorThreshold = double(Field.GetAppearance().ColorThreshold);
	const double VoxelSizeCm = double(Params.VoxelSizeCm);
	const int32 Steps = FMath::CeilToInt(double(RadiusCm / StepCm));

	FRandomStream Random(Params.Seed);

	for (int32 Z = -Steps; Z <= Steps; ++Z)
	{
		for (int32 Y = -Steps; Y <= Steps; ++Y)
		{
			for (int32 X = -Steps; X <= Steps; ++X)
			{
				// 每个格点固定消耗三次随机数，无论该点是否被跳过：
				// 跳过点也抽签，同种子的两次 Build 才能逐项一致。
				const double JitterX = double(Random.FRandRange(-0.5f, 0.5f));
				const double JitterY = double(Random.FRandRange(-0.5f, 0.5f));
				const double JitterZ = double(Random.FRandRange(-0.5f, 0.5f));

				const FVector Point(
					(double(X) + JitterX) * StepCm,
					(double(Y) + JitterY) * StepCm,
					(double(Z) + JitterZ) * StepCm);

				const double Distance = Point.Size();
				if (Distance > RadiusCm || Distance < MinDirectionalRadiusRatio * RadiusCm)
				{
					continue;
				}

				++Out.SampleCount;

				const FVector UnitDir = Point / Distance;
				const double Depth = (RadiusCm - Distance) / VoxelSizeCm;
				const FPlanetGeologySample Sample = Field.Sample(UnitDir, Depth);

				if (Sample.DominantNorm <= ColorThreshold
					|| !Out.BlocksByElement.IsValidIndex(Sample.DominantIndex))
				{
					continue;
				}

				FPlanetMineralBlock& Block =
					Out.BlocksByElement[Sample.DominantIndex].AddDefaulted_GetRef();
				Block.LocationCm = Point;
				Block.ElementIndex = Sample.DominantIndex;
			}
		}
	}
}
