// Copyright Voxel Plugin SAS. All Rights Reserved.
// 将 UVoxelPlanetGenerator 封装为可直接摆放的 Actor。

#pragma once

#include "CoreMinimal.h"
#include "VoxelWorld.h"
#include "Engine/TimerHandle.h"
#include "VoxelPlanetActor.generated.h"

class UMaterialInterface;
class UPlanetAtmosphereComponent;
class UPlanetSurfaceGridComponent;
class UVoxelPlanetGenerator;
class UVoxelPlanetTerrainModule;
class UVoxelSimpleInvokerComponent;

/**
 * 开箱即用的行星 Actor。
 *
 * **直接继承 AVoxelWorld**，而不是把它当成子对象持有。
 *
 * 早先的写法是 `CreateDefaultSubobject<AVoxelWorld>`，那造出来的是「Actor 子对象」：
 * 它有 Outer 关系（GC、生命周期跟着走），但**不在组件树里** —— UE 的变换传播只沿
 * 组件树走，所以行星动、体素世界不动，得每帧手动同步一次。两个位置一旦分叉，就是
 * 「星球跑掉了」「渲染网格留在原地」这类问题。
 *
 * 继承之后体素世界**就是**这个 Actor：一个变换、一个生命周期，同步代码整个消失。
 * 代价是基类的属性全部出现在 Details 面板里（约 110 个），其中 `VoxelSize` /
 * `MaxLOD` / `NumberOfThreads` / `VoxelMaterial` 现在由基类自己管、也由基类自己的
 * `meta=(Recreate)` 处理重建 —— 这是有意让出去的，不是遗漏。
 *
 * 拖进关卡即可创建一颗光滑的球体行星，球心位于 Actor 所在位置。标准 Gizmo
 * 直接可用：移动即平移行星，等比缩放即改变其半径。
 *
 * 非等比缩放被有意忽略：它会剪切体素网格，把行星扭成一团畸形。
 */
UCLASS(Blueprintable)
class STELLARERAPLANET_API AVoxelPlanetActor : public AVoxelWorld
{
	GENERATED_BODY()

public:
	AVoxelPlanetActor();

	//~ Begin AActor Interface
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
	//~ End AActor Interface

	/** 把当前设置推入生成器，然后重建世界。 */
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

	/** 显示 / 隐藏编辑模式下的表面网格。 */
	void SetEditGridVisible(bool bVisible);

	/**
	 * 体素被编辑后调用：通知网格重建（防抖），并按 SaveDebounceSeconds 触发保存。
	 * SaveDebounceSeconds = 0 时不在运行中保存，只在 EndPlay 兜一次。
	 */
	void NotifyVoxelEdited();

	/**
	 * 把当前体素数据写进 SaveObject（压缩后）。没有 SaveObject 时只记一条
	 * Warning（每颗行星一次），不崩溃、也不阻止继续编辑。
	 *
	 * 由 NotifyVoxelEdited 的 SaveDebounceSeconds 防抖调用（0 = 只在 EndPlay 保存）；
	 * EndPlay 会再调一次，但只在仍有未保存编辑且有 SaveObject 时才真正写入。
	 */
	void SaveEditsToSaveObject();

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

	/** 半径（体素数，不含 Actor 缩放）。1 体素 = 基类的 VoxelSize 厘米。 */
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
	 * 则由本 Actor 上的 Radius / 基类的 VoxelSize / 变换决定，两者互不干扰。
	 *
	 * 模块自带的配色会盖过上面的 SurfaceColor；纯球模块没有配色，用 SurfaceColor。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet|Terrain")
	TObjectPtr<UVoxelPlanetTerrainModule> Terrain;

	//////////////////////////////////////////////////////////////////////////
	// 性能

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

	//////////////////////////////////////////////////////////////////////////
	// 运行时

	/**
	 * 当前驱动此行星的生成器实例。加载时重建。
	 *
	 * 名字不能叫 Generator —— 基类已经有一个同名的 FVoxelGeneratorPicker
	 * （那是交给渲染器的选择器，这个是我们的适配器实例）。RebuildPlanet 负责把
	 * 前者指向后者。
	 */
	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "Planet")
	TObjectPtr<UVoxelPlanetGenerator> PlanetGenerator;

	/** 以球心为中心的 LOD Invoker。挂在基类的 WorldRoot 下，随之变换。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Planet")
	TObjectPtr<UVoxelSimpleInvokerComponent> LODInvoker;

	/** 大气壳。构造时自动创建，每颗行星一个。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "StellarEra|Atmosphere")
	TObjectPtr<UPlanetAtmosphereComponent> AtmosphereComponent;

	UFUNCTION(BlueprintPure, Category = "StellarEra|Atmosphere")
	UPlanetAtmosphereComponent* GetAtmosphereComponent() const { return AtmosphereComponent; }

	/** 编辑模式下的表面网格基线。构造时自动创建。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Planet|Edit")
	TObjectPtr<UPlanetSurfaceGridComponent> EditGrid;

	/** 编辑后多久写一次 SaveObject（秒，默认 2；0 = 只在 EndPlay 保存）。 */
	UPROPERTY(EditAnywhere, Category = "Planet|Edit", meta = (ClampMin = "0.0"))
	float SaveDebounceSeconds = 2.f;

private:
	/**
	 * 修正继承重构前遗留的组件树：确保基类的 WorldRoot 就是本 Actor 的根组件。
	 *
	 * 已保存的关卡实例把 RootComponent 序列化成了老版本的 PlanetRoot，而
	 * WorldRoot（承载全部体素网格的那个组件）成了游离组件 —— 网格画在世界原点，
	 * 坐标换算却按 Actor 位置算，LOD 因此完全失效。成因与推导见 .cpp。
	 */
	void RepairLegacyRootComponent();

	/** 含 Actor 等比缩放的半径（体素数）。 */
	float GetScaledRadiusVoxels() const;

	/** OrbitLOD 的解析结果：-1 时按半径自动推导，否则钳到 [0, 26]。 */
	int32 ComputeOrbitLOD() const;

	/**
	 * 把 OrbitLOD / 半径 / VoxelSize 推给 LOD Invoker，值没变就什么都不做。
	 *
	 * 与 RebuildPlanet 分开：这三项只影响 LOD 提示，不影响世界几何，改了不必
	 * 重建整颗星。放在哈希守卫**之前**调用，于是拖动星球时它每帧都跑 ——
	 * 「值没变就早退」那道判断就是为这条热路径写的。
	 */
	void RefreshLODInvoker();

	/** 对**我们自己的**、会影响生成器或世界尺寸的设置做 FNV-1a 哈希。 */
	uint64 ComputeParamsHash() const;

	/** 上次 RebuildPlanet 构建世界所用的哈希。哈希不变则跳过重建。 */
	uint64 LastParamsHash = 0;

	/** 防抖保存的定时器。 */
	FTimerHandle SaveTimerHandle;

	/** 上次保存之后是否又有编辑。只有 NotifyVoxelEdited 会置位。 */
	bool bHasUnsavedEdits = false;

	/** 缺 SaveObject 的警告只刷一次，别让每次编辑都往日志里灌。 */
	bool bLoggedMissingSaveObject = false;
};
