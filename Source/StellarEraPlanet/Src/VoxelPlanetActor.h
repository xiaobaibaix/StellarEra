// Copyright Voxel Plugin SAS. All Rights Reserved.
// 把地质流水线的快照封装为可直接摆放的 Actor。

#pragma once

#include "CoreMinimal.h"
#include "VoxelWorld.h"
#include "Async/Future.h"
#include "Engine/TimerHandle.h"
#include "VoxelData/VoxelSave.h"
// FObjectPreSaveContext / FObjectPostSaveRootContext：PreSave / PostSaveRoot 覆写的形参。
#include "UObject/ObjectSaveContext.h"
#include "PlanetGeoGenerator.h"
#include "GeoBakeCache.h"
#include "VoxelPlanetActor.generated.h"

class UMaterialInterface;
class UMaterialInstanceDynamic;
class UPlanetSurfaceGridComponent;
class UPlanetSkyLightComponent;
class UPlanetSunLightComponent;
class UPlanetGeoGenerator;
class UPlanetRecipe;
struct FPlanetSnapshot;
class UVoxelSimpleInvokerComponent;
class UTextureCube;
class UTexture2D;
class AVoxelPlanetActor;

/** 后台地质烘焙的开始 / 结束（结束带成功标志与耗时）。编辑器模块订阅它弹通知。 */
DECLARE_MULTICAST_DELEGATE_OneParam(FGeoBakeStartedEvent, AVoxelPlanetActor*);
DECLARE_MULTICAST_DELEGATE_ThreeParams(FGeoBakeFinishedEvent, AVoxelPlanetActor*, bool /*bSuccess*/, double /*Seconds*/);

