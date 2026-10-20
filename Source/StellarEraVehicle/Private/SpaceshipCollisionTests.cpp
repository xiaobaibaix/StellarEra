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

#endif
