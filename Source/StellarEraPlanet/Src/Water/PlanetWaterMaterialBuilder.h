// 水材质 M_PlanetWater 的构建器（只在编辑器里存在）。

#pragma once

#include "CoreMinimal.h"

#if WITH_EDITOR

/**
 * 重建 /Game/Code/Planet/Materials/M_PlanetWater 并保存。材质图完全由代码生成，
 * 所以可复现。既有资产走 LoadPackage 后覆盖 —— 对磁盘上已有的包用 CreatePackage
 * 会拿到未完全加载的空壳，SavePackage 必定失败且错误被 SAVE_NoError 吞掉。
 * 失败时返回 false 并把原因写进 OutError（不弹窗）。
 */
STELLARERAPLANET_API bool BuildPlanetWaterMaterial(FString& OutError);

#endif // WITH_EDITOR
