// Copyright Voxel Plugin SAS. All Rights Reserved.
// 行星的矿物透视开关：进出模式、隐藏/恢复行星与大气、跟随、按元素筛选。

#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "PlanetGeologyTypes.h"
#include "PlanetMineralSurface.h"
#include "VoxelData/VoxelDataLock.h"
#include "PlanetMineralViewComponent.generated.h"

class APlanetMineralViewActor;
class AVoxelPlanetActor;
class FPlanetGeologyField;
class UPlanetGeologyPreset;

/**
 * 行星上的矿物透视组件。观察性质：不改体素、不碰存档、不影响 N 体。
 *
 * 生命周期：Activate 时解析采样并 spawn 一个独立的 APlanetMineralViewActor，
 * 同时隐藏行星、关碰撞、关大气；Deactivate 全部恢复，重复调用安全。
 * 只在激活期间 Tick，每帧把观察体钉到行星球心。
 *
 * 编辑器预览（EnterMineralPreviewInEditor）是同一套状态的另一种宿主世界：那里只做
 * 非序列化的临时隐藏行星（不关碰撞、不关大气、不翻任何 UPROPERTY，退出后关卡不脏）、
 * 不 Tick（编辑器里行星不动，位置已由 spawn 给定）。
 *
 * 预览期间改地质预设**不会**自动重建，只把预览标脏（MarkPreviewStale）并打一行日志；
 * 要新矿体得点 RefreshMineralPreview。理由是代价与触发频率不匹配：一次重建要重算整份
 * 等值面再加观察体，耗时随 CellsPerFeature 平方增长，而 Details 面板拖一次数值框会
 * 连发十几次改动 —— 自动重建的结果是编辑器全程无响应，且中间十几次的结果没人看。
 */
UCLASS(ClassGroup = (StellarEra), meta = (BlueprintSpawnableComponent))
class STELLARERAPLANET_API UPlanetMineralViewComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	UPlanetMineralViewComponent();

	//~ Begin UActorComponent Interface
	virtual void OnRegister() override;
	virtual void OnUnregister() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
	//~ End UActorComponent Interface

	/** 基类的 Activate(bool) 仍然可用：本类的同名重载只是多了一种入口。 */
	using USceneComponent::Activate;

	/**
	 * 进入矿物透视。
	 * 失败只有三种：没有地质场、采样参数非法、观察体/材质创建失败（打 Error）。
	 */
	bool Activate(AVoxelPlanetActor* InPlanet);

	/** 退出并恢复行星网格、碰撞、大气。重复调用安全。 */
	void Deactivate();

	bool IsActive() const { return bActive; }

#if WITH_EDITOR
	/**
	 * 在编辑器视口里进入矿物预览（不进 PIE）。
	 *
	 * 与 PIE 的差别只有「怎么让行星消失」：这里用非序列化的临时隐藏，
	 * 不翻任何 UPROPERTY，所以退出后关卡不会留下脏包。
	 */
	UFUNCTION(CallInEditor, Category = "Planet|MineralView")
	void EnterMineralPreviewInEditor();

	/** 退出编辑器预览，撤销临时隐藏。重复调用安全。 */
	UFUNCTION(CallInEditor, Category = "Planet|MineralView")
	void ExitMineralPreviewInEditor();

	/**
	 * 按当前地质预设重算预览的矿体。预览没在跑时等价于进入预览。
	 *
	 * **唯一的重建入口**：改预设不会自动重建（见 MarkPreviewStale）。走的是与
	 * EnterMineralPreviewInEditor 同一条路 —— Activate 开头先 Deactivate，缓存按
	 * 地质对象判等，预设改过就必然重建。
	 */
	UFUNCTION(CallInEditor, Category = "Planet|MineralView",
		meta = (DisplayName = "刷新观测视图"))
	void RefreshMineralPreview();

	/**
	 * 把预览标成「显示的是旧矿体」，不做任何重建。
	 *
	 * 返回 true 表示这次是「从新变旧」的那一下 —— 只有这一下打日志：改动是连发的，
	 * 每一下都打会把输出刷满。
	 */
	bool MarkPreviewStale();

	/** 预览里的矿体是否落后于地质预设（预设改过、还没点刷新）。 */
	bool IsPreviewStale() const { return bPreviewStale; }
