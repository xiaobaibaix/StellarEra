// 地质场的自动化测试：解析、确定性、边界、区间覆盖、材质颜色。

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Math/RandomStream.h"
// VoxelItemStack.h 不是自包含的：它用到 FVoxelIntBox 却没 include 对应头，
// 直接单独包含会在 VoxelItemStack.h(38) 报「缺少类型说明符」。
#include "VoxelIntBox.h"
#include "VoxelItemStack.h"
#include "VoxelMaterial.h"
#include "PlanetGeologyField.h"
#include "VoxelPlanetNoiseTerrain.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace PlanetGeologyTest
{
	// 两个已知元素 + 一条未知元素 + 一条非法层厚。
	const TCHAR* ElementsJson = TEXT(R"JSON(
{
  "version": 1,
  "elements": [
    {
      "id": "iron",
      "displayName": "铁",
      "color": [0.55, 0.28, 0.16],
      "noise": { "frequency": 3.0, "octaves": 2, "lacunarity": 2.0, "gain": 0.5, "seed": 101, "depthScale": 0.3 }
    },
    {
      "id": "copper",
      "displayName": "铜",
      "color": [0.9, 0.4, 0.1],
      "noise": { "frequency": 4.0, "octaves": 2, "lacunarity": 2.0, "gain": 0.5, "seed": 202, "depthScale": 0.3 }
    }
  ]
}
)JSON");

	const TCHAR* PlanetJson = TEXT(R"JSON(
{
  "version": 1,
  "geometryAmplitude": 1.5,
  "appearance": { "colorThreshold": 0.22, "fadeRange": 0.15, "purityForFullColor": 0.7 },
  "elements": {
    "iron":   { "abundance": 0.75, "peakDepth": -30, "layerThickness": 20 },
    "copper": { "abundance": 0.45, "peakDepth": -65, "layerThickness": 42 },
    "gold":   { "abundance": 0.9,  "peakDepth": -10, "layerThickness": 10 },
    "zinc":   { "abundance": 0.5,  "peakDepth": -10, "layerThickness": 0 }
  }
}
)JSON");
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlanetGeologyParseTest,
	"StellarEra.Planet.Geology.Parse",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetGeologyParseTest::RunTest(const FString& Parameters)
{
	using namespace PlanetGeologyTest;

	FPlanetGeologyLoadReport Report;
	const TSharedPtr<const FPlanetGeologyField> Field =
		FPlanetGeologyField::LoadFromString(ElementsJson, PlanetJson, Report);

	TestNotNull(TEXT("加载返回非空"), Field.Get());
	if (!Field.IsValid())
	{
		return false;
	}

	TestFalse(TEXT("合法输入不应产生 error"), Report.HasErrors());
	TestEqual(TEXT("只接受两个已知元素"), Field->NumElements(), 2);
	// 用容差比较而不是 TestEqual：0.75f + 0.45f 在二进制里不是精确的 1.2f。
	TestTrue(TEXT("丰度和 = 0.75 + 0.45"),
		FMath::IsNearlyEqual(Field->GetTotalAbundance(), 1.2f, 1e-4f));
	TestTrue(TEXT("最大触达深度 = max(30+60, 65+126)"),
		FMath::IsNearlyEqual(Field->GetMaxReachDepth(), 191.f, 1e-4f));
	TestTrue(TEXT("几何振幅"),
		FMath::IsNearlyEqual(Field->GetMaxGeometryDelta(), 1.5f, 1e-4f));

	// 未知 id 与非法层厚都必须留下可定位的警告，而不是静默消失。
	bool bSawUnknown = false;
	bool bSawBadThickness = false;
	for (const FString& Warning : Report.Warnings)
	{
		bSawUnknown |= Warning.Contains(TEXT("gold"));
		bSawBadThickness |= Warning.Contains(TEXT("zinc"));
	}
	TestTrue(TEXT("未知元素有警告"), bSawUnknown);
	TestTrue(TEXT("layerThickness <= 0 有警告"), bSawBadThickness);

	// 空元素表是合法的「没有矿」。
	FPlanetGeologyLoadReport EmptyReport;
	const TSharedPtr<const FPlanetGeologyField> EmptyField = FPlanetGeologyField::LoadFromString(
		TEXT(R"JSON({"version":1,"elements":[]})JSON"), PlanetJson, EmptyReport);
	TestFalse(TEXT("空元素表不是错误"), EmptyReport.HasErrors());
	TestTrue(TEXT("空元素表产出空场"), EmptyField.IsValid() && EmptyField->IsEmpty());

	// 语法错误退化成空场，而不是崩溃或让行星消失。
	FPlanetGeologyLoadReport BadReport;
	const TSharedPtr<const FPlanetGeologyField> BadField =
		FPlanetGeologyField::LoadFromString(TEXT("{"), PlanetJson, BadReport);
	TestTrue(TEXT("语法错误有 error"), BadReport.HasErrors());
	TestTrue(TEXT("坏数据退化为空场"), BadField.IsValid() && BadField->IsEmpty());

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
