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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlanetGeologyDeterminismTest,
	"StellarEra.Planet.Geology.Determinism",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetGeologyDeterminismTest::RunTest(const FString& Parameters)
{
	using namespace PlanetGeologyTest;

	const TCHAR* ElementsA = TEXT(R"JSON(
{
  "version": 1,
  "elements": [
    { "id": "iron", "color": [0.55, 0.28, 0.16],
      "noise": { "frequency": 3.0, "octaves": 2, "lacunarity": 2.0, "gain": 0.5, "seed": 101, "depthScale": 0.3 } }
  ]
}
)JSON");

	const TCHAR* ElementsB = TEXT(R"JSON(
{
  "version": 1,
  "elements": [
    { "id": "iron", "color": [0.55, 0.28, 0.16],
      "noise": { "frequency": 3.0, "octaves": 2, "lacunarity": 2.0, "gain": 0.5, "seed": 999, "depthScale": 0.3 } }
  ]
}
)JSON");

	const TCHAR* Planet = TEXT(R"JSON(
{
  "version": 1,
  "geometryAmplitude": 1.5,
  "elements": {
    "iron": { "abundance": 0.8, "peakDepth": -30, "layerThickness": 20 }
  }
}
)JSON");

	FPlanetGeologyLoadReport ReportA;
	FPlanetGeologyLoadReport ReportB;
	const TSharedPtr<const FPlanetGeologyField> FieldA =
		FPlanetGeologyField::LoadFromString(ElementsA, Planet, ReportA);
	const TSharedPtr<const FPlanetGeologyField> FieldB =
		FPlanetGeologyField::LoadFromString(ElementsB, Planet, ReportB);

	const FVector Direction = FVector(0.3, -0.5, 0.8).GetSafeNormal();
	constexpr double Depth = -30.0;

	const FPlanetGeologySample First = FieldA->Sample(Direction, Depth);
	const FPlanetGeologySample Second = FieldA->Sample(Direction, Depth);
	TestEqual(TEXT("同坐标两次采样 TotalNorm 完全一致"), First.TotalNorm, Second.TotalNorm);
	TestEqual(TEXT("同坐标两次采样 Purity 完全一致"), First.Purity, Second.Purity);
	TestEqual(TEXT("同坐标两次采样颜色完全一致"), First.MineralColor, Second.MineralColor);

	const FPlanetGeologySample OtherSeed = FieldB->Sample(Direction, Depth);
	TestTrue(TEXT("不同 noise.seed 得到不同布局"), First.TotalNorm != OtherSeed.TotalNorm);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlanetGeologyBoundsTest,
	"StellarEra.Planet.Geology.Bounds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetGeologyBoundsTest::RunTest(const FString& Parameters)
{
	using namespace PlanetGeologyTest;

	FPlanetGeologyLoadReport Report;
	const TSharedPtr<const FPlanetGeologyField> Field =
		FPlanetGeologyField::LoadFromString(ElementsJson, PlanetJson, Report);
	if (!Field.IsValid())
	{
		return false;
	}

	const float MaxDelta = Field->GetMaxGeometryDelta();
	FRandomStream Stream(20261024);

	bool bSampleBounds = true;
	bool bDeltaBounds = true;
	for (int32 Index = 0; Index < 2048; ++Index)
	{
		const FVector Direction = Stream.GetUnitVector();
		const double Depth = Stream.FRandRange(-400.0, 80.0);

		const FPlanetGeologySample Sample = Field->Sample(Direction, Depth);
		bSampleBounds &= Sample.TotalNorm >= 0.f && Sample.TotalNorm <= 1.f;
		bSampleBounds &= Sample.Purity >= 0.f && Sample.Purity <= 1.f;

		const float Delta = Field->SampleGeometryDelta(Direction, Depth);
		bDeltaBounds &= Delta >= 0.f && Delta <= MaxDelta;
	}

	TestTrue(TEXT("TotalNorm 与 Purity 都在 [0,1]"), bSampleBounds);
	TestTrue(TEXT("GeometryDelta 在 [0, geometryAmplitude]"), bDeltaBounds);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlanetGeologyRangeCoverageTest,
	"StellarEra.Planet.Geology.RangeCoverage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetGeologyRangeCoverageTest::RunTest(const FString& Parameters)
{
	// 故意用大几何振幅，让「区间漏掉 Geology 项」立刻暴露成真失败。
	const TCHAR* Elements = TEXT(R"JSON(
{
  "version": 1,
  "elements": [
    { "id": "iron", "color": [0.55, 0.28, 0.16],
      "noise": { "frequency": 0.5, "octaves": 1, "lacunarity": 2.0, "gain": 0.5, "seed": 101, "depthScale": 0.3 } }
  ]
}
)JSON");

	const TCHAR* Planet = TEXT(R"JSON(
{
  "version": 1,
  "geometryAmplitude": 12.0,
  "appearance": { "colorThreshold": 0.0, "fadeRange": 0.001, "purityForFullColor": 0.01 },
  "elements": {
    "iron": { "abundance": 1.0, "peakDepth": 0, "layerThickness": 200 }
  }
}
)JSON");

	FPlanetGeologyLoadReport Report;
	const TSharedPtr<const FPlanetGeologyField> Field =
		FPlanetGeologyField::LoadFromString(Elements, Planet, Report);

	FVoxelPlanetTerrainContext Context;
	Context.Radius = 100.f;
	Context.Geology = Field;

	// Amplitude 设为 0：把「基础起伏振幅」带来的区间余量清零，让密度下界只剩
	// 地质增量一个未知量。否则基础振幅 8 体素的保守余量（最多 8/5=1.6 的密度
	// 余量）会盖住矿脉增量，区间测试即使漏项也可能碰巧通过。
	FPlanetNoiseParams Params;
	Params.Amplitude = 0.f;
	Params.SeaLevel = -0.35f;
	const FVoxelPlanetNoiseTerrainInstance Instance(Context, Params);
	const FVoxelItemStack& Items = FVoxelItemStack::Empty;

	FRandomStream Stream(424242);
	bool bRangeCovers = true;
	FString FailureDetail;

	// 仿照网格化器：只检查贴着地表的 32³ 区块，而不是包住球心的整块包围盒。
	for (int32 BoxIndex = 0; BoxIndex < 48 && bRangeCovers; ++BoxIndex)
	{
		const FVector Direction = Stream.GetUnitVector();
		const FVector Center =
			Direction * (100.0 + Stream.FRandRange(-8.0, 8.0));
		const FIntVector BoxMin(
			FMath::FloorToInt(Center.X) - 16,
			FMath::FloorToInt(Center.Y) - 16,
			FMath::FloorToInt(Center.Z) - 16);
		const FIntVector BoxMax(BoxMin.X + 31, BoxMin.Y + 31, BoxMin.Z + 31);
		const FVoxelIntBox Bounds(BoxMin, BoxMax);
		const TVoxelRange<v_flt> Range = Instance.GetValueRangeImpl(Bounds, 0, Items);

		for (int32 SampleIndex = 0; SampleIndex < 256; ++SampleIndex)
		{
			const FVector P(
				Stream.FRandRange(double(BoxMin.X), double(BoxMax.X + 1)),
				Stream.FRandRange(double(BoxMin.Y), double(BoxMax.Y + 1)),
				Stream.FRandRange(double(BoxMin.Z), double(BoxMax.Z + 1)));

			const v_flt Value = Instance.GetValueImpl(
				v_flt(P.X), v_flt(P.Y), v_flt(P.Z), 0, Items);

			if (Value < Range.Min || Value > Range.Max)
			{
				bRangeCovers = false;
				FailureDetail = FString::Printf(
					TEXT("盒 %d: 值 %.4f 落在区间 [%.4f, %.4f] 之外"),
					BoxIndex, double(Value), double(Range.Min), double(Range.Max));
				break;
			}
		}
	}

	TestTrue(*FString::Printf(TEXT("GetValueRangeImpl 覆盖所有采样值 %s"), *FailureDetail), bRangeCovers);

	// 地表查询必须与密度场落在同一个面上：否则角色出生会陷进矿脉凸起里。
	UVoxelPlanetNoiseTerrain* Terrain = NewObject<UVoxelPlanetNoiseTerrain>();
	const FPlanetNoiseParams TerrainParams = Terrain->MakeParams();
	const FVoxelPlanetNoiseTerrainInstance SurfaceInstance(Context, TerrainParams);

	bool bSurfaceConsistent = true;
	for (int32 Index = 0; Index < 32; ++Index)
	{
		const FVector Direction = Stream.GetUnitVector();
		const float SurfaceRadius = Terrain->GetSurfaceRadiusVoxels(Context, Direction);
		const FVector P = Direction * double(SurfaceRadius);
		const double Value = double(SurfaceInstance.GetValueImpl(
			v_flt(P.X), v_flt(P.Y), v_flt(P.Z), 0, Items));

		// 密度 = (距离 - 地表)/5；允许 float 半径带来的一点误差。
		if (FMath::Abs(Value) > 0.1)
		{
			bSurfaceConsistent = false;
			break;
		}
	}
	TestTrue(TEXT("地表查询处的密度约为 0"), bSurfaceConsistent);

	return bRangeCovers && bSurfaceConsistent;
}

#endif // WITH_DEV_AUTOMATION_TESTS