/**
 * 开箱即用的行星 Actor。**直接继承 AVoxelWorld** 而非持有子对象：UE 的变换只沿
 * 组件树传播，子对象不随 Actor 动、得每帧手动同步，位置一分叉就出「星球跑了」。
 * 拖进关卡即得光滑球体行星；移动 = 平移，等比缩放 = 改半径，非等比缩放会被忽略。
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
	virtual void PostInitProperties() override;
	virtual void BeginDestroy() override;
#endif
	//~ End AActor Interface

	/** 把当前设置推入生成器，然后重建世界。 */
	UFUNCTION(BlueprintCallable, Category = "Planet")
	void RebuildPlanet();

	/**
	 * 绕过「参数没变就跳过」的守卫强制重建。地形模块参数变的是资产而非本 Actor
	 * 属性，RebuildPlanet 的参数哈希察觉不到，守卫会吞掉重建，看起来像参数没生效。
	 */
	void InvalidateAndRebuild();

	/** 显示 / 隐藏编辑模式下的表面网格。 */
	void SetEditGridVisible(bool bVisible);

	/**
	 * 体素被编辑后调用：通知表面网格重建并标记未保存编辑。**不在这里存档**：旧的
	 * 2 秒防抖重存实测 0.29–1.92 s 且随编辑量线性增长，只在 RequestSaveEdits /
	 * EndPlay 存。
	 */
	void NotifyVoxelEdited();

	/**
	 * 请求把未保存编辑写进 SaveObject，**GetSave 与压缩在后台线程完成**。同一时刻
	 * 只有一份任务在读体素；存档期间又来的编辑置 pending，任务回来后立刻再存一次。
	 */
	void RequestSaveEdits();

	/**
	 * **同步**存档：GetSave + 压缩 + 落盘全在调用线程。只给 EndPlay 用（异步落盘
	 * 不保证进程退出前完成）；平时用 RequestSaveEdits。
	 */
	void SaveEditsToSaveObject();

	/**
	 * 行星表面基准半径（厘米，含 Actor 等比缩放），**不含地形起伏**。想知道某方向
	 * 的地面高度用 GetSurfaceRadiusCmAtDirection。
	 */
	UFUNCTION(BlueprintPure, Category = "Planet")
	float GetSurfaceRadiusCm() const;

	/** 生成器/哈希/LOD/缓存用的半径：不含 Actor 缩放。 */
	float GetGeneratorRadiusVoxels() const { return Radius; }

	/** 物理半径（厘米）：生成器半径 × 体素尺寸，不含 Actor 缩放。仿真与接触门限用它。 */
	float GetPhysicalRadiusCm() const { return GetGeneratorRadiusVoxels() * VoxelSize; }

	/** 行星 LOD 档位：按半径自动推导（三角边长约半径 1/16），不含 Actor 缩放。 */
	int32 ComputeOrbitLOD() const;

	/**
	 * 指定方向的地表半径（厘米，含起伏与 Actor 缩放），角色出生与飞船生成用。
	 * 原先按完美球面算会把角色塞进山体（振幅 8 体素 = 800cm）。WorldDirection
	 * 是世界空间方向、不必归一化；体素与世界空间只差等比缩放，方向可原样传递。
	 */
	UFUNCTION(BlueprintPure, Category = "Planet")
	float GetSurfaceRadiusCmAtDirection(const FVector& WorldDirection) const;

	/** 任何方向的地表都不低于此半径（厘米，含缩放、起伏下界与河床下切）。随半径 / 地形 /
	 *  水文重建而变，大气拿它当解析地面。没有地形模块时等于 GetSurfaceRadiusCm。 */
	UFUNCTION(BlueprintPure, Category = "Planet")
	float GetMinSurfaceRadiusCm() const;

	/**
	 * 指定方向上的**真实体素地表**半径：从体素数据找空/实翻转，挖掘出的坑也看得见
	 * （GetSurfaceRadiusCmAtDirection 只看生成器理想地表）。以解析半径为种子沿径向
	 * march，找不到翻转或行星未创建时返回解析半径。成本约每方向 1 倍半径/体素次
	 * 采样，**每帧每方向调一次即可**。前提：Actor 缩放 1 且无 WorldOffset。
	 */
	float GetVoxelSurfaceRadiusCmAtDirection(const FVector& WorldDirection) const;

	/**
	 * 世界空间射线 → 行星**体素表面**命中点（LOD0，与渲染 LOD 无关）。星图用：摄像机
	 * 离地表几千公里、LineTrace 打不到。先解析求交，再由该点做体素符号行进，误差
	 * ≤ 1 体素。只在返回 true 时写 Out；MaxDistanceCm 供调用方做最近命中剪枝。
	 */
	bool RaycastSurfaceCm(const FVector& RayOriginWorld, const FVector& RayDirectionWorld,
		float MaxDistanceCm, FVector& OutHitLocationWorld, FVector& OutHitNormalWorld,
		float& OutDistanceCm) const;

	//////////////////////////////////////////////////////////////////////////
	// 基础

	/** 半径（体素数，不含 Actor 缩放）。1 体素 = 基类的 VoxelSize 厘米。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet", meta = (ClampMin = "16"))
	float Radius = 100.f;

	/** 半径（米）。按人习惯的单位填，改它会换算成 Radius；改 Radius 或 VoxelSize 时反向回写这里。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet",
		meta = (DisplayName = "RadiusMeters", ClampMin = "1"))
	float RadiusMeters = 10000.f;

	/** 表面兜底色：岩性与矿物由材质解码，只有矿物预览这类旧调用方还要一个底色。 */
	static FLinearColor GetSurfaceColor() { return FLinearColor(0.18f, 0.42f, 0.16f); }

	//////////////////////////////////////////////////////////////////////////
	// 新地质流水线

	/**
	 * 非空 = 这颗星由新地质流水线生成：地形取自配方；半径用本 Actor 的 Radius、体素尺寸用 VoxelSize（配方不存），
	 * GeologyAsset、水文、biome cubemap 旁路。烘焙在后台线程完成后再建世界。
	 */
	/** 是否驱动恒星灯。关掉 = 灯保持原样不再被写；大气材质那份太阳列表不受影响。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet|SunLight")
	bool bDrivesSunLights = true;

	/** 基础强度；每盏灯实际强度 = 本值 × 该恒星的相对亮度 × 平方反比因子。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet|SunLight", meta = (ClampMin = "0.0"))
	float BaseLightIntensity = 10.0f;

	/** 平方反比的参考距离（厘米）：1e8 cm = 1000 km，即主星到行星的典型距离。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet|SunLight", meta = (ClampMin = "1.0"))
	float SunFalloffReferenceDistance = 1.0e8f;

	/** 天光补光强度：暗面全靠它填，0 = 关闭。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet|SunLight", meta = (ClampMin = "0.0"))
	float SkyFillIntensity = 2.0f;

	/** 占位属性：细节面板把它画成「立即烘焙」按钮（行定制在编辑器模块）。 */
	UPROPERTY(EditAnywhere, Transient, Category = "Planet|Geo", meta = (DisplayName = "立即烘焙"))
	FGeoBakeNowAction BakeNowAction;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet|Geo")
	TObjectPtr<UPlanetRecipe> GeoRecipe;

	UFUNCTION(BlueprintPure, Category = "Planet|Geo")
	bool IsGeoMode() const { return GeoRecipe != nullptr; }

	/** 当前地质快照；烘焙完成前为空。 */
	TSharedPtr<const FPlanetSnapshot> GetGeoSnapshot() const { return GeoSnapshot; }

	/** 后台烘焙开始 / 结束。编辑器模块订阅来弹通知；运行时没人订阅就没有开销。 */
	static FGeoBakeStartedEvent GeoBakeStarted;
	static FGeoBakeFinishedEvent GeoBakeFinished;

	/** 立即烘焙：忽略磁盘缓存，强制重跑一遍。 */
	void BakeNow();

	/** 重新烘这颗行星（改完配方后从这里应用）。 */
	UFUNCTION(BlueprintCallable, Category = "Planet|Geo",
		meta = (CallInEditor, DisplayName = "重建行星（应用地质预设改动）"))
	void ReloadGeology();

	/**
	 * 重建星灯。实现在蓝图（BP_VoxelPlanet 的 RebuildStarLightsInternal）：销毁已挂的
	 * 星灯再按 SunStars 重新生成。放在 C++ 只为让按钮落在「星球」栏，蓝图自定义事件
	 * 没有 Category 字段，按钮永远在「默认」。
	 */
	UFUNCTION(BlueprintCallable, Category = "Planet|SunLight",
		meta = (CallInEditor, DisplayName = "RebuildStarLights"))
	void RebuildStarLights();

	//////////////////////////////////////////////////////////////////////////
	// 性能

	/**
	 * 无视 World Partition 流送单元常驻加载。按空间加载时，摄像机跨界会销毁重建
	 * 此 Actor，整个行星生成随之重启，流送空洞会重新打开数十秒。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet")
	bool bAlwaysLoaded = true;

	//////////////////////////////////////////////////////////////////////////
	// 运行时

	/**
	 * geo 模式下的生成器。加载时重建；名字不能叫 Generator —— 基类同名成员是交给
	 * 渲染器的 FVoxelGeneratorPicker，RebuildPlanet 负责把前者指向后者。
	 */
	UPROPERTY(Transient, BlueprintReadOnly)
	TObjectPtr<UPlanetGeoGenerator> GeoGenerator;

	/**
	 * 以球心为中心的 LOD Invoker。挂在基类的 WorldRoot 下，随之变换。
	 * 不加 VisibleAnywhere：组件指针会被细节面板内联展开，把组件自己的分类铺到 Actor 上。
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Planet",
		meta = (DisplayName = "LODInvoker"))
	TObjectPtr<UVoxelSimpleInvokerComponent> LODInvoker;

	/**
	 * 自建大气的天光：按玩家视角算天空立方图喂关卡 SkyLight。缺了环境光就永远是同一张静态图。
	 * 同样不加 VisibleAnywhere（理由见 LODInvoker）。
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Planet")
	TObjectPtr<UPlanetSkyLightComponent> PlanetSkyLight;

	/**
	 * 恒星灯驱动：收集关卡里的恒星，逐颗转向/上色/写强度（含平方反比）。
	 * 挂在星球而不是大气上——没有大气壳的星球同样要有光。
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Planet",
		meta = (DisplayName = "SunLightDriver"))
	TObjectPtr<UPlanetSunLightComponent> SunLightDriver;

	/** 运行期烘焙的 biome cubemap（瞬态：每次重建重烘，不落盘）。 */
	UPROPERTY(Transient, BlueprintReadOnly)
	TObjectPtr<UTextureCube> BiomeCubemap;

	/** 地质调色板 LUT（瞬态：随快照矿物表重建）。材质参数 `GeoPalette`。 */
	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> GeoPaletteTexture;

	/**
	 * 装了 BiomeCubemap 的材质实例，**运行期 VoxelMaterial 指向它**。必须当独立
	 * 字段强引用：渲染器只存弱指针，否则下次 GC 会被收走。
	 */
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> BiomeMaterial;

	/**
	 * 用户原本设的 VoxelMaterial（MID 的父材质）。重建时不必再猜用户想用的材质；
	 * 保存前也靠它把 VoxelMaterial 换回去 —— 瞬态对象不进包。
	 */
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> BaseVoxelMaterial;

	/**
	 * 行星存档资产路径：`/Game/Code/Maps/Saves/PlanetVoxel/<地图名>/<Actor 标签>`。
	 * 地图名剥掉 `UEDPIE_<n>_` 前缀（否则每次 PIE 换路径，编辑落不到同一资产）；
	 * 非字母数字换成 `_`；空地图 → Untitled、空标签 → Planet。纯函数，便于测试。
	 */
	static FString MakePlanetSaveObjectAssetPath(const FString& MapName, const FString& ActorLabel);

	/**
	 * 把 MID 一路剥到能当父材质的 Material / MaterialInstanceConstant。
	 * SetParentInternal 不接受 MID（静默拒绝）；而 PIE 复制关卡时 VoxelMaterial 会
	 * 变成上一轮 MID 的副本、BaseVoxelMaterial 被清空，新 MID 父为空会让插件建
	 * chunk 材质时 ensure、整颗星变灰。传 nullptr / 非 MID 原样返回，纯函数。
	 */
	static UMaterialInterface* StripMaterialInstanceDynamic(UMaterialInterface* Candidate);

	/**
	 * 建 biome MID。**Outer 必须是瞬态包，不能是行星 Actor**：编辑器态插件会把
	 * VoxelMaterial 登记进全局 FVoxelMaterialInterfaceManager（GC root），Outer 指向
	 * Actor 就让旧关卡永远收不掉，切图时以 "World Memory Leaks" 直接 Fatal。
	 */
	static UMaterialInstanceDynamic* CreateBiomeMaterialMID(UMaterialInterface* Parent);

	/**
	 * 256×3 调色板 → 瞬态 UTexture2D。**Outer 必须是瞬态包**（CreateTransient 的默认）：
	 * 挂到 Actor 下会让材质管理器把它钉成 GC root，切图时 World Memory Leaks 直接 Fatal。
	 */
	static UTexture2D* CreatePaletteTexture(const TArray<FColor>& Pixels);

	/**
	 * MID 的父材质：M_GeoPlanet 存在就用它（解地质通道），否则用 Fallback。
	 * **建 MID 时才查**：构造函数里 FObjectFinder 缺失资产会打 CDO Error，且缓存住就再也不看新资产。
	 */
	static UMaterialInterface* ChooseBiomeParentMaterial(UMaterialInterface* Fallback);

