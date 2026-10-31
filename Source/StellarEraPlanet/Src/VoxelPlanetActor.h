// Copyright Voxel Plugin SAS. All Rights Reserved.
// 将 UVoxelPlanetGenerator 封装为可直接摆放的 Actor。

#pragma once

#include "CoreMinimal.h"
#include "VoxelWorld.h"
#include "Async/Future.h"
#include "Engine/TimerHandle.h"
#include "VoxelData/VoxelSave.h"
// FObjectPreSaveContext / FObjectPostSaveRootContext：PreSave / PostSaveRoot 覆写的形参。
#include "UObject/ObjectSaveContext.h"
#include "VoxelPlanetActor.generated.h"

class UMaterialInterface;
class UMaterialInstanceDynamic;
class UPlanetSurfaceGridComponent;
class UVoxelPlanetGenerator;
class UVoxelPlanetTerrainModule;
class UVoxelSimpleInvokerComponent;
class UPlanetGeologyPreset;
class UTextureCube;
class FPlanetGeologyField;

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
	virtual void PostLoad() override;
	virtual void PreSave(FObjectPreSaveContext ObjectSaveContext) override;
	virtual void PostSaveRoot(FObjectPostSaveRootContext ObjectSaveContext) override;
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
	 * 体素被编辑后调用：通知表面网格重建，并记为「有未保存编辑」。
	 *
	 * **不在这里存档**（2026-10-28 改）。旧行为是每次编辑都重置一个 2 秒防抖
	 * 定时器，停手 2 秒就整星重存一次 —— 日志实测 CompressVoxelSave 0.29–1.92 s，
	 * 且随累计编辑量线性增长，全在游戏线程上。现在改成退出编辑模式
	 * （RequestSaveEdits）与 EndPlay 才存。
	 */
	void NotifyVoxelEdited();

	/**
	 * 请求把未保存的编辑写进 SaveObject。**GetSave 与压缩在后台线程完成。**
	 *
	 * 回游戏线程只做三件事：把压好的存档赋给 SaveObject、标脏、落盘。
	 * 存档期间又来了编辑就置 pending，等这次任务回来后立刻再存一次 ——
	 * 同一时刻永远只有一份任务在读体素数据。
	 *
	 * 由 UPlanetEditComponent 在退出编辑模式时调用。
	 */
	void RequestSaveEdits();

	/**
	 * **同步**存档：GetSave + 压缩 + 落盘全在调用线程，调用方会被阻塞。
	 *
	 * 只给 EndPlay 用（退出 PIE 必须把最后几笔真正写进磁盘；后台任务的
	 * 异步落盘不保证在进程退出前完成）。平时用 RequestSaveEdits。
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

	/** 行星的地质场。生成器还没建好时为 nullptr。 */
	TSharedPtr<const FPlanetGeologyField> GetGeologyField();

	/**
	 * 世界空间射线 → 行星**体素表面**的命中点（LOD0 数据，与渲染 LOD 无关）。
	 *
	 * 星图用：那里摄像机离地表几千公里，体素碰撞只覆盖几十米，LineTrace 什么也打不到。
	 * 两步：先用随方向变化的半径解析求交（PlanetSurfaceRay），再从该点沿射线做体素
	 * 符号行进，找空/实翻转的格子。误差 ≤ 1 体素。
	 *
	 * 只在返回 true 时写 Out 参数。
	 *
	 * @param MaxDistanceCm        命中点超过这个距离就返回 false（调用方靠它做最近命中剪枝）。
	 * @param OutHitLocationWorld  命中点（世界空间）。
	 * @param OutHitNormalWorld    径向外法线（世界空间）。
	 * @param OutDistanceCm        射线原点到命中点的距离（厘米）。
	 */
	bool RaycastSurfaceCm(const FVector& RayOriginWorld, const FVector& RayDirectionWorld,
		float MaxDistanceCm, FVector& OutHitLocationWorld, FVector& OutHitNormalWorld,
		float& OutDistanceCm) const;

	//////////////////////////////////////////////////////////////////////////
	// 基础

	/** 半径（体素数，不含 Actor 缩放）。1 体素 = 基类的 VoxelSize 厘米。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet", meta = (ClampMin = "16"))
	float Radius = 100.f;

	/** 球体表面颜色，以顶点色传给材质。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet|Surface")
	FLinearColor SurfaceColor = FLinearColor(0.18f, 0.42f, 0.16f);

	//////////////////////////////////////////////////////////////////////////
	// biome cubemap（颜色与网格密度解耦）

	/**
	 * biome cubemap 每面分辨率，0 = 关闭（只剩顶点色）。
	 *
	 * 顶点色只能存在于网格采样点上：LOD 6/7 的采样间距是 32/64 米，而配色是按
	 * 海拔硬分带的 —— 一块粗 LOD 的整片区域会在「水」与「沙」之间整块翻面，
	 * 于是 LOD 边界上出现一条颜色突变的直线（见 PlanetBiomeCubemap.h）。
	 * 把配色按方向烘到 cubemap 上，材质逐像素查表，颜色就与网格密度无关了。
	 *
	 * 只对**噪声地形**（UVoxelPlanetNoiseTerrain）生效：配色是它的成员。
	 * 256 在半径 1 公里的星上约 4~8 米一个纹素；烘焙是一次性成本，
	 * 只在重建行星时发生，移动 Actor 不会重烘。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet|Color", meta = (ClampMin = "0", ClampMax = "2048"))
	int32 BiomeCubemapResolution = 256;

	/**
	 * 材质里顶点色与 cubemap 色的混合权重（材质参数 `BiomeCubeBlend`）。
	 *
	 * 1 = 完全查表（粗 LOD 平滑，近处也只剩 cubemap 的分辨率）；
	 * 0 = 完全顶点色（回到旧行为，接缝在）。**默认 1**：接缝是当前要治的问题，
	 * 而顶点色在粗 LOD 上本来就是错的。想保留近处细节可以调到 0.7 左右。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet|Color", meta = (ClampMin = "0", ClampMax = "1"))
	float BiomeCubemapBlend = 1.f;

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

	/** 地质预设资产。空 = 这颗行星没有矿。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet|Geology")
	TObjectPtr<UPlanetGeologyPreset> GeologyAsset;

	/**
	 * 上面这个指针变化时广播（Details 面板编辑会触发）。
	 *
	 * 镜像方订阅它保持同步 —— 目前是 `UPlanetMineralViewComponent` 的地质预设槽。
	 * 用委托而不是让本类去调组件，是因为依赖方向是 Minerals → Terrain：Terrain
	 * 不能反向 include Minerals，否则成环。
	 */
	DECLARE_MULTICAST_DELEGATE(FOnPlanetGeologyAssetChanged);
	FOnPlanetGeologyAssetChanged OnGeologyAssetChanged;

	/**
	 * 旧字段，只用于加载时迁移（存的是 JSON 预设名）。
	 *
	 * **默认值 "earth" 必须保留**：旧关卡的 Actor 从来没序列化过这个字段，
	 * 它的值一直来自 CDO 默认值。删掉默认值 = 迁移条件恒为假，老关卡直接丢矿。
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Planet|Geology",
		meta = (DeprecatedProperty))
	FString GeologyPreset = TEXT("earth");

	/** 重新取地质场并重建这颗行星：改完预设资产后从这里应用。 */
	UFUNCTION(BlueprintCallable, Category = "Planet|Geology",
		meta = (CallInEditor, DisplayName = "重建行星（应用地质预设改动）"))
	void ReloadGeology();

	/** 当前地质预设资产（供编辑器预览订阅；Task 5 接入）。 */
	UFUNCTION(BlueprintPure, Category = "Planet|Geology")
	UPlanetGeologyPreset* GetGeologyPresetAsset() const { return GeologyAsset; }

	/**
	 * 旧字段名 → 预设资产路径：`earth` → `/Game/Geology/Planets/DA_Geology_Earth`。
	 * 空名返回空路径。纯函数，便于测试。
	 */
	static FSoftObjectPath MakeLegacyGeologyAssetPath(const FString& LegacyName);

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

	/** 运行期烘焙的 biome cubemap（瞬态：每次重建重烘，不落盘）。 */
	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "Planet|Color")
	TObjectPtr<UTextureCube> BiomeCubemap;

	/**
	 * 装了 BiomeCubemap 的材质实例。**运行期 VoxelMaterial 指向的就是它**。
	 *
	 * 它必须当独立字段持有：渲染器只在自己的动态设置里存一个弱指针，
	 * 没有强引用的话这张 MID 会在下一次 GC 被收走。
	 */
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> BiomeMaterial;

	/**
	 * 用户原本设的 VoxelMaterial（MID 的父材质）。从 VoxelMaterial 捕获。
	 *
	 * 存它是为了两件事：重建时不必回头去猜用户想用的是哪个材质，
	 * 以及保存前把 VoxelMaterial 换回它 —— 瞬态对象不进包，留着 MID
	 * 会被写成一个空引用。
	 */
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> BaseVoxelMaterial;

	/**
	 * 行星存档资产的路径：`/Game/Saves/PlanetVoxel/<地图名>/<Actor 标签>`。
	 *
	 * 纯函数，便于无头测试。规则：
	 * - 地图名剥掉 `UEDPIE_<n>_` 前缀 —— 否则每开一次 PIE 就换一个资产路径，
	 *   编辑永远落不到同一个资产上；
	 * - 地图名与标签里非字母数字的字符一律换成 `_`（资产名不允许 `/`、`:`、空格等）；
	 * - 空地图 → `Untitled`，空标签 → `Planet`，保证路径恒定有效。
	 */
	static FString MakePlanetSaveObjectAssetPath(const FString& MapName, const FString& ActorLabel);

	/**
	 * 把 MID 一路剥到能当父材质的那个（Material / MaterialInstanceConstant）。
	 *
	 * 存在的唯一理由：`UMaterialInstance::SetParentInternal` 只接受
	 * `UMaterial` 和 `UMaterialInstanceConstant`，传 MID 过去会被**静默拒绝**，
	 * 新 MID 的 `Parent` 留在 nullptr。而 PIE 复制关卡时，`VoxelMaterial`
	 * （非 Transient）会被拷成上一轮 MID 的副本，`BaseVoxelMaterial`
	 * （Transient）却被清空 —— 于是「用户设的材质」读出来就是那个 MID。
	 * 不剥的话：新 MID 父为空 → 插件建 chunk 材质时 `ParentInstance->Parent`
	 * 为 null → 回退默认材质 → `VoxelRenderUtilities.cpp:518` 的 ensure，
	 * 整颗星退回灰色。
	 *
	 * 传 nullptr 或非 MID 时原样返回；剥到空也返回 nullptr，由调用方决定兜底。
	 * 纯函数，便于无头测试。
	 */
	static UMaterialInterface* StripMaterialInstanceDynamic(UMaterialInterface* Candidate);

	/** 编辑模式下的表面网格基线。构造时自动创建。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Planet|Edit")
	TObjectPtr<UPlanetSurfaceGridComponent> EditGrid;

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

	/**
	 * 烘焙 biome cubemap 并把材质实例装进 VoxelMaterial。
	 *
	 * @param bForceBake true = 无条件重烘（重建行星时用：地形参数变了，色也跟着变）；
	 *                   false = 只在没有贴图或分辨率变了时重烘（调混合权重这类编辑用）。
	 * @return VoxelMaterial 这次**被换掉**了吗。调用方据此决定要不要通知渲染器
	 *         立刻换材质（ApplyNewMaterials）；世界还没建时由 CreateWorld 自己读。
	 */
	bool InstallBiomeMaterial(bool bForceBake);

	/** 上次烘焙用的分辨率。-1 = 从没烘过。 */
	int32 BakedCubemapResolution = -1;

	/** PreSave 把 VoxelMaterial 换回 BaseVoxelMaterial 了，PostSaveRoot 负责换回来。 */
	bool bRestoreVoxelMaterialAfterSave = false;

	/** 烘焙失败的警告只刷一次。 */
	bool bLoggedBiomeBakeFailure = false;

	/** 上次 RebuildPlanet 构建世界所用的哈希。哈希不变则跳过重建。 */
	uint64 LastParamsHash = 0;

	/**
	 * 后台存档任务；无效 = 没有在跑。
	 *
	 * 存成 TFuture 是为了 EndPlay 能先等它跑完再做同步兜底 —— 否则退出时
	 * 会有一份「读到一半」的任务和一次同步 GetSave 同时压在体素数据上。
	 *
	 * 结果是 TSharedPtr 而不是 TUniquePtr：TFuture::Get() 返回的是 const 引用
	 * （Async/Future.h:226），独占指针取不出来，共享指针可以拷一份。
	 */
	TFuture<TSharedPtr<FVoxelCompressedWorldSave>> SaveJob;

	/** 轮询后台任务是否完成（0.05 s 一跳）。完成后自清。 */
	FTimerHandle SavePollTimerHandle;

	/** 存档进行中又来了编辑：这次任务回来后立刻再存一次。 */
	bool bSavePending = false;

	/** 上次保存之后是否又有编辑。只有 NotifyVoxelEdited 会置位。 */
	bool bHasUnsavedEdits = false;

	/** 后台任务结束后在游戏线程收尾：赋值 SaveObject、标脏、落盘。 */
	void FinishSaveJob();

	/** 定时器回调：任务就绪就收尾，并处理存档期间攒下的新编辑。 */
	void PollSaveJob();

	/** 缺 SaveObject 的警告只刷一次，别让每次编辑都往日志里灌。 */
	bool bLoggedMissingSaveObject = false;

