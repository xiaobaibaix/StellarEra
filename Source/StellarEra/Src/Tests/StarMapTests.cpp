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
#include "StarMapCameraInputComponent.h"
#include "StarMapCameraPawn.h"
#include "StellarEraPlayerController.h"

#include "BlueprintComponentTestHelpers.h"

#include <limits>

#if WITH_DEV_AUTOMATION_TESTS

// 断言写法：指针一律用 TestTrue/TestFalse —— TestEqual 走 LexToString，指针没有可靠的 ToString。

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

	TestTrue(TEXT("初始未选中"), Subsystem->GetSelectedBody() == nullptr);

	AActor* Body = NewObject<AActor>();
	Subsystem->SetSelectedBody(Body);
	TestTrue(TEXT("选中往返"), Subsystem->GetSelectedBody() == Body);

	Subsystem->SetSelectedBody(nullptr);
	TestTrue(TEXT("取消选中"), Subsystem->GetSelectedBody() == nullptr);

	TestFalse(TEXT("初始星图未激活"), Subsystem->IsStarMapActive());
	Subsystem->SetStarMapActive(true);
	TestTrue(TEXT("设上后激活"), Subsystem->IsStarMapActive());
	Subsystem->SetStarMapActive(false);
	TestFalse(TEXT("清掉后不激活"), Subsystem->IsStarMapActive());

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
	// 数值来源：结果 = In.C + T × (255 − In.C) 再取整，T = 0.45f 实际是
	// 0.44999998807907104。截断会得 125，所以取整是必需的。
	const FColor Prediction(90, 170, 255);
	const FColor Trail(20, 50, 110);

	{
		const FColor Result = UCosmosTrajectoryComponent::BrightenTowardWhite(Prediction, 0.f);
		TestTrue(TEXT("0：R 不变"), Result.R == Prediction.R);
		TestTrue(TEXT("0：G 不变"), Result.G == Prediction.G);
		TestTrue(TEXT("0：B 不变"), Result.B == Prediction.B);
		TestTrue(TEXT("0：A 不变"), Result.A == Prediction.A);
	}

	{
		const FColor Result = UCosmosTrajectoryComponent::BrightenTowardWhite(FColor(20, 50, 110, 128), 1.f);
		TestTrue(TEXT("1：R 是 255"), Result.R == 255);
		TestTrue(TEXT("1：G 是 255"), Result.G == 255);
		TestTrue(TEXT("1：B 是 255"), Result.B == 255);
		TestTrue(TEXT("1：alpha 保留"), Result.A == 128);
	}

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

	{
		TArray<FStarMapScreenCandidate> Empty;
		TestTrue(TEXT("空列表返回 nullptr"),
			AStarMapCameraPawn::PickBodyAtScreenPosition(
				Empty, FVector2D(100.0, 100.0), 20.f) == nullptr);
	}

	{
		TArray<FStarMapScreenCandidate> Candidates;
		Candidates.Add({ A, FVector2D(100.0, 100.0) });   // 距点 10
		Candidates.Add({ B, FVector2D(130.0, 100.0) });   // 距点 20（正好在边界上）
		Candidates.Add({ C, FVector2D(200.0, 100.0) });   // 距点 90，超出容差

		TestTrue(TEXT("取最近的（C 被容差挡掉，A 比 B 近）"),
			AStarMapCameraPawn::PickBodyAtScreenPosition(
				Candidates, FVector2D(110.0, 100.0), 20.f) == A);
	}

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

	{
		TArray<FStarMapScreenCandidate> Candidates;
		Candidates.Add({ nullptr, FVector2D(100.0, 100.0) });   // 距点 0，但 Actor 为空
		Candidates.Add({ C, FVector2D(105.0, 100.0) });         // 距点 5

		TestTrue(TEXT("跳过 Actor 为空的候选"),
			AStarMapCameraPawn::PickBodyAtScreenPosition(
				Candidates, FVector2D(100.0, 100.0), 20.f) == C);
	}

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
	// 容差 = max(固定像素容差, 该候选的屏幕投影半径)：近处大盘面点得中、远处小目标行为不变。
	AActor* A = NewObject<AActor>();
	AActor* B = NewObject<AActor>();

	{
		TArray<FStarMapScreenCandidate> Candidates;
		Candidates.Add({ A, FVector2D(500.0, 300.0), 100.f });

		TestTrue(TEXT("距中心 80 px、盘面半径 100 → 命中（点在盘面上）"),
			AStarMapCameraPawn::PickBodyAtScreenPosition(
				Candidates, FVector2D(580.0, 300.0), 20.f) == A);
	}

	{
		TArray<FStarMapScreenCandidate> Candidates;
		Candidates.Add({ A, FVector2D(500.0, 300.0), 0.f });

		TestTrue(TEXT("半径为 0 时距中心 80 px 超出固定容差 → 不命中"),
			AStarMapCameraPawn::PickBodyAtScreenPosition(
				Candidates, FVector2D(580.0, 300.0), 20.f) == nullptr);
	}

	{
		TArray<FStarMapScreenCandidate> Candidates;
		Candidates.Add({ A, FVector2D(500.0, 300.0), 100.f });

		TestTrue(TEXT("距离恰好等于投影半径 → 命中（边界含）"),
			AStarMapCameraPawn::PickBodyAtScreenPosition(
				Candidates, FVector2D(600.0, 300.0), 20.f) == A);
	}

	{
		TArray<FStarMapScreenCandidate> Candidates;
		Candidates.Add({ A, FVector2D(800.0, 300.0), 400.f });   // 中心距 300，但 300 < 400 在容差内
		Candidates.Add({ B, FVector2D(510.0, 300.0), 0.f });     // 中心距 10

		TestTrue(TEXT("中心更近的赢，哪怕另一颗盘面更大"),
			AStarMapCameraPawn::PickBodyAtScreenPosition(
				Candidates, FVector2D(500.0, 300.0), 20.f) == B);
	}

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
	// 球体轮廓放大系数 1/cos α（sin α = R/d）。double 可用 TestEqual 浮点容差重载。
	constexpr double Tolerance = 1e-9;

	TestEqual(TEXT("d=2R：1/cos30° = 2/√3"),
		AStarMapCameraPawn::SilhouetteRadiusScale(2.0, 1.0),
		2.0 / FMath::Sqrt(3.0), Tolerance);

	TestEqual(TEXT("d=10R：10/√99"),
		AStarMapCameraPawn::SilhouetteRadiusScale(10.0, 1.0),
		10.0 / FMath::Sqrt(99.0), Tolerance);

	TestEqual(TEXT("d=R 返回 0"),
		AStarMapCameraPawn::SilhouetteRadiusScale(1.0, 1.0), 0.0, Tolerance);

	TestEqual(TEXT("相机在球内返回 0"),
		AStarMapCameraPawn::SilhouetteRadiusScale(0.5, 1.0), 0.0, Tolerance);

	TestEqual(TEXT("半径非正返回 0"),
		AStarMapCameraPawn::SilhouetteRadiusScale(2.0, 0.0), 0.0, Tolerance);

	return true;
}

