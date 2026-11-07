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

	// 在这里重新判定落地：此时位置已是本帧写回的，球的位置与颜色才对得上。
	const bool bLanded = Ship->IsLanded();

	// LifeTime 0 = 只画这一帧。SDPG_Foreground 让线穿透船体：球心在船体几何中心，
	// 不穿透大半球面会被船壳挡住（与合力箭头同一个考虑）。
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
