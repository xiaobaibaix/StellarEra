// Copyright Voxel Plugin SAS. All Rights Reserved.

#include "PlanetEditComponent.h"

#include "PlanetBrushApplicator.h"
#include "VoxelPlanetActor.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/LineBatchComponent.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "VoxelIntBox.h"
#include "VoxelRender/IVoxelLODManager.h"
#include "VoxelVector.h"

UPlanetEditComponent::UPlanetEditComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	// 预览只在编辑模式里需要；SetEditMode 打开模式时才启用，平时不占每帧。
	PrimaryComponentTick.bStartWithTickEnabled = false;

	// 与 UPlanetSurfaceGridComponent 同理：排进 TG_PostUpdateWork，等 N 体子系统搬完行星
	// 再读位置，预览线框不会比星球慢一帧。
	PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
}

void UPlanetEditComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// 队列里没做完的编辑必须在这里收尾：Tick 一停就没人推进，留着是半成品 + 没存档。
	FinishAllEdits();

	// 角色被销毁时把网格收掉，否则切换 Pawn 的路径漏了退出就会留下网格。
	if (bEditMode)
	{
		SetEditMode(false);
	}

	// 解析器通常捕获 Owner 自己；Owner 正在销毁，留着就是悬空引用。
	TargetResolver = nullptr;
	GridPlanet.Reset();
	EditQueue.Reset();
	PendingRenderPlanet.Reset();
	PendingRenderBounds = FPlanetEditVoxelBox();
	LastEditedPlanet.Reset();

	Super::EndPlay(EndPlayReason);
}

void UPlanetEditComponent::BeginPlay()
{
	Super::BeginPlay();

	// BrushSizeVoxels 是 EditAnywhere：蓝图或关卡实例里可能存着区间外的旧值，进游戏时夹一次。
	BrushSizeVoxels = PlanetEditBrush::ClampSize(
		BrushSizeVoxels, MinBrushSizeVoxels, MaxBrushSizeVoxels);
}

void UPlanetEditComponent::SetTargetResolver(FPlanetEditTargetResolver InResolver)
{
	TargetResolver = MoveTemp(InResolver);
}

void UPlanetEditComponent::SetGridPlanet(AVoxelPlanetActor* Planet)
{
	if (GridPlanet.Get() == Planet)
	{
		return;
	}

	// 换星前把队列做干净：编辑记着旧星的体素坐标与 Actor，继续推进就是拿旧坐标写新星。
	FinishAllEdits();

	// 换星先收旧星网格再决定新星要不要亮；角色调它，星图不调。
	if (bEditMode)
	{
		if (AVoxelPlanetActor* Previous = GridPlanet.Get())
		{
			if (IsValid(Previous))
			{
				Previous->SetEditGridVisible(false);
			}
		}
		if (IsValid(Planet))
		{
			Planet->SetEditGridVisible(true);
		}
	}

	GridPlanet = Planet;
}

void UPlanetEditComponent::ConfigureBrushRange(
	const int32 InMinSize, const int32 InMaxSize, const int32 InitialSize)
{
	MinBrushSizeVoxels = FMath::Max(InMinSize, 1);
	MaxBrushSizeVoxels = FMath::Max(InMaxSize, MinBrushSizeVoxels);
	BrushSizeVoxels = PlanetEditBrush::ClampSize(
		InitialSize, MinBrushSizeVoxels, MaxBrushSizeVoxels);
}