#endif

	/**
	 * 元素数，返回 Elements.Num()（运行期采样顺序）。
	 *
	 * 与观察体的动态网格组件数相等 —— 这个前提由 APlanetMineralViewActor::Build 的
	 * 「网格数 == 元素数」校验保证（不等时 Build 直接失败）。若将来允许部分构建，
	 * 图例布局与命中测试必须改用 ViewActor->NumElements()。
	 */
	int32 NumElements() const;
	bool IsElementVisible(int32 Index) const;
	/** 参数不能叫 bVisible：USceneComponent 已有同名私有位域，MSVC 的 C4458 在
	 *  本模块是错误。 */
	void SetElementVisible(int32 Index, bool bNewVisible);
	void ToggleElement(int32 Index);
	void SetAllElementsVisible(bool bNewVisible);

	/**
	 * 过渡期开关：true 走体素世界，false 走 marching cubes。
	 *
	 * 只在两条路径并存的那一轮有用，用于 A/B 对照矿体位置。Task 7 删除 MC
	 * 路径时本开关一并删除 —— 不许留在主干。
	 */
	UPROPERTY(EditAnywhere, Category = "Planet|MineralView")
	bool bUseVoxelWorlds = true;

	/** 改某个元素的颜色。体素路径即时生效；没有视图时返回 false。 */
	bool SetElementColor(int32 ElementIndex, const FLinearColor& Color);

	/**
	 * 某个元素的矿量（立方厘米）。
	 *
	 * **走解析场，不走体素世界**：体素世界里的体素数量随 LOD 变（远处块边长
	 * 64 米、近处 1 米），数出来的值不是稳定量。这里按固定步长对区域 AABB 采样，
	 * 结果与相机位置无关。
	 *
	 * 代价是 O((区域边长 / 步长)³)：调用方**必须传粗步长**（米级），别按单个体素
	 * 的尺寸来调。这里不替你夹步长 —— 夹了会让报出的数悄悄变。
	 *
	 * @param StepCm 采样步长（厘米）。≤ 0 或没有地质场时返回 0。
	 */
	float GetElementVolumeCm3(int32 ElementIndex, float StepCm) const;

	/** 所有元素的矿量，下标 = 运行期元素下标。 */
	TArray<float> GetAllElementVolumesCm3(float StepCm) const;

	AVoxelPlanetActor* GetViewPlanet() const { return Planet.Get(); }
	TSharedPtr<const FPlanetGeologyField> GetGeologyField() const { return CachedGeology; }

	/** 运行期元素定义（采样顺序）；图例、数字键、矿体组件下标共用它。 */
	const TArray<FPlanetGeologyElement>& GetElements() const { return Elements; }

	/**
	 * 观测视图密度：一个**特征尺度**横向切多少格（片层=全厚、脉=管直径、
	 * 浸染=粒径、囊=囊直径）。
	 *
	 * 耗时随「簇数 × 元素数 × `CellsPerFeature`²」增长：调大一档，角点数按平方涨，
	 * 首进与重建都跟着变慢。要量覆盖率或耗时请在编辑器**前台**实测 —— 后台会整进程
	 * 限 3 FPS，测出来的秒数没有意义。改动会让缓存失效。
	 */
	UPROPERTY(EditAnywhere, Category = "Planet|MineralView",
		meta = (ClampMin = "4", ClampMax = "32"))
	int32 CellsPerFeature = 12;

	/**
	 * 地质预设。**镜像**：真值在宿主行星 Actor 的 `GeologyAsset` 上，这里只是
	 * Details 面板上多出来的一个可编辑入口（选了矿物组件也能直接换数据）。
	 *
	 * 标 `Transient` 是刻意的：这份值不存盘，磁盘上永远只有行星那一份，不会
	 * 出现「组件存 A、行星存 B」。两个方向各有一条同步路径：
	 *   - 改这里 → `PostEditChangeProperty` → `ApplyGeologyAssetToPlanet()`
	 *   - 改行星 → 行星广播 `OnGeologyAssetChanged` → `RefreshGeologyAssetFromPlanet()`
	 * 少了第二条，先改行星、再选中组件会看到过期值。
	 */
	UPROPERTY(EditAnywhere, Transient, Category = "Planet|MineralView",
		meta = (DisplayName = "Geology Asset"))
	TObjectPtr<UPlanetGeologyPreset> GeologyAsset;

	/** 宿主行星当前的地质预设。宿主不是行星 → nullptr。 */
	UPlanetGeologyPreset* GetPlanetGeologyAsset() const;

	/** 从宿主行星刷新槽里的值。宿主不是行星 → 槽置空（不留一个无主的假值）。 */
	void RefreshGeologyAssetFromPlanet();

	/** 把槽里的值写回宿主行星并标脏。宿主不是行星 → 返回 false，什么都不写。 */
	bool ApplyGeologyAssetToPlanet();

