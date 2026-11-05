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
	// 开局操控的是飞船：默认 Pawn 类就是飞船类。
	//
	// 先落到原生类，再尝试用蓝图子类覆盖。模型与参数都挂在 BP_SpaceshipPawn 上，
	// 生成原生 ASpaceshipPawn 只能得到一个占位立方体；蓝图缺失时宁可留一艘方块船，
	// 也不要让开局没有船可开。
	//
	// 注意引擎的默认 Pawn 路径只会按类**新生成**一个实例，没有「用关卡里已经摆着
	// 的那个 Actor」这种模式。关卡里通常已经手摆了一艘船（模型、实例参数都在那上面），
	// 所以 SpawnDefaultPawnFor 会优先把关卡里那艘交出去，DefaultPawnClass 只在那条
	// 兜底路径（关卡里一艘船都没有）里真正用到。
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

	// 纯 C++ 的调试 HUD（状态条 + 短暂提示），不依赖任何蓝图资产。
	// 换成 UMG 时在这里改 HUDClass 即可，其余代码不用动。
	HUDClass = AStellarEraHUD::StaticClass();
}

void AStellarEraGameMode::BeginPlay()
{
	Super::BeginPlay();

	// 玩家的 Pawn 已经由引擎在 RestartPlayer 里按 SpawnDefaultPawnFor 交给他了
	// （发生在 BeginPlay 之前）。这里只负责把飞船摆到出生点。
	EnsureSpaceshipExists();
}

AActor* AStellarEraGameMode::ChoosePlayerStart_Implementation(AController* Player)
{
	// 不走超类：它用角色胶囊在出生点上做 EncroachingBlockingGeometry，而飞船就压在
	// 出生点上（EnsureSpaceshipExists 会把它摆过去），船体是 ECC_Pawn 且 Block 全通道，
	// 于是这个出生点被整条判掉，Pawn 落到 WorldSettings —— 世界原点，离行星 2000 km。
	//
	// 本工程里出生点只由作者摆放决定，「谁站在上面」不该影响选点。关掉飞船碰撞再选、
	// 选完再恢复也能达到同样效果，但那是在跟引擎的判据较劲；直接取 PlayerStart 更直白。
	return FindSpawnPlayerStart();
}

APawn* AStellarEraGameMode::SpawnDefaultPawnFor_Implementation(AController* NewPlayer, AActor* StartSpot)
{
	if (!StartSpot)
	{
		return Super::SpawnDefaultPawnFor_Implementation(NewPlayer, StartSpot);
	}

	// 返回关卡里手摆的那艘：模型、相机、物理参数都在它身上，另生成一艘会让关卡里
	// 多出一艘不知从哪来的船。返回已存在的 Actor 是合法的 —— 引擎拿到它之后只做
	// SetPawn + FinishRestartPlayer，不会重新摆位（摆位由 EnsureSpaceshipExists 负责）。
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
	// 直接遍历，不再走 FindPlayerStart / Super::ChoosePlayerStart：那条路会用角色胶囊
	// 在出生点上做遮挡检测，而飞船就摆在那儿，出生点会被整条判掉。这里只认作者摆放：
	// PIE 的「Play From Here」优先（APlayerStartPIE），否则取关卡里第一个 PlayerStart。
	//
	// 找不到时返回空指针 —— 注意 FindPlayerStart 找不到时返回的是 WorldSettings，
	// 不是空，所以不能拿它判「关卡里有没有出生点」。
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
	// 出生点只有 PlayerStart 一个来源，位置和朝向都取自它。开局交给玩家的就是这艘
	// 飞船（见 SpawnDefaultPawnFor），所以「作者把 PlayerStart 摆在哪，玩家就从哪
	// 开始」这一条完全由它决定 —— 早先这里按「球心 + 固定方向 ×（地表半径 + 固定
	// 高度）」自己算，和 PlayerStart 毫无关系，摆好的出生点看起来完全没生效。
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

	// 关卡里已经手摆了一艘：把它摆到出生点，而不是再生成一艘。
	//
	// 早先这里是「手摆的优先，直接跳过生成」，那条规则会让 PlayerStart 形同虚设 ——
	// 飞船一直待在美术当初摆的地方。现在位置由 PlayerStart 决定，手摆的那艘只
	// 提供模型与参数。也不能改成「另生成一艘」：关卡里会多出一艘不知从哪来的船。
	if (ASpaceshipPawn* Ship = FindExistingShip())
	{
		UE_LOG(LogTemp, Log,
			TEXT("StellarEraGameMode: 关卡里已有飞船 %s，摆到 PlayerStart %s（%s → %s）。"),
			*Ship->GetName(),
			*PlayerStart->GetName(),
			*Ship->GetActorLocation().ToString(),
			*SpawnLocation.ToString());

		// TeleportPhysics：飞船带物理体，普通 SetActorLocation 会被还没跟上的物理
		// 状态拉回原处，表现就是「摆了但没动」。
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
