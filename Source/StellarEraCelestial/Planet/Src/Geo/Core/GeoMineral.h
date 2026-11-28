// 矿物的快照侧表示：调色板（只供显示）与覆层（贴地表的薄矿层）。矿体本身用 FGeoPrimitive。
#pragma once
#include "CoreMinimal.h"
#include "GeoField2D.h"
#include "GeoRng.h"

/** 下标 = 矿物编号 − 1；编号 0 恒为「无矿」。来自配方的矿物目录，只用于显示，不进内容哈希。 */
struct FGeoMineralInfo
{
	FString Name;
	FColor Color = FColor::White;
};

/** 覆层：地表下 [0, DepthVoxels] 体素内、Mask > 0.5 处有这种矿。锰结核、风化壳、砂矿走这条。 */
struct FGeoMineralOverlay
{
	uint8 MineralId = 0;
	uint8 Grade = 0;
	float DepthVoxels = 2.f;
	FGeoField2D Mask;

	uint64 Hash() const
	{
		uint64 H = GeoHash::Combine(uint64(MineralId) | (uint64(Grade) << 8), GeoHash::Float(DepthVoxels));
		return GeoHash::Combine(H, Mask.Hash());
	}
};
