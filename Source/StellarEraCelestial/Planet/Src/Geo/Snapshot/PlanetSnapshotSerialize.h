// 把烘焙快照整体写成二进制，供 UGeoBakeCache 落盘/读回，跳过重复烘焙。
#pragma once
#include "CoreMinimal.h"

struct FPlanetSnapshot;

namespace GeoBakeCacheFormat
{
	/** 改了任何阶段的产出逻辑就 +1：旧缓存会因版本不一致被判失效。 */
	constexpr int32 Version = 2;
}

/** 序列化成 blob（约 100+ MB）；失败返回 false。 */
bool SerializePlanetSnapshot(const FPlanetSnapshot& Snapshot, TArray<uint8>& OutBlob);

/** 反序列化；magic/版本不符或数据损坏返回空。原语索引会按 Prims 重建。 */
TSharedPtr<FPlanetSnapshot> DeserializePlanetSnapshot(const TArray<uint8>& Blob);
