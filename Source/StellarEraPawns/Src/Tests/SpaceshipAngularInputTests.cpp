// Copyright Voxel Plugin SAS. All Rights Reserved.
//
// 飞船角加速度端口的用例，分两条测试：
//
// `StellarEra.Spaceship.AngularInput`（纯函数，不建 Actor、不依赖关卡）：
//   1. 鼠标增量（机体本地，度/秒）→ 世界系角加速度（rad/s²），除以 dt；
//   2. 横滚是持续角加速度，同向到 MaxRollRate 时衰减到 0；
//   3. 反打给全额角加速度（不按 |rate| 衰减）；
//   4. dt 为 0 时不产生 inf。
//
// `StellarEra.Spaceship.AngularInputState`（真 Actor + 编辑器世界）：
//   5. 待发冲量「读走并清零」、目标转速在界内削到上限；
//   6. 转速被物理带出上限后**绝不主动减速**（同向/零输入 α = 0，反向才减速）；
//   7. 进入落地自由视角时丢弃待发冲量。

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "Spaceship/SpaceshipPawn.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraShipAngularInputTest,
	"StellarEra.Spaceship.AngularInput",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraShipAngularInputTest::RunTest(const FString& Parameters)
{
	const FQuat Identity = FQuat::Identity;
	const FVector NoAngularVelocity = FVector::ZeroVector;
	const float Dt = 0.02f;

	// ── 1. 鼠标冲量 → α = 冲量 / dt ──
	{
		const FVector Alpha = ASpaceshipPawn::ComputeAngularAccelerationWorldRadPerS2(
			Identity, FVector(0.f, 10.f, 20.f), NoAngularVelocity,
			/*RollInput=*/0.f, /*RollAlpha=*/1500.f, /*MaxRollRate=*/120.f, Dt);

		// FVector 的分量在 UE5 是 double，TestEqual 的 float/double 两组重载
		// 会因「参数 1 选 double 版、参数 2/3 选 float 版」而 C2666，故显式收窄。
		TestEqual(TEXT("α_y = pitch/dt"),
			float(Alpha.Y), float(FMath::DegreesToRadians(10.0)) / Dt, 1.0e-5f);
		TestEqual(TEXT("α_z = yaw/dt"),
			float(Alpha.Z), float(FMath::DegreesToRadians(20.0)) / Dt, 1.0e-5f);
		TestEqual(TEXT("没滚就不给 α_x"), float(Alpha.X), 0.f, 1.0e-6f);
	}

	// ── 2. 横滚到顶：衰减到 0 ──
	{
		const FVector AtMaxRate(FMath::DegreesToRadians(120.f), 0.f, 0.f);
		const FVector Alpha = ASpaceshipPawn::ComputeAngularAccelerationWorldRadPerS2(
			Identity, FVector::ZeroVector, AtMaxRate,
			/*RollInput=*/1.f, 1500.f, 120.f, Dt);

		TestEqual(TEXT("到 MaxRollRate 后角加速度为 0"), float(Alpha.X), 0.f, 1.0e-6f);
	}

	// ── 3. 半速：衰减一半 ──
	{
		const FVector HalfRate(FMath::DegreesToRadians(60.f), 0.f, 0.f);
		const FVector Alpha = ASpaceshipPawn::ComputeAngularAccelerationWorldRadPerS2(
			Identity, FVector::ZeroVector, HalfRate, 1.f, 1500.f, 120.f, Dt);

		TestEqual(TEXT("半速时是额定角加速度的一半"),
			float(Alpha.X), float(FMath::DegreesToRadians(750.0)), 1.0e-3f);
	}

	// ── 4. 反向滚同样有效（对称） ──
	{
		const FVector Alpha = ASpaceshipPawn::ComputeAngularAccelerationWorldRadPerS2(
			Identity, FVector::ZeroVector, NoAngularVelocity, -1.f, 1500.f, 120.f, Dt);

		TestEqual(TEXT("A 键给出负角加速度"),
			float(Alpha.X), -float(FMath::DegreesToRadians(1500.0)), 1.0e-3f);
	}

	// ── 5. dt = 0：零，不产生 inf ──
	{
		const FVector Alpha = ASpaceshipPawn::ComputeAngularAccelerationWorldRadPerS2(
			Identity, FVector(0.f, 10.f, 0.f), NoAngularVelocity, 0.f, 1500.f, 120.f, 0.f);

		TestTrue(TEXT("dt=0 返回零"), Alpha.IsNearlyZero());
	}

	// ── 6. 机身绕世界 Z 转 90°：本地 pitch 变成世界 −X ──
	{
		const FQuat Yaw90(FVector::UpVector, FMath::DegreesToRadians(90.f));
		const FVector Alpha = ASpaceshipPawn::ComputeAngularAccelerationWorldRadPerS2(
			Yaw90, FVector(0.f, 10.f, 0.f), NoAngularVelocity, 0.f, 1500.f, 120.f, Dt);

		// 本地 +Y 在 90° 偏航后指向世界 −X。
		TestTrue(TEXT("本地 Y 已转到世界 −X"), Alpha.X < -0.1f);
		TestEqual(TEXT("世界 Y 分量为零"), float(Alpha.Y), 0.f, 1.0e-5f);
	}

	// ── 7. 反打不被 taper 衰减（给全额角加速度） ──
	{
		// 实际转速 +120°/s（= MaxRollRate），继续压同向：taper = 0。
		const FVector AtMaxRate(FMath::DegreesToRadians(120.f), 0.f, 0.f);
		const FVector SameDirection = ASpaceshipPawn::ComputeAngularAccelerationWorldRadPerS2(
			Identity, FVector::ZeroVector, AtMaxRate, /*RollInput=*/1.f, 1500.f, 120.f, Dt);
		TestEqual(TEXT("同向到顶后不再加速"), float(SameDirection.X), 0.f, 1.0e-6f);

		// 同样的转速反打：必须是全额 A，而不是 1 − v/Max 的衰减值。
		const FVector Counter = ASpaceshipPawn::ComputeAngularAccelerationWorldRadPerS2(
			Identity, FVector::ZeroVector, AtMaxRate, /*RollInput=*/-1.f, 1500.f, 120.f, Dt);
		TestEqual(TEXT("反打给全额角加速度"),
			float(Counter.X), -float(FMath::DegreesToRadians(1500.0)), 1.0e-3f);

		// 半速反打同样给全额（同向时这里只会剩一半，见用例 3）。
		const FVector HalfRate(FMath::DegreesToRadians(60.f), 0.f, 0.f);
		const FVector HalfCounter = ASpaceshipPawn::ComputeAngularAccelerationWorldRadPerS2(
			Identity, FVector::ZeroVector, HalfRate, /*RollInput=*/-1.f, 1500.f, 120.f, Dt);
		TestEqual(TEXT("半速反打也是全额"),
			float(HalfCounter.X), -float(FMath::DegreesToRadians(1500.0)), 1.0e-3f);
	}

	return true;
}

