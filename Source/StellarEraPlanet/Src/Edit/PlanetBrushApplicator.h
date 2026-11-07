// Copyright Voxel Plugin SAS. All Rights Reserved.
// 一笔挖 / 放的执行：命中点 → Voxel 工具 → 通知重建与保存。

#pragma once

#include "CoreMinimal.h"
#include "PlanetEditBrush.h"

class AVoxelPlanetActor;

/**
 * 角色走碰撞射线、星图走解析射线，命中点来源完全不同，但「命中点 → 体素工具 → 通知重建/
 * 保存」逐字相同。抽出来两边只需产出 (Planet, CenterWorld)。
 */
namespace PlanetBrushApplicator
{
	/**
	 * 在 CenterWorld 落一笔，返回是否真的改到了体素。CenterWorld 由调用方按
	 * GetEditCenterWorld 算好：挖 = 命中点，放 = 沿命中法线外移半边长。
	 * 空操作返回 false 且不调 NotifyVoxelEdited（表面刷新 + 未保存标记）；渲染仍按包围盒刷一次。
	 * 判据是写入谓词逐格比对旧值（DidChangeAnything）：只记真的变了的格子，不像
	 * OutModifiedValues 在大笔刷吃 GB 内存，也不像「中心格前后相同」漏掉只改外圈的笔。
	 * @param OutEditedBounds  真的改到时填整笔包围盒（体素空间，Min 含 / MaxExclusive 不含）。
	 * @param bUpdateRender    false = 只改数据不刷渲染；连续挖期间用，改在松手时对并集刷一次。
	 */
	STELLARERAPLANET_API bool Apply(
		AVoxelPlanetActor& Planet,
		const FVector& CenterWorld,
		EPlanetBrushShape Shape,
		int32 SizeVoxels,
		int32 MinSizeVoxels,
		int32 MaxSizeVoxels,
		bool bPlace,
		bool bMultiThreaded,
		FPlanetEditVoxelBox* OutEditedBounds = nullptr,
		bool bUpdateRender = true);
}
