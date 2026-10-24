// Copyright Voxel Plugin SAS. All Rights Reserved.

#include "PlanetEditComponent.h"

#include "PlanetBrushApplicator.h"
#include "PlanetCharacter.h"
#include "VoxelPlanetActor.h"
#include "CollisionQueryParams.h"
#include "Components/LineBatchComponent.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "SceneTypes.h"
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
	Super::EndPlay(EndPlayReason);
}

AVoxelPlanetActor* UPlanetEditComponent::ResolvePlanet() const
{
	const APlanetCharacter* Character = Cast<APlanetCharacter>(GetOwner());
	return Character ? Character->PlanetActor.Get() : nullptr;
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
	}

	// 缓存的行星指针在拆卸阶段可能已经失效（EndPlay 也会走到这里），先验一下。
	AVoxelPlanetActor* Planet = ResolvePlanet();
	if (IsValid(Planet))
	{
		Planet->SetEditGridVisible(bEditMode);
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
}

void UPlanetEditComponent::SetBrushShape(const EPlanetBrushShape NewShape)
{
	BrushShape = NewShape;
}

void UPlanetEditComponent::AdjustBrushSize(const int32 Delta)
{
	BrushSizeVoxels = PlanetEditBrush::ClampSize(BrushSizeVoxels + Delta);
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
	if (!bEditMode)
	{
		return false;
	}

	AVoxelPlanetActor* Planet = ResolvePlanet();
	FHitResult Hit;
	if (!Planet || !Planet->IsCreated() || !TraceEditTarget(ScreenPosition, Hit))
	{
		return false;
	}

	const float VoxelSize = FMath::Max(Planet->VoxelSize, 1.f);
	const FVector Center = PlanetEditBrush::GetEditCenterWorld(
		Hit.Location, Hit.ImpactNormal, BrushSizeVoxels, VoxelSize, bPlace,
		MinBrushSizeVoxels, MaxBrushSizeVoxels);

	return PlanetBrushApplicator::Apply(*Planet, Center, BrushShape, BrushSizeVoxels,
		MinBrushSizeVoxels, MaxBrushSizeVoxels, bPlace, bMultiThreadedEdits);
}

bool UPlanetEditComponent::TraceEditTarget(const FVector2D& ScreenPosition, FHitResult& OutHit) const
{
	AVoxelPlanetActor* Planet = ResolvePlanet();
	UWorld* World = GetWorld();
	if (!Planet || !Planet->IsCreated() || !World)
	{
		return false;
	}

	const APlayerController* PlayerController = World->GetFirstPlayerController();
	if (!PlayerController)
	{
		return false;
	}

	FVector WorldOrigin = FVector::ZeroVector;
	FVector WorldDirection = FVector::ZeroVector;
	// Deproject 的参数是 float；本项目警告即错误，double→float 必须显式转。
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

	// 实际可达距离受角色体素 invoker 的 CollisionsRange（约 20000 厘米）限制：
	// 更远的地形只画不碰撞，射线会穿过去，点击既不改体素也没有反馈 —— 这是
	// 设计如此，不是丢事件。这里的 1e5 只是射线本身的上限。
	const FVector End = WorldOrigin + WorldDirection * MaxEditDistanceCm;

	// 体素碰撞的 presets 由关卡决定，可能只在 Visibility 或 WorldStatic 之一上响应。
	// 先打 Visibility；只有它**完全没有命中**时才补 WorldStatic —— 一次点击至多
	// 两条射线。两条通道的命中都按「Actor 必须是当前行星」过滤：飞船、角色、
	// 其它 Actor 都能挡住编辑。
	// 注意：Visibility 若先撞上一个挡 Visibility 的非行星 Actor，就不再补
	// WorldStatic —— 否则第二条射线会穿过挡住视线的物体，直接编辑后面的行星。
	FCollisionQueryParams Params(SCENE_QUERY_STAT(PlanetEditBrush), /*bTraceComplex=*/true);
	Params.AddIgnoredActor(GetOwner());

	if (World->LineTraceSingleByChannel(OutHit, WorldOrigin, End, ECC_Visibility, Params))
	{
		return OutHit.GetActor() == Planet;
	}

	return World->LineTraceSingleByChannel(OutHit, WorldOrigin, End, ECC_WorldStatic, Params) &&
		OutHit.GetActor() == Planet;
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

	AVoxelPlanetActor* Planet = ResolvePlanet();
	FHitResult Hit;
	if (!Planet || !Planet->IsCreated() ||
		!TraceEditTarget(FVector2D(MouseX, MouseY), Hit))
	{
		HideBrushPreview();
		return;
	}

	const float VoxelSize = FMath::Max(Planet->VoxelSize, 1.f);
	const FVector Center = PlanetEditBrush::GetEditCenterWorld(
		Hit.Location, Hit.ImpactNormal, BrushSizeVoxels, VoxelSize, bPreviewPlacing);

	TArray<FPlanetEditSegment> Segments;
	if (BrushShape == EPlanetBrushShape::Sphere)
	{
		const float Radius = PlanetEditBrush::GetHalfExtentCm(BrushSizeVoxels, VoxelSize);
		PlanetEditBrush::BuildSpherePreviewSegments(Center, Radius, Segments);
	}
	else
	{
		// 与挖 / 放走完全相同的 MakeBox → MakeVoxelBox，预览框才与真实格子一致。
		const FVoxelVector LocalCenter = Planet->GlobalToLocalFloat(Center);
		const FPlanetEditBox Box = PlanetEditBrush::MakeBox(
			FVector(LocalCenter.X, LocalCenter.Y, LocalCenter.Z), BrushSizeVoxels);
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
