// Copyright Voxel Plugin SAS. All Rights Reserved.
// 行星地质场：元素表 + 行星参数合成的只读采样场。

#pragma once

#include "CoreMinimal.h"
#include "PlanetGeologyTypes.h"

class UPlanetGeologyPreset;

/**
 * 一颗行星的地质场。
 *
 * 由 UPlanetGeologyPreset 资产（元素表 + 外观 + 几何参数）构建，构造后完全不可变；
 * 生成器实例用 TSharedPtr<const ...> 在后台线程共享。
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
	 * 契约**包括全元素 Abundance = 0 的退化配置**：Sample 仍给出 DominantNorm /
	 * DominantIndex（它们只依赖 Norm），只是没有颜色权重（TotalNorm / 颜色为默认值）。
	 * 同理，混合丰度下若只命中 `Abundance = 0` 的元素，也照样给出它们的 Norm 与下标，
	 * 只是该元素不贡献颜色。
	 *
	 * 内部走同一个 Accumulate，一次循环填完，不额外调噪声。
	 *
	 * **测试 oracle，非生产路径**：生产入口是 SampleElementNormAtNormalized（逐元素）
	 * 与 Sample（取最强元素）。与它们不同，本入口（和 SampleElementNormBruteForce）
	 * 没有「球外早退」—— 点落在 `|P| > 1` 时走完整求值，而生产入口直接返回 0。
	 * 球外比较必须记住这条不对称。
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
	 * 与 SampleElementNormAtNormalized 同一公式，但**绕开各生成器的加速结构**直接遍历全部基元
	 * （同样是各生成器取 max）。只给测试用：索引漏基元不会报错，只表现为「矿看起来少了」。
	 */
	float SampleElementNormBruteForce(int32 ElementIndex, const FVector& NormalizedPoint) const;

	/**
	 * 该元素的全部有界区域（归一化空间），观测视图用它分簇。
	 * 顺序 = 生成器顺序 × 各生成器的区域顺序。
	 */
	void EnumerateRegions(int32 ElementIndex, TArray<FPlanetGeologyRegion>& Out) const;

	/** 第一个囊状生成器的矿囊表（归一化空间）；没有囊状生成器时返回空数组。 */
	const TArray<FPlanetGeologyPod>& GetPods(int32 ElementIndex) const;

	/**
	 * 矿脉造成的地表外凸量（体素，∈ [0, GetMaxGeometryDelta()]）。
	 * 密度场和地表查询必须都走它，否则脚下与眼睛看到的不是同一个面。
	 */
	float SampleGeometryDelta(const FVector& UnitDir, double Depth) const;

	float GetMaxGeometryDelta() const { return GeometryAmplitude; }
	const FPlanetGeologyAppearance& GetAppearance() const { return Appearance; }
	/** 空场 = 没有元素，**或**所有元素的生成器都为空。 */
	bool IsEmpty() const;
	int32 NumElements() const { return Elements.Num(); }
	float GetTotalAbundance() const { return TotalAbundance; }

	/**
	 * 元素定义的副本（**运行期采样顺序**），与该顺序下的 DominantIndex 对齐。
	 *
	 * 图例、数字键、矿体组件下标都读这个。现在的顺序就是 Elements 的顺序
	 * （即预设资产 Elements 数组的顺序），不再有「元素表顺序 vs 采样顺序」两种。
	 */
	TArray<FPlanetGeologyElement> GetSampledElementDefinitions() const;

private:
	/** 单元素求值：各生成器取 max。P 为归一化坐标。 */
	float EvaluateElementNorm(int32 ElementIndex, const FVector& NormalizedPoint) const;

	/**
	 * 归一化换算用的行星半径（体素）。用途：
	 * (1) `Accumulate` 把外部签名 (UnitDir, Depth) 换算成归一化坐标；
	 * (2) `BuildFromPreset` 用它设 MaxReachDepth（矿囊可及整个球体）。
	 * 半径 0 时归一化点退化成方向。
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
	TArray<FPlanetGeologyElement> Elements;
};
