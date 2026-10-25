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
 * 纯球地形：value = |P - Center| - Radius。
 *
 * 这是行星最原始的样子，也是「没挂地形模块」时的兜底。它存在的意义有两个：
 * 一是让「换回光滑球」变成一次资产切换而不是改代码，二是保留那份**精确的解析
 * 区间**（见 FVoxelPlanetSphereTerrainInstance::GetValueRangeImpl）—— 那是行星
 * 唯一的速度杠杆，需要极致性能时仍然用它。
 *
 * 本模块没有自己的参数：颜色取自行星上的基准色（见 FVoxelPlanetTerrainContext）。
 * 一个没有 UPROPERTY 的 UDataAsset 看着奇怪，但它的价值是「可被选中」——
 * 在行星的 Terrain 下拉里挑中它，就是挑中了「光滑球」这种地形。
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
 * 密度场实例。
 *
 * 模板的第二个参数是**宿主类型**，不是本模块自己：TVoxelGeneratorInstanceHelper
 * 会把它作为 UStaticClass 传给 FVoxelGeneratorInstance，而那个参数的类型是
 * TSubclassOf<UVoxelGenerator>，构造期 checkf 它确实派生自 UVoxelGenerator
 * （见 VoxelGeneratorHelpers.h:45 与 VoxelGeneratorInstance.h:59）。
 *
 * 本模块是 UDataAsset，不是 UVoxelGenerator，直接填自己会当场触发那个 checkf。
 * 填 UVoxelPlanetGenerator 才是**语义正确**的选择：真正被 VoxelWorld 序列化、
 * 真正被调用 GetInstance() 的就是那个适配器，Class 字段记的应该是它。
 *
 * 该字段只用于序列化（VoxelGeneratorInstance.h:67 的注释），而生成器实例本身
 * 从不被序列化 —— FVoxelGeneratorPicker 只存 TSubclassOf/TObjectPtr，每次建世界
 * 都重新调 GetInstance()（见 VoxelGeneratorPicker.h:63-65）。所以宿主指针传空
 * 是安全的。
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
