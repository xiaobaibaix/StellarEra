// Copyright Voxel Plugin SAS. All Rights Reserved.
// 行星地形模块的抽象基类：地形长什么样归模块，在哪、多大归行星。

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "VoxelMinimal.h"
// FPlanetWaterConfig（GetWaterConfig 的参数）需要完整定义。
#include "PlanetHydrologyTypes.h"
#include "VoxelPlanetTerrainModule.generated.h"

class FVoxelGeneratorInstance;
class FPlanetGeologyField;
class FPlanetHydrologyField;

/**
 * 行星交给地形模块的上下文。模块刻意拿不到 Actor，只认半径/球心/颜色，所以同一
 * 套预设能挂任意位置与尺寸的行星。不做 USTRUCT：只在 C++ 侧按值传递。
 */
struct FVoxelPlanetTerrainContext
{
	/** 基准半径（体素，不含地形起伏）。 */
	float Radius = 100.f;

	/** 行星球心（体素坐标，VoxelWorld 局部空间）。 */
	FVector Center = FVector::ZeroVector;

	/** 基准色。纯球模块直接用它；自带配色的模块可当兜底色或忽略。 */
	FLinearColor BaseColor = FLinearColor(0.18f, 0.42f, 0.16f);

	/** 地质场；空 = 没有矿。只读、构造后不可变，可安全在后台体素线程共享。 */
	TSharedPtr<const FPlanetGeologyField> Geology;

	/** 水文场；空 = 没有水（纯球模块或未重写 GetWaterConfig）。只读、不可变，密度场靠它下切河道。 */
	TSharedPtr<const FPlanetHydrologyField> Hydrology;
};

/**
 * 行星地形的可插拔模块：给定体素坐标回答「实体还是空气」。做成 UDataAsset 让一套
 * 地形在多颗行星间共享。子类必须保证 GetValueRangeImpl 覆盖 GetValueImpl 的所有
 * 取值 —— 估低出洞、估高卡死，且都是静默的。
 */
UCLASS(Abstract)
class UVoxelPlanetTerrainModule : public UDataAsset
{
	GENERATED_BODY()

public:
	/**
	 * 产出密度场实例，重建世界时调用一次，随后快照在后台线程跑几百万次。
	 * 默认实现返回纯球：子类忘重写时给颗光滑球，好过行星消失。
	 */
	virtual TVoxelSharedRef<FVoxelGeneratorInstance> GetInstance(const FVoxelPlanetTerrainContext& Context);

	/** 某方向的地表半径（体素，含起伏）。原先假设完美球面会把角色塞进山体；
	 * 海平面 clamp 由模块内部消化。UnitDir 为体素空间单位方向。 */
	virtual float GetSurfaceRadiusVoxels(const FVoxelPlanetTerrainContext& Context, const FVector& UnitDir) const
		PURE_VIRTUAL(UVoxelPlanetTerrainModule::GetSurfaceRadiusVoxels, return Context.Radius;);

	/** 未削平的归一化位移（0 = 基准球面）。与 GetSurfaceRadiusVoxels 的区别是有
	 * 没有被海平面削平，是水文烘焙的唯一输入 —— 混用会把海床当陆地再淹一遍。 */
	virtual float GetRawOffset(const FVector& UnitDir) const;

	/** 水体配置。false = 不要水；OutConfig 在 false 时仍是合法默认值，调用方不必分支。 */
	virtual bool GetWaterConfig(FPlanetWaterConfig& OutConfig) const;

#if WITH_EDITOR
	/** 改了地形参数要重建引用它的行星 —— RebuildPlanet 只认自己的属性，
	 * 资产参数变化它毫不知情，看起来像「参数没生效」。 */
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
};
