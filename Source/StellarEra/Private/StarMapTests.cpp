// Copyright Voxel Plugin SAS. All Rights Reserved.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#include "CosmosOrbitComponent.h"
#include "CosmosSimulationSubsystem.h"
#include "CosmosTrajectoryComponent.h"

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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraStarMapHighlightTest,
	"StellarEra.StarMap.Highlight",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraStarMapHighlightTest::RunTest(const FString& Parameters)
{
	// 具体数值的来源：T = 0.45f 的 float32 值是 0.44999998807907104，
	// 结果 = In.C + T × (255 − In.C)，再按 FMath::RoundToInt32 取整。
	//   90 + 0.45 × 165 = 164.249998 → 164
	//  170 + 0.45 ×  85 = 208.249999 → 208
	//   20 + 0.45 × 235 = 125.749997 → 126   ← 截断会得 125，所以取整是必需的
	const FColor Prediction(90, 170, 255);
	const FColor Trail(20, 50, 110);

	// ── Amount = 0 → 原样 ──
	{
		const FColor Result = UCosmosTrajectoryComponent::BrightenTowardWhite(Prediction, 0.f);
		TestTrue(TEXT("0：R 不变"), Result.R == Prediction.R);
		TestTrue(TEXT("0：G 不变"), Result.G == Prediction.G);
		TestTrue(TEXT("0：B 不变"), Result.B == Prediction.B);
		TestTrue(TEXT("0：A 不变"), Result.A == Prediction.A);
	}

	// ── Amount = 1 → 纯白，alpha 保留 ──
	{
		const FColor Result = UCosmosTrajectoryComponent::BrightenTowardWhite(FColor(20, 50, 110, 128), 1.f);
		TestTrue(TEXT("1：R 是 255"), Result.R == 255);
		TestTrue(TEXT("1：G 是 255"), Result.G == 255);
		TestTrue(TEXT("1：B 是 255"), Result.B == 255);
		TestTrue(TEXT("1：alpha 保留"), Result.A == 128);
	}

	// ── Amount = 0.45 → 具体数值 ──
	{
		const FColor Result = UCosmosTrajectoryComponent::BrightenTowardWhite(Prediction, 0.45f);
		TestEqual(TEXT("预测色 0.45：R"), int32(Result.R), 164);
		TestEqual(TEXT("预测色 0.45：G"), int32(Result.G), 208);
		TestEqual(TEXT("预测色 0.45：B"), int32(Result.B), 255);
	}
	{
		const FColor Result = UCosmosTrajectoryComponent::BrightenTowardWhite(Trail, 0.45f);
		TestEqual(TEXT("拖尾色 0.45：R"), int32(Result.R), 126);
		TestEqual(TEXT("拖尾色 0.45：G"), int32(Result.G), 142);
		TestEqual(TEXT("拖尾色 0.45：B"), int32(Result.B), 175);
	}

	// ── 单调不减：每一档都只会更亮，不会更暗 ──
	{
		const FColor Low = UCosmosTrajectoryComponent::BrightenTowardWhite(Trail, 0.2f);
		const FColor High = UCosmosTrajectoryComponent::BrightenTowardWhite(Trail, 0.8f);
		TestTrue(TEXT("0.2 ≥ 原色：R"), Low.R >= Trail.R);
		TestTrue(TEXT("0.2 ≥ 原色：G"), Low.G >= Trail.G);
		TestTrue(TEXT("0.2 ≥ 原色：B"), Low.B >= Trail.B);
		TestTrue(TEXT("0.8 ≥ 0.2：R"), High.R >= Low.R);
		TestTrue(TEXT("0.8 ≥ 0.2：G"), High.G >= Low.G);
		TestTrue(TEXT("0.8 ≥ 0.2：B"), High.B >= Low.B);
	}

	// ── 越界输入被夹住，不炸 ──
	{
		const FColor Neg = UCosmosTrajectoryComponent::BrightenTowardWhite(Trail, -3.f);
		const FColor Over = UCosmosTrajectoryComponent::BrightenTowardWhite(Trail, 7.f);
		TestTrue(TEXT("-3 等同于 0"), Neg.R == Trail.R && Neg.G == Trail.G && Neg.B == Trail.B);
		TestTrue(TEXT("7 等同于 1"), Over.R == 255 && Over.G == 255 && Over.B == 255);
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
