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
#include "VoxelPlanetGenerator.h"
#include "VoxelPlanetNoiseTerrain.h"
#include "VoxelGenerators/VoxelGeneratorInstance.h"

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

	// 极端数值必须被夹取并留下警告，而不是把地表推出世界。
	{
		FPlanetGeologyLoadReport ClampReport;
		const TSharedPtr<const FPlanetGeologyField> ClampedField =
			FPlanetGeologyField::LoadFromString(ElementsJson, TEXT(R"JSON(
{
  "version": 1,
  "geometryAmplitude": 1e6,
  "elements": {
    "iron": { "abundance": 1.0, "peakDepth": 0, "layerThickness": 10 }
  }
}
)JSON"), ClampReport);

		TestTrue(TEXT("极端 geometryAmplitude 仍产出有效场"),
			ClampedField.IsValid() && !ClampedField->IsEmpty());
		TestTrue(TEXT("geometryAmplitude 夹到 <= 64"),
			ClampedField.IsValid() && ClampedField->GetMaxGeometryDelta() <= 64.f);

		bool bSawAmplitudeWarning = false;
		for (const FString& Warning : ClampReport.Warnings)
		{
			bSawAmplitudeWarning |= Warning.Contains(TEXT("geometryAmplitude"));
		}
		TestTrue(TEXT("geometryAmplitude 夹取有警告"), bSawAmplitudeWarning);

		// peakDepth / layerThickness 的极端值同样要夹到有限上界，
		// 否则 MaxReachDepth 会变成 inf，高斯剪枝早退失效。
		FPlanetGeologyLoadReport DepthReport;
		const TSharedPtr<const FPlanetGeologyField> DepthField =
			FPlanetGeologyField::LoadFromString(ElementsJson, TEXT(R"JSON(
{
  "version": 1,
  "elements": {
    "iron": { "abundance": 1.0, "peakDepth": 1e9, "layerThickness": 1e9 }
  }
}
)JSON"), DepthReport);
		TestTrue(TEXT("极端 peakDepth/layerThickness 仍产出有效场"),
			DepthField.IsValid() && !DepthField->IsEmpty());
		TestTrue(TEXT("最大触达深度是有限值且被夹到 40000"),
			DepthField.IsValid()
				&& FMath::IsFinite(DepthField->GetMaxReachDepth())
				&& DepthField->GetMaxReachDepth() <= 40000.f);
	}

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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlanetGeologyColorTest,
	"StellarEra.Planet.Geology.Color",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetGeologyColorTest::RunTest(const FString& Parameters)
{
	const TCHAR* Elements = TEXT(R"JSON(
{
  "version": 1,
  "elements": [
    { "id": "iron", "color": [0.8, 0.2, 0.2],
      "noise": { "frequency": 3.0, "octaves": 1, "lacunarity": 2.0, "gain": 0.5, "seed": 101, "depthScale": 0.3 } }
  ]
}
)JSON");

	// 阈值 0、过渡极窄、纯度门槛极低 → 只要有一点含量就完全用元素色。
	const TCHAR* Planet = TEXT(R"JSON(
{
  "version": 1,
  "geometryAmplitude": 0.0,
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

	FVoxelPlanetTerrainContext NoGeoContext;
	NoGeoContext.Radius = 100.f;

	FVoxelPlanetTerrainContext EmptyGeoContext;
	EmptyGeoContext.Radius = 100.f;
	EmptyGeoContext.Geology = MakeShared<FPlanetGeologyField>();

	FPlanetNoiseParams Params;
	Params.Amplitude = 8.f;
	Params.SeaLevel = -0.35f;

	const FVoxelPlanetNoiseTerrainInstance WithGeo(Context, Params);
	const FVoxelPlanetNoiseTerrainInstance WithoutGeo(NoGeoContext, Params);
	const FVoxelPlanetNoiseTerrainInstance WithEmptyGeo(EmptyGeoContext, Params);
	const FVoxelItemStack& Items = FVoxelItemStack::Empty;

	FRandomStream Stream(777);
	FVector LandDirection = FVector::UpVector;
	FVector WaterDirection = FVector::DownVector;
	bool bFoundLand = false;
	bool bFoundWater = false;

	for (int32 Index = 0; Index < 4096 && !(bFoundLand && bFoundWater); ++Index)
	{
		const FVector Direction = Stream.GetUnitVector();
		const double Offset = double(ComputePlanetOffset(Params, Direction));
		// 陆地点还要求矿物含量足够高：否则噪声恰好接近 0 时 TotalNorm 会低于
		// fadeRange，颜色只混了一部分，断言就变成概率性的。
		if (!bFoundLand
			&& Offset > double(Params.SeaLevel) + 0.1
			&& Field->Sample(Direction, 0.0).TotalNorm > 0.5)
		{
			LandDirection = Direction;
			bFoundLand = true;
		}
		if (!bFoundWater && Offset <= double(Params.SeaLevel) + UE_KINDA_SMALL_NUMBER)
		{
			WaterDirection = Direction;
			bFoundWater = true;
		}
	}

	TestTrue(TEXT("找到陆地采样点"), bFoundLand);
	TestTrue(TEXT("找到水面采样点"), bFoundWater);

	// 陆地：depth = 0 落在峰值层，且方向高于海平面 → 完全使用元素色。
	{
		const FVector P = LandDirection * 100.0;
		const FColor Color = WithGeo.GetMaterialImpl(
			v_flt(P.X), v_flt(P.Y), v_flt(P.Z), 0, Items).GetColor();

		const auto Near = [](uint8 Actual, float Expected)
		{
			return FMath::Abs(int32(Actual) - FMath::RoundToInt(Expected * 255.f)) <= 2;
		};
		TestTrue(TEXT("纯矿地表 R 接近元素色"), Near(Color.R, 0.8f));
		TestTrue(TEXT("纯矿地表 G 接近元素色"), Near(Color.G, 0.2f));
		TestTrue(TEXT("纯矿地表 B 接近元素色"), Near(Color.B, 0.2f));
	}

	// 空场不能改变任何像素的颜色。
	{
		const FVector P = LandDirection * 100.0;
		const FColor Baseline = WithoutGeo.GetMaterialImpl(
			v_flt(P.X), v_flt(P.Y), v_flt(P.Z), 0, Items).GetColor();
		const FColor EmptyGeo = WithEmptyGeo.GetMaterialImpl(
			v_flt(P.X), v_flt(P.Y), v_flt(P.Z), 0, Items).GetColor();
		TestEqual(TEXT("空场不改变地形底色"), EmptyGeo, Baseline);
	}

	// 水面不叠矿色：与没有地质的实例逐通道一致。
	{
		const FVector P = WaterDirection * 100.0;
		const FColor Baseline = WithoutGeo.GetMaterialImpl(
			v_flt(P.X), v_flt(P.Y), v_flt(P.Z), 0, Items).GetColor();
		const FColor WithGeology = WithGeo.GetMaterialImpl(
			v_flt(P.X), v_flt(P.Y), v_flt(P.Z), 0, Items).GetColor();
		TestEqual(TEXT("水面不叠矿色"), WithGeology, Baseline);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlanetGeologyDefaultContentTest,
	"StellarEra.Planet.Geology.DefaultContent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetGeologyDefaultContentTest::RunTest(const FString& Parameters)
{
	FPlanetGeologyLoadReport Report;
	const TSharedPtr<const FPlanetGeologyField> Field =
		FPlanetGeologyField::LoadFromFiles(TEXT("earth"), Report);

	for (const FString& Error : Report.Errors)
	{
		AddError(Error);
	}
	for (const FString& Warning : Report.Warnings)
	{
		AddWarning(Warning);
	}

	TestTrue(TEXT("earth 预设加载成功"), Field.IsValid() && !Field->IsEmpty());
	TestEqual(TEXT("默认 6 种元素"), Field->NumElements(), 6);
	TestTrue(TEXT("预设列表包含 earth"),
		FPlanetGeologyField::ListPresetNames().Contains(TEXT("earth")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlanetGeologyPipelineTest,
	"StellarEra.Planet.Geology.Pipeline",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetGeologyPipelineTest::RunTest(const FString& Parameters)
{
	// 1) 真正走生成器的「预设名 → 懒加载缓存 → 地形上下文」链路。
	// 前面的测试都手工构造 Context，漏掉 Context.Geology 时它们仍然会绿。
	UVoxelPlanetGenerator* Generator = NewObject<UVoxelPlanetGenerator>();
	Generator->Radius = 1500.f;
	Generator->GeologyPreset = TEXT("earth");

	const FVoxelPlanetTerrainContext Context = Generator->MakeTerrainContext();
	TestTrue(TEXT("MakeTerrainContext 带上非空地质场"),
		Context.Geology.IsValid() && !Context.Geology->IsEmpty());
	TestTrue(TEXT("earth 预设启用了几何外凸"),
		Context.Geology.IsValid() && Context.Geology->GetMaxGeometryDelta() > 0.f);

	// 缓存失效后必须真重新读盘，而不是永久返回空场。
	Generator->InvalidateGeology();
	const TSharedPtr<const FPlanetGeologyField> Reloaded = Generator->GetGeology();
	TestTrue(TEXT("InvalidateGeology 后重新加载仍是非空场"),
		Reloaded.IsValid() && !Reloaded->IsEmpty());
	if (!Reloaded.IsValid())
	{
		return false;
	}

	// 2) 深度符号 pin：earth 的 iron 峰在 peakDepth=+30、层厚 20。
	// 向内（depth=+30）是峰顶；基准球面以上（depth=-30）是 exp(-9)≈1e-4 的尾部，
	// 即使考虑深处 titanium/uranium 的尾巴，总和也差两个数量级。
	// 用 32 个 Fibonacci 方向取总和，避免单个噪声低谷让断言变成概率性的。
	double SumPos = 0.0;
	double SumNeg = 0.0;
	float MaxPos = 0.f;
	constexpr int32 NumDirections = 32;
	for (int32 Index = 0; Index < NumDirections; ++Index)
	{
		const double Z = 1.0 - 2.0 * (double(Index) + 0.5) / double(NumDirections);
		const double RadiusXY = FMath::Sqrt(FMath::Max(0.0, 1.0 - Z * Z));
		const double Angle =
			UE_DOUBLE_TWO_PI * double(Index) * 0.6180339887498949;
		const FVector Direction(
			RadiusXY * FMath::Cos(Angle), RadiusXY * FMath::Sin(Angle), Z);

		const float Pos = Reloaded->Sample(Direction, 30.0).TotalNorm;
		const float Neg = Reloaded->Sample(Direction, -30.0).TotalNorm;
		SumPos += Pos;
		SumNeg += Neg;
		MaxPos = FMath::Max(MaxPos, Pos);
	}

	TestTrue(*FString::Printf(
		TEXT("向内 +30 的总含量远高于向外 -30（%.3f vs %.3f）"), SumPos, SumNeg),
		SumPos > 5.0 * SumNeg && SumPos > 0.0);
	TestTrue(*FString::Printf(
		TEXT("至少一个方向在 +30 有明显矿层（max=%.3f）"), MaxPos),
		MaxPos > 0.2f);

	// 3) 生成出的实例按行星半径分区：球内密度为负、球外为正。
	// 默认没有挂地形模块，走的是纯球代理；这里验证的是 GetInstance() 这条
	// 生产路径确实拿到了 MakeTerrainContext() 的 Radius，而不是手工构造的实例。
	const TVoxelSharedRef<FVoxelGeneratorInstance> Instance = Generator->GetInstance();
	const v_flt InsideValue = Instance->GetValue(
		v_flt(Generator->Radius - 20.f), 0.f, 0.f, 0, FVoxelItemStack::Empty);
	const v_flt OutsideValue = Instance->GetValue(
		v_flt(Generator->Radius + 20.f), 0.f, 0.f, 0, FVoxelItemStack::Empty);
	TestTrue(TEXT("半径以内密度为负"), InsideValue < 0.f);
	TestTrue(TEXT("半径以外密度为正"), OutsideValue > 0.f);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
