// 体素星球的配置类型定义。
//
// 把生成器的 30 个参数按职责拆成独立结构体，而不是平铺在一个类里。
// 好处：
//   1. Details 面板自动按组折叠，30 个参数不会糊成一片
//   2. 每一层可以整体启停、整体复制
//   3. 地形层与插件接口解耦，可以单独测试

#pragma once

#include "CoreMinimal.h"
#include "VoxelPlanetTypes.generated.h"

///////////////////////////////////////////////////////////////////////////////
// 第 1 层：大陆 —— 决定哪里是海、哪里是陆

USTRUCT(BlueprintType)
struct FVoxelPlanetContinentLayer
{
	GENERATED_BODY()

	/** 是否启用这一层。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Continent")
	bool bEnabled = true;

	/** 低频：决定大陆的分布位置。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Continent")
	float Frequency = 2.f;

	/** 大陆的峰值海拔，单位体素。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Continent")
	float Height = 60.f;

	/** 分形倍频数。越多细节越丰富，代价也越高。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Continent", meta = (ClampMin = "1", ClampMax = "8"))
	int32 Octaves = 5;

	/**
	 * 对比度：把噪声从 0 附近推向两极，
	 * 负值变成深海盆地，正值变成高耸大陆。
	 * 0 = 平缓丘陵，1+ = 强烈海陆分化。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Continent", meta = (ClampMin = "0", ClampMax = "4"))
	float Contrast = 1.5f;
};

///////////////////////////////////////////////////////////////////////////////
// 第 2 层：山脉 —— 模拟真实造山运动

USTRUCT(BlueprintType)
struct FVoxelPlanetMountainLayer
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mountain")
	bool bEnabled = true;

	/** 中频：控制山脉走向。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mountain")
	float Frequency = 6.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mountain")
	float Height = 25.f;

	/** 指数锐化。越大峰越尖，越像刀刃山脊。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mountain", meta = (ClampMin = "1", ClampMax = "4"))
	float Sharpness = 2.f;

	/**
	 * 陆地偏置：山脉只长在大陆高地。
	 * 真实山脉形成于板块边界，不会长在汪洋中央。
	 * 0 = 全球均匀分布，1 = 严格限制在陆地上。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mountain", meta = (ClampMin = "0", ClampMax = "1"))
	float LandBias = 0.7f;
};

///////////////////////////////////////////////////////////////////////////////
// 第 3 层：地表细节

USTRUCT(BlueprintType)
struct FVoxelPlanetDetailLayer
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Detail")
	bool bEnabled = true;

	/** 高频：地表粗糙度。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Detail")
	float Frequency = 20.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Detail")
	float Height = 6.f;
};

///////////////////////////////////////////////////////////////////////////////
// 第 4 层：峡谷 —— 只向下切，不向上堆

USTRUCT(BlueprintType)
struct FVoxelPlanetRavineLayer
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ravine")
	bool bEnabled = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ravine")
	float Frequency = 3.f;

	/** 切割深度，单位体素。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ravine")
	float Depth = 15.f;
};

///////////////////////////////////////////////////////////////////////////////
// 洞穴：地壳内部的空腔

USTRUCT(BlueprintType)
struct FVoxelPlanetCaveSettings
{
	GENERATED_BODY()

	/** 洞穴密度。0 = 不挖洞穴。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cave", meta = (ClampMin = "0", ClampMax = "1"))
	float Amount = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cave")
	float Frequency = 3.f;
};

///////////////////////////////////////////////////////////////////////////////
// 空心壳层：挖空星球内部

USTRUCT(BlueprintType)
struct FVoxelPlanetShellSettings
{
	GENERATED_BODY()

	/** 地壳厚度，单位体素。大于 0 时内部中空。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shell", meta = (ClampMin = "0"))
	float Thickness = 0.f;

	/** 中空区域的内壁颜色。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shell")
	FLinearColor CoreColor = FLinearColor(0.22f, 0.10f, 0.05f);
};

///////////////////////////////////////////////////////////////////////////////
// 海平面

USTRUCT(BlueprintType)
struct FVoxelPlanetSeaSettings
{
	GENERATED_BODY()

	/** 是否启用海洋。关闭后所有表面都用陆地色带。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sea")
	bool bEnabled = true;

	/** 海平面深度，单位体素。低于 Radius - Depth 的表面判定为水域。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sea", meta = (ClampMin = "0"))
	float Depth = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sea")
	FLinearColor DeepColor = FLinearColor(0.02f, 0.06f, 0.25f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sea")
	FLinearColor ShoreColor = FLinearColor(0.75f, 0.68f, 0.45f);
};

///////////////////////////////////////////////////////////////////////////////
// 陆地色带：从低地到雪顶的四段渐变

USTRUCT(BlueprintType)
struct FVoxelPlanetLandPalette
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Land")
	FLinearColor LowlandColor = FLinearColor(0.18f, 0.42f, 0.16f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Land")
	FLinearColor HighlandColor = FLinearColor(0.42f, 0.40f, 0.22f);

	/** 雪线高度，自 Radius 起算。0 = 不显示雪顶。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Land", meta = (ClampMin = "0"))
	float SnowLine = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Land")
	FLinearColor SnowColor = FLinearColor(0.92f, 0.92f, 0.95f);
};

///////////////////////////////////////////////////////////////////////////////
// 尺寸与性能

USTRUCT(BlueprintType)
struct FVoxelPlanetScaleSettings
{
	GENERATED_BODY()

	/** 单个体素边长（厘米）。必须与体素世界的 VoxelSize 保持一致。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scale", meta = (ClampMin = "0.0001"))
	float VoxelSize = 100.f;

	/** 体素世界渲染距离，单位 chunk。1 chunk = 32 体素。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scale", meta = (ClampMin = "1"))
	int32 RenderDistanceChunks = 64;

	/** LOD 每提升一级，采样步长翻倍。星球越大越需要调高。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scale", meta = (ClampMin = "0", ClampMax = "10"))
	int32 MaxLOD = 5;
};
