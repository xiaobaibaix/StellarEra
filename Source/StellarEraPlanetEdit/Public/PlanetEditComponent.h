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
#include "PlanetProgressiveEdit.h"
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
	 * 配置笔刷尺寸区间与初始尺寸。星图 Pawn 在构造函数里调（8..1280，默认 64）；
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

	/**
	 * 按「档」调尺寸：一档 = BrushSizeStepVoxels，按住 Ctrl 粗调再乘
	 * PlanetEditBrush::CoarseStepMultiplier。
	 *
	 * 星图区间 8..1280，一档 10 体素从默认 64 走到上限要按 128 下 —— 粗调是
	 * 让这个上限够得着的唯一办法。角色区间只有 1..8，粗调对它是空转。
	 */
	void AdjustBrushSizeByStep(bool bIncrease, bool bCoarse);

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

	/**
	 * 笔刷尺寸区间（体素数，直径 / 边长）。角色 1..8，星图 8..1280。
	 *
	 * ClampMax 只是面板上的滑条上限，真正的夹取在 PlanetEditBrush::ClampSize。
	 * 星图那套 1280 的代价是 Size³，见 PlanetEditBrush::MaxStarMapSizeVoxels。
	 */
	UPROPERTY(EditAnywhere, Category = "PlanetEdit", meta = (ClampMin = "1", ClampMax = "1280"))
	int32 MinBrushSizeVoxels = 1;

	UPROPERTY(EditAnywhere, Category = "PlanetEdit", meta = (ClampMin = "1", ClampMax = "1280"))
	int32 MaxBrushSizeVoxels = 8;

	/** 体素工具是否并行。小笔刷单线程延迟更低，128 体素的大笔刷必须并行。 */
	UPROPERTY(EditAnywhere, Category = "PlanetEdit")
	bool bMultiThreadedEdits = false;

	/** 连续挖的间隔（秒）。太小会把体素工具刷爆，太大手感发涩。 */
	UPROPERTY(EditAnywhere, Category = "PlanetEdit", meta = (ClampMin = "0.01"))
	float ContinuousDigIntervalSeconds = 0.12f;

	/**
	 * 连续挖期间多久刷一次渲染（秒）。
	 *
	 * 连挖时每一笔都只改体素数据、不刷渲染，改到的范围攒起来，到这里或松手时
	 * 用并集刷一次 —— 否则 0.12 s 一笔就把同一个 chunk 重建一遍，纯浪费。
	 * 更短会反复重建，更长就看不见坑在变大；0.5 s 是这两头之间的取舍。
	 */
	UPROPERTY(EditAnywhere, Category = "PlanetEdit", meta = (ClampMin = "0.05"))
	float ContinuousDigRenderFlushIntervalSeconds = 0.5f;

	/**
	 * 一次「调大一档」走多少体素。角色区间只有 1..8，一档一格；星图区间 8..1280，
	 * 一档十格 —— 从 64 按一格一档要按三十几下才到上限，手感等于没反应。
	 * 只影响按键 / 滚轮的步进，不改变区间本身。
	 *
	 * 1280 上限配一档十格要按 128 下，所以还有 Ctrl 粗调（×10）。
	 */
	UPROPERTY(EditAnywhere, Category = "PlanetEdit", meta = (ClampMin = "1"))
	int32 BrushSizeStepVoxels = 1;

	/** HUD 提示行里的「怎么放」。角色「右键点放」，星图「Shift+左键按住连续挖 · Ctrl+左键填」。 */
	UPROPERTY(EditAnywhere, Category = "PlanetEdit")
	FString PlaceActionHint = TEXT("右键点放");

	/**
	 * 预览线框**在屏幕上**的目标线宽（像素）。
	 *
	 * 引擎的线厚是世界单位，星图远看整颗星球时会细成发丝；这里按相机距离每帧
	 * 换算成世界厚度（PlanetEditBrush::PreviewLineThicknessWorld），所以不管
	 * 相机离星球多远，屏幕上看到的都一样粗。
	 */
	UPROPERTY(EditAnywhere, Category = "PlanetEdit", meta = (ClampMin = "0.5"))
	float PreviewLineThicknessPixels = 2.0f;

	/**
	 * 渐进编辑的块边长（体素）。128³ ≈ 2.1e6 格，16 线程下约 3 ms。
	 *
	 * 比它小的笔刷（包围盒每轴 ≤ 块边长）仍走一步做完的同步路径：切出来只有
	 * 一块，排队只是白绕一圈。角色的全部笔刷（1..8）都在这一侧。
	 */
	UPROPERTY(EditAnywhere, Category = "PlanetEdit", meta = (ClampMin = "8"))
	int32 ProgressiveEditChunkSizeVoxels = 128;

	/**
	 * 每帧最多花多少毫秒推进编辑。4 ms 在 60 fps 下约把一帧从 16.7 ms 抬到
	 * 20 ms，手感仍是流畅的；调大 = 更快看到结果、帧率更差。
	 *
	 * 每帧**至少**推进一块（第一块不看预算），否则预算再小也永远不动。
	 */
	UPROPERTY(EditAnywhere, Category = "PlanetEdit", meta = (ClampMin = "0.1"))
	float ProgressiveEditBudgetMs = 4.f;

	/**
	 * 待处理编辑最高笔数。超了丢下标 1 的：队首正在做不能丢，
	 * 而最新那一笔永远比排在中间的最有意义。
	 */
	UPROPERTY(EditAnywhere, Category = "PlanetEdit", meta = (ClampMin = "1"))
	int32 MaxQueuedEdits = 16;

