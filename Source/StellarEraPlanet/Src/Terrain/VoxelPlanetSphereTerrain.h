// Copyright Voxel Plugin SAS. All Rights Reserved.
// 纯球地形模块：等值面是一个严格的球面。

#pragma once

#include "CoreMinimal.h"
#include "VoxelPlanetTerrainModule.h"
// 实例的宿主类型。见下方 FVoxelPlanetSphereTerrainInstance 的说明。
#include "VoxelPlanetGenerator.h"
#include "VoxelGenerators/VoxelGeneratorHelpers.h"
#include "VoxelPlanetSphereTerrain.generated.h"

/**
 * 纯球地形：value = |P - Center| - Radius。「没挂地形模块」时的兜底，也是唯一有
 * 精确解析区间的实现。没有参数，可被选中即代表「光滑球」这种地形。
 */
UCLASS()
class UVoxelPlanetSphereTerrain : public UVoxelPlanetTerrainModule
{
	GENERATED_BODY()

public:
	//~ Begin UVoxelPlanetTerrainModule Interface
	virtual TVoxelSharedRef<FVoxelGeneratorInstance> GetInstance(const FVoxelPlanetTerrainContext& Context) override;
	virtual float GetSurfaceRadiusVoxels(const FVoxelPlanetTerrainContext& Context, const FVector& UnitDir) const override;
	//~ End UVoxelPlanetTerrainModule Interface
};

/**
 * 模板第二参数是**宿主类型**，必须是 UVoxelGenerator 子类（构造期 checkf）；本模块
 * 是 UDataAsset，填自己会触发。填 UVoxelPlanetGenerator 语义正确：真正被序列化、
 * 调 GetInstance 的就是那个适配器。Class 只用于序列化，实例从不被序列化。
 */
class FVoxelPlanetSphereTerrainInstance : public TVoxelGeneratorInstanceHelper<FVoxelPlanetSphereTerrainInstance, UVoxelPlanetGenerator>
{
public:
	using Super = TVoxelGeneratorInstanceHelper<FVoxelPlanetSphereTerrainInstance, UVoxelPlanetGenerator>;

	explicit FVoxelPlanetSphereTerrainInstance(const FVoxelPlanetTerrainContext& Context);

	//~ Begin FVoxelGeneratorInstance Interface
	v_flt GetValueImpl(v_flt X, v_flt Y, v_flt Z, int32 LOD, const FVoxelItemStack& Items) const;
	FVoxelMaterial GetMaterialImpl(v_flt X, v_flt Y, v_flt Z, int32 LOD, const FVoxelItemStack& Items) const;

	TVoxelRange<v_flt> GetValueRangeImpl(const FVoxelIntBox& Bounds, int32 LOD, const FVoxelItemStack& Items) const;

	virtual FVector GetUpVector(v_flt X, v_flt Y, v_flt Z) const override final;
	//~ End FVoxelGeneratorInstance Interface

private:
	const float Radius;
	const FVector Center;
	const FLinearColor SurfaceColor;
};
