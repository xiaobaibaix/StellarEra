// 分块编辑的逐格谓词：与 VoxelFree 的 SphereEdit / BoxEdit 逐字同式。
// 放 Private 是因为参数用插件的 FVoxelValue（Voxel 是私有依赖）；运行期与单测共用这一份实现。

#pragma once

#include "CoreMinimal.h"
#include "VoxelIntBox.h"
#include "VoxelValue.h"
#include "PlanetProgressiveEdit.h"

namespace PlanetProgressiveKernel
{
	/** SphereEdit 的两个阈值：(R + 2)² 与 max(R - 2, 0)²。 */
	void GetSphereThresholds(float Radius, float& OutSquaredRadiusPlus2, float& OutSquaredRadiusMinus2);

	/**
	 * 球的逐格取值，照抄 VoxelSphereToolsImpl::SphereEdit<false>：
	 *   SqDist >  (R+2)² → 原样返回；<= (R-2)² → 填 Full() / 挖 Empty()
	 *   其余 → clamp(R-D, -2, 2)/2，填取反，再 MergeAsset(旧, 新, 挖) —— 挖取 max、填取 min
	 * 参数是距离而非坐标，便于单测直接喂代表点（含 ±2 边界内外）。
	 */
	FVoxelValue EvaluateSphereValue(
		float Radius,
		float SquaredRadiusPlus2,
		float SquaredRadiusMinus2,
		float SquaredDistance,
		FVoxelValue CurrentValue,
		bool bPlace);

	/**
	 * 方块的逐格取值，照抄 VoxelBoxToolsImpl::BoxEdit<bAdd>：
	 *   任一轴贴边（== Min 或 == MaxExclusive-1）→ 空实翻转时置 0（表面），否则不动；内部 → Full()/Empty()
	 * 贴边按整笔编辑的包围盒判，不是当前块：按块判会把每个块边界刷成表面，挖出来是一堆方块印。
	 * 调用方须保证 Voxel 落在 [BoxMin, BoxMaxExclusive) 内，函数本身不判断盒外格子。
	 */
	FVoxelValue EvaluateBoxValue(
		const FIntVector& Voxel,
		const FIntVector& BoxMin,
		const FIntVector& BoxMaxExclusive,
		FVoxelValue CurrentValue,
		bool bPlace);

	/**
	 * 对一格按顺序应用 Ops[0 .. NumOps-1] 返回最终值。顺序即语义：方块贴边判定读「当前值」，
	 * 必须与依次执行 NumOps 次独立编辑逐格完全一致。
	 * 传裸指针 + 数量而非 TArray：调用方在 ParallelSet 的 lambda 里用，不能碰可能重分配的容器。
	 */
	FVoxelValue EvaluateOps(
		const FPlanetEditOp* Ops,
		int32 NumOps,
		int32 X, int32 Y, int32 Z,
		FVoxelValue CurrentValue,
		bool bPlace);

	/**
	 * 体素中心到球心的平方距离。与插件同式：FVector 是 double，算完落到 float。
	 */
	float SquaredDistanceTo(const FVector& CenterVoxels, int32 X, int32 Y, int32 Z);
}