private:
	bool bActive = false;
	TWeakObjectPtr<AVoxelPlanetActor> Planet;
	TSharedPtr<const FPlanetGeologyField> CachedGeology;
	TArray<FPlanetGeologyElement> Elements;

	/** 上一次构建的结果。Deactivate 不清：再进同一颗行星即时出图。 */
	PlanetMineralSurface::FBuildResult CachedSurface;
	PlanetMineralSurface::FCacheKey CachedKey;
	/**
	 * 产出 CachedSurface 的那份地质对象。Deactivate 不清：它是缓存有效性的判据。
	 *
	 * 必须是共享指针而不是裸指针：ReloadGeology() 会换一个新对象，旧对象若已被释放、
	 * 新对象又落在同一地址，裸指针会假相等。持有它同时保证地址不会被复用。
	 */
	TSharedPtr<const FPlanetGeologyField> CachedSurfaceGeology;
	bool bHasCachedSurface = false;

	UPROPERTY(Transient)
	TObjectPtr<APlanetMineralViewActor> ViewActor;

	/**
	 * 视图期间持有的行星体素数据读锁。
	 *
	 * 为什么整段视图期间只锁一次而不是逐体素加锁：生成器在插件工作线程上按体素
	 * 调用密度查询，逐体素加锁的开销会吃掉全部收益。视图期间不做挖掘、且
	 * Activate 已经停掉了行星自己的 LOD 调用器，所以只会与激活瞬间仍在飞的
	 * 少量块生成任务竞争 —— 那把读锁的等待有界。
	 */
	TUniquePtr<FVoxelReadScopeLock> PlanetDataLock;

	/**
	 * 行星的 LOD 调用器是**本组件**关掉的。
	 *
	 * 只恢复**自己关掉的**那一次：进入时调用器本来就是关的话，SetEnabled(false)
	 * 是空操作，退出时无条件打开就等于替别人改了状态。
	 * `bIsInvokerEnabled` 不是 UPROPERTY，切它不会弄脏关卡，编辑器预览里照样可以
	 * 恢复 —— 所以 Deactivate 的恢复**不**按 bEditorPreview 分叉。
	 */
	bool bDisabledPlanetInvoker = false;

	/**
	 * 恢复 Activate 关掉的行星 LOD 调用器。幂等：没关过时是空操作。
	 *
	 * 显式传行星而不是读成员：构建失败那条路径上 Planet 还没赋值，而调用器已经
	 * 被关了 —— 漏掉那一次恢复，行星的 LOD 会一直停着，且日志里没有任何线索。
	 */
	void RestorePlanetInvoker(AVoxelPlanetActor* PlanetActor);

	bool bSavedAtmosphereEnabled = true;
	bool bSavedAtmosphereVisible = true;
	bool bSavedPlanetHidden = false;
	bool bSavedCollisionEnabled = true;

	/** 本次激活是编辑器预览而不是 PIE/运行时。 */
	bool bEditorPreview = false;
	/** 进入前行星是否已被临时隐藏（编辑器分支的恢复值）。 */
	bool bSavedPlanetHiddenInEditor = false;
#if WITH_EDITOR
	/** 预设自上次构建后被改过：预览显示的是旧矿体。 */
	bool bPreviewStale = false;
	/** 预设变更订阅。 */
	FDelegateHandle PresetChangedHandle;
	/** 实际订上的那份预设：退订必须找它，不能用「当下」的 GeologyAsset。 */
	TWeakObjectPtr<UPlanetGeologyPreset> SubscribedPreset;
	void OnPreviewPresetChanged(UPlanetGeologyPreset* Preset);
#endif
};
