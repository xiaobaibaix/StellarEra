// Copyright Voxel Plugin SAS. All Rights Reserved.
// 将 UVoxelPlanetGenerator 封装为可直接摆放的 Actor。

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "VoxelPlanetActor.generated.h"

class AVoxelWorld;
class UMaterialInterface;
class UVoxelPlanetGenerator;
class UVoxelPlanetTerrainModule;
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

	/**
	 * 无条件重建，绕过「参数没变就跳过」的守卫。
	 *
	 * 给地形模块用：在资产面板里改模块参数时，变的是资产而不是本 Actor 的属性，
	 * RebuildPlanet 的参数哈希察觉不到，那道守卫会把重建整个吞掉，看起来就像
	 * 「参数没生效」。
	 */
	void InvalidateAndRebuild();

	/**
	 * 把 Actor 变换同步给代理体素世界。
	 *
	 * AVoxelWorld 是 Actor 子对象，它的变换不跟着 PlanetRoot 走（见 RebuildPlanet
	 * 里的说明）。RebuildPlanet 会调一次；局部坐标模式下 N 体子系统还会**每帧**
	 * 调一次 —— 星球每帧移动，不同步的话渲染网格会留在原地、星球跑掉。
	 *
	 * 幂等：变换已经一致时什么都不做。
	 */
	void SyncVoxelWorldTransform();

	/**
	 * 行星表面半径（厘米，含 Actor 等比缩放），**不含地形起伏**。
	 *
	 * 这是基准球面的半径。有地形时地表在它上下浮动，想知道「某个方向上的地面
	 * 有多高」要用 GetSurfaceRadiusCmAtDirection。
	 */
	UFUNCTION(BlueprintPure, Category = "Planet")
	float GetSurfaceRadiusCm() const;

	/**
	 * 指定方向上的地表半径（厘米，含起伏与 Actor 等比缩放）。
	 *
	 * 角色出生和飞船生成用它。原先它们假设地表是完美球面，有地形之后那个假设
	 * 会把角色直接塞进山体里（振幅 8 体素 = 800cm，山峰高出基准面 1600cm）。
	 *
	 * WorldDirection 是世界空间方向，不必归一化。体素空间与世界空间之间只有
	 * 等比缩放、没有旋转，方向因此可以原样传递。
	 */
	UFUNCTION(BlueprintPure, Category = "Planet")
	float GetSurfaceRadiusCmAtDirection(const FVector& WorldDirection) const;

	//////////////////////////////////////////////////////////////////////////
	// 基础

	/** 半径（体素数，不含 Actor 缩放）。1 体素 = VoxelSize 厘米。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet", meta = (ClampMin = "16"))
	float Radius = 100.f;

	/** 球体表面颜色，以顶点色传给材质。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet|Surface")
	FLinearColor SurfaceColor = FLinearColor(0.18f, 0.42f, 0.16f);

	//////////////////////////////////////////////////////////////////////////
	// 地形

	/**
	 * 决定这颗行星地形长什么样的模块。
	 *
	 * 空 = 光滑球。模块是**可共享的资产**：在内容浏览器里建一份预设，多颗行星
	 * 可以同时引用它，改一次参数所有引用它的行星一起变。行星自身的「在哪、多大」
	 * 则由本 Actor 上的 Radius / VoxelSize / 变换决定，两者互不干扰。
	 *
	 * 模块自带的配色会盖过上面的 SurfaceColor；纯球模块没有配色，用 SurfaceColor。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet|Terrain")
	TObjectPtr<UVoxelPlanetTerrainModule> Terrain;

	//////////////////////////////////////////////////////////////////////////
	// 坐标模式

	/**
	 * 局部坐标模式。**N 体星系下必须为真。**
	 *
	 * 为真时生成器的球心锚在 Actor 的局部原点，世界位置完全由 Actor 变换承担。
	 * 这正是「移动 Actor 不重建体素世界」的前提：ComputeParamsHash 不再把每帧
	 * 变化的 Actor 位置混进去，重建守卫于是能正常生效。
	 *
	 * 为假时保留旧行为 —— 把 Actor 位置混进哈希，移动 Actor 即触发整颗星重建。
	 * 单颗静止的行星两种模式结果完全相同（球心都在原点），所以默认 true 不影响
	 * 已经摆好的星球。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet|Coordinates")
	bool bUseLocalCoordinates = true;

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

	/** 行星球心（VoxelWorld 局部空间的体素坐标），由 Actor 位置换算而来。 */
	FVector GetCenterInVoxels() const;

	/** 对所有会传入生成器或 Voxel World 的设置做 FNV-1a 哈希。 */
	uint64 ComputeParamsHash() const;

	/** 上次 RebuildPlanet 构建世界所用的哈希。哈希不变则跳过重建。 */
	uint64 LastParamsHash = 0;
};
