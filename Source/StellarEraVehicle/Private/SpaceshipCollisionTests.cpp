// Copyright Voxel Plugin SAS. All Rights Reserved.
//
// 飞船碰撞形状的资产级验收测试。
//
// 断言的是「改完以后资源里真的长这样」，而不是「能编译」：
//   1. BP_SpaceshipPawn 的 CDO 上，Hull 开启 QueryAndPhysics、根球 Collision 关闭碰撞；
//   2. 根球半径仍是 300（N 体接触半径的来源，换碰撞形状不应改它）；
//   3. Hull 用的静态网格确实带 Simple Collision 凸包 —— 没有的话打开碰撞也不会挡任何东西。
//
// editor world 不跑 BeginPlay，所以这里读 CDO；运行期组件用的是同一套构造逻辑。

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "PhysicsEngine/BodySetup.h"
#include "SpaceshipPawn.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraSpaceshipCollisionTest,
	"StellarEra.Vehicle.SpaceshipCollision",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraSpaceshipCollisionTest::RunTest(const FString& Parameters)
{
	UClass* ShipClass = LoadClass<ASpaceshipPawn>(
		nullptr, TEXT("/Game/Code/BP_SpaceshipPawn.BP_SpaceshipPawn_C"));
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

	USphereComponent* Collision = Cast<USphereComponent>(
		CDO->GetDefaultSubobjectByName(TEXT("Collision")));
	UStaticMeshComponent* Hull = Cast<UStaticMeshComponent>(
		CDO->GetDefaultSubobjectByName(TEXT("Hull")));
	TestNotNull(TEXT("根组件 Collision"), Collision);
	TestNotNull(TEXT("船体组件 Hull"), Hull);
	if (!Collision || !Hull)
	{
		return false;
	}

	TestTrue(TEXT("根球 Collision 已关闭碰撞"),
		Collision->GetCollisionEnabled() == ECollisionEnabled::NoCollision);
	TestTrue(TEXT("根球半径仍是 300 cm（N 体接触半径）"),
		FMath::IsNearlyEqual(Collision->GetScaledSphereRadius(), 300.f));

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
	"StellarEra.Vehicle.ShipContactProbes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraShipContactProbesTest::RunTest(const FString& Parameters)
{
	UClass* ShipClass = LoadClass<ASpaceshipPawn>(
		nullptr, TEXT("/Game/Code/BP_SpaceshipPawn.BP_SpaceshipPawn_C"));
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

	// 结构断言而不是数字断言：每个探针都必须落在「Hull 网格包围盒 × Hull 相对变换」
	// 附近。网格换了形状测试照样有效，只有探针变换接错时才会红。
	const UStaticMeshComponent* Hull = Cast<UStaticMeshComponent>(
		CDO->GetDefaultSubobjectByName(TEXT("Hull")));
	TestNotNull(TEXT("船体组件 Hull"), Hull);
	if (!Hull || !Hull->GetStaticMesh())
	{
		return false;
	}

	const FBox ExpectedBox =
		Hull->GetStaticMesh()->GetBoundingBox().TransformBy(Hull->GetRelativeTransform());

	// 容差 100 cm：Simple Collision 常常是单独导的、比渲染网格略大（本资源顶部
	// 超出约 27 cm），所以不能要求严格在包围盒内。而 Hull 相对变换的 Translation
	// 是 (0,0,653) —— 漏乘或用错顺序都会偏出几百厘米，100 cm 足够把接线错误区分开。
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
		// 失败时把第一个越界点和参照框一起打出来 —— 光说「不在盒子里」查不出是
		// 变换漏乘还是网格本来就有碰撞体外包。
		AddError(FString::Printf(
			TEXT("越界探针 %s（共 %d 个，最大越界 %.2f cm，探针包围盒 %s .. %s）；"
				 "Hull 相对变换 %s；包围盒 %s .. %s"),
			*Offender->ToString(), Probes.Num(), MaxOvershoot,
			*ProbeBox.Min.ToString(), *ProbeBox.Max.ToString(),
			*Hull->GetRelativeTransform().ToHumanReadableString(),
			*ExpectedBox.Min.ToString(), *ExpectedBox.Max.ToString()));
	}

	// 船体腹部在根组件下方（本资源约 -343 cm）：全部探针都在根之上说明
	// 相对变换漏乘了 Hull 的位置。
	double MinZ = TNumericLimits<double>::Max();
	for (const FVector& Probe : Probes)
	{
		MinZ = FMath::Min(MinZ, Probe.Z);
	}
	TestTrue(TEXT("探针覆盖到船体腹部（Z < 0）"), MinZ < 0.0);

	return true;
}

#endif
