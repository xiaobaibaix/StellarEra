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
#include "PlanetMineralViewComponent.h"
#include "PlanetGeologyPreset.h"
#include "VoxelPlanetActor.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	/**
	 * 取编辑器世界。
	 *
	 * 不能写 GEngine->GetEditorWorldContext()：那个方法是 UEditorEngine 的，而本模块
	 * 刻意不带 UnrealEd 依赖（理由见 StellarEraMinerals.Build.cs 顶部）。这里按 WorldType
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlanetMineralViewEmptySlotTest,
	"StellarEra.Planet.MineralView.EmptySlot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetMineralViewEmptySlotTest::RunTest(const FString& Parameters)
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

	// 槽 0 空、槽 1 一个三角形：空元素仍要占一个组件槽，否则图例、数字键的
	// 下标会整体错位。
	TArray<FPlanetMineralMesh> Meshes;
	Meshes.SetNum(2);
	Meshes[1] = MakeMineralViewTriangleMesh(1);

	TArray<FPlanetGeologyElement> Elements;
	Elements.SetNum(2);
	Elements[0].Id = TEXT("a");
	Elements[0].DisplayName = TEXT("A");
	Elements[0].Color = FLinearColor::Red;
	Elements[1].Id = TEXT("b");
	Elements[1].DisplayName = TEXT("B");
	Elements[1].Color = FLinearColor::Blue;

	const bool bBuilt = View->Build(Meshes, FLinearColor::Gray,
		/*SurfaceRadiusCm=*/100000.f, Elements);
	TestTrue(TEXT("Build 成功"), bBuilt);
	if (!bBuilt)
	{
		View->Destroy();
		return false;
	}

	TestEqual(TEXT("两个元素两个组件槽"), View->NumElements(), 2);

	UDynamicMeshComponent* EmptyComponent = View->GetElementMesh(0);
	UDynamicMeshComponent* OneComponent = View->GetElementMesh(1);
	if (!TestNotNull(TEXT("空槽也有组件"), EmptyComponent)
		|| !TestNotNull(TEXT("非空槽的组件"), OneComponent))
	{
		View->Destroy();
		return false;
	}

	const UE::Geometry::FDynamicMesh3* EmptyMesh = EmptyComponent->GetMesh();
	const UE::Geometry::FDynamicMesh3* OneMesh = OneComponent->GetMesh();
	if (!TestNotNull(TEXT("空槽的动态网格"), EmptyMesh)
		|| !TestNotNull(TEXT("非空槽的动态网格"), OneMesh))
	{
		View->Destroy();
		return false;
	}

	TestEqual(TEXT("空槽没有三角形"), EmptyMesh->TriangleCount(), 0);
	TestEqual(TEXT("非空槽有一个三角形"), OneMesh->TriangleCount(), 1);
	TestTrue(TEXT("空槽默认仍然可见"), View->IsElementVisible(0));

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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlanetMineralViewCellsPerFeatureTest,
	"StellarEra.Planet.MineralView.CellsPerFeature",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetMineralViewCellsPerFeatureTest::RunTest(const FString& Parameters)
{
	const UPlanetMineralViewComponent* Defaults = GetDefault<UPlanetMineralViewComponent>();
	TestNotNull(TEXT("组件 CDO"), Defaults);
	if (!Defaults)
	{
		return false;
	}
	TestEqual(TEXT("默认每特征 12 格"), Defaults->CellsPerFeature, 12);

	PlanetMineralSurface::FCacheKey Key;
	Key.CellsPerFeature = 12;
	PlanetMineralSurface::FCacheKey Same = Key;
	TestTrue(TEXT("同分辨率键相等"), Key == Same);

	Same.CellsPerFeature = 24;
	TestTrue(TEXT("改分辨率使键不等"), Key != Same);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlanetMineralViewGeologyAssetMirrorTest,
	"StellarEra.Planet.MineralView.GeologyAssetMirror",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetMineralViewGeologyAssetMirrorTest::RunTest(const FString& Parameters)
{
	// 刻意**不** spawn 行星：SpawnActor 会跑 OnConstruction → RebuildPlanet →
	// 建整个体素世界，无头进程被限 3 FPS 时一条属性转发测试能耗上几分钟。
	// 这里 NewObject 出未注册的对象，只验转发本身；体素那侧由行星自己的测试覆盖。
	UPlanetGeologyPreset* PresetA = NewObject<UPlanetGeologyPreset>(GetTransientPackage());
	UPlanetGeologyPreset* PresetB = NewObject<UPlanetGeologyPreset>(GetTransientPackage());
	if (!TestNotNull(TEXT("预设 A"), PresetA) || !TestNotNull(TEXT("预设 B"), PresetB))
	{
		return false;
	}

	AVoxelPlanetActor* Planet = NewObject<AVoxelPlanetActor>(GetTransientPackage());
	if (!TestNotNull(TEXT("行星 Actor"), Planet))
	{
		return false;
	}
	// 组件的 Owner 由外链推出（UActorComponent 构造时取 GetTypedOuter<AActor>），
	// 不需要另外挂到 Actor 的组件表上。
	UPlanetMineralViewComponent* View = NewObject<UPlanetMineralViewComponent>(Planet);
	if (!TestNotNull(TEXT("矿物组件"), View))
	{
		return false;
	}

	TestNull(TEXT("槽默认空"), View->GeologyAsset.Get());
	TestNull(TEXT("行星那份默认也空"), Planet->GetGeologyPresetAsset());

	// 行星 → 组件
	Planet->GeologyAsset = PresetA;
	View->RefreshGeologyAssetFromPlanet();
	TestTrue(TEXT("刷新后槽读到行星那份"), View->GeologyAsset.Get() == PresetA);

	// 组件 → 行星
	View->GeologyAsset = PresetB;
	TestTrue(TEXT("写回成功"), View->ApplyGeologyAssetToPlanet());
	TestTrue(TEXT("行星跟着变"), Planet->GetGeologyPresetAsset() == PresetB);

	// 细节面板改槽：走 PostEditChangeProperty 那条胶水，而不是直接调 Apply
	View->GeologyAsset = PresetA;
	if (FProperty* SlotProperty = FindFProperty<FProperty>(
		UPlanetMineralViewComponent::StaticClass(), TEXT("GeologyAsset")))
	{
		FPropertyChangedEvent Event(SlotProperty, EPropertyChangeType::ValueSet);
		View->PostEditChangeProperty(Event);
		TestTrue(TEXT("面板改槽也写回行星"), Planet->GetGeologyPresetAsset() == PresetA);
	}
	else
	{
		AddError(TEXT("找不到 GeologyAsset 属性：转发没有被接上"));
	}

	// 宿主不是行星：不转发、不崩、也不静默留一个假值
	AActor* PlainActor = NewObject<AActor>(GetTransientPackage());
	UPlanetMineralViewComponent* Orphan = PlainActor
		? NewObject<UPlanetMineralViewComponent>(PlainActor)
		: nullptr;
	if (TestNotNull(TEXT("非行星宿主上的组件"), Orphan))
	{
		TestNull(TEXT("宿主不是行星 → 读不到"), Orphan->GetPlanetGeologyAsset());
		Orphan->GeologyAsset = PresetA;
		Orphan->RefreshGeologyAssetFromPlanet();
		TestNull(TEXT("宿主不是行星 → 刷新把槽清空"), Orphan->GeologyAsset.Get());
		TestFalse(TEXT("宿主不是行星 → 写回失败"), Orphan->ApplyGeologyAssetToPlanet());
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
