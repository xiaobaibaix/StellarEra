// Copyright Voxel Plugin SAS. All Rights Reserved.
// 行星地质系统的值类型：元素定义、采样结果、外观参数、加载诊断。

#pragma once

#include "CoreMinimal.h"

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
	float PeakDepth = 0.f;
	float LayerThickness = 16.f;
	/** |peakDepth| + 3 * layerThickness，解析时算好，供采样早退。 */
	float ReachDepth = 0.f;

	// --- 噪声参数（来自元素表） ---
	float Frequency = 3.f;
	int32 Octaves = 2;
	float Lacunarity = 2.f;
	float Gain = 0.5f;
	int32 Seed = 0;
	float DepthScale = 0.3f;
};

/** 一次地质采样的结果。TotalNorm / Purity 均已归一化到 [0,1]。 */
struct FPlanetGeologySample
{
	float TotalNorm = 0.f;
	float Purity = 0.f;
	FLinearColor MineralColor = FLinearColor::Black;
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
