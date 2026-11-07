// 把一个元素的矿物场接进 VoxelFree。每元素一个实例；本类不产生几何定义，
// 判定全在 PlanetMineralVoxel::EvaluateVoxel，这里只搬参数并把区间查询翻成「整块是否纯空气」。

#pragma once

#include "CoreMinimal.h"
#include "Minerals/View/PlanetMineralVoxelSampling.h"
#include "VoxelGenerators/VoxelGeneratorHelpers.h"
#include "VoxelMinimal.h"
#include "PlanetMineralVoxelGenerator.generated.h"

/** 生成器实例。模板第二个参数是宿主类型，必须是 UVoxelGenerator 子类（构造期 checkf）。 */
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
	 * 必需：FVoxelGeneratorInstance::GetUpVector 是纯虚的，而 TVoxelGeneratorInstanceHelper 不提供默认实现 ——
	 * 缺了本类就是抽象类，MakeVoxelShared 报 C2259。插件的放置器与 PackWorldUpInUVs 用它定向。
	 */
	virtual FVector GetUpVector(v_flt X, v_flt Y, v_flt Z) const override final;
	//~ End FVoxelGeneratorInstance Interface

private:
	FPlanetMineralVoxelParams Params;
	FLinearColor Color;

	/**
	 * 区域盒换算到体素索引空间的逐个盒子，GetValueRangeImpl 用它整块判空。
	 * 存逐个盒而非并集 AABB：区域可能散得很开（铀 140 颗颗粒铺满壳层），并集会退化成整壳包围盒，性能杠杆全丢。
	 * 每块 N 次 AABB 求交，相对跳过那块的 32³ = 32768 次求值可忽略；构造期换一次即可。
	 */
	TArray<FBox3d> RegionBoxesVoxels;
};

/**
 * 一个元素的矿物生成器。参数不走 UVoxelGenerator 的属性系统：GetParameters 会枚举可编辑属性且只认 9 种类型；
 * 全部参数由 UPlanetMineralViewComponent 在 CreateWorld 前用 Configure 直接赋值。
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
