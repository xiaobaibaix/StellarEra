// Copyright Voxel Plugin SAS. All Rights Reserved.
// 噪声地形模块：球面起伏 + 海平面 + 按高度与纬度分色。

#pragma once

#include "CoreMinimal.h"
#include "VoxelPlanetTerrainModule.h"
// FPlanetHydrologyField：GetWaterConfig 的配置与实例成员都指向它。
#include "PlanetHydrologyField.h"
// 实例的宿主类型；理由见 VoxelPlanetSphereTerrain.h 的同名实例。
#include "VoxelPlanetGenerator.h"
#include "VoxelGenerators/VoxelGeneratorHelpers.h"
#include "VoxelPlanetNoiseTerrain.generated.h"

/**
 * 位移函数的参数快照：模块（游戏线程）与实例（体素线程）共用同一份算法，否则
 * 角色会站到与眼睛看到的不同地面。它也是区间正确性的来源 —— 输出必落在 [-1,1]。
 */
struct FPlanetNoiseParams
{
	// --- 形状 ---

	float Amplitude = 8.f;
	float Frequency = 2.5f;
	int32 Octaves = 4;
	float Lacunarity = 2.f;
	float Gain = 0.5f;

	/**
	 * 海平面（归一化，0 = 基准球面）。**不削平地形**，只决定配色分带与水面高度。
	 * 默认 0.0 按「水位」重定：旧值 −0.35 会让海只剩 0.01% 表面积的连通块。
	 */
	float SeaLevel = 0.f;

	/** 由 Seed 派生的采样偏移。Perlin 本身无种子，挪动采样起点即可换一套地貌。 */
	FVector SeedOffset = FVector::ZeroVector;

	// --- 配色 ---

	FLinearColor DeepWaterColor = FLinearColor::Black;
	FLinearColor ShallowWaterColor = FLinearColor::Black;
	FLinearColor SandColor = FLinearColor::Black;
	FLinearColor GrassColor = FLinearColor::Black;
	FLinearColor RockColor = FLinearColor::Black;
	FLinearColor SnowColor = FLinearColor::Black;

	float WaterDepthRange = 0.25f;
	float SandToGrassHeight = 0.08f;
	float GrassToRockHeight = 0.55f;
	float PolarStart = 0.72f;
};

/** 归一化位移，输出必落在 [-1,1]；模块与实例共用的唯一实现。海平面不参与形状。 */
float ComputeRawPlanetOffset(const FPlanetNoiseParams& Params, const FVector& UnitDir);

class FPlanetGeologyField;

/**
 * 地表配色：水/沙/草/岩/雪分带 + 极地雪 + 地质元素色。**唯一实现** —— 顶点色与
 * cubemap 查表必须逐字走同一段，否则 LOD 边界上接缝会从采样密度差变成两套配色。
 */
FLinearColor ComputePlanetSurfaceColor(
	const FPlanetNoiseParams& Params,
	const FPlanetGeologyField* Geology,
	const FPlanetHydrologyField* Hydrology,
	double Radius,
	const FVector& UnitDir,
	double Distance);

/**
 * 噪声地形：三维噪声把球面推高推低，海平面以下照常起伏（是海底，不是平面）。
 * 噪声采在单位方向上，球面天然无缝，没有极点奇点或经线接缝。
 */
UCLASS()
class UVoxelPlanetNoiseTerrain : public UVoxelPlanetTerrainModule
{
	GENERATED_BODY()

public:
	//~ Begin UVoxelPlanetTerrainModule Interface
	virtual TVoxelSharedRef<FVoxelGeneratorInstance> GetInstance(const FVoxelPlanetTerrainContext& Context) override;
	virtual float GetSurfaceRadiusVoxels(const FVoxelPlanetTerrainContext& Context, const FVector& UnitDir) const override;
	virtual float GetRawOffset(const FVector& UnitDir) const override;
	virtual bool GetWaterConfig(FPlanetWaterConfig& OutConfig) const override;
	//~ End UVoxelPlanetTerrainModule Interface

	/** 把当前属性打包成参数快照。实例与查询共用它，保证两边算法一致。 */
	FPlanetNoiseParams MakeParams() const;

	//////////////////////////////////////////////////////////////////////////
	// 起伏

