// 程序化球形星球生成器 —— 插件适配层。
//
// 这个类现在很薄：它只负责
//   1. 持有配置（分成多个结构体）
//   2. 把配置交给 FVoxelPlanetTerrain 和 FVoxelPlanetMaterializer
//   3. 实现插件要求的四个纯虚函数，把调用转发给上面两个纯计算类
//
// 真正的数学在 FVoxelPlanetTerrain 里，不依赖任何插件接口，可独立测试。

#pragma once

#include "CoreMinimal.h"
#include "VoxelMinimal.h"
#include "VoxelGenerators/VoxelGeneratorHelpers.h"
#include "VoxelPlanetTypes.h"
#include "VoxelPlanetGenerator.generated.h"

class FVoxelPlanetTerrain;
class FVoxelPlanetMaterializer;

/**
 * 程序化星球生成器。
 *
 * 整个世界是一个有符号距离场：Value = |P - Center| - (Radius + 地形偏移)。
 * 负值实体，正值空气。
 */
UCLASS(Blueprintable)
class VOXELPLANET_API UVoxelPlanetGenerator : public UVoxelGenerator
{
	GENERATED_BODY()

public:
	//~ Begin UVoxelGenerator Interface
	virtual TVoxelSharedRef<FVoxelGeneratorInstance> GetInstance() override;
	//~ End UVoxelGenerator Interface

	//////////////////////////////////////////////////////////////////////////
	// 基础

	/** 星球基准半径，单位体素。世界坐标原点位于 Center。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet", meta = (ClampMin = "16"))
	float Radius = 1000.f;

	/** 星球中心，单位体素。用于在同一个世界里放置多颗星球。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet")
	FVector Center = FVector::ZeroVector;

	/** 随机种子。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet")
	int32 Seed = 1337;

	//////////////////////////////////////////////////////////////////////////
	// 地形分层 —— 每一层独立启停

	/** 第 1 层：大陆。决定海陆分布。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain")
	FVoxelPlanetContinentLayer Continents;

	/** 第 2 层：山脉。模拟造山运动。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain")
	FVoxelPlanetMountainLayer Mountains;

	/** 第 3 层：地表细节。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain")
	FVoxelPlanetDetailLayer Details;

	/** 第 4 层：峡谷。默认关闭。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain")
	FVoxelPlanetRavineLayer Ravines;

	//////////////////////////////////////////////////////////////////////////
	// 内部结构

	/** 洞穴。挖空地壳内部。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interior")
	FVoxelPlanetCaveSettings Caves;

	/** 空心壳层。挖空星球内部。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interior")
	FVoxelPlanetShellSettings Shell;

	//////////////////////////////////////////////////////////////////////////
	// 表面

	/** 海洋设置。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface")
	FVoxelPlanetSeaSettings Sea;

	/** 陆地色带。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface")
	FVoxelPlanetLandPalette Land;

	//////////////////////////////////////////////////////////////////////////
	// 尺寸与性能

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scale")
	FVoxelPlanetScaleSettings Scale;
};

/**
 * 星球生成器实例。
 *
 * 与生成器分离的原因：生成器是 UObject（参数要能被编辑器读写、被 GC 管理），
 * 而密度计算需要多线程并发访问。实例类在构造时把参数拷贝成 const 成员，
 * 之后全程无锁只读 —— 这就是它线程安全的根本原因。
 */
class FVoxelPlanetGeneratorInstance : public TVoxelGeneratorInstanceHelper<FVoxelPlanetGeneratorInstance, UVoxelPlanetGenerator>
{
public:
	using Super = TVoxelGeneratorInstanceHelper<FVoxelPlanetGeneratorInstance, UVoxelPlanetGenerator>;

	explicit FVoxelPlanetGeneratorInstance(UVoxelPlanetGenerator& MyGenerator);

	//~ Begin FVoxelGeneratorInstance Interface
	virtual void Init(const FVoxelGeneratorInit& InitStruct) override;

	/** 密度场。负值实体，正值空气。 */
	v_flt GetValueImpl(v_flt X, v_flt Y, v_flt Z, int32 LOD, const FVoxelItemStack& Items) const;

	/** 材质。深埋体素提前返回以省开销。 */
	FVoxelMaterial GetMaterialImpl(v_flt X, v_flt Y, v_flt Z, int32 LOD, const FVoxelItemStack& Items) const;

	/**
	 * 密度取值区间 —— 星球唯一的性能命门。
	 * 插件的空块剔除完全依赖它。算窄了星球会出现空洞。
	 */
	TVoxelRange<v_flt> GetValueRangeImpl(const FVoxelIntBox& Bounds, int32 LOD, const FVoxelItemStack& Items) const;

	/** 上向量。必须径向，不能返回 FVector::UpVector。 */
	virtual FVector GetUpVector(v_flt X, v_flt Y, v_flt Z) const override final;
	//~ End FVoxelGeneratorInstance Interface

private:
	const float Radius;
	const FVector Center;

	/**
	 * 以下三项在 Init() 里由地形配置推导并缓存，
	 * 避免 GetValueRangeImpl 每次调用都重新计算。
	 * 注意它们不是 const —— Init() 是非 const 虚函数，需要写入。
	 */
	float MaxRadiusOffset = 0.0;
	float MaxCrustRadius = 0.0;
	float CrustPadding = 0.0;

	// 配置只读副本
	const FVoxelPlanetCaveSettings Caves;
	const FVoxelPlanetShellSettings Shell;
	const FVoxelPlanetSeaSettings Sea;

	// 纯计算对象，不持有任何 UObject
	TVoxelSharedPtr<FVoxelPlanetTerrain> Terrain;
	TVoxelSharedPtr<FVoxelPlanetMaterializer> Materializer;
};
