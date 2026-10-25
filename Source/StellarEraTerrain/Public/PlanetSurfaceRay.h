// Copyright Voxel Plugin SAS. All Rights Reserved.
// 行星表面的解析射线：球面求交 + 逐方向半径的不动点迭代。
// 纯数学，不碰 UObject，全部可无头测试。

#pragma once

#include "CoreMinimal.h"
#include "Templates/Function.h"

namespace PlanetSurfaceRay
{
	/**
	 * 射线与球面的第一个穿入点距离。打不到 / 起点在球内 / 整个球在背后 → −1。
	 * 起点正好在球面上且朝内也返回 −1 —— 穿入距离 0 不满足 Entry > 0（严格比较）。
	 *
	 * 本函数原来叫 AStarMapCameraPawn::RaySphereFirstHitCm（星图摄像机用它做
	 * 行星遮挡夹取）；搬到本模块后两边共用一个实现，星图那个函数只做转发。
	 */
	STELLARERATERRAIN_API double RaySphereFirstHit(
		const FVector& RayOrigin, const FVector& UnitDir,
		const FVector& SphereCenter, double SphereRadiusCm);

	/**
	 * 球面不平（地形起伏）时的解析命中距离。
	 *
	 * 先用 BaseRadiusCm 求一个球面交点，再按该方向上的真实半径重算交点，迭代
	 * Iterations 次。地形起伏远小于半径，固定点收敛得很快；某次迭代算出的半径
	 * 让射线打空时保留上一次的距离，而不是整体失败。
	 *
	 * 坐标：调用方给**同一原点的局部坐标**（起点、方向、半径都是），本函数不关心
	 * 它是世界空间还是体素空间。半径采样回调吃单位方向、吐半径。
	 */
	STELLARERATERRAIN_API bool SolveAnalyticHit(
		const FVector& LocalOrigin,
		const FVector& LocalDir,
		double BaseRadiusCm,
		int32 Iterations,
		TFunctionRef<double(const FVector& UnitDirection)> RadiusAtDirection,
		double& OutDistanceCm);
}
