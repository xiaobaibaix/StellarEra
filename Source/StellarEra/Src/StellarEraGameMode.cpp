// Copyright Voxel Plugin SAS. All Rights Reserved.

#include "StellarEraGameMode.h"

#include "SpaceshipPawn.h"
#include "StellarEraHUD.h"
#include "StellarEraPlayerController.h"
#include "Engine/PlayerStartPIE.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerStart.h"
#include "UObject/ConstructorHelpers.h"

AStellarEraGameMode::AStellarEraGameMode()
{
	// 先落到原生类，再尝试用蓝图子类覆盖：模型与参数都在 BP_SpaceshipPawn 上，
	// 蓝图缺失时宁可留方块船也别让开局没船可开。DefaultPawnClass 只在兜底路径用到。
	DefaultPawnClass = ASpaceshipPawn::StaticClass();

	static ConstructorHelpers::FClassFinder<ASpaceshipPawn> ShipClassFinder(
		TEXT("/Game/Code/Pawns/BP_SpaceshipPawn.BP_SpaceshipPawn_C"));
	if (ShipClassFinder.Succeeded())
	{
		DefaultPawnClass = ShipClassFinder.Class;
	}
	else
	{
		UE_LOG(LogTemp, Warning,
			TEXT("StellarEraGameMode: 找不到 BP_SpaceshipPawn，自动生成将使用原生 ASpaceshipPawn（占位立方体）。"));
	}

	PlayerControllerClass = AStellarEraPlayerController::StaticClass();

	// 纯 C++ 调试 HUD。换 UMG 时只改 HUDClass。
	HUDClass = AStellarEraHUD::StaticClass();
}

void AStellarEraGameMode::BeginPlay()
{
	Super::BeginPlay();

	// Pawn 已由引擎在 RestartPlayer 里交出（早于 BeginPlay），这里只负责摆到出生点。
	EnsureSpaceshipExists();
}

AActor* AStellarEraGameMode::ChoosePlayerStart_Implementation(AController* Player)
{
	// 不走超类：它用角色胶囊做 EncroachingBlockingGeometry，而飞船就压在出生点上
	// （ECC_Pawn + Block 全通道），会被整条判掉、Pawn 落到世界原点。
	return FindSpawnPlayerStart();
}

APawn* AStellarEraGameMode::SpawnDefaultPawnFor_Implementation(AController* NewPlayer, AActor* StartSpot)
{
	if (!StartSpot)
	{
		return Super::SpawnDefaultPawnFor_Implementation(NewPlayer, StartSpot);
	}

	// 返回关卡里手摆的那艘：模型、相机、物理参数都在它身上，另生成会多出出口。
	// 引擎拿到已有 Actor 只做 SetPawn + FinishRestartPlayer，摆位交给 EnsureSpaceshipExists。
	if (ASpaceshipPawn* ExistingShip = FindExistingShip())
	{
		return ExistingShip;
	}

	UClass* ClassToSpawn = DefaultPawnClass ? DefaultPawnClass.Get() : ASpaceshipPawn::StaticClass();
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	return GetWorld()->SpawnActor<ASpaceshipPawn>(
		ClassToSpawn, StartSpot->GetActorLocation(), StartSpot->GetActorRotation(), Params);
}

ASpaceshipPawn* AStellarEraGameMode::FindExistingShip() const
{
	for (TActorIterator<ASpaceshipPawn> It(GetWorld()); It; ++It)
	{
		return *It;
	}
	return nullptr;
}

APlayerStart* AStellarEraGameMode::FindSpawnPlayerStart()
{
	// 直接遍历，不走 FindPlayerStart（会做遮挡检测，飞船就摆在那儿）。PIE 的
	// 「Play From Here」（APlayerStartPIE）优先，否则取第一个。找不到返回 nullptr ——
	// FindPlayerStart 找不到时返回 WorldSettings 而非空，别拿它判。
	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}

	APlayerStart* FirstPlayerStart = nullptr;
	for (TActorIterator<APlayerStart> It(World); It; ++It)
	{
		if (It->IsA<APlayerStartPIE>())
		{
			return *It;
		}
		if (!FirstPlayerStart)
		{
			FirstPlayerStart = *It;
		}
	}
	return FirstPlayerStart;
}

void AStellarEraGameMode::EnsureSpaceshipExists()
{
	// 位置和朝向都取自 PlayerStart：早先按「球心 + 固定方向 × 高度」自己算，出生点形同没生效。
	APlayerStart* PlayerStart = FindSpawnPlayerStart();
	if (!PlayerStart)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("StellarEraGameMode: 关卡里没有 PlayerStart，无法确定飞船出生点，跳过摆放。"
				"在关卡里摆一个 PlayerStart 即可。"));
		return;
	}

	const FVector SpawnLocation = PlayerStart->GetActorLocation();
	const FRotator SpawnRotation = PlayerStart->GetActorRotation();

	// 手摆的那艘摆到出生点而非再生成：早先「手摆的直接跳过」让 PlayerStart 形同虚设，
	// 另生成又会让关卡里多出一艘船。
	if (ASpaceshipPawn* Ship = FindExistingShip())
	{
		UE_LOG(LogTemp, Log,
			TEXT("StellarEraGameMode: 关卡里已有飞船 %s，摆到 PlayerStart %s（%s → %s）。"),
			*Ship->GetName(),
			*PlayerStart->GetName(),
			*Ship->GetActorLocation().ToString(),
			*SpawnLocation.ToString());

		// TeleportPhysics：飞船带物理体，普通 SetActorLocation 会被物理状态拉回原处。
		Ship->SetActorLocationAndRotation(SpawnLocation, SpawnRotation,
			/*bSweep=*/false, nullptr, ETeleportType::TeleportPhysics);
		return;
	}

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	UClass* ClassToSpawn = DefaultPawnClass ? DefaultPawnClass.Get() : ASpaceshipPawn::StaticClass();
	if (!GetWorld()->SpawnActor<ASpaceshipPawn>(ClassToSpawn, SpawnLocation, SpawnRotation, Params))
	{
		UE_LOG(LogTemp, Warning,
			TEXT("StellarEraGameMode: 飞船生成失败（SpawnActor 返回空）。"));
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("StellarEraGameMode: 关卡里没有飞船，已在 PlayerStart %s 处生成一艘 %s。"),
		*SpawnLocation.ToString(),
		*ClassToSpawn->GetName());
}