	/**
	 * 相对基准球面的最大起伏（体素）。也是性能旋钮：不确定带宽是它的两倍，
	 * 调大行星会变慢。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain|Shape", meta = (ClampMin = "0"))
	float Amplitude = 8.f;

	/**
	 * 单位球面上的噪声频率，越大越碎。调高必须同时调低 Amplitude，否则坡度超过
	 * CMC 的 WalkableFloorAngle（44.8°）角色开始打滑；默认参数下约 7°。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain|Shape", meta = (ClampMin = "0.01"))
	float Frequency = 2.5f;

	/** 分形叠加的层数。**这是唯一影响采样成本的参数**（每体素 Octaves 次 Perlin）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain|Shape", meta = (ClampMin = "1", ClampMax = "8"))
	int32 Octaves = 4;

	/** 每层频率的倍数。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain|Shape", meta = (ClampMin = "1.01"))
	float Lacunarity = 2.f;

	/** 每层振幅的衰减。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain|Shape", meta = (ClampMin = "0", ClampMax = "1"))
	float Gain = 0.5f;

	/** 随机种子。换一个就是一颗地貌完全不同的行星。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain|Shape")
	int32 Seed = 1337;

	//////////////////////////////////////////////////////////////////////////
	// 海

	/**
	 * 海平面（归一化，0 = 基准球面）。**不参与地形形状**，只决定配色分带与水面高度。
	 * 默认 0.0 是「水位」语义下的量：沿用旧值 −0.35 会让海只剩 0.01% 表面积的连通块。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain|Ocean", meta = (ClampMin = "-1", ClampMax = "1"))
	float SeaLevel = 0.f;

	/** 水色随深度过渡的下潜范围（归一化）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain|Ocean", meta = (ClampMin = "0.001"))
	float WaterDepthRange = 0.25f;

	//////////////////////////////////////////////////////////////////////////
	// 水体

	/** 关掉 = 回到「一颗没水的星」：不烘焙、不出水面、地形也没有河床。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain|Water")
	bool bEnableWater = true;

	/** 水文栅格每面分辨率（256 预览 / 512 默认 / 1024 最终）。也决定水面几何精度上限。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain|Water", meta = (ClampMin = "32", ClampMax = "2048"))
	int32 HydrologyResolution = 512;

	/** 汇流量超过它才算河（格数）。越小河越多、越细。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain|Water", meta = (ClampMin = "2"))
	float MinRiverFlow = 24.f;

	/** 河道最大下切深度（体素）。也是密度不确定带的加宽量，调大会减少可跳过区块。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain|Water", meta = (ClampMin = "1", ClampMax = "32"))
	float MaxCarveDepth = 4.f;

	/** 河宽的全局缩放。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain|Water", meta = (ClampMin = "0.1"))
	float RiverWidthScale = 1.f;

	//////////////////////////////////////////////////////////////////////////
	// 配色

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain|Color")
	FLinearColor DeepWaterColor = FLinearColor(0.02f, 0.06f, 0.22f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain|Color")
	FLinearColor ShallowWaterColor = FLinearColor(0.05f, 0.22f, 0.38f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain|Color")
	FLinearColor SandColor = FLinearColor(0.72f, 0.66f, 0.44f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain|Color")
	FLinearColor GrassColor = FLinearColor(0.18f, 0.42f, 0.16f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain|Color")
	FLinearColor RockColor = FLinearColor(0.32f, 0.29f, 0.26f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain|Color")
	FLinearColor SnowColor = FLinearColor(0.92f, 0.94f, 0.97f);

	/** 沙地转草地的归一化高度（0 = 海平面，1 = 最高峰）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain|Color", meta = (ClampMin = "0", ClampMax = "1"))
	float SandToGrassHeight = 0.08f;

	/** 草地转岩石的归一化高度。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain|Color", meta = (ClampMin = "0", ClampMax = "1"))
	float GrassToRockHeight = 0.55f;

	/** 极地起白的位置：|方向 Z| 超过它就开始往雪色靠。1 = 关闭极地雪。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain|Color", meta = (ClampMin = "0", ClampMax = "1"))
	float PolarStart = 0.72f;
};

class FPlanetGeologyField;

/** 密度场实例。模板第二个参数是宿主类型而非本模块 —— 理由见纯球模块的同名类。 */
class FVoxelPlanetNoiseTerrainInstance : public TVoxelGeneratorInstanceHelper<FVoxelPlanetNoiseTerrainInstance, UVoxelPlanetGenerator>
{
public:
	using Super = TVoxelGeneratorInstanceHelper<FVoxelPlanetNoiseTerrainInstance, UVoxelPlanetGenerator>;

	explicit FVoxelPlanetNoiseTerrainInstance(const FVoxelPlanetTerrainContext& Context, const FPlanetNoiseParams& Params);

	//~ Begin FVoxelGeneratorInstance Interface
	v_flt GetValueImpl(v_flt X, v_flt Y, v_flt Z, int32 LOD, const FVoxelItemStack& Items) const;
	FVoxelMaterial GetMaterialImpl(v_flt X, v_flt Y, v_flt Z, int32 LOD, const FVoxelItemStack& Items) const;

	TVoxelRange<v_flt> GetValueRangeImpl(const FVoxelIntBox& Bounds, int32 LOD, const FVoxelItemStack& Items) const;

	virtual FVector GetUpVector(v_flt X, v_flt Y, v_flt Z) const override final;
	//~ End FVoxelGeneratorInstance Interface

private:
	const float Radius;
	const FVector Center;
	const FPlanetNoiseParams Params;

	/** 与 Context.Geology 同一份只读场；为空 = 没有矿。 */
	const TSharedPtr<const FPlanetGeologyField> Geology;

	/** 与 Context.Hydrology 同一份只读场；为空 = 没有水（也就没有河床下切）。 */
	const TSharedPtr<const FPlanetHydrologyField> Hydrology;

	/**
	 * 地表可能落在的径向区间，构造时按同一套包络算好。下界 = Radius − Amplitude −
	 * MaxCarveDepth，上界 = Radius + Amplitude + 最大外凸（SeaLevel 不参与）。
	 * GetValueImpl 靠它做内外早退：比下界深必为实体、比上界高必为空气。
	 */
	const double MinSurfaceRadius;
	const double MaxSurfaceRadius;

	/** 由采样点求相对球心的单位方向；退化到球心时给一个任意方向。 */
	FVector GetUnitDirection(const FVector& RelativePosition) const;
};
