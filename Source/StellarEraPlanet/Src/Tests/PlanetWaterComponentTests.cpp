// 水面组件的接线测试。
//
// 只钉**不需要世界**的那一侧：没有水文场、或者没有世界时，组件不能凭空造出
// 未注册的分量（那会泄漏在关卡里，且随重建越积越多）。
// 「有世界时真的画出水面」由 Task 9 的 PIE 验收负责。

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "PlanetHydrologyField.h"
#include "PlanetWaterComponent.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraWaterComponentTest,
	"StellarEra.Planet.Water.Component",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraWaterComponentTest::RunTest(const FString& Parameters)
{
	UPlanetWaterComponent* Component = NewObject<UPlanetWaterComponent>();
	TestNotNull(TEXT("组件必须能实例化"), Component);
	if (!Component)
	{
		return false;
	}

	// --- 没有水文场：什么都不该有 ---
	Component->RebuildWater(/*Field=*/nullptr, FPlanetWaterConfig(), 100.0, 1000.0);
	TestEqual(TEXT("水文场为空时不能有水面分量"), Component->GetWaterMeshCount(), 0);

	// --- 有水文场但没有世界：仍然什么都不该有 ---
	// 未注册的组件会留在关卡里、随每次重建累积 —— 这比「水面暂时不显示」糟得多。
	// 三个栅格都必须按 CellCount 填满：Create 不做补齐，尺寸不对就是无效场。
	const int32 Resolution = 8;
	const int32 CellCount = 6 * (Resolution + 2) * (Resolution + 2);
	TArray<float> WaterLevel;
	TArray<float> CarveDepth;
	TArray<float> RiverStrength;
	WaterLevel.Init(900.f, CellCount);
	CarveDepth.Init(0.f, CellCount);
	RiverStrength.Init(0.f, CellCount);
	const TSharedPtr<FPlanetHydrologyField> Field = FPlanetHydrologyField::Create(
		Resolution, 900.f, 4.f,
		MoveTemp(WaterLevel), MoveTemp(CarveDepth), MoveTemp(RiverStrength),
		TArray<FPlanetLakeBasin>(), TArray<FPlanetRiverPolyline>());

	if (TestTrue(TEXT("手工构造的水文场必须有效"), Field.IsValid() && Field->IsValid()))
	{
		Component->RebuildWater(Field.Get(), FPlanetWaterConfig(), 100.0, 1000.0);
		TestEqual(TEXT("没有世界时不能创建分量"), Component->GetWaterMeshCount(), 0);
	}

	return true;
}

#endif
