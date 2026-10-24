// Copyright Voxel Plugin SAS. All Rights Reserved.
// 角色身上的行星编辑组件：模式、笔刷状态、屏幕射线与体素工具调用。

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "PlanetEditBrush.h"
#include "PlanetEditComponent.generated.h"

class APlanetCharacter;
class AVoxelPlanetActor;
class ULineBatchComponent;
struct FHitResult;

/**
 * 编辑模式的状态与执行。
 *
 * 挂在 APlanetCharacter 上，不挂在行星上：模式属于「谁在编辑」，角色换星、
 * 上下船、进星图时状态要跟着角色走。行星只负责网格与保存。
 *
 * 本头不 include 任何 Voxel / Planet 头：Voxel 与 Planet 都是本模块的私有依赖。
 * AVoxelPlanetActor 只前向声明，所有插件调用都在 .cpp。
 */
UCLASS(ClassGroup = (StellarEra), meta = (BlueprintSpawnableComponent))
class STELLARERAVEHICLE_API UPlanetEditComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UPlanetEditComponent();

	//~ Begin UActorComponent Interface
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;
	//~ End UActorComponent Interface

	bool IsEditMode() const { return bEditMode; }
	EPlanetBrushShape GetBrushShape() const { return BrushShape; }
	int32 GetBrushSizeVoxels() const { return BrushSizeVoxels; }

	/** 进入 / 退出编辑模式。进入时显示当前行星的网格，退出时隐藏。 */
	void SetEditMode(bool bEnabled);
	void ToggleEditMode() { SetEditMode(!bEditMode); }

	void SetBrushShape(EPlanetBrushShape NewShape);

	/** 滚轮 ±1；内部夹到 [1, 8]。 */
	void AdjustBrushSize(int32 Delta);

	/** 屏幕位置挖一次。返回是否真的改到了体素。 */
	bool DigAtScreenPosition(const FVector2D& ScreenPosition);

	/** 屏幕位置放一次。返回是否真的改到了体素。 */
	bool PlaceAtScreenPosition(const FVector2D& ScreenPosition);

	/**
	 * 预览切换到「放」还是「挖」的中心。
	 *
	 * 右键按住 = true（放，中心沿法线外移半个笔刷），松开 = false（挖，中心就是
	 * 命中点）。只影响下一次 Tick 画出来的线框，不改任何体素。
	 */
	void SetPreviewPlacing(bool bPlacing);

	/** 射线最远距离（厘米）。默认 1000 米：足够覆盖任意可瞄准的表面。 */
	UPROPERTY(EditAnywhere, Category = "PlanetEdit", meta = (ClampMin = "100.0"))
	float MaxEditDistanceCm = 1.0e5f;

	/** 笔刷尺寸下限 / 上限（体素数）。角色 1..8，星图 8..128。 */
	UPROPERTY(EditAnywhere, Category = "PlanetEdit", meta = (ClampMin = "1", ClampMax = "128"))
	int32 MinBrushSizeVoxels = 1;

	UPROPERTY(EditAnywhere, Category = "PlanetEdit", meta = (ClampMin = "1", ClampMax = "128"))
	int32 MaxBrushSizeVoxels = 8;

	/** 体素工具是否并行。小笔刷单线程延迟更低，128 体素的大笔刷必须并行。 */
	UPROPERTY(EditAnywhere, Category = "PlanetEdit")
	bool bMultiThreadedEdits = false;

private:
	bool ApplyBrushAtScreenPosition(const FVector2D& ScreenPosition, bool bPlace);
	AVoxelPlanetActor* ResolvePlanet() const;

	/**
	 * 屏幕位置 → 行星表面命中点。挖 / 放 / 预览共用同一条射线路径。
	 *
	 * 先打 Visibility；只有它**完全没有命中**时才补 WorldStatic（体素碰撞的
	 * presets 由关卡决定，可能只在其中一条通道上响应）。两条通道的命中都要求
	 * Actor 是当前行星，飞船 / 角色 / 其它 Actor 都能挡住编辑与预览。
	 */
	bool TraceEditTarget(const FVector2D& ScreenPosition, FHitResult& OutHit) const;

	/** 每帧按鼠标位置重建线框。没在编辑 / 没有鼠标 / 没打到行星时清空。 */
	void UpdateBrushPreview();

	/** 懒创建预览用的 LineBatchComponent，挂在角色根组件下。 */
	void EnsurePreviewLineBatch();

	/** 清掉上一次画的线。 */
	void HideBrushPreview();

	/** 预览线框。Transient：运行时重建，不参与序列化。 */
	UPROPERTY(Transient)
	TObjectPtr<ULineBatchComponent> PreviewLineBatch;

	/** 右键按住 = 预览放，否则预览挖。 */
	bool bPreviewPlacing = false;

	/** 是否在编辑模式。不进反射：这是运行时状态，不是可配置项。 */
	bool bEditMode = false;

	UPROPERTY(EditAnywhere, Category = "PlanetEdit")
	EPlanetBrushShape BrushShape = EPlanetBrushShape::Sphere;

	/** 直径 / 边长（体素数）。 */
	UPROPERTY(EditAnywhere, Category = "PlanetEdit", meta = (ClampMin = "1"))
	int32 BrushSizeVoxels = 2;
};
