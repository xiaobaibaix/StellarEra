// Copyright Voxel Plugin SAS. All Rights Reserved.
// 角色身上的行星编辑组件：模式、笔刷状态、屏幕射线与体素工具调用。

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "PlanetEditBrush.h"
#include "PlanetEditComponent.generated.h"

class APlanetCharacter;
class AVoxelPlanetActor;

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

	/** 射线最远距离（厘米）。默认 1000 米：足够覆盖任意可瞄准的表面。 */
	UPROPERTY(EditAnywhere, Category = "PlanetEdit", meta = (ClampMin = "100.0"))
	float MaxEditDistanceCm = 1.0e5f;

private:
	bool ApplyBrushAtScreenPosition(const FVector2D& ScreenPosition, bool bPlace);
	AVoxelPlanetActor* ResolvePlanet() const;

	/** 是否在编辑模式。不进反射：这是运行时状态，不是可配置项。 */
	bool bEditMode = false;

	UPROPERTY(EditAnywhere, Category = "PlanetEdit")
	EPlanetBrushShape BrushShape = EPlanetBrushShape::Sphere;

	/** 直径 / 边长（体素数）。 */
	UPROPERTY(EditAnywhere, Category = "PlanetEdit", meta = (ClampMin = "1", ClampMax = "8"))
	int32 BrushSizeVoxels = 2;
};
