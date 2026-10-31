// Copyright Voxel Plugin SAS. All Rights Reserved.

#include "StellarEraGameMode.h"

#include "PlanetCharacter.h"
#include "SpaceshipPawn.h"
#include "StellarEraHUD.h"
#include "StellarEraPlayerController.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerStart.h"
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

	// 纯 C++ 的调试 HUD（状态条 + 短暂提示），不依赖任何蓝图资产。
	// 换成 UMG 时在这里改 HUDClass 即可，其余代码不用动。
	HUDClass = AStellarEraHUD::StaticClass();

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

APlayerStart* AStellarEraGameMode::FindSpawnPlayerStart()
{
	// 借引擎自己的选点逻辑：它优先取 PIE 的 APlayerStartPIE（「Play From Here」），
	// 否则在关卡里的 PlayerStart 中挑一个。作者把出生点摆在哪，飞船就出现在哪，
	// 代码里不写死方向或高度。
	//
	// 不能靠「FindPlayerStart 返回空」判断关卡里没有 PlayerStart —— 找不到时它
	// 返回的是 WorldSettings，不是空指针（见 GameModeBase.cpp 里
	// FindPlayerStart_Implementation 末尾的 BestStart = World->GetWorldSettings()）。
	// 所以这里必须判类型。
	//
	// 传空的 Controller 是安全的：ShouldSpawnAtStartSpot 会判空返回 false，
	// GetDefaultPawnClassForController 也不碰 Controller。
	return Cast<APlayerStart>(FindPlayerStart(nullptr));
}

void AStellarEraGameMode::EnsureSpaceshipExists()
{
	if (!bAutoPlaceSpaceshipAtStart)
	{
		return;
	}

	// 出生点只有 PlayerStart 一个来源，位置和朝向都取自它。玩家（角色）由引擎
	// 按 DefaultPawnClass 生成在同一个 PlayerStart 上，飞船摆在同一处，开局登船
	// 的距离判据（BoardingRadius = 600 cm）才成立 —— 早先这里按「球心 + 固定方向
	// ×（地表半径 + 固定高度）」自己算，和 PlayerStart 毫无关系，摆好的出生点
	// 看起来完全没生效。
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
	TActorIterator<ASpaceshipPawn> ExistingShip(GetWorld());
	if (ASpaceshipPawn* Ship = ExistingShip ? *ExistingShip : nullptr)
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
	UClass* ClassToSpawn = ShipClass ? ShipClass.Get() : ASpaceshipPawn::StaticClass();
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