// 状态契约：待发冲量只兑现一次、进入自由视角时被丢弃。
//
// 这一组要真 Actor（有 World 才能跑 Tick / IsLanded），所以和上面的纯函数用例分开；
// 建世界的方式与 SpaceshipDebugViewTests 一致（编辑器世界，不跑 BeginPlay）。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraShipAngularInputStateTest,
	"StellarEra.Spaceship.AngularInputState",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraShipAngularInputStateTest::RunTest(const FString& Parameters)
{
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

	const FVector ShipLocation(0.0, 0.0, 1000.0);
	ASpaceshipPawn* Ship = World->SpawnActor<ASpaceshipPawn>(
		ASpaceshipPawn::StaticClass(), FTransform(FRotator::ZeroRotator, ShipLocation));
	TestNotNull(TEXT("能生成飞船"), Ship);
	if (!Ship)
	{
		return false;
	}

	// ── 1. 读走并清零 ──
	Ship->Look(5.f, 5.f);
	const FVector FirstRead = Ship->GetAngularAccelerationWorldRadPerS2(0.02f);
	TestTrue(TEXT("第一次读到非零角加速度"), !FirstRead.IsNearlyZero());

	const FVector SecondRead = Ship->GetAngularAccelerationWorldRadPerS2(0.02f);
	TestTrue(TEXT("第二次读不给加速度（冲量已读走）"), SecondRead.IsNearlyZero());

	// 清零之后还能重新累加，证明第二次读的零不是因为别的状态没复位。
	Ship->Look(5.f, 0.f);
	TestTrue(TEXT("清零后仍可重新累加"),
		!Ship->GetAngularAccelerationWorldRadPerS2(0.02f).IsNearlyZero());

	// ── 2. 「读走」与目标夹取：持续输入时角加速度收敛到 0（已在目标转速上） ──
	// 先把实际角速度写成 90°/s（= MaxYawRate），再给同样的正向冲量：
	// 目标被夹在 90，差值为 0，所以不该再给加速度。
	Ship->SetCosmosAngularVelocity(FVector(0.f, 0.f, FMath::DegreesToRadians(90.f)));
	Ship->Look(15.f, 0.f); // 偏航 15° × 6 = 90°/s 的增量
	TestTrue(TEXT("已在上限时不再加速"),
		Ship->GetAngularAccelerationWorldRadPerS2(0.02f).IsNearlyZero(1.0e-3f));
	Ship->SetCosmosAngularVelocity(FVector::ZeroVector);

	// ── 3. 超界时绝不主动减速（落地翻滚的恢复必须是纯物理） ──
	// 飞船没转，世界系偏航就是本地 Z。300°/s 远在 MaxYawRate(90) 之外，
	// 模拟被落地冲击带起来的翻滚。
	Ship->SetCosmosAngularVelocity(FVector(0.f, 0.f, FMath::DegreesToRadians(300.f)));

	// 3a. 没摸鼠标：α 必须恰好是 0（旧实现会给出 (90−300)/dt 的巨大刹车）。
	TestTrue(TEXT("超界且无输入：不主动减速"),
		Ship->GetAngularAccelerationWorldRadPerS2(0.02f).IsNearlyZero());

	// 3b. 同向输入：仍然是 0，不把转速往 90 拽。
	Ship->Look(20.f / 6.f, 0.f); // +20°/s 的增量
	TestTrue(TEXT("超界同向输入：不主动减速"),
		Ship->GetAngularAccelerationWorldRadPerS2(0.02f).IsNearlyZero());

	// 3c. 反向输入：按玩家给的量减速（−20°/s 的增量 → α = −20°/dt）。
	Ship->Look(-20.f / 6.f, 0.f);
	TestEqual(TEXT("超界反向输入按给定增量减速"),
		float(Ship->GetAngularAccelerationWorldRadPerS2(0.02f).Z),
		float(FMath::DegreesToRadians(-20.0)) / 0.02f, 2.0e-3f);

	// 3d. 回归：界内朝上限加速仍削到 90（Critical 的判据：稳态有上限）。
	Ship->SetCosmosAngularVelocity(FVector(0.f, 0.f, FMath::DegreesToRadians(50.f)));
	Ship->Look(100.f / 6.f, 0.f); // +100°/s 的增量
	TestEqual(TEXT("界内 +100 削到 90，α = (90−50)/dt"),
		float(Ship->GetAngularAccelerationWorldRadPerS2(0.02f).Z),
		float(FMath::DegreesToRadians(40.0)) / 0.02f, 5.0e-2f);

	Ship->SetCosmosAngularVelocity(FVector::ZeroVector);

	// ── 4. 进入自由视角时丢掉待发冲量 ──
	// 平台顶面落在落地检测球内 10 cm，判定才会成立。
	const float RadiusCm = Ship->GetLandingQueryRadiusCm();
	AStaticMeshActor* Platform = World->SpawnActor<AStaticMeshActor>();
	TestNotNull(TEXT("平台生成"), Platform);
	if (!Platform)
	{
		return false;
	}
	{
		UStaticMeshComponent* Mesh = Platform->GetStaticMeshComponent();
		Mesh->SetMobility(EComponentMobility::Movable);
		Mesh->SetStaticMesh(LoadObject<UStaticMesh>(
			nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
		Mesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		Mesh->SetCollisionObjectType(ECC_WorldStatic);
		Mesh->SetCollisionResponseToAllChannels(ECR_Block);
		Platform->SetActorScale3D(FVector(20.f, 20.f, 1.f));
		Platform->SetActorLocation(
			FVector(ShipLocation.X, ShipLocation.Y, ShipLocation.Z - RadiusCm + 10.0 - 50.0));
	}

	Ship->SetThrust(0.f);
	Ship->SetVertical(0.f);
	Ship->Tick(0.02f);
	TestTrue(TEXT("平台就位后判为落地"), Ship->IsLanded());
	TestTrue(TEXT("落地自由视角下不给角加速度"),
		Ship->GetAngularAccelerationWorldRadPerS2(0.02f).IsNearlyZero());

	// 先退出自由视角（拉起），攒一次冲量，再重新进入 —— 进入那一刻 Tick 必须丢弃它，
	// 退出后不能补发。期间不读冲量，否则读走清零会让断言失去意义。
	Ship->SetThrust(1.f);
	Ship->Tick(0.02f);
	Ship->Look(10.f, 10.f);
	Ship->SetThrust(0.f);
	Ship->Tick(0.02f);
	Ship->SetThrust(1.f);
	Ship->Tick(0.02f);
	TestTrue(TEXT("退出自由视角不补发进入前攒的冲量"),
		Ship->GetAngularAccelerationWorldRadPerS2(0.02f).IsNearlyZero());

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
