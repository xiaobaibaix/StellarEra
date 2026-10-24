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
	 */
	static TSharedPtr<const FPlanetGeologyField> LoadFromFiles(
		const FString& PresetName, FPlanetGeologyLoadReport& OutReport);

	/** 从内存加载，给测试和热重载用。 */
	static TSharedPtr<const FPlanetGeologyField> LoadFromString(
		const FString& ElementsJson, const FString& PlanetJson, FPlanetGeologyLoadReport& OutReport);

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

	/** 元素定义的副本（元素表顺序），给矿物剖面图例用。 */
	TArray<FPlanetGeologyElement> GetElementDefinitions() const;

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
};