void UPlanetEditComponent::SetEditMode(const bool bEnabled)
{
	if (bEditMode == bEnabled)
	{
		return;
	}

	bEditMode = bEnabled;

	// 预览随编辑模式开关：退出时停 Tick 并擦掉上一次画的线，否则线框会留在原地。
	SetComponentTickEnabled(bEditMode);
	if (!bEditMode)
	{
		bPreviewPlacing = false;
		HideBrushPreview();

		// Tick 马上要停，队列里没做完的编辑在这里一次做完并刷渲染。
		// 必须在 RequestSaveEdits 之前：半成品不该被存下去。
		FinishAllEdits();
		// 退出编辑就清连挖：标志留着会让下次进编辑模式直接开挖，也会让星图一直以为在连挖。
		SetContinuousDigging(false);

		// 退出编辑模式才存档。整星 GetSave + 压缩实测 0.29–1.92 s（随编辑量增长），
		// 收工时只付一次，且 GetSave / 压缩在后台线程跑（见 AVoxelPlanetActor::RequestSaveEdits）。
		if (AVoxelPlanetActor* Edited = LastEditedPlanet.Get())
		{
			if (IsValid(Edited))
			{
				Edited->RequestSaveEdits();
			}
		}
		LastEditedPlanet.Reset();
	}

	// 网格归属由 SetGridPlanet 决定；缓存的行星在拆卸阶段可能已失效，先验一下。
	if (AVoxelPlanetActor* Planet = GridPlanet.Get())
	{
		if (IsValid(Planet))
		{
			Planet->SetEditGridVisible(bEditMode);
		}
	}
}

void UPlanetEditComponent::FlushPendingRenderUpdate()
{
	PendingRenderAgeSeconds = 0.f;

	AVoxelPlanetActor* const Planet = PendingRenderPlanet.Get();
	const FPlanetEditVoxelBox Bounds = PendingRenderBounds;
	PendingRenderBounds = FPlanetEditVoxelBox();

	// IsValid 只说明 Actor 还活着，不说明体素世界还在：AVoxelWorld::EndPlay 会 DestroyWorld
	// 并置空 LODManager。PIE 拆除时行星 EndPlay 常先于本组件，此时 GetLODManager() 就是空指针，
	// 必须在这里挡住，与 FPlanetProgressiveEdit 的 IsCreated 守卫同一判据。
	if (!IsValid(Planet) || !Planet->IsCreated() || !PlanetEditBrush::IsValidVoxelBox(Bounds))
	{
		return;
	}

	// 与体素工具内部同一入口（LODManager.UpdateBounds）；插件没有「攒着稍后刷」的机制，
	// 并集必须由本组件自己记。
	Planet->GetLODManager().UpdateBounds(FVoxelIntBox(Bounds.Min, Bounds.MaxExclusive));
}

void UPlanetEditComponent::AccumulateRenderBounds(
	AVoxelPlanetActor* const Planet, const FPlanetEditVoxelBox& Bounds)
{
	if (!IsValid(Planet) || !PlanetEditBrush::IsValidVoxelBox(Bounds))
	{
		return;
	}

	// 换了一颗行星：旧范围的坐标属于旧星体素空间，不能混。先刷掉旧的再换 —— 直接清空会
	// 丢掉旧星还等着刷的范围，那片网格就永远停在旧数据上。
	if (PendingRenderPlanet.Get() != Planet)
	{
		FlushPendingRenderUpdate();
		PendingRenderPlanet = Planet;
	}

	PendingRenderBounds = PlanetEditBrush::UnionVoxelBox(PendingRenderBounds, Bounds);
}

