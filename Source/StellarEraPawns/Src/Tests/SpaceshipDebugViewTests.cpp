// Copyright Voxel Plugin SAS. All Rights Reserved.
//
// 飞船调试视角（B 键）的状态开关。只测状态与接线 —— 线框球与箭头的显示要在 PIE 里看。
// 守两件事：默认关、切换可逆；检测球组件的 tick 排在 N 体写回之后。

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "LandingDebugSphereComponent.h"
#include "SpaceshipPawn.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraShipDebugViewModeTest,
	"StellarEra.Spaceship.DebugViewModeToggle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraShipDebugViewModeTest::RunTest(const FString& Parameters)
{
	// 原生 ASpaceshipPawn 就够了：本用例只碰状态位，不加载蓝图资源。
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

	ASpaceshipPawn* Ship = World->SpawnActor<ASpaceshipPawn>();
	TestNotNull(TEXT("能生成飞船"), Ship);
	if (!Ship)
	{
		return false;
	}

	TestFalse(TEXT("调试视角默认关"), Ship->IsDebugViewMode());

	Ship->ToggleDebugViewMode();
	TestTrue(TEXT("切换一次后为开"), Ship->IsDebugViewMode());

	Ship->ToggleDebugViewMode();
	TestFalse(TEXT("再切一次回到关"), Ship->IsDebugViewMode());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraLandingDebugSphereTickTest,
	"StellarEra.Spaceship.LandingDebugSphereTickGroup",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraLandingDebugSphereTickTest::RunTest(const FString& Parameters)
{
	// 守 tick 顺序：TickObjects（N 体写回位置）跑在 TG_PostUpdateWork 之前。
	// 换回 TG_PrePhysics 就会画在上一帧的船位上 —— 7 km/s @ 25 fps 偏约 280 m。
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

	ASpaceshipPawn* Ship = World->SpawnActor<ASpaceshipPawn>();
	TestNotNull(TEXT("能生成飞船"), Ship);
	if (!Ship)
	{
		return false;
	}

	ULandingDebugSphereComponent* Sphere =
		Ship->FindComponentByClass<ULandingDebugSphereComponent>();
	if (!TestNotNull(TEXT("飞船上有检测球组件"), Sphere))
	{
		return false;
	}

	TestTrue(TEXT("检测球组件参与 tick"), Sphere->PrimaryComponentTick.bCanEverTick);
	TestEqual(TEXT("检测球 tick 排在写回之后"),
		static_cast<int32>(Sphere->PrimaryComponentTick.TickGroup.GetValue()),
		static_cast<int32>(TG_PostUpdateWork));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
