// 观察体的冒烟测试：每个元素一个动态网格组件、显隐互不影响、空数据不崩。

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "Engine/EngineTypes.h"
#include "Engine/World.h"
#include "Components/DynamicMeshComponent.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "PlanetMineralSurface.h"
#include "PlanetMineralViewActor.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	/**
	 * 取编辑器世界。
	 *
	 * 不能写 GEngine->GetEditorWorldContext()：那个方法是 UEditorEngine 的，而本模块
	 * 刻意不带 UnrealEd 依赖（见 StellarEraPlanet.Build.cs 末尾）。这里按 WorldType
	 * 扫一遍 UEngine 的世界上下文，与 UEditorEngine::GetEditorWorldContext 的实现
	 * （EditorEngine.cpp 里取第一个 EWorldType::Editor）等价。
	 */
	UWorld* FindMineralViewEditorWorld()
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

	/** 一个三角形，位置随 Index 错开。只验分组与显隐，不验几何。 */
	FPlanetMineralMesh MakeMineralViewTriangleMesh(int32 Index)
	{
		FPlanetMineralMesh Mesh;
		const float Offset = 1000.f * float(Index + 1);
		Mesh.VerticesCm.Add(FVector3f(Offset, 0.f, 0.f));
		Mesh.VerticesCm.Add(FVector3f(Offset + 1000.f, 0.f, 0.f));
		Mesh.VerticesCm.Add(FVector3f(Offset, 1000.f, 0.f));
		Mesh.Triangles.Add(0);
		Mesh.Triangles.Add(1);
		Mesh.Triangles.Add(2);
		Mesh.OccupiedCells = 1;
		return Mesh;
	}
} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlanetMineralViewActorTest,
	"StellarEra.Planet.MineralView.ActorBuild",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetMineralViewActorTest::RunTest(const FString& Parameters)
{
	UWorld* World = FindMineralViewEditorWorld();
	if (!TestNotNull(TEXT("编辑器世界"), World))
	{
		return false;
	}

	APlanetMineralViewActor* View = World->SpawnActor<APlanetMineralViewActor>();
	if (!TestNotNull(TEXT("观察体 spawn 成功"), View))
	{
		return false;
	}

	TArray<FPlanetMineralMesh> Meshes;
	Meshes.Add(MakeMineralViewTriangleMesh(0));
	Meshes.Add(MakeMineralViewTriangleMesh(1));

	TArray<FPlanetGeologyElement> Elements;
	Elements.SetNum(2);
	Elements[0].Id = TEXT("a");
	Elements[0].DisplayName = TEXT("A");
	Elements[0].Color = FLinearColor::Red;
	Elements[1].Id = TEXT("b");
	Elements[1].DisplayName = TEXT("B");
	Elements[1].Color = FLinearColor::Blue;

	const bool bBuilt = View->Build(Meshes, FLinearColor(0.1f, 0.2f, 0.1f, 1.f),
		/*SurfaceRadiusCm=*/100000.f, Elements);

	TestTrue(TEXT("Build 成功（材质已生成、引擎网格可加载）"), bBuilt);
	if (!bBuilt)
	{
		View->Destroy();
		return false;
	}

	TestEqual(TEXT("每个元素一个动态网格组件"), View->NumElements(), 2);
	TestTrue(TEXT("默认全部可见 0"), View->IsElementVisible(0));
	TestTrue(TEXT("默认全部可见 1"), View->IsElementVisible(1));

	// 三角形真的进了动态网格：AppendTriangle 失败时 Build 不会报错，只会少面。
	UDynamicMeshComponent* ElementMesh = View->GetElementMesh(0);
	if (!TestNotNull(TEXT("元素 0 的组件"), ElementMesh))
	{
		View->Destroy();
		return false;
	}
	const UE::Geometry::FDynamicMesh3* DynamicMesh = ElementMesh->GetMesh();
	if (TestNotNull(TEXT("元素 0 的动态网格"), DynamicMesh))
	{
		TestEqual(TEXT("一个三角形"), DynamicMesh->TriangleCount(), 1);
		TestEqual(TEXT("三个顶点"), DynamicMesh->VertexCount(), 3);
	}

	View->SetElementVisible(0, false);
	TestFalse(TEXT("关掉 0"), View->IsElementVisible(0));
	TestTrue(TEXT("关 0 不影响 1"), View->IsElementVisible(1));

	View->SetAllElementsVisible(true);
	TestTrue(TEXT("全开恢复 0"), View->IsElementVisible(0));

	// 越界下标必须安全：调用方是需求键与鼠标命中，不该让进程崩。
	View->SetElementVisible(99, false);
	TestFalse(TEXT("越界下标不生效"), View->IsElementVisible(99));

	View->Destroy();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlanetMineralViewActorEmptyTest,
	"StellarEra.Planet.MineralView.Empty",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetMineralViewActorEmptyTest::RunTest(const FString& Parameters)
{
	UWorld* World = FindMineralViewEditorWorld();
	if (!TestNotNull(TEXT("编辑器世界"), World))
	{
		return false;
	}

	APlanetMineralViewActor* View = World->SpawnActor<APlanetMineralViewActor>();
	if (!TestNotNull(TEXT("观察体 spawn 成功"), View))
	{
		return false;
	}

	const bool bBuilt = View->Build({}, FLinearColor::Gray, 100000.f, {});
	TestTrue(TEXT("空元素表也能建出球壳"), bBuilt);
	TestEqual(TEXT("没有元素组件"), View->NumElements(), 0);

	View->Destroy();
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
