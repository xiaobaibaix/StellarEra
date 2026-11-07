// Copyright Voxel Plugin SAS. All Rights Reserved.
// 行星地质场：元素表 + 行星参数合成的只读采样场。

#pragma once

#include "CoreMinimal.h"
#include "PlanetGeologyTypes.h"

class UPlanetGeologyPreset;

/**
 * 一颗行星的地质场：由 UPlanetGeologyPreset 构建，构造后完全不可变；
 * 生成器实例用 TSharedPtr<const ...> 在后台线程共享。
 */
class STELLARERAPLANET_API FPlanetGeologyField
{
public:
	FPlanetGeologyField() = default;

	/**
	 * 从预设资产生成场，必须在游戏线程调用（读 UObject），构造用 check(IsInGameThread()) 兜住；建完不可变，可跨线程共享。
	 * PlanetRadiusVoxels 用于把 (UnitDir, Depth) 换算成归一化坐标，并设 MaxReachDepth（矿囊可及整个球体）。
	 */
	static TSharedPtr<const FPlanetGeologyField> BuildFromPreset(
		const UPlanetGeologyPreset& Preset, double PlanetRadiusVoxels);

	/** 指定方向与深度（体素，基准球面为 0，向内为正）的成分。 */
	FPlanetGeologySample Sample(const FVector& UnitDir, double Depth) const;

	/**
	 * 每个元素的强度（∈ [0,1]），下标 = 运行期采样顺序。DominantNorm/DominantIndex 即本数组最大值及其下标（并列取靠前）。
	 * 契约包含全元素 Abundance = 0 的退化配置：仍给 Norm 与下标，只是不贡献颜色。
	 * 测试 oracle，非生产路径：与生产入口不同，|P| > 1 时不早退而走完整求值，球外比较要记住这条不对称。
	 */
	void SampleElementNorms(const FVector& UnitDir, double Depth, TArray<float>& OutNorms) const;

	/** 单元素在归一化空间某点的强度（∈ [0,1]），球外恒 0。矿囊模型的原生入口，不必绕回 (方向, 深度)。 */
	float SampleElementNormAtNormalized(int32 ElementIndex, const FVector& NormalizedPoint) const;

	/** 与 SampleElementNormAtNormalized 同公式，但绕开加速结构直接遍历全部基元（同样取 max）。只给测试用：索引漏基元不报错，只表现为矿变少。 */
	float SampleElementNormBruteForce(int32 ElementIndex, const FVector& NormalizedPoint) const;

	/** 该元素的全部有界区域（归一化空间），观测视图用它分簇。顺序 = 生成器顺序 × 各生成器的区域顺序。 */
	void EnumerateRegions(int32 ElementIndex, TArray<FPlanetGeologyRegion>& Out) const;

	/** 第一个囊状生成器的矿囊表（归一化空间）；没有囊状生成器时返回空数组。 */
	const TArray<FPlanetGeologyPod>& GetPods(int32 ElementIndex) const;

	/** 矿脉造成的地表外凸量（体素，∈ [0, GetMaxGeometryDelta()]）。密度场与地表查询必须都走它，否则脚下与看到的不是同一个面。 */
	float SampleGeometryDelta(const FVector& UnitDir, double Depth) const;

	float GetMaxGeometryDelta() const { return GeometryAmplitude; }
	const FPlanetGeologyAppearance& GetAppearance() const { return Appearance; }
	/** 空场 = 没有元素，**或**所有元素的生成器都为空。 */
	bool IsEmpty() const;
	int32 NumElements() const { return Elements.Num(); }
	float GetTotalAbundance() const { return TotalAbundance; }

	/** 元素定义副本（运行期采样顺序），与该顺序下的 DominantIndex 对齐；图例、数字键、矿体组件下标都读它。 */
	TArray<FPlanetGeologyElement> GetSampledElementDefinitions() const;

private:
	/** 单元素求值：各生成器取 max。P 为归一化坐标。 */
	float EvaluateElementNorm(int32 ElementIndex, const FVector& NormalizedPoint) const;

	/** 归一化换算用的行星半径（体素），并用于设 MaxReachDepth；半径 0 时归一化点退化成方向。 */
	double RadiusVoxels = 0.0;

	struct FAccumulation
	{
		double Total = 0.0;
		double MaxRaw = 0.0;
		/** 最强元素的 raw_i / abundance_i；与丰度无关，供颜色显现判定。 */
		double DominantNorm = 0.0;
		int32 DominantIndex = INDEX_NONE;
	};

	/** 单遍累加；bComputeColor 为假时跳过加权颜色。OutElementNorms 非空时按元素下标写入每个 Norm（含 0）。 */
	void Accumulate(const FVector& UnitDir, double Depth, bool bComputeColor,
		FAccumulation& OutAccumulation, FLinearColor& OutWeightedColor,
		TArray<float>* OutElementNorms = nullptr) const;

	FPlanetGeologyAppearance Appearance;
	float GeometryAmplitude = 1.5f;
	float TotalAbundance = 0.f;
	float MaxReachDepth = 0.f;
	TArray<FPlanetGeologyElement> Elements;
};
