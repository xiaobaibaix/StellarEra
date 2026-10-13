// Copyright Voxel Plugin SAS. All Rights Reserved.
// 将 UVoxelPlanetGenerator 封装为可直接摆放的 Actor。

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "VoxelPlanetActor.generated.h"

class AVoxelWorld;
class UMaterialInterface;
class UVoxelPlanetGenerator;
class USceneComponent;
class UVoxelSimpleInvokerComponent;

/**
 * 开箱即用的行星 Actor。
 *
 * 拖进关卡即可创建一颗光滑的球体行星，球心位于 Actor 所在位置。标准 Gizmo
 * 直接可用：移动即平移行星，等比缩放即改变其半径。
 *
 * 非等比缩放被有意忽略：它会剪切体素网格，把行星扭成一团畸形。
 */
UCLASS(Blueprintable)
class AVoxelPlanetActor : public AActor
{
	GENERATED_BODY()

public:
	AVoxelPlanetActor();

	//~ Begin AActor Interface
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
	//~ End AActor Interface

	/** 将当前设置与变换推入生成器，然后重建。 */
	UFUNCTION(BlueprintCallable, Category = "Planet")
	void RebuildPlanet();

	/** 行星表面半径（厘米，含 Actor 等比缩放）。角色据此贴合球面。 */
	UFUNCTION(BlueprintPure, Category = "Planet")
	float GetSurfaceRadiusCm() const;

	//////////////////////////////////////////////////////////////////////////
	// 基础

	/** 半径（体素数，不含 Actor 缩放）。1 体素 = VoxelSize 厘米。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet", meta = (ClampMin = "16"))
	float Radius = 100.f;

	/** 球体表面颜色，以顶点色传给材质。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet|Surface")
	FLinearColor SurfaceColor = FLinearColor(0.18f, 0.42f, 0.16f);

	//////////////////////////////////////////////////////////////////////////
	// 缩放与性能

	/**
	 * 表面材质。世界采用 RGB 材质配置，生成器的表面颜色以顶点色传递：
	 * 材质只需一个接到 Base Color 的 VertexColor 节点。缺了它，世界会退回
	 * 插件自带的纯色默认值，烘出的颜色永远不会显示。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet|Material")
	TObjectPtr<UMaterialInterface> PlanetMaterial;

	/** 单个体素的尺寸（厘米）。必须与 Voxel World 的 VoxelSize 一致。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet|Scale", meta = (ClampMin = "0.0001"))
	float VoxelSize = 100.f;

	/** 生成与网格化体素的后台线程数。线程越多，区块流送越快。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet|Performance", meta = (ClampMin = "1", ClampMax = "16"))
	int32 VoxelThreads = 4;

	/** 比该 LOD 更粗糙的区块不再生成网格。细分精度实际由 Orbit LOD 决定，一般无需改动。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet|Performance", meta = (ClampMin = "0", ClampMax = "25"))
	int32 MaxLOD = 6;

	/**
	 * 行星全局 LOD 基准：整颗星（无论摄像机在哪）都细分到该 LOD。
	 * -1 = 按半径自动推导，使表面三角边长约为半径的 1/16（1km 星约 64m 边长）。
	 * 没有它，轨道视角下唯一的 LOD 来源是编辑器摄像机的短距 Invoker，
	 * 区块会塌到八叉树底部，1km 星球以 1km 间距采样，看起来坑洼不平。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet|Performance", meta = (ClampMin = "-1", ClampMax = "26"))
	int32 OrbitLOD = -1;

	/**
	 * 无论 World Partition 流送单元如何，都让此 Actor 常驻加载。
	 * 若按空间加载，摄像机跨越单元边界时该 Actor 会被销毁重建，
	 * 整个行星生成随之重启，流送空洞会重新打开数十秒。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet|Performance")
	bool bAlwaysLoaded = true;

	/** 当前驱动此行星的生成器实例。加载时重建。 */
	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "Planet")
	TObjectPtr<UVoxelPlanetGenerator> Generator;

	/** 渲染此行星的代理 Voxel World。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Planet")
	TObjectPtr<AVoxelWorld> VoxelWorld;

	/** 行星根组件：位于球心，Invoker 挂在其下随之移动。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Planet")
	TObjectPtr<USceneComponent> PlanetRoot;

	/** 以球心为中心的 LOD Invoker：保证任何视角下整颗星达到 OrbitLOD 精度。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Planet")
	TObjectPtr<UVoxelSimpleInvokerComponent> LODInvoker;

private:
	/** 含 Actor 等比缩放的半径（体素数）。 */
	float GetScaledRadiusVoxels() const;

	/** OrbitLOD 的解析结果：-1 时按半径自动推导，否则钳到 [0, 26]。 */
	int32 ComputeOrbitLOD() const;

	/** 行星球心（体素坐标），由 Actor 位置换算而来。 */
	FVector GetCenterInVoxels() const;

	/** 对所有会传入生成器或 Voxel World 的设置做 FNV-1a 哈希。 */
	uint64 ComputeParamsHash() const;

	/** 上次 RebuildPlanet 构建世界所用的哈希。哈希不变则跳过重建。 */
	uint64 LastParamsHash = 0;
};