#if WITH_DEV_AUTOMATION_TESTS
	/** 生产路径只有烘焙回调能写快照；测试装一颗带矿的星需要这条口子。 */
	void SetGeoSnapshotForTest(TSharedPtr<const FPlanetSnapshot> InSnapshot)
	{
		GeoSnapshot = MoveTemp(InSnapshot);
	}
	bool ApplyGeoDebugOverlayForTest() { return ApplyGeoDebugOverlay(); }
#endif

private:
	/**
	 * 编辑模式下的表面网格基线，**由蓝图子类 BP_VoxelPlanet 挂载**（编辑是可选
	 * 玩法，原生实例不带它）。每次现查而不缓存：编辑器世界里的缓存时机并不可靠。
	 */
	UPlanetSurfaceGridComponent* FindEditGrid() const;

	/**
	 * 修正继承重构前遗留的组件树：确保基类 WorldRoot 就是本 Actor 的根组件。
	 * 旧实例的 RootComponent 是老 PlanetRoot、WorldRoot 游离，导致网格画在世界原点、
	 * 坐标换算按 Actor 位置算，LOD 完全失效。成因见 .cpp。
	 */
	void RepairLegacyRootComponent();

	/** 含 Actor 等比缩放的半径（体素数）。 */
	float GetScaledRadiusVoxels() const;

	/**
	 * LOD to Set 只在首次同步时写，之后面板手改的值归用户、重建不再覆盖；
	 * LOD Range 是几何量，始终跟半径走。不影响世界几何，故在哈希守卫之前调用，
	 * 拖动星球时每帧都跑。
	 */
	void RefreshLODInvoker();

	/** 对**我们自己的**、会影响生成器或世界尺寸的设置做 FNV-1a 哈希。 */
	uint64 ComputeParamsHash() const;

	/** 按当前半径修正 LOD 档位；半径与体素尺寸以本 Actor 为准，配方不存。 */
	void SyncGeoDimensions();

	/** 后台烘焙；烘焙中重复调用被忽略。回调只在游戏线程。 */
	void StartGeoBake();
	void OnGeoBakeFinished(TSharedPtr<const FPlanetSnapshot> InSnapshot, double BakeSeconds);

	/** geo 模式的建世界流程；快照没到时只发起烘焙并返回。 */
	void RebuildGeoPlanet();

	/** 配方产物，非 UPROPERTY：体素线程要的是无 UObject 的 const 快照。 */
	TSharedPtr<const FPlanetSnapshot> GeoSnapshot;
	bool bGeoBakeInFlight = false;
	/** 烘焙失败过：不自动重试，等 InvalidateAndRebuild 强制。 */
	bool bGeoBakeFailed = false;
	/** 烘焙途中配方又改了：当前结果作废，完成后立刻重烘。 */
	bool bGeoRebakePending = false;
	/** 本次烘焙的配置哈希（缓存键）；立即烘焙会跳过缓存。 */
	uint64 PendingCacheHash = 0;
	/** 本次快照来自缓存：命中后不再回写，避免每次启动都把缓存重新标脏。 */
	bool bSnapshotFromCache = false;
	bool bForceRebake = false;
	/** 按高程图层烘地表底图并切换材质；返回材质是否换了（换了要 ApplyNewMaterials）。 */
	bool ApplyGeoDebugOverlay();
	FName OverlayBakedLayer;
	uint64 OverlayBakedHash = 0;
