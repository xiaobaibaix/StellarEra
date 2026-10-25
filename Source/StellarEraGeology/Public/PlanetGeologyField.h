// Copyright Voxel Plugin SAS. All Rights Reserved.
// 行星地质场：元素表 + 行星参数合成的只读采样场。

#pragma once

#include "CoreMinimal.h"
#include "FastNoise/VoxelFastNoise.h"
#include "PlanetGeologyTypes.h"

class UPlanetGeologyPreset;

/**
 * 一颗行星的地质场。
 *
 * 由 Content/Geology/Elements.json（元素表）与 Planets/<preset>.json（行星参数）
 * 合成，构造后完全不可变；生成器实例用 TSharedPtr<const ...> 在后台线程共享。
 *
 * 设计见 docs/superpowers/specs/2026-10-24-planet-geology-design.md。
 */
class STELLARERAGEOLOGY_API FPlanetGeologyField
{
public:
	FPlanetGeologyField() = default;

	/**
	 * 从预设资产生成场。**必须在游戏线程调用**（要读 UObject），构造函数用
	 * check(IsInGameThread()) 兜住。建完即不可变，可跨线程共享。
	 *
	 * PlanetRadiusVoxels 的用途：(1) 把外部签名 (UnitDir, Depth) 换算成归一化坐标；
	 * (2) 用它设 MaxReachDepth（矿囊可及整个球体）。矿囊本身存的是归一化空间的位置，
	 * 与行星大小无关。
	 */
	static TSharedPtr<const FPlanetGeologyField> BuildFromPreset(
		const UPlanetGeologyPreset& Preset, double PlanetRadiusVoxels);

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
	 * 每个元素的强度（∈ [0,1]），下标 = 运行期元素下标
	 * （即 GetSampledElementDefinitions 的顺序）。
	 *
	 * 与 Sample().DominantNorm / DominantIndex 同源：DominantNorm 就是本数组的最大值、
	 * DominantIndex 就是它的下标（并列时取靠前的那个）。等值面构建器要按元素各取一面，
	 * 必须拿到整条数组；Sample 只给最大值。
	 *
	 * 内部走同一个 Accumulate，一次循环填完，不额外调噪声。
	 */
	void SampleElementNorms(const FVector& UnitDir, double Depth, TArray<float>& OutNorms) const;

	/**
	 * 单元素在**归一化空间**某点的强度（∈ [0,1]）。球外恒 0。
	 *
	 * 这是矿囊模型的原生入口：观测视图的角点本来就在归一化/厘米空间里，
	 * 不必绕回 (方向, 深度)。
	 */
	float SampleElementNormAtNormalized(int32 ElementIndex, const FVector& NormalizedPoint) const;

	/**
	 * 与 SampleElementNormAtNormalized 同一公式，但**绕开空间索引**遍历该元素全部矿囊。
	 * 只给测试用：索引漏囊不会报错，只表现为「矿看起来少了」。
	 */
	float SampleElementNormBruteForce(int32 ElementIndex, const FVector& NormalizedPoint) const;

	/** 该元素的矿囊表（归一化空间）。越界返回空数组。 */
	const TArray<FPlanetGeologyPod>& GetPods(int32 ElementIndex) const;

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
	 * 图例、数字键、矿体组件下标都读这个。和 GetElementDefinitions 的区别：那个按
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

	/** 单元素求值：矿囊径向衰减 × 噪声稀疏。P 为归一化坐标。 */
	float EvaluateElementNorm(int32 ElementIndex, const FVector& NormalizedPoint) const;

	/** 建 PodIndex。BuildFromPreset 末尾调用一次。 */
	void BuildPodIndex();

	/**
	 * 矿囊格索引：格边 = 全局最大矿囊半径。
	 *
	 * 每囊插入其包围盒覆盖的全部格子（AABB 边长可达 2 格且可跨整数边界，
	 * 每轴最多 3 格，最多 27 格）。查询只查 P 所在格就够：
	 * 任何包含 P 的矿囊，其包围盒必覆盖 P 所在格。
	 */
	TMap<FIntVector, TArray<FPlanetGeologyPodRef>> PodIndex;
	float PodIndexCellSize = 0.f;

	/**
	 * 归一化换算用的行星半径（体素）。用途：
	 * (1) `Accumulate` 把外部签名 (UnitDir, Depth) 换算成归一化坐标；
	 * (2) `BuildFromPreset` 用它设 MaxReachDepth（矿囊可及整个球体）。
	 * 旧矿层分支不读它；半径 0 时归一化点退化成方向。
	 */
	double RadiusVoxels = 0.0;

	struct FAccumulation
	{
		double Total = 0.0;
		double MaxRaw = 0.0;
		/** 最强元素的 raw_i / abundance_i；与丰度无关，供颜色显现判定。 */
		double DominantNorm = 0.0;
		int32 DominantIndex = INDEX_NONE;
	};

	/** 单遍累加；bComputeColor 为假时跳过加权颜色，供几何查询用。
	 *  OutElementNorms 非空时按元素下标写入每个元素的 Norm（含 0），长度由调用方保证。 */
	void Accumulate(const FVector& UnitDir, double Depth, bool bComputeColor,
		FAccumulation& OutAccumulation, FLinearColor& OutWeightedColor,
		TArray<float>* OutElementNorms = nullptr) const;

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
