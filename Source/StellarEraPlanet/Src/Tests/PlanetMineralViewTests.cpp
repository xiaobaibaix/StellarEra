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
	/** 取编辑器世界。不能写 GEngine->GetEditorWorldContext() —— 那是 UEditorEngine 的
	 *  方法，本模块刻意不带 UnrealEd 依赖（见 Build.cs）；按 WorldType 扫 UEngine 的
	 *  世界上下文等价。 */
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

	// 槽 0 空、槽 1 一个三角形：空元素仍要占槽，否则图例与数字键的下标整体错位。
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
	// 刻意**不** spawn 行星：SpawnActor 会跑 OnConstruction → 建整个体素世界，无头 3 FPS
	// 下能耗几分钟。这里 NewObject 未注册对象只验转发，体素侧由行星自己的测试覆盖。
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
	// 组件 Owner 由外链推出（构造时取 GetTypedOuter<AActor>），不需挂进 Actor 组件表。
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

	// 细节面板改槽：走 PostEditChangeProperty 那条胶水，而不是直接调 Apply。
	// PostEditChangeProperty 是 WITH_EDITOR-only 的重载，Game 目标里不存在。
#if WITH_EDITOR
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
#endif

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

// IsPreviewStale / MarkPreviewStale / RefreshMineralPreview 都是 WITH_EDITOR-only。
#if WITH_EDITOR
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlanetMineralViewPresetEditStaleTest,
	"StellarEra.Planet.MineralView.PresetEditMarksStale",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetMineralViewPresetEditStaleTest::RunTest(const FString& Parameters)
{
	// 与 GeologyAssetMirror 同理不 spawn 行星。这里验「预设改动只标脏、不重建」的状态机；
	// 真重建要一颗真行星，由 PIE 手工验收覆盖。
	AVoxelPlanetActor* Planet = NewObject<AVoxelPlanetActor>(GetTransientPackage());
	if (!TestNotNull(TEXT("行星 Actor"), Planet))
	{
		return false;
	}
	UPlanetMineralViewComponent* View = NewObject<UPlanetMineralViewComponent>(Planet);
	if (!TestNotNull(TEXT("矿物组件"), View))
	{
		return false;
	}

	TestFalse(TEXT("刚建出来不算过期"), View->IsPreviewStale());
	TestTrue(TEXT("第一次改动：从新变旧的那一下"), View->MarkPreviewStale());
	TestTrue(TEXT("已标脏"), View->IsPreviewStale());
	TestFalse(TEXT("连发的后续改动不再重复标脏（也就不再打日志）"), View->MarkPreviewStale());
	TestTrue(TEXT("仍然是脏的"), View->IsPreviewStale());

	// 没有世界/地质场时手动刷新必须安全失败：不崩，且把「过期」清掉（预览都没了）。
	View->RefreshMineralPreview();
	TestFalse(TEXT("刷新（这里必然失败）之后不再标脏"), View->IsPreviewStale());

	return true;
}
#endif // WITH_EDITOR

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlanetMineralViewStatisticsTest,
	"StellarEra.Planet.MineralView.Statistics",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetMineralViewStatisticsTest::RunTest(const FString& Parameters)
{
	// 统计的对外契约：没有地质场时安全失败（返回 0 / 空表），不能崩。用 NewObject
	// 而非 SpawnActor 出组件 —— 后者跑 OnConstruction 建整个体素世界，无头下能耗几分钟。
	AVoxelPlanetActor* Planet = NewObject<AVoxelPlanetActor>(GetTransientPackage());
	UPlanetMineralViewComponent* View = Planet
		? NewObject<UPlanetMineralViewComponent>(Planet)
		: nullptr;
	if (!TestNotNull(TEXT("组件"), View))
	{
		return false;
	}

	TestEqual(TEXT("没有地质场时体积为 0"),
		View->GetElementVolumeCm3(0, 100.f), 0.f, 1e-6f);
	TestEqual(TEXT("没有地质场时整表为空"),
		View->GetAllElementVolumesCm3(100.f).Num(), 0);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
