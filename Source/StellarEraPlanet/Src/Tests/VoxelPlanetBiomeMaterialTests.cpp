// Copyright Voxel Plugin SAS. All Rights Reserved.
// biome MID 的父材质剥离：PIE 复制后 VoxelMaterial 是上一轮 MID 的副本。

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "VoxelPlanetActor.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/MaterialInstanceDynamic.h"

#if WITH_DEV_AUTOMATION_TESTS

// 剥 MID 只剥 MID，别的原样放行。钉 VoxelRenderUtilities.cpp:518 那个 ensure 的根因：
// PIE 复制后 VoxelMaterial 成了上一轮 MID 的副本，拿它当父材质创建新 MID 会被
// SetParentInternal 静默拒绝（只接受 Material / MIC），Parent 留在 nullptr。
// 非 MID（尤其 MIC）必须原样返回 —— 多剥一层会把用户在 MIC 里设的参数丢掉。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraBiomeMaterialUnwrapTest,
	"StellarEra.Planet.BiomeMaterial.UnwrapDynamicInstance",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraBiomeMaterialUnwrapTest::RunTest(const FString& Parameters)
{
	UMaterial* Base = NewObject<UMaterial>(GetTransientPackage());
	if (!TestNotNull(TEXT("基准材质创建成功"), Base))
	{
		return false;
	}

	TestNull(TEXT("nullptr 进 nullptr 出"),
		AVoxelPlanetActor::StripMaterialInstanceDynamic(nullptr));
	TestTrue(TEXT("Material 原样返回"),
		AVoxelPlanetActor::StripMaterialInstanceDynamic(Base) == Base);

	// MIC 是 SetParentInternal 接受的合法父材质，不能被剥掉。
	UMaterialInstanceConstant* Constant = NewObject<UMaterialInstanceConstant>(GetTransientPackage());
	Constant->Parent = Base;
	TestTrue(TEXT("MaterialInstanceConstant 原样返回"),
		AVoxelPlanetActor::StripMaterialInstanceDynamic(Constant) == Constant);

	UMaterialInstanceDynamic* Dynamic = UMaterialInstanceDynamic::Create(Base, GetTransientPackage());
	if (TestNotNull(TEXT("MID 创建成功"), Dynamic))
	{
		UMaterialInterface* Stripped = AVoxelPlanetActor::StripMaterialInstanceDynamic(Dynamic);
		TestTrue(TEXT("MID 剥到父材质"), Stripped == Base);
		TestTrue(TEXT("剥完不再是 MID 自己"), Stripped != Dynamic);
	}

	// 父为空的 MID 正是 SetParentInternal 拒绝后留下的形态，必须返回 nullptr 让调用方兜底：
	// 原样返出的话插件那边的 Cast 会成功、Parent 却是空，照样在同一个 ensure 上炸。
	UMaterialInstanceDynamic* Orphan = NewObject<UMaterialInstanceDynamic>(GetTransientPackage());
	Orphan->Parent = nullptr;
	TestNull(TEXT("父为空的 MID → nullptr"),
		AVoxelPlanetActor::StripMaterialInstanceDynamic(Orphan));

	return true;
}

// 切图崩溃回归（2026-11-01）：编辑器态插件把 VoxelMaterial 原样登记进全局
// FVoxelMaterialInterfaceManager（GC root）。MID 的 Outer 若是行星 Actor，可达链就是
// MID → Actor → Level → World → Package 永远收不掉，切图时 UEditorEngine 以
// "World Memory Leaks" 直接 Fatal。所以 Outer 必须是瞬态包。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraBiomeMaterialOuterTest,
	"StellarEra.Planet.BiomeMaterial.TransientOuter",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraBiomeMaterialOuterTest::RunTest(const FString& Parameters)
{
	UMaterial* Base = NewObject<UMaterial>(GetTransientPackage());
	if (!TestNotNull(TEXT("基准材质创建成功"), Base))
	{
		return false;
	}

	UMaterialInstanceDynamic* Dynamic = AVoxelPlanetActor::CreateBiomeMaterialMID(Base);
	if (!TestNotNull(TEXT("biome MID 创建成功"), Dynamic))
	{
		return false;
	}

	TestTrue(TEXT("父材质正确"), Dynamic->Parent == Base);
	TestTrue(TEXT("Outer 是瞬态包（否则切图会 World Memory Leaks）"),
		Dynamic->GetOuter() == GetTransientPackage());

	return true;
}

#endif