// 绕行灵敏度随「离焦点表面的高度 / 表面半径」缩放：贴地时很迟钝，拉远看星系时一拖扫过，
// 倍率跨到 1 以上、两端各夹上限。数值都是 double，用 TestEqual 的浮点容差重载。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraStarMapOrbitLookScaleTest,
	"StellarEra.StarMap.OrbitLookScale",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraStarMapOrbitLookScaleTest::RunTest(const FString& Parameters)
{
	constexpr double Tolerance = 1e-9;
	constexpr double Min = 0.05;
	constexpr double Max = 8.0;

	// 小天体：参考高度按 5 米兜底，贴飞船进星图不会一碰就飞。
	TestEqual(TEXT("飞船焦点 5 米、碰撞半径 0.5 米"),
		AStarMapCameraPawn::OrbitLookScale(500.0, 50.0, Min, Max), 0.9, Tolerance);

	TestEqual(TEXT("距离 = 2R：倍率 1"),
		AStarMapCameraPawn::OrbitLookScale(2.0e6, 1.0e6, Min, Max), 1.0, Tolerance);
	TestEqual(TEXT("距离 = 3R（整颗入画）：倍率 2"),
		AStarMapCameraPawn::OrbitLookScale(3.0e6, 1.0e6, Min, Max), 2.0, Tolerance);
	TestEqual(TEXT("距离 = 4R：倍率 3"),
		AStarMapCameraPawn::OrbitLookScale(4.0e6, 1.0e6, Min, Max), 3.0, Tolerance);

	TestEqual(TEXT("距离 = 10R：夹到上限"),
		AStarMapCameraPawn::OrbitLookScale(1.0e7, 1.0e6, Min, Max), 8.0, Tolerance);
	TestEqual(TEXT("距离 50 AU：仍夹在上限"),
		AStarMapCameraPawn::OrbitLookScale(5.0e7, 1.0e6, Min, Max), 8.0, Tolerance);

	TestEqual(TEXT("贴地 50 米：夹到下限"),
		AStarMapCameraPawn::OrbitLookScale(1.005e6, 1.0e6, Min, Max), 0.05, Tolerance);
	TestEqual(TEXT("高度 0：夹到下限"),
		AStarMapCameraPawn::OrbitLookScale(1.0e6, 1.0e6, Min, Max), 0.05, Tolerance);

	TestEqual(TEXT("距离 0：不缩放"),
		AStarMapCameraPawn::OrbitLookScale(0.0, 1.0e6, Min, Max), 1.0, Tolerance);
	TestEqual(TEXT("距离为负：不缩放"),
		AStarMapCameraPawn::OrbitLookScale(-1.0, 1.0e6, Min, Max), 1.0, Tolerance);

	TestEqual(TEXT("半径 0：不缩放"),
		AStarMapCameraPawn::OrbitLookScale(1.0e6, 0.0, Min, Max), 1.0, Tolerance);

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

	// 求解器的 GetBody 是裸的 Bodies[Index]，越界直接 UB，下标校验必须在这层做。
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
	// 星图的聚焦目标只认挂了星图相机的天体：飞船没有相机，进星图看它所在的行星。
	// 这条判定同时管进场自动聚焦与图内鼠标拾取，所以只验 HasStarMapCamera 就够。
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

	// 裸 AActor 没有根组件，CAC 与 Attach 都建不起来；统一挂一个已注册的 SceneComponent 当根。
	auto MakeActor = [World]() -> AActor*
	{
		AActor* Actor = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity);
		if (!Actor)
		{
			return nullptr;
		}
		USceneComponent* Root = NewObject<USceneComponent>(Actor, TEXT("TestRoot"));
		Actor->SetRootComponent(Root);
		Root->RegisterComponent();
		return Actor;
	};

	TestFalse(TEXT("空 Actor → 不可聚焦"),
		AStarMapCameraPawn::HasStarMapCamera(nullptr));

	ASpaceshipPawn* Ship = World->SpawnActor<ASpaceshipPawn>(
		ASpaceshipPawn::StaticClass(), FTransform::Identity);
	if (!TestNotNull(TEXT("飞船"), Ship))
	{
		return false;
	}
	TestFalse(TEXT("飞船没有星图相机 → 不可聚焦"),
		AStarMapCameraPawn::HasStarMapCamera(Ship));

	AActor* Plain = MakeActor();
	if (!TestNotNull(TEXT("普通 Actor"), Plain))
	{
		return false;
	}
	TestFalse(TEXT("普通 Actor → 不可聚焦"),
		AStarMapCameraPawn::HasStarMapCamera(Plain));

	AActor* CacPlanet = MakeActor();
	if (!TestNotNull(TEXT("挂了 CAC 的行星"), CacPlanet))
	{
		return false;
	}
	UChildActorComponent* CameraCac = NewObject<UChildActorComponent>(CacPlanet);
	CameraCac->SetChildActorClass(AStarMapCameraPawn::StaticClass());
	CameraCac->RegisterComponent();
	TestTrue(TEXT("挂了 CAC 星图相机 → 可聚焦"),
		AStarMapCameraPawn::HasStarMapCamera(CacPlanet));

	AActor* AttachedPlanet = MakeActor();
	AStarMapCameraPawn* AttachedCamera = World->SpawnActor<AStarMapCameraPawn>(
		AStarMapCameraPawn::StaticClass(), FTransform::Identity);
	if (!TestNotNull(TEXT("手动附加的行星"), AttachedPlanet) ||
		!TestNotNull(TEXT("手动附加的相机"), AttachedCamera))
	{
		return false;
	}
	TestTrue(TEXT("相机挂到行星上"),
		AttachedCamera->AttachToActor(AttachedPlanet, FAttachmentTransformRules::KeepRelativeTransform));
	TestTrue(TEXT("手动附加的星图相机 → 可聚焦"),
		AStarMapCameraPawn::HasStarMapCamera(AttachedPlanet));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraStarMapZoomLimitsTest,
	"StellarEra.StarMap.ZoomLimits",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraStarMapZoomLimitsTest::RunTest(const FString& Parameters)
{
	// 滚轮缩放的是「相机到地表的高度」而非到球心；拉远被视线上的行星挡住时停在穿入点之前。

	constexpr double Tolerance = 1e-6;

	TestEqual(TEXT("远处拉近一格按 1.15 缩放"),
		AStarMapCameraPawn::ZoomAltitudeCm(1.0e7, 1.15, 1.f, 5000.0, 5.0e7),
		1.0e7 / 1.15, Tolerance);
	TestEqual(TEXT("近处拉近一格同样按 1.15 缩放"),
		AStarMapCameraPawn::ZoomAltitudeCm(2.0e4, 1.15, 1.f, 5000.0, 5.0e7),
		2.0e4 / 1.15, Tolerance);

	TestEqual(TEXT("滚到下限就停住"),
		AStarMapCameraPawn::ZoomAltitudeCm(5000.0, 1.15, 1.f, 5000.0, 5.0e7),
		5000.0, Tolerance);
	TestEqual(TEXT("拉远一格高度变大"),
		AStarMapCameraPawn::ZoomAltitudeCm(5000.0, 1.15, -1.f, 5000.0, 5.0e7),
		5750.0, Tolerance);
	TestEqual(TEXT("拉远上限"),
		AStarMapCameraPawn::ZoomAltitudeCm(4.9e7, 1.15, -3.f, 5000.0, 5.0e7),
		5.0e7, Tolerance);

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

	TestEqual(TEXT("擦不到的行星不限制"),
		AStarMapCameraPawn::LimitDistanceByObstaclesCm(
			FocusLocation, RayDirection,
			TArray<FStarMapObstacle>{
				FStarMapObstacle{ FVector(1.5e6, 1.0e6 + 1.0, 0.0), 1.0e6 } },
			5000.0, 100.0, 5.0e7),
		5.0e7, Tolerance);

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

// 星图 Pawn 的编辑组件由蓝图挂载，笔刷参数在蓝图类默认值里，所以要查 SCS 模板而非 CDO
// （见 BlueprintComponentTestHelpers.h）。这一项替 HUD 与输入层看着这些可调参数。
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
	// 游戏里 Spawn 的是蓝图子类：只断言不变量、不钉具体数字，改细节面板不会把测试弄红。
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

// 星图里左 / 右 / 中三个键各管什么（纯函数，OnLookInput 调它）。右键改的是视线朝向
// 而非绕行位置；左键被编辑 / 矿物透视借走后，绕行改由中键承担。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraStarMapLookDispatchTest,
	"StellarEra.StarMap.LookDispatch",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraStarMapLookDispatchTest::RunTest(const FString& Parameters)
{
	using EMode = EStarMapLookMode;

	// 参数顺序：(右键, 左键, 中键, 编辑模式, 连续挖, 矿物透视)
	auto Resolve = [](bool bRight, bool bLeft, bool bMiddle, bool bEdit, bool bDig, bool bMineral)
	{
		return UStarMapCameraInputComponent::ResolveLookMode(bRight, bLeft, bMiddle, bEdit, bDig, bMineral);
	};

	TestTrue(TEXT("普通星图 左键绕行"), Resolve(false, true, false, false, false, false) == EMode::Orbit);
	TestTrue(TEXT("普通星图 右键转头"), Resolve(true, false, false, false, false, false) == EMode::Aim);
	TestTrue(TEXT("普通星图 中键绕行"), Resolve(false, false, true, false, false, false) == EMode::Orbit);

	TestTrue(TEXT("编辑模式 左键不转"), Resolve(false, true, false, true, false, false) == EMode::None);
	TestTrue(TEXT("编辑模式 右键转头"), Resolve(true, false, false, true, false, false) == EMode::Aim);
	TestTrue(TEXT("编辑模式 中键绕行"), Resolve(false, false, true, true, false, false) == EMode::Orbit);

	TestTrue(TEXT("矿物透视 左键不转"), Resolve(false, true, false, false, false, true) == EMode::None);
	TestTrue(TEXT("矿物透视 右键转头"), Resolve(true, false, false, false, false, true) == EMode::Aim);
	TestTrue(TEXT("矿物透视 中键绕行"), Resolve(false, false, true, false, false, true) == EMode::Orbit);

	TestTrue(TEXT("连续挖 左键不转"), Resolve(false, true, false, true, true, false) == EMode::None);
	TestTrue(TEXT("连续挖 右键不转头"), Resolve(true, false, false, true, true, false) == EMode::None);
	TestTrue(TEXT("连续挖 中键不转"), Resolve(false, false, true, true, true, false) == EMode::None);

	TestTrue(TEXT("左右同按 右键优先"), Resolve(true, true, false, false, false, false) == EMode::Aim);
	TestTrue(TEXT("右中同按 右键优先"), Resolve(true, false, true, false, false, false) == EMode::Aim);

	TestTrue(TEXT("没按键 不动"), Resolve(false, false, false, false, false, false) == EMode::None);

	return true;
}

// 转头偏移的圆锥夹取：合成视线必须落在「相机 → 焦点」方向的 MaxAimOffsetDeg 圆锥内，
// 否则能把镜头转到背后，绕行位置与画面对不上。
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

	{
		const FRotator Out = AStarMapCameraPawn::ClampAimOffset(Base, FRotator(0.0, 30.0, 0.0), MaxDeg);
		TestEqual(TEXT("30° 偏航不夹取 yaw"), Out.Yaw, 30.0, Tolerance);
		TestEqual(TEXT("30° 偏航不夹取 pitch"), Out.Pitch, 0.0, Tolerance);
	}

	{
		const FRotator Out = AStarMapCameraPawn::ClampAimOffset(Base, FRotator(0.0, 120.0, 0.0), MaxDeg);
		TestEqual(TEXT("120° 偏航夹到 90°"), AngleBetween(Out, Base), 90.0, Tolerance);
		TestEqual(TEXT("夹取后只剩偏航"), Out.Pitch, 0.0, Tolerance);
	}

	{
		const FRotator Out = AStarMapCameraPawn::ClampAimOffset(Base, FRotator(120.0, 0.0, 0.0), MaxDeg);
		TestEqual(TEXT("120° 俯仰夹到 90°"), AngleBetween(Out, Base), 90.0, Tolerance);
	}

	{
		const FRotator Out = AStarMapCameraPawn::ClampAimOffset(Base, FRotator(60.0, 120.0, 0.0), MaxDeg);
		TestEqual(TEXT("斜向 104.5° 夹到 90°"), AngleBetween(Out, Base), 90.0, Tolerance);
	}

	// pitch 100°/yaw 180° 合成只有 80°：夹取按合成后的夹角判，不是逐轴判。
	{
		const FRotator Out = AStarMapCameraPawn::ClampAimOffset(Base, FRotator(100.0, 180.0, 0.0), MaxDeg);
		TestEqual(TEXT("pitch 100°/yaw 180° 合成后仅 80°，不夹取"),
			AngleBetween(Out, Base), 80.0, Tolerance);
	}

	{
		const FRotator Out = AStarMapCameraPawn::ClampAimOffset(Base, FRotator(0.0, 90.0, 0.0), MaxDeg);
		TestEqual(TEXT("恰好 90° 不夹取"), Out.Yaw, 90.0, Tolerance);
	}

	{
		const FRotator Out = AStarMapCameraPawn::ClampAimOffset(Base, FRotator(0.0, 30.0, 0.0), 0.0);
		TestTrue(TEXT("上限 0 返回基准朝向"), Out.Equals(Base, Tolerance));
	}

	{
		const FRotator Out = AStarMapCameraPawn::ClampAimOffset(
			Base, FRotator(0.0, std::numeric_limits<double>::quiet_NaN(), 0.0), MaxDeg);
		TestTrue(TEXT("NaN 偏航退回基准朝向"), Out.Equals(Base, Tolerance));
	}

	{
		const FRotator Tilted(0.0, 45.0, 0.0);
		const FRotator Out = AStarMapCameraPawn::ClampAimOffset(Tilted, FRotator(0.0, 150.0, 0.0), MaxDeg);
		TestEqual(TEXT("非零基准 夹到 90°"), AngleBetween(Out, Tilted), 90.0, Tolerance);
	}

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
	// BP_VoxelPlanet 挂 Child Actor Component 当「自带相机」，控制器进图时优先用它
	// （ResolvePlanetStarMapCamera）。三段要验：CAC 扫描、附加 Actor 兜底扫描、Pawn →
	// 行星 → 相机的串联与优先级。行星本体不真生，只读组件/附加关系。

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

	// 裸 AActor 没有根组件，AttachToActor 会静默失败；统一带一个已注册的 SceneComponent 当根。
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


	TestTrue(TEXT("CAC: 空 Owner → 空"),
		AStarMapCameraPawn::FindChildStarMapCamera(nullptr) == nullptr);

	AActor* NoCacOwner = MakeActor(TEXT("没挂 CAC 的行星"));
	if (!TestNotNull(TEXT("没挂 CAC 的行星"), NoCacOwner))
	{
		return false;
	}
	TestTrue(TEXT("CAC: 没挂 CAC → 空"),
		AStarMapCameraPawn::FindChildStarMapCamera(NoCacOwner) == nullptr);

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
		AStarMapCameraPawn::FindChildStarMapCamera(WrongCacOwner) == nullptr);

	AActor* CacPlanet = MakeActor(TEXT("挂了 CAC 的行星"));
	if (!TestNotNull(TEXT("挂了 CAC 的行星"), CacPlanet))
	{
		return false;
	}
	UChildActorComponent* CameraCac = NewObject<UChildActorComponent>(CacPlanet);
	CameraCac->SetChildActorClass(AStarMapCameraPawn::StaticClass());
	CameraCac->RegisterComponent();

	AStarMapCameraPawn* ChildCamera =
		AStarMapCameraPawn::FindChildStarMapCamera(CacPlanet);
	if (!TestNotNull(TEXT("CAC: 挂了相机 → 解析到子相机"), ChildCamera))
	{
		return false;
	}
	// UChildActorComponent 构造里 bSetOwner=false：子 Actor 的 GetOwner() 是空，别拿它断言。
	TestTrue(TEXT("CAC: 解析到的就是该组件的子 Actor"),
		ChildCamera == Cast<AStarMapCameraPawn>(CameraCac->GetChildActor()));
	TestTrue(TEXT("CAC: 子相机的父 Actor 是行星 Actor"),
		ChildCamera->GetAttachParentActor() == CacPlanet);

	// 优先级：过渡期三颗行星上既有新加的 CAC，也还留着旧的手动相机，取 CAC 的那台。
	AStarMapCameraPawn* LegacyCamera = World->SpawnActor<AStarMapCameraPawn>(
		AStarMapCameraPawn::StaticClass(), FTransform::Identity);
	if (!TestNotNull(TEXT("旧的手动相机"), LegacyCamera))
	{
		return false;
	}
	TestTrue(TEXT("旧相机挂到同一颗行星上"),
		LegacyCamera->AttachToActor(CacPlanet, FAttachmentTransformRules::KeepRelativeTransform));
	TestTrue(TEXT("CAC: 两种相机都在时优先取 CAC 的"),
		AStarMapCameraPawn::FindChildStarMapCamera(CacPlanet) == ChildCamera);


	TestTrue(TEXT("附加: 空 Owner → 空"),
		AStarMapCameraPawn::FindAttachedStarMapCamera(nullptr) == nullptr);

	AActor* Plain = MakeActor(TEXT("普通 Actor"));
	if (!TestNotNull(TEXT("普通 Actor"), Plain))
	{
		return false;
	}
	TestTrue(TEXT("没挂附加 Actor → 空"),
		AStarMapCameraPawn::FindAttachedStarMapCamera(Plain) == nullptr);

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
		AStarMapCameraPawn::FindAttachedStarMapCamera(WrongOwner) == nullptr);

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
		AStarMapCameraPawn::FindAttachedStarMapCamera(PlanetOwner);
	if (!TestNotNull(TEXT("挂了星图相机 → 解析到它"), Resolved))
	{
		return false;
	}
	TestTrue(TEXT("解析到的就是那台相机"), Resolved == AttachedCamera);
	TestTrue(TEXT("相机的父 Actor 是行星"), Resolved->GetAttachParentActor() == PlanetOwner);

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
		AStarMapCameraPawn::FindAttachedStarMapCamera(OuterPlanet) == nullptr);

	TestTrue(TEXT("空 Pawn → 无行星相机"),
		AStarMapCameraPawn::ResolvePlanetStarMapCamera(nullptr) == nullptr);

	ASpaceshipPawn* Ship = World->SpawnActor<ASpaceshipPawn>(
		ASpaceshipPawn::StaticClass(), FTransform::Identity);
	if (!TestNotNull(TEXT("飞船"), Ship))
	{
		return false;
	}
	// 编辑器世界不走 BeginPlay，PlanetActor 还是空 —— 正好覆盖「解析不出行星」的分支
	// （深空进图不进入星图，见 FindStarMapCameraFor 与 EnterStarMap 的入参校验）。
	TestTrue(TEXT("飞船无参考行星 → 无行星相机"),
		AStarMapCameraPawn::ResolvePlanetStarMapCamera(Ship) == nullptr);

	return true;
}

// 入场锚点：行星相机挂在球心，进星图前要沿「球心 → 当前视点」搬到球面附近。

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraStarMapEntryAnchorTest,
	"StellarEra.StarMap.EntryAnchor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraStarMapEntryAnchorTest::RunTest(const FString& Parameters)
{
	const FVector Center(1.0e7, -2.0e6, 3.0e5); // 不放在原点，顺带验证减法不是「按原点算」
	constexpr double Radius = 1.0e6;
	constexpr double Altitude = 1.0e5;
	constexpr double Tolerance = 1.0;

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
