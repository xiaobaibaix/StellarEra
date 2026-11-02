// 水材质 M_PlanetWater 的构建器（只在编辑器里存在）。

#pragma once

#include "CoreMinimal.h"

#if WITH_EDITOR

/**
 * 重建 /Game/Code/Planet/Materials/M_PlanetWater 并保存。
 *
 * 与 PlanetAtmosphereMaterialBuilder 同一套路：材质图完全由代码生成，改参数就
 * 重跑一次命令 —— 于是这张材质图是可复现的，不是某个资产里的手工艺品。
 *
 * 既有资产会被**重新加载后覆盖**（不是「找不到才新建」）：包在磁盘上但没进内存时
 * CreatePackage 会拿到一个未完全加载的包，SavePackage 必定失败，错误还会被
 * SAVE_NoError 吞掉。所以这里按资产是否存在分两条路径，并在日志里说明走的是哪条。
 *
 * 成功时由本函数把「新建 / 重建」与资产路径写进日志；失败时返回 false 并把原因
 * 写进 OutError（不弹窗），错误日志由控制台命令按需打。
 */
STELLARERAPLANET_API bool BuildPlanetWaterMaterial(FString& OutError);

#endif // WITH_EDITOR
