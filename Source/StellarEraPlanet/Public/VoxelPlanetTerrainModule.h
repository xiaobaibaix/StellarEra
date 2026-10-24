// Copyright Voxel Plugin SAS. All Rights Reserved.
// 行星地形模块的抽象基类：地形长什么样归模块，在哪、多大归行星。

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "VoxelMinimal.h"
#include "VoxelPlanetTerrainModule.generated.h"

class FVoxelGeneratorInstance;
class FPlanetGeologyField;

/**
 * 行星交给地形模块的上下文。
 *
 * 模块刻意拿不到 Actor：它只认半径、球心和一个颜色。所以同一套地形预设能挂在
 * 任意位置、任意尺寸的行星上，行星的「在哪、多大」与模块的「长什么样」在这里
 * 交汇，也只在这里交汇。
 *
 * 刻意不做成 USTRUCT：它只在 C++ 侧按值传递，不需要反射，也不需要进蓝图。
 */
struct FVoxelPlanetTerrainContext
{
	/** 基准半径（体素，不含地形起伏）。 */
	float Radius = 100.f;

	/** 行星球心（体素坐标，VoxelWorld 局部空间）。 */
	FVector Center = FVector::ZeroVector;

	/**
	 * 行星上的基准色。纯球模块直接用它；自带配色的模块可以当兜底色，
	 * 也可以完全忽略。
	 */
	FLinearColor BaseColor = FLinearColor(0.18f, 0.42f, 0.16f);

	/**
	 * 这颗行星的地质场；为空 = 没有矿。
	 *
	 * 只读指针，构造后不可变，因此可以安全地在后台体素线程上共享。
	 */
	TSharedPtr<const FPlanetGeologyField> Geology;
};

/**
 * 行星地形的可插拔模块。
 *
 * 模块负责**生成密度场** —— 给定体素坐标，回答「这里是实体还是空气」。
 * 行星只负责把球心、半径和基准色递进来，以及把产出的实例交给 VoxelWorld。
 *
 * 做成 UDataAsset 而不是内联子对象，是为了让一套地形能在多颗行星之间共享：
 * 在内容浏览器里改一次预设，引用它的行星一起变。
 *
 * 加一种新地形 = 加一个子类 + 在它的资产里调参数，行星和生成器都不用动。
 *
 * 子类必须保证的一件事：产出的实例，其 GetValueRangeImpl 返回的区间**必须覆盖**
 * GetValueImpl 在同样范围内可能产生的所有值。这是插件的契约，估低了地表会出洞，
 * 估高了行星会卡死，而且两种都是静默的。推荐做法是把区间从模块自己声明的包络
 * （振幅、海平面之类）推导出来，而不是手算常量 —— 见 VoxelPlanetNoiseTerrain.cpp。
 */
UCLASS(Abstract)
class UVoxelPlanetTerrainModule : public UDataAsset
{
	GENERATED_BODY()

public:
	/**
	 * 产出密度场实例。行星的生成器在重建世界时调用一次，实例随后被快照下来，
	 * 在后台线程上被调用几百万次。
	 *
	 * 基类的默认实现返回纯球，见 .cpp —— 抽象类不该被实例化，但真有子类忘了
	 * 重写时，给一颗光滑球总好过让整颗行星消失。
	 */
	virtual TVoxelSharedRef<FVoxelGeneratorInstance> GetInstance(const FVoxelPlanetTerrainContext& Context);

	/**
	 * 某个方向上的地表半径（体素，含起伏）。
	 *
	 * 给角色出生和飞船生成用。它们原先假设地表是一个完美球面
	 * （见 PlanetCharacter.cpp 的 SnapToPlanetSurface），有地形之后那个假设会把
	 * 角色直接塞进山体里。海平面之类的 clamp 由模块内部消化，调用方不必知道
	 * 有海这回事。
	 *
	 * UnitDir 是体素空间的单位方向（由球心指向外的方向）。
	 */
	virtual float GetSurfaceRadiusVoxels(const FVoxelPlanetTerrainContext& Context, const FVector& UnitDir) const
		PURE_VIRTUAL(UVoxelPlanetTerrainModule::GetSurfaceRadiusVoxels, return Context.Radius;);

#if WITH_EDITOR
	/**
	 * 改了地形参数要让引用它的行星重建。
	 *
	 * 行星的 RebuildPlanet 只在**自己的**属性被改时触发，资产里的参数改了它毫不知情
	 * —— 在资产面板里调半天振幅，关卡里的行星纹丝不动，看起来像「参数没生效」。
	 */
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
};