void UPlanetEditComponent::TickProgressiveEdits()
{
	if (EditQueue.Num() == 0)
	{
		NextFairEditIndex = INDEX_NONE;
		return;
	}

	const double StartSeconds = FPlatformTime::Seconds();
	const double BudgetMs = FMath::Max(static_cast<double>(ProgressiveEditBudgetMs), 0.1);
	const double Share = FMath::Clamp(
		static_cast<double>(ProgressiveEditHeadBudgetShare), 0.0, 1.0);
	// 只有一笔活跃时没有轮转对象，整份预算都给队首，否则单笔挖掘会比旧实现慢。
	const double HeadBudgetMs =
		EditQueue.Num() > 1 ? BudgetMs * Share : BudgetMs;

	// 取「已用毫秒」：两个阶段用同一把尺子，预算才好分配。
	auto ElapsedMs = [StartSeconds]()
	{
		return (FPlatformTime::Seconds() - StartSeconds) * 1000.0;
	};

	bool bAppliedChunk = false;

	// --- 阶段一：队首优先 ---
	// 预算检查放在推进过一块之后：份额再小这一帧也要走一块，否则队列永远推不动。收尾块
	// 之后同样检查，避免多笔同时接近完成时一帧连环推完、突破预算。
	while (EditQueue.Num() > 0)
	{
		const TSharedPtr<FPlanetProgressiveEdit>& Edit = EditQueue[0];
		if (!Edit.IsValid())
		{
			// Setup 失败时不会入队，这里只是防御：坏掉的项不能堵住队头。
			EditQueue.RemoveAt(0);
			continue;
		}

		const bool bMoreWork = Edit->ApplyNextChunk();
		bAppliedChunk = true;

		// 只并刚写完的那一块：整笔范围在没做完时刷，会重建还没改的 chunk。
		AccumulateRenderBounds(Edit->GetPlanet().Get(), Edit->GetLastChunkBounds());

		if (!bMoreWork)
		{
			OnEditFinished(*Edit);
			EditQueue.RemoveAt(0);
			if (ElapsedMs() >= HeadBudgetMs)
			{
				break;
			}
			continue;
		}

		if (ElapsedMs() >= HeadBudgetMs)
		{
			break;
		}
	}

	// --- 阶段二：余量轮转 ---
	// 循环只受**剩余预算**约束，不是「每帧每笔一块」的硬保证：块很便宜时
	//（大包围盒的零角块、薄盒）游标会绕回来，同一笔可能在本帧被推进两次。
	// 最坏只是多一块的超出量。不做「再补一块」的补偿：块粒度 128³ 约 2.9 ms，
	// 补一块就会把单帧顶到 9 ms 以上。
	while (PlanetEditSchedule::GetFairCandidateCount(EditQueue.Num()) > 0)
	{
		if (ElapsedMs() >= BudgetMs)
		{
			break;
		}

		NextFairEditIndex = PlanetEditSchedule::GetNextFairIndex(
			NextFairEditIndex, EditQueue.Num());
		if (NextFairEditIndex == INDEX_NONE)
		{
			break;
		}

		const TSharedPtr<FPlanetProgressiveEdit>& Edit = EditQueue[NextFairEditIndex];
		if (!Edit.IsValid())
		{
			EditQueue.RemoveAt(NextFairEditIndex);
			continue;
		}

		const bool bMoreWork = Edit->ApplyNextChunk();
		bAppliedChunk = true;

		AccumulateRenderBounds(Edit->GetPlanet().Get(), Edit->GetLastChunkBounds());

		if (!bMoreWork)
		{
			OnEditFinished(*Edit);
			// 删掉后数组变短：滑进游标格的笔就在游标原下标上。游标始终在 1..N-1 循环、
			// 从不取 0，逐次扫遍这些下标，不会有笔永远轮不到。
			EditQueue.RemoveAt(NextFairEditIndex);
		}
	}

	if (bAppliedChunk)
	{
		// 当帧就刷：洞因此看得见地从光标处向外长，而不是等整笔做完才出现。
		FlushPendingRenderUpdate();
	}
}

void UPlanetEditComponent::OnEditFinished(FPlanetProgressiveEdit& Edit)
{
	// 没改到任何体素的编辑不该让这颗星被记成「编辑过」，否则退出编辑模式时白存一次整星。
	if (!Edit.DidChangeAnything())
	{
		return;
	}

	AVoxelPlanetActor* const Planet = Edit.GetPlanet().Get();
	if (!IsValid(Planet))
	{
		return;
	}

	LastEditedPlanet = Planet;
	Planet->NotifyVoxelEdited();
}

void UPlanetEditComponent::FinishAllEdits()
{
	if (EditQueue.Num() == 0)
	{
		return;
	}

	for (const TSharedPtr<FPlanetProgressiveEdit>& Edit : EditQueue)
	{
		if (!Edit.IsValid())
		{
			continue;
		}

		// ApplyAll 会推进到完成；返回 false 表示行星没了，AppliedBounds 是实际写到的部分。
		Edit->ApplyAll();
		AccumulateRenderBounds(Edit->GetPlanet().Get(), Edit->GetAppliedBounds());
		OnEditFinished(*Edit);
	}

	EditQueue.Reset();
	FlushPendingRenderUpdate();
}