#if WITH_EDITOR
	/**
	 * 编辑器世界里懒创建 / 加载本行星的存档资产并绑定到 SaveObject。
	 *
	 * 只在编辑器世界执行：PIE 世界里的 Actor 是从编辑器实例复制来的，值会继承，
	 * 不需要也不应该每开一次 PIE 就重新找资产。资产缺失（例如首次摆放）时用
	 * MakePlanetSaveObjectAssetPath 的路径新建，创建后立刻落盘一次。
	 *
	 * **必须在 RebuildPlanet / CreateWorld 之前调用** —— 世界创建时会读
	 * SaveObject；晚于它赋值，本次运行仍从空白世界开始。
	 */
	void EnsureSaveObject();

	/**
	 * 把 SaveObject 所在的资产包写到磁盘。**同步**：写完才返回。
	 *
	 * 编辑器里的 PIE 只把编辑写进内存中的资产副本，不调用这个函数的话退出
	 * 编辑器后编辑就没了（用户报的「第二次运行什么也没有」的另一半原因）。
	 *
	 * 不用 SAVE_Async：那个标志只把**文件写入**挪到写入器线程，序列化仍在调用
	 * 线程上，省下的不是主要那块；而异步写不保证在 PIE 进程退出前完成，代价是
	 * 「最后一次编辑可能丢」。存档现在一次编辑会话只有一笔（退出编辑模式 /
	 * 退出 PIE），同步写的停顿落在离散动作上，看不出来。
	 *
	 * @return 真的写盘为 true；SaveObject 为空、包是瞬态的（PIE 临时副本）、
	 *         或 SavePackage 失败时为 false。
	 */
	bool SaveSaveObjectToDisk();

	/** 落盘失败的警告只刷一次。成功后复位，下次再失败仍能看见。 */
	bool bLoggedSaveDiskFailure = false;
#endif
};
