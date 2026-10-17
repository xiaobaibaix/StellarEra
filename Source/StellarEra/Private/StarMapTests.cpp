// Copyright Voxel Plugin SAS. All Rights Reserved.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#include "CosmosOrbitComponent.h"
#include "CosmosSimulationSubsystem.h"
#include "CosmosTrajectoryComponent.h"
#include "StarMapCameraPawn.h"

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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraStarMapPickingTest,
	"StellarEra.StarMap.Picking",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraStarMapPickingTest::RunTest(const FString& Parameters)
{
	// 拾取是纯数学，不碰世界、不碰碰撞 —— 所以能无头测。投影留在调用方。
	AActor* A = NewObject<AActor>();
	AActor* B = NewObject<AActor>();
	AActor* C = NewObject<AActor>();

	// ── 空列表 ──
	{
		TArray<FStarMapScreenCandidate> Empty;
		TestTrue(TEXT("空列表返回 nullptr"),
			AStarMapCameraPawn::PickBodyAtScreenPosition(
				Empty, FVector2D(100.0, 100.0), 20.f) == nullptr);
	}

	// ── 命中最近者 ──
	{
		TArray<FStarMapScreenCandidate> Candidates;
		Candidates.Add({ A, FVector2D(100.0, 100.0) });   // 距点 10
		Candidates.Add({ B, FVector2D(130.0, 100.0) });   // 距点 20（正好在边界上）
		Candidates.Add({ C, FVector2D(200.0, 100.0) });   // 距点 90，超出容差

		TestTrue(TEXT("取最近的（C 被容差挡掉，A 比 B 近）"),
			AStarMapCameraPawn::PickBodyAtScreenPosition(
				Candidates, FVector2D(110.0, 100.0), 20.f) == A);
	}

	// ── 边界含：恰好等于半径算命中，小一点点就不算 ──
	{
		TArray<FStarMapScreenCandidate> Candidates;
		Candidates.Add({ B, FVector2D(130.0, 100.0) });   // 与 (110,100) 的距离正好 20

		TestTrue(TEXT("距离恰好等于半径 → 命中（边界含）"),
			AStarMapCameraPawn::PickBodyAtScreenPosition(
				Candidates, FVector2D(110.0, 100.0), 20.f) == B);
		TestTrue(TEXT("半径小一点点 → 不命中"),
			AStarMapCameraPawn::PickBodyAtScreenPosition(
				Candidates, FVector2D(110.0, 100.0), 19.9f) == nullptr);
	}

	// ── 等距：取**先出现**的那个（换个顺序结果跟着换）──
	{
		TArray<FStarMapScreenCandidate> First;
		First.Add({ A, FVector2D(90.0, 100.0) });         // 距点 10
		First.Add({ B, FVector2D(110.0, 100.0) });        // 距点 10

		TArray<FStarMapScreenCandidate> Reversed;
		Reversed.Add({ B, FVector2D(110.0, 100.0) });
		Reversed.Add({ A, FVector2D(90.0, 100.0) });

		TestTrue(TEXT("等距取先出现的"),
			AStarMapCameraPawn::PickBodyAtScreenPosition(
				First, FVector2D(100.0, 100.0), 20.f) == A);
		TestTrue(TEXT("换序后取新的先出现者（证明它认的是顺序，不是 A）"),
			AStarMapCameraPawn::PickBodyAtScreenPosition(
				Reversed, FVector2D(100.0, 100.0), 20.f) == B);
	}

	// ── Actor 为空的候选被跳过，哪怕它更近 ──
	{
		TArray<FStarMapScreenCandidate> Candidates;
		Candidates.Add({ nullptr, FVector2D(100.0, 100.0) });   // 距点 0，但 Actor 为空
		Candidates.Add({ C, FVector2D(105.0, 100.0) });         // 距点 5

		TestTrue(TEXT("跳过 Actor 为空的候选"),
			AStarMapCameraPawn::PickBodyAtScreenPosition(
				Candidates, FVector2D(100.0, 100.0), 20.f) == C);
	}

	// ── 全都不在容差内 ──
	{
		TArray<FStarMapScreenCandidate> Candidates;
		Candidates.Add({ A, FVector2D(500.0, 500.0) });
		TestTrue(TEXT("都不在容差内返回 nullptr"),
			AStarMapCameraPawn::PickBodyAtScreenPosition(
				Candidates, FVector2D(0.0, 0.0), 20.f) == nullptr);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraStarMapPickingScreenRadiusTest,
	"StellarEra.StarMap.PickingScreenRadius",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraStarMapPickingScreenRadiusTest::RunTest(const FString& Parameters)
{
	// 容差 = max(固定像素容差, 该候选的屏幕投影半径)。这一组专门盯住
	// 「近处大盘面点得中」与「远处小目标的行为不变」两件事。
	AActor* A = NewObject<AActor>();
	AActor* B = NewObject<AActor>();

	// ── 投影半径放大容差：距中心 80 px，固定容差 20 够不着，半径 100 够得着 ──
	{
		TArray<FStarMapScreenCandidate> Candidates;
		Candidates.Add({ A, FVector2D(500.0, 300.0), 100.f });

		TestTrue(TEXT("距中心 80 px、盘面半径 100 → 命中（点在盘面上）"),
			AStarMapCameraPawn::PickBodyAtScreenPosition(
				Candidates, FVector2D(580.0, 300.0), 20.f) == A);
	}

	// ── 回归：半径为 0 时退回固定容差，旧行为不变 ──
	{
		TArray<FStarMapScreenCandidate> Candidates;
		Candidates.Add({ A, FVector2D(500.0, 300.0), 0.f });

		TestTrue(TEXT("半径为 0 时距中心 80 px 超出固定容差 → 不命中"),
			AStarMapCameraPawn::PickBodyAtScreenPosition(
				Candidates, FVector2D(580.0, 300.0), 20.f) == nullptr);
	}

	// ── 边界含：恰好等于投影半径算命中 ──
	{
		TArray<FStarMapScreenCandidate> Candidates;
		Candidates.Add({ A, FVector2D(500.0, 300.0), 100.f });

		TestTrue(TEXT("距离恰好等于投影半径 → 命中（边界含）"),
			AStarMapCameraPawn::PickBodyAtScreenPosition(
				Candidates, FVector2D(600.0, 300.0), 20.f) == A);
	}

	// ── 两个都在容差内：离点击**中心**最近的那个赢（不是盘面半径最大的） ──
	{
		TArray<FStarMapScreenCandidate> Candidates;
		Candidates.Add({ A, FVector2D(800.0, 300.0), 400.f });   // 中心距 300，但 300 < 400 在容差内
		Candidates.Add({ B, FVector2D(510.0, 300.0), 0.f });     // 中心距 10

		TestTrue(TEXT("中心更近的赢，哪怕另一颗盘面更大"),
			AStarMapCameraPawn::PickBodyAtScreenPosition(
				Candidates, FVector2D(500.0, 300.0), 20.f) == B);
	}

	// ── 负半径不放大容差、也不炸 ──
	{
		TArray<FStarMapScreenCandidate> Candidates;
		Candidates.Add({ A, FVector2D(500.0, 300.0), -5.f });

		TestTrue(TEXT("半径 -5 不放大容差：距中心 30 px → 不命中"),
			AStarMapCameraPawn::PickBodyAtScreenPosition(
				Candidates, FVector2D(530.0, 300.0), 20.f) == nullptr);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraStarMapSilhouetteScaleTest,
	"StellarEra.StarMap.SilhouetteRadiusScale",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraStarMapSilhouetteScaleTest::RunTest(const FString& Parameters)
{
	// 球体轮廓放大系数 1/cos α（sin α = R/d）。这是 double 不是指针，
	// 所以可以用 TestEqual 的浮点容差重载（指针才必须用 TestTrue）。
	constexpr double Tolerance = 1e-9;

	// ── d = 2R（本项目的最近距离就是 2 个行星半径）：α = 30°，系数 = 2/√3 ──
	TestEqual(TEXT("d=2R：1/cos30° = 2/√3"),
		AStarMapCameraPawn::SilhouetteRadiusScale(2.0, 1.0),
		2.0 / FMath::Sqrt(3.0), Tolerance);

	// ── d = 10R：几乎不用放大（0.5% 量级）──
	TestEqual(TEXT("d=10R：10/√99"),
		AStarMapCameraPawn::SilhouetteRadiusScale(10.0, 1.0),
		10.0 / FMath::Sqrt(99.0), Tolerance);

	// ── 相机贴着球面（d == R）：算不出来，返回 0 而不是无穷大 ──
	TestEqual(TEXT("d=R 返回 0"),
		AStarMapCameraPawn::SilhouetteRadiusScale(1.0, 1.0), 0.0, Tolerance);

	// ── 相机在球内：同样返回 0 ──
	TestEqual(TEXT("相机在球内返回 0"),
		AStarMapCameraPawn::SilhouetteRadiusScale(0.5, 1.0), 0.0, Tolerance);

	// ── 半径非正：返回 0 ──
	TestEqual(TEXT("半径非正返回 0"),
		AStarMapCameraPawn::SilhouetteRadiusScale(2.0, 0.0), 0.0, Tolerance);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
