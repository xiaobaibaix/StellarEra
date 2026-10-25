// Copyright Voxel Plugin SAS. All Rights Reserved.
// 行星的矿物透视开关：进出模式、隐藏/恢复行星与大气、跟随、按元素筛选。

#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "PlanetGeologyTypes.h"
#include "PlanetMineralViewComponent.generated.h"

class APlanetMineralViewActor;
class AVoxelPlanetActor;
class FPlanetGeologyField;

/**
 * 行星上的矿物透视组件。观察性质：不改体素、不碰存档、不影响 N 体。
 *
 * 生命周期：Activate 时解析采样并 spawn 一个独立的 APlanetMineralViewActor，
 * 同时隐藏行星、关碰撞、关大气；Deactivate 全部恢复，重复调用安全。
 * 只在激活期间 Tick，每帧把观察体钉到行星球心。
 */
UCLASS(ClassGroup = (StellarEra), meta = (BlueprintSpawnableComponent))
class STELLARERAPLANET_API UPlanetMineralViewComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	UPlanetMineralViewComponent();

	//~ Begin UActorComponent Interface
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;
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

	/**
	 * 元素数，返回 Elements.Num()（运行期采样顺序）。
	 *
	 * 与观察体的 ISM 组数相等 —— 这个前提由 APlanetMineralViewActor::Build 的
	 * 「组数 == 元素数」校验保证（不等时 Build 直接失败）。若将来允许部分构建，
	 * 图例布局与命中测试必须改用 ViewActor->NumElements()。
	 */
	int32 NumElements() const;
	bool IsElementVisible(int32 Index) const;
	/** 参数不能叫 bVisible：USceneComponent 已有同名私有位域，MSVC 的 C4458 在
	 *  本模块是错误。 */
	void SetElementVisible(int32 Index, bool bNewVisible);
	void ToggleElement(int32 Index);
	void SetAllElementsVisible(bool bNewVisible);

	AVoxelPlanetActor* GetViewPlanet() const { return Planet.Get(); }
	TSharedPtr<const FPlanetGeologyField> GetGeologyField() const { return CachedGeology; }

	/** 运行期元素定义（采样顺序）；图例、数字键、方块组下标共用它。 */
	const TArray<FPlanetGeologyElement>& GetElements() const { return Elements; }

	/** 采样间距 / 表面半径。太大没细节，太小方块数与进入耗时爆炸。 */
	UPROPERTY(EditAnywhere, Category = "Planet|MineralView", meta = (ClampMin = "0.01", ClampMax = "0.25"))
	float SpacingRatio = 1.f / 16.f;

	/** 方块边长 / 采样间距。1.0 会让相邻方块互相顶住。 */
	UPROPERTY(EditAnywhere, Category = "Planet|MineralView", meta = (ClampMin = "0.05", ClampMax = "1.0"))
	float BlockSizeRatio = 0.45f;

	/** 抖动种子；同种子生成同一套方块。 */
	UPROPERTY(EditAnywhere, Category = "Planet|MineralView")
	int32 Seed = 1337;

private:
	bool bActive = false;
	TWeakObjectPtr<AVoxelPlanetActor> Planet;
	TSharedPtr<const FPlanetGeologyField> CachedGeology;
	TArray<FPlanetGeologyElement> Elements;

	UPROPERTY(Transient)
	TObjectPtr<APlanetMineralViewActor> ViewActor;

	bool bSavedAtmosphereEnabled = true;
	bool bSavedAtmosphereVisible = true;
	bool bSavedPlanetHidden = false;
	bool bSavedCollisionEnabled = true;
};
