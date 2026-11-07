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
 * 行星生成器适配器：自己不产生密度，只把半径/球心/基准色打包成上下文递给地形
 * 模块。这一层不可省：FVoxelGeneratorPicker 只接受 UVoxelGenerator，而模块是
 * 共享的 UDataAsset。每颗行星一份。
 */
UCLASS(Blueprintable)
class UVoxelPlanetGenerator : public UVoxelGenerator
{
	GENERATED_BODY()

public:
	//~ Begin UVoxelGenerator Interface
	virtual TVoxelSharedRef<FVoxelGeneratorInstance> GetInstance() override;
	//~ End UVoxelGenerator Interface

	/** 当前行星的完整地形上下文。**唯一构造点**，避免新增字段出现多份手工拷贝。 */
	FVoxelPlanetTerrainContext MakeTerrainContext();

	/** 当前水文场；空 = 这颗星不要水。只在 RefreshHydrology() 之后有效。
	 * **不要**在 GetInstance 里懒构建 —— 那会在体素线程触发一次秒级烘焙。 */
	TSharedPtr<const FPlanetHydrologyField> GetHydrology() const;

	/** 需要时重新烘焙，行星在推完 Radius / Terrain、取样实例之前调用。缓存键 =
	 * 模块指针 + 半径 + 水体配置；即使忘了 InvalidateHydrology，改海平面也会重烘。 */
	void RefreshHydrology();

	/** 丢弃缓存；下一次 RefreshHydrology 一定重烘。 */
	void InvalidateHydrology();

	/** 按 GeologyPreset + Radius 懒加载并缓存。预设空 → nullptr；无元素 → 有效空场
	 * 并打 Warning。半径进缓存键，因为归一化深度要换算成半径比例。 */
	TSharedPtr<const FPlanetGeologyField> GetGeology();

	/** 丢弃缓存；下次 GetGeology 按当前资产重新构建。 */
	void InvalidateGeology();

	/** 地形模块。为空时退化成光滑球（内部临时造纯球模块），不是错误状态。 */
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

	/** 地质预设；空 = 关闭地质。**必须 VisibleAnywhere 不能 EditAnywhere**：
	 * GetParameters 会枚举可编辑属性并触发 ensure。作者入口在 Actor::GeologyAsset。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "Planet|Geology")
	TObjectPtr<UPlanetGeologyPreset> GeologyPreset;

private:
	TSharedPtr<const FPlanetGeologyField> GeologyCache;
	TWeakObjectPtr<UPlanetGeologyPreset> GeologyCachePreset;
	double GeologyCacheRadius = 0.0;
	/** 建缓存时的预设修订号：资产内容变了就重建。 */
	int32 GeologyCacheRevision = 0;

	TSharedPtr<FPlanetHydrologyField> HydrologyCache;
	TWeakObjectPtr<UVoxelPlanetTerrainModule> HydrologyCacheTerrain;
	double HydrologyCacheRadius = 0.0;
	FPlanetHydrologyParams HydrologyCacheParams;

	/** Terrain 为空时代理的纯球模块；做成子对象让有无模块走同一条代码路径。 */
	UPROPERTY(Transient)
	TObjectPtr<UVoxelPlanetSphereTerrain> FallbackTerrain;
};
