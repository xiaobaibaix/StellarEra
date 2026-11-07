// Copyright Voxel Plugin SAS. All Rights Reserved.
//
// 飞船碰撞形状的资产级验收测试：断言资源里真的长这样，而不只是能编译。
// 覆盖 Hull 根组件 + QueryAndPhysics、组件挂接、根球已删、兜底接触半径 300 cm、
// 网格带 Simple Collision 凸包。editor world 不跑 BeginPlay，所以读 CDO。

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Camera/CameraComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "GameFramework/SpringArmComponent.h"
#include "PhysicsEngine/BodySetup.h"
#include "SpaceshipPawn.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraSpaceshipCollisionTest,
	"StellarEra.Spaceship.SpaceshipCollision",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraSpaceshipCollisionTest::RunTest(const FString& Parameters)
{
	UClass* ShipClass = LoadClass<ASpaceshipPawn>(
		nullptr, TEXT("/Game/Code/Pawns/BP_SpaceshipPawn.BP_SpaceshipPawn_C"));
	TestNotNull(TEXT("BP_SpaceshipPawn_C 可加载"), ShipClass);
	if (!ShipClass)
	{
		return false;
	}

	ASpaceshipPawn* CDO = Cast<ASpaceshipPawn>(ShipClass->GetDefaultObject());
	TestNotNull(TEXT("BP_SpaceshipPawn CDO"), CDO);
	if (!CDO)
	{
		return false;
	}

	UStaticMeshComponent* Hull = Cast<UStaticMeshComponent>(
		CDO->GetDefaultSubobjectByName(TEXT("Hull")));
	USpringArmComponent* SpringArm = Cast<USpringArmComponent>(
		CDO->GetDefaultSubobjectByName(TEXT("SpringArm")));
	UCameraComponent* Camera = Cast<UCameraComponent>(
		CDO->GetDefaultSubobjectByName(TEXT("Camera")));
	TestNotNull(TEXT("船体组件 Hull"), Hull);
	TestNotNull(TEXT("弹簧臂 SpringArm"), SpringArm);
	TestNotNull(TEXT("相机 Camera"), Camera);
	if (!Hull || !SpringArm || !Camera)
	{
		return false;
	}

	TestTrue(TEXT("Hull 是根组件"), CDO->GetRootComponent() == Hull);
	TestNull(TEXT("根球 Collision 已删除"),
		CDO->GetDefaultSubobjectByName(TEXT("Collision")));
	TestTrue(TEXT("SpringArm 挂在 Hull 下"),
		SpringArm->GetAttachParent() == Hull);
	TestTrue(TEXT("Camera 挂在 SpringArm 下"),
		Camera->GetAttachParent() == SpringArm);

	TestTrue(TEXT("兜底接触半径仍是 300 cm（N 体接触半径）"),
		FMath::IsNearlyEqual(CDO->GetContactRadiusCm(), 300.f));

	TestTrue(TEXT("Hull 使用 QueryAndPhysics"),
		Hull->GetCollisionEnabled() == ECollisionEnabled::QueryAndPhysics);
	TestTrue(TEXT("Hull 碰撞通道为 ECC_Pawn"),
		Hull->GetCollisionObjectType() == ECC_Pawn);

	UStaticMesh* Mesh = Hull->GetStaticMesh();
	TestNotNull(TEXT("Hull 指定了静态网格"), Mesh);
	if (!Mesh)
	{
		return false;
	}

	const UBodySetup* BodySetup = Mesh->GetBodySetup();
	TestNotNull(TEXT("Hull 网格有 BodySetup"), BodySetup);
	if (BodySetup)
	{
		TestTrue(TEXT("Hull 网格带 Simple Collision 凸包"),
			BodySetup->AggGeom.ConvexElems.Num() > 0);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraShipContactProbesTest,
	"StellarEra.Spaceship.ShipContactProbes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraShipContactProbesTest::RunTest(const FString& Parameters)
{
	UClass* ShipClass = LoadClass<ASpaceshipPawn>(
		nullptr, TEXT("/Game/Code/Pawns/BP_SpaceshipPawn.BP_SpaceshipPawn_C"));
	TestNotNull(TEXT("BP_SpaceshipPawn_C 可加载"), ShipClass);
	if (!ShipClass)
	{
		return false;
	}

	ASpaceshipPawn* CDO = Cast<ASpaceshipPawn>(ShipClass->GetDefaultObject());
	TestNotNull(TEXT("BP_SpaceshipPawn CDO"), CDO);
	if (!CDO)
	{
		return false;
	}

	TArray<FVector> Probes;
	TestTrue(TEXT("能从 Hull 凸包生成接触探针"), CDO->BuildContactProbes(Probes));
	TestTrue(TEXT("探针数量 > 0"), Probes.Num() > 0);
	if (Probes.Num() == 0)
	{
		return false;
	}

	// 结构断言：每个探针都必须落在「Hull 网格包围盒 × Hull 相对变换」附近。
	// 网格换形状测试照样有效，只有探针变换接错时才红。
	const UStaticMeshComponent* Hull = Cast<UStaticMeshComponent>(
		CDO->GetDefaultSubobjectByName(TEXT("Hull")));
	TestNotNull(TEXT("船体组件 Hull"), Hull);
	if (!Hull || !Hull->GetStaticMesh())
	{
		return false;
	}

	// 参照框必须用「Hull → Actor」的相对变换，不能用 Hull->GetRelativeTransform()：
	// Hull 是根组件时 UE 把 RelativeLocation 直接存成世界坐标，测试框会和探针一起
	// 被平移，越界永远测不出来。
	const FTransform HullToActor =
		Hull->GetComponentTransform().GetRelativeTransform(CDO->GetActorTransform());
	const FBox ExpectedBox =
		Hull->GetStaticMesh()->GetBoundingBox().TransformBy(HullToActor);

	// 容差 100 cm：Simple Collision 常是单独导的、比渲染网格略大（这里顶部超出约 27 cm）。
	constexpr double ToleranceCm = 100.0;

	const FVector* Offender = nullptr;
	double MaxOvershoot = 0.0;
	FBox ProbeBox(ForceInit);
	for (const FVector& Probe : Probes)
	{
		ProbeBox += Probe;
		const FVector Closest = ExpectedBox.GetClosestPointTo(Probe);
		MaxOvershoot = FMath::Max(MaxOvershoot, (double)FVector::Dist(Closest, Probe));
		if (!Offender && FVector::DistSquared(Closest, Probe) > ToleranceCm * ToleranceCm)
		{
			Offender = &Probe;
		}
	}
	TestTrue(TEXT("所有探针都在 Hull 包围盒 100 cm 内"), Offender == nullptr);
	if (Offender)
	{
		// 失败时把第一个越界点与参照框一起打出来，才能分清是变换漏乘还是网格外包。
		AddError(FString::Printf(
			TEXT("越界探针 %s（共 %d 个，最大越界 %.2f cm，探针包围盒 %s .. %s）；"
				 "Hull 相对变换 %s；包围盒 %s .. %s"),
			*Offender->ToString(), Probes.Num(), MaxOvershoot,
			*ProbeBox.Min.ToString(), *ProbeBox.Max.ToString(),
			*Hull->GetRelativeTransform().ToHumanReadableString(),
			*ExpectedBox.Min.ToString(), *ExpectedBox.Max.ToString()));
	}

	// 船体腹部在原点下方：全部探针都在原点之上说明 Hull 的相对变换没乘进去。
	double MinZ = TNumericLimits<double>::Max();
	for (const FVector& Probe : Probes)
	{
		MinZ = FMath::Min(MinZ, Probe.Z);
	}
	TestTrue(TEXT("探针覆盖到船体腹部（Z < 0）"), MinZ < 0.0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraShipContactProbesActorLocalTest,
	"StellarEra.Spaceship.ContactProbesAreActorLocal",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraShipContactProbesActorLocalTest::RunTest(const FString& Parameters)
{
	UClass* ShipClass = LoadClass<ASpaceshipPawn>(
		nullptr, TEXT("/Game/Code/Pawns/BP_SpaceshipPawn.BP_SpaceshipPawn_C"));
	TestNotNull(TEXT("BP_SpaceshipPawn_C 可加载"), ShipClass);
	if (!ShipClass)
	{
		return false;
	}

	ASpaceshipPawn* CDO = Cast<ASpaceshipPawn>(ShipClass->GetDefaultObject());
	TestNotNull(TEXT("BP_SpaceshipPawn CDO"), CDO);
	if (!CDO)
	{
		return false;
	}

	USceneComponent* Root = CDO->GetRootComponent();
	TestNotNull(TEXT("CDO 有根组件"), Root);
	if (!Root)
	{
		return false;
	}

	// 回归测试：探针必须是 **Actor 本地**坐标，与 Actor 摆在世界哪里无关。
	// 曾经的 bug：Hull 成了根组件后仍用 GetRelativeTransform() 当「Hull → Actor」，
	// 而根组件的 RelativeLocation 直接存世界坐标 —— 飞船被摆到 PlayerStart（2 AU 高）
	// 后探针跟着偏 2 AU，求解器检测不到接触，飞船直接掉进行星里。
	const FVector OriginalLocation = CDO->GetActorLocation();

	Root->SetWorldLocation(FVector::ZeroVector);
	TArray<FVector> AtOrigin;
	TestTrue(TEXT("原点处能生成探针"), CDO->BuildContactProbes(AtOrigin));

	Root->SetWorldLocation(FVector(1.0e6, -2.0e6, 3.0e6));
	TArray<FVector> FarAway;
	TestTrue(TEXT("远处能生成探针"), CDO->BuildContactProbes(FarAway));

	// 先把 CDO 还回原样，避免影响同一会话里后续的生成/测试。
	Root->SetWorldLocation(OriginalLocation);

	TestTrue(TEXT("两次都生成了探针"), AtOrigin.Num() > 0 && FarAway.Num() > 0);
	TestEqual(TEXT("两次探针数量一致"), FarAway.Num(), AtOrigin.Num());
	if (AtOrigin.Num() == 0 || AtOrigin.Num() != FarAway.Num())
	{
		return false;
	}

	double MaxDeltaCm = 0.0;
	for (int32 Index = 0; Index < AtOrigin.Num(); ++Index)
	{
		MaxDeltaCm = FMath::Max(MaxDeltaCm,
			double(FVector::Dist(AtOrigin[Index], FarAway[Index])));
	}

	const bool bIsActorLocal = MaxDeltaCm < 0.01;
	TestTrue(TEXT("探针不随 Actor 摆位平移（Actor 本地坐标）"), bIsActorLocal);
	if (!bIsActorLocal)
	{
		AddError(FString::Printf(
			TEXT("Actor 从原点移到 (1e6,-2e6,3e6) 后，同一个探针偏了 %.2f cm；")
			TEXT("说明探针里混进了世界位置（根组件的 GetRelativeTransform 是"
				 "世界变换，不是 Actor 相对变换）"),
			MaxDeltaCm));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraShipLandingDetectionTest,
	"StellarEra.Spaceship.LandingDetectionReachesHullBottom",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraShipLandingDetectionTest::RunTest(const FString& Parameters)
{
	// 回归测试：飞船是**船底**着地，机身中心比地面高一整个船体深度（本资源约 1000 cm）。
	// 落地检测球心在机身中心，旧版固定半径 600 cm 够不到地面 —— IsLanded() 恒为假，
	// 按 E 也下不了船。这里不用真实行星，而是在独立编辑器世界里生成飞船 + WorldStatic 平台。

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

	UClass* ShipClass = LoadClass<ASpaceshipPawn>(
		nullptr, TEXT("/Game/Code/Pawns/BP_SpaceshipPawn.BP_SpaceshipPawn_C"));
	TestNotNull(TEXT("BP_SpaceshipPawn_C 可加载"), ShipClass);
	if (!ShipClass)
	{
		return false;
	}

	const FVector ShipLocation(0.0, 0.0, 1.0e7);
	ASpaceshipPawn* Ship = World->SpawnActor<ASpaceshipPawn>(
		ShipClass, FTransform(FRotator::ZeroRotator, ShipLocation));
	TestNotNull(TEXT("能生成飞船"), Ship);
	if (!Ship)
	{
		return false;
	}

	Ship->RebuildContactProbes();
	TArray<FVector> Probes;
	const bool bHasProbes = Ship->BuildContactProbes(Probes);
	TestTrue(TEXT("Hull 有 Simple Collision 探针"), bHasProbes && Probes.Num() > 0);
	if (Probes.Num() == 0)
	{
		return false;
	}

	// 机身未旋转：最低探针的 -Z 就是船底到机身中心的距离。
	double DepthCm = 0.0;
	for (const FVector& Probe : Probes)
	{
		DepthCm = FMath::Max(DepthCm, -double(Probe.Z));
	}

	const float RadiusCm = Ship->GetLandingQueryRadiusCm();
	TestTrue(TEXT("船底在机身中心下方"), DepthCm > 0.0);
	TestTrue(TEXT("落地检测球半径覆盖船底（否则船底着地也判不出落地）"),
		RadiusCm >= DepthCm);

	// 平台：引擎立方体 100 cm 见方，放大到 2000×2000×100，顶面放在 TopZ。
	auto SpawnPlatform = [World](const FVector& Center, double TopZ) -> AStaticMeshActor*
	{
		AStaticMeshActor* Platform = World->SpawnActor<AStaticMeshActor>();
		if (!Platform)
		{
			return nullptr;
		}
		UStaticMeshComponent* Mesh = Platform->GetStaticMeshComponent();
		Mesh->SetMobility(EComponentMobility::Movable);
		Mesh->SetStaticMesh(LoadObject<UStaticMesh>(
			nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
		Mesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		Mesh->SetCollisionObjectType(ECC_WorldStatic);
		Mesh->SetCollisionResponseToAllChannels(ECR_Block);
		Platform->SetActorScale3D(FVector(20.f, 20.f, 1.f));
		Platform->SetActorLocation(FVector(Center.X, Center.Y, TopZ - 50.0));
		return Platform;
	};

	// 用例 1：顶面落在检测球内 10 cm —— 必须判为落地。
	AStaticMeshActor* NearPlatform =
		SpawnPlatform(ShipLocation, ShipLocation.Z - RadiusCm + 10.0);
	TestNotNull(TEXT("近平台生成"), NearPlatform);
	const bool bNearLanded = Ship->IsLanded();
	TestTrue(TEXT("平台顶面在检测球内时 IsLanded 为真"), bNearLanded);
	if (NearPlatform)
	{
		NearPlatform->Destroy();
	}

	// 用例 2：顶面落在检测球外 100 cm —— 必须判为未落地。
	AStaticMeshActor* FarPlatform =
		SpawnPlatform(ShipLocation, ShipLocation.Z - RadiusCm - 100.0);
	TestNotNull(TEXT("远平台生成"), FarPlatform);
	const bool bFarLanded = Ship->IsLanded();
	TestFalse(TEXT("平台顶面在检测球外时 IsLanded 为假"), bFarLanded);
	if (FarPlatform)
	{
		FarPlatform->Destroy();
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraShipLandingUsesRelativeSpeedTest,
	"StellarEra.Spaceship.LandedUsesRelativeSpeed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraShipLandingUsesRelativeSpeedTest::RunTest(const FString& Parameters)
{
	// 回归测试：飞船的**绝对**速度就是行星的轨道速度（本关约 7.3 km/s = 7.3e5 cm/s）。
	// IsLanded 的 200 cm/s 速度门若比绝对值，落地后永远为假。判据必须是**相对参考行星**的速度。

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

	UClass* ShipClass = LoadClass<ASpaceshipPawn>(
		nullptr, TEXT("/Game/Code/Pawns/BP_SpaceshipPawn.BP_SpaceshipPawn_C"));
	TestNotNull(TEXT("BP_SpaceshipPawn_C 可加载"), ShipClass);
	if (!ShipClass)
	{
		return false;
	}

	const FVector ShipLocation(0.0, 0.0, 1.0e7);
	ASpaceshipPawn* Ship = World->SpawnActor<ASpaceshipPawn>(
		ShipClass, FTransform(FRotator::ZeroRotator, ShipLocation));
	TestNotNull(TEXT("能生成飞船"), Ship);
	if (!Ship)
	{
		return false;
	}

	Ship->RebuildContactProbes();

	// 平台放在检测球内 10 cm：几何上确实「船底贴地」。
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

	// 行星轨道速度（世界厘米/秒）：7.17 km/s，远高于 200 cm/s 的阈值。
	const FVector PlanetVelocityCmPerSecond(717354.0, 0.0, 0.0);

	// ① 贴着行星停稳：相对速度为零，必须判为落地。
	Ship->SetCosmosVelocity(PlanetVelocityCmPerSecond);
	Ship->SetReferencePlanetVelocity(PlanetVelocityCmPerSecond);
	TestTrue(TEXT("相对速度为 0 时，落在平台上必须判为已落地"), Ship->IsLanded());

	// ② 同样的几何，但相对速度 10 m/s（1000 cm/s）超过阈值：必须判为未落地。
	// 这一条守住的是速度门本身 —— 不能为了修 ① 把门整个拆掉。
	Ship->SetCosmosVelocity(PlanetVelocityCmPerSecond + FVector(1000.0, 0.0, 0.0));
	TestFalse(TEXT("相对速度超过阈值时必须判为未落地"), Ship->IsLanded());

	// ③ 没有参考行星（深空）：相对速度退化为绝对速度，同样受阈值约束。
	Ship->SetCosmosVelocity(FVector(1000.0, 0.0, 0.0));
	Ship->SetReferencePlanetVelocity(FVector::ZeroVector);
	TestFalse(TEXT("没有参考行星时按绝对速度判断"), Ship->IsLanded());

	Ship->SetCosmosVelocity(FVector::ZeroVector);
	TestTrue(TEXT("没有参考行星且速度为零时仍判为已落地"), Ship->IsLanded());

	return true;
}

#endif
