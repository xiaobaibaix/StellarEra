// 恒星 Actor 的骨架：空场景根 + 网格 + 两个组件齐备、网格缩放正确、不 Tick。

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Components/StaticMeshComponent.h"
#include "StarBodyActor.h"
#include "CosmosOrbitComponent.h"
#include "CosmosTypes.h"
#include "StarBodyComponent.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	UWorld* FindStarBodyActorEditorWorld()
	{
		if (!GEngine)
		{
			return nullptr;
		}
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			if (Context.WorldType == EWorldType::Editor && Context.World())
			{
				return Context.World();
			}
		}
		return nullptr;
	}
} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraStarBodyActorSkeletonTest,
	"StellarEra.Cosmos.StarBodyActor.Skeleton",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FStellarEraStarBodyActorSkeletonTest::RunTest(const FString& Parameters)
{
	UWorld* World = FindStarBodyActorEditorWorld();
	if (!TestNotNull(TEXT("编辑器世界"), World))
	{
		return false;
	}

	AStarBodyActor* Star = World->SpawnActor<AStarBodyActor>(
		AStarBodyActor::StaticClass(), FVector(0.0, 0.0, 0.0), FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("恒星 Actor"), Star))
	{
		return false;
	}

	TestNotNull(TEXT("场景根组件"), Star->StarRoot.Get());
	TestNotNull(TEXT("恒星网格组件"), Star->StarMesh.Get());
	TestNotNull(TEXT("N 体轨道组件"), Star->Orbit.Get());
	TestNotNull(TEXT("恒星标记组件"), Star->StarBody.Get());

	if (Star->StarRoot)
	{
		// 根是空场景组件，网格挂在它下面：相机 / 轨迹挂在根上才不会被恒星缩放带跑。
		TestEqual(TEXT("根组件是空场景根"), Star->GetRootComponent(),
			static_cast<USceneComponent*>(Star->StarRoot.Get()));
	}

	if (Star->StarMesh)
	{
		TestEqual(TEXT("网格挂在场景根下"), Star->StarMesh->GetAttachParent(),
			static_cast<USceneComponent*>(Star->StarRoot.Get()));

		const FVector Scale = Star->StarMesh->GetRelativeScale3D();
		TestEqual(TEXT("缩放 X"), Scale.X, 10000.0, 0.5);
		TestEqual(TEXT("缩放 Y"), Scale.Y, 10000.0, 0.5);
		TestEqual(TEXT("缩放 Z"), Scale.Z, 10000.0, 0.5);

		// 物理模式：局部半径 = 真实半径，Actor 再乘 FarScale 得到远场尺寸。
		TestEqual(TEXT("默认物理半径"), Star->PhysicalRadiusKm, 696340.0f, 0.5f);
		Star->ApplyScaleMode(true);
		const double ExpectedPhysicalScale =
			double(Star->PhysicalRadiusKm) * CosmosUnits::CentimetersPerKilometer / 50.0;
		const FVector PhysicalScale = Star->StarMesh->GetRelativeScale3D();
		TestEqual(TEXT("物理缩放 X"), PhysicalScale.X, ExpectedPhysicalScale, 1000.0);
		TestEqual(TEXT("物理缩放 Y"), PhysicalScale.Y, ExpectedPhysicalScale, 1000.0);
		TestEqual(TEXT("物理缩放 Z"), PhysicalScale.Z, ExpectedPhysicalScale, 1000.0);

		Star->ApplyScaleMode(false);
		const FVector RestoredScale = Star->StarMesh->GetRelativeScale3D();
		TestEqual(TEXT("恢复缩放 X"), RestoredScale.X, 10000.0, 0.5);
		TestEqual(TEXT("恢复缩放 Y"), RestoredScale.Y, 10000.0, 0.5);
		TestEqual(TEXT("恢复缩放 Z"), RestoredScale.Z, 10000.0, 0.5);

		TestEqual(TEXT("不参与碰撞"), Star->StarMesh->GetCollisionEnabled(),
			ECollisionEnabled::NoCollision);

		const UStaticMesh* Mesh = Star->StarMesh->GetStaticMesh();
		if (TestNotNull(TEXT("静态网格"), Mesh))
		{
			// BasicShapes/Sphere 的外接半径是 50cm，×10000 = 5e5cm = 5 公里。
			const FBoxSphereBounds Bounds = Mesh->GetBounds();
			TestEqual(TEXT("球半径 50cm"), (double)Bounds.SphereRadius, 50.0, 0.1);
		}
	}

	TestFalse(TEXT("恒星 Actor 不 Tick"), Star->PrimaryActorTick.bCanEverTick);
	TestFalse(TEXT("轨道组件不 Tick"), Star->Orbit && Star->Orbit->PrimaryComponentTick.bCanEverTick);

	Star->Destroy();
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
