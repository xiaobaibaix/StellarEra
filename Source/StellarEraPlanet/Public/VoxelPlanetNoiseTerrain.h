// Copyright Voxel Plugin SAS. All Rights Reserved.
// 噪声地形模块：球面起伏 + 海平面 + 按高度与纬度分色。

#pragma once

#include "CoreMinimal.h"
#include "VoxelPlanetTerrainModule.h"
// 实例的宿主类型。理由见 VoxelPlanetSphereTerrain.h 里同名实例的说明 ——
// 两份实例的模板参数是同一个理由，不是巧合。
#include "VoxelPlanetGenerator.h"
#include "VoxelGenerators/VoxelGeneratorHelpers.h"
#include "VoxelPlanetNoiseTerrain.generated.h"

/**
 * 位移函数的参数快照。
 *
 * 单独拎出来是为了让「模块」与「实例」共用同一份算法：模块在游戏线程上被角色
 * 出生逻辑查询，实例在后台线程上被体素采样调用，两边必须算出同一个数 ——
 * 否则角色会站到与眼睛看到的不同的地面上。共用结构 + 共用自由函数是唯一能保证
 * 这一点的写法。
 *
 * 它同时是**区间正确性的来源**：ComputePlanetOffset 的输出必然落在
 * [SeaLevel, 1]，而这个包络完全由本结构里的字段决定，见实例的 GetValueRangeImpl。
 */
struct FPlanetNoiseParams
{
	// --- 形状 ---

	float Amplitude = 8.f;
	float Frequency = 2.5f;
	int32 Octaves = 4;
	float Lacunarity = 2.f;
	float Gain = 0.5f;
	float SeaLevel = -0.35f;

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

/** 归一化位移，输出必然落在 [SeaLevel, 1]。模块与实例共用的唯一实现。 */
float ComputePlanetOffset(const FPlanetNoiseParams& Params, const FVector& UnitDir);

/**
 * 噪声地形：球面被一个三维噪声场推高推低，低于海平面的部分被削平成海。
 *
 * 噪声采在**单位方向**上，不是 UV 上 —— 球面因此天然无缝，等距圆柱投影那套
 * 极点奇点问题在这里不存在，也不需要处理经线接缝。
 *
 * 这是「看起来像一颗行星」所需的最小完整集：有山、有谷、有海、有雪线。
 */
UCLASS()
class UVoxelPlanetNoiseTerrain : public UVoxelPlanetTerrainModule
{
	GENERATED_BODY()

public:
	//~ Begin UVoxelPlanetTerrainModule Interface
	virtual TVoxelSharedRef<FVoxelGeneratorInstance> GetInstance(const FVoxelPlanetTerrainContext& Context) override;
	virtual float GetSurfaceRadiusVoxels(const FVoxelPlanetTerrainContext& Context, const FVector& UnitDir) const override;
	//~ End UVoxelPlanetTerrainModule Interface

	/** 把当前属性打包成参数快照。实例与查询共用它，保证两边算法一致。 */
	FPlanetNoiseParams MakeParams() const;

	//////////////////////////////////////////////////////////////////////////
	// 起伏

	/**
	 * 相对基准球面的最大起伏（体素）。
	 *
	 * 它同时是**性能旋钮**：密度区间的不确定带宽度就是这个值的两倍，而插件
	 * 只能跳过区间完全在实体外或完全在实体内的区块。调大它行星会变慢。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain|Shape", meta = (ClampMin = "0"))
	float Amplitude = 8.f;

	/**
	 * 单位球面上的噪声频率。越大特征越小越碎。
	 *
	 * 调高它必须同时调低 Amplitude，否则地表会陡到走不上去：
	 * 坡度 ≈ Amplitude × VoxelSize / 半波长，半波长 ≈ π × 半径(体素) / Frequency。
	 * CMC 的 WalkableFloorAngle 默认 44.8°，超过它角色就开始打滑。
	 * 默认值（半径 100 体素、体素 100cm）下坡度约 7°，好走。
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
	 * 海平面（归一化，0 = 基准球面）。
	 *
	 * 低于它的地形在密度场里被**削平** —— 海是一片实心的平面，角色走在水面上。
	 * 真·可下潜的水体（游泳、透明、单独的水面网格）是另一件事，不在这里。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain|Ocean", meta = (ClampMin = "-1", ClampMax = "1"))
	float SeaLevel = -0.35f;

	/** 水色随深度过渡的下潜范围（归一化）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain|Ocean", meta = (ClampMin = "0.001"))
	float WaterDepthRange = 0.25f;

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

	/** 由采样点求相对球心的单位方向；退化到球心时给一个任意方向。 */
	FVector GetUnitDirection(const FVector& RelativePosition) const;
};
