// Copyright Voxel Plugin SAS. All Rights Reserved.

#include "PlanetEditComponent.h"

#include "PlanetBrushApplicator.h"
#include "VoxelPlanetActor.h"
#include "Components/LineBatchComponent.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
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
	// 角色被销毁时把网格收掉，否则切换 Pawn 的路径漏了退出就会留下网格。
	if (bEditMode)
	{
		SetEditMode(false);
	}

	// 解析器通常捕获 Owner 自己；Owner 正在销毁，留着就是悬空引用。
	TargetResolver = nullptr;
	GridPlanet.Reset();

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
		// 退出编辑就清连挖：Tick 虽已停，但标志留着会让下次进编辑模式直接开挖，
		// 也会让星图相机一直以为在连挖。
		SetContinuousDigging(false);
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

void UPlanetEditComponent::SetPreviewPlacing(const bool bPlacing)
{
	bPreviewPlacing = bPlacing;
}

void UPlanetEditComponent::TickComponent(const float DeltaTime, const ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	UpdateBrushPreview();

	if (!bContinuousDigging)
	{
		return;
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

void UPlanetEditComponent::SetContinuousDigging(const bool bEnabled)
{
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

	return PlanetBrushApplicator::Apply(*Target.Planet, Center, BrushShape, BrushSizeVoxels,
		MinBrushSizeVoxels, MaxBrushSizeVoxels, bPlace, bMultiThreadedEdits);
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
	if (BrushShape == EPlanetBrushShape::Sphere)
	{
		const float Radius = PlanetEditBrush::GetHalfExtentCm(
			BrushSizeVoxels, VoxelSize, MinBrushSizeVoxels, MaxBrushSizeVoxels);
		PlanetEditBrush::BuildSpherePreviewSegments(Center, Radius, Segments);
	}
	else
	{
		// 与挖 / 放走完全相同的 MakeBox → MakeVoxelBox，预览框才与真实格子一致
		//（区间取本组件的，星图是 8..128）。
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

	TArray<FBatchedLine> Lines;
	Lines.Reserve(Segments.Num());
	for (const FPlanetEditSegment& Segment : Segments)
	{
		// LifeTime < 0：不自动过期；本组件每帧 Flush + 重画，鼠标一动线框就跟着走。
		Lines.Emplace(Segment.Start, Segment.End, PreviewColor, -1.f, 1.5f,
			static_cast<uint8>(SDPG_World));
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
