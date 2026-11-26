// Copyright Voxel Plugin SAS. All Rights Reserved.

#include "StarMapCameraPawn.h"

#include "Components/SceneComponent.h"
#include "Components/InputComponent.h"
#include "StarMapInputComponent.h"
#include "StarMapRigComponent.h"

AStarMapCameraPawn::AStarMapCameraPawn()
{
	StarMapRoot = CreateDefaultSubobject<USceneComponent>(TEXT("StarMapRoot"));
	SetRootComponent(StarMapRoot);

	StarMapRig = CreateDefaultSubobject<UStarMapRigComponent>(TEXT("StarMapRig"));
	StarMapInput = CreateDefaultSubobject<UStarMapInputComponent>(TEXT("StarMapInput"));
}

void AStarMapCameraPawn::SetSourcePawn(APawn* InSourcePawn)
{
	SourcePawn = InSourcePawn;
	if (StarMapRig)
	{
		StarMapRig->SetSourcePawn(InSourcePawn);
	}
}

void AStarMapCameraPawn::CalcCamera(float DeltaSeconds, FMinimalViewInfo& OutResult)
{
	// FOV / 后期由 Super 填；没有 CameraComponent 时它就是默认值，星图不改这两项。
	Super::CalcCamera(DeltaSeconds, OutResult);

	if (StarMapRig && (StarMapRig->IsActive() || StarMapRig->IsExiting()))
	{
		StarMapRig->CalcView(DeltaSeconds, OutResult);
	}
}

void AStarMapCameraPawn::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	if (StarMapInput)
	{
		StarMapInput->Bind(PlayerInputComponent);
	}
}
