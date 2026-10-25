// 等值面矿体的自动化测试：按元素强度出口、运行期顺序、几何、对齐、阈值、确定性、空场。

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Math/RandomStream.h"
#include "PlanetGeologyField.h"
#include "PlanetMineralSurface.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace PlanetMineralSurfaceTest
{
	// 三个元素：a/b/c。半径 1000 体素，矿带峰值 150 / 450 / 750，厚度 50。
	// 3σ = 150 体素，三条带分别覆盖 0..300 / 300..600 / 600..900 —— 互不重叠，
	// 于是「顶点落在哪条带」能唯一确定它属于哪个元素。
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
		TSharedPtr<const FPlanetGeologyField> Field = FPlanetGeologyField::LoadFromString(
			ElementsJson, MakePlanetJson(ColorThreshold), Report, /*PlanetRadiusVoxels=*/1000.0);
		check(Field.IsValid());
		return Field;
	}

	PlanetMineralSurface::FBuildParams MakeParams(const FPlanetGeologyField& Field,
		int32 GridResolution)
	{
		PlanetMineralSurface::FBuildParams Params;
		Params.SurfaceRadiusCm = 100000.f; // 1000 体素 × 100 cm
		Params.VoxelSizeCm = 100.f;
		Params.GridResolution = GridResolution;
		Params.IsoValue = Field.GetAppearance().ColorThreshold;
		return Params;
	}

	/** 顶点 → 深度（体素，基准球面为 0，向内为正）。 */
	double VertexDepthVoxels(const FVector3f& VertexCm)
	{
		const double DistanceCm =
			FVector3d(double(VertexCm.X), double(VertexCm.Y), double(VertexCm.Z)).Size();
		return (100000.0 - DistanceCm) / 100.0;
	}

	/** 元素 id → 峰值深度（体素）；未知 id 返回 -1。 */
	double PeakDepthVoxels(const FString& Id)
	{
		if (Id == TEXT("a")) { return 150.0; }
		if (Id == TEXT("b")) { return 450.0; }
		if (Id == TEXT("c")) { return 750.0; }
		return -1.0;
	}
} // namespace PlanetMineralSurfaceTest

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlanetElementNormsMatchesDominantTest,
	"StellarEra.Planet.Geology.ElementNorms.MatchesDominant",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetElementNormsMatchesDominantTest::RunTest(const FString& Parameters)
{
	const TSharedPtr<const FPlanetGeologyField> Field = PlanetMineralSurfaceTest::MakeField();

	// 新出口必须与 Sample 同源：最大值 = DominantNorm，argmax = DominantIndex。
	// 两条路径都只在严格大于时才换赢家，所以并列时也选同一个下标。
	FRandomStream Random(2026);
	const double Probes[] = { 5.0, 150.0, 450.0, 750.0 };
	TArray<float> Norms;

	for (int32 Iteration = 0; Iteration < 64; ++Iteration)
	{
		const FVector UnitDir = Random.GetUnitVector();
		for (const double Depth : Probes)
		{
			Field->SampleElementNorms(UnitDir, Depth, Norms);
			if (!TestEqual(TEXT("每个元素一个强度"), Norms.Num(), Field->NumElements()))
			{
				return false;
			}

			float MaxNorm = 0.f;
			int32 MaxIndex = INDEX_NONE;
			for (int32 Index = 0; Index < Norms.Num(); ++Index)
			{
				if (Norms[Index] > MaxNorm)
				{
					MaxNorm = Norms[Index];
					MaxIndex = Index;
				}
			}

			const FPlanetGeologySample Sample = Field->Sample(UnitDir, Depth);
			TestTrue(TEXT("最大值等于 DominantNorm"),
				FMath::IsNearlyEqual(MaxNorm, float(Sample.DominantNorm), 1e-6f));
			TestEqual(TEXT("argmax 等于 DominantIndex"), MaxIndex, Sample.DominantIndex);
		}
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlanetElementNormsBandProbeTest,
	"StellarEra.Planet.Geology.ElementNorms.BandProbe",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetElementNormsBandProbeTest::RunTest(const FString& Parameters)
{
	const TSharedPtr<const FPlanetGeologyField> Field = PlanetMineralSurfaceTest::MakeField();
	const TArray<FPlanetGeologyElement> Sampled = Field->GetSampledElementDefinitions();

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

	FRandomStream Random(99);
	TArray<float> Norms;
	for (int32 Iteration = 0; Iteration < 32; ++Iteration)
	{
		const FVector UnitDir = Random.GetUnitVector();
		for (const FProbe& Probe : Probes)
		{
			int32 ProbeIndex = INDEX_NONE;
			for (int32 Index = 0; Index < Sampled.Num(); ++Index)
			{
				if (Sampled[Index].Id == FString(Probe.Id))
				{
					ProbeIndex = Index;
				}
			}
			if (!TestTrue(TEXT("夹具里能找到 a/b/c"), Sampled.IsValidIndex(ProbeIndex)))
			{
				return false;
			}

			Field->SampleElementNorms(UnitDir, Probe.Depth, Norms);
			// 别的带在这个深度已经衰减到 3σ 之外（< 1e-4），本带必须是最强的。
			for (int32 Index = 0; Index < Norms.Num(); ++Index)
			{
				if (Index == ProbeIndex)
				{
					continue;
				}
				TestTrue(*FString::Printf(TEXT("%s 的峰值深度上别的元素更弱"), Probe.Id),
					Norms[Index] <= Norms[ProbeIndex] + 1e-6f);
			}
		}
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlanetGeologySampledDefinitionsTest,
	"StellarEra.Planet.Geology.SampledDefinitions",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetGeologySampledDefinitionsTest::RunTest(const FString& Parameters)
{
	const TSharedPtr<const FPlanetGeologyField> Field = PlanetMineralSurfaceTest::MakeField();

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

#endif // WITH_DEV_AUTOMATION_TESTS
