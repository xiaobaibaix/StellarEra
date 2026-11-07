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
class UPlanetWaterComponent;
class UVoxelPlanetGenerator;
class UVoxelPlanetTerrainModule;
class UVoxelSimpleInvokerComponent;
class UPlanetGeologyPreset;
class UTextureCube;
class FPlanetGeologyField;

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

	/**
	 * 指定方向的地表半径（厘米，含起伏与 Actor 缩放），角色出生与飞船生成用。
	 * 原先按完美球面算会把角色塞进山体（振幅 8 体素 = 800cm）。WorldDirection
	 * 是世界空间方向、不必归一化；体素与世界空间只差等比缩放，方向可原样传递。
	 */
	UFUNCTION(BlueprintPure, Category = "Planet")
	float GetSurfaceRadiusCmAtDirection(const FVector& WorldDirection) const;

	/**
	 * 指定方向上的**真实体素地表**半径：从体素数据找空/实翻转，挖掘出的坑也看得见
	 * （GetSurfaceRadiusCmAtDirection 只看生成器理想地表）。以解析半径为种子沿径向
	 * march，找不到翻转或行星未创建时返回解析半径。成本约每方向 1 倍半径/体素次
	 * 采样，**每帧每方向调一次即可**。前提：Actor 缩放 1 且无 WorldOffset。
	 */
	float GetVoxelSurfaceRadiusCmAtDirection(const FVector& WorldDirection) const;

	/** 行星的地质场。生成器还没建好时为 nullptr。 */
	TSharedPtr<const FPlanetGeologyField> GetGeologyField();

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

	/** 球体表面颜色，以顶点色传给材质。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet|Surface")
	FLinearColor SurfaceColor = FLinearColor(0.18f, 0.42f, 0.16f);

	//////////////////////////////////////////////////////////////////////////
	// biome cubemap（颜色与网格密度解耦）

	/**
	 * biome cubemap 每面分辨率，0 = 关闭（只剩顶点色）。顶点色只在网格采样点上，
	 * 粗 LOD 下按海拔分带会在 LOD 边界整块翻面（见 PlanetBiomeCubemap.h）；烘到
	 * cubemap 后颜色与网格密度无关。只对噪声地形生效，只在重建时烘焙。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet|Color", meta = (ClampMin = "0", ClampMax = "2048"))
	int32 BiomeCubemapResolution = 256;

	/**
	 * 顶点色与 cubemap 色的混合权重（材质参数 `BiomeCubeBlend`）。1 = 完全查表
	 * （粗 LOD 平滑，近处也只剩 cubemap 分辨率），0 = 回到顶点色（接缝在）。
	 * **默认 1**：接缝是要治的问题；想保留近处细节可调到 0.7 左右。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet|Color", meta = (ClampMin = "0", ClampMax = "1"))
	float BiomeCubemapBlend = 1.f;

	//////////////////////////////////////////////////////////////////////////
	// 地形

	/**
	 * 决定这颗行星地形形态的模块，空 = 光滑球。模块是**可共享资产**：多颗行星可
	 * 引用同一份，改参数一起变；行星的位置 / 半径由本 Actor 决定。模块自带的配色
	 * 会盖过 SurfaceColor，纯球模块没有配色。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet|Terrain")
	TObjectPtr<UVoxelPlanetTerrainModule> Terrain;

	/** 地质预设资产。空 = 这颗行星没有矿。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet|Geology")
	TObjectPtr<UPlanetGeologyPreset> GeologyAsset;

	/**
	 * GeologyAsset 变化时广播。镜像方（UPlanetMineralViewComponent）订阅以保持同步；
	 * 用委托而非直接调用，是因为 Terrain 不能反向 include Minerals，否则成环。
	 */
	DECLARE_MULTICAST_DELEGATE(FOnPlanetGeologyAssetChanged);
	FOnPlanetGeologyAssetChanged OnGeologyAssetChanged;

	/**
	 * 旧字段，只用于加载时迁移（JSON 预设名）。**默认值 "earth" 必须保留**：旧关卡
	 * 从未序列化过它、值一直来自 CDO，删掉默认值会让迁移条件恒假、老关卡丢矿。
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
	 * 旧字段名 → 预设资产路径（`earth` → `/Game/.../DA_Geology_Earth`），空名返回
	 * 空路径。纯函数，便于测试。
	 */
	static FSoftObjectPath MakeLegacyGeologyAssetPath(const FString& LegacyName);

	//////////////////////////////////////////////////////////////////////////
	// 性能

	/**
	 * 行星全局 LOD 基准：整颗星都细分到该 LOD。-1 = 按半径自动推导（三角边长约
	 * 半径 1/16）。没有它，轨道视角下唯一的 LOD 来源是摄像机短距 Invoker，区块会
	 * 塌到八叉树底部，表面坑洼不平。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet|Performance", meta = (ClampMin = "-1", ClampMax = "26"))
	int32 OrbitLOD = -1;

	/**
	 * 无视 World Partition 流送单元常驻加载。按空间加载时，摄像机跨界会销毁重建
	 * 此 Actor，整个行星生成随之重启，流送空洞会重新打开数十秒。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet|Performance")
	bool bAlwaysLoaded = true;

	//////////////////////////////////////////////////////////////////////////
	// 运行时

	/**
	 * 当前驱动此行星的生成器实例，加载时重建。名字不能叫 Generator —— 基类同名
	 * 成员是交给渲染器的 FVoxelGeneratorPicker，RebuildPlanet 负责把前者指向后者。
	 */
	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "Planet")
	TObjectPtr<UVoxelPlanetGenerator> PlanetGenerator;

	/** 以球心为中心的 LOD Invoker。挂在基类的 WorldRoot 下，随之变换。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Planet")
	TObjectPtr<UVoxelSimpleInvokerComponent> LODInvoker;

	/** 行星水面。构造函数里创建，挂在根组件下面。 */
	UPROPERTY(Transient, VisibleAnywhere, Category = "水体")
	TObjectPtr<UPlanetWaterComponent> WaterComponent;

	/** 运行期烘焙的 biome cubemap（瞬态：每次重建重烘，不落盘）。 */
	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "Planet|Color")
	TObjectPtr<UTextureCube> BiomeCubemap;

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

	/** OrbitLOD 的解析结果：-1 时按半径自动推导，否则钳到 [0, 26]。 */
	int32 ComputeOrbitLOD() const;

	/**
	 * 把 OrbitLOD / 半径 / VoxelSize 推给 LOD Invoker，值没变就早退。只影响 LOD
	 * 提示、不影响世界几何，故与 RebuildPlanet 分开；在哈希守卫之前调用，拖动星球
	 * 时每帧都跑。
	 */
	void RefreshLODInvoker();

	/** 对**我们自己的**、会影响生成器或世界尺寸的设置做 FNV-1a 哈希。 */
	uint64 ComputeParamsHash() const;

	/**
	 * 烘焙 biome cubemap 并把材质实例装进 VoxelMaterial。bForceBake true = 无条件
	 * 重烘（地形参数变了）；false = 只在无贴图或分辨率变化时重烘。返回 VoxelMaterial
	 * 是否**被换掉**，调用方据此决定是否 ApplyNewMaterials。
	 */
	bool InstallBiomeMaterial(bool bForceBake);

	/** 按当前水文场重建水面。没有水的行星会拆掉全部分量。 */
	void RebuildWater();

	/** 上次烘焙用的分辨率。-1 = 从没烘过。 */
	int32 BakedCubemapResolution = -1;

	/** PreSave 把 VoxelMaterial 换回 BaseVoxelMaterial 了，PostSaveRoot 负责换回来。 */
	bool bRestoreVoxelMaterialAfterSave = false;

	/** 烘焙失败的警告只刷一次。 */
	bool bLoggedBiomeBakeFailure = false;

	/** 上次 RebuildPlanet 构建世界所用的哈希。哈希不变则跳过重建。 */
	uint64 LastParamsHash = 0;

	/**
	 * 上次重建水面用的 VoxelSize（cm）；-1 = 从没建过。VoxelSize 刻意不进
	 * ComputeParamsHash，但水面要用它折算厘米，所以 RebuildPlanet 在哈希守卫之前
	 * 单独比较 —— 否则只改 VoxelSize 时水面会停在旧尺寸、与地形错位。
	 */
	float LastWaterVoxelSize = -1.f;

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