#if WITH_EDITOR
	/** 配方是资产，改它不触发本 Actor 的 PostEditChangeProperty，只能听全局事件。 */
	void OnAnyObjectPropertyChanged(UObject* Object, FPropertyChangedEvent& Event);
	FDelegateHandle RecipeChangedHandle;
#endif

	/** 按快照矿物表建 / 复用 LUT 纹理并绑到 MID 的 `GeoPalette`；无快照时什么都不做。 */
	void BindGeoPalette(UMaterialInstanceDynamic* MID);

	/** 上次建 LUT 用的矿物表哈希；变了才重建纹理。非 UPROPERTY：瞬态缓存不落盘。 */
	uint64 GeoPaletteHash = 0;

	/** PreSave 把 VoxelMaterial 换回 BaseVoxelMaterial 了，PostSaveRoot 负责换回来。 */
	bool bRestoreVoxelMaterialAfterSave = false;

	/** 上次 RebuildPlanet 构建世界所用的哈希。哈希不变则跳过重建。 */
	uint64 LastParamsHash = 0;

	/**
	 * invoker 同步状态：用来区分「值没变」和「用户在面板里手改了」。
	 * 手改过的 LOD to Set 归用户，重建不再覆盖。
	 */
	bool bLODInvokerSynced = false;
	float LastAppliedRangeSource = -1.f;
	int32 LastWrittenLODToSet = INDEX_NONE;

	/**
	 * 后台存档任务；无效 = 没有在跑。存 TFuture 是为了 EndPlay 能先等它结束再做
	 * 同步兜底。结果是 TSharedPtr 因为 TFuture::Get() 返回 const 引用，独占指针取不出。
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
	 * 编辑器世界里懒创建 / 加载本行星的存档资产并绑定到 SaveObject（PIE 世界的值
	 * 会继承，不重复找）。**必须在 RebuildPlanet / CreateWorld 之前调用** —— 世界
	 * 创建时会读 SaveObject，晚赋值本次运行仍从空白世界开始。
	 */
	void EnsureSaveObject();

	/**
	 * 把 SaveObject 所在资产包**同步**写到磁盘，写完才返回。不写的话退出编辑器后
	 * 编辑就没了。不用 SAVE_Async：它只把文件写入挪到写入器线程，序列化仍在调用
	 * 线程，且不保证 PIE 退出前完成；一次编辑会话只有一笔，停顿看不出来。
	 * @return SaveObject 为空、包是瞬态（PIE 临时副本）或 SavePackage 失败时为 false。
	 */
	bool SaveSaveObjectToDisk();

	/** 落盘失败的警告只刷一次。成功后复位，下次再失败仍能看见。 */
	bool bLoggedSaveDiskFailure = false;
#endif
};
