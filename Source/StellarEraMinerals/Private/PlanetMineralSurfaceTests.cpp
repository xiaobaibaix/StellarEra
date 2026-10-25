// Copyright Voxel Plugin SAS. All Rights Reserved.
// 矿物等值面构建：闭网格、局部栅格范围、分辨率语义、非法参数。

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "PlanetGeologyField.h"
#include "PlanetGeologyPreset.h"
#include "PlanetMineralSurface.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace PlanetMineralSurfaceTest
{
	constexpr float SurfaceRadiusCm = 100000.f; // 1 km 半径
	constexpr float VoxelSizeCm = 100.f;        // 1 体素 = 1 m

	FPlanetGeologyElementParams MakeElement(FName Id, float PodRadius, int32 Seed)
	{
		FPlanetGeologyElementParams Params;
		Params.Id = Id;
		Params.DisplayName = Id.ToString();
		Params.PodCount = 1;
		Params.PodRadius = PodRadius;
		Params.PodRadiusJitter = 0.f;
		Params.PodDepthBias = 0.f;
		Params.ShapeDetail = 2.0f;
		Params.VeinThreshold = 0.f;
		Params.Seed = Seed;
		return Params;
	}

	/**
	 * 矿囊位置由预设的随机流决定，不能指定。所以本文件所有几何断言
	 * 都先读 Field.GetPods() 拿到实际位置，再拿它当基准。
	 */
	UPlanetGeologyPreset* MakePreset(const TArray<FPlanetGeologyElementParams>& Elements)
	{
		UPlanetGeologyPreset* Preset = NewObject<UPlanetGeologyPreset>();
		Preset->Elements = Elements;
		Preset->Appearance.ColorThreshold = 0.05f;
		Preset->Appearance.FadeRange = 0.12f;
		Preset->Appearance.PurityForFullColor = 0.45f;
		return Preset;
	}

	TSharedPtr<const FPlanetGeologyField> MakeField(const UPlanetGeologyPreset& Preset)
	{
		return FPlanetGeologyField::BuildFromPreset(
			Preset, double(SurfaceRadiusCm / VoxelSizeCm));
	}

	PlanetMineralSurface::FBuildParams MakeParams(const FPlanetGeologyField& Field,
		int32 CellsPerPod)
	{
		PlanetMineralSurface::FBuildParams Params;
		Params.SurfaceRadiusCm = SurfaceRadiusCm;
		Params.VoxelSizeCm = VoxelSizeCm;
		Params.CellsPerPod = CellsPerPod;
		Params.IsoValue = Field.GetAppearance().ColorThreshold;
		return Params;
	}

	int32 CountNonEmptyMeshes(const PlanetMineralSurface::FBuildResult& Result)
	{
		int32 Count = 0;
		for (const FPlanetMineralMesh& Mesh : Result.MeshesByElement)
		{
			Count += Mesh.IsEmpty() ? 0 : 1;
		}
		return Count;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlanetMineralSurfaceBuildTest,
	"StellarEra.Planet.MineralSurface.Build",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetMineralSurfaceBuildTest::RunTest(const FString& Parameters)
{
	using namespace PlanetMineralSurfaceTest;

	UPlanetGeologyPreset* Preset = MakePreset({
		MakeElement(TEXT("a"), 0.06f, 11),
		MakeElement(TEXT("b"), 0.05f, 22),
		MakeElement(TEXT("c"), 0.04f, 33)});
	const auto Field = MakeField(*Preset);

	PlanetMineralSurface::FBuildResult Result;
	PlanetMineralSurface::Build(*Field, MakeParams(*Field, 12), Result);

	TestEqual(TEXT("每个元素一块网格"), Result.MeshesByElement.Num(), 3);
	TestTrue(TEXT("至少两个元素出面"), CountNonEmptyMeshes(Result) >= 2);
	TestTrue(TEXT("角点数非 0"), Result.GridPoints > 0);

	bool bTrianglesMultipleOfThree = true;
	for (const FPlanetMineralMesh& Mesh : Result.MeshesByElement)
	{
		bTrianglesMultipleOfThree &= Mesh.Triangles.Num() % 3 == 0;
	}
	TestTrue(TEXT("每块网格的三角形数都被 3 整除"), bTrianglesMultipleOfThree);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlanetMineralSurfaceClosedMeshTest,
	"StellarEra.Planet.MineralSurface.ClosedMesh",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetMineralSurfaceClosedMeshTest::RunTest(const FString& Parameters)
{
	using namespace PlanetMineralSurfaceTest;

	UPlanetGeologyPreset* Preset = MakePreset({
		MakeElement(TEXT("a"), 0.07f, 101),
		MakeElement(TEXT("b"), 0.03f, 202)});
	const auto Field = MakeField(*Preset);

	PlanetMineralSurface::FBuildResult Result;
	PlanetMineralSurface::Build(*Field, MakeParams(*Field, 12), Result);

	for (int32 ElementIndex = 0; ElementIndex < Result.MeshesByElement.Num(); ++ElementIndex)
	{
		const FPlanetMineralMesh& Mesh = Result.MeshesByElement[ElementIndex];
		if (Mesh.IsEmpty())
		{
			continue;
		}

		// 每条无向边恰好两个三角形 —— 面上开洞会破坏它。
		TMap<uint64, int32> EdgeUseCount;
		for (int32 TriangleIndex = 0; TriangleIndex < Mesh.Triangles.Num(); TriangleIndex += 3)
		{
			for (int32 EdgeIndex = 0; EdgeIndex < 3; ++EdgeIndex)
			{
				const int32 A = Mesh.Triangles[TriangleIndex + EdgeIndex];
				const int32 B = Mesh.Triangles[TriangleIndex + (EdgeIndex + 1) % 3];
				const uint64 Key = (uint64(FMath::Min(A, B)) << 32) | uint64(FMath::Max(A, B));
				++EdgeUseCount.FindOrAdd(Key);
			}
		}

		int32 OpenEdges = 0;
		for (const TPair<uint64, int32>& Pair : EdgeUseCount)
		{
			OpenEdges += Pair.Value == 2 ? 0 : 1;
		}
		TestEqual(*FString::Printf(TEXT("元素 %d 网格封闭（开放边 %d）"), ElementIndex, OpenEdges),
			OpenEdges, 0);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlanetMineralSurfacePodLocalTest,
	"StellarEra.Planet.MineralSurface.PodLocal",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetMineralSurfacePodLocalTest::RunTest(const FString& Parameters)
{
	using namespace PlanetMineralSurfaceTest;

	UPlanetGeologyPreset* Preset = MakePreset({
		MakeElement(TEXT("a"), 0.06f, 7),
		MakeElement(TEXT("b"), 0.05f, 8)});
	const auto Field = MakeField(*Preset);

	PlanetMineralSurface::FBuildResult Result;
	PlanetMineralSurface::Build(*Field, MakeParams(*Field, 12), Result);

	for (int32 ElementIndex = 0; ElementIndex < Result.MeshesByElement.Num(); ++ElementIndex)
	{
		const FPlanetMineralMesh& Mesh = Result.MeshesByElement[ElementIndex];
		if (Mesh.IsEmpty())
		{
			continue;
		}

		// 该元素所有矿囊包围盒的并集，外扩得多一点（插值 + 夹回球面会略微出界）。
		FBox PodBounds(ForceInit);
		for (const FPlanetGeologyPod& Pod : Field->GetPods(ElementIndex))
		{
			const FVector CenterCm = Pod.Center * SurfaceRadiusCm;
			const float RadiusCm = Pod.Radius * SurfaceRadiusCm;
			PodBounds += FBox(CenterCm - FVector(RadiusCm), CenterCm + FVector(RadiusCm));
		}
		PodBounds = PodBounds.ExpandBy(
			FVector(PodBounds.GetExtent().GetMax() * 0.5) + FVector::OneVector);

		int32 Outside = 0;
		for (const FVector3f& Vertex : Mesh.VerticesCm)
		{
			if (!PodBounds.IsInside(FVector(Vertex)))
			{
				++Outside;
			}
		}
		TestEqual(*FString::Printf(TEXT("元素 %d 的顶点全在矿囊包围盒内（越界 %d）"),
			ElementIndex, Outside), Outside, 0);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlanetMineralSurfaceCellsPerPodTest,
	"StellarEra.Planet.MineralSurface.CellsPerPod",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetMineralSurfaceCellsPerPodTest::RunTest(const FString& Parameters)
{
	using namespace PlanetMineralSurfaceTest;

	UPlanetGeologyPreset* Preset = MakePreset({MakeElement(TEXT("a"), 0.08f, 5)});
	const auto Field = MakeField(*Preset);

	PlanetMineralSurface::FBuildResult Coarse;
	PlanetMineralSurface::Build(*Field, MakeParams(*Field, 4), Coarse);

	PlanetMineralSurface::FBuildResult Fine;
	PlanetMineralSurface::Build(*Field, MakeParams(*Field, 24), Fine);

	const int32 CoarseTriangles = Coarse.MeshesByElement[0].Triangles.Num();
	const int32 FineTriangles = Fine.MeshesByElement[0].Triangles.Num();

	TestTrue(*FString::Printf(TEXT("粗网格 %d 个三角形索引，非 0"), CoarseTriangles),
		CoarseTriangles > 0);
	TestTrue(*FString::Printf(TEXT("细网格 %d 远多于粗网格 %d"), FineTriangles, CoarseTriangles),
		FineTriangles > CoarseTriangles * 3);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlanetMineralSurfaceEmptyTest,
	"StellarEra.Planet.MineralSurface.InvalidParams",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetMineralSurfaceEmptyTest::RunTest(const FString& Parameters)
{
	using namespace PlanetMineralSurfaceTest;

	UPlanetGeologyPreset* Preset = MakePreset({MakeElement(TEXT("a"), 0.06f, 11)});
	const auto Field = MakeField(*Preset);

	PlanetMineralSurface::FBuildResult Result;

	PlanetMineralSurface::FBuildParams BadParams = MakeParams(*Field, 12);
	BadParams.SurfaceRadiusCm = 0.f;
	PlanetMineralSurface::Build(*Field, BadParams, Result);
	TestEqual(TEXT("半径 0 → 空结果"), Result.MeshesByElement.Num(), 0);

	BadParams = MakeParams(*Field, 12);
	BadParams.CellsPerPod = 0;
	PlanetMineralSurface::Build(*Field, BadParams, Result);
	TestEqual(TEXT("CellsPerPod 0 → 空结果"), Result.MeshesByElement.Num(), 0);

	BadParams = MakeParams(*Field, 12);
	BadParams.VoxelSizeCm = 0.f;
	PlanetMineralSurface::Build(*Field, BadParams, Result);
	TestEqual(TEXT("体素尺寸 0 → 空结果"), Result.MeshesByElement.Num(), 0);
	return true;
}

//////////////////////////////////////////////////////////////////////////
// spec §7.1「保留」表：语义与矿层模型无关，只换夹具。缺一个都算覆盖回退。

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlanetMineralSurfaceIsoAccuracyTest,
	"StellarEra.Planet.MineralSurface.IsoAccuracy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetMineralSurfaceIsoAccuracyTest::RunTest(const FString& Parameters)
{
	using namespace PlanetMineralSurfaceTest;

	UPlanetGeologyPreset* Preset = MakePreset({
		MakeElement(TEXT("a"), 0.06f, 11),
		MakeElement(TEXT("b"), 0.05f, 22)});
	const auto Field = MakeField(*Preset);

	PlanetMineralSurface::FBuildResult Result;
	const PlanetMineralSurface::FBuildParams Params = MakeParams(*Field, 16);
	PlanetMineralSurface::Build(*Field, Params, Result);

	const double RadiusCm = double(Params.SurfaceRadiusCm);
	const double Iso = double(Params.IsoValue);
	const float Tolerance = 0.15f;

	for (int32 ElementIndex = 0; ElementIndex < Result.MeshesByElement.Num(); ++ElementIndex)
	{
		const FPlanetMineralMesh& Mesh = Result.MeshesByElement[ElementIndex];
		int32 Probes = 0;
		int32 OffSurface = 0;

		// 露头矿囊的顶点不参与等值线精度，由 SurfaceOutcrop 覆盖。但排除范围不能只按
		// 「被夹回球面」（|v| ≥ R − 1 cm）：球外角点恒为 0（SampleElementNormAtNormalized
		// 的球外早退），跨球面的网格边上因此有一条台阶，MC 顶点落在台阶近旁时离球面
		// 最多差一格再多出球外容差（1.0001 R 的 10 cm），这些点上的真值远高于插入值。
		// 一格的上界取「该元素最大矿囊半径对应的格边」（簇内最小半径 ≤ 该半径）。
		double MaxPodRadiusCm = 0.0;
		for (const FPlanetGeologyPod& Pod : Field->GetPods(ElementIndex))
		{
			MaxPodRadiusCm = FMath::Max(MaxPodRadiusCm, double(Pod.Radius) * RadiusCm);
		}
		const double CellSizeBoundCm = 2.0 * MaxPodRadiusCm / double(Params.CellsPerPod);
		const double ExcludedRadiusCm = RadiusCm * 1.0001 - CellSizeBoundCm;

		for (int32 Index = 0; Index < Mesh.VerticesCm.Num(); Index += 13)
		{
			const FVector3f& Vertex = Mesh.VerticesCm[Index];
			const FVector Position(double(Vertex.X), double(Vertex.Y), double(Vertex.Z));
			const double LengthCm = Position.Size();
			if (LengthCm <= UE_DOUBLE_KINDA_SMALL_NUMBER || LengthCm >= ExcludedRadiusCm)
			{
				continue;
			}

			++Probes;
			const float Norm = Field->SampleElementNormAtNormalized(
				ElementIndex, Position / RadiusCm);
			OffSurface += FMath::Abs(double(Norm) - Iso) > Tolerance ? 1 : 0;
		}

		TestTrue(*FString::Printf(TEXT("元素 #%d 采到了顶点"), ElementIndex), Probes > 0);
		TestEqual(*FString::Printf(TEXT("元素 #%d 的顶点都在等值线附近（%d/%d 越界）"),
			ElementIndex, OffSurface, Probes), OffSurface, 0);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlanetMineralSurfaceElementOrderTest,
	"StellarEra.Planet.MineralSurface.ElementOrder",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetMineralSurfaceElementOrderTest::RunTest(const FString& Parameters)
{
	using namespace PlanetMineralSurfaceTest;

	// 小半径、少矿囊：保证两元素互不重叠，DominantIndex 不会来回跳。
	UPlanetGeologyPreset* Preset = MakePreset({
		MakeElement(TEXT("a"), 0.02f, 11),
		MakeElement(TEXT("b"), 0.02f, 22)});
	const auto Field = MakeField(*Preset);

	PlanetMineralSurface::FBuildResult Result;
	const PlanetMineralSurface::FBuildParams Params = MakeParams(*Field, 16);
	PlanetMineralSurface::Build(*Field, Params, Result);

	const TArray<FPlanetGeologyElement> Sampled = Field->GetSampledElementDefinitions();
	const double RadiusCm = double(Params.SurfaceRadiusCm);
	const double VoxelSizeCmValue = double(Params.VoxelSizeCm);

	for (int32 ElementIndex = 0; ElementIndex < Result.MeshesByElement.Num(); ++ElementIndex)
	{
		const FPlanetMineralMesh& Mesh = Result.MeshesByElement[ElementIndex];
		int32 Probes = 0;
		int32 Mismatches = 0;
		for (int32 Index = 0; Index < Mesh.VerticesCm.Num(); Index += 17)
		{
			const FVector3f& Vertex = Mesh.VerticesCm[Index];
			const FVector Position(double(Vertex.X), double(Vertex.Y), double(Vertex.Z));
			const double LengthCm = Position.Size();
			if (LengthCm <= UE_DOUBLE_KINDA_SMALL_NUMBER)
			{
				continue;
			}

			++Probes;
			const double Depth = (RadiusCm - LengthCm) / VoxelSizeCmValue;
			const FPlanetGeologySample Sample =
				Field->Sample(Position / LengthCm, Depth);
			if (!Sampled.IsValidIndex(Sample.DominantIndex))
			{
				++Mismatches;
				continue;
			}
			Mismatches += Sample.DominantIndex == ElementIndex ? 0 : 1;
		}

		TestTrue(*FString::Printf(TEXT("元素 #%d 采到了顶点"), ElementIndex), Probes > 0);
		TestEqual(*FString::Printf(TEXT("元素 #%d 的顶点归它自己（%d/%d 错配）"),
			ElementIndex, Mismatches, Probes), Mismatches, 0);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlanetMineralSurfaceThresholdTest,
	"StellarEra.Planet.MineralSurface.Threshold",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetMineralSurfaceThresholdTest::RunTest(const FString& Parameters)
{
	using namespace PlanetMineralSurfaceTest;

	auto CountTriangles = [](const PlanetMineralSurface::FBuildResult& Result)
	{
		int32 Total = 0;
		for (const FPlanetMineralMesh& Mesh : Result.MeshesByElement)
		{
			Total += Mesh.Triangles.Num();
		}
		return Total;
	};
	auto CountOccupied = [](const PlanetMineralSurface::FBuildResult& Result)
	{
		int32 Total = 0;
		for (const FPlanetMineralMesh& Mesh : Result.MeshesByElement)
		{
			Total += Mesh.OccupiedCells;
		}
		return Total;
	};

	UPlanetGeologyPreset* Preset = MakePreset({
		MakeElement(TEXT("a"), 0.07f, 11),
		MakeElement(TEXT("b"), 0.06f, 22)});
	const auto Field = MakeField(*Preset);

	PlanetMineralSurface::FBuildParams Low = MakeParams(*Field, 16);
	Low.IsoValue = Field->GetAppearance().ColorThreshold;
	PlanetMineralSurface::FBuildParams High = Low;
	High.IsoValue = 0.6f;

	PlanetMineralSurface::FBuildResult LowResult;
	PlanetMineralSurface::Build(*Field, Low, LowResult);
	PlanetMineralSurface::FBuildResult HighResult;
	PlanetMineralSurface::Build(*Field, High, HighResult);

	TestTrue(TEXT("阈值升高，占据格数严格变少"), CountOccupied(HighResult) < CountOccupied(LowResult));
	TestTrue(TEXT("高阈值仍有几何"), CountTriangles(HighResult) > 0);
	TestTrue(TEXT("阈值升高，三角形不增加"), CountTriangles(HighResult) <= CountTriangles(LowResult));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlanetMineralSurfaceEmptyFieldTest,
	"StellarEra.Planet.MineralSurface.Empty",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetMineralSurfaceEmptyFieldTest::RunTest(const FString& Parameters)
{
	using namespace PlanetMineralSurfaceTest;

	// 所有元素 PodCount = 0：场的 IsEmpty() 为真（spec §3.6），Build 干净拒绝。
	FPlanetGeologyElementParams Zero = MakeElement(TEXT("a"), 0.06f, 11);
	Zero.PodCount = 0;
	UPlanetGeologyPreset* Preset = MakePreset({Zero});
	const auto Field = MakeField(*Preset);

	TestTrue(TEXT("全零矿囊的场是空场"), Field->IsEmpty());

	PlanetMineralSurface::FBuildResult Result;
	PlanetMineralSurface::Build(*Field, MakeParams(*Field, 12), Result);
	TestEqual(TEXT("空场 → 空结果"), Result.MeshesByElement.Num(), 0);
	TestEqual(TEXT("空场不花时间求角点"), Result.GridPoints, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlanetMineralSurfaceDeterministicTest,
	"StellarEra.Planet.MineralSurface.Deterministic",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetMineralSurfaceDeterministicTest::RunTest(const FString& Parameters)
{
	using namespace PlanetMineralSurfaceTest;

	UPlanetGeologyPreset* Preset = MakePreset({
		MakeElement(TEXT("a"), 0.06f, 11),
		MakeElement(TEXT("b"), 0.05f, 22)});
	const auto Field = MakeField(*Preset);

	PlanetMineralSurface::FBuildResult First;
	PlanetMineralSurface::FBuildResult Second;
	const PlanetMineralSurface::FBuildParams Params = MakeParams(*Field, 12);
	PlanetMineralSurface::Build(*Field, Params, First);
	PlanetMineralSurface::Build(*Field, Params, Second);

	if (!TestEqual(TEXT("元素数一致"), First.MeshesByElement.Num(), Second.MeshesByElement.Num()))
	{
		return false;
	}

	for (int32 ElementIndex = 0; ElementIndex < First.MeshesByElement.Num(); ++ElementIndex)
	{
		const FPlanetMineralMesh& A = First.MeshesByElement[ElementIndex];
		const FPlanetMineralMesh& B = Second.MeshesByElement[ElementIndex];
		TestEqual(*FString::Printf(TEXT("元素 %d 顶点数一致"), ElementIndex),
			A.VerticesCm.Num(), B.VerticesCm.Num());
		TestEqual(*FString::Printf(TEXT("元素 %d 三角索引一致"), ElementIndex),
			A.Triangles.Num(), B.Triangles.Num());

		// 逐顶点、逐索引相同：分簇的并行顺序若泄漏进结果，这里会抓到。
		int32 VertexDiff = 0;
		for (int32 Index = 0; Index < FMath::Min(A.VerticesCm.Num(), B.VerticesCm.Num()); ++Index)
		{
			VertexDiff += A.VerticesCm[Index] == B.VerticesCm[Index] ? 0 : 1;
		}
		TestEqual(*FString::Printf(TEXT("元素 %d 顶点裂变数"), ElementIndex), VertexDiff, 0);

		int32 IndexDiff = 0;
		for (int32 Index = 0; Index < FMath::Min(A.Triangles.Num(), B.Triangles.Num()); ++Index)
		{
			IndexDiff += A.Triangles[Index] == B.Triangles[Index] ? 0 : 1;
		}
		TestEqual(*FString::Printf(TEXT("元素 %d 索引裂变数"), ElementIndex), IndexDiff, 0);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlanetMineralSurfaceEmptySlotsTest,
	"StellarEra.Planet.MineralSurface.EmptySlots",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetMineralSurfaceEmptySlotsTest::RunTest(const FString& Parameters)
{
	using namespace PlanetMineralSurfaceTest;

	FPlanetGeologyElementParams Middle = MakeElement(TEXT("b"), 0.06f, 22);
	Middle.PodCount = 0;

	UPlanetGeologyPreset* Preset = MakePreset({
		MakeElement(TEXT("a"), 0.06f, 11),
		Middle,
		MakeElement(TEXT("c"), 0.06f, 33)});
	const auto Field = MakeField(*Preset);

	PlanetMineralSurface::FBuildResult Result;
	PlanetMineralSurface::Build(*Field, MakeParams(*Field, 12), Result);

	// 网格数 == 元素数：观察体按下标建组件，少一个就会错位。
	TestEqual(TEXT("网格数等于元素数"), Result.MeshesByElement.Num(), 3);
	bool bMiddleEmpty = Result.MeshesByElement.IsValidIndex(1)
		&& Result.MeshesByElement[1].IsEmpty();
	TestTrue(TEXT("中间那个零矿囊元素留空网格"), bMiddleEmpty);
	TestTrue(TEXT("两侧元素仍有几何"),
		Result.MeshesByElement.IsValidIndex(0) && !Result.MeshesByElement[0].IsEmpty()
		&& Result.MeshesByElement.IsValidIndex(2) && !Result.MeshesByElement[2].IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlanetMineralSurfaceCacheKeyTest,
	"StellarEra.Planet.MineralSurface.CacheKey",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetMineralSurfaceCacheKeyTest::RunTest(const FString& Parameters)
{
	PlanetMineralSurface::FCacheKey Base;
	Base.SurfaceRadiusCm = 100000.f;
	Base.Scale = FVector::OneVector;
	Base.CellsPerPod = 12;

	PlanetMineralSurface::FCacheKey Same = Base;
	TestTrue(TEXT("同值键相等"), Same == Base);

	PlanetMineralSurface::FCacheKey Resolution = Base;
	Resolution.CellsPerPod = 24;
	TestTrue(TEXT("改分辨率 → 键不等"), Resolution != Base);

	PlanetMineralSurface::FCacheKey Radius = Base;
	Radius.SurfaceRadiusCm = 200000.f;
	TestTrue(TEXT("改半径 → 键不等"), Radius != Base);

	PlanetMineralSurface::FCacheKey Scale = Base;
	Scale.Scale = FVector(2.0, 2.0, 2.0);
	TestTrue(TEXT("改缩放 → 键不等"), Scale != Base);

	// 半径容差 0.01 cm（0.1 mm）：亚微米级抖动不该触发重建。
	PlanetMineralSurface::FCacheKey TinyRadius = Base;
	TinyRadius.SurfaceRadiusCm = Base.SurfaceRadiusCm + 0.001f;
	TestTrue(TEXT("半径抖动在容差内 → 键仍相等"), TinyRadius == Base);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlanetMineralSurfaceOutcropTest,
	"StellarEra.Planet.MineralSurface.SurfaceOutcrop",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetMineralSurfaceOutcropTest::RunTest(const FString& Parameters)
{
	using namespace PlanetMineralSurfaceTest;

	// 48 囊 / r=0.08：约 22% 的囊满足 rn + r > 1，固定种子下必然出现露头。
	FPlanetGeologyElementParams Element = MakeElement(TEXT("a"), 0.08f, 5);
	Element.PodCount = 48;
	Element.PodRadiusJitter = 0.2f;
	UPlanetGeologyPreset* Preset = MakePreset({Element});
	const auto Field = MakeField(*Preset);

	bool bAnyPodCrossesSurface = false;
	for (const FPlanetGeologyPod& Pod : Field->GetPods(0))
	{
		bAnyPodCrossesSurface |= Pod.Center.Size() + Pod.Radius > 1.f;
	}
	if (!TestTrue(TEXT("夹具里确有矿囊探出球面"), bAnyPodCrossesSurface))
	{
		return false;
	}

	PlanetMineralSurface::FBuildResult Result;
	const PlanetMineralSurface::FBuildParams Params = MakeParams(*Field, 12);
	PlanetMineralSurface::Build(*Field, Params, Result);

	const FPlanetMineralMesh& Mesh = Result.MeshesByElement[0];
	if (!TestTrue(TEXT("出面了"), !Mesh.IsEmpty()))
	{
		return false;
	}

	const double RadiusCm = double(Params.SurfaceRadiusCm);
	int32 Outside = 0;
	int32 Clamped = 0;
	for (const FVector3f& Vertex : Mesh.VerticesCm)
	{
		const double LengthCm =
			FVector(double(Vertex.X), double(Vertex.Y), double(Vertex.Z)).Size();
		Outside += LengthCm > RadiusCm + 1.0 ? 1 : 0;
		Clamped += LengthCm >= RadiusCm - 1.0 ? 1 : 0;
	}

	TestEqual(*FString::Printf(TEXT("顶点全部夹在球内或球面上（越界 %d）"), Outside), Outside, 0);
	TestTrue(*FString::Printf(TEXT("露头顶点被夹到球面（%d 个）"), Clamped), Clamped > 0);
	return true;
}

#endif
