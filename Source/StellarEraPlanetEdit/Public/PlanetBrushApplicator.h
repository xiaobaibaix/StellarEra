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
	 * 空操作（在岩石里放石头、挖空气）返回 false，且**不**调 NotifyVoxelEdited
	 *（表面网格刷新 + 未保存标记）；渲染仍会按请求包围盒刷新一次。
	 *
	 * 判据是写入谓词逐格比对旧值得到的结果（FPlanetProgressiveEdit::
	 * DidChangeAnything）：只记「真的变了的格子」，既不像装满的
	 * OutModifiedValues 那样在大笔刷时吃掉 GB 级内存，也不会像「中心格
	 * 前后是否相同」那样漏掉只改到外圈的笔刷。
	 *
	 * @param OutEditedBounds  可选。真的改到体素时填整笔编辑的包围盒（体素空间，
	 *                         Min 含 / MaxExclusive 不含）；没改到时不动。
	 * @param bUpdateRender    false = 只改数据、不请求渲染刷新。连续挖期间用：
	 *                         每 0.12 s 刷一次同一个 chunk 会把它重建好几遍，
	 *                         改成松手时对并集刷一次。
	 */
	STELLARERAPLANETEDIT_API bool Apply(
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
