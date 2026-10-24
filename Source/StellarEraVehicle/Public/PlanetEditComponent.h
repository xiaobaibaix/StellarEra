// Copyright Voxel Plugin SAS. All Rights Reserved.
// 行星编辑组件：模式、笔刷状态、屏幕射线解析、体素工具调用。
//
// 角色与星图相机共用本组件。两者唯一的差别是「怎么把屏幕射线变成编辑目标」：
// 角色离地表几米，用 LineTrace；星图离地表几千公里、碰撞体根本够不着，得用
// 解析球面 + 体素行进。这个差别由 Owner 通过 SetTargetResolver 注入。

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "PlanetEditBrush.h"
#include "Templates/Function.h"
#include "PlanetEditComponent.generated.h"

class AVoxelPlanetActor;
class ULineBatchComponent;

/** 一次屏幕射线解析出的编辑目标。全部是世界空间。 */
struct FPlanetEditTarget
{
	AVoxelPlanetActor* Planet = nullptr;
	FVector HitLocation = FVector::ZeroVector;
	FVector HitNormal = FVector::UpVector;
};

/**
 * 屏幕射线 → 编辑目标。
 *
 * 返回 false 表示没打到可编辑表面。实现方负责自己尊重 MaxDistanceCm ——
 * 调用方把「最远打多远」原样传进来，不会替实现方截断射线。
 */
using FPlanetEditTargetResolver = TFunction<bool(
	const FVector& RayOrigin, const FVector& RayDirection, float MaxDistanceCm,
	FPlanetEditTarget& OutTarget)>;

UCLASS(ClassGroup = (StellarEra), meta = (BlueprintSpawnableComponent))
class STELLARERAVEHICLE_API UPlanetEditComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UPlanetEditComponent();

	//~ Begin UActorComponent Interface
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;
	//~ End UActorComponent Interface

	bool IsEditMode() const { return bEditMode; }
	EPlanetBrushShape GetBrushShape() const { return BrushShape; }
	int32 GetBrushSizeVoxels() const { return BrushSizeVoxels; }
	int32 GetMinBrushSizeVoxels() const { return MinBrushSizeVoxels; }
	int32 GetMaxBrushSizeVoxels() const { return MaxBrushSizeVoxels; }
	const FString& GetPlaceActionHint() const { return PlaceActionHint; }

	/**
	 * 装「屏幕射线 → 编辑目标」的解析器。由 Owner 在 BeginPlay 里调一次。
	 *
	 * 没装时 TryResolveTarget 恒 false：预览与落笔都不会发生。
	 */
	void SetTargetResolver(FPlanetEditTargetResolver InResolver);

	/**
	 * 编辑网格归哪颗行星。角色在 BeginPlay / 换星时更新；
	 * 星图从不调用 —— 星图里不该出现贴着地表的网格。
	 */
	void SetGridPlanet(AVoxelPlanetActor* Planet);

	/**
	 * 配置笔刷尺寸区间与初始尺寸。星图 Pawn 在构造函数里调（8..128，默认 32）；
	 * 角色不调，用成员默认值 1..8。
	 */
	void ConfigureBrushRange(int32 InMinSize, int32 InMaxSize, int32 InitialSize);

	void SetEditMode(bool bEnabled);
	void ToggleEditMode() { SetEditMode(!bEditMode); }

	void SetBrushShape(EPlanetBrushShape NewShape);

	/** ±1（滚轮 / 方括号）；内部夹到 [MinBrushSizeVoxels, MaxBrushSizeVoxels]。 */
	void AdjustBrushSize(int32 Delta);

	bool DigAtScreenPosition(const FVector2D& ScreenPosition);
	bool PlaceAtScreenPosition(const FVector2D& ScreenPosition);

	/**
	 * 屏幕位置 → 编辑目标。只解析，不写体素。
	 *
	 * 控制器用它判断「这次点击该不该归编辑管」：解析成功就消费掉这次点击，
	 * 失败才交回给星图的天体选择。预览也走同一条路。
	 */
	bool TryResolveTarget(const FVector2D& ScreenPosition, FPlanetEditTarget& OutTarget) const;

	void SetPreviewPlacing(bool bPlacing);

	/** 射线最远距离（厘米）。角色默认 1000 米；星图设 50 AU。 */
	UPROPERTY(EditAnywhere, Category = "PlanetEdit", meta = (ClampMin = "100.0"))
	float MaxEditDistanceCm = 1.0e5f;

	/** 笔刷尺寸区间（体素数，直径 / 边长）。角色 1..8，星图 8..128。 */
	UPROPERTY(EditAnywhere, Category = "PlanetEdit", meta = (ClampMin = "1", ClampMax = "128"))
	int32 MinBrushSizeVoxels = 1;

	UPROPERTY(EditAnywhere, Category = "PlanetEdit", meta = (ClampMin = "1", ClampMax = "128"))
	int32 MaxBrushSizeVoxels = 8;

	/** 体素工具是否并行。小笔刷单线程延迟更低，128 体素的大笔刷必须并行。 */
	UPROPERTY(EditAnywhere, Category = "PlanetEdit")
	bool bMultiThreadedEdits = false;

	/** HUD 提示行里的「怎么放」。角色「右键点放」，星图「Shift+左键填」。 */
	UPROPERTY(EditAnywhere, Category = "PlanetEdit")
	FString PlaceActionHint = TEXT("右键点放");

private:
	bool ApplyBrushAtScreenPosition(const FVector2D& ScreenPosition, bool bPlace);
	void UpdateBrushPreview();
	void EnsurePreviewLineBatch();
	void HideBrushPreview();

	FPlanetEditTargetResolver TargetResolver;
	TWeakObjectPtr<AVoxelPlanetActor> GridPlanet;

	UPROPERTY(Transient)
	TObjectPtr<ULineBatchComponent> PreviewLineBatch;

	bool bPreviewPlacing = false;
	bool bEditMode = false;

	UPROPERTY(EditAnywhere, Category = "PlanetEdit")
	EPlanetBrushShape BrushShape = EPlanetBrushShape::Sphere;

	/**
	 * 直径 / 边长（体素数）。区间由 Min/Max 决定，BeginPlay 再夹一次。
	 * 这里刻意不写 ClampMax：上限是每个 Pawn 配的，元数据只能写死一个值。
	 */
	UPROPERTY(EditAnywhere, Category = "PlanetEdit", meta = (ClampMin = "1"))
	int32 BrushSizeVoxels = 2;
};
