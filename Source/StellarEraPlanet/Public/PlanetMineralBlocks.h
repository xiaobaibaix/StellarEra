// Copyright Voxel Plugin SAS. All Rights Reserved.
// 行星内部矿物块采样：纯函数，不碰世界、不建组件。

#pragma once

#include "CoreMinimal.h"

class FPlanetGeologyField;

/** 一个矿物方块：位置相对球心（厘米），归属哪个元素（运行期元素下标）。 */
struct FPlanetMineralBlock
{
	FVector LocationCm = FVector::ZeroVector;
	int32 ElementIndex = INDEX_NONE;
};

namespace PlanetMineralBlocks
{
	struct FBuildParams
	{
		/** 基准球面半径（厘米），与 AVoxelPlanetActor::GetSurfaceRadiusCm 一致。 */
		float SurfaceRadiusCm = 0.f;
		/** 一个体素多少厘米；地质深度用它从体素换算回厘米。 */
		float VoxelSizeCm = 0.f;
		/** 采样间距 / 表面半径。1/16 时格点约 33³。 */
		float SpacingRatio = 1.f / 16.f;
		/** 抖动与跳过判定的随机种子；同种子结果逐项相同。 */
		int32 Seed = 0;
	};

	struct FBuildResult
	{
		/** 每个元素一组方块；下标 = 运行期元素下标 = Sample.DominantIndex。 */
		TArray<TArray<FPlanetMineralBlock>> BlocksByElement;
		/** 球内的采样点总数，供日志与性能诊断。 */
		int32 SampleCount = 0;
	};

	/**
	 * 按地质场解析采样，生成球内矿物方块。
	 *
	 * 判据与地表着色完全一致：`DominantNorm > Appearance.ColorThreshold` 才留下，
	 * 元素归属取 `Sample.DominantIndex`。空地质场直接返回（不遍历格点）。
	 */
	void Build(const FPlanetGeologyField& Field, const FBuildParams& Params, FBuildResult& Out);
}
