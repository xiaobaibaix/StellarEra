// Copyright Voxel Plugin SAS. All Rights Reserved.
// 行星地质场：元素表 + 行星参数合成的只读采样场。

#pragma once

#include "CoreMinimal.h"
#include "FastNoise/VoxelFastNoise.h"
#include "PlanetGeologyTypes.h"

/**
 * 一颗行星的地质场。
 *
 * 由 Content/Geology/Elements.json（元素表）与 Planets/<preset>.json（行星参数）
 * 合成，构造后完全不可变；生成器实例用 TSharedPtr<const ...> 在后台线程共享。
 *
 * 设计见 docs/superpowers/specs/2026-10-24-planet-geology-design.md。
 */
class STELLARERAPLANET_API FPlanetGeologyField
{
public:
	FPlanetGeologyField() = default;

	/**
	 * 从磁盘加载。PresetName 只能是 Planets 下的文件名（不含扩展名）。
	 * 任何失败都返回一个**有效的空场**，错误写进 OutReport。
	 *
	 * PlanetRadiusVoxels 用来把 JSON 里的半径比例（peakDepthRatio /
	 * layerThicknessRatio）解析成体素深度。只用绝对深度的旧数据传 0 即可；
	 * 带比例字段却没有半径的矿层会被跳过并留下警告。
	 */
	static TSharedPtr<const FPlanetGeologyField> LoadFromFiles(
		const FString& PresetName, FPlanetGeologyLoadReport& OutReport,
		double PlanetRadiusVoxels = 0.0);

	/** 从内存加载，给测试和热重载用。半径语义同 LoadFromFiles。 */
	static TSharedPtr<const FPlanetGeologyField> LoadFromString(
		const FString& ElementsJson, const FString& PlanetJson, FPlanetGeologyLoadReport& OutReport,
		double PlanetRadiusVoxels = 0.0);

	/** Content/Geology/Planets 下所有预设名（排序后），给编辑器下拉框用。 */
	static TArray<FString> ListPresetNames();

	/** 指定方向与深度（体素，基准球面为 0，向内为正）的成分。 */
	FPlanetGeologySample Sample(const FVector& UnitDir, double Depth) const;

	/**
	 * 矿脉造成的地表外凸量（体素，∈ [0, GetMaxGeometryDelta()]）。
	 * 密度场和地表查询必须都走它，否则脚下与眼睛看到的不是同一个面。
	 */
	float SampleGeometryDelta(const FVector& UnitDir, double Depth) const;

	float GetMaxGeometryDelta() const { return GeometryAmplitude; }
	float GetMaxReachDepth() const { return MaxReachDepth; }
	const FPlanetGeologyAppearance& GetAppearance() const { return Appearance; }
	bool IsEmpty() const { return Elements.Num() == 0; }
	int32 NumElements() const { return Elements.Num(); }
	float GetTotalAbundance() const { return TotalAbundance; }

	/**
	 * 元素定义的副本（元素表顺序）。
	 *
	 * 生产调用点为零：图例用的是 GetSampledElementDefinitions()。保留本函数只作
	 * **运行期采样顺序的对照 / 测试用**（测试拿它确认两种顺序确实可能不同）。
	 */
	TArray<FPlanetGeologyElement> GetElementDefinitions() const;

	/**
	 * 元素定义的副本（**运行期采样顺序**），与该顺序下的 DominantIndex 对齐。
	 *
	 * 图例、数字键、方块组下标都读这个。和 GetElementDefinitions 的区别：那个按
	 * Elements.json 的 ElementOrder 顺序输出，只作对照 / 测试用；这里的顺序是
	 * 采样器实际遍历的顺序（给矿物块归属用）。两者通常不同 —— 行星 JSON 的
	 * elements 是对象，解析后按哈希顺序落进 Elements。
	 */
	TArray<FPlanetGeologyElement> GetSampledElementDefinitions() const;

private:
	struct FElementRuntime
	{
		FPlanetGeologyElement Definition;
		FVoxelFastNoise Noise;
	};

	struct FAccumulation
	{
		double Total = 0.0;
		double MaxRaw = 0.0;
		/** 最强元素的 raw_i / abundance_i；与丰度无关，供颜色显现判定。 */
		double DominantNorm = 0.0;
		int32 DominantIndex = INDEX_NONE;
	};

	/** 单遍累加；bComputeColor 为假时跳过加权颜色，供几何查询用。 */
	void Accumulate(const FVector& UnitDir, double Depth, bool bComputeColor,
		FAccumulation& OutAccumulation, FLinearColor& OutWeightedColor) const;

	FPlanetGeologyAppearance Appearance;
	float GeometryAmplitude = 1.5f;
	float TotalAbundance = 0.f;
	float MaxReachDepth = 0.f;
	TArray<FElementRuntime> Elements;

	/**
	 * Elements.json 数组里的 id 顺序；GetElementDefinitions 按它输出（spec §3.3）。
	 * 运行期采样仍按 Elements 的现有顺序，不受影响。
	 */
	TArray<FString> ElementOrder;
};
