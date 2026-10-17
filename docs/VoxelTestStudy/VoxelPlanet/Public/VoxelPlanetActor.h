// 可放置的星球 Actor。
// 把 UVoxelPlanetGenerator 包装成能直接拖进关卡、可用 gizmo 操作的物体。

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "VoxelPlanetTypes.h"
#include "VoxelPlanetActor.generated.h"

class AVoxelWorld;
class UVoxelPlanetGenerator;

/**
 * 可放置的星球 Actor。
 *
 * 拖进关卡后会创建一颗中心位于 Actor 位置的星球。标准 gizmo 可用：
 * 移动即平移星球，等比缩放即改变半径。
 *
 * 非等比缩放会被刻意忽略 —— 它会切变体素网格，把星球变成畸形的斜块。
 *
 * 这个类不做任何地形计算，只负责把设置同步给生成器并管理体素世界生命周期。
 */
UCLASS(Blueprintable)
class VOXELPLANET_API AVoxelPlanetActor : public AActor
{
	GENERATED_BODY()

public:
	AVoxelPlanetActor();

	//~ Begin AActor Interface
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;
	//~ End AActor Interface

	/** 把当前设置与变换推入生成器，然后重建体素世界。 */
	UFUNCTION(BlueprintCallable, Category = "Planet")
	void RebuildPlanet();

	//////////////////////////////////////////////////////////////////////////
	// 坐标模式

	/**
	 * 是否使用局部坐标。
	 *
	 * 开启（默认，宇宙场景需要）：
	 *   密度场球心锚在生成器原点，星球的世界位置完全由 Actor 变换承担。
	 *   这样 Actor 被外力（轨道运动）移动时，生成器参数无需重算，
	 *   渲染网格跟随 WorldRoot 变换即可，不会重建 LOD。
	 *
	 * 关闭（固定场景用）：
	 *   球心跟随 Actor 位置换算。适合放在关卡里不动的星球。
	 *
	 * 代价：生成器参数只在 OnConstruction / BeginPlay 求值一次，
	 * 运行时移动 Actor 不会重新触发。所以局部坐标模式是移动场景的唯一正确选择。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet")
	bool bUseLocalCoordinates = true;

	//////////////////////////////////////////////////////////////////////////
	// 基础

	/** 基准半径，单位体素（未计入 Actor 缩放）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet", meta = (ClampMin = "16"))
	float Radius = 1000.f;

	/** 随机种子。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet")
	int32 Seed = 1337;

	//////////////////////////////////////////////////////////////////////////
	// 地形分层

	/** 第 1 层：大陆。决定海陆分布。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain")
	FVoxelPlanetContinentLayer Continents;

	/** 第 2 层：山脉。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain")
	FVoxelPlanetMountainLayer Mountains;

	/** 第 3 层：地表细节。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain")
	FVoxelPlanetDetailLayer Details;

	/** 第 4 层：峡谷。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain")
	FVoxelPlanetRavineLayer Ravines;

	//////////////////////////////////////////////////////////////////////////
	// 内部结构

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interior")
	FVoxelPlanetCaveSettings Caves;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interior")
	FVoxelPlanetShellSettings Shell;

	//////////////////////////////////////////////////////////////////////////
	// 表面

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface")
	FVoxelPlanetSeaSettings Sea;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface")
	FVoxelPlanetLandPalette Land;

	//////////////////////////////////////////////////////////////////////////
	// 尺寸与性能

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scale")
	FVoxelPlanetScaleSettings Scale;

	//////////////////////////////////////////////////////////////////////////
	// 运行时对象

	/** 当前驱动这颗星球的生成器实例。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Planet")
	TObjectPtr<UVoxelPlanetGenerator> Generator;

	/** 负责渲染这颗星球的代理体素世界。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Planet")
	TObjectPtr<AVoxelWorld> VoxelWorld;

private:
	/** 计入等比缩放后的半径，单位体素。 */
	float GetScaledRadiusVoxels() const;

	/** 由 Actor 位置换算出的星球中心，单位体素。 */
	FVector GetCenterInVoxels() const;
};
