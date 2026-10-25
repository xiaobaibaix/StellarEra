// Copyright Voxel Plugin SAS. All Rights Reserved.
// 行星地质系统的值类型：元素定义、采样结果、外观参数、加载诊断。

#pragma once

#include "CoreMinimal.h"
#include "PlanetGeologyTypes.generated.h"

/**
 * 一条高斯矿层的深度剖面。
 *
 * 深度单位是体素，正 = 基准球面以下。同一元素可以有多条带：
 * 浅层带服务地表挖矿，深层带用半径比例描述行星内部结构。
 */
struct FPlanetGeologyBand
{
	float PeakDepth = 0.f;
	float LayerThickness = 16.f;
	/** |peakDepth| + 3 * layerThickness，解析时算好。 */
	float ReachDepth = 0.f;
};

/** 一个矿囊：归一化空间的球（行星基准半径 = 1）。 */
struct FPlanetGeologyPod
{
	FVector Center = FVector::ZeroVector;
	float Radius = 0.f;
};

/** 空间索引里的一条引用：哪个元素的第几个矿囊。 */
struct FPlanetGeologyPodRef
{
	int32 ElementIndex = INDEX_NONE;
	int32 PodIndex = INDEX_NONE;
};

/**
 * 元素表的单条定义。
 *
 * 刻意不是 USTRUCT：它只在地质系统内部按值传递，不进反射、不进蓝图。
 * 它仍是 JSON 解析与运行期采样的内部类型；可进资产的编辑形态见
 * FPlanetGeologyElementParams（PlanetGeologyPreset.h）。
 */
struct FPlanetGeologyElement
{
	FString Id;
	FString DisplayName;
	FLinearColor Color = FLinearColor::White;

	// --- 行星级参数（解析行星 JSON 时填入） ---
	float Abundance = 0.f;
	/** 该元素的全部矿层，解析后至少一条。 */
	TArray<FPlanetGeologyBand> Bands;
	/** 所有带里最大的 |peakDepth| + 3 * layerThickness，供采样早退。 */
	float ReachDepth = 0.f;

	// --- 矿囊（预设路径；JSON 路径下为空，走 Bands 分支） ---
	TArray<FPlanetGeologyPod> Pods;
	/** 名义半径（归一化）。噪声坐标的缩放基准，不随单囊抖动。 */
	float PodRadius = 0.05f;
	/** 一个矿囊直径内大约几个噪声周期。 */
	float ShapeDetail = 3.f;

	// --- 噪声参数 ---
	int32 Octaves = 2;
	float Lacunarity = 2.f;
	float Gain = 0.5f;
	int32 Seed = 0;
	/** 旧矿层路径专用：噪声频率与深度缩放。**Task 2 必须留着**，Task 3 与 Bands 一起删。 */
	float Frequency = 3.f;
	float DepthScale = 0.3f;

	/**
	 * 矿脉稀疏阈值（0..0.95，来自元素表 noise.veinThreshold）。
	 *
	 * Perlin 噪声 0.5 是均值：阈值越高，只有噪声峰值附近才留下含量，
	 * 单元素脉越稀疏、越纯。0 表示不稀疏（旧行为）。
	 * 每个元素可以不同，用来让各矿脉的分布与密度互不相同。
	 */
	float VeinThreshold = 0.f;
};

/** 一次地质采样的结果。TotalNorm / DominantNorm / Purity 均已归一化到 [0,1]。 */
struct FPlanetGeologySample
{
	float TotalNorm = 0.f;
	/**
	 * 最强元素自身的归一化强度 = 高斯层因子 × 矿脉因子（raw_i / abundance_i）。
	 *
	 * 与元素丰度无关：丰度 0.05 的铀在自己的矿脉里也能到 1。
	 * 颜色显现必须用它而不是 TotalNorm，否则低丰度元素永远够不到阈值。
	 */
	float DominantNorm = 0.f;
	float Purity = 0.f;
	FLinearColor MineralColor = FLinearColor::Black;
	/** 最强元素在元素表中的下标；-1 表示当前采样没有任何矿。 */
	int32 DominantIndex = INDEX_NONE;
};

/** 行星级的外观参数。改 USTRUCT 是为了能在 UDataAsset 里编辑。 */
USTRUCT(BlueprintType)
struct STELLARERAGEOLOGY_API FPlanetGeologyAppearance
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Appearance",
		meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float ColorThreshold = 0.22f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Appearance",
		meta = (ClampMin = "0.001", ClampMax = "1.0"))
	float FadeRange = 0.15f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Appearance",
		meta = (ClampMin = "0.01", ClampMax = "1.0"))
	float PurityForFullColor = 0.7f;
};

/** 解析诊断。Errors 非空时整份配置按「没有矿」处理。 */
struct FPlanetGeologyLoadReport
{
	TArray<FString> Warnings;
	TArray<FString> Errors;

	bool HasErrors() const { return Errors.Num() > 0; }
};
