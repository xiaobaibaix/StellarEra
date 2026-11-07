// Copyright Voxel Plugin SAS. All Rights Reserved.
// 行星表面的解析射线：球面求交 + 逐方向半径的不动点迭代。
// 纯数学，不碰 UObject，全部可无头测试。

#pragma once

#include "CoreMinimal.h"
#include "Templates/Function.h"

namespace PlanetSurfaceRay
{
	/**
	 * 射线与球面的第一个穿入点距离。打不到 / 起点在球内 / 整个球在背后 → −1；
	 * 起点在球面上朝内也是 −1（Entry > 0 严格比较）。星图摄像机与体素射线共用。
	 */
	STELLARERAPLANET_API double RaySphereFirstHit(
		const FVector& RayOrigin, const FVector& UnitDir,
		const FVector& SphereCenter, double SphereRadiusCm);

	/** 射线与球面的**穿出点**（远根）距离。与 FirstHit 唯一区别：起点在球内时
	 * 照样返回正数 —— 体素行进需要它当几何终点。打不到 / 在背后 → −1。 */
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

	/** 沿射线找空/实翻转时两方向各允许走多远。**无人为深度上限**：终点是包围球与
	 * 射线交点。擦不到包围球时退化为 AnalyticDistanceCm + BoundingRadiusCm。 */
	STELLARERAPLANET_API FMarchStepRange ComputeMarchStepRange(
		const FVector& RayOrigin, const FVector& UnitDir,
		double AnalyticDistanceCm, double BoundingRadiusCm, double VoxelSizeCm);

	/**
	 * 沿一条线找空/实翻转：起点为空往负向找第一格实心，为实往正向找第一格空气。
	 * **「正方向」含义随调用方而变**（径向查询向外、射线行进向内），两处符号相反。
	 * 找到时 OutBoundaryStep = ±(步 − 0.5)，误差 ≤ 1 体素；走完预算未翻转返回 false
	 * 且不写输出。抽样回调的锁归调用方。
	 */
	STELLARERAPLANET_API bool FindSurfaceBoundaryStep(
		int32 MaxForwardSteps, int32 MaxBackwardSteps,
		TFunctionRef<bool(int32 Step)> SampleEmptyAt,
		double& OutBoundaryStep);

	/** 球面不平时的解析命中距离：先用 BaseRadiusCm 求交点，再按该方向真实半径
	 * 重算，迭代 Iterations 次。某次半径让射线打空时保留上一次距离而非整体失败。 */
	STELLARERAPLANET_API bool SolveAnalyticHit(
		const FVector& LocalOrigin,
		const FVector& LocalDir,
		double BaseRadiusCm,
		int32 Iterations,
		TFunctionRef<double(const FVector& UnitDirection)> RadiusAtDirection,
		double& OutDistanceCm);
}
