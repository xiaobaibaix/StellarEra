// Copyright Voxel Plugin SAS. All Rights Reserved.
// biome MID 的父材质剥离：PIE 复制后 VoxelMaterial 是上一轮 MID 的副本。

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "VoxelPlanetActor.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/MaterialInstanceDynamic.h"

#if WITH_DEV_AUTOMATION_TESTS

// 剥 MID 只能剥 MID，别的必须原样放行。
//
// 这条钉的是 VoxelRenderUtilities.cpp:518 那个 ensure 的根因：PIE 复制关卡时
// VoxelMaterial（非 Transient）被拷成上一轮 BiomeMaterialMID 的副本，而
// BaseVoxelMaterial（Transient）被清空。于是「用户设的材质」读出来是 MID，
// 拿它当父材质创建新 MID 会被 SetParentInternal 静默拒绝（只接受 Material /
// MaterialInstanceConstant），新 MID 的 Parent 留在 nullptr，插件建 chunk
// 材质时回退默认材质，Cast<UMaterialInstanceDynamic> 失败 → ensure。
//
// 所以这里同时钉两个方向：
// - MID 必须被剥到父材质，不能原样返出去；
// - 非 MID（尤其是 MaterialInstanceConstant）必须原样返回 —— 多剥一层会把
//   用户在 MIC 里设的参数丢掉。
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

	// 父为空的 MID —— 正是 SetParentInternal 拒绝之后留下的形态。必须返回
	// nullptr 让调用方走兜底：原样返出去的话，插件那边的 Cast 会成功、Parent
	// 却是空，照样在同一个 ensure 上炸。
	UMaterialInstanceDynamic* Orphan = NewObject<UMaterialInstanceDynamic>(GetTransientPackage());
	Orphan->Parent = nullptr;
	TestNull(TEXT("父为空的 MID → nullptr"),
		AVoxelPlanetActor::StripMaterialInstanceDynamic(Orphan));

	return true;
}

#endif
