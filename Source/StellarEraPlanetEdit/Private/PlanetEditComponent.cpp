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

	// 与 UPlanetSurfaceGridComponent 同理：N 体子系统先搬行星，排进 TG_PostUpdateWork
	// 的组件读到的才是本帧最终位置，预览线框不会比星球慢一帧。
	PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
}

void UPlanetEditComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// 队列里没做完的编辑必须在这里收尾：Tick 一停就再也没人推进它们，
	// 留着就是挖了一半的坑 + 没存档。
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

	// BrushSizeVoxels 是 EditAnywhere：蓝图或关卡实例里可能存着区间外的旧值
	// （例如从 1..8 的组件改成 8..128 的星图 Pawn），进游戏时按区间夹一次。
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

	// 换星前把队列做干净：队列里的编辑记着旧星的体素坐标与 Actor，
	// 换过去之后继续推进就是拿旧坐标写新星（或写已失效的 Actor）。
	FinishAllEdits();

	// 换星时先把旧星上的网格收掉，再决定新星要不要亮。这条路径原先写在
	// APlanetCharacter::ReevaluatePlanet 里，现在收进组件：角色调它，星图不调。
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
		// 必须在下面的 RequestSaveEdits **之前**：半成品不该被存下去。
		FinishAllEdits();
		// 退出编辑就清连挖：Tick 虽已停，但标志留着会让下次进编辑模式直接开挖，
		// 也会让星图相机一直以为在连挖。真要关它会先把手头攒的渲染刷新掉。
		SetContinuousDigging(false);

		// 退出编辑模式才存档。整星 GetSave + 压缩实测 0.29–1.92 s（随累计编辑量
		// 增长），原先每笔编辑都重置一个 2 秒防抖定时器，等于停手 2 秒就卡一次、
		// 而且卡在游戏线程上。现在这笔开销只在收工时付一次，且 GetSave / 压缩
		// 在后台线程跑（见 AVoxelPlanetActor::RequestSaveEdits）。
		if (AVoxelPlanetActor* Edited = LastEditedPlanet.Get())
		{
			if (IsValid(Edited))
			{
				Edited->RequestSaveEdits();
			}
		}
		LastEditedPlanet.Reset();
	}

	// 网格归属由 SetGridPlanet 决定（角色 = 脚下行星，星图 = 空）。
	// 缓存的行星在拆卸阶段可能已经失效，先验一下。
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

	if (!IsValid(Planet) || !PlanetEditBrush::IsValidVoxelBox(Bounds))
	{
		return;
	}

	// 与体素工具内部走的是同一个入口（FVoxelToolHelpers::UpdateWorld →
	// LODManager.UpdateBounds，见 VoxelToolHelpers.cpp:74）。插件自己没有
	// 「先攒着、稍后一起刷」的机制，所以这个并集必须由本组件自己记。
	Planet->GetLODManager().UpdateBounds(FVoxelIntBox(Bounds.Min, Bounds.MaxExclusive));
}