void UPlanetEditComponent::SetPreviewPlacing(const bool bPlacing)
{
	bPreviewPlacing = bPlacing;
}

void UPlanetEditComponent::TickComponent(const float DeltaTime, const ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	UpdateBrushPreview();

	// 渐进编辑不能挂在 bContinuousDigging 下面：单击一次也要逐帧做完。
	TickProgressiveEdits();

	if (!bContinuousDigging)
	{
		return;
	}

	// 连挖期间渲染攒着刷：到间隔刷一次，否则松开鼠标前看不到坑在变大。
	PendingRenderAgeSeconds += DeltaTime;
	if (PendingRenderAgeSeconds >= ContinuousDigRenderFlushIntervalSeconds)
	{
		FlushPendingRenderUpdate();
	}

	if (!PlanetEditBrush::ShouldDigNow(
			ContinuousDigAccumulatorSeconds, DeltaTime, ContinuousDigIntervalSeconds))
	{
		return;
	}

	// 与预览同源：Canvas 只在 PostRender 有效，鼠标位置从控制器查。
	UWorld* World = GetWorld();
	APlayerController* PlayerController = World ? World->GetFirstPlayerController() : nullptr;
	float MouseX = 0.f;
	float MouseY = 0.f;
	if (PlayerController && PlayerController->GetMousePosition(MouseX, MouseY))
	{
		DigAtScreenPosition(FVector2D(MouseX, MouseY));
	}
}

void UPlanetEditComponent::SetBrushShape(const EPlanetBrushShape NewShape)
{
	BrushShape = NewShape;
}

void UPlanetEditComponent::AdjustBrushSize(const int32 Delta)
{
	BrushSizeVoxels = PlanetEditBrush::ClampSize(
		BrushSizeVoxels + Delta, MinBrushSizeVoxels, MaxBrushSizeVoxels);
}

void UPlanetEditComponent::AdjustBrushSizeByStep(const bool bIncrease, const bool bCoarse)
{
	const int32 Step = PlanetEditBrush::ResolveBrushStep(BrushSizeStepVoxels, bCoarse);
	AdjustBrushSize(bIncrease ? Step : -Step);
}

void UPlanetEditComponent::SetContinuousDigging(const bool bEnabled)
{
	// 关的这一刻把连挖期间攒的渲染一次刷掉。
	if (bContinuousDigging && !bEnabled)
	{
		FlushPendingRenderUpdate();
	}

	bContinuousDigging = bEnabled;
	// 开或关都清零：开启时按下那一下已由控制器挖过，关闭时清掉余量。
	ContinuousDigAccumulatorSeconds = 0.f;
}

bool UPlanetEditComponent::DigAtScreenPosition(const FVector2D& ScreenPosition)
{
	return ApplyBrushAtScreenPosition(ScreenPosition, /*bPlace=*/false);
}

bool UPlanetEditComponent::PlaceAtScreenPosition(const FVector2D& ScreenPosition)
{
	return ApplyBrushAtScreenPosition(ScreenPosition, /*bPlace=*/true);
}

