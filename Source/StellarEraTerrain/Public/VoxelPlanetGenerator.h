// Copyright Voxel Plugin SAS. All Rights Reserved.
// 把行星的「在哪、多大」翻译成地形模块认识的上下文。

#pragma once

#include "CoreMinimal.h"
#include "VoxelMinimal.h"
#include "VoxelGenerators/VoxelGeneratorHelpers.h"
// FVoxelPlanetTerrainContext（MakeTerrainContext 的返回类型）需要完整定义。
#include "VoxelPlanetTerrainModule.h"
#include "VoxelPlanetGenerator.generated.h"

class UVoxelPlanetGenerator;
class UVoxelPlanetSphereTerrain;
class UVoxelPlanetTerrainModule;
class UPlanetGeologyPreset;
class FPlanetGeologyField;

/**
 * 行星生成器适配器。
 *
 * 它自己**不产生任何密度**：地形长什么样全部由 Terrain 指向的地形模块决定
 * （见 UVoxelPlanetTerrainModule）。这个类只做一件事 —— 把行星的半径、球心和
 * 基准色打包成 FVoxelPlanetTerrainContext 递给模块，再把模块产出的实例原样交给
 * VoxelWorld。
 *
 * 为什么非要有这一层：插件的 FVoxelGeneratorPicker 只接受 UVoxelGenerator，
 * 而地形模块是 UDataAsset（这样它才能作为内容浏览器里的共享预设存在）。适配器
 * 是唯一同时满足这两边的类型。它同时也是**每颗行星一份**的：模块是可共享的资产，
 * 上下文是每颗行星私有的，两者必须分开存放。
 */
UCLASS(Blueprintable)
class UVoxelPlanetGenerator : public UVoxelGenerator
{
	GENERATED_BODY()

public:
	//~ Begin UVoxelGenerator Interface
	virtual TVoxelSharedRef<FVoxelGeneratorInstance> GetInstance() override;
	//~ End UVoxelGenerator Interface

	/**
	 * 当前行星的完整地形上下文。**唯一构造点**：密度场与地表查询都从这里拿，
	 * 否则新增字段会出现第三份手工拷贝。
	 */
	FVoxelPlanetTerrainContext MakeTerrainContext();

	/**
	 * 按 GeologyPreset 资产 + Radius 懒加载并缓存。
	 * - 预设为空 → 返回 nullptr；
	 * - 预设没有任何元素 → 返回**有效空场**（IsValid() 且 IsEmpty()），并打 Warning；
	 * - 缓存键是「资产 + 半径 + 资产修订号」，直到 InvalidateGeology()。
	 *
	 * 半径进缓存键的原因：归一化坐标要把体素深度换算成半径比例，同一预设
	 * 装在不同大小的行星上，同一个 Depth 对应的归一化点不同。
	 */
	TSharedPtr<const FPlanetGeologyField> GetGeology();

	/** 丢弃缓存；下次 GetGeology 按当前资产重新构建。 */
	void InvalidateGeology();

	/**
	 * 要用的地形模块。为空时退化成一颗光滑球（内部临时造一个纯球模块），
	 * 因此「没挂地形」不是错误状态，只是最朴素的那种地形。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet")
	TObjectPtr<UVoxelPlanetTerrainModule> Terrain;

	/** 行星半径（体素数）。世界空间原点位于 Center 处。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet", meta = (ClampMin = "16"))
	float Radius = 1000.f;

	/** 行星球心（体素坐标）。便于在同一个世界里摆放多颗行星。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet")
	FVector Center = FVector::ZeroVector;

	/** 交给模块的基准色。模块可以拿它当兜底色，也可以完全忽略（自带配色）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet")
	FLinearColor SurfaceColor = FLinearColor(0.18f, 0.42f, 0.16f);

	/**
	 * 地质预设资产。空 = 关闭地质。
	 *
	 * 必须是 **VisibleAnywhere**（= Edit + EditConst），不能是 EditAnywhere：
	 * VoxelFree 的 UVoxelGenerator::GetParameters 会枚举可编辑属性并构造
	 * FVoxelGeneratorParameterType。作者入口是 AVoxelPlanetActor::GeologyAsset，
	 * RebuildPlanet 每次把它推到这里。
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "Planet|Geology")
	TObjectPtr<UPlanetGeologyPreset> GeologyPreset;

private:
	TSharedPtr<const FPlanetGeologyField> GeologyCache;
	TWeakObjectPtr<UPlanetGeologyPreset> GeologyCachePreset;
	double GeologyCacheRadius = 0.0;
	/** 建缓存时的预设修订号：资产内容变了就重建。 */
	int32 GeologyCacheRevision = 0;

	/**
	 * Terrain 为空时代理的那个纯球模块。
	 *
	 * 做成子对象而不是「另一条分支」：这样无论有没有挂模块，走的都是同一条
	 * 代码路径，不存在一份只在缺模块时才被执行的、平时没人测的逻辑。
	 */
	UPROPERTY(Transient)
	TObjectPtr<UVoxelPlanetSphereTerrain> FallbackTerrain;
};
