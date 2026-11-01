// 把一个元素的矿物场接进 VoxelFree。
//
// 每元素一个本类实例。它自己**不产生任何几何定义** —— 判定全在
// PlanetMineralVoxel::EvaluateVoxel 里，这里只负责把参数搬进生成器实例、
// 并把区间查询翻译成「这整块是不是纯空气」。

#pragma once

#include "CoreMinimal.h"
#include "Minerals/View/PlanetMineralVoxelSampling.h"
#include "VoxelGenerators/VoxelGeneratorHelpers.h"
#include "VoxelMinimal.h"
#include "PlanetMineralVoxelGenerator.generated.h"

/**
 * 生成器实例。
 *
 * 模板第二个参数是**宿主类型**，必须是 UVoxelGenerator 子类：
 * TVoxelGeneratorInstanceHelper 会把它作为 UStaticClass 交给基类，
 * 构造期 checkf 它确实派生自 UVoxelGenerator。
 */
class FPlanetMineralVoxelInstance
	: public TVoxelGeneratorInstanceHelper<FPlanetMineralVoxelInstance, UPlanetMineralVoxelGenerator>
{
public:
	using Super = TVoxelGeneratorInstanceHelper<
		FPlanetMineralVoxelInstance, UPlanetMineralVoxelGenerator>;

	explicit FPlanetMineralVoxelInstance(
		const FPlanetMineralVoxelParams& InParams, const FLinearColor& InColor);

	//~ Begin FVoxelGeneratorInstance Interface
	v_flt GetValueImpl(v_flt X, v_flt Y, v_flt Z, int32 LOD, const FVoxelItemStack& Items) const;
	FVoxelMaterial GetMaterialImpl(v_flt X, v_flt Y, v_flt Z, int32 LOD, const FVoxelItemStack& Items) const;
	TVoxelRange<v_flt> GetValueRangeImpl(const FVoxelIntBox& Bounds, int32 LOD, const FVoxelItemStack& Items) const;

	/**
	 * 必需：FVoxelGeneratorInstance::GetUpVector 是**纯虚**的
	 * （VoxelGeneratorInstance.h:100），而 TVoxelGeneratorInstanceHelper 只转发了
	 * 值/材质/区间三件套，不提供默认实现 —— 缺了它本类就是抽象类，
	 * MakeVoxelShared 当场报 C2259。
	 *
	 * 插件的放置器与 PackWorldUpInUVs 用它给物体/UV 定向。
	 */
	virtual FVector GetUpVector(v_flt X, v_flt Y, v_flt Z) const override final;
	//~ End FVoxelGeneratorInstance Interface

private:
	FPlanetMineralVoxelParams Params;
	FLinearColor Color;

	/**
	 * 区域盒换算到体素索引空间后的并集 AABB。GetValueRangeImpl 用它做整块判空。
	 *
	 * 单独存一份而不是每次重算：区间查询按块调用，重建 AABB 在块数量级上是纯浪费。
	 */
	FBox3d RegionBoundsVoxels;
	bool bHasRegionBounds = false;
};

/**
 * 一个元素的矿物生成器。
 *
 * 参数**不走** UVoxelGenerator 的属性系统：VoxelFree 的 GetParameters 会枚举
 * 可编辑属性并构造 FVoxelGeneratorParameterType，而它只认 9 种类型。全部参数
 * 由 UPlanetMineralViewComponent 在 CreateWorld 之前用 Configure 直接赋值。
 */
UCLASS()
class STELLARERAPLANET_API UPlanetMineralVoxelGenerator : public UVoxelGenerator
{
	GENERATED_BODY()

public:
	//~ Begin UVoxelGenerator Interface
	virtual TVoxelSharedRef<FVoxelGeneratorInstance> GetInstance() override;
	//~ End UVoxelGenerator Interface

	/** 一次性灌入全部参数。必须在 CreateWorld 之前调用。 */
	void Configure(const FPlanetMineralVoxelParams& InParams, const FLinearColor& InColor);

private:
	FPlanetMineralVoxelParams Params;
	FLinearColor Color = FLinearColor::White;
};
