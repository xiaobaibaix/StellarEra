// 材质调色板：行 0 岩性 / 行 1 矿物 / 行 2 线索。材质按行中心 v 采样，改布局要同步 MF_GeoDecode。
#pragma once
#include "CoreMinimal.h"

struct FGeoMineralInfo;

namespace GeoPalette
{
	/** LUT 尺寸：创建 UTexture2D 与材质 u/v 采样都按它。 */
	constexpr int32 Width = 256;
	constexpr int32 Rows = 3;

	/** 256×3 行优先 BGRA；行 1 下标 0 恒为黑（矿物编号 = 下标 + 1）。 */
	TArray<FColor> BuildLUT(const TArray<FGeoMineralInfo>& Minerals);
}
