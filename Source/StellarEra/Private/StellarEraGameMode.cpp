// Copyright Voxel Plugin SAS. All Rights Reserved.

#include "StellarEraGameMode.h"

#include "PlanetCharacter.h"
#include "SpaceshipPawn.h"
#include "StellarEraPlayerController.h"
#include "VoxelPlanetActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

AStellarEraGameMode::AStellarEraGameMode()
{
	// 关卡里没有 PlayerStart，角色会生成在世界原点。APlanetCharacter 的
	// BeginPlay 会把自己摆到球面正上方，所以这里不需要额外处理出生点。
	//
	// 和飞船一样，先落到原生类，再尝试用蓝图子类覆盖。
	//
	// 但角色的失败模式比飞船严苛得多：飞船的蓝图缺失只是少一艘好看的船，
	// 角色的蓝图缺失等于玩家本体出问题。所以这里退回的是原生
	// APlanetCharacter —— 移动、重力、相机全都在 C++ 里，蓝图只负责模型，
	// 缺了它角色照样能走，只是顶着引擎骷髅方块。
	DefaultPawnClass = APlanetCharacter::StaticClass();

	static ConstructorHelpers::FClassFinder<APlanetCharacter> CharacterClassFinder(
		TEXT("/Game/Code/BP_PlanetCharacter.BP_PlanetCharacter_C"));
	if (CharacterClassFinder.Succeeded())
	{
		DefaultPawnClass = CharacterClassFinder.Class;
	}
	else
	{
		UE_LOG(LogTemp, Warning,
			TEXT("StellarEraGameMode: 找不到 BP_PlanetCharacter，将使用原生 APlanetCharacter（占位骷髅方块）。"));
	}

	PlayerControllerClass = AStellarEraPlayerController::StaticClass();

	// 先落到原生类，再尝试用蓝图子类覆盖。
	//
	// 模型与摆位都挂在 BP_SpaceshipPawn 上，生成原生 ASpaceshipPawn 只能得到一个
	// 占位立方体。但蓝图缺失时绝不能什么都不生成 —— 宁可留一艘方块船，也不要让
	// 开局登船因为「关卡里一艘船都没有」而失败。
	ShipClass = ASpaceshipPawn::StaticClass();

	static ConstructorHelpers::FClassFinder<ASpaceshipPawn> ShipClassFinder(
		TEXT("/Game/Code/BP_SpaceshipPawn.BP_SpaceshipPawn_C"));
	if (ShipClassFinder.Succeeded())
	{
		ShipClass = ShipClassFinder.Class;
	}
	else
	{
		UE_LOG(LogTemp, Warning,
			TEXT("StellarEraGameMode: 找不到 BP_SpaceshipPawn，自动生成将使用原生 ASpaceshipPawn（占位立方体）。"));
	}
}

void AStellarEraGameMode::BeginPlay()
{
	Super::BeginPlay();
	EnsureSpaceshipExists();

	if (!bStartInSpaceship)
	{
		return;
	}

	// 轮询而不是只等一帧：本函数与「本地玩家登录并拿到 DefaultPawn」的先后由引擎
	// 决定，实测只推迟一帧时 GetPawn() 还不是 APlanetCharacter，登船会静默失败
	// （飞船一直在关卡里，报错文案却说是「没有飞船」）。FirstDelay = 0 让第一次
	// 尝试仍在下一帧发生，之后就按 StartupShipRetryInterval 反复试。
	StartupShipAttempts = 0;
	GetWorldTimerManager().SetTimer(
		StartupShipTimerHandle,
		this,
		&AStellarEraGameMode::PossessStartupShip,
		StartupShipRetryInterval,
		/*bLoop=*/true,
		/*FirstDelay=*/0.f);
}

void AStellarEraGameMode::PossessStartupShip()
{
	AStellarEraPlayerController* StellarPlayerController =
		Cast<AStellarEraPlayerController>(GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr);

	if (StellarPlayerController && StellarPlayerController->EnterNearestShip())
	{
		GetWorldTimerManager().ClearTimer(StartupShipTimerHandle);
		return;
	}

	// 还没就位就再等一轮。只有试满次数才放弃。
	if (++StartupShipAttempts < StartupShipMaxAttempts)
	{
		return;
	}

	GetWorldTimerManager().ClearTimer(StartupShipTimerHandle);

	if (!StellarPlayerController)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("StellarEraGameMode: 开局登船失败：%d 次重试后仍拿不到 AStellarEraPlayerController。"),
			StartupShipAttempts);
		return;
	}

	// 打出当时的 Pawn 类型，好区分「关卡里真没飞船」和「Pawn 一直没准备好」。
	const APawn* CurrentPawn = StellarPlayerController->GetPawn();
	UE_LOG(LogTemp, Warning,
		TEXT("StellarEraGameMode: 开局登船失败：%d 次重试后仍未完成，当前 Pawn = %s。"),
		StartupShipAttempts,
		CurrentPawn ? *CurrentPawn->GetClass()->GetName() : TEXT("null"));
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
		// 打出找到的这艘船，好区分「关卡里真有」和「它随后被 World Partition
		// 流送卸载了」——后者正是开局登船失败的现场：这一刻看得见，等 5 秒重试
		// 窗口跑起来时已经不在世界里了。
		UE_LOG(LogTemp, Log,
			TEXT("StellarEraGameMode: 关卡里已有飞船 %s @ %s，跳过生成。"),
			*ExistingShip->GetName(),
			*ExistingShip->GetActorLocation().ToString());
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

	// 按方向取地表：有地形时地表在基准球面上下浮动，用基准半径会把飞船埋进
	// 山体里或悬在半空。
	const FVector Location =
		Planet->GetActorLocation() +
		Direction * (Planet->GetSurfaceRadiusCmAtDirection(Direction) + ShipSpawnHeight);

	// 让飞船的“上”与球面法线一致：否则在极地附近它会横躺着。
	const FQuat AlignToSurface = FQuat::FindBetweenNormals(FVector::UpVector, Direction);

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	UClass* ClassToSpawn = ShipClass ? ShipClass.Get() : ASpaceshipPawn::StaticClass();
	if (!GetWorld()->SpawnActor<ASpaceshipPawn>(ClassToSpawn, Location, AlignToSurface.Rotator(), Params))
	{
		UE_LOG(LogTemp, Warning,
			TEXT("StellarEraGameMode: 飞船生成失败（SpawnActor 返回空）。"));
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("StellarEraGameMode: 关卡里没有飞船，已在 %s 生成一艘 %s。"),
		*Location.ToString(),
		*ClassToSpawn->GetName());
}
