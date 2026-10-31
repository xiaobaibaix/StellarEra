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
	STELLARERAPLANET_API double RaySphereFirstHit(
		const FVector& RayOrigin, const FVector& UnitDir,
		const FVector& SphereCenter, double SphereRadiusCm);

	/**
	 * 射线与球面的**穿出点**（远根）距离。打不到 / 穿出点在射线背后 → −1。
	 *
	 * 与 RaySphereFirstHit 的差别只有一条：起点在球内时这里**照样返回正数**
	 * （就是穿出点）。体素射线行进需要它当「几何终点」—— 从球内出发的射线
	 * 也必须有尽头，否则扫描无处可停。
	 */
	STELLARERAPLANET_API double RaySphereFarHit(
		const FVector& RayOrigin, const FVector& UnitDir,
		const FVector& SphereCenter, double SphereRadiusCm);

	/** 体素射线行进的两个方向各自的步数上界（单位：体素）。 */
	struct FMarchStepRange
	{
		/** 从解析命中点朝里（沿射线）能走多少步 = 到包围球远根的距离。 */
		int32 InwardMaxSteps = 1;

		/** 从解析命中点朝外（逆射线）能走多少步 = 到包围球近根、但不越过射线原点。 */
		int32 BackwardMaxSteps = 1;
	};

	/**
	 * 沿射线找空/实翻转时，两个方向各允许走多远。**没有人为深度上限**：
	 * 终点就是半径 BoundingRadiusCm 的球与射线的交点，所以挖多深都能重新瞄回来。
	 *
	 * BoundingRadiusCm 由调用方给（行星用表面半径 × 1.25，与包围球同一份余量）；
	 * VoxelSizeCm 是 1 个体素的边长。射线擦不到包围球时退化为
	 * AnalyticDistanceCm + BoundingRadiusCm，而不是静默失败。
	 */
	STELLARERAPLANET_API FMarchStepRange ComputeMarchStepRange(
		const FVector& RayOrigin, const FVector& UnitDir,
		double AnalyticDistanceCm, double BoundingRadiusCm, double VoxelSizeCm);

	/**
	 * 沿一条线找空/实翻转。SampleEmptyAt(Step) 抽样，Step 是相对起点（Step 0）
	 * 的体素步数，正 = 沿查询方向。
	 *
	 * 规则与 RaycastSurfaceCm 相同：起点为空就往负方向走、找第一格实心；
	 * 起点为实则往正方向走、找第一格空气。
	 *
	 * 注意「正方向」在两处的物理含义不同：本函数的查询方向由调用方定义
	 * （径向查询时是**向外**），RaycastSurfaceCm 沿入射射线行进（**向内**），
	 * 所以「往起点所在那一侧走」这条规则在两处的符号正好相反。
	 *
	 * 找到时 OutBoundaryStep = ±(边界步 − 0.5)，即两个采样点的中点 —— 真实表面
	 * 一定在这两格之间，误差 ≤ 1 体素。走完预算仍未翻转时返回 false
	 * （挖穿 / 整段全实）且不写 OutBoundaryStep，调用方据此退回解析值。
	 *
	 * 抽样回调的锁由调用方自己管（可以每段一取）—— 本函数只管走哪边、走多远。
	 */
	STELLARERAPLANET_API bool FindSurfaceBoundaryStep(
		int32 MaxForwardSteps, int32 MaxBackwardSteps,
		TFunctionRef<bool(int32 Step)> SampleEmptyAt,
		double& OutBoundaryStep);

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
	STELLARERAPLANET_API bool SolveAnalyticHit(
		const FVector& LocalOrigin,
		const FVector& LocalDir,
		double BaseRadiusCm,
		int32 Iterations,
		TFunctionRef<double(const FVector& UnitDirection)> RadiusAtDirection,
		double& OutDistanceCm);
}
