// Copyright Voxel Plugin SAS. All Rights Reserved.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#include "CosmosOrbitComponent.h"
#include "CosmosSimulationSubsystem.h"

#if WITH_DEV_AUTOMATION_TESTS

// 断言写法说明：指针一律用 TestTrue/TestFalse 比较。TestEqual 的泛型重载要走
// LexToString，而指针没有可靠的 ToString —— 会编不过或打出没用的东西。

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraStarMapSelectionTest,
	"StellarEra.StarMap.Selection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraStarMapSelectionTest::RunTest(const FString& Parameters)
{
	// 裸实例就够：这几个方法都不碰世界（NewObject 不会调 Initialize）。
	UCosmosSimulationSubsystem* Subsystem = NewObject<UCosmosSimulationSubsystem>();
	if (!TestNotNull(TEXT("子系统裸实例"), Subsystem))
	{
		return false;
	}

	// ── 选中状态往返 ──
	TestTrue(TEXT("初始未选中"), Subsystem->GetSelectedBody() == nullptr);

	AActor* Body = NewObject<AActor>();
	Subsystem->SetSelectedBody(Body);
	TestTrue(TEXT("选中往返"), Subsystem->GetSelectedBody() == Body);

	Subsystem->SetSelectedBody(nullptr);
	TestTrue(TEXT("取消选中"), Subsystem->GetSelectedBody() == nullptr);

	// ── 星图激活标志 ──
	TestFalse(TEXT("初始星图未激活"), Subsystem->IsStarMapActive());
	Subsystem->SetStarMapActive(true);
	TestTrue(TEXT("设上后激活"), Subsystem->IsStarMapActive());
	Subsystem->SetStarMapActive(false);
	TestFalse(TEXT("清掉后不激活"), Subsystem->IsStarMapActive());

	// ── 天体枚举 ──
	{
		TArray<AActor*> Out;
		Out.Add(Body);            // 先塞一个脏元素，验 Out 会被 Reset
		Subsystem->GetRegisteredBodies(Out);
		TestEqual(TEXT("没注册天体时枚举为空"), Out.Num(), 0);
	}

	AActor* PlanetActor = NewObject<AActor>();
	UCosmosOrbitComponent* Orbit = NewObject<UCosmosOrbitComponent>(PlanetActor);
	Subsystem->RegisterBody(Orbit);

	// 一个 Outer 不是 Actor 的组件：GetOwner() 返回 nullptr，应当被跳过。
	UCosmosOrbitComponent* Orphan = NewObject<UCosmosOrbitComponent>();
	Subsystem->RegisterBody(Orphan);

	// nullptr 注册是空操作，不该进表。
	Subsystem->RegisterBody(nullptr);

	{
		TArray<AActor*> Out;
		Out.Add(Body);            // 脏元素
		Subsystem->GetRegisteredBodies(Out);
		TestEqual(TEXT("枚举到 1 个（跳过没有 Owner 的组件与 nullptr）"), Out.Num(), 1);
		TestTrue(TEXT("枚举到的是组件的 Owner"), Out.Num() == 1 && Out[0] == PlanetActor);
	}

	Subsystem->UnregisterBody(Orbit);
	{
		TArray<AActor*> Out;
		Subsystem->GetRegisteredBodies(Out);
		TestEqual(TEXT("注销后枚举为空"), Out.Num(), 0);
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
