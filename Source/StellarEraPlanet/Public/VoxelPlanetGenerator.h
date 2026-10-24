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
	 * 按 GeologyPreset 懒加载并缓存。
	 * - 预设为空 → 返回 nullptr；
	 * - 加载失败 → 返回**有效空场**（IsValid() 且 IsEmpty()），错误写入日志；
	 * - 缓存按预设名键控，失败结果同样被缓存，直到 InvalidateGeology()。
	 */
	TSharedPtr<const FPlanetGeologyField> GetGeology();

	/** 丢弃缓存；下次 GetGeology 重新读盘。 */
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

	/** 地质预设名：Content/Geology/Planets/<name>.json。空 = 关闭地质。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet|Geology")
	FString GeologyPreset;

private:
	/** 地质场缓存。键是 GeologyPreset；失败也缓存，避免每个采样点都读盘。 */
	TSharedPtr<const FPlanetGeologyField> GeologyCache;
	FString GeologyCachePreset;
	bool bGeologyCacheValid = false;

	/**
	 * Terrain 为空时代理的那个纯球模块。
	 *
	 * 做成子对象而不是「另一条分支」：这样无论有没有挂模块，走的都是同一条
	 * 代码路径，不存在一份只在缺模块时才被执行的、平时没人测的逻辑。
	 */
	UPROPERTY(Transient)
	TObjectPtr<UVoxelPlanetSphereTerrain> FallbackTerrain;
};
