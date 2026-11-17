#include "GeoPalette.h"
#include "GeoLithology.h"
#include "GeoMineral.h"
#include "GeoPrimitive.h"

TArray<FColor> GeoPalette::BuildLUT(const TArray<FGeoMineralInfo>& Minerals)
{
	TArray<FColor> LUT;
	LUT.Init(FColor::Black, Width * Rows);

	// 行 0：岩性编号直接下标。行 0 的 0 号是「无」，调试色给洋红，材质不会在真实采样里用到它。
	for (int32 Id = 0; Id < GeoLithology::Count; ++Id)
	{
		LUT[Id] = GeoLithology::DebugColor(uint8(Id));
	}
	// 行 1：矿物编号 = 下标 + 1，0 号留黑；最多 255 种，越界不写（保持黑）。
	for (int32 Index = 0; Index < Minerals.Num() && Index + 1 < Width; ++Index)
	{
		LUT[Width + Index + 1] = Minerals[Index].Color;
	}
	// 行 2：线索编号 0..4。
	for (int32 Id = 0; Id < GeoClue::Count; ++Id)
	{
		LUT[2 * Width + Id] = GeoClue::DebugColor(uint8(Id));
	}
	return LUT;
}
