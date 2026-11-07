// Copyright Voxel Plugin SAS. All Rights Reserved.
// 行星编辑组件：模式、笔刷状态、屏幕射线解析、体素工具调用。
// 角色与星图相机共用；「屏幕射线 → 编辑目标」的差别由 Owner 通过 SetTargetResolver 注入。

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

/** 屏幕射线 → 编辑目标；false = 没打到可编辑表面。MaxDistanceCm 由调用方传入，实现方自己尊重。 */
using FPlanetEditTargetResolver = TFunction<bool(
	const FVector& RayOrigin, const FVector& RayDirection, float MaxDistanceCm,
	FPlanetEditTarget& OutTarget)>;

UCLASS(ClassGroup = (StellarEra), meta = (BlueprintSpawnableComponent))
class STELLARERAPLANET_API UPlanetEditComponent : public UActorComponent
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

	/** 装「屏幕射线 → 编辑目标」的解析器；没装时 TryResolveTarget 恒 false，预览与落笔都不会发生。 */
	void SetTargetResolver(FPlanetEditTargetResolver InResolver);

	/** 编辑网格归哪颗行星。角色在 BeginPlay / 换星时更新；星图不调（不该出现贴地网格）。 */
	void SetGridPlanet(AVoxelPlanetActor* Planet);

	/** 配置笔刷尺寸区间与初始尺寸。星图 Pawn 调（8..1280，默认 64）；角色用成员默认值 1..8。 */
	void ConfigureBrushRange(int32 InMinSize, int32 InMaxSize, int32 InitialSize);

	void SetEditMode(bool bEnabled);
	void ToggleEditMode() { SetEditMode(!bEditMode); }

	void SetBrushShape(EPlanetBrushShape NewShape);

	/** Delta 是体素数不是档数：内部按 BrushSizeVoxels + Delta 夹到 [Min, Max]；一档多少由调用方乘 GetBrushSizeStepVoxels()。 */
	void AdjustBrushSize(int32 Delta);

	/** 按档调尺寸：一档 = BrushSizeStepVoxels，Ctrl 粗调再乘 CoarseStepMultiplier。星图一档 10 体素要按 128 下才到顶，粗调是够得着上限的唯一办法。 */
	void AdjustBrushSizeByStep(bool bIncrease, bool bCoarse);

	/** 一次「调大一档」走多少体素。见 BrushSizeStepVoxels。 */
	int32 GetBrushSizeStepVoxels() const { return BrushSizeStepVoxels; }

	bool DigAtScreenPosition(const FVector2D& ScreenPosition);
	bool PlaceAtScreenPosition(const FVector2D& ScreenPosition);

	/** 按住连续挖的开 / 关：开启后 Tick 按 ContinuousDigIntervalSeconds 节流重复挖（按下那一下由控制器立即挖），退出编辑模式自动关闭。 */
	void SetContinuousDigging(bool bEnabled);
	bool IsContinuousDigging() const { return bContinuousDigging; }

	/** 屏幕位置 → 编辑目标，只解析不写体素。控制器用它判断点击该不该归编辑管（失败交回星图天体选择）；预览也走这条。 */
	bool TryResolveTarget(const FVector2D& ScreenPosition, FPlanetEditTarget& OutTarget) const;

	void SetPreviewPlacing(bool bPlacing);

	/** 射线最远距离（厘米）。角色默认 1000 米；星图设 50 AU。 */
	UPROPERTY(EditAnywhere, Category = "PlanetEdit", meta = (ClampMin = "100.0"))
	float MaxEditDistanceCm = 1.0e5f;

	/** 笔刷尺寸区间（体素数）。角色 1..8，星图 8..1280。ClampMax 只是滑条上限，真正夹取在 ClampSize；1280 的代价见 MaxStarMapSizeVoxels。 */
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

	/** 连挖期间多久刷一次渲染（秒）。每笔只改数据，范围攒到这里或松手一起刷，否则 0.12 s 一笔就把同一 chunk 重建一遍；0.5 s 是频率与可见性的取舍。 */
	UPROPERTY(EditAnywhere, Category = "PlanetEdit", meta = (ClampMin = "0.05"))
	float ContinuousDigRenderFlushIntervalSeconds = 0.5f;

	/** 一次「调大一档」走多少体素。角色 1、星图 10；只影响按键 / 滚轮步进，不改区间。1280 上限配一档 10 还要靠 Ctrl 粗调。 */
	UPROPERTY(EditAnywhere, Category = "PlanetEdit", meta = (ClampMin = "1"))
	int32 BrushSizeStepVoxels = 1;

	/** HUD 提示行里的「怎么放」。角色「右键点放」，星图「Shift+左键按住连续挖 · Ctrl+左键填」。 */
	UPROPERTY(EditAnywhere, Category = "PlanetEdit")
	FString PlaceActionHint = TEXT("右键点放");

	/** 预览线框在屏幕上的目标线宽（像素）。引擎线厚是世界单位，星图远看会细成发丝，故按相机距离每帧换算成世界厚度。 */
	UPROPERTY(EditAnywhere, Category = "PlanetEdit", meta = (ClampMin = "0.5"))
	float PreviewLineThicknessPixels = 2.0f;

	/**
	 * 预览线沿视线朝相机推的深度（预览半径的比例），是深度偏置而非挪位置：投影不变、只有深度变近，
	 * 等价于 DrawTranslucentLine 的 DepthBias（ULineBatchComponent 没暴露）。挖模式赤道线躺在切平面里
	 * 被地形台阶切成一截截；推几个百分点后贴地那圈压在地形前，埋在地下那半仍被挡掉。
	 */
	UPROPERTY(EditAnywhere, Category = "PlanetEdit", meta = (ClampMin = "0.0", ClampMax = "0.2"))
	float PreviewDepthBiasFraction = 0.02f;

	/** 渐进编辑的块边长（体素），128³ ≈ 2.1e6 格。包围盒每轴 ≤ 块边长的笔刷仍走一步做完的同步路径，切出来只有一块，排队白绕。 */
	UPROPERTY(EditAnywhere, Category = "PlanetEdit", meta = (ClampMin = "8"))
	int32 ProgressiveEditChunkSizeVoxels = 128;

	/** 每帧最多花多少毫秒推进编辑；调大 = 更快看到结果、帧率更差。每帧至少推进一块（第一块不看预算），否则预算再小也永远不动。 */
	UPROPERTY(EditAnywhere, Category = "PlanetEdit", meta = (ClampMin = "0.1"))
	float ProgressiveEditBudgetMs = 4.f;

	/** 队首独占的预算份额（0~1），剩下留给其他活跃笔轮转；只有一笔时整份都归队首。默认 0.7：多笔时队首拿大头，别的笔不饿死。 */
	UPROPERTY(EditAnywhere, Category = "PlanetEdit", meta = (ClampMin = "0", ClampMax = "1"))
	float ProgressiveEditHeadBudgetShare = 0.7f;

	/** 一笔编辑最多合并多少次落笔。合并的 job 每格要按顺序过 op，上限防「一格里过几十个球」；到上限后新的点击另起一笔。 */
	UPROPERTY(EditAnywhere, Category = "PlanetEdit", meta = (ClampMin = "1"))
	int32 MaxMergedOps = 8;

	/** 待处理编辑最高笔数。超了丢下标 1 的：队首正在做不能丢，最新的比中间的有意义。 */
	UPROPERTY(EditAnywhere, Category = "PlanetEdit", meta = (ClampMin = "1"))
	int32 MaxQueuedEdits = 16;

