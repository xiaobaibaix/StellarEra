// Copyright Voxel Plugin SAS. All Rights Reserved.

#include "LandingDebugSphereComponent.h"

#include "DrawDebugHelpers.h"
#include "SpaceshipPawn.h"

ULandingDebugSphereComponent::ULandingDebugSphereComponent()
{
	// TG_PostUpdateWork 排在 UCosmosSimulationSubsystem 写回位置之后（见头文件）。
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
}

void ULandingDebugSphereComponent::TickComponent(
	float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	const ASpaceshipPawn* Ship = Cast<ASpaceshipPawn>(GetOwner());
	if (!Ship || !Ship->IsDebugViewMode() || !Ship->IsLocallyControlled())
	{
		return;
	}

	// 落地状态在这里重新判定，而不是复用飞船 Tick 里的值：此时位置已是本帧写回
	// 后的，球的位置与颜色才对得上同一个球。
	const bool bLanded = Ship->IsLanded();

	// LifeTime 0 = 只画这一帧；每帧重画，所以它一直跟着飞船走。
	// SDPG_Foreground 让线穿透船体：球心就在船体几何中心，不穿透的话大半球面
	// 会被船壳挡住（与合力箭头同一个考虑）。
	DrawDebugSphere(
		GetWorld(),
		Ship->GetActorLocation(),
		Ship->GetLandingQueryRadiusCm(),
		/*Segments=*/32,
		bLanded ? FColor::Green : FColor::Orange,
		/*bPersistentLines=*/false,
		/*LifeTime=*/0.f,
		SDPG_Foreground,
		/*Thickness=*/2.f);
}
