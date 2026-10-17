// 星球表面探测器 —— 实现。

#include "PlanetSurfaceProbe.h"
#include "VoxelWorld.h"
#include "VoxelData/VoxelDataIncludes.h"

FSurfaceProbeResult FPlanetSurfaceProbe::ProbeVoxelWorld(
	AVoxelWorld* VoxelWorld,
	const FVector& SearchOriginVoxels,
	const FVector& DirectionVoxels,
	double StepVoxels,
	double MaxDistanceVoxels)
{
	FSurfaceProbeResult Result;

	if (!VoxelWorld)
	{
		return Result;
	}

	FVector Direction = DirectionVoxels;
	if (Direction.IsNearlyZero())
	{
		return Result;
	}
	Direction = Direction.GetSafeNormal();

	const double Step = FMath::Max(StepVoxels, 0.5);
	const double MaxDist = FMath::Max(MaxDistanceVoxels, Step);

	FVoxelData& Data = VoxelWorld->GetData();

	// 步进探测：从起点沿方向前进，找密度由「空」变「实」的转折点。
	//
	// 为什么不用物理射线：体素世界不是标准碰撞体，
	// 物理射线要经过碰撞网格，慢且在远处精度差。
	// 步进直接问体素数据，精确且可控。
	double PrevValue = 1.0;   // 假设起点在空气中
	bool bFoundPrev = false;

	for (double Dist = 0.0; Dist <= MaxDist; Dist += Step)
	{
		const FVector SamplePoint = SearchOriginVoxels + Direction * Dist;

		// 体素坐标取整。密度值直接问生成器实例。
		const FIntVoxel Voxel(FMath::RoundToInt(SamplePoint.X),
			FMath::RoundToInt(SamplePoint.Y),
			FMath::RoundToInt(SamplePoint.Z));

		float Density = 0.f;
		if (Data.GetValue(Voxel, /*LOD*/ 0, FVoxelItemStack::EmptyRef, Density))
		{
			// 空 = 正，实 = 负
			const bool bIsSolid = (Density <= 0.f);

			if (bFoundPrev && PrevValue > 0.f && bIsSolid)
			{
				// 找到了表面。二分细化交界点，让边界更精确。
				double Low = Dist - Step;
				double High = Dist;
				for (int32 i = 0; i < 6; ++i)
				{
					const double Mid = (Low + High) * 0.5;
					const FVector MidPoint = SearchOriginVoxels + Direction * Mid;

					float MidDensity = 0.f;
					if (Data.GetValue(
						FIntVoxel(FMath::RoundToInt(MidPoint.X),
							FMath::RoundToInt(MidPoint.Y),
							FMath::RoundToInt(MidPoint.Z)),
						0, FVoxelItemStack::EmptyRef, MidDensity))
					{
						if (MidDensity <= 0.f)
						{
							High = Mid;
						}
						else
						{
							Low = Mid;
						}
					}
					else
					{
						break;
					}
				}

				Result.bHit = true;
				Result.VoxelLocation = SearchOriginVoxels + Direction * High;

				// 转回世界坐标
				Result.WorldLocation = VoxelWorld->LocalToGlobalFloat(Result.VoxelLocation);

				// 表面法线 = 从球心指向命中点的径向方向。
				// 对球形星球这是精确的——比数值梯度更稳定。
				Result.WorldNormal = Result.VoxelLocation.GetSafeNormal();

				// 距球心的距离，约等于「当地海拔 + 基准半径」
				Result.SurfaceDistanceVoxels = Result.VoxelLocation.Size();

				return Result;
			}

			PrevValue = Density;
			bFoundPrev = true;
		}
		else
		{
			// 数据不存在说明这块还没生成（可能在 LOD 范围外）
			bFoundPrev = false;
			PrevValue = 1.0;
		}
	}

	return Result;
}

double FPlanetSurfaceProbe::EstimateSurfaceRadius(
	double Radius,
	const FVector& Direction,
	TFunction<double(const FVector&)> TerrainRadiusOffset)
{
	const FVector N = Direction.GetSafeNormal();
	if (N.IsNearlyZero())
	{
		return Radius;
	}

	if (!TerrainRadiusOffset)
	{
		return Radius;
	}

	return Radius + TerrainRadiusOffset(N);
}

float FPlanetSurfaceProbe::ComputeDigDifficulty(double DepthVoxels, float Hardness)
{
	// DepthVoxels > 0 表示在基准半径之外（山上），
	// < 0 表示深入地下。越深越难挖。
	//
	// 线性 + 指数混合：浅层几乎不增加难度，深层增长很快。
	const double Depth = FMath::Max(-DepthVoxels, 0.0);
	const double DepthFactor = 1.0 + Depth * 0.08 + Depth * Depth * 0.0008;

	const float HardnessFactor = FMath::Max(Hardness, 0.1f);

	return float(DepthFactor * HardnessFactor);
}

float FPlanetSurfaceProbe::SampleOreAbundance(
	const FVector& UnitDirection,
	double PlanetSeed,
	EOreType Ore,
	float GlobalAbundance)
{
	if (GlobalAbundance <= 0.f)
	{
		return 0.f;
	}

	const FVector N = UnitDirection.GetSafeNormal();
	if (N.IsNearlyZero())
	{
		return 0.f;
	}

	// 用廉价的三重正弦模拟矿脉分布。
	// 不用噪声库是为了让这个类保持零依赖，
	// 且矿脉分布不需要高质量噪声——低频正弦足够。
	//
	// 每种矿用不同的频率组合，保证不同矿的分布不相关。
	const int32 OreIndex = int32(Ore);
	const double SeedOffset = PlanetSeed * 0.017 + OreIndex * 13.37;

	const double A = N.X * (2.1 + OreIndex * 0.3) + SeedOffset;
	const double B = N.Y * (3.3 + OreIndex * 0.5) + SeedOffset * 1.7;
	const double C = N.Z * (1.7 + OreIndex * 0.2) + SeedOffset * 2.3;

	// 三层不同频率，制造"有些地方富有些地方贫"的效果
	double Pattern = FMath::Sin(A) * 0.5;
	Pattern += FMath::Sin(B * 1.7 + 1.3) * 0.3;
	Pattern += FMath::Sin(C * 2.9 + 2.1) * 0.2;

	// 映射到 0~1
	const double Normalized = (Pattern + 1.0) * 0.5;

	// 乘全局丰富度
	return float(Normalized * GlobalAbundance);
}
