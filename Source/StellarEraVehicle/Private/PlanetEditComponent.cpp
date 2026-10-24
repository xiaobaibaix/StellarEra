// Copyright Voxel Plugin SAS. All Rights Reserved.

#include "PlanetEditComponent.h"

#include "PlanetCharacter.h"
#include "VoxelPlanetActor.h"
#include "CollisionQueryParams.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "VoxelIntBox.h"
#include "VoxelTools/Gen/VoxelBoxTools.h"
#include "VoxelTools/Gen/VoxelSphereTools.h"
#include "VoxelVector.h"

UPlanetEditComponent::UPlanetEditComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
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

	// 缓存的行星指针在拆卸阶段可能已经失效（EndPlay 也会走到这里），先验一下。
	AVoxelPlanetActor* Planet = ResolvePlanet();
	if (IsValid(Planet))
	{
		Planet->SetEditGridVisible(bEditMode);
	}
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
	// 设计 §5.3：先打 Visibility；只有它**完全没有命中**时才补 WorldStatic ——
	// 一次点击至多两条射线。两条通道的命中都按「Actor 必须是当前行星」过滤：
	// 飞船、角色、其它 Actor 都能挡住编辑。
	// 注意：Visibility 若先撞上一个挡 Visibility 的非行星 Actor，就不再补
	// WorldStatic —— 否则第二条射线会穿过挡住视线的物体，直接编辑后面的行星。
	FCollisionQueryParams Params(SCENE_QUERY_STAT(PlanetEditBrush), /*bTraceComplex=*/true);
	Params.AddIgnoredActor(GetOwner());

	FHitResult Hit;
	const bool bVisibilityHit = World->LineTraceSingleByChannel(
		Hit, WorldOrigin, End, ECC_Visibility, Params);
	bool bHasHit = bVisibilityHit && Hit.GetActor() == Planet;

	if (!bVisibilityHit)
	{
		bHasHit = World->LineTraceSingleByChannel(
			Hit, WorldOrigin, End, ECC_WorldStatic, Params) &&
			Hit.GetActor() == Planet;
	}
	if (!bHasHit)
	{
		return false;
	}

	const float VoxelSize = FMath::Max(Planet->VoxelSize, 1.f);
	const FVector Center = PlanetEditBrush::GetEditCenterWorld(
		Hit.Location, Hit.ImpactNormal, BrushSizeVoxels, VoxelSize, bPlace);

	// 工具的 OutModifiedValues 只记录**值真的变了**的体素（VoxelDataImpl::Set 里
	// `OldValue != NewValue` 才追加），这正是「是否真的改到了体素」的判据。
	// 插件的 OutEditedBounds 不能用：宏会无条件把它设成请求包围盒，即使在岩石里
	// 放石头这种空操作也会被填上。
	TArray<FModifiedVoxelValue> ModifiedValues;

	if (BrushShape == EPlanetBrushShape::Sphere)
	{
		const float Radius = PlanetEditBrush::GetHalfExtentCm(BrushSizeVoxels, VoxelSize);
		if (bPlace)
		{
			UVoxelSphereTools::AddSphere(Planet, Center, Radius,
				/*OutModifiedValues=*/&ModifiedValues, /*OutEditedBounds=*/nullptr,
				/*bMultiThreaded=*/false, /*bConvertToVoxelSpace=*/true,
				/*bUpdateRender=*/true);
		}
		else
		{
			UVoxelSphereTools::RemoveSphere(Planet, Center, Radius,
				/*OutModifiedValues=*/&ModifiedValues, /*OutEditedBounds=*/nullptr,
				/*bMultiThreaded=*/false, /*bConvertToVoxelSpace=*/true,
				/*bUpdateRender=*/true);
		}
	}
	else
	{
		const FVoxelVector LocalCenter = Planet->GlobalToLocalFloat(Center);
		const FPlanetEditBox Box = PlanetEditBrush::MakeBox(
			FVector(LocalCenter.X, LocalCenter.Y, LocalCenter.Z), BrushSizeVoxels);
		// FPlanetEditBox 的 Min / Max 两端都含（MakeBox 的尺寸就是 Max-Min+1），
		// 而 FVoxelIntBox 的 Max 是开区间（"Max of the box. Exclusive"）。直接透传
		// 会让方块笔刷少一格：尺寸 2 只挖 1 格。换算收在 MakeVoxelBox 里（有单测），
		// 这里拿到的 MaxExclusive 已经加过 1。
		const FPlanetEditVoxelBox VoxelBox = PlanetEditBrush::MakeVoxelBox(Box);
		const FVoxelIntBox Bounds(VoxelBox.Min, VoxelBox.MaxExclusive);

		if (bPlace)
		{
			UVoxelBoxTools::AddBox(Planet, Bounds,
				/*OutModifiedValues=*/&ModifiedValues, /*OutEditedBounds=*/nullptr,
				/*bMultiThreaded=*/false, /*bUpdateRender=*/true);
		}
		else
		{
			UVoxelBoxTools::RemoveBox(Planet, Bounds,
				/*OutModifiedValues=*/&ModifiedValues, /*OutEditedBounds=*/nullptr,
				/*bMultiThreaded=*/false, /*bUpdateRender=*/true);
		}
	}

	// 空操作（在岩石里放石头、挖空气）不重建网格、也不触发保存。
	if (ModifiedValues.Num() == 0)
	{
		return false;
	}

	Planet->NotifyVoxelEdited();
	return true;
}
