// 地质系统的类型定义。

#pragma once

#include "CoreMinimal.h"
#include "GeologyTypes.generated.h"

/** 矿种。与工厂系统的 EOreType 语义一致但独立定义，避免跨模块耦合。 */
UENUM(BlueprintType)
enum class EGemType : uint8
{
	None = 0,
	Iron,
	Copper,
	Silicon,
	Titanium,
	Uranium,
	Water,

	Count
};

/** 单个矿种的地质配置——即「一张地质图」的参数。 */
USTRUCT(BlueprintType)
struct FGemConfig
{
	GENERATED_BODY()

	/** 这颗星球有没有这种矿。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gem")
	bool bPresent = false;

	/** 全局权重 0~1。决定这张地质图的「整体强度」。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gem", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float GlobalWeight = 0.f;

	/**
	 * 峰值深度，单位体素，相对基准半径。
	 *   ≈0     → 露天矿，地表就露出来
	 *   很负   → 深埋矿，地表看不出，挖开才碰到
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gem")
	double PeakDepth = -50.0;

	/**
	 * 矿层厚度，单位体素。
	 * 决定浓度随深度的衰减速度：厚 → 矿层宽广。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gem", meta = (ClampMin = "1.0"))
	double LayerThickness = 200.0;

	/** 空间噪声频率。低 → 矿脉大块；高 → 矿脉碎。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gem", meta = (ClampMin = "0.01"))
	float SpatialFrequency = 2.0;

	/** 空间噪声强度。0 = 完全均匀；1 = 完全由噪声决定。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gem", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float SpatialVariance = 0.6f;

	/** 开采硬度。1 = 普通，2.5 = 极硬。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gem", meta = (ClampMin = "0.1"))
	float Hardness = 1.0f;

	/** 单次开采的基础产量。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gem", meta = (ClampMin = "0.1"))
	float BaseYield = 1.0f;

	/** 矿种本色。高纯度时地表呈现这个颜色。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gem")
	FLinearColor DisplayColor = FLinearColor::White;
};

/** 一颗星球完整的地质配置。 */
USTRUCT(BlueprintType)
struct FGeologyConfig
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gem")
	FGemConfig Iron;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gem")
	FGemConfig Copper;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gem")
	FGemConfig Silicon;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gem")
	FGemConfig Titanium;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gem")
	FGemConfig Uranium;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gem")
	FGemConfig Water;

	/** 星球种子。相同种子 → 完全相同的地质布局。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet")
	int32 PlanetSeed = 1337;

	/** 星球基准半径，单位体素。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet", meta = (ClampMin = "1.0"))
	double PlanetRadius = 1000.0;

	/**
	 * 开采阈值：总重量低于此值视为无矿。
	 * 这是「矿脉」与「整个星球都有一点」的分界。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float MiningThreshold = 0.12f;

	/**
	 * 露天显现阈值：地表权重超过此值才算「露头」。
	 * 比 MiningThreshold 高，避免地表到处都有微弱矿色。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float OutcropThreshold = 0.22f;

	/** 混杂矿的颜色（纯度低时趋向这个色）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet")
	FLinearColor MixedRockColor = FLinearColor(0.32f, 0.28f, 0.24f);

	/** 纯矿呈现本色所需的纯度。低于此值开始向混杂色过渡。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float PurityForFullColor = 0.75f;

	/** 取某矿种的配置。 */
	const FGemConfig* GetGemConfig(EGemType Gem) const;

	/** 列出星球上存在的矿种。 */
	void GetPresentGems(TArray<EGemType>& OutGems) const;

	/** 默认配置：一颗类地行星（铁铜硅丰富，钛铀稀少，有水）。 */
	static FGeologyConfig MakeDefaultTerrestrial(double Radius = 1000.0);
};
