// Copyright Voxel Plugin SAS. All Rights Reserved.
// 行星地质系统的值类型：元素定义、采样结果、外观参数、加载诊断。

#pragma once

#include "CoreMinimal.h"

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

/**
 * 元素表的单条定义。
 *
 * 刻意不是 USTRUCT：它只在地质系统内部按值传递，不进反射、不进蓝图。
 * JSON 才是唯一的编辑入口。
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

	// --- 噪声参数（来自元素表） ---
	float Frequency = 3.f;
	int32 Octaves = 2;
	float Lacunarity = 2.f;
	float Gain = 0.5f;
	int32 Seed = 0;
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

/** 行星级的外观参数（来自 Planets/<preset>.json 的 appearance 段）。 */
struct FPlanetGeologyAppearance
{
	float ColorThreshold = 0.22f;
	float FadeRange = 0.15f;
	float PurityForFullColor = 0.7f;
};

/** 解析诊断。Errors 非空时整份配置按「没有矿」处理。 */
struct FPlanetGeologyLoadReport
{
	TArray<FString> Warnings;
	TArray<FString> Errors;

	bool HasErrors() const { return Errors.Num() > 0; }
};
