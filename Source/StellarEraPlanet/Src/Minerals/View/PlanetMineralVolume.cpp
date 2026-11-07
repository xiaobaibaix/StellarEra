// Copyright Voxel Plugin SAS. All Rights Reserved.

#include "PlanetMineralVolume.h"

int64 CountMergedBoxSamples(
	const TArray<FPlanetMineralRegionBox>& Boxes,
	double Step,
	TFunctionRef<bool(const FVector3d& Point)> IsSolid)
{
	if (Step <= 0.0)
	{
		return 0;
	}

	// 遍历所有有效盒子的并集 AABB，再逐点判归属：重叠区因此只数一遍。
	FVector3d UnionMin = FVector3d::ZeroVector;
	FVector3d UnionMax = FVector3d::ZeroVector;
	bool bHasValidBox = false;
	for (const FPlanetMineralRegionBox& Box : Boxes)
	{
		if (!Box.IsValid())
		{
			continue;
		}

		if (!bHasValidBox)
		{
			UnionMin = Box.Min;
			UnionMax = Box.Max;
			bHasValidBox = true;
			continue;
		}

		UnionMin.X = FMath::Min(UnionMin.X, Box.Min.X);
		UnionMin.Y = FMath::Min(UnionMin.Y, Box.Min.Y);
		UnionMin.Z = FMath::Min(UnionMin.Z, Box.Min.Z);
		UnionMax.X = FMath::Max(UnionMax.X, Box.Max.X);
		UnionMax.Y = FMath::Max(UnionMax.Y, Box.Max.Y);
		UnionMax.Z = FMath::Max(UnionMax.Z, Box.Max.Z);
	}

	if (!bHasValidBox)
	{
		return 0;
	}

	int64 Count = 0;
	for (double Z = UnionMin.Z; Z <= UnionMax.Z; Z += Step)
	{
		for (double Y = UnionMin.Y; Y <= UnionMax.Y; Y += Step)
		{
			for (double X = UnionMin.X; X <= UnionMax.X; X += Step)
			{
				const FVector3d Point(X, Y, Z);

				bool bInsideAnyBox = false;
				for (const FPlanetMineralRegionBox& Box : Boxes)
				{
					if (Box.IsValid() && Box.Contains(Point))
					{
						bInsideAnyBox = true;
						break;
					}
				}
				if (!bInsideAnyBox)
				{
					continue;
				}

				if (IsSolid(Point))
				{
					++Count;
				}
			}
		}
	}

	return Count;
}
