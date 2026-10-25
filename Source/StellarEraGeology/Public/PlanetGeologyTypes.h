// Copyright Voxel Plugin SAS. All Rights Reserved.
// 行星地质系统的值类型：元素定义、矿囊、采样结果、外观参数。

#pragma once

#include "CoreMinimal.h"
#include "PlanetGeologyTypes.generated.h"

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
 * 元素的运行期定义（预置资产 → 地质场的内部形态）。
 *
 * 刻意不是 USTRUCT：它只在地质系统内部按值传递，不进反射、不进蓝图。
 * 可编辑形态见 FPlanetGeologyElementParams（PlanetGeologyPreset.h），
 * 预设资产是唯一编辑入口。
 */
struct FPlanetGeologyElement
{
	FString Id;
	FString DisplayName;
	FLinearColor Color = FLinearColor::White;

	/**
	 * 只影响颜色混合权重与图例占比，不改几何。
	 * 几何外凸量取各元素 Norm 的最大值（见 SampleGeometryDelta），与 Abundance 无关。
	 */
	float Abundance = 0.f;

	// --- 矿囊 ---
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

	/**
	 * 矿脉稀疏阈值（0..0.95，来自预设资产的 VeinThreshold）。
	 *
	 * Perlin 噪声 0.5 是均值：阈值越高，只有噪声峰值附近才留下含量，
	 * 单元素矿囊越稀疏、越纯。0 表示不稀疏。
	 * 每个元素可以不同，用来让各矿囊的分布与密度互不相同。
	 */
	float VeinThreshold = 0.f;
};

/** 一次地质采样的结果。TotalNorm / DominantNorm / Purity 均已归一化到 [0,1]。 */
struct FPlanetGeologySample
{
	float TotalNorm = 0.f;
	/**
	 * 最强元素自身的归一化强度 = 矿囊衰减 × 矿脉因子（raw_i / abundance_i）。
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
