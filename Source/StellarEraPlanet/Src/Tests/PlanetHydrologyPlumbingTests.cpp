// 水文接线的回归测试：没有水的行星不该烘焙，也不该凭空长出水面。
//
// 这一侧只能钉「空」的一侧 —— 「有水的一侧」需要地形模块真正重写
// GetWaterConfig，那在 Task 5 里由 PlanetHydrologyPlumbingTests 的后续用例覆盖。

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
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

#endif
