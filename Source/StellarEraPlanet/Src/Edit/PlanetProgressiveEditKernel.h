// 分块编辑的逐格谓词：与 VoxelFree 的 SphereEdit / BoxEdit 逐字同式。
//
// 放 Private 不放 Public：参数与返回值都是插件的 FVoxelValue，而 Voxel 是
// StellarEraPlanetEdit 的**私有**依赖，不允许从公开头泄漏出去
//（公开头 PlanetEditBrush.h 里同样的取舍说明）。
//
// 这一份实现同时被运行期（PlanetProgressiveEdit.cpp）与单测
// （PlanetProgressiveEditTests.cpp，与插件原函数逐格比对）使用 ——
// 测试钉住的就是真正在跑的那条公式，不存在第二份实现。

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
	 * 球的逐格取值。照抄 VoxelSphereToolsImpl::SphereEdit<false> 的 lambda：
	 *
	 *   SqDist >  (R+2)²  → 原样返回（不改）
	 *   SqDist <= (R-2)²  → 填 Full() / 挖 Empty()
	 *   其余              → clamp(R - D, -2, 2) / 2，填时取反
	 *                       再 MergeAsset(旧值, 新值, 挖) —— 挖取 max、填取 min
	 *
	 * 参数是距离而不是坐标，是为了让单测能直接喂代表点（含 ±2 边界内外）。
	 */
	FVoxelValue EvaluateSphereValue(
		float Radius,
		float SquaredRadiusPlus2,
		float SquaredRadiusMinus2,
		float SquaredDistance,
		FVoxelValue CurrentValue,
		bool bPlace);

	/**
	 * 方块的逐格取值。照抄 VoxelBoxToolsImpl::BoxEdit<bAdd>：
	 *
	 *   任一轴贴边（== Min 或 == MaxExclusive - 1）
	 *       → 新值与旧值空实相反时置 0（表面），否则不动
	 *   内部 → 填 Full() / 挖 Empty()
	 *
	 * **贴边按整笔编辑的包围盒判**，不是按当前块的包围盒 —— 块只是执行单位，
	 * 语义的唯一来源是整笔编辑的盒子。按块判会把每个块的边界都刷成表面，
	 * 挖出来是一堆方块印而不是一个大坑。
	 * 调用方必须保证 Voxel 落在 [BoxMin, BoxMaxExclusive) 内：函数本身不判断盒外格子。
	 */
	FVoxelValue EvaluateBoxValue(
		const FIntVector& Voxel,
		const FIntVector& BoxMin,
		const FIntVector& BoxMaxExclusive,
		FVoxelValue CurrentValue,
		bool bPlace);

	/**
	 * 对一格按顺序应用 Ops[0 .. NumOps-1]，返回最终值。
	 *
	 * 顺序就是语义：方块笔刷的贴边判定读「当前值」，所以必须与依次执行
	 * NumOps 次独立编辑逐格完全一致。
	 *
	 * 传裸指针 + 数量而不是 TArray：调用方在 ParallelSet 的 lambda 里用，
	 * 那里不能碰可能被重分配的容器。
	 */
	FVoxelValue EvaluateOps(
		const FPlanetEditOp* Ops,
		int32 NumOps,
		int32 X, int32 Y, int32 Z,
		FVoxelValue CurrentValue,
		bool bPlace);

	/**
	 * 体素中心到球心的平方距离。与插件同式：
	 * `FVector(X - Position.X, Y - Position.Y, Z - Position.Z).SizeSquared()`
	 * —— FVector 是 double，算完落到 float。
	 */
	float SquaredDistanceTo(const FVector& CenterVoxels, int32 X, int32 Y, int32 Z);
}
