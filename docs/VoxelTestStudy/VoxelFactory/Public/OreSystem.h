// 矿脉系统 —— 决定哪里有什么矿、挖得动多快。
//
// 当前只有 PlanetSurfaceProbe::SampleOreAbundance 一个临时函数，
// 缺三件事：
//   1. 深度分层 —— 地表和地心一样丰富不合理
//   2. 矿脉成块 —— 现在是均匀撒，玩家不需要「找矿」
//   3. 星球差异 —— 每颗星球的矿种组成应该不同
//
// 设计目标：让「找矿」成为一个实际的决策。
// 如果哪里都是矿，玩家就没有探索动机。

#pragma once

#include "CoreMinimal.h"
#include "FactoryTypes.h"
#include "OreSystem.generated.h"

class FVoxelFastNoise;

/**
 * 单种矿物的分布配置。
 */
USTRUCT(BlueprintType)
struct FOreDistribution
{
	GENERATED_BODY()

	/** 该矿物在这颗星球上是否存在。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ore")
	bool bPresent = false;

	/** 全局丰富度，0~1。星球层面的总矿量。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ore", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float GlobalAbundance = 0.f;

	/**
	 * 最佳深度，单位体素（相对基准半径的偏移，负值表示地下）。
	 * 在这个深度浓度最高，向上向下递减。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ore")
	double PeakDepth = -50.0;

	/**
	 * 矿层厚度，单位体素。
	 * 决定浓度随深度衰减的速度：厚度大 → 矿层宽广。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ore", meta = (ClampMin = "1.0"))
	double LayerThickness = 200.0;

	/** 空间噪声频率。越低矿脉越大块，越高越碎。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ore", meta = (ClampMin = "0.01"))
	float SpatialFrequency = 2.0;

	/** 空间噪声强度，0~1。0 = 完全均匀，1 = 完全由噪声决定。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ore", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float SpatialVariance = 0.6f;

	/**
	 * 浓度阈值，0~1。
	 * 低于这个值的区域完全没有矿。
	 * 这是「矿脉」而不是「整个星球都有一点」的关键。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ore", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float Threshold = 0.35f;

	/** 开采硬度，1 = 普通，3 = 极硬。影响挖掘耗时。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ore", meta = (ClampMin = "0.1"))
	float Hardness = 1.0f;

	/** 每次开采产出的基础数量。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ore", meta = (ClampMin = "0.1"))
	float BaseYield = 1.0f;
};

/**
 * 一颗星球的完整矿脉配置。
 */
USTRUCT(BlueprintType)
struct FOrePlanetConfig
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ore")
	FOreDistribution Iron;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ore")
	FOreDistribution Copper;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ore")
	FOreDistribution Silicon;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ore")
	FOreDistribution Titanium;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ore")
	FOreDistribution Uranium;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ore")
	FOreDistribution Water;

	/** 星球随机种子。不同种子 → 完全不同的矿脉布局。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ore")
	int32 PlanetSeed = 1337;

	/** 星球基准半径，单位体素。用于把绝对深度转成相对深度。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ore", meta = (ClampMin = "1.0"))
	double PlanetRadius = 1000.0;

	/** 取某种矿的配置。 */
	const FOreDistribution* GetDistribution(EOreType Ore) const;

	/** 列出星球上存在的所有矿种。 */
	void GetAvailableOres(TArray<EOreType>& OutOres) const;

	/** 星球上是否存在这种矿。 */
	bool HasOre(EOreType Ore) const { return GetDistribution(Ore) != nullptr && GetDistribution(Ore)->bPresent; }
};

/**
 * 矿脉采样器。
 *
 * 纯计算类，不持有 UObject 状态，可在任何地方调用。
 * 线程安全：所有查询函数都是 const。
 */
class VOXELFACTORY_API FOreSystem
{
public:
	/** 用配置初始化内部噪声。 */
	void Init(const FOrePlanetConfig& Config);

	/**
	 * 采样某点的矿物浓度。
	 *
	 * @param VoxelPosition 该点的体素坐标
	 * @param Ore     查哪种矿
	 * @param OutRichness 返回浓度 0~1（已考虑阈值，低于阈值为 0）
	 *
	 * 浓度由三个因素相乘：
	 *   深度剖面（该矿在多深的地方）× 空间噪声（这里是斑块还是空白）
	 */
	float SampleRichness(const FVector& VoxelPosition, EOreType Ore, float& OutRichness) const;

	/** 便利版：只要浓度值。 */
	float SampleRichness(const FVector& VoxelPosition, EOreType Ore) const;

	/** 该点的所有矿种浓度。 */
	void SampleAllOres(const FVector& VoxelPosition, TMap<EOreType, float>& OutOres) const;

	/** 该点最富的矿种。bRichness 返回浓度。 */
	EOreType GetDominantOre(const FVector& VoxelPosition, float& OutRichness) const;

	/** 开采难度：受深度与硬度影响。 */
	static float GetDigDifficulty(const FOreDistribution& Dist, double DepthVoxels);

	/** 每次开采产量：受浓度与基础产量影响。 */
	static float GetYield(const FOreDistribution& Dist, float Richness);

	/** 深度剖面：某矿在给定深度的浓度倍率，0~1。 */
	static float ComputeDepthProfile(const FOreDistribution& Dist, double DepthVoxels);

private:
	FOrePlanetConfig Config;

	/** 内部噪声。用于空间分布。 */
	TSharedPtr<class FVoxelFastNoise> SpatialNoise;
};
