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
 * 行星上的矿物透视组件：不改体素、不碰存档、不影响 N 体。Activate 解析采样并 spawn 独立的 APlanetMineralViewActor，
 * 隐藏行星、关碰撞、关大气，Deactivate 恢复，重复调用安全；编辑器预览用非序列化临时隐藏，退出后关卡不脏。
 * 预览期间改预设不自动重建（拖动会连发十几次，重建按平方增长），只标脏。
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

	/** 进入矿物透视。失败仅三种：无地质场、采样参数非法、观察体/材质创建失败（打 Error）。 */
	bool Activate(AVoxelPlanetActor* InPlanet);

	/** 退出并恢复行星网格、碰撞、大气。重复调用安全。 */
	void Deactivate();

	bool IsActive() const { return bActive; }

#if WITH_EDITOR
	/** 编辑器视口里的矿物预览（不进 PIE）：用非序列化临时隐藏让行星消失，不翻 UPROPERTY，退出后关卡不脏。 */
	UFUNCTION(CallInEditor, Category = "Planet|MineralView")
	void EnterMineralPreviewInEditor();

	/** 退出编辑器预览，撤销临时隐藏。重复调用安全。 */
	UFUNCTION(CallInEditor, Category = "Planet|MineralView")
	void ExitMineralPreviewInEditor();

	/**
	 * 按当前地质预设重算预览矿体；未运行时等价于进入预览。唯一重建入口，缓存按地质对象判等。
	 */
	UFUNCTION(CallInEditor, Category = "Planet|MineralView",
		meta = (DisplayName = "刷新观测视图"))
	void RefreshMineralPreview();

	/**
	 * 把预览标成「显示的是旧矿体」，不重建。返回 true 仅表示「从新变旧」那一下，调用方只在这一下打日志。
	 */
	bool MarkPreviewStale();

	/** 预览里的矿体是否落后于地质预设（预设改过、还没点刷新）。 */
	bool IsPreviewStale() const { return bPreviewStale; }
#endif

	/**
	 * 元素数（运行期采样顺序），等于观察体的动态网格组件数。若将来允许部分构建，图例布局与命中测试须改用 ViewActor->NumElements()。
	 */
	int32 NumElements() const;
	bool IsElementVisible(int32 Index) const;
	/** 参数不能叫 bVisible：USceneComponent 已有同名私有位域，MSVC 的 C4458 在
	 *  本模块是错误。 */
	void SetElementVisible(int32 Index, bool bNewVisible);
	void ToggleElement(int32 Index);
	void SetAllElementsVisible(bool bNewVisible);

	/** 过渡期开关：true 走体素世界，false 走 marching cubes。Task 7 删除 MC 路径时一并删除，不许留在主干。 */
	UPROPERTY(EditAnywhere, Category = "Planet|MineralView")
	bool bUseVoxelWorlds = true;

	/** 改某个元素的颜色。体素路径即时生效；没有视图时返回 false。 */
	bool SetElementColor(int32 ElementIndex, const FLinearColor& Color);

	/**
	 * 某元素的矿量（立方厘米）。走解析场而非体素世界（体素数量随 LOD 变）。代价 O((边长 / 步长)³)，
	 * 调用方必须传米级粗步长，本函数不夹步长。@param StepCm 采样步长（厘米），≤ 0 或无地质场返回 0。
	 */
	float GetElementVolumeCm3(int32 ElementIndex, float StepCm) const;

	/** 所有元素的矿量，下标 = 运行期元素下标。 */
	TArray<float> GetAllElementVolumesCm3(float StepCm) const;

	AVoxelPlanetActor* GetViewPlanet() const { return Planet.Get(); }
	TSharedPtr<const FPlanetGeologyField> GetGeologyField() const { return CachedGeology; }

	/** 运行期元素定义（采样顺序）；图例、数字键、矿体组件下标共用它。 */
	const TArray<FPlanetGeologyElement>& GetElements() const { return Elements; }

	/**
	 * 观测视图密度：一个特征尺度横向切多少格。耗时随「簇数 × 元素数 × CellsPerFeature²」增长，
	 * 须在编辑器前台实测（后台限 3 FPS）。改动会让缓存失效。
	 */
	UPROPERTY(EditAnywhere, Category = "Planet|MineralView",
		meta = (ClampMin = "4", ClampMax = "32"))
	int32 CellsPerFeature = 12;

	/**
	 * 地质预设镜像：真值在宿主行星的 GeologyAsset 上。标 Transient 是刻意的，只存行星那一份，避免两边各存一份。
	 * 双向同步：改这里 → PostEditChangeProperty → ApplyGeologyAssetToPlanet；改行星 → OnGeologyAssetChanged。
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
	 * 产出 CachedSurface 的地质对象，Deactivate 不清，它是缓存有效性的判据。用共享指针而非裸指针：
	 * ReloadGeology() 换新对象后旧对象若释放、新对象落在同一地址，裸指针会假相等。
	 */
	TSharedPtr<const FPlanetGeologyField> CachedSurfaceGeology;
	bool bHasCachedSurface = false;

	UPROPERTY(Transient)
	TObjectPtr<APlanetMineralViewActor> ViewActor;

	/**
	 * 视图期间持有的行星体素数据读锁。整段只锁一次（逐体素加锁会吃掉全部收益）；
	 * 视图期间不挖掘且行星 LOD 调用器已停，只与激活瞬间在飞的少量块生成任务竞争。
	 */
	TUniquePtr<FVoxelReadScopeLock> PlanetDataLock;

	/**
	 * 行星 LOD 调用器是本组件关掉的，退出只恢复自己关掉的那一次 —— 进入时本来就关的话，无条件打开等于替别人改状态。
	 * bIsInvokerEnabled 不是 UPROPERTY，不脏关卡。
	 */
	bool bDisabledPlanetInvoker = false;

	/**
	 * 恢复 Activate 关掉的行星 LOD 调用器，幂等。显式传行星而非读成员：
	 * 构建失败时 Planet 尚未赋值，漏恢复会让 LOD 一直停着且无日志线索。
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