bool UPlanetEditComponent::ApplyBrushAtScreenPosition(
	const FVector2D& ScreenPosition,
	const bool bPlace)
{
	FPlanetEditTarget Target;
	if (!TryResolveTarget(ScreenPosition, Target))
	{
		return false;
	}

	const float VoxelSize = FMath::Max(Target.Planet->VoxelSize, 1.f);
	const FVector Center = PlanetEditBrush::GetEditCenterWorld(
		Target.HitLocation, Target.HitNormal, BrushSizeVoxels, VoxelSize, bPlace,
		MinBrushSizeVoxels, MaxBrushSizeVoxels);

	// 小笔刷（角色的全部笔刷都在这一侧）包围盒切出来只有一块，排队白绕。
	// 球包围盒每轴最多 SizeVoxels + 7 格，+1 留余量。
	const bool bFitsOneChunk = BrushSizeVoxels + 8 <= ProgressiveEditChunkSizeVoxels;

	if (bFitsOneChunk)
	{
		// 连挖期间不让工具逐笔刷渲染：0.12 s 一笔就把同一 chunk 重建一遍。
		// 关掉 bUpdateRender，范围攒进 PendingRenderBounds，按间隔或松手一起刷。
		const bool bDeferRender = bContinuousDigging;

		FPlanetEditVoxelBox EditedBounds;
		const bool bEdited = PlanetBrushApplicator::Apply(*Target.Planet, Center, BrushShape,
			BrushSizeVoxels, MinBrushSizeVoxels, MaxBrushSizeVoxels, bPlace, bMultiThreadedEdits,
			&EditedBounds, /*bUpdateRender=*/!bDeferRender);

		if (!bEdited)
		{
			return false;
		}

		// 只有真的改到过体素才算编辑过，否则退出编辑模式时白存一次整星。
		LastEditedPlanet = Target.Planet;

		if (bDeferRender)
		{
			AccumulateRenderBounds(Target.Planet, EditedBounds);
		}

		return true;
	}

	// 先试着并进已有的笔：合进来的落笔只切自己的包围盒，块接在已做前缀之后。省下的
	// 是队列条目而非扫描量（重叠格仍会被两边各写一次）；从队尾往前找，最新优先接收；
	// 这个循环不能动队列。都合不进去才新开一笔。
	for (int32 Index = EditQueue.Num() - 1; Index >= 0; --Index)
	{
		const TSharedPtr<FPlanetProgressiveEdit>& Edit = EditQueue[Index];
		if (!Edit.IsValid())
		{
			continue;
		}

		if (Edit->TryMergeWorld(Target.Planet, Center, BrushShape, BrushSizeVoxels,
				MinBrushSizeVoxels, MaxBrushSizeVoxels, bPlace,
				ProgressiveEditChunkSizeVoxels, bMultiThreadedEdits, MaxMergedOps))
		{
			return true;
		}
	}

	// 大笔刷切成块排队，由 TickProgressiveEdits 每帧推进几块。超上限丢下标 1 的笔：
	// 队首正在做不能丢，最新的比中间的有意义。
	const int32 MaxQueue = FMath::Max(MaxQueuedEdits, 1);
	if (EditQueue.Num() >= MaxQueue)
	{
		if (EditQueue.Num() <= 1)
		{
			return false;
		}
		EditQueue.RemoveAt(1);
	}

	TSharedPtr<FPlanetProgressiveEdit> Edit = MakeShared<FPlanetProgressiveEdit>();
	if (!Edit->Setup(Target.Planet, Center, BrushShape, BrushSizeVoxels, MinBrushSizeVoxels,
			MaxBrushSizeVoxels, bPlace, ProgressiveEditChunkSizeVoxels, bMultiThreadedEdits))
	{
		return false;
	}

	EditQueue.Add(MoveTemp(Edit));

	// 返回值变成「有没有接受这一笔」：体素要几帧才写完，此刻无从知道；
	// 控制器本来就不消费它。
	return true;
}

bool UPlanetEditComponent::TryResolveTarget(
	const FVector2D& ScreenPosition, FPlanetEditTarget& OutTarget) const
{
	if (!bEditMode || !TargetResolver)
	{
		return false;
	}

	const UWorld* World = GetWorld();
	const APlayerController* PlayerController = World ? World->GetFirstPlayerController() : nullptr;
	if (!PlayerController)
	{
		return false;
	}

	FVector WorldOrigin = FVector::ZeroVector;
	FVector WorldDirection = FVector::ZeroVector;
	// Deproject 的参数是 float；本项目警告即错误，double → float 必须显式转。
	if (!PlayerController->DeprojectScreenPositionToWorld(
		static_cast<float>(ScreenPosition.X), static_cast<float>(ScreenPosition.Y),
		WorldOrigin, WorldDirection))
	{
		return false;
	}
	if (WorldDirection.IsNearlyZero())
	{
		return false;
	}

	OutTarget = FPlanetEditTarget();
	if (!TargetResolver(WorldOrigin, WorldDirection, MaxEditDistanceCm, OutTarget))
	{
		return false;
	}

	// 解析器只负责「往哪打」，可编辑性在这里统一把关：行星必须还有体素世界。
	return IsValid(OutTarget.Planet) && OutTarget.Planet->IsCreated();
}

