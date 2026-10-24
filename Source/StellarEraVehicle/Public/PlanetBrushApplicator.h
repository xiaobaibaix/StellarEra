// Copyright Voxel Plugin SAS. All Rights Reserved.
// 一笔挖 / 放的执行：命中点 → Voxel 工具 → 通知重建与保存。

#pragma once

#include "CoreMinimal.h"
#include "PlanetEditBrush.h"

class AVoxelPlanetActor;

/**
 * 角色走碰撞射线、星图走解析射线，两条路径的**命中点来源**完全不同，
 * 但「命中点 → 体素工具 → 通知重建/保存」这一段逐字相同。
 *
 * 这段代码留在组件里的话，组件就得同时懂两种求交；抽出来之后，
 * 两边都只需要产出一个 (Planet, CenterWorld)。
 */
namespace PlanetBrushApplicator
{
	/**
	 * 在 CenterWorld 落一笔，返回是否真的改到了体素。
	 *
	 * CenterWorld 由调用方按 GetEditCenterWorld 算好：挖 = 命中点本身，
	 * 放 = 沿命中法线外移一个半边长。
	 *
	 * 空操作（在岩石里放石头、挖空气）返回 false，且**不**触发网格重建与
	 * 存档 —— 判据是工具写回的 OutModifiedValues 数量：它只记录值真的变了的
	 * 体素。插件的 OutEditedBounds 不能用，宏会无条件把它设成请求包围盒。
	 */
	STELLARERAVEHICLE_API bool Apply(
		AVoxelPlanetActor& Planet,
		const FVector& CenterWorld,
		EPlanetBrushShape Shape,
		int32 SizeVoxels,
		int32 MinSizeVoxels,
		int32 MaxSizeVoxels,
		bool bPlace,
		bool bMultiThreaded);
}
