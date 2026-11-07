// Copyright Voxel Plugin SAS. All Rights Reserved.
// 矿量的区域采样：纯函数，不碰 UObject、不碰世界。

#pragma once

#include "CoreMinimal.h"

/** 一个区域的 AABB，单位与 Step 一致（本项目里是体素）。 */
struct FPlanetMineralRegionBox
{
	FVector3d Min = FVector3d::ZeroVector;
	FVector3d Max = FVector3d::ZeroVector;

	bool IsValid() const
	{
		return Min.X <= Max.X && Min.Y <= Max.Y && Min.Z <= Max.Z;
	}

	bool Contains(const FVector3d& Point) const
	{
		return Point.X >= Min.X && Point.X <= Max.X
			&& Point.Y >= Min.Y && Point.Y <= Max.Y
			&& Point.Z >= Min.Z && Point.Z <= Max.Z;
	}
};

/**
 * 在若干 AABB 的并集上按固定步长采样，数出谓词为真的点数。去重靠「对并集 AABB 只遍历一次 + 逐点判是否落在任一 AABB 内」：
 * 逐 AABB 各扫一遍会把重叠区数两遍，而矿脉相邻段的 AABB 必然重叠。
 * @param Step 采样步长，与 Boxes 同单位；≤ 0 时返回 0。
 * @param IsSolid 采样点是否计入；捕获环境由调用方保证调用期间有效。
 */
int64 CountMergedBoxSamples(
	const TArray<FPlanetMineralRegionBox>& Boxes,
	double Step,
	TFunctionRef<bool(const FVector3d& Point)> IsSolid);
