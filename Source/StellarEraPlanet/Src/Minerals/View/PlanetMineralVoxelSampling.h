// 矿物等值面的采样核心：给定注入的回调，求一个体素的密度值。
// 刻意做成纯函数 + 注入回调：单元测试不必建体素世界或地质资产，注入两个 lambda 就能覆盖全部早退分支。

#pragma once

#include "CoreMinimal.h"
#include "Templates/Function.h"

/** 一次矿物求值所需的全部输入，纯值类型可跨线程共享（回调必须线程安全）。坐标全在行星体素索引空间（Center 恒为原点）。 */
struct FPlanetMineralVoxelParams
{
	/** 该元素所有区域的 AABB（归一化空间，半径 1 = 行星基准球面）。空数组 = 这个元素没有矿。 */
	TArray<FBox> RegionBoxes;

	/** 该元素在归一化点的强度 ∈ [0,1]，为空时求值恒返回空气；运行期绑 SampleElementNormAtNormalized。 */
	TFunction<float(const FVector& NormalizedPoint)> SampleElementNorm;

	/** 行星在该体素索引处是否实心，为空时视作实心（漏绑不该让整颗星没矿）。运行期绑行星的 FVoxelData 读取。 */
	TFunction<bool(const FIntVector& VoxelIndex)> IsSolidAtPlanetVoxel;

	/** 行星基准半径（体素数）。≤ 0 时求值恒返回空气。 */
	double RadiusVoxels = 0.0;

	/** 等值线，传 GetAppearance().ColorThreshold。不能是 0：矿体外地质场恒 0，没填会整颗星贴在等值面上。 */
	double IsoValue = 0.22;

	/** 软化带宽，调用方传 GetAppearance().FadeRange。≤ 0 时按 1e-4 处理。 */
	double FadeRange = 0.15;
};

namespace PlanetMineralVoxel
{
	/** 所有早退分支返回的空气值。取正数而非 0：等值面抽取只看符号，恒正保证「空气」不会被浮点误差当成实心。 */
	constexpr double AirValue = 1.0;

	/** 归一化点是否落在任一区域盒内。空数组恒 false。 */
	STELLARERAPLANET_API bool IsInsideAnyRegion(
		const TArray<FBox>& RegionBoxes, const FVector& NormalizedPoint);

	/**
	 * 求一个体素的密度值，负 = 实心（是矿）、正 = 空气。早退顺序按代价：区域 AABB → 行星密度 → 地质场求值；
	 * 绝大多数体素在第一步判空，这是本方案比 marching cubes 便宜的主要来源。
	 * 返回值已夹到 FVoxelValue 的 int16 量程，调用方不必再夹。
	 */
	STELLARERAPLANET_API double EvaluateVoxel(
		const FPlanetMineralVoxelParams& Params, const FIntVector& VoxelIndex);
}