private:
	bool ApplyBrushAtScreenPosition(const FVector2D& ScreenPosition, bool bPlace);
	void UpdateBrushPreview();
	void EnsurePreviewLineBatch();
	void HideBrushPreview();

	/** 每帧推进队列：多笔时队首先吃 HeadBudgetShare，余量按 NextFairEditIndex 在 1..N-1 轮转；只有一笔时队首独占。累计超过 ProgressiveEditBudgetMs 就停。 */
	void TickProgressiveEdits();

	/** 一笔编辑做完：真的改到过体素才记到 LastEditedPlanet 并通知重建 / 存档。 */
	void OnEditFinished(FPlanetProgressiveEdit& Edit);

	/** 把队列里所有编辑一次做完并刷渲染。退出编辑模式 / EndPlay / 换星前必须调，且要在存档之前 —— 之后 Tick 不再推进。 */
	void FinishAllEdits();

	/** 把 Bounds（属于 Planet）并进待刷范围；换星时先刷掉旧星的范围再记新星。 */
	void AccumulateRenderBounds(AVoxelPlanetActor* Planet, const FPlanetEditVoxelBox& Bounds);

	/** 把连挖期间攒下的范围一次性交给渲染，然后清空。 */
	void FlushPendingRenderUpdate();

	FPlanetEditTargetResolver TargetResolver;
	TWeakObjectPtr<AVoxelPlanetActor> GridPlanet;

	/** 还没做完的编辑，下标 0 是正在推进的那一笔。 */
	TArray<TSharedPtr<FPlanetProgressiveEdit>> EditQueue;

	/** 余量轮转的游标（跨帧保留）。INDEX_NONE = 还没开始轮转。 */
	int32 NextFairEditIndex = INDEX_NONE;

	/** 连挖期间攒下、还没刷给渲染的范围（体素空间，右端开）。无效 = 没有待刷新的东西。 */
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

	/** 直径 / 边长（体素数）。区间由 Min/Max 决定，BeginPlay 再夹一次；这里刻意不写 ClampMax —— 上限每个 Pawn 不同。 */
	UPROPERTY(EditAnywhere, Category = "PlanetEdit", meta = (ClampMin = "1"))
	int32 BrushSizeVoxels = 2;
};
