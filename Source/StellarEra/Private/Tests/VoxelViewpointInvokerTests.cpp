// 视点门控：只有当前视点 Pawn 的体素调用器参与 LOD。
//
// 这里验「开关本身」与「谓词合成」。BeginPlay 那一条验不了 —— 编辑器世界的
// SpawnActor 只走 OnRegister（自动启用），不走 BeginPlay。BeginPlay 的效果
// 只能在 PIE 里看日志，见计划任务 3。

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"

#include "VoxelMessages.h"
#include "VoxelComponents/VoxelInvokerComponent.h"
#include "VoxelViewpointInvokers.h"
#include "PlanetCharacter.h"
#include "SpaceshipPawn.h"
#include "StarMapCameraPawn.h"
#include "StellarEraPlayerController.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	/** 一次性编辑器世界，析构时销毁。照抄 StarMapTests.cpp 的写法。 */
	struct FScopedInvokerTestWorld
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

		~FScopedInvokerTestWorld()
		{
			if (World)
			{
				GEngine->DestroyWorldContext(World);
				World->DestroyWorld(false);
			}
		}
	};

	/** Owner 身上的全部体素调用器。 */
	TArray<UVoxelInvokerComponentBase*> CollectInvokers(const AActor* Owner)
	{
		TArray<UVoxelInvokerComponentBase*> Invokers;
		Owner->GetComponents<UVoxelInvokerComponentBase>(Invokers);
		return Invokers;
	}

	bool AllEnabled(const TArray<UVoxelInvokerComponentBase*>& Invokers)
	{
		for (const UVoxelInvokerComponentBase* Invoker : Invokers)
		{
			if (!Invoker->IsInvokerEnabled())
			{
				return false;
			}
		}
		return true;
	}

	bool AllDisabled(const TArray<UVoxelInvokerComponentBase*>& Invokers)
	{
		for (const UVoxelInvokerComponentBase* Invoker : Invokers)
		{
			if (Invoker->IsInvokerEnabled())
			{
				return false;
			}
		}
		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraInvokerViewpointShipTest,
	"StellarEra.Planet.InvokerViewpoint.Ship",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraInvokerViewpointShipTest::RunTest(const FString& Parameters)
{
	// 本测试的重点是「重复传同一个值不产生插件报错」：
	// EnableInvoker/DisableInvoker 在状态已对时会往 Message Log 写
	// "Invoker already enabled/disabled"，这里把消息截下来数一遍。
	FScopedInvokerTestWorld Scoped;
	if (!TestTrue(TEXT("测试世界"), Scoped.Create()))
	{
		return false;
	}

	ASpaceshipPawn* Ship = Scoped.World->SpawnActor<ASpaceshipPawn>(
		ASpaceshipPawn::StaticClass(), FTransform::Identity);
	if (!TestNotNull(TEXT("飞船"), Ship))
	{
		return false;
	}

	const TArray<UVoxelInvokerComponentBase*> Invokers = CollectInvokers(Ship);
	// 细气泡 + LOD3 + LOD5 + LOD6 过渡环。
	if (!TestEqual(TEXT("飞船的调用器个数"), Invokers.Num(), 4))
	{
		return false;
	}

	// OnRegister 自动启用：这是「生成后、首次 Possess 前」的既有行为。
	TestTrue(TEXT("生成后自动启用"), AllEnabled(Invokers));

	TArray<FString> Messages;
	const FDelegateHandle Handle = FVoxelMessages::LogMessageDelegate.AddLambda(
		[&Messages](const TSharedRef<FTokenizedMessage>& Message, EVoxelShowNotification)
		{
			Messages.Add(Message->ToText().ToString());
		});
	ON_SCOPE_EXIT
	{
		FVoxelMessages::LogMessageDelegate.Remove(Handle);
	};

	Ship->SetViewpointInvokersActive(false);
	TestTrue(TEXT("非视点 → 全部关闭"), AllDisabled(Invokers));

	Ship->SetViewpointInvokersActive(false);
	TestTrue(TEXT("重复关闭仍关闭"), AllDisabled(Invokers));

	Ship->SetViewpointInvokersActive(true);
	TestTrue(TEXT("视点 → 全部打开"), AllEnabled(Invokers));

	Ship->SetViewpointInvokersActive(true);
	TestTrue(TEXT("重复打开仍打开"), AllEnabled(Invokers));

	TestTrue(TEXT("重复开关不产生插件报错"),
		!Messages.ContainsByPredicate([](const FString& Message)
		{
			return Message.Contains(TEXT("already"));
		}));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraInvokerViewpointCharacterTest,
	"StellarEra.Planet.InvokerViewpoint.Character",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraInvokerViewpointCharacterTest::RunTest(const FString& Parameters)
{
	// 角色同时受「登船」与「视点」两条规则约束。谓词是「与」：
	// 登船中即使拿到视点也必须保持关闭，否则登船期间脚下 200 米的精细块
	// 会跟着飞船高速平移、每帧重网格化。
	FScopedInvokerTestWorld Scoped;
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

	// 1 个贴身环（LOD 0 @ 200 米 + 碰撞）+ 3 个过渡环（LOD 2/4/6）。
	// 过渡环是后加的，但它们必须与贴身环同进同退 —— 下面每一条 AllEnabled /
	// AllDisabled 都是对**全部 4 个**求值，漏掉任何一个都会在这里挂掉。
	const TArray<UVoxelInvokerComponentBase*> Invokers = CollectInvokers(Character);
	if (!TestEqual(TEXT("角色的调用器个数"), Invokers.Num(), 4))
	{
		return false;
	}

	// 过渡环只能做 LOD：碰撞那条细分路径不读 LODToSet、锁死 Height 0，
	// 开在公里级盒子上会把块数直接顶爆。这条断言是那道约束的守门人。
	for (const UVoxelInvokerComponentBase* Invoker : Invokers)
	{
		const UVoxelSimpleInvokerComponent* Simple = Cast<UVoxelSimpleInvokerComponent>(Invoker);
		if (Simple && Simple->LODToSet > 0)
		{
			TestFalse(TEXT("过渡环不参与碰撞"), Simple->bUseForCollisions);
		}
	}

	// 不挂接 + 非视点 → 关
	Character->SetViewpointInvokersActive(false);
	TestTrue(TEXT("地面且非视点 → 关闭"), AllDisabled(Invokers));

	// 登船（与 BoardShip 同序：先挂后设）+ 拿到视点 → 仍关
	Character->AttachToActor(Ship, FAttachmentTransformRules::KeepWorldTransform);
	Character->SetViewpointInvokersActive(true);
	TestTrue(TEXT("登船中即使拿到视点也保持关闭"), AllDisabled(Invokers));

	// 离船（与 LeaveShip 同序：先摘后设）+ 仍是视点 → 开
	Character->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
	Character->SetViewpointInvokersActive(true);
	TestTrue(TEXT("离船且是视点 → 打开"), AllEnabled(Invokers));

	// SetBoarded 走同一个谓词：登船关、离船开。
	Character->AttachToActor(Ship, FAttachmentTransformRules::KeepWorldTransform);
	Character->SetBoarded(true);
	TestTrue(TEXT("登船 → 关闭"), AllDisabled(Invokers));

	Character->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
	Character->SetBoarded(false);
	TestTrue(TEXT("离船且是视点 → 重新打开"), AllEnabled(Invokers));

	// 视点被拿走（进星图），人还在地上 → 关
	Character->SetViewpointInvokersActive(false);
	TestTrue(TEXT("非视点 → 关闭"), AllDisabled(Invokers));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraInvokerViewpointStarMapTest,
	"StellarEra.Planet.InvokerViewpoint.StarMap",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraInvokerViewpointStarMapTest::RunTest(const FString& Parameters)
{
	FScopedInvokerTestWorld Scoped;
	if (!TestTrue(TEXT("测试世界"), Scoped.Create()))
	{
		return false;
	}

	AStarMapCameraPawn* Camera = Scoped.World->SpawnActor<AStarMapCameraPawn>(
		AStarMapCameraPawn::StaticClass(), FTransform::Identity);
	if (!TestNotNull(TEXT("星图相机"), Camera))
	{
		return false;
	}

	const TArray<UVoxelInvokerComponentBase*> Invokers = CollectInvokers(Camera);
	if (!TestEqual(TEXT("星图相机的调用器个数"), Invokers.Num(), 4))
	{
		return false;
	}

	Camera->SetViewpointInvokersActive(false);
	TestTrue(TEXT("非视点 → 四档一起关闭"), AllDisabled(Invokers));

	Camera->SetViewpointInvokersActive(true);
	TestTrue(TEXT("视点 → 四档一起打开"), AllEnabled(Invokers));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraInvokerViewpointGateTest,
	"StellarEra.Planet.InvokerViewpoint.Gate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraInvokerViewpointGateTest::RunTest(const FString& Parameters)
{
	// 驱动逻辑本身：遍历世界，只把 InPawn 当成视点；InPawn 为空时全关。
	// 不能直接调 OnPossess —— 它是 protected，而且需要本地玩家与 HUD。
	FScopedInvokerTestWorld Scoped;
	if (!TestTrue(TEXT("测试世界"), Scoped.Create()))
	{
		return false;
	}

	ASpaceshipPawn* Ship = Scoped.World->SpawnActor<ASpaceshipPawn>(
		ASpaceshipPawn::StaticClass(), FTransform::Identity);
	APlanetCharacter* Character = Scoped.World->SpawnActor<APlanetCharacter>(
		APlanetCharacter::StaticClass(), FTransform::Identity);
	AStarMapCameraPawn* Camera = Scoped.World->SpawnActor<AStarMapCameraPawn>(
		AStarMapCameraPawn::StaticClass(), FTransform::Identity);
	APawn* Plain = Scoped.World->SpawnActor<APawn>(
		APawn::StaticClass(), FTransform::Identity);

	if (!TestNotNull(TEXT("飞船"), Ship) || !TestNotNull(TEXT("角色"), Character) ||
		!TestNotNull(TEXT("星图相机"), Camera) || !TestNotNull(TEXT("普通 Pawn"), Plain))
	{
		return false;
	}

	const TArray<UVoxelInvokerComponentBase*> ShipInvokers = CollectInvokers(Ship);
	const TArray<UVoxelInvokerComponentBase*> CharacterInvokers = CollectInvokers(Character);
	const TArray<UVoxelInvokerComponentBase*> CameraInvokers = CollectInvokers(Camera);

	// 视点 = 飞船
	AStellarEraPlayerController::ApplyViewpointInvokerGate(Scoped.World, Ship);
	TestTrue(TEXT("视点飞船 → 打开"), AllEnabled(ShipInvokers));
	TestTrue(TEXT("角色 → 关闭"), AllDisabled(CharacterInvokers));
	TestTrue(TEXT("星图相机 → 关闭"), AllDisabled(CameraInvokers));

	// 视点 = 角色（下船）
	AStellarEraPlayerController::ApplyViewpointInvokerGate(Scoped.World, Character);
	TestTrue(TEXT("视点角色 → 打开"), AllEnabled(CharacterInvokers));
	TestTrue(TEXT("飞船 → 关闭"), AllDisabled(ShipInvokers));

	// 视点 = 星图相机（进星图）
	AStellarEraPlayerController::ApplyViewpointInvokerGate(Scoped.World, Camera);
	TestTrue(TEXT("视点星图 → 打开"), AllEnabled(CameraInvokers));
	TestTrue(TEXT("角色 → 关闭"), AllDisabled(CharacterInvokers));
	TestTrue(TEXT("飞船 → 关闭"), AllDisabled(ShipInvokers));

	// 没有视点（OnUnPossess 路径）
	AStellarEraPlayerController::ApplyViewpointInvokerGate(Scoped.World, nullptr);
	TestTrue(TEXT("无视点 → 星图关闭"), AllDisabled(CameraInvokers));

	return true;
}

#endif
