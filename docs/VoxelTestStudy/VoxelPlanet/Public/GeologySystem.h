// 地质系统 —— 六张矿种分布图混合成成分向量。
//
// 设计要点：
//   1. 每种矿有独立的「地质图」（深度剖面 × 空间噪声）
//   2. 在采样点逐张求值，混合成成分向量
//   3. 纯度 = Max/Sum 决定「这是纯矿还是混杂矿」
//   4. PeakDepth ≈ 0 的矿 → 露天；PeakDepth 很负 → 必须深挖
//      「露出一点迹象」和「挖开才看得见」是同一个公式的不同参数
//
// 与体素星球的关系：
//   本系统是纯计算，不依赖任何 Actor。
//   通过两个接口与体素挂钩：
//     · GetGeologyColor()  给生成器的 GetMaterialImpl 调用
//     · SampleComposition() 给挖掘机调用
//   生成器不需要知道本系统的内部实现。

#pragma once

#include "CoreMinimal.h"
#include "VoxelMinimal.h"
#include "FastNoise/VoxelFastNoise.h"
#include "GeologyTypes.generated.h"

/**
 * 地质采样结果。
 *
 * 描述「某个位置上有什么矿、纯度多少」。
 */
USTRUCT(BlueprintType)
struct FGeologySample
{
	GENERATED_BODY()

	/** 各矿种的重量，未归一化。索引对应 EGemType。 */
	UPROPERTY(BlueprintReadOnly, Category = "Geology")
	TArray<float> Weights;

	/** 总量。接近 0 表示这里几乎没有矿。 */
	UPROPERTY(BlueprintReadOnly, Category = "Geology")
	float TotalWeight = 0.f;

	/**
	 * 纯度 0~1：主矿占比。
	 * 接近 1 = 纯矿；接近 0 = 混杂矿，看不出是什么。
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Geology")
	float Purity = 0.f;

	/** 主矿种。Purity 为 0 时是 None。 */
	UPROPERTY(BlueprintReadOnly, Category = "Geology")
	EGemType DominantGem = EGemType::None;

	/** 是否达到阈值（值得开采）。 */
	UPROPERTY(BlueprintReadOnly, Category = "Geology")
	bool bWorthMining = false;

	/** 是否为空（完全没矿）。 */
	bool IsEmpty() const { return TotalWeight <= KINDA_SMALL_NUMBER; }

	/** 取某矿种的重量。 */
	float GetWeight(EGemType Gem) const
	{
		const int32 Index = int32(Gem);
		return Weights.IsValidIndex(Index) ? Weights[Index] : 0.f;
	}
};

/**
 * 地质系统。
 *
 * 线程安全：所有查询都是 const，内部只读噪声。
 * 纯计算：不需要 Actor，不需要世界上下文。
 */
class VOXELPLANET_API FGeologySystem
{
public:
	FGeologySystem();

	/** 用配置初始化。会重建内部噪声。 */
	void Init(const FGeologyConfig& InConfig);

	/** 只读访问配置。 */
	const FGeologyConfig& GetConfig() const { return Config; }

	//////////////////////////////////////////////////////////////////////
	// 采样

	/**
	 * 采样一个体素位置的地质成分。
	 *
	 * @param VoxelPosition 该点体素坐标
	 * @param SurfaceRadius 该方向的地表半径。用来判断「在地表还是地下」——
	 *        决定露天矿要不要显现。
	 * @return 采样结果
	 */
	FGeologySample SampleComposition(
		const FVector& VoxelPosition,
		double SurfaceRadius = 0.0) const;

	/**
	 * 该点的地表是否露天矿。
	 *
	 * 判据：地表那一层的 TotalWeight 超过阈值，且纯度够高。
	 * 这样玩家从太空看就能看出「这块地表有矿」，但看不出是什么矿。
	 */
	UFUNCTION(BlueprintPure, Category = "Geology")
	bool IsSurfaceOutcrop(const FVector& VoxelPosition, double SurfaceRadius) const;

	//////////////////////////////////////////////////////////////////////
	// 颜色

	/**
	 * 按地质成分给出颜色。
	 *
	 * 高纯度 → 接近矿种本色（饱和）
	 * 低纯度 → 灰褐色混合矿（去饱和 + 偏暗）
	 *
	 * 露天矿会比埋藏矿稍亮一点，让地表露头更明显。
	 */
	UFUNCTION(BlueprintPure, Category = "Geology")
	FLinearColor GetGeologyColor(const FGeologySample& Sample, bool bOutcrop = false) const;

	/** 某矿种的代表色。 */
	UFUNCTION(BlueprintPure, Category = "Geology")
	static FLinearColor GetGemColor(EGemType Gem);

	/** 矿种的显示名。 */
	UFUNCTION(BlueprintPure, Category = "Geology")
	static FText GetGemName(EGemType Gem);

	//////////////////////////////////////////////////////////////////////
	// 开采

	/**
	 * 该点能开采出什么。
	 *
	 * 返回主矿种与产量。若纯度不足则返回 None。
	 *
	 * @param Sample        采样结果
	 * @param BaseYield     基础产量
	 * @param OutYield      实际产量
	 * @return 主矿种，不值得开采时返回 None
	 */
	UFUNCTION(BlueprintPure, Category = "Geology")
	EGemType DetermineYield(
		const FGeologySample& Sample,
		float BaseYield,
		float& OutYield) const;

	/** 该点的开采难度（含深度与硬度）。 */
	UFUNCTION(BlueprintPure, Category = "Geology")
	float GetDigDifficulty(const FGeologySample& Sample, double DepthVoxels) const;

private:
	/** 计算单个矿种在给定深度的重量。 */
	float ComputeGemWeight(
		const FGemConfig& Gem,
		const FVector& UnitDirection,
		double DepthVoxels,
		int32 GemIndex) const;

	FGeologyConfig Config;

	/** 空间噪声。每种矿用不同的种子偏移，保证分布不相关。 */
	FVoxelFastNoise SpatialNoise;
};
