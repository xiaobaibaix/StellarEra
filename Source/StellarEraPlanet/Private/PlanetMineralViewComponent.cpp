// Copyright Voxel Plugin SAS. All Rights Reserved.

#include "PlanetMineralViewComponent.h"

#include "PlanetAtmosphereComponent.h"
#include "PlanetGeologyField.h"
#include "PlanetMineralBlocks.h"
#include "PlanetMineralViewActor.h"
#include "VoxelPlanetActor.h"

#include "Engine/World.h"
#include "HAL/PlatformTime.h"

UPlanetMineralViewComponent::UPlanetMineralViewComponent()
{
	// 只在激活期间 Tick，且排在后更新阶段（此时 N 体已把行星位置写回）。
	PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
}

bool UPlanetMineralViewComponent::Activate(AVoxelPlanetActor* InPlanet)
{
	if (bActive)
	{
		Deactivate();
	}

	if (!InPlanet)
	{
		return false;
	}

	TSharedPtr<const FPlanetGeologyField> Geology = InPlanet->GetGeologyField();
	if (!Geology.IsValid() || Geology->IsEmpty())
	{
		return false;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	const float SurfaceRadiusCm = InPlanet->GetSurfaceRadiusCm();
	// 地质深度以体素计，换回厘米要用单个体素的厘米尺寸（基类属性，与 Radius 同级）。
	const float VoxelSizeCm = InPlanet->VoxelSize;
	if (SurfaceRadiusCm <= 0.f || VoxelSizeCm <= 0.f)
	{
		return false;
	}

	PlanetMineralBlocks::FBuildParams Params;
	Params.SurfaceRadiusCm = SurfaceRadiusCm;
	Params.VoxelSizeCm = VoxelSizeCm;
	Params.SpacingRatio = SpacingRatio;
	Params.Seed = Seed;

	PlanetMineralBlocks::FBuildResult Blocks;
	const double BuildStartSeconds = FPlatformTime::Seconds();
	PlanetMineralBlocks::Build(*Geology, Params, Blocks);

	int32 BlockCount = 0;
	for (const TArray<FPlanetMineralBlock>& Group : Blocks.BlocksByElement)
	{
		BlockCount += Group.Num();
	}
	UE_LOG(LogTemp, Display, TEXT("[矿物] %s：采样 %d 点，生成 %d 个方块，用时 %.1f ms"),
		*InPlanet->GetName(), Blocks.SampleCount, BlockCount,
		(FPlatformTime::Seconds() - BuildStartSeconds) * 1000.0);

	const TArray<FPlanetGeologyElement> SampledElements =
		Geology->GetSampledElementDefinitions();
	const float BlockSizeCm = SurfaceRadiusCm * SpacingRatio * BlockSizeRatio;

	FActorSpawnParameters SpawnParams;
	// Owner 是 AActor*：组件不能当 Owner，取本组件的宿主 Actor（就是这颗行星）。
	SpawnParams.Owner = GetOwner();
	SpawnParams.SpawnCollisionHandlingOverride =
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ViewActor = World->SpawnActor<APlanetMineralViewActor>(
		APlanetMineralViewActor::StaticClass(),
		InPlanet->GetActorLocation(), FRotator::ZeroRotator, SpawnParams);

	if (!ViewActor
		|| !ViewActor->Build(Blocks.BlocksByElement, InPlanet->SurfaceColor,
			SurfaceRadiusCm, BlockSizeCm, SampledElements))
	{
		UE_LOG(LogTemp, Error, TEXT("[矿物] 观察体创建失败：%s"), *InPlanet->GetName());
		if (ViewActor)
		{
			ViewActor->Destroy();
			ViewActor = nullptr;
		}
		return false;
	}

	Planet = InPlanet;
	CachedGeology = Geology;
	Elements = SampledElements;

	// 先存后改：恢复时原样写回，别假设进入前一定是「可见 + 有碰撞」。
	bSavedPlanetHidden = InPlanet->IsHidden();
	bSavedCollisionEnabled = InPlanet->GetActorEnableCollision();
	InPlanet->SetActorEnableCollision(false);
	InPlanet->SetActorHiddenInGame(true);

	if (UPlanetAtmosphereComponent* Atmosphere = InPlanet->GetAtmosphereComponent())
	{
		bSavedAtmosphereEnabled = Atmosphere->bAtmosphereEnabled;
		bSavedAtmosphereVisible = Atmosphere->IsVisible();
		Atmosphere->bAtmosphereEnabled = false;
		Atmosphere->SetVisibility(false);
	}

	bActive = true;
	SetComponentTickEnabled(true);
	return true;
}

void UPlanetMineralViewComponent::Deactivate()
{
	if (!bActive)
	{
		return;
	}
	bActive = false;
	SetComponentTickEnabled(false);

	if (APlanetMineralViewActor* View = ViewActor)
	{
		View->Destroy();
	}
	ViewActor = nullptr;

	if (AVoxelPlanetActor* RestorePlanet = Planet.Get())
	{
		RestorePlanet->SetActorHiddenInGame(bSavedPlanetHidden);
		RestorePlanet->SetActorEnableCollision(bSavedCollisionEnabled);

		if (UPlanetAtmosphereComponent* Atmosphere = RestorePlanet->GetAtmosphereComponent())
		{
			Atmosphere->bAtmosphereEnabled = bSavedAtmosphereEnabled;
			if (bSavedAtmosphereEnabled)
			{
				// Refresh 末尾会 SetVisibility(true)：必须先刷新、再恢复进入前的
				// 可见性，否则「enabled 但被手动隐藏」的壳会被强行显示。
				Atmosphere->RefreshAtmosphereGeometry();
			}
			Atmosphere->SetVisibility(bSavedAtmosphereVisible);
		}
	}

	Planet.Reset();
	CachedGeology.Reset();
	Elements.Reset();
}

void UPlanetMineralViewComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Deactivate();
	Super::EndPlay(EndPlayReason);
}

void UPlanetMineralViewComponent::TickComponent(
	float DeltaTime,
	ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!bActive)
	{
		return;
	}

	// 只跟位置：N 体只平移，行星不转向也不缩放；跟着转会把方块拧歪。
	if (const AVoxelPlanetActor* PlanetActor = Planet.Get())
	{
		if (ViewActor)
		{
			ViewActor->SetActorLocation(PlanetActor->GetActorLocation());
		}
	}
}

int32 UPlanetMineralViewComponent::NumElements() const
{
	return Elements.Num();
}

bool UPlanetMineralViewComponent::IsElementVisible(int32 Index) const
{
	return ViewActor && ViewActor->IsElementVisible(Index);
}

void UPlanetMineralViewComponent::SetElementVisible(int32 Index, bool bNewVisible)
{
	if (ViewActor)
	{
		ViewActor->SetElementVisible(Index, bNewVisible);
	}
}

void UPlanetMineralViewComponent::ToggleElement(int32 Index)
{
	if (!Elements.IsValidIndex(Index))
	{
		return;
	}
	SetElementVisible(Index, !IsElementVisible(Index));
}

void UPlanetMineralViewComponent::SetAllElementsVisible(bool bNewVisible)
{
	if (ViewActor)
	{
		ViewActor->SetAllElementsVisible(bNewVisible);
	}
}
