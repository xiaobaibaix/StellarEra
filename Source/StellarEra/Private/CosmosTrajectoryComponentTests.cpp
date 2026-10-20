// Copyright Voxel Plugin SAS. All Rights Reserved.

#include "CoreMinimal.h"
#include "Components/LineBatchComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Misc/AutomationTest.h"

#include "CosmosOrbitComponent.h"
#include "CosmosTrajectoryComponent.h"

#include <limits>

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

	// 裸 AActor 没有根组件时 GetActorLocation() 恒为 0 —— SpawnActor 的摆放变换
	// 读不回来。剖分边界靠摆放位置算，所以必须挂一个 SceneComponent 当根。
	void AddRootComponent(AActor* Owner, const FVector& Location)
	{
		USceneComponent* Root = NewObject<USceneComponent>(Owner, TEXT("Root"));
		Owner->SetRootComponent(Root);
		Root->RegisterComponent();
		Root->SetWorldLocation(Location);
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
	Trajectory->bDrawHillSphere = false;      // 同上：希尔球会往同一个行批里加线
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

// 希尔球半径：r_H = a · (m / 3M)^(1/3)。
// 这里只测纯函数，不建世界 —— 公式错了，画出来的球就是错的，而画面看不出来。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraHillSphereTest,
	"StellarEra.Cosmos.HillSphere",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraHillSphereTest::RunTest(const FString& Parameters)
{
	// (m / 3M) = 1/27 → 立方根 = 1/3，所以半径 = 距离 / 3。
	double RadiusCm = -1.0;
	TestTrue(TEXT("有效输入返回 true"),
		UCosmosTrajectoryComponent::ComputeHillRadiusCm(1.0, 9.0, 300.0, RadiusCm));
	TestEqual(TEXT("r_H = a·(m/3M)^(1/3)"), RadiusCm, 100.0, 1.0e-9);

	double DoubleDistanceCm = 0.0;
	TestTrue(TEXT("两倍距离可算"),
		UCosmosTrajectoryComponent::ComputeHillRadiusCm(1.0, 9.0, 600.0, DoubleDistanceCm));
	TestEqual(TEXT("半径随距离线性缩放"), DoubleDistanceCm, 2.0 * RadiusCm, 1.0e-9);

	// 次体不轻于主星：希尔近似不成立。
	double InvalidCm = -1.0;
	TestFalse(TEXT("次体与主星等重时拒绝"),
		UCosmosTrajectoryComponent::ComputeHillRadiusCm(9.0, 9.0, 300.0, InvalidCm));
	TestEqual(TEXT("拒绝时输出清零"), InvalidCm, 0.0, 0.0);

	TestFalse(TEXT("次体比主星更重时拒绝"),
		UCosmosTrajectoryComponent::ComputeHillRadiusCm(10.0, 9.0, 300.0, InvalidCm));
	TestFalse(TEXT("质量为零时拒绝"),
		UCosmosTrajectoryComponent::ComputeHillRadiusCm(0.0, 9.0, 300.0, InvalidCm));
	TestFalse(TEXT("距离为零时拒绝"),
		UCosmosTrajectoryComponent::ComputeHillRadiusCm(1.0, 9.0, 0.0, InvalidCm));
	TestFalse(TEXT("NaN 时拒绝"),
		UCosmosTrajectoryComponent::ComputeHillRadiusCm(
			std::numeric_limits<double>::quiet_NaN(), 9.0, 300.0, InvalidCm));

	// 本关卡量级：行星 290740.8393297756 M⊕，中心 3329460837.433315 M⊕，
	// 距离 157.91 AU（1 AU = 1e6 cm）→ r_H ≈ 4.857e6 cm ≈ 4.86 AU。
	double LevelRadiusCm = 0.0;
	TestTrue(TEXT("本关卡参数可算"),
		UCosmosTrajectoryComponent::ComputeHillRadiusCm(
			290740.8393297756, 3329460837.433315, 157.91e6, LevelRadiusCm));
	TestEqual(TEXT("本关卡希尔半径 ≈ 4.857e6 cm"), LevelRadiusCm, 4.857e6, 2.0e4);

	return true;
}

// 瞬时引力主导边界：|P-r_i| / |P-r_j| = sqrt(m_i/m_j) 的 Apollonius 球，
// 以及「这一对是不是点上的前两名」的裁剪规则。纯函数，不建世界。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraDominanceBoundaryTest,
	"StellarEra.Cosmos.DominanceBoundary",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraDominanceBoundaryTest::RunTest(const FString& Parameters)
{
	FVector3d Center;
	double RadiusCm = -1.0;
	FVector3d Normal;

	// 等质量：中垂面 —— 球心在中点、半径 0、法线沿 A→B。
	TestTrue(TEXT("等质量可算"),
		UCosmosTrajectoryComponent::ComputeDominanceBoundary(
			FVector3d(0.0, 0.0, 0.0), 1.0, FVector3d(10.0, 0.0, 0.0), 1.0,
			Center, RadiusCm, Normal));
	TestEqual(TEXT("等质量球心在中点 X"), Center.X, 5.0, 1.0e-9);
	TestEqual(TEXT("等质量半径为零（平面）"), RadiusCm, 0.0, 1.0e-9);
	TestEqual(TEXT("法线沿 A→B"), Normal.X, 1.0, 1.0e-9);

	// 质量比 4:1 → k=2：球心在 40/3，半径 20/3。
	TestTrue(TEXT("不等质量可算"),
		UCosmosTrajectoryComponent::ComputeDominanceBoundary(
			FVector3d::ZeroVector, 4.0, FVector3d(10.0, 0.0, 0.0), 1.0,
			Center, RadiusCm, Normal));
	TestEqual(TEXT("球心 X"), Center.X, 40.0 / 3.0, 1.0e-9);
	TestEqual(TEXT("半径"), RadiusCm, 20.0 / 3.0, 1.0e-9);

	// 球面上沿 +X 的点：到 A 距离 20、到 B 距离 10，引力比正好 4:1。
	TestEqual(TEXT("边界点两侧引力相等"),
		4.0 / (20.0 * 20.0), 1.0 / (10.0 * 10.0), 1.0e-15);

	// 第三体裁剪：C 远而轻 → 这对仍是前两名；C 压在边界点上 → 不是。
	const TArray<FVector3d> FarPositions = {
		FVector3d::ZeroVector, FVector3d(10.0, 0.0, 0.0), FVector3d(1000.0, 0.0, 0.0) };
	const TArray<double> FarMasses = { 4.0, 1.0, 1.0 };
	TestTrue(TEXT("第三颗很远时边界点仍归这对"),
		UCosmosTrajectoryComponent::ArePairDominantAt(
			FVector3d(20.0, 0.0, 0.0), FarPositions, FarMasses, 0, 1));

	const TArray<FVector3d> NearPositions = {
		FVector3d::ZeroVector, FVector3d(10.0, 0.0, 0.0), FVector3d(20.0, 0.0, 0.0) };
	const TArray<double> NearMasses = { 4.0, 1.0, 100.0 };
	TestFalse(TEXT("更重的第三颗把边界裁掉"),
		UCosmosTrajectoryComponent::ArePairDominantAt(
			FVector3d(20.0, 0.0, 0.0), NearPositions, NearMasses, 0, 1));

	TestFalse(TEXT("采样点压在天体上不算边界"),
		UCosmosTrajectoryComponent::ArePairDominantAt(
			FVector3d::ZeroVector, FarPositions, FarMasses, 0, 1));

	// 无效输入。
	TestFalse(TEXT("两点重合时拒绝"),
		UCosmosTrajectoryComponent::ComputeDominanceBoundary(
			FVector3d::ZeroVector, 1.0, FVector3d::ZeroVector, 1.0,
			Center, RadiusCm, Normal));
	TestFalse(TEXT("零质量拒绝"),
		UCosmosTrajectoryComponent::ComputeDominanceBoundary(
			FVector3d::ZeroVector, 0.0, FVector3d(10.0, 0.0, 0.0), 1.0,
			Center, RadiusCm, Normal));

	return true;
}

// 剖分绘制：恒星组件画出 patch；连续 Tick 不累加；双方都开时不重复；
// 只有较轻一方开时由它自己画；加等质量第三体后多出平面边界。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraDominancePartitionTest,
	"StellarEra.Cosmos.DominancePartition",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraDominancePartitionTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Editor, false);
	if (!TestNotNull(TEXT("测试世界"), World))
	{
		return false;
	}

	FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Editor);
	WorldContext.SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());

	AActor* StarActor = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity);
	AActor* PlanetActor = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity);
	if (!TestNotNull(TEXT("恒星 Actor"), StarActor)
		|| !TestNotNull(TEXT("行星 Actor"), PlanetActor))
	{
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
		return false;
	}
	AddRootComponent(StarActor, FVector::ZeroVector);
	AddRootComponent(PlanetActor, FVector(5.0e7, 0.0, 0.0));

	AddOrbitComponent(StarActor, 3329460837.433315, FVector::ZeroVector, TEXT("TestStarOrbit"));
	AddOrbitComponent(PlanetActor, 290740.8393297756, FVector::ZeroVector, TEXT("TestPlanetOrbit"));

	UCosmosTrajectoryComponent* StarTrajectory = NewObject<UCosmosTrajectoryComponent>(
		StarActor, TEXT("TestStarTrajectory"));
	StarActor->AddInstanceComponent(StarTrajectory);
	StarTrajectory->bShowInEditor = true;
	StarTrajectory->bDrawPrediction = false;
	StarTrajectory->bDrawVelocityArrow = false;
	StarTrajectory->bDrawGravityArrow = false;
	StarTrajectory->bDrawHillSphere = false;
	StarTrajectory->bDrawDominancePartition = true;
	StarTrajectory->RegisterComponent();

	StarTrajectory->TickComponent(0.016f, LEVELTICK_ViewportsOnly, nullptr);
	ULineBatchComponent* StarBatch = FindLineBatch(StarActor);
	if (!TestNotNull(TEXT("恒星行批"), StarBatch))
	{
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
		return false;
	}
	const int32 FirstCount = StarBatch->BatchedLines.Num();
	TestTrue(TEXT("恒星画出剖分网格"), FirstCount > 0);

	StarTrajectory->TickComponent(0.016f, LEVELTICK_ViewportsOnly, nullptr);
	TestEqual(TEXT("连续 Tick 不累加"), StarBatch->BatchedLines.Num(), FirstCount);

	// 行星也开：恒星更重，仍由恒星画；行星自己的行批必须为空。
	UCosmosTrajectoryComponent* PlanetTrajectory = NewObject<UCosmosTrajectoryComponent>(
		PlanetActor, TEXT("TestPlanetTrajectory"));
	PlanetActor->AddInstanceComponent(PlanetTrajectory);
	PlanetTrajectory->bShowInEditor = true;
	PlanetTrajectory->bDrawPrediction = false;
	PlanetTrajectory->bDrawVelocityArrow = false;
	PlanetTrajectory->bDrawGravityArrow = false;
	PlanetTrajectory->bDrawHillSphere = false;
	PlanetTrajectory->bDrawDominancePartition = true;
	PlanetTrajectory->RegisterComponent();

	StarTrajectory->TickComponent(0.016f, LEVELTICK_ViewportsOnly, nullptr);
	PlanetTrajectory->TickComponent(0.016f, LEVELTICK_ViewportsOnly, nullptr);
	ULineBatchComponent* PlanetBatch = FindLineBatch(PlanetActor);
	if (!TestNotNull(TEXT("行星行批"), PlanetBatch))
	{
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
		return false;
	}
	TestEqual(TEXT("较轻一方不重复画"), PlanetBatch->BatchedLines.Num(), 0);

	// 恒星关、行星开：行星必须自己画，否则「只开一个组件」等于失效。
	StarTrajectory->bDrawDominancePartition = false;
	StarTrajectory->TickComponent(0.016f, LEVELTICK_ViewportsOnly, nullptr);
	PlanetTrajectory->TickComponent(0.016f, LEVELTICK_ViewportsOnly, nullptr);
	TestTrue(TEXT("只有较轻一方开时由它自己画"), PlanetBatch->BatchedLines.Num() > 0);

	// 加一颗与恒星等质量的第三体：恒星多出「中垂面」patch，线段数增加。
	AActor* ThirdActor = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity);
	if (TestNotNull(TEXT("第三体 Actor"), ThirdActor))
	{
		AddRootComponent(ThirdActor, FVector(-5.0e7, 0.0, 0.0));
		AddOrbitComponent(ThirdActor, 3329460837.433315, FVector::ZeroVector,
			TEXT("TestThirdOrbit"));
		StarTrajectory->bDrawDominancePartition = true;
		StarTrajectory->TickComponent(0.016f, LEVELTICK_ViewportsOnly, nullptr);
		TestTrue(TEXT("三体剖分比两体多出线段"),
			StarBatch->BatchedLines.Num() > FirstCount);
	}

	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
