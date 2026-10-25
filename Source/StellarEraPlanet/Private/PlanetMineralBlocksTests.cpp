// 矿物块采样的自动化测试：球内约束、元素归属、运行期顺序、确定性、阈值、空场。

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Math/RandomStream.h"
#include "PlanetGeologyField.h"
#include "PlanetMineralBlocks.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace PlanetMineralBlocksTest
{
	// 三个元素：a/b/c。半径 1000 体素，矿带峰值 150 / 450 / 750，厚度 50。
	// 3σ = 150 体素，三条带分别覆盖 0..300 / 300..600 / 600..900 —— 互不重叠。
	const TCHAR* ElementsJson = TEXT(R"JSON(
{
  "version": 1,
  "elements": [
    {
      "id": "a",
      "displayName": "A",
      "color": [1.0, 0.0, 0.0],
      "noise": { "frequency": 3.0, "octaves": 2, "lacunarity": 2.0, "gain": 0.5, "seed": 11, "depthScale": 0.3 }
    },
    {
      "id": "b",
      "displayName": "B",
      "color": [0.0, 1.0, 0.0],
      "noise": { "frequency": 3.0, "octaves": 2, "lacunarity": 2.0, "gain": 0.5, "seed": 22, "depthScale": 0.3 }
    },
    {
      "id": "c",
      "displayName": "C",
      "color": [0.0, 0.0, 1.0],
      "noise": { "frequency": 3.0, "octaves": 2, "lacunarity": 2.0, "gain": 0.5, "seed": 33, "depthScale": 0.3 }
    }
  ]
}
)JSON");

	/**
	 * 行星 JSON。故意按 c / a / b 写，让「写下来的顺序」与 Elements.json 的
	 * a / b / c 不同 —— 运行期顺序由哈希决定，但至少不是数组顺序的简单复制。
	 */
	FString MakePlanetJson(float ColorThreshold)
	{
		return FString::Printf(TEXT(R"JSON(
{
  "version": 1,
  "geometryAmplitude": 1.5,
  "appearance": { "colorThreshold": %f, "fadeRange": 0.15, "purityForFullColor": 0.7 },
  "elements": {
    "c": { "abundance": 1.0, "peakDepth": 750, "layerThickness": 50 },
    "a": { "abundance": 1.0, "peakDepth": 150, "layerThickness": 50 },
    "b": { "abundance": 1.0, "peakDepth": 450, "layerThickness": 50 }
  }
}
)JSON"), ColorThreshold);
	}

	const TCHAR* EmptyPlanetJson = TEXT(R"JSON(
{
  "version": 1,
  "appearance": { "colorThreshold": 0.22, "fadeRange": 0.15, "purityForFullColor": 0.7 },
  "elements": {}
}
)JSON");

	TSharedPtr<const FPlanetGeologyField> MakeField(float ColorThreshold = 0.22f)
	{
		FPlanetGeologyLoadReport Report;
		TSharedPtr<const FPlanetGeologyField> Field =
			FPlanetGeologyField::LoadFromString(ElementsJson, MakePlanetJson(ColorThreshold),
				Report, /*PlanetRadiusVoxels=*/1000.0);
		check(Field.IsValid());
		return Field;
	}

	PlanetMineralBlocks::FBuildParams MakeParams()
	{
		PlanetMineralBlocks::FBuildParams Params;
		Params.SurfaceRadiusCm = 100000.f; // 1000 体素 × 100 cm
		Params.VoxelSizeCm = 100.f;
		Params.SpacingRatio = 1.f / 16.f;
		Params.Seed = 2026;
		return Params;
	}

	int32 CountBlocks(const PlanetMineralBlocks::FBuildResult& Result)
	{
		int32 Total = 0;
		for (const TArray<FPlanetMineralBlock>& Group : Result.BlocksByElement)
		{
			Total += Group.Num();
		}
		return Total;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlanetMineralBlocksInsideSphereTest,
	"StellarEra.Planet.MineralBlocks.InsideSphere",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetMineralBlocksInsideSphereTest::RunTest(const FString& Parameters)
{
	const TSharedPtr<const FPlanetGeologyField> Field = PlanetMineralBlocksTest::MakeField();
	TestFalse(TEXT("测试地质场非空"), Field->IsEmpty());

	PlanetMineralBlocks::FBuildResult Result;
	PlanetMineralBlocks::Build(*Field, PlanetMineralBlocksTest::MakeParams(), Result);

	TestTrue(TEXT("有采样点"), Result.SampleCount > 0);
	TestEqual(TEXT("每个元素一组"), Result.BlocksByElement.Num(), Field->NumElements());
	TestTrue(TEXT("生成了矿物块"), PlanetMineralBlocksTest::CountBlocks(Result) > 0);

	const double RadiusCm = 100000.0;
	for (const TArray<FPlanetMineralBlock>& Group : Result.BlocksByElement)
	{
		for (const FPlanetMineralBlock& Block : Group)
		{
			// 抖动最多把格点推出半格，但「在球内」的判定在抖动之后才做，所以恒成立。
			TestTrue(TEXT("方块在球内"), Block.LocationCm.Size() <= RadiusCm + 1.0);
			TestTrue(TEXT("不在球心退化区"), Block.LocationCm.Size() >= 0.05 * RadiusCm - 1.0);
		}
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlanetMineralBlocksElementMappingTest,
	"StellarEra.Planet.MineralBlocks.ElementMapping",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetMineralBlocksElementMappingTest::RunTest(const FString& Parameters)
{
	const TSharedPtr<const FPlanetGeologyField> Field = PlanetMineralBlocksTest::MakeField();

	PlanetMineralBlocks::FBuildResult Result;
	PlanetMineralBlocks::Build(*Field, PlanetMineralBlocksTest::MakeParams(), Result);

	const FPlanetGeologyAppearance& Appearance = Field->GetAppearance();
	const double RadiusCm = 100000.0;
	const double VoxelSizeCm = 100.0;

	int32 Checked = 0;
	for (int32 GroupIndex = 0; GroupIndex < Result.BlocksByElement.Num(); ++GroupIndex)
	{
		const TArray<FPlanetMineralBlock>& Group = Result.BlocksByElement[GroupIndex];
		// 每组最多查 64 个：完整核对要几万次 Perlin，测试没必要那么慢。
		const int32 Limit = FMath::Min(64, Group.Num());
		for (int32 Index = 0; Index < Limit; ++Index)
		{
			const FPlanetMineralBlock& Block = Group[Index];
			TestEqual(TEXT("组下标就是元素下标"), Block.ElementIndex, GroupIndex);

			const double Distance = Block.LocationCm.Size();
			const FVector UnitDir = Block.LocationCm / Distance;
			const double Depth = (RadiusCm - Distance) / VoxelSizeCm;
			const FPlanetGeologySample Sample = Field->Sample(UnitDir, Depth);

			TestEqual(TEXT("归属与重新采样一致"), Sample.DominantIndex, GroupIndex);
			TestTrue(TEXT("通过颜色的阈值"),
				Sample.DominantNorm > Appearance.ColorThreshold);
			++Checked;
		}
	}

	TestTrue(TEXT("至少核对了一个方块"), Checked > 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlanetMineralBlocksSampledDefinitionsTest,
	"StellarEra.Planet.MineralBlocks.SampledDefinitions",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetMineralBlocksSampledDefinitionsTest::RunTest(const FString& Parameters)
{
	const TSharedPtr<const FPlanetGeologyField> Field = PlanetMineralBlocksTest::MakeField();

	const TArray<FPlanetGeologyElement> Sampled = Field->GetSampledElementDefinitions();
	const TArray<FPlanetGeologyElement> Ordered = Field->GetElementDefinitions();

	TestEqual(TEXT("采样顺序数量 = 运行期元素数"), Sampled.Num(), Field->NumElements());
	TestEqual(TEXT("数组顺序数量 = 运行期元素数"), Ordered.Num(), Field->NumElements());

	// 运行期顺序里每个 id 都必须在数组顺序里出现。
	for (const FPlanetGeologyElement& Element : Sampled)
	{
		TestTrue(FString::Printf(TEXT("'%s' 在元素表里"), *Element.Id),
			Ordered.ContainsByPredicate([&Element](const FPlanetGeologyElement& Candidate)
			{
				return Candidate.Id == Element.Id;
			}));
	}

	// 深度探针：在每条矿带的峰值深度上采样，最强元素必须是该带的元素。
	// Sampled 若错用了 ElementOrder，这里就会指到别的 id 上。
	struct FProbe
	{
		const TCHAR* Id;
		double Depth;
	};
	const FProbe Probes[] = {
		{ TEXT("a"), 150.0 },
		{ TEXT("b"), 450.0 },
		{ TEXT("c"), 750.0 },
	};

	FRandomStream Random(4242);
	int32 Mapped = 0;
	constexpr int32 Iterations = 64;
	for (int32 Iteration = 0; Iteration < Iterations; ++Iteration)
	{
		const FVector UnitDir = Random.GetUnitVector();
		for (const FProbe& Probe : Probes)
		{
			const FPlanetGeologySample Sample = Field->Sample(UnitDir, Probe.Depth);
			if (!Sampled.IsValidIndex(Sample.DominantIndex))
			{
				// 极小概率的噪声退化（整条带含量为 0）：不算错，但要求几乎全部命中。
				continue;
			}
			TestEqual(
				*FString::Printf(TEXT("%s 在深度 %.0f 的归属"), Probe.Id, Probe.Depth),
				Sampled[Sample.DominantIndex].Id,
				FString(Probe.Id));
			++Mapped;
		}
	}

	TestTrue(TEXT("几乎全部探针都命中了元素"), Mapped >= Iterations * 3 - 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlanetMineralBlocksDeterministicTest,
	"StellarEra.Planet.MineralBlocks.Deterministic",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetMineralBlocksDeterministicTest::RunTest(const FString& Parameters)
{
	const TSharedPtr<const FPlanetGeologyField> Field = PlanetMineralBlocksTest::MakeField();
	const PlanetMineralBlocks::FBuildParams Params = PlanetMineralBlocksTest::MakeParams();

	PlanetMineralBlocks::FBuildResult First;
	PlanetMineralBlocks::FBuildResult Second;
	PlanetMineralBlocks::Build(*Field, Params, First);
	PlanetMineralBlocks::Build(*Field, Params, Second);

	TestEqual(TEXT("采样点数一致"), Second.SampleCount, First.SampleCount);
	TestEqual(TEXT("组数一致"), Second.BlocksByElement.Num(), First.BlocksByElement.Num());

	for (int32 GroupIndex = 0; GroupIndex < First.BlocksByElement.Num(); ++GroupIndex)
	{
		const TArray<FPlanetMineralBlock>& A = First.BlocksByElement[GroupIndex];
		const TArray<FPlanetMineralBlock>& B = Second.BlocksByElement[GroupIndex];
		TestEqual(TEXT("组内数量一致"), B.Num(), A.Num());

		const int32 Limit = FMath::Min(A.Num(), B.Num());
		for (int32 Index = 0; Index < Limit; ++Index)
		{
			TestEqual(TEXT("位置一致"), B[Index].LocationCm, A[Index].LocationCm);
			TestEqual(TEXT("归属一致"), B[Index].ElementIndex, A[Index].ElementIndex);
		}
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlanetMineralBlocksThresholdTest,
	"StellarEra.Planet.MineralBlocks.Threshold",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetMineralBlocksThresholdTest::RunTest(const FString& Parameters)
{
	const TSharedPtr<const FPlanetGeologyField> Low = PlanetMineralBlocksTest::MakeField(0.22f);
	const TSharedPtr<const FPlanetGeologyField> High = PlanetMineralBlocksTest::MakeField(0.90f);

	PlanetMineralBlocks::FBuildResult LowResult;
	PlanetMineralBlocks::FBuildResult HighResult;
	PlanetMineralBlocks::Build(*Low, PlanetMineralBlocksTest::MakeParams(), LowResult);
	PlanetMineralBlocks::Build(*High, PlanetMineralBlocksTest::MakeParams(), HighResult);

	const int32 LowCount = PlanetMineralBlocksTest::CountBlocks(LowResult);
	const int32 HighCount = PlanetMineralBlocksTest::CountBlocks(HighResult);

	TestTrue(TEXT("低阈值确实有方块"), LowCount > 0);
	TestTrue(TEXT("高阈值不会更多"), HighCount <= LowCount);
	TestTrue(TEXT("高阈值确实更少"), HighCount < LowCount);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlanetMineralBlocksEmptyTest,
	"StellarEra.Planet.MineralBlocks.Empty",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetMineralBlocksEmptyTest::RunTest(const FString& Parameters)
{
	FPlanetGeologyLoadReport Report;
	const TSharedPtr<const FPlanetGeologyField> Field = FPlanetGeologyField::LoadFromString(
		PlanetMineralBlocksTest::ElementsJson,
		PlanetMineralBlocksTest::EmptyPlanetJson, Report, 1000.0);

	TestFalse(TEXT("解析没有报错"), Report.HasErrors());
	TestTrue(TEXT("空地质场"), Field->IsEmpty());
	TestEqual(TEXT("没有采样顺序元素"), Field->GetSampledElementDefinitions().Num(), 0);

	PlanetMineralBlocks::FBuildResult Result;
	PlanetMineralBlocks::Build(*Field, PlanetMineralBlocksTest::MakeParams(), Result);

	TestEqual(TEXT("空场直接返回，不采样"), Result.SampleCount, 0);
	TestEqual(TEXT("没有元素组"), Result.BlocksByElement.Num(), 0);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
