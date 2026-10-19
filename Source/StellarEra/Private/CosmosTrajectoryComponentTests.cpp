// Copyright Voxel Plugin SAS. All Rights Reserved.

#include "CoreMinimal.h"
#include "Components/LineBatchComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Misc/AutomationTest.h"

#include "CosmosOrbitComponent.h"
#include "CosmosTrajectoryComponent.h"

#if WITH_DEV_AUTOMATION_TESTS

// 回归测试：旧版本 OnRegister 每次都用默认名新建行批，而行批的 outer 是 Actor
// （不是轨迹组件），组件重建时不会跟着销毁。组件每重注册一次就多一个带旧线的
// 行批、且旧线永不 Flush —— 表现就是「改了天体速度，预测线更新了，旧线还留着」。
//
// 本测试不去看画面，直接数 Actor 上的 ULineBatchComponent：
//   1. 反复 Unregister/Register，行批数量必须恒为 1；
//   2. 连续 Tick 两次，BatchedLines 不累加（每帧 Flush 后整批重画）；
//   3. 改 InitialVelocityKmPerSecond 后再 Tick，线段数不变、但几何变了。

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraTrajectoryLineBatchTest,
	"StellarEra.Trajectory.LineBatchNoAccumulation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace
{
	UCosmosOrbitComponent* AddOrbitComponent(AActor* Owner, double MassEarth,
		const FVector& InitialVelocityKmPerSecond, const TCHAR* ComponentName)
	{
		UCosmosOrbitComponent* Orbit = NewObject<UCosmosOrbitComponent>(Owner, ComponentName);
		Owner->AddInstanceComponent(Orbit);
		Orbit->MassEarth = MassEarth;
		Orbit->InitialVelocityKmPerSecond = InitialVelocityKmPerSecond;
		Orbit->RegisterComponent();
		return Orbit;
	}

	int32 CountLineBatches(const AActor* Owner)
	{
		TArray<ULineBatchComponent*> Batches;
		Owner->GetComponents<ULineBatchComponent>(Batches);
		return Batches.Num();
	}

	ULineBatchComponent* FindLineBatch(const AActor* Owner)
	{
		TArray<ULineBatchComponent*> Batches;
		Owner->GetComponents<ULineBatchComponent>(Batches);
		return Batches.Num() > 0 ? Batches[0] : nullptr;
	}
}

bool FStellarEraTrajectoryLineBatchTest::RunTest(const FString& Parameters)
{
	// ── 独立的编辑器世界：不碰当前关卡 ──
	UWorld* World = UWorld::CreateWorld(EWorldType::Editor, false);
	if (!TestNotNull(TEXT("测试世界"), World))
	{
		return false;
	}

	FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Editor);
	WorldContext.SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());

	AActor* StarActor = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity);
	AActor* PlanetActor = World->SpawnActor<AActor>(AActor::StaticClass(),
		FTransform(FVector(5.0e7, 0.0, 0.0)));
	if (!TestNotNull(TEXT("恒星 Actor"), StarActor)
		|| !TestNotNull(TEXT("行星 Actor"), PlanetActor))
	{
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
		return false;
	}

	AddOrbitComponent(StarActor, 3329460837.433315, FVector::ZeroVector, TEXT("TestStarOrbit"));
	UCosmosOrbitComponent* PlanetOrbit = AddOrbitComponent(PlanetActor, 290740.8393297756,
		FVector(0.0, 237.0235231766674, 0.0), TEXT("TestPlanetOrbit"));

	UCosmosTrajectoryComponent* Trajectory = NewObject<UCosmosTrajectoryComponent>(
		PlanetActor, TEXT("TestTrajectory"));
	PlanetActor->AddInstanceComponent(Trajectory);
	Trajectory->bShowInEditor = true;
	Trajectory->bDrawPrediction = true;
	Trajectory->bDrawVelocityArrow = false;   // 只数预测折线，段数才可预期
	Trajectory->bDrawGravityArrow = false;
	Trajectory->PredictionYears = 1.0;        // 固定窗口，避开自动周期估算
	Trajectory->RegisterComponent();

	// ── 1. 反复重注册（模拟蓝图重编译/组件重建），行批不得累积 ──
	for (int32 Cycle = 0; Cycle < 3; ++Cycle)
	{
		Trajectory->UnregisterComponent();
		Trajectory->RegisterComponent();
		TestEqual(
			*FString::Printf(TEXT("第 %d 次重注册后行批数量"), Cycle + 1),
			CountLineBatches(PlanetActor), 1);
	}

	ULineBatchComponent* LineBatch = FindLineBatch(PlanetActor);
	if (!TestNotNull(TEXT("行星 Actor 上的行批"), LineBatch))
	{
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
		return false;
	}

	// ── 2. 连续两次 Tick：同一批行，不累加 ──
	Trajectory->TickComponent(0.016f, LEVELTICK_ViewportsOnly, nullptr);
	const int32 SegmentsFirst =
		Trajectory->bDrawPrediction ? LineBatch->BatchedLines.Num() : 0;
	const FVector FirstSegmentEnd =
		LineBatch->BatchedLines.Num() > 0 ? LineBatch->BatchedLines[0].End : FVector::ZeroVector;

	Trajectory->TickComponent(0.016f, LEVELTICK_ViewportsOnly, nullptr);
	TestEqual(TEXT("两次 Tick 后线段数不累加"), LineBatch->BatchedLines.Num(), SegmentsFirst);
	TestTrue(TEXT("预测折线确实画出来了"), SegmentsFirst > 0);

	// ── 3. 改初速后再 Tick：旧线被 Flush，几何随之改变 ──
	PlanetOrbit->InitialVelocityKmPerSecond = FVector(0.0, 0.0, 237.0235231766674);
	Trajectory->TickComponent(0.016f, LEVELTICK_ViewportsOnly, nullptr);

	TestEqual(TEXT("改速度后线段数不变（没有叠加旧线）"),
		LineBatch->BatchedLines.Num(), SegmentsFirst);
	TestEqual(TEXT("改速度后行批仍然只有一个"), CountLineBatches(PlanetActor), 1);

	const FVector NewFirstSegmentEnd =
		LineBatch->BatchedLines.Num() > 0 ? LineBatch->BatchedLines[0].End : FVector::ZeroVector;
	TestTrue(TEXT("预测几何随速度改变"),
		FVector::DistSquared(FirstSegmentEnd, NewFirstSegmentEnd) > 1.0);

	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
