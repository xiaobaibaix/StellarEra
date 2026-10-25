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

	/**
	 * 浅部矿带夹具：与 Content/Geology/Planets/earth.json 的硅带同形
	 * （peakDepth 5 体素、layerThickness 22、colorThreshold 0.05）。
	 *
	 * 高斯在 |深度 - 峰值| = sqrt(ln 20) × 22 ≈ 38 体素处降到 0.05，而峰值只有
	 * 5 体素深 —— 带的外沿整个跨过球面。球外角点恒为 0，于是 MC 会在「内侧正值」
	 * 与「球外 0」之间做线性插值，把顶点放到球外。现有夹具的带都在 150/450/750
	 * 体素深，离球面足够远，永远碰不到这一条。
	 */
	const TCHAR* SurfaceBandPlanetJson = TEXT(R"JSON(
{
  "version": 1,
  "geometryAmplitude": 1.5,
  "appearance": { "colorThreshold": 0.05, "fadeRange": 0.12, "purityForFullColor": 0.45 },
  "elements": {
    "a": { "abundance": 1.0, "peakDepth": 5, "layerThickness": 22 }
  }
}
)JSON");

	TSharedPtr<const FPlanetGeologyField> MakeSurfaceBandField()
	{
		FPlanetGeologyLoadReport Report;
		TSharedPtr<const FPlanetGeologyField> Field = FPlanetGeologyField::LoadFromString(
			ElementsJson, SurfaceBandPlanetJson, Report, /*PlanetRadiusVoxels=*/1000.0);
		check(Field.IsValid());
		return Field;
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlanetMineralSurfaceBuildTest,
	"StellarEra.Planet.MineralSurface.Build",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetMineralSurfaceBuildTest::RunTest(const FString& Parameters)
{
	const TSharedPtr<const FPlanetGeologyField> Field = PlanetMineralSurfaceTest::MakeField();
	const PlanetMineralSurface::FBuildParams Params =
		PlanetMineralSurfaceTest::MakeParams(*Field, /*GridResolution=*/80);

	PlanetMineralSurface::FBuildResult Result;
	PlanetMineralSurface::Build(*Field, Params, Result);

	// 0.3 秒预算关心的是这两个数：耗时进日志，headless 跑也能留下证据。
	AddInfo(FString::Printf(TEXT("构建 %.1f ms，求值角点 %d（网格 %d³）"),
		Result.BuildSeconds * 1000.0, Result.GridPoints, Params.GridResolution));

	if (!TestEqual(TEXT("每个元素一个槽"), Result.MeshesByElement.Num(), Field->NumElements()))
	{
		return false;
	}

	TestTrue(TEXT("求过噪声的角点非空"), Result.GridPoints > 0);
	TestTrue(TEXT("构建耗时在合理量级"),
		Result.BuildSeconds > 0.0 && Result.BuildSeconds < 30.0);

	const double RadiusCm = double(Params.SurfaceRadiusCm);
	const double MaxDepth = double(Field->GetMaxReachDepth());

	int32 TotalTriangles = 0;
	for (int32 ElementIndex = 0; ElementIndex < Result.MeshesByElement.Num(); ++ElementIndex)
	{
		const FPlanetMineralMesh& Mesh = Result.MeshesByElement[ElementIndex];
		TestTrue(*FString::Printf(TEXT("元素 #%d 有几何"), ElementIndex), !Mesh.IsEmpty());
		TestEqual(TEXT("三角形下标数组长度是 3 的倍数"), Mesh.Triangles.Num() % 3, 0);
		TotalTriangles += Mesh.Triangles.Num() / 3;

		int32 BadIndices = 0;
		for (const int32 VertexIndex : Mesh.Triangles)
		{
			if (!Mesh.VerticesCm.IsValidIndex(VertexIndex))
			{
				++BadIndices;
			}
		}
		TestEqual(TEXT("三角形下标全部合法"), BadIndices, 0);

		int32 OutsideSphere = 0;
		int32 OutsideDepth = 0;
		for (const FVector3f& Vertex : Mesh.VerticesCm)
		{
			const double DistanceCm =
				FVector3d(double(Vertex.X), double(Vertex.Y), double(Vertex.Z)).Size();
			// 允许 1e-4 的相对越界：等值面端点就是最外一圈角点本身。
			if (DistanceCm > RadiusCm * (1.0 + 1e-4))
			{
				++OutsideSphere;
			}
			const double Depth = PlanetMineralSurfaceTest::VertexDepthVoxels(Vertex);
			if (Depth < -1e-2 || Depth > MaxDepth + 1e-2)
			{
				++OutsideDepth;
			}
		}
		TestEqual(TEXT("顶点都在球内"), OutsideSphere, 0);
		TestEqual(TEXT("顶点深度都在可达范围内"), OutsideDepth, 0);
	}

	TestTrue(TEXT("总三角形数非零"), TotalTriangles > 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlanetMineralSurfaceIsoAccuracyTest,
	"StellarEra.Planet.MineralSurface.IsoAccuracy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetMineralSurfaceIsoAccuracyTest::RunTest(const FString& Parameters)
{
	const TSharedPtr<const FPlanetGeologyField> Field = PlanetMineralSurfaceTest::MakeField();
	const PlanetMineralSurface::FBuildParams Params =
		PlanetMineralSurfaceTest::MakeParams(*Field, /*GridResolution=*/80);

	PlanetMineralSurface::FBuildResult Result;
	PlanetMineralSurface::Build(*Field, Params, Result);

	const double RadiusCm = double(Params.SurfaceRadiusCm);
	const double VoxelSizeCm = double(Params.VoxelSizeCm);
	const float Iso = Params.IsoValue;
	// MC 抽的是「网格三线性插值场」的等值面，顶点处再回采真实场会有插值误差；
	// 0.15 是等值线的 3 倍，宽到不抖，又窄到随机曲面必挂。
	const float Tolerance = 0.15f;

	TArray<float> Norms;
	for (int32 ElementIndex = 0; ElementIndex < Result.MeshesByElement.Num(); ++ElementIndex)
	{
		const FPlanetMineralMesh& Mesh = Result.MeshesByElement[ElementIndex];
		int32 Probes = 0;
		int32 Violations = 0;
		// 每隔 13 个顶点取一个：几千个探针够定性，全量会让测试跑到十几秒。
		for (int32 Index = 0; Index < Mesh.VerticesCm.Num(); Index += 13)
		{
			const FVector3f& Vertex = Mesh.VerticesCm[Index];
			const FVector3d Position(double(Vertex.X), double(Vertex.Y), double(Vertex.Z));
			const double DistanceCm = Position.Size();
			if (DistanceCm <= UE_DOUBLE_KINDA_SMALL_NUMBER)
			{
				continue;
			}

			const double Depth = (RadiusCm - DistanceCm) / VoxelSizeCm;
			Field->SampleElementNorms(FVector(Position / DistanceCm), Depth, Norms);
			if (!Norms.IsValidIndex(ElementIndex))
			{
				continue;
			}

			++Probes;
			if (FMath::Abs(Norms[ElementIndex] - Iso) > Tolerance)
			{
				++Violations;
			}
		}

		TestTrue(*FString::Printf(TEXT("元素 #%d 采到了顶点"), ElementIndex), Probes > 0);
		TestTrue(*FString::Printf(TEXT("元素 #%d 的顶点都在等值线附近（%d/%d 越界）"),
			ElementIndex, Violations, Probes), Violations * 10 <= Probes);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlanetMineralSurfaceElementOrderTest,
	"StellarEra.Planet.MineralSurface.ElementOrder",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetMineralSurfaceElementOrderTest::RunTest(const FString& Parameters)
{
	const TSharedPtr<const FPlanetGeologyField> Field = PlanetMineralSurfaceTest::MakeField();
	const PlanetMineralSurface::FBuildParams Params =
		PlanetMineralSurfaceTest::MakeParams(*Field, /*GridResolution=*/80);

	PlanetMineralSurface::FBuildResult Result;
	PlanetMineralSurface::Build(*Field, Params, Result);

	const TArray<FPlanetGeologyElement> Sampled = Field->GetSampledElementDefinitions();
	const double RadiusCm = double(Params.SurfaceRadiusCm);
	const double VoxelSizeCm = double(Params.VoxelSizeCm);

	for (int32 ElementIndex = 0; ElementIndex < Result.MeshesByElement.Num(); ++ElementIndex)
	{
		const FPlanetMineralMesh& Mesh = Result.MeshesByElement[ElementIndex];
		int32 Probes = 0;
		int32 Mismatches = 0;
		for (int32 Index = 0; Index < Mesh.VerticesCm.Num(); Index += 17)
		{
			const FVector3f& Vertex = Mesh.VerticesCm[Index];
			const FVector3d Position(double(Vertex.X), double(Vertex.Y), double(Vertex.Z));
			const double DistanceCm = Position.Size();
			if (DistanceCm <= UE_DOUBLE_KINDA_SMALL_NUMBER)
			{
				continue;
			}

			// 三条矿带互不重叠，所以顶点处的「最强元素」必须就是这份网格的元素。
			// 若 Build 把网格下标与 Sampled 顺序对错了，这条会立刻红。
			const double Depth = (RadiusCm - DistanceCm) / VoxelSizeCm;
			const FPlanetGeologySample Sample =
				Field->Sample(FVector(Position / DistanceCm), Depth);
			if (!Sampled.IsValidIndex(Sample.DominantIndex))
			{
				continue;
			}

			++Probes;
			if (Sample.DominantIndex != ElementIndex)
			{
				++Mismatches;
			}
		}

		TestTrue(*FString::Printf(TEXT("元素 #%d 采到了顶点"), ElementIndex), Probes > 0);
		TestTrue(*FString::Printf(TEXT("元素 #%d 的顶点归它自己（%d/%d 错配）"),
			ElementIndex, Mismatches, Probes), Mismatches * 20 <= Probes);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlanetMineralSurfaceThresholdTest,
	"StellarEra.Planet.MineralSurface.Threshold",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetMineralSurfaceThresholdTest::RunTest(const FString& Parameters)
{
	auto CountTriangles = [](const PlanetMineralSurface::FBuildResult& Result)
	{
		int32 Count = 0;
		for (const FPlanetMineralMesh& Mesh : Result.MeshesByElement)
		{
			Count += Mesh.Triangles.Num() / 3;
		}
		return Count;
	};
	auto CountOccupied = [](const PlanetMineralSurface::FBuildResult& Result)
	{
		int32 Count = 0;
		for (const FPlanetMineralMesh& Mesh : Result.MeshesByElement)
		{
			Count += Mesh.OccupiedCells;
		}
		return Count;
	};

	const TSharedPtr<const FPlanetGeologyField> Field = PlanetMineralSurfaceTest::MakeField();

	PlanetMineralSurface::FBuildParams LowParams =
		PlanetMineralSurfaceTest::MakeParams(*Field, /*GridResolution=*/80);
	LowParams.IsoValue = 0.05f;

	// 0.5 对应矿带半厚 ±42 体素（0.05 是 ±87）：几何一定还在，占据格数一定更少。
	PlanetMineralSurface::FBuildParams HighParams = LowParams;
	HighParams.IsoValue = 0.5f;

	PlanetMineralSurface::FBuildResult Low;
	PlanetMineralSurface::FBuildResult High;
	PlanetMineralSurface::Build(*Field, LowParams, Low);
	PlanetMineralSurface::Build(*Field, HighParams, High);

	// 占据格数是集合包含关系：值 > 0.5 的角点必然 > 0.05。严格变少是硬保证，
	// 也是「IsoValue 真的接进去了」的判据。
	TestTrue(TEXT("阈值升高，占据格数严格变少"), CountOccupied(High) < CountOccupied(Low));
	TestTrue(TEXT("高阈值仍有几何"), CountTriangles(High) > 0);
	// spec §7 只要求「总三角形数不增加」。别改成严格变小：等值面上移/下移 42 体素
	// 只让表面积少约 8%，格点量化足以把这个差值抹平，断言会偶发变红。
	TestTrue(TEXT("阈值升高，三角形不增加"), CountTriangles(High) <= CountTriangles(Low));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlanetMineralSurfaceEmptyTest,
	"StellarEra.Planet.MineralSurface.Empty",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetMineralSurfaceEmptyTest::RunTest(const FString& Parameters)
{
	FPlanetGeologyLoadReport Report;
	const TSharedPtr<const FPlanetGeologyField> Field = FPlanetGeologyField::LoadFromString(
		PlanetMineralSurfaceTest::ElementsJson, PlanetMineralSurfaceTest::EmptyPlanetJson,
		Report, 1000.0);
	if (!TestTrue(TEXT("空场加载出有效对象"), Field.IsValid()))
	{
		return false;
	}
	TestTrue(TEXT("场是空的"), Field->IsEmpty());

	// 先塞一份假结果，确认 Build 会清干净（缓存复用时走的就是不清空这条路）。
	PlanetMineralSurface::FBuildResult Result;
	Result.MeshesByElement.SetNum(3);
	Result.GridPoints = 123;
	Result.BuildSeconds = 9.0;

	PlanetMineralSurface::Build(*Field,
		PlanetMineralSurfaceTest::MakeParams(*Field, 40), Result);

	TestEqual(TEXT("空场不给网格"), Result.MeshesByElement.Num(), 0);
	TestEqual(TEXT("空场不求点"), Result.GridPoints, 0);
	TestTrue(TEXT("空场耗时清零"), Result.BuildSeconds == 0.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlanetMineralSurfaceDeterministicTest,
	"StellarEra.Planet.MineralSurface.Deterministic",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetMineralSurfaceDeterministicTest::RunTest(const FString& Parameters)
{
	const TSharedPtr<const FPlanetGeologyField> Field = PlanetMineralSurfaceTest::MakeField();
	const PlanetMineralSurface::FBuildParams Params =
		PlanetMineralSurfaceTest::MakeParams(*Field, /*GridResolution=*/40);

	// MC 并行抽取不保证顶点顺序，只保证顶点集合。比较前按 (X,Y,Z) 排序。
	auto SortedVertices = [](const FPlanetMineralMesh& Mesh)
	{
		TArray<FVector3f> Sorted = Mesh.VerticesCm;
		Sorted.Sort([](const FVector3f& Left, const FVector3f& Right)
		{
			if (Left.X != Right.X) { return Left.X < Right.X; }
			if (Left.Y != Right.Y) { return Left.Y < Right.Y; }
			return Left.Z < Right.Z;
		});
		return Sorted;
	};

	PlanetMineralSurface::FBuildResult First;
	PlanetMineralSurface::FBuildResult Second;
	PlanetMineralSurface::Build(*Field, Params, First);
	PlanetMineralSurface::Build(*Field, Params, Second);

	if (!TestEqual(TEXT("两次构建元素数一致"),
		Second.MeshesByElement.Num(), First.MeshesByElement.Num()))
	{
		return false;
	}

	for (int32 ElementIndex = 0; ElementIndex < First.MeshesByElement.Num(); ++ElementIndex)
	{
		const FPlanetMineralMesh& Expected = First.MeshesByElement[ElementIndex];
		const FPlanetMineralMesh& Actual = Second.MeshesByElement[ElementIndex];

		TestEqual(*FString::Printf(TEXT("元素 #%d 三角形数一致"), ElementIndex),
			Actual.Triangles.Num(), Expected.Triangles.Num());

		const TArray<FVector3f> ExpectedVertices = SortedVertices(Expected);
		const TArray<FVector3f> ActualVertices = SortedVertices(Actual);
		if (!TestEqual(*FString::Printf(TEXT("元素 #%d 顶点数一致"), ElementIndex),
			ActualVertices.Num(), ExpectedVertices.Num()))
		{
			continue;
		}

		int32 Differing = 0;
		for (int32 Index = 0; Index < ExpectedVertices.Num(); ++Index)
		{
			if (ExpectedVertices[Index].X != ActualVertices[Index].X
				|| ExpectedVertices[Index].Y != ActualVertices[Index].Y
				|| ExpectedVertices[Index].Z != ActualVertices[Index].Z)
			{
				++Differing;
			}
		}
		TestEqual(*FString::Printf(TEXT("元素 #%d 顶点集合一致"), ElementIndex),
			Differing, 0);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlanetMineralSurfaceBandTest,
	"StellarEra.Planet.MineralSurface.SurfaceBand",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetMineralSurfaceBandTest::RunTest(const FString& Parameters)
{
	const TSharedPtr<const FPlanetGeologyField> Field =
		PlanetMineralSurfaceTest::MakeSurfaceBandField();
	const PlanetMineralSurface::FBuildParams Params =
		PlanetMineralSurfaceTest::MakeParams(*Field, /*GridResolution=*/40);
	const double RadiusCm = double(Params.SurfaceRadiusCm);

	PlanetMineralSurface::FBuildResult Result;
	PlanetMineralSurface::Build(*Field, Params, Result);

	int32 TotalTriangles = 0;
	double MaxRatio = 0.0;
	for (const FPlanetMineralMesh& Mesh : Result.MeshesByElement)
	{
		TotalTriangles += Mesh.Triangles.Num() / 3;
		for (const FVector3f& Vertex : Mesh.VerticesCm)
		{
			MaxRatio = FMath::Max(MaxRatio,
				FVector3d(double(Vertex.X), double(Vertex.Y), double(Vertex.Z)).Size()
				/ RadiusCm);
		}
	}

	// 极大值也进日志：没有夹取时它就是红态证据，夹取之后必须正好是 1.0。
	AddInfo(FString::Printf(TEXT("浅部带：%d 个三角形，max |p| / R = %.6f"),
		TotalTriangles, MaxRatio));

	TestTrue(TEXT("浅部带真的在等值线之上（有三角形）"), TotalTriangles > 0);
	TestTrue(*FString::Printf(TEXT("所有顶点都在球内（max |p| / R = %.6f）"), MaxRatio),
		MaxRatio <= 1.0 + 1e-6);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlanetMineralSurfaceEmptySlotsTest,
	"StellarEra.Planet.MineralSurface.EmptySlots",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetMineralSurfaceEmptySlotsTest::RunTest(const FString& Parameters)
{
	const TSharedPtr<const FPlanetGeologyField> Field = PlanetMineralSurfaceTest::MakeField();
	PlanetMineralSurface::FBuildParams Params =
		PlanetMineralSurfaceTest::MakeParams(*Field, /*GridResolution=*/20);
	// Norm 归一化到 [0,1]；等值线取 2，任何角点都在门限以下 → 全部元素空网格。
	Params.IsoValue = 2.f;

	PlanetMineralSurface::FBuildResult Result;
	PlanetMineralSurface::Build(*Field, Params, Result);

	if (!TestEqual(TEXT("空元素照样占槽，元素数不变"),
		Result.MeshesByElement.Num(), Field->NumElements()))
	{
		return false;
	}
	for (int32 Index = 0; Index < Result.MeshesByElement.Num(); ++Index)
	{
		TestEqual(*FString::Printf(TEXT("元素 #%d 没有三角形"), Index),
			Result.MeshesByElement[Index].Triangles.Num(), 0);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlanetMineralSurfaceGridResolutionTest,
	"StellarEra.Planet.MineralSurface.GridResolution",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetMineralSurfaceGridResolutionTest::RunTest(const FString& Parameters)
{
	const TSharedPtr<const FPlanetGeologyField> Field = PlanetMineralSurfaceTest::MakeField();
	auto CountTriangles = [](const PlanetMineralSurface::FBuildResult& Result)
	{
		int32 Count = 0;
		for (const FPlanetMineralMesh& Mesh : Result.MeshesByElement)
		{
			Count += Mesh.Triangles.Num() / 3;
		}
		return Count;
	};

	// 缓存键测试只证明 GridResolution 进得了键；这条证明它真的接进了 FBuildParams
	// 并改变了网格。同一份场、只改格数。
	PlanetMineralSurface::FBuildResult Coarse;
	PlanetMineralSurface::FBuildResult Fine;
	PlanetMineralSurface::Build(*Field, PlanetMineralSurfaceTest::MakeParams(*Field, 20), Coarse);
	PlanetMineralSurface::Build(*Field, PlanetMineralSurfaceTest::MakeParams(*Field, 40), Fine);

	const int32 CoarseTriangles = CountTriangles(Coarse);
	const int32 FineTriangles = CountTriangles(Fine);
	AddInfo(FString::Printf(TEXT("格数 20 → %d 个三角形；格数 40 → %d 个三角形"),
		CoarseTriangles, FineTriangles));

	TestTrue(TEXT("两个格数都抽出几何"), CoarseTriangles > 0 && FineTriangles > 0);
	TestTrue(TEXT("格数变化改变了三角形数"), CoarseTriangles != FineTriangles);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlanetMineralSurfaceCacheKeyTest,
	"StellarEra.Planet.MineralSurface.CacheKey",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetMineralSurfaceCacheKeyTest::RunTest(const FString& Parameters)
{
	PlanetMineralSurface::FCacheKey Base;
	Base.SurfaceRadiusCm = 100000.f;
	Base.Scale = FVector(1.0, 1.0, 1.0);
	Base.GridResolution = 40;

	PlanetMineralSurface::FCacheKey Same = Base;
	TestTrue(TEXT("同参数相等"), Same == Base);
	TestFalse(TEXT("同参数不不等"), Same != Base);

	PlanetMineralSurface::FCacheKey Radius = Base;
	Radius.SurfaceRadiusCm = 100001.f;
	TestTrue(TEXT("半径差 1 cm 不相等"), Radius != Base);

	PlanetMineralSurface::FCacheKey TinyRadius = Base;
	TinyRadius.SurfaceRadiusCm = 100000.003f;
	TestTrue(TEXT("半径差 0.03 mm 视为同一颗行星"), TinyRadius == Base);

	PlanetMineralSurface::FCacheKey Scale = Base;
	Scale.Scale = FVector(2.0, 2.0, 2.0);
	TestTrue(TEXT("缩放变化不相等"), Scale != Base);

	PlanetMineralSurface::FCacheKey Resolution = Base;
	Resolution.GridResolution = 56;
	TestTrue(TEXT("格数变化不相等"), Resolution != Base);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
