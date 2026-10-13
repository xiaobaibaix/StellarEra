// Copyright Voxel Plugin SAS. All Rights Reserved.

#include "StellarEraGameMode.h"

#include "PlanetCharacter.h"
#include "SpaceshipPawn.h"
#include "StellarEraPlayerController.h"
#include "VoxelPlanetActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"

AStellarEraGameMode::AStellarEraGameMode()
{
	// 关卡里没有 PlayerStart，角色会生成在世界原点。APlanetCharacter 的
	// BeginPlay 会把自己摆到球面正上方，所以这里不需要额外处理出生点。
	DefaultPawnClass = APlanetCharacter::StaticClass();
	PlayerControllerClass = AStellarEraPlayerController::StaticClass();
}

void AStellarEraGameMode::BeginPlay()
{
	Super::BeginPlay();
	EnsureSpaceshipExists();
}

void AStellarEraGameMode::EnsureSpaceshipExists()
{
	if (!bSpawnSpaceshipIfMissing)
	{
		return;
	}

	// 关卡里已经手摆了一艘就不再生成，让美术摆位优先于自动布置。
	TActorIterator<ASpaceshipPawn> ExistingShip(GetWorld());
	if (ExistingShip)
	{
		return;
	}

	AVoxelPlanetActor* Planet = nullptr;
	for (TActorIterator<AVoxelPlanetActor> It(GetWorld()); It; ++It)
	{
		Planet = *It;
		break;
	}
	if (!Planet)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("StellarEraGameMode: 关卡里没有 AVoxelPlanetActor，跳过飞船生成。"));
		return;
	}

	const FVector Direction = ShipSpawnDirection.GetSafeNormal();
	if (Direction.IsNearlyZero())
	{
		UE_LOG(LogTemp, Warning,
			TEXT("StellarEraGameMode: ShipSpawnDirection 是零向量，跳过飞船生成。"));
		return;
	}

	const FVector Location =
		Planet->GetActorLocation() + Direction * (Planet->GetSurfaceRadiusCm() + ShipSpawnHeight);

	// 让飞船的“上”与球面法线一致：否则在极地附近它会横躺着。
	const FQuat AlignToSurface = FQuat::FindBetweenNormals(FVector::UpVector, Direction);

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	if (!GetWorld()->SpawnActor<ASpaceshipPawn>(Location, AlignToSurface.Rotator(), Params))
	{
		UE_LOG(LogTemp, Warning,
			TEXT("StellarEraGameMode: 飞船生成失败（SpawnActor 返回空）。"));
	}
}