void UPlanetEditComponent::AccumulateRenderBounds(
	AVoxelPlanetActor* const Planet, const FPlanetEditVoxelBox& Bounds)
{
	if (!IsValid(Planet) || !PlanetEditBrush::IsValidVoxelBox(Bounds))
	{
		return;
	}

	// 换了一颗行星：旧范围的坐标属于旧星的体素空间，不能混进来。
	// 轮转下同一帧里可能真的换星，所以必须先把旧的刷掉再换 —— 直接清空会
	// 把旧星还等着刷的范围丢掉，那一片的网格就永远停在旧数据上。
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
	// 只有一笔活跃时没有可以轮转的对象，余量份额无处可去：整份预算都给队首，
	// 否则单笔挖掘会比改前慢（旧实现独占 4 ms）。
	const double HeadBudgetMs =
		EditQueue.Num() > 1 ? BudgetMs * Share : BudgetMs;

	// 取「已用毫秒」：两个阶段用同一把尺子，预算才好分配。
	auto ElapsedMs = [StartSeconds]()
	{
		return (FPlatformTime::Seconds() - StartSeconds) * 1000.0;
	};

	bool bAppliedChunk = false;

	// --- 阶段一：队首优先 ---
	// 预算检查放在**推进过一块之后**：份额再小，这一帧也要往前走一块，
	// 否则队列永远推不动。
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

		// 只并刚写完的那一块：整笔的范围在没做完时刷，会让渲染把还没改的
		// chunk 也重建一遍。
		AccumulateRenderBounds(Edit->GetPlanet().Get(), Edit->GetLastChunkBounds());

		if (!bMoreWork)
		{
			OnEditFinished(*Edit);
			EditQueue.RemoveAt(0);
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
			// 删掉后数组变短：滑进游标那一格的笔此刻就在游标原来的下标上。
			// 下一次 GetNextFairIndex 要么越过它、要么回绕 / 夹回 1，本帧具体
			// 轮到谁取决于这次算术；但不会有笔被永久跳过 —— 游标从不回到 0，
			// 任何一笔最多一圈就会被重新取到。
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
	// 一笔都没改到的编辑（在岩石里放石头、挖空气）不该让这颗星被记成
	// 「编辑过」—— 否则退出编辑模式时会白存一次整星。
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

		// ApplyAll 内部会一直推进到完成；返回 false 表示行星已经没了，
		// 此时 AppliedBounds 就是它实际写到的那一部分。
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

	// 连挖期间渲染是攒着刷的（见 ApplyBrushAtScreenPosition）：到间隔就刷一次，
	// 否则松开鼠标之前看不到坑在变大。
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
	// 松手（以及退出编辑模式）时把连挖期间攒下的渲染一次刷掉。放在改标志之前：
	// Flush 不依赖 bContinuousDigging，但顺序清楚些 —— 关的这一刻就是收工的那一刻。
	if (bContinuousDigging && !bEnabled)
	{
		FlushPendingRenderUpdate();
	}

	bContinuousDigging = bEnabled;
	// 无论开还是关都清零：开启时按下那一下已经由控制器挖过，不该立刻再挖；
	// 关闭时清掉余量，下次开启从干净状态起算。
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

	// 小笔刷（角色的全部笔刷都在这一侧）包围盒切出来只有一块，排队只是白绕。
	// 球包围盒每轴是 ±(半径+3) 再取整：半径 = SizeVoxels/2 + 1 个体素，
	// 于是每轴最多 SizeVoxels + 7 格，+1 留余量。
	const bool bFitsOneChunk = BrushSizeVoxels + 8 <= ProgressiveEditChunkSizeVoxels;

	if (bFitsOneChunk)
	{
		// 连挖期间不让工具逐笔刷渲染：0.12 s 一笔就把同一个 chunk 重建一遍，纯浪费。
		// 关掉 bUpdateRender，把改到的范围攒进 PendingRenderBounds，由
		// FlushPendingRenderUpdate 按间隔或松手时一次性刷。
		const bool bDeferRender = bContinuousDigging;

		FPlanetEditVoxelBox EditedBounds;
		const bool bEdited = PlanetBrushApplicator::Apply(*Target.Planet, Center, BrushShape,
			BrushSizeVoxels, MinBrushSizeVoxels, MaxBrushSizeVoxels, bPlace, bMultiThreadedEdits,
			&EditedBounds, /*bUpdateRender=*/!bDeferRender);

		if (!bEdited)
		{
			return false;
		}

		// 只有真的改到过体素才算「这颗星被编辑过」：在岩石里放石头、挖空气不该
		// 让退出编辑模式时白存一次整星。
		LastEditedPlanet = Target.Planet;

		if (bDeferRender)
		{
			AccumulateRenderBounds(Target.Planet, EditedBounds);
		}

		return true;
	}

	// 大笔刷：切成块排队，由 TickProgressiveEdits 每帧推进几块。
	// 超过上限时丢掉下标 1 的那笔：队首正在做不能丢，而最新的一笔永远比
	// 排在中间的最有意义。
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

	// 返回值语义从「有没有改到体素」变成「有没有接受这一笔」：体素要几帧后
	// 才写得完，此刻无从知道。控制器本来就不消费它（见 StellarEraPlayerController
	// 里「不用它判断归属」的注释）。
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
		// 极轴用命中点的地表法线：中间那条纬线就是贴地的赤道，相机怎么转都是个
		// 规整的球，不会退化成两条斜穿的直线。
		PlanetEditBrush::BuildSpherePreviewSegments(
			Center, Target.HitNormal, Radius, Segments);
	}
	else
	{
		// 与挖 / 放走完全相同的 MakeBox → MakeVoxelBox，预览框才与真实格子一致
		//（区间取本组件的，星图是 8..128）。
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

	// 线宽按**屏幕像素**恒定：FBatchedLine 的厚度是世界单位，不换算的话星图远看
	// 整颗星球时线会细成发丝。取不到相机信息（无 camera manager / 视口宽为 0）时
	// 退回旧的 1.5 世界厘米。
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
			// 至少一条线宽：比线本身还薄的偏移看不出效果（星图的线宽按屏幕像素换算，
			// 离得越远换算出来的世界厚度越大）。
			const float BiasFromExtent = PreviewExtentCm * PreviewDepthBiasFraction;

			// 但最多推到预览尺寸的 10%（比例本身更大的话以它为准）。再往上就不是
			// 「压过与地形共面的那一圈」，而是把埋在地下的那半也一起翻上来了 ——
			// 星图缩到整颗星球时线宽换算会有上千米，没有这个上限就会越界。
			const float BiasCap = PreviewExtentCm * FMath::Max(PreviewDepthBiasFraction, 0.1f);
			DepthBiasCm = FMath::Min(FMath::Max(BiasFromExtent, LineThicknessCm), BiasCap);
		}
	}

	// 沿「相机 → 点」这条射线把点拉近 DepthBiasCm。
	//
	// 投影位置一个像素都不动（同一个视线上），变的只有深度，所以它是货真价实的
	// 深度偏置，而不是把球挪半格。贴地的赤道纬线与地形共面，不推的话深度测试会
	// 沿地形起伏把它切成一截一截；推过之后它稳定压在贴地那一层前面，而埋在地下
	// 半径量级深的那半仍然过不了深度测试。
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
		// LifeTime < 0：不自动过期；本组件每帧 Flush + 重画，鼠标一动线框就跟着走。
		//
		// 深度组用 World：埋在地下的那半段本来就该被地形挡掉。断线由上面的
		// 视线方向深度偏置负责修 —— 换成 Foreground 能让整颗球透出来，但那是
		// 让地下那半压在地表上，等于谎报「挖的是个完整的球」。
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
