// Copyright Voxel Plugin SAS. All Rights Reserved.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"

#include "CosmosOrbitComponent.h"
#include "CosmosSimulationSubsystem.h"
#include "CosmosTrajectoryComponent.h"
#include "Components/ChildActorComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "PlanetCharacter.h"
#include "PlanetEditComponent.h"
#include "SpaceshipPawn.h"
#include "StarMapCameraPawn.h"
#include "StellarEraPlayerController.h"

#include "BlueprintComponentTestHelpers.h"

#include <limits>

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

// 绕行灵敏度随「离焦点表面的高度 / 表面半径」缩放。相机绕的是焦点中心：贴地时
// 同样的鼠标位移对画面的影响必须很小（用户原话「一不小心就移过了」），拉远看整个
// 星系时则要一拖就扫过去（「鼠标很难移动星球」）。所以倍率跨到 1 以上，两端各夹
// 一个可调上限。数值都在 double 上，用 TestEqual 的浮点容差重载（现有约定）。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraStarMapOrbitLookScaleTest,
	"StellarEra.StarMap.OrbitLookScale",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraStarMapOrbitLookScaleTest::RunTest(const FString& Parameters)
{
	constexpr double Tolerance = 1e-9;
	constexpr double Min = 0.05;
	constexpr double Max = 8.0;

	// ── 小天体（飞船/角色）：参考高度按 5 米兜底，5 米处仍是 0.9 ──
	// 这条钉住「贴着飞船进星图不会变成一碰就飞」。
	TestEqual(TEXT("飞船焦点 5 米、碰撞半径 0.5 米"),
		AStarMapCameraPawn::OrbitLookScale(500.0, 50.0, Min, Max), 0.9, Tolerance);

	// ── 行星：2R 处高度 = 半径，倍率 1；再远按比例增长 ──
	TestEqual(TEXT("距离 = 2R：倍率 1"),
		AStarMapCameraPawn::OrbitLookScale(2.0e6, 1.0e6, Min, Max), 1.0, Tolerance);
	TestEqual(TEXT("距离 = 3R（整颗入画）：倍率 2"),
		AStarMapCameraPawn::OrbitLookScale(3.0e6, 1.0e6, Min, Max), 2.0, Tolerance);
	TestEqual(TEXT("距离 = 4R：倍率 3"),
		AStarMapCameraPawn::OrbitLookScale(4.0e6, 1.0e6, Min, Max), 3.0, Tolerance);

	// ── 远距离封顶：10R 时原始倍率 9，夹到 Max ──
	TestEqual(TEXT("距离 = 10R：夹到上限"),
		AStarMapCameraPawn::OrbitLookScale(1.0e7, 1.0e6, Min, Max), 8.0, Tolerance);
	TestEqual(TEXT("距离 50 AU：仍夹在上限"),
		AStarMapCameraPawn::OrbitLookScale(5.0e7, 1.0e6, Min, Max), 8.0, Tolerance);

	// ── 贴地下限（MinSurfaceAltitudeCm = 5000 厘米）：高度比 0.005，夹到下限 ──
	TestEqual(TEXT("贴地 50 米：夹到下限"),
		AStarMapCameraPawn::OrbitLookScale(1.005e6, 1.0e6, Min, Max), 0.05, Tolerance);
	TestEqual(TEXT("高度 0：夹到下限"),
		AStarMapCameraPawn::OrbitLookScale(1.0e6, 1.0e6, Min, Max), 0.05, Tolerance);

	// ── 还没选中过焦点（OrbitDistanceCm 哨兵 0）：不缩放，也躲开除零 ──
	TestEqual(TEXT("距离 0：不缩放"),
		AStarMapCameraPawn::OrbitLookScale(0.0, 1.0e6, Min, Max), 1.0, Tolerance);
	TestEqual(TEXT("距离为负：不缩放"),
		AStarMapCameraPawn::OrbitLookScale(-1.0, 1.0e6, Min, Max), 1.0, Tolerance);

	// ── 表面半径非正（没有碰撞几何的 Actor）：不缩放 ──
	TestEqual(TEXT("半径 0：不缩放"),
		AStarMapCameraPawn::OrbitLookScale(1.0e6, 0.0, Min, Max), 1.0, Tolerance);

	// ── 参数越界：下限被上限压住（Min > Max 时返回上限，不能把反向区间喂给 Clamp）──
	TestEqual(TEXT("下限 > 上限：按下限被压到上限"),
		AStarMapCameraPawn::OrbitLookScale(3.0e6, 1.0e6, 5.0, 2.0), 2.0, Tolerance);
	// 下限为负夹到 0：高度 0 时得到精确的 0。
	TestEqual(TEXT("下限 < 0：夹到 0"),
		AStarMapCameraPawn::OrbitLookScale(1.0e6, 1.0e6, -1.0, Max), 0.0, Tolerance);
	// 上限为负夹到 0：倍率整体为 0（调用方不该这么配，但函数必须确定）。
	TestEqual(TEXT("上限 < 0：倍率 0"),
		AStarMapCameraPawn::OrbitLookScale(3.0e6, 1.0e6, Min, -1.0), 0.0, Tolerance);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraStarMapVelocityQueryTest,
	"StellarEra.StarMap.VelocityQuery",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraStarMapVelocityQueryTest::RunTest(const FString& Parameters)
{
	// 裸实例就够：这个接口只转发给求解器，不碰世界（NewObject 不会调 Initialize）。
	UCosmosSimulationSubsystem* Subsystem = NewObject<UCosmosSimulationSubsystem>();
	if (!TestNotNull(TEXT("子系统裸实例"), Subsystem))
	{
		return false;
	}

	// 求解器里一颗天体都没有 —— 每个下标都必须安全返回零向量。
	// 这一层守卫是本接口唯一的逻辑：求解器的 GetBody 内部是裸的 Bodies[Index]，
	// 越界不报错、直接是 UB，所以下标校验必须在这里做。
	TestTrue(TEXT("INDEX_NONE 返回零"),
		Subsystem->GetBodyVelocityGameUnits(INDEX_NONE).IsNearlyZero());
	TestTrue(TEXT("负下标返回零"),
		Subsystem->GetBodyVelocityGameUnits(-1).IsNearlyZero());
	TestTrue(TEXT("0 号越界返回零"),
		Subsystem->GetBodyVelocityGameUnits(0).IsNearlyZero());
	TestTrue(TEXT("大下标越界返回零"),
		Subsystem->GetBodyVelocityGameUnits(9999).IsNearlyZero());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraStarMapFocusTargetTest,
	"StellarEra.StarMap.FocusTarget",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraStarMapFocusTargetTest::RunTest(const FString& Parameters)
{
	// 进入星图时的焦点选择。步行路径是这次要盯住的那条：角色按 M 时若没有
	// 焦点，星图相机停在角色眼位（CalcCamera 冻结），星图 UI 又只画选中/悬停，
	// 画面与普通视角完全一样 —— 玩家会以为 M 键没生效。
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

	APawn* OtherPawn = World->SpawnActor<APawn>(
		APawn::StaticClass(), FTransform::Identity);
	ASpaceshipPawn* NearShip = World->SpawnActor<ASpaceshipPawn>(
		ASpaceshipPawn::StaticClass(),
		FTransform(FRotator::ZeroRotator, FVector(500.0, 0.0, 0.0)));
	ASpaceshipPawn* FarShip = World->SpawnActor<ASpaceshipPawn>(
		ASpaceshipPawn::StaticClass(),
		FTransform(FRotator::ZeroRotator, FVector(50000.0, 0.0, 0.0)));
	APlanetCharacter* Character = World->SpawnActor<APlanetCharacter>(
		APlanetCharacter::StaticClass(), FTransform::Identity);

	if (!TestNotNull(TEXT("其他 Pawn"), OtherPawn) ||
		!TestNotNull(TEXT("近处飞船"), NearShip) ||
		!TestNotNull(TEXT("远处飞船"), FarShip) ||
		!TestNotNull(TEXT("角色"), Character))
	{
		return false;
	}

	// ── 驾驶中：焦点就是当前飞船 ──
	TestTrue(TEXT("驾驶中 → 当前飞船"),
		AStellarEraPlayerController::ResolveStarMapFocusShip(NearShip) == NearShip);

	// ── 步行：取角色最近的飞船，而不是随机一艘 ──
	TestTrue(TEXT("步行 → 最近的飞船"),
		AStellarEraPlayerController::ResolveStarMapFocusShip(Character) == NearShip);

	// ── 其他 Pawn / 空：没有焦点（相机保持冻结）──
	TestTrue(TEXT("其他 Pawn → 无焦点"),
		AStellarEraPlayerController::ResolveStarMapFocusShip(OtherPawn) == nullptr);
	TestTrue(TEXT("空 Pawn → 无焦点"),
		AStellarEraPlayerController::ResolveStarMapFocusShip(nullptr) == nullptr);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraStarMapZoomLimitsTest,
	"StellarEra.StarMap.ZoomLimits",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraStarMapZoomLimitsTest::RunTest(const FString& Parameters)
{
	// 滚轮缩放的是「相机到地表的高度」，不是到球心的距离；拉远被视线上的行星
	// 挡住时，相机停在穿入点之前。这两条都是纯函数，不需要世界。

	constexpr double Tolerance = 1e-6;

	// ── 高度缩放：同一倍率，近处一格几十米、远处一格几个 AU ──
	TestEqual(TEXT("远处拉近一格按 1.15 缩放"),
		AStarMapCameraPawn::ZoomAltitudeCm(1.0e7, 1.15, 1.f, 5000.0, 5.0e7),
		1.0e7 / 1.15, Tolerance);
	TestEqual(TEXT("近处拉近一格同样按 1.15 缩放"),
		AStarMapCameraPawn::ZoomAltitudeCm(2.0e4, 1.15, 1.f, 5000.0, 5.0e7),
		2.0e4 / 1.15, Tolerance);

	// 贴地时滚轮停在下限：不会穿进地表。
	TestEqual(TEXT("滚到下限就停住"),
		AStarMapCameraPawn::ZoomAltitudeCm(5000.0, 1.15, 1.f, 5000.0, 5.0e7),
		5000.0, Tolerance);
	TestEqual(TEXT("拉远一格高度变大"),
		AStarMapCameraPawn::ZoomAltitudeCm(5000.0, 1.15, -1.f, 5000.0, 5.0e7),
		5750.0, Tolerance);
	// 越过全局上限就停住。
	TestEqual(TEXT("拉远上限"),
		AStarMapCameraPawn::ZoomAltitudeCm(4.9e7, 1.15, -3.f, 5000.0, 5.0e7),
		5.0e7, Tolerance);

	// ── 射线-球：行星挡在拉远路径上时，相机停在表面之前 ──
	// 焦点在原点，行星球心 (1.5e6, 0, 0)、半径 1e6 → 穿入点在 0.5e6。
	const FVector FocusLocation = FVector::ZeroVector;
	const FVector RayDirection = FVector::ForwardVector;

	TestEqual(TEXT("穿入距离"),
		AStarMapCameraPawn::RaySphereFirstHitCm(
			FocusLocation, RayDirection, FVector(1.5e6, 0.0, 0.0), 1.0e6),
		5.0e5, 1.0);

	TArray<FStarMapObstacle> Blocking;
	{
		FStarMapObstacle Obstacle;
		Obstacle.Center = FVector(1.5e6, 0.0, 0.0);
		Obstacle.RadiusCm = 1.0e6;
		Blocking.Add(Obstacle);
	}

	TestEqual(TEXT("拉远上限被行星压到穿入点前 50 米"),
		AStarMapCameraPawn::LimitDistanceByObstaclesCm(
			FocusLocation, RayDirection, Blocking, 5000.0, 100.0, 5.0e7),
		5.0e5 - 5000.0, 1.0);

	// 球心偏移超过半径：射线擦不到，不限制。
	TestEqual(TEXT("擦不到的行星不限制"),
		AStarMapCameraPawn::LimitDistanceByObstaclesCm(
			FocusLocation, RayDirection,
			TArray<FStarMapObstacle>{
				FStarMapObstacle{ FVector(1.5e6, 1.0e6 + 1.0, 0.0), 1.0e6 } },
			5000.0, 100.0, 5.0e7),
		5.0e7, Tolerance);

	// 球在射线背后：相机越拉越远也碰不到它。
	TestEqual(TEXT("背后的行星不限制"),
		AStarMapCameraPawn::LimitDistanceByObstaclesCm(
			FocusLocation, RayDirection,
			TArray<FStarMapObstacle>{
				FStarMapObstacle{ FVector(-1.5e6, 0.0, 0.0), 1.0e6 } },
			5000.0, 100.0, 5.0e7),
		5.0e7, Tolerance);

	// 起点在球内（焦点贴在山里）：返回 -1，不给约束 —— 返回 0 会把缩放锁死。
	TestEqual(TEXT("起点在球内返回 -1"),
		AStarMapCameraPawn::RaySphereFirstHitCm(
			FVector::ZeroVector, RayDirection, FVector(1.0e6, 0.0, 0.0), 2.0e6),
		-1.0, Tolerance);

	// 障碍物比下限还近：上限不能低于下限，否则 Clamp 的区间会反转。
	TestEqual(TEXT("障碍物比下限更近时保下限"),
		AStarMapCameraPawn::LimitDistanceByObstaclesCm(
			FocusLocation, RayDirection,
			TArray<FStarMapObstacle>{
				FStarMapObstacle{ FVector(1.0e6 + 1000.0, 0.0, 0.0), 1.0e6 } },
			5000.0, 2.0e4, 5.0e7),
		2.0e4, Tolerance);

	return true;
}

// 星图 Pawn 的编辑组件由 BP_StarMapCameraPawn 挂载（2026-10-31 从构造函数移出），
// 笔刷参数在蓝图的类默认值里，所以这里查蓝图 SCS 里的组件模板（不是 CDO ——
// 蓝图挂的组件不在 CDO 上，见 BlueprintComponentTestHelpers.h）。
// 这一项钉的是星图笔刷的四个可调参数（默认 64、区间 8..1280、
// Shift+左键按住连续挖 / Ctrl+左键填、50 AU）：测试替 HUD 与输入层看着它们。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraStarMapEditComponentDefaultsTest,
	"StellarEra.StarMap.EditComponentDefaults",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraStarMapEditComponentDefaultsTest::RunTest(const FString& Parameters)
{
	const UPlanetEditComponent* Edit =
		Cast<UPlanetEditComponent>(
			StellarEraTest::FindBlueprintComponentTemplate(
				TEXT("/Game/Code/Pawns/BP_StarMapCameraPawn.BP_StarMapCameraPawn"),
				UPlanetEditComponent::StaticClass()));
	TestNotNull(TEXT("BP_StarMapCameraPawn 蓝图里挂着 PlanetEdit"), Edit);
	if (!Edit)
	{
		return false;
	}

	TestEqual(TEXT("默认 64 体素"), Edit->GetBrushSizeVoxels(), 64);
	TestEqual(TEXT("下限 8 体素"), Edit->GetMinBrushSizeVoxels(), 8);
	TestEqual(TEXT("上限 1280 体素"), Edit->GetMaxBrushSizeVoxels(), 1280);
	TestEqual(TEXT("一档 10 体素"), Edit->GetBrushSizeStepVoxels(), 10);
	TestEqual(TEXT("提示含连续挖与 Ctrl 填"), Edit->GetPlaceActionHint(),
		FString(TEXT("Shift+左键按住连续挖 · Ctrl+左键填")));
	TestTrue(TEXT("编辑距离 50 AU"),
		FMath::IsNearlyEqual(Edit->MaxEditDistanceCm, 5.0e7f));
	TestTrue(TEXT("大笔刷走并行"), Edit->bMultiThreadedEdits);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraStarMapBlueprintPawnTest,
	"StellarEra.StarMap.BlueprintPawn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraStarMapBlueprintPawnTest::RunTest(const FString& Parameters)
{
	// 游戏里 Spawn 的是蓝图子类，不是原生 CDO：只断言不变量，不钉具体数字，
	// 这样用户在细节面板里把默认直径从 64 改成别的也不会把测试弄红。
	const UPlanetEditComponent* Edit =
		Cast<UPlanetEditComponent>(
			StellarEraTest::FindBlueprintComponentTemplate(
				TEXT("/Game/Code/Pawns/BP_StarMapCameraPawn.BP_StarMapCameraPawn"),
				UPlanetEditComponent::StaticClass()));
	if (!TestNotNull(TEXT("BP_StarMapCameraPawn 蓝图里挂着 PlanetEdit"), Edit))
	{
		return false;
	}

	const int32 Size = Edit->GetBrushSizeVoxels();
	TestTrue(TEXT("默认直径落在区间内"),
		Size >= Edit->GetMinBrushSizeVoxels() && Size <= Edit->GetMaxBrushSizeVoxels());
	TestTrue(TEXT("提示串写的是 Ctrl 填"),
		Edit->GetPlaceActionHint().Contains(TEXT("Ctrl+左键填")));
	TestTrue(TEXT("编辑距离覆盖到 50 AU 量级"), Edit->MaxEditDistanceCm > 1.0e7f);
	TestTrue(TEXT("体素编辑走多线程"), Edit->bMultiThreadedEdits);

	return true;
}

// 星图里左 / 右 / 中三个键各管什么。纯函数，OnLook 调它，测试直接调它。
//
// 右键在本项目里是「转头」的通用键。星图里它改的是**视线朝向**而不是绕行位置
// （见 AStarMapCameraPawn::AimLook）：锁定天体后画面不再死盯球心，但相机到焦点的
// 距离与绕行方向都不变。
//
// 左键默认绕行；编辑模式 / 矿物透视把左键借走（挖掘填充 / 图例）之后，绕行改由
// 中键承担 —— 否则这两种状态下就没有绕行的键了。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraStarMapLookDispatchTest,
	"StellarEra.StarMap.LookDispatch",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraStarMapLookDispatchTest::RunTest(const FString& Parameters)
{
	using FController = AStellarEraPlayerController;
	using EMode = EStarMapLookMode;

	// 参数顺序：(右键, 左键, 中键, 编辑模式, 连续挖, 矿物透视)
	auto Resolve = [](bool bRight, bool bLeft, bool bMiddle, bool bEdit, bool bDig, bool bMineral)
	{
		return FController::ResolveStarMapLookMode(bRight, bLeft, bMiddle, bEdit, bDig, bMineral);
	};

	// ── 普通星图：左键绕行、右键转头、中键也绕行 ──
	TestTrue(TEXT("普通星图 左键绕行"), Resolve(false, true, false, false, false, false) == EMode::Orbit);
	TestTrue(TEXT("普通星图 右键转头"), Resolve(true, false, false, false, false, false) == EMode::Aim);
	TestTrue(TEXT("普通星图 中键绕行"), Resolve(false, false, true, false, false, false) == EMode::Orbit);

	// ── 编辑模式：左键归挖掘 / 填充，绕行改由中键，右键仍转头 ──
	TestTrue(TEXT("编辑模式 左键不转"), Resolve(false, true, false, true, false, false) == EMode::None);
	TestTrue(TEXT("编辑模式 右键转头"), Resolve(true, false, false, true, false, false) == EMode::Aim);
	TestTrue(TEXT("编辑模式 中键绕行"), Resolve(false, false, true, true, false, false) == EMode::Orbit);

	// ── 矿物透视：左键归图例，与编辑模式同一套门 ──
	TestTrue(TEXT("矿物透视 左键不转"), Resolve(false, true, false, false, false, true) == EMode::None);
	TestTrue(TEXT("矿物透视 右键转头"), Resolve(true, false, false, false, false, true) == EMode::Aim);
	TestTrue(TEXT("矿物透视 中键绕行"), Resolve(false, false, true, false, false, true) == EMode::Orbit);

	// ── 连续挖：一个键都不放行 —— 挖掘点跟着光标走，视角一动路径就画歪 ──
	TestTrue(TEXT("连续挖 左键不转"), Resolve(false, true, false, true, true, false) == EMode::None);
	TestTrue(TEXT("连续挖 右键不转头"), Resolve(true, false, false, true, true, false) == EMode::None);
	TestTrue(TEXT("连续挖 中键不转"), Resolve(false, false, true, true, true, false) == EMode::None);

	// ── 同时按住：右键优先（转头不改变位置，比绕行更「安全」）──
	TestTrue(TEXT("左右同按 右键优先"), Resolve(true, true, false, false, false, false) == EMode::Aim);
	TestTrue(TEXT("右中同按 右键优先"), Resolve(true, false, true, false, false, false) == EMode::Aim);

	// ── 什么都没按：不动 ──
	TestTrue(TEXT("没按键 不动"), Resolve(false, false, false, false, false, false) == EMode::None);

	return true;
}

// 转头偏移的圆锥夹取。OrbitRotation 决定相机位置，AimOffset 只决定朝向：
// 合成后的视线方向必须落在「相机 → 焦点」方向的 MaxAimOffsetDeg 圆锥内，
// 否则玩家能把镜头转到背后，绕行位置与画面对不上。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraStarMapAimConeTest,
	"StellarEra.StarMap.AimCone",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraStarMapAimConeTest::RunTest(const FString& Parameters)
{
	constexpr double Tolerance = 1e-6;

	/** 两个朝向之间的夹角（度）。 */
	auto AngleBetween = [](const FRotator& A, const FRotator& B)
	{
		const double Dot = FMath::Clamp(
			double(FVector::DotProduct(A.Vector(), B.Vector())), -1.0, 1.0);
		return FMath::RadiansToDegrees(FMath::Acos(Dot));
	};

	// 基准朝向：看向 +X（yaw 0 / pitch 0 / roll 0）。
	const FRotator Base = FRotator::ZeroRotator;
	constexpr double MaxDeg = 90.0;

	// ── 不超界：原样合成，一点都不夹 ──
	{
		const FRotator Out = AStarMapCameraPawn::ClampAimOffset(Base, FRotator(0.0, 30.0, 0.0), MaxDeg);
		TestEqual(TEXT("30° 偏航不夹取 yaw"), Out.Yaw, 30.0, Tolerance);
		TestEqual(TEXT("30° 偏航不夹取 pitch"), Out.Pitch, 0.0, Tolerance);
	}

	// ── 超界：压回锥面（方向夹角恰好等于上限）──
	{
		const FRotator Out = AStarMapCameraPawn::ClampAimOffset(Base, FRotator(0.0, 120.0, 0.0), MaxDeg);
		TestEqual(TEXT("120° 偏航夹到 90°"), AngleBetween(Out, Base), 90.0, Tolerance);
		TestEqual(TEXT("夹取后只剩偏航"), Out.Pitch, 0.0, Tolerance);
	}

	// 俯仰方向单独超界同样要夹。
	{
		const FRotator Out = AStarMapCameraPawn::ClampAimOffset(Base, FRotator(120.0, 0.0, 0.0), MaxDeg);
		TestEqual(TEXT("120° 俯仰夹到 90°"), AngleBetween(Out, Base), 90.0, Tolerance);
	}

	// 斜向超界：俯仰与偏航同时给大值时，合成方向实际是 104.5°，同样要压回 90°。
	{
		const FRotator Out = AStarMapCameraPawn::ClampAimOffset(Base, FRotator(60.0, 120.0, 0.0), MaxDeg);
		TestEqual(TEXT("斜向 104.5° 夹到 90°"), AngleBetween(Out, Base), 90.0, Tolerance);
	}

	// 越过天顶的偏移（pitch 100°）合成出来只有 80° —— 说明夹取按**合成后的夹角**
	// 判，不是逐轴判：逐轴版会先把 pitch 压到 90、把 yaw 压到 180 以内的另一支，
	// 得到的方向根本不一样。
	{
		const FRotator Out = AStarMapCameraPawn::ClampAimOffset(Base, FRotator(100.0, 180.0, 0.0), MaxDeg);
		TestEqual(TEXT("pitch 100°/yaw 180° 合成后仅 80°，不夹取"),
			AngleBetween(Out, Base), 80.0, Tolerance);
	}

	// ── 边界：恰好等于上限不夹 ──
	{
		const FRotator Out = AStarMapCameraPawn::ClampAimOffset(Base, FRotator(0.0, 90.0, 0.0), MaxDeg);
		TestEqual(TEXT("恰好 90° 不夹取"), Out.Yaw, 90.0, Tolerance);
	}

	// ── 上限 0：退化成品，原样返回基准朝向 ──
	{
		const FRotator Out = AStarMapCameraPawn::ClampAimOffset(Base, FRotator(0.0, 30.0, 0.0), 0.0);
		TestTrue(TEXT("上限 0 返回基准朝向"), Out.Equals(Base, Tolerance));
	}

	// ── NaN：按「没有偏移」处理，绝不能把 NaN 写进 OutResult.Rotation ──
	{
		const FRotator Out = AStarMapCameraPawn::ClampAimOffset(
			Base, FRotator(0.0, std::numeric_limits<double>::quiet_NaN(), 0.0), MaxDeg);
		TestTrue(TEXT("NaN 偏航退回基准朝向"), Out.Equals(Base, Tolerance));
	}

	// ── 基准朝向非零：夹的是相对基准的夹角，不是世界轴 ──
	{
		const FRotator Tilted(0.0, 45.0, 0.0);
		const FRotator Out = AStarMapCameraPawn::ClampAimOffset(Tilted, FRotator(0.0, 150.0, 0.0), MaxDeg);
		TestEqual(TEXT("非零基准 夹到 90°"), AngleBetween(Out, Tilted), 90.0, Tolerance);
	}

	// ── 夹取结果的 roll 恒为 0：星图相机不允许翻滚 ──
	{
		const FRotator Out = AStarMapCameraPawn::ClampAimOffset(Base, FRotator(20.0, 20.0, 30.0), MaxDeg);
		TestEqual(TEXT("偏移里的 roll 被丢掉"), Out.Roll, 0.0, Tolerance);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraStarMapPlanetCameraTest,
	"StellarEra.StarMap.PlanetCamera",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraStarMapPlanetCameraTest::RunTest(const FString& Parameters)
{
	// B2 路线（2026-11-01，用户选定）：BP_VoxelPlanet 挂 Child Actor Component 当
	// 「自带相机」，控制器进图时优先用它（ResolvePlanetStarMapCamera）。解析链有
	// 三段要验：CAC 扫描（FindChildStarMapCamera）、附加 Actor 兜底扫描
	// （FindAttachedStarMapCamera）、以及 Pawn → 行星 → 相机的串联与优先级。
	// 行星本体不真生（体素组件太重）：扫描只读组件 / 附加关系与 Cast，
	// 「普通 Actor + CAC / 附加相机」就能把逻辑验满；串联的「解析不出行星」
	// 分支用没走 BeginPlay 的飞船验（编辑器世界 SpawnActor 只走 OnRegister）。

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

	// 裸 AActor 没有根组件，AttachToActor 会静默失败（附加关系建立不起来）。
	// 测试里的「行星 / 挂载物」统一用这个造：带一个已注册的 SceneComponent 当根。
	auto MakeActor = [World](const TCHAR* Label) -> AActor*
	{
		AActor* Actor = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity);
		if (!Actor)
		{
			return nullptr;
		}
		USceneComponent* Root = NewObject<USceneComponent>(Actor, TEXT("TestRoot"));
		Actor->SetRootComponent(Root);
		Root->RegisterComponent();
		UE_LOG(LogTemp, Verbose, TEXT("[test] 造 Actor %s"), Label);
		return Actor;
	};

	// ══ 一、CAC 扫描：BP_VoxelPlanet 上那台「自带相机」══

	// ── 空 Owner / 没挂 CAC：返回空，不是崩 ──
	TestTrue(TEXT("CAC: 空 Owner → 空"),
		AStellarEraPlayerController::FindChildStarMapCamera(nullptr) == nullptr);

	AActor* NoCacOwner = MakeActor(TEXT("没挂 CAC 的行星"));
	if (!TestNotNull(TEXT("没挂 CAC 的行星"), NoCacOwner))
	{
		return false;
	}
	TestTrue(TEXT("CAC: 没挂 CAC → 空"),
		AStellarEraPlayerController::FindChildStarMapCamera(NoCacOwner) == nullptr);

	// ── CAC 子类不是星图相机：不认 ──
	// AActor 是抽象类，用具体可生成的 APawn 当「错的子类」。
	AActor* WrongCacOwner = MakeActor(TEXT("子类错误的行星"));
	if (!TestNotNull(TEXT("子类错误的行星"), WrongCacOwner))
	{
		return false;
	}
	UChildActorComponent* WrongCac = NewObject<UChildActorComponent>(WrongCacOwner);
	WrongCac->SetChildActorClass(APawn::StaticClass());
	WrongCac->RegisterComponent();
	TestTrue(TEXT("CAC: 子类不是星图相机 → 空"),
		AStellarEraPlayerController::FindChildStarMapCamera(WrongCacOwner) == nullptr);

	// ── CAC 子类是星图相机：返回子 Actor，且归属是行星 Actor ──
	AActor* CacPlanet = MakeActor(TEXT("挂了 CAC 的行星"));
	if (!TestNotNull(TEXT("挂了 CAC 的行星"), CacPlanet))
	{
		return false;
	}
	UChildActorComponent* CameraCac = NewObject<UChildActorComponent>(CacPlanet);
	CameraCac->SetChildActorClass(AStarMapCameraPawn::StaticClass());
	CameraCac->RegisterComponent();

	AStarMapCameraPawn* ChildCamera =
		AStellarEraPlayerController::FindChildStarMapCamera(CacPlanet);
	if (!TestNotNull(TEXT("CAC: 挂了相机 → 解析到子相机"), ChildCamera))
	{
		return false;
	}
	// 注意：UChildActorComponent 构造里 bSetOwner=false，子 Actor 的 GetOwner() 就是空，
	// 只有 ParentComponent/ParentActor 指向宿主。别拿 GetOwner() 断言。
	TestTrue(TEXT("CAC: 解析到的就是该组件的子 Actor"),
		ChildCamera == Cast<AStarMapCameraPawn>(CameraCac->GetChildActor()));
	TestTrue(TEXT("CAC: 子相机的父 Actor 是行星 Actor"),
		ChildCamera->GetAttachParentActor() == CacPlanet);

	// ── 优先级：CAC 与手动附加相机同时存在时，取 CAC 的那台 ──
	// 过渡期真实情况 —— 三颗行星上既有新加的 CAC，也还留着旧的手动相机。
	AStarMapCameraPawn* LegacyCamera = World->SpawnActor<AStarMapCameraPawn>(
		AStarMapCameraPawn::StaticClass(), FTransform::Identity);
	if (!TestNotNull(TEXT("旧的手动相机"), LegacyCamera))
	{
		return false;
	}
	TestTrue(TEXT("旧相机挂到同一颗行星上"),
		LegacyCamera->AttachToActor(CacPlanet, FAttachmentTransformRules::KeepRelativeTransform));
	TestTrue(TEXT("CAC: 两种相机都在时优先取 CAC 的"),
		AStellarEraPlayerController::FindChildStarMapCamera(CacPlanet) == ChildCamera);

	// ══ 二、附加 Actor 兜底扫描：手动 Attach 的旧摆法 ══

	// ── 空 Owner / 没挂任何 Actor：返回空，不是崩 ──
	TestTrue(TEXT("附加: 空 Owner → 空"),
		AStellarEraPlayerController::FindAttachedStarMapCamera(nullptr) == nullptr);

	AActor* Plain = MakeActor(TEXT("普通 Actor"));
	if (!TestNotNull(TEXT("普通 Actor"), Plain))
	{
		return false;
	}
	TestTrue(TEXT("没挂附加 Actor → 空"),
		AStellarEraPlayerController::FindAttachedStarMapCamera(Plain) == nullptr);

	// ── 挂了东西但不是星图相机：不认 ──
	AActor* WrongOwner = MakeActor(TEXT("错误子类 Owner"));
	AActor* WrongChild = MakeActor(TEXT("错误子类 Child"));
	if (!TestNotNull(TEXT("错误子类 Owner"), WrongOwner) ||
		!TestNotNull(TEXT("错误子类 Child"), WrongChild))
	{
		return false;
	}
	TestTrue(TEXT("非相机也能挂上去"),
		WrongChild->AttachToActor(WrongOwner, FAttachmentTransformRules::KeepRelativeTransform));
	TestTrue(TEXT("附加的不是星图相机 → 空"),
		AStellarEraPlayerController::FindAttachedStarMapCamera(WrongOwner) == nullptr);

	// ── 直接挂星图相机：返回它，且父 Actor 是行星 ──
	AActor* PlanetOwner = MakeActor(TEXT("行星 Owner"));
	if (!TestNotNull(TEXT("行星 Owner"), PlanetOwner))
	{
		return false;
	}
	AStarMapCameraPawn* AttachedCamera = World->SpawnActor<AStarMapCameraPawn>(
		AStarMapCameraPawn::StaticClass(), FTransform::Identity);
	if (!TestNotNull(TEXT("星图相机"), AttachedCamera))
	{
		return false;
	}
	TestTrue(TEXT("相机挂到行星上"),
		AttachedCamera->AttachToActor(PlanetOwner, FAttachmentTransformRules::KeepRelativeTransform));

	AStarMapCameraPawn* Resolved =
		AStellarEraPlayerController::FindAttachedStarMapCamera(PlanetOwner);
	if (!TestNotNull(TEXT("挂了星图相机 → 解析到它"), Resolved))
	{
		return false;
	}
	TestTrue(TEXT("解析到的就是那台相机"), Resolved == AttachedCamera);
	TestTrue(TEXT("相机的父 Actor 是行星"), Resolved->GetAttachParentActor() == PlanetOwner);

	// ── 相机挂在「行星的挂载物」上（隔一层）：不认，避免飞船/平台上挂的相机被当成行星相机 ──
	AActor* OuterPlanet = MakeActor(TEXT("外层行星"));
	AActor* Middle = MakeActor(TEXT("中间挂载物"));
	AStarMapCameraPawn* NestedCamera = World->SpawnActor<AStarMapCameraPawn>(
		AStarMapCameraPawn::StaticClass(), FTransform::Identity);
	if (!TestNotNull(TEXT("外层行星"), OuterPlanet) ||
		!TestNotNull(TEXT("中间挂载物"), Middle) ||
		!TestNotNull(TEXT("嵌套相机"), NestedCamera))
	{
		return false;
	}
	TestTrue(TEXT("挂载物挂到行星上"),
		Middle->AttachToActor(OuterPlanet, FAttachmentTransformRules::KeepRelativeTransform));
	TestTrue(TEXT("相机挂到挂载物上"),
		NestedCamera->AttachToActor(Middle, FAttachmentTransformRules::KeepRelativeTransform));
	TestTrue(TEXT("隔一层挂的相机 → 空（不递归）"),
		AStellarEraPlayerController::FindAttachedStarMapCamera(OuterPlanet) == nullptr);

	// ── 串联：进图 Pawn → 参考行星 → 相机 ──
	TestTrue(TEXT("空 Pawn → 无行星相机"),
		AStellarEraPlayerController::ResolvePlanetStarMapCamera(nullptr) == nullptr);

	ASpaceshipPawn* Ship = World->SpawnActor<ASpaceshipPawn>(
		ASpaceshipPawn::StaticClass(), FTransform::Identity);
	if (!TestNotNull(TEXT("飞船"), Ship))
	{
		return false;
	}
	// 编辑器世界不走 BeginPlay，PlanetActor 还是空 —— 正好覆盖「解析不出行星」
	// 的分支（深空进图 → 退回单例相机，见 ToggleStarMap 进入分支）。
	TestTrue(TEXT("飞船无参考行星 → 无行星相机"),
		AStellarEraPlayerController::ResolvePlanetStarMapCamera(Ship) == nullptr);

	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// 入场锚点：行星相机挂在球心，进星图前要沿「球心 → 当前视点」搬到球面附近。
// ─────────────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraStarMapEntryAnchorTest,
	"StellarEra.StarMap.EntryAnchor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraStarMapEntryAnchorTest::RunTest(const FString& Parameters)
{
	const FVector Center(1.0e7, -2.0e6, 3.0e5); // 不放在原点，顺带验证减法不是「按原点算」
	constexpr double Radius = 1.0e6;
	constexpr double Altitude = 1.0e5;
	constexpr double Tolerance = 1.0;

	// ── 方向：只取「球心 → 视点」的方向，与视点远近无关 ──
	{
		const FVector Anchor = AStarMapCameraPawn::ComputeSurfaceAnchorLocation(
			Center, Center + FVector(5.0e6, 0.0, 0.0), Radius, Altitude);
		TestEqual(TEXT("沿 +X：距离 = 半径 + 高度"),
			static_cast<double>((Anchor - Center).Size()), Radius + Altitude, Tolerance);
		TestTrue(TEXT("沿 +X：方向正确"),
			(Anchor - Center).GetSafeNormal().Equals(FVector::ForwardVector, 1e-6));
	}
	{
		// 视点离得极近（几乎贴着球心）方向仍要归一化，不能按距离缩放
		const FVector Anchor = AStarMapCameraPawn::ComputeSurfaceAnchorLocation(
			Center, Center + FVector(0.0, 1.0, 0.0), Radius, Altitude);
		TestEqual(TEXT("视点很近：距离仍 = 半径 + 高度"),
			static_cast<double>((Anchor - Center).Size()), Radius + Altitude, Tolerance);
	}

	// ── 高度：夹在 [0, 半径/4]，防止锚点浮到半空 ──
	{
		const FVector Anchor = AStarMapCameraPawn::ComputeSurfaceAnchorLocation(
			Center, Center + FVector(0.0, 0.0, 1.0e6), Radius, Radius);
		TestEqual(TEXT("高度超过半径/4 → 夹到半径/4"),
			static_cast<double>((Anchor - Center).Size()), Radius + Radius * 0.25, Tolerance);
	}
	{
		const FVector Anchor = AStarMapCameraPawn::ComputeSurfaceAnchorLocation(
			Center, Center + FVector(0.0, 0.0, 1.0e6), Radius, -5.0e5);
		TestEqual(TEXT("负高度 → 夹到 0"), static_cast<double>((Anchor - Center).Size()),
			Radius, Tolerance);
	}

	// ── 退化输入 ──
	{
		const FVector Anchor = AStarMapCameraPawn::ComputeSurfaceAnchorLocation(
			Center, Center, Radius, Altitude);
		TestTrue(TEXT("视点与球心重合 → 退回世界 +Z"),
			(Anchor - Center).GetSafeNormal().Equals(FVector::UpVector, 1e-6));
	}
	{
		const FVector Anchor = AStarMapCameraPawn::ComputeSurfaceAnchorLocation(
			Center, Center + FVector(1.0e6, 0.0, 0.0), 0.0, Altitude);
		TestTrue(TEXT("半径 <= 0 → 原样返回球心"), Anchor.Equals(Center));
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
