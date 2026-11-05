// Copyright Voxel Plugin SAS. All Rights Reserved.
//
// 开局把飞船交给玩家，而不是生成角色。
//
// 守的是这次的回归：飞船摆在 PlayerStart 上，引擎的 EncroachingBlockingGeometry
// 会把出生点整条判掉（船体是 ECC_Pawn 且 Block 全通道），Pawn 于是落到
// WorldSettings —— 世界原点，离行星 2000 km。角色因此找不到行星，开局登船
// 仍然「成功」（FindNearestTo 没有距离判据），落地后按 E 舱里没有角色，
// LeaveShip 静默返回。
//
// 现在选点直接取 PlayerStart（不做遮挡检测），默认 Pawn 先去关卡里找飞船，
// 找不到才在出生点生成一艘。这里锁住这三件事。
//
// 验不了的部分：真正按下 E 生成角色要 PIE 的输入，这里只锁住选点与默认 Pawn。

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerStart.h"

#include "SpaceshipPawn.h"
#include "StellarEraGameMode.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraStartupPawnTest,
	"StellarEra.GameMode.StartupPawnIsSpaceship",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraStartupPawnTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Editor, false);
	if (!TestNotNull(TEXT("测试世界"), World))
	{
		return false;
	}
	FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Editor);
	WorldContext.SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());

	ON_SCOPE_EXIT
	{
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
	};

	AStellarEraGameMode* GameMode = World->SpawnActor<AStellarEraGameMode>();
	if (!TestNotNull(TEXT("游戏模式"), GameMode))
	{
		return false;
	}

	// 默认 Pawn 类必须是飞船：开局玩家操控的不是角色。
	UClass* const DefaultPawnClass = GameMode->GetDefaultPawnClassForController(nullptr);
	if (TestNotNull(TEXT("DefaultPawnClass 已配置"), DefaultPawnClass))
	{
		TestTrue(TEXT("默认 Pawn 类是飞船"),
			DefaultPawnClass->IsChildOf(ASpaceshipPawn::StaticClass()));
	}

	APlayerStart* PlayerStart = World->SpawnActor<APlayerStart>();
	if (!TestNotNull(TEXT("PlayerStart"), PlayerStart))
	{
		return false;
	}
	// 挪离原点：下面要按坐标断言兜底生成的船就在出生点上。
	PlayerStart->SetActorLocation(FVector(1000.0, 2000.0, 3000.0));

	// 关卡里的真实布局：飞船与 PlayerStart 在同一点。
	ASpaceshipPawn* Ship = World->SpawnActor<ASpaceshipPawn>();
	if (!TestNotNull(TEXT("飞船"), Ship))
	{
		return false;
	}
	Ship->SetActorLocation(PlayerStart->GetActorLocation());

	AActor* const Chosen = GameMode->ChoosePlayerStart(nullptr);
	TestTrue(TEXT("飞船压在出生点上时仍然能选中该出生点"),
		Chosen == static_cast<AActor*>(PlayerStart));

	// 选点不再靠「临时关掉飞船碰撞」，所以碰撞从头到尾都该是开着的。
	TestTrue(TEXT("选点没有动过飞船的碰撞"), Ship->GetActorEnableCollision());

	APawn* const StartupPawn = GameMode->SpawnDefaultPawnFor(nullptr, PlayerStart);
	TestTrue(TEXT("开局交给玩家的是关卡里那艘飞船本人"),
		StartupPawn == static_cast<APawn*>(Ship));

	int32 ShipCount = 0;
	for (TActorIterator<ASpaceshipPawn> It(World); It; ++It)
	{
		++ShipCount;
	}
	TestEqual(TEXT("没有额外生成第二艘飞船"), ShipCount, 1);

	// 兜底：关卡里没有飞船时，按 DefaultPawnClass 在出生点生成一艘。
	Ship->Destroy();
	APawn* const FallbackPawn = GameMode->SpawnDefaultPawnFor(nullptr, PlayerStart);
	if (TestNotNull(TEXT("关卡里没有飞船时会生成一艘"), FallbackPawn))
	{
		TestTrue(TEXT("兜底生成的是一艘飞船"),
			FallbackPawn->IsA<ASpaceshipPawn>());
		TestTrue(TEXT("兜底飞船生成在出生点上"),
			FallbackPawn->GetActorLocation().Equals(PlayerStart->GetActorLocation(), 1.0));
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
