// 水文接线的回归测试：没有水的行星不该烘焙，也不该凭空长出水面。
//
// 两个方向都在这里：Plumbing 钉「空」的一侧（纯球模块早退、不烘焙），
// Cache 钉「有水」的一侧 —— 用地形替身真正走进烘焙分支，验证缓存键
// （模块指针 + 半径 + 水体配置）在命中、失效、参数变化三种情况下的行为。

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "PlanetHydrologyField.h"
#include "PlanetHydrologyTestTerrain.h"
#include "VoxelPlanetGenerator.h"
#include "VoxelPlanetSphereTerrain.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraHydrologyPlumbingTest,
	"StellarEra.Planet.Hydrology.Plumbing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraHydrologyPlumbingTest::RunTest(const FString& Parameters)
{
	// --- 默认模块：不出水 ---
	UVoxelPlanetSphereTerrain* Sphere = NewObject<UVoxelPlanetSphereTerrain>();
	TestNotNull(TEXT("纯球模块必须能实例化"), Sphere);
	if (!Sphere)
	{
		return false;
	}

	FPlanetWaterConfig Config;
	TestFalse(TEXT("默认 GetWaterConfig 必须返回 false（纯球不出水）"),
		Sphere->GetWaterConfig(Config));
	TestEqual(TEXT("默认 GetRawOffset 必须是 0（光滑球）"),
		Sphere->GetRawOffset(FVector(0.3, 0.5, 0.8).GetSafeNormal()), 0.f);

	// --- 生成器：没有水就不烘焙 ---
	UVoxelPlanetGenerator* Generator = NewObject<UVoxelPlanetGenerator>();
	TestNotNull(TEXT("生成器必须能实例化"), Generator);
	if (!Generator)
	{
		return false;
	}

	Generator->Terrain = nullptr;   // 走纯球兜底
	Generator->Radius = 500.f;
	Generator->RefreshHydrology();
	TestNull(TEXT("纯球行星不能有水文场"), Generator->GetHydrology().Get());
	TestNull(TEXT("上下文里也不能有水文场"),
		Generator->MakeTerrainContext().Hydrology.Get());

	// 改半径不该凭空造出水来（缓存重置路径也不能出错）。
	Generator->Radius = 800.f;
	Generator->RefreshHydrology();
	TestNull(TEXT("改半径后仍然没有水"), Generator->GetHydrology().Get());

	// 丢弃缓存不崩。
	Generator->InvalidateHydrology();
	TestNull(TEXT("丢弃缓存后仍然没有水"), Generator->GetHydrology().Get());

	return true;
}

// 「有水」的一侧：缓存键必须真的被比较。
//
// 这条用例是 Plumbing 的补集。Plumbing 用空模块，RefreshHydrology 在
// GetWaterConfig 的早退处就返回了，缓存键一个都没读过 —— 那里的所有断言在
// 空缓存上都恒真。这里换一个真会返回 true 的地形替身，字段才被真正读写。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraHydrologyCacheTest,
	"StellarEra.Planet.Hydrology.Cache",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraHydrologyCacheTest::RunTest(const FString& Parameters)
{
	UPlanetHydrologyTestTerrain* Terrain = NewObject<UPlanetHydrologyTestTerrain>();
	if (!TestNotNull(TEXT("地形替身必须能实例化"), Terrain))
	{
		return false;
	}
	// 16 格/面：烘焙仍是完整流水线，但只有 6×18² ≈ 1944 格，毫秒级。
	Terrain->Resolution = 16;
	Terrain->bEnableWater = true;

	UVoxelPlanetGenerator* Generator = NewObject<UVoxelPlanetGenerator>();
	if (!TestNotNull(TEXT("生成器必须能实例化"), Generator))
	{
		return false;
	}
	Generator->Terrain = Terrain;
	Generator->Radius = 500.f;

	// --- 第一次：必须烘出有效场 ---
	Generator->RefreshHydrology();
	const TSharedPtr<const FPlanetHydrologyField> First = Generator->GetHydrology();
	if (!TestTrue(TEXT("有水的模块必须烘出水文场"), First.IsValid()))
	{
		return false;
	}
	TestTrue(TEXT("烘出来的场必须是有效场"), First->IsValid());
	TestEqual(TEXT("场的分辨率必须等于配置"), First->GetResolution(), 16);

	// --- 同模块同半径再刷：必须命中缓存，返回同一个对象 ---
	Generator->RefreshHydrology();
	TestTrue(TEXT("参数没变时必须复用缓存（同一个 TSharedPtr 对象）"),
		Generator->GetHydrology().Get() == First.Get());

	// --- 失效后：必须重新烘焙出**不同的**对象 ---
	Generator->InvalidateHydrology();
	TestNull(TEXT("失效后缓存立刻为空"), Generator->GetHydrology().Get());

	Generator->RefreshHydrology();
	const TSharedPtr<const FPlanetHydrologyField> Second = Generator->GetHydrology();
	if (!TestTrue(TEXT("失效后必须能重新烘出"), Second.IsValid()))
	{
		return false;
	}
	TestTrue(TEXT("失效后必须是新对象（不是旧的缓存）"), Second.Get() != First.Get());

	// --- 同一模块改一个配置字段：缓存键变化，下一次必须重烘 ---
	Terrain->SeaLevel = 0.1f;
	Generator->RefreshHydrology();
	const TSharedPtr<const FPlanetHydrologyField> Third = Generator->GetHydrology();
	if (!TestTrue(TEXT("改配置后必须仍能烘出"), Third.IsValid()))
	{
		return false;
	}
	TestTrue(TEXT("水体配置变化必须让缓存失效"), Third.Get() != Second.Get());

	return true;
}

#endif