private:
	bool ApplyBrushAtScreenPosition(const FVector2D& ScreenPosition, bool bPlace);
	void UpdateBrushPreview();
	void EnsurePreviewLineBatch();
	void HideBrushPreview();

	/** 每帧推进队首编辑，累计超过 ProgressiveEditBudgetMs 就停。 */
	void TickProgressiveEdits();

	/** 一笔编辑做完：真的改到过体素才记到 LastEditedPlanet 并通知重建 / 存档。 */
	void OnEditFinished(FPlanetProgressiveEdit& Edit);

	/**
	 * 把队列里所有编辑一次做完并刷渲染。
	 *
	 * 退出编辑模式 / EndPlay / 换星前必须调：这三处之后 Tick 不再推进队列，
	 * 留着就是「挖了一半的坑」+ 没存档。它们也都要在存档之前调 ——
	 * 没做完的编辑不该被 RequestSaveEdits 存成一个半成品。
	 */
	void FinishAllEdits();

	/** 把 Bounds（属于 Planet）并进待刷范围，换星时丢掉旧星的坐标。 */
	void AccumulateRenderBounds(AVoxelPlanetActor* Planet, const FPlanetEditVoxelBox& Bounds);

	/** 把连挖期间攒下的范围一次性交给渲染，然后清空。 */
	void FlushPendingRenderUpdate();

	FPlanetEditTargetResolver TargetResolver;
	TWeakObjectPtr<AVoxelPlanetActor> GridPlanet;

	/** 还没做完的编辑，下标 0 是正在推进的那一笔。 */
	TArray<TSharedPtr<FPlanetProgressiveEdit>> EditQueue;

	/**
	 * 连挖期间攒下的、还没刷给渲染的范围（行星体素空间，Min 含 / MaxExclusive 不含）。
	 * 无效 = 没有待刷新的东西。
	 */
	FPlanetEditVoxelBox PendingRenderBounds;

	/** PendingRenderBounds 属于哪颗行星：换星后旧范围的坐标不能混到新星上。 */
	TWeakObjectPtr<AVoxelPlanetActor> PendingRenderPlanet;

	/** 距上次刷渲染过了多久（秒）。Tick 推进，Flush 清零。 */
	float PendingRenderAgeSeconds = 0.f;

	/** 最后一颗被真正改到过的行星。退出编辑模式时向它请求存档。 */
	TWeakObjectPtr<AVoxelPlanetActor> LastEditedPlanet;

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