void UPlanetEditComponent::UpdateBrushPreview()
{
	if (!bEditMode)
	{
		HideBrushPreview();
		return;
	}

	UWorld* World = GetWorld();
	APlayerController* PlayerController = World ? World->GetFirstPlayerController() : nullptr;
	float MouseX = 0.f;
	float MouseY = 0.f;
	if (!PlayerController || !PlayerController->GetMousePosition(MouseX, MouseY))
	{
		HideBrushPreview();
		return;
	}

	FPlanetEditTarget Target;
	if (!TryResolveTarget(FVector2D(MouseX, MouseY), Target))
	{
		HideBrushPreview();
		return;
	}

	AVoxelPlanetActor* const Planet = Target.Planet;
	const float VoxelSize = FMath::Max(Planet->VoxelSize, 1.f);
	const FVector Center = PlanetEditBrush::GetEditCenterWorld(
		Target.HitLocation, Target.HitNormal, BrushSizeVoxels, VoxelSize, bPreviewPlacing,
		MinBrushSizeVoxels, MaxBrushSizeVoxels);

	TArray<FPlanetEditSegment> Segments;
	// 预览的尺度（球 = 半径；方块 = 半边长），深度偏置按它取比例。
	float PreviewExtentCm = 0.f;
	if (BrushShape == EPlanetBrushShape::Sphere)
	{
		const float Radius = PlanetEditBrush::GetHalfExtentCm(
			BrushSizeVoxels, VoxelSize, MinBrushSizeVoxels, MaxBrushSizeVoxels);
		PreviewExtentCm = Radius;
		// 极轴用命中点地表法线：中间那条纬线是贴地赤道，相机怎么转都是规整的球。
		PlanetEditBrush::BuildSpherePreviewSegments(
			Center, Target.HitNormal, Radius, Segments);
	}
	else
	{
		// 与挖 / 放走相同的 MakeBox → MakeVoxelBox，预览框才与真实格子一致。
		PreviewExtentCm = PlanetEditBrush::GetHalfExtentCm(
			BrushSizeVoxels, VoxelSize, MinBrushSizeVoxels, MaxBrushSizeVoxels);
		const FVoxelVector LocalCenter = Planet->GlobalToLocalFloat(Center);
		const FPlanetEditBox Box = PlanetEditBrush::MakeBox(
			FVector(LocalCenter.X, LocalCenter.Y, LocalCenter.Z), BrushSizeVoxels,
			MinBrushSizeVoxels, MaxBrushSizeVoxels);
		const FPlanetEditVoxelBox VoxelBox = PlanetEditBrush::MakeVoxelBox(Box);
		PlanetEditBrush::BuildBoxPreviewSegments(VoxelBox.Min, VoxelBox.MaxExclusive, Segments);

		// 盒子的棱生成在体素空间；换回世界空间才能画在星球表面。
		for (FPlanetEditSegment& Segment : Segments)
		{
			const FVoxelVector WorldStart = Planet->LocalToGlobalFloat(FVoxelVector(Segment.Start));
			const FVoxelVector WorldEnd = Planet->LocalToGlobalFloat(FVoxelVector(Segment.End));
			Segment.Start = FVector(WorldStart.X, WorldStart.Y, WorldStart.Z);
			Segment.End = FVector(WorldEnd.X, WorldEnd.Y, WorldEnd.Z);
		}
	}

	EnsurePreviewLineBatch();
	if (!PreviewLineBatch)
	{
		return;
	}

	// 挖：琥珀色；放：青绿色。颜色不同，不看位置也能分清当前是哪种操作。
	const FLinearColor PreviewColor = bPreviewPlacing
		? FLinearColor(0.20f, 0.90f, 0.95f)
		: FLinearColor(1.00f, 0.80f, 0.25f);

	// 线宽按屏幕像素恒定：FBatchedLine 厚度是世界单位，不换算星图远看会细成发丝；
	// 取不到相机信息时退回 1.5 世界厘米。
	constexpr float FallbackThicknessCm = 1.5f;
	float LineThicknessCm = FallbackThicknessCm;
	FVector CameraLocation = FVector::ZeroVector;
	float DepthBiasCm = 0.f;
	if (const APlayerCameraManager* CameraManager = PlayerController->PlayerCameraManager)
	{
		CameraLocation = CameraManager->GetCameraLocation();

		int32 ViewportWidthPx = 0;
		int32 ViewportHeightPx = 0;
		PlayerController->GetViewportSize(ViewportWidthPx, ViewportHeightPx);
		LineThicknessCm = PlanetEditBrush::PreviewLineThicknessWorld(
			static_cast<float>(FVector::Dist(CameraLocation, Center)),
			CameraManager->GetFOVAngle(),
			static_cast<float>(ViewportWidthPx),
			PreviewLineThicknessPixels,
			FallbackThicknessCm);

		if (PreviewDepthBiasFraction > 0.f)
		{
			// 至少一条线宽：比线本身还薄的偏移看不出效果。
			const float BiasFromExtent = PreviewExtentCm * PreviewDepthBiasFraction;

			// 最多推到预览尺寸的 10%（比例本身更大时以它为准）。再往上会把埋在地下那半
			// 也翻上来；星图整星缩放时线宽换算可上千米，没这个上限会越界。
			const float BiasCap = PreviewExtentCm * FMath::Max(PreviewDepthBiasFraction, 0.1f);
			DepthBiasCm = FMath::Min(FMath::Max(BiasFromExtent, LineThicknessCm), BiasCap);
		}
	}

	// 沿「相机 → 点」射线把点拉近 DepthBiasCm：投影位置不动、只有深度变近，是真正的深度
	// 偏置而非挪位置。贴地赤道线与地形共面，不推会被切成一截截；推过后压在地形前，
	// 埋在地下半径量级深的那半仍过不了深度测试。
	const auto PushTowardCamera = [CameraLocation, DepthBiasCm](const FVector& Point) -> FVector
	{
		if (DepthBiasCm <= 0.f)
		{
			return Point;
		}
		const FVector ToCamera = CameraLocation - Point;
		const double Distance = ToCamera.Size();
		if (Distance <= UE_DOUBLE_KINDA_SMALL_NUMBER)
		{
			return Point;
		}
		// 相机几乎贴着点时按比例收（最多推到四分之一距离），不会把点推穿相机。
		const double Push = FMath::Min(static_cast<double>(DepthBiasCm), Distance * 0.25);
		return Point + ToCamera * (Push / Distance);
	};

	TArray<FBatchedLine> Lines;
	Lines.Reserve(Segments.Num());
	for (const FPlanetEditSegment& Segment : Segments)
	{
		// LifeTime < 0 不自动过期；本组件每帧 Flush 重画。
		// 深度组用 World：埋在地下那段本就该被挡；换 Foreground 会让整颗球透出来，等于谎报。
		Lines.Emplace(PushTowardCamera(Segment.Start), PushTowardCamera(Segment.End),
			PreviewColor, -1.f, LineThicknessCm, static_cast<uint8>(SDPG_World));
	}

	PreviewLineBatch->Flush();
	PreviewLineBatch->DrawLines(Lines);
}

void UPlanetEditComponent::EnsurePreviewLineBatch()
{
	if (PreviewLineBatch)
	{
		return;
	}

	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return;
	}

	PreviewLineBatch = NewObject<ULineBatchComponent>(Owner, TEXT("PlanetBrushPreviewLines"));
	PreviewLineBatch->SetupAttachment(Owner->GetRootComponent());
	PreviewLineBatch->RegisterComponent();
}

void UPlanetEditComponent::HideBrushPreview()
{
	if (PreviewLineBatch)
	{
		// 早退也必须清掉上一次提交的线，否则旧线会一直留在画面上。
		PreviewLineBatch->Flush();
	}
}
