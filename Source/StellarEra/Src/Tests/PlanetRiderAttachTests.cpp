// 骑乘 / 登船的状态账本。
//
// 背景：登船期间角色的 AttachParent 是飞船，「有没有附着父级」曾经等价于
// 「在不在船上」。改成骑乘之后角色的 AttachParent 平时就是行星，那个等价
// 关系不成立了，判据必须改成显式的 bBoarded。本文件是那条判据的守门人。
//
// 验不了的部分：「星球平移时角色真的被带着走」需要一颗活的体素行星，编辑器
// 世界造不出来，只能在 PIE 里看（见 plans/2026-11-01-planet-rider-attach.md
// 的 Task 5）。这里只锁住纯账本规则。

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "Engine/World.h"

#include "PlanetCharacter.h"
#include "SpaceshipPawn.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	/** 一次性编辑器世界，析构时销毁。照抄 VoxelViewpointInvokerTests.cpp 的写法。 */
	struct FScopedRiderTestWorld
	{
		UWorld* World = nullptr;

		bool Create()
		{
			World = UWorld::CreateWorld(EWorldType::Editor, false);
			if (!World)
			{
				return false;
			}
			FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Editor);
			Context.SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			return true;
		}

		~FScopedRiderTestWorld()
		{
			if (World)
			{
				GEngine->DestroyWorldContext(World);
				World->DestroyWorld(false);
			}
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraPlanetBoardedFlagTest,
	"StellarEra.Planet.BoardedFlag",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraPlanetBoardedFlagTest::RunTest(const FString& Parameters)
{
	FScopedRiderTestWorld Scoped;
	if (!TestTrue(TEXT("测试世界"), Scoped.Create()))
	{
		return false;
	}

	ASpaceshipPawn* Ship = Scoped.World->SpawnActor<ASpaceshipPawn>(
		ASpaceshipPawn::StaticClass(), FTransform::Identity);
	APlanetCharacter* Character = Scoped.World->SpawnActor<APlanetCharacter>(
		APlanetCharacter::StaticClass(), FTransform::Identity);
	if (!TestNotNull(TEXT("飞船"), Ship) || !TestNotNull(TEXT("角色"), Character))
	{
		return false;
	}

	TestFalse(TEXT("出生时不登船"), Character->IsBoarded());

	// 与 ASpaceshipPawn::Board 同序：先挂后设。
	Character->AttachToActor(Ship, FAttachmentTransformRules::KeepWorldTransform);
	Character->SetBoarded(true);
	TestTrue(TEXT("登船后 IsBoarded 为真"), Character->IsBoarded());

	// 这是本次改动的核心：附着父级非空**不再**蕴含「在船上」。
	// 骑乘之后角色平时就挂在行星下，旧判据会把走路误判成登船。
	TestNotNull(TEXT("登船时确实挂着飞船"), Character->GetAttachParentActor());

	// 与 ASpaceshipPawn::DisembarkCrew 同序：先摘后设。
	Character->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
	Character->SetBoarded(false);
	TestFalse(TEXT("离船后 IsBoarded 为假"), Character->IsBoarded());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraPlanetRidePlanetBookkeepingTest,
	"StellarEra.Planet.RidePlanetBookkeeping",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraPlanetRidePlanetBookkeepingTest::RunTest(const FString& Parameters)
{
	FScopedRiderTestWorld Scoped;
	if (!TestTrue(TEXT("测试世界"), Scoped.Create()))
	{
		return false;
	}

	ASpaceshipPawn* Ship = Scoped.World->SpawnActor<ASpaceshipPawn>(
		ASpaceshipPawn::StaticClass(), FTransform::Identity);
	APlanetCharacter* Character = Scoped.World->SpawnActor<APlanetCharacter>(
		APlanetCharacter::StaticClass(), FTransform::Identity);
	if (!TestNotNull(TEXT("飞船"), Ship) || !TestNotNull(TEXT("角色"), Character))
	{
		return false;
	}

	// 规则一：登船期间父级归飞船，SetRidePlanet 不许抢。
	// 抢了的话角色会同时是飞船和行星的子物体 —— 附着只能有一个父级，
	// 结果是登船失效（角色留在地表上）。
	Character->AttachToActor(Ship, FAttachmentTransformRules::KeepWorldTransform);
	Character->SetBoarded(true);
	Character->SetRidePlanet(nullptr);
	TestEqual(TEXT("登船期间父级仍是飞船"), Character->GetAttachParentActor(),
		static_cast<AActor*>(Ship));

	// 规则二：不在船上且没有行星时，传 nullptr 会把附着摘干净。
	// 离船路径依赖这条：SetBoarded(false) 先摘飞船，再让 ReevaluatePlanet
	// 决定挂到哪颗星上。
	Character->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
	Character->SetBoarded(false);
	Character->SetRidePlanet(nullptr);
	TestNull(TEXT("离船且没有行星 → 没有附着父级"), Character->GetAttachParentActor());

	// 规则三：重复传同一颗星是幂等的（ReevaluatePlanet 每 0.5 秒调一次，
	// 每次重挂都会让 KeepWorldTransform 重新算一遍相对变换，产生漂移）。
	Character->SetRidePlanet(nullptr);
	Character->SetRidePlanet(nullptr);
	TestNull(TEXT("重复设空不报错也不产生父级"), Character->GetAttachParentActor());

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
