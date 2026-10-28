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
class STELLARERAPLANETEDIT_API UPlanetEditComponent : public UActorComponent
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
	 * 配置笔刷尺寸区间与初始尺寸。星图 Pawn 在构造函数里调（8..128，默认 64）；
	 * 角色不调，用成员默认值 1..8。
	 */
	void ConfigureBrushRange(int32 InMinSize, int32 InMaxSize, int32 InitialSize);

	void SetEditMode(bool bEnabled);
	void ToggleEditMode() { SetEditMode(!bEditMode); }

	void SetBrushShape(EPlanetBrushShape NewShape);

	/**
	 * Delta 是**体素数**，不是档数：内部按 `BrushSizeVoxels + Delta` 夹到
	 * [MinBrushSizeVoxels, MaxBrushSizeVoxels]。一次按一档走多少由调用方乘上
	 * GetBrushSizeStepVoxels() —— 角色 1、星图 10。
	 */
	void AdjustBrushSize(int32 Delta);

	/** 一次「调大一档」走多少体素。见 BrushSizeStepVoxels。 */
	int32 GetBrushSizeStepVoxels() const { return BrushSizeStepVoxels; }

	bool DigAtScreenPosition(const FVector2D& ScreenPosition);
	bool PlaceAtScreenPosition(const FVector2D& ScreenPosition);

	/**
	 * 按住连续挖：开 / 关。
	 *
	 * 开启后组件在 Tick 里按 ContinuousDigIntervalSeconds 节流，在当前光标处
	 * 重复调用 DigAtScreenPosition。按下那一下由控制器立即挖，这里不补刀。
	 * 退出编辑模式会自动关闭。
	 */
	void SetContinuousDigging(bool bEnabled);
	bool IsContinuousDigging() const { return bContinuousDigging; }

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

	/** 连续挖的间隔（秒）。太小会把体素工具刷爆，太大手感发涩。 */
	UPROPERTY(EditAnywhere, Category = "PlanetEdit", meta = (ClampMin = "0.01"))
	float ContinuousDigIntervalSeconds = 0.12f;

	/**
	 * 一次「调大一档」走多少体素。角色区间只有 1..8，一档一格；星图区间 8..128，
	 * 一档十格 —— 从 64 按一格一档要按三十几下才到上限，手感等于没反应。
	 * 只影响按键 / 滚轮的步进，不改变区间本身。
	 */
	UPROPERTY(EditAnywhere, Category = "PlanetEdit", meta = (ClampMin = "1"))
	int32 BrushSizeStepVoxels = 1;

	/** HUD 提示行里的「怎么放」。角色「右键点放」，星图「Shift+左键按住连续挖 · Ctrl+左键填」。 */
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

	/** 连续挖是否开启。Tick 只在编辑模式下跑，退出编辑模式时由 SetEditMode 清掉。 */
	bool bContinuousDigging = false;

	/** 距上次连续挖累计的秒数；由 PlanetEditBrush::ShouldDigNow 推进并清零。 */
	float ContinuousDigAccumulatorSeconds = 0.f;

	UPROPERTY(EditAnywhere, Category = "PlanetEdit")
	EPlanetBrushShape BrushShape = EPlanetBrushShape::Sphere;

	/**
	 * 直径 / 边长（体素数）。区间由 Min/Max 决定，BeginPlay 再夹一次。
	 * 这里刻意不写 ClampMax：上限是每个 Pawn 配的，元数据只能写死一个值。
	 */
	UPROPERTY(EditAnywhere, Category = "PlanetEdit", meta = (ClampMin = "1"))
	int32 BrushSizeVoxels = 2;
};
