// Copyright Voxel Plugin SAS. All Rights Reserved.
// 矿物等值面构建：闭网格、局部栅格范围、分辨率语义、非法参数。

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "PlanetGeologyDisseminatedDistribution.h"
#include "PlanetGeologyField.h"
#include "PlanetGeologyLensDistribution.h"
#include "PlanetGeologyPreset.h"
#include "PlanetGeologyVeinDistribution.h"
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
		int32 CellsPerFeature)
	{
		PlanetMineralSurface::FBuildParams Params;
		Params.SurfaceRadiusCm = SurfaceRadiusCm;
		Params.VoxelSizeCm = VoxelSizeCm;
		Params.CellsPerFeature = CellsPerFeature;
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

	/**
	 * 与 PlanetMineralSurface::Build 同一套格边公式（基准 + 两条上限钳制）。
	 *
	 * 测试里凡是「顶点最多离矿囊包围盒一格」这类界，都必须用钳制后的格边，
	 * 否则上限一旦生效（夹具变大 / 改参数）就会假红。
	 * RawExtentCm 是矿囊包围盒并集的原始边长；外扩量按基准格边计（与 Build 一致）。
	 */
	double ExpectedCellSizeCm(double MinRadiusCm, const FVector& RawExtentCm, int32 CellsPerFeature)
	{
		constexpr int32 MaxCellsPerAxis = 512;
		constexpr int64 MaxTotalCorners = 8000000;

		double CellSize = 2.0 * MinRadiusCm / double(CellsPerFeature);
		const FVector PaddedExtent = RawExtentCm + FVector(2.0 * CellSize);
		const double ExtentMax = PaddedExtent.GetMax();
		// 除数先减掉外扩的两格 + 角点那一格，上限才是硬上限（与 Build 的取整账一致）。
		CellSize = FMath::Max(CellSize, ExtentMax / double(MaxCellsPerAxis - 2));
		CellSize = FMath::Max(CellSize, ExtentMax
			/ (FMath::Pow(static_cast<double>(MaxTotalCorners), 1.0 / 3.0) - 3.0));
		return CellSize;
	}

	/** 矿囊包围盒（厘米空间）——与 Build 收集 PodBounds 的写法相同。 */
	FBox PodAabbCm(const FPlanetGeologyPod& Pod, double InSurfaceRadiusCm)
	{
		const FVector CenterCm = Pod.Center * InSurfaceRadiusCm;
		const FVector RadiusCm = FVector(double(Pod.Radius) * InSurfaceRadiusCm);
		return FBox(CenterCm - RadiusCm, CenterCm + RadiusCm);
	}

	/** 两个矿囊的 AABB（归一化空间）是否相交 —— 判据与 Build 的分簇完全相同。 */
	bool ArePodAabbsOverlapping(const FPlanetGeologyPod& A, const FPlanetGeologyPod& B)
	{
		const FVector AMin = A.Center - FVector(A.Radius);
		const FVector AMax = A.Center + FVector(A.Radius);
		const FVector BMin = B.Center - FVector(B.Radius);
		const FVector BMax = B.Center + FVector(B.Radius);
		return AMin.X <= BMax.X && AMax.X >= BMin.X
			&& AMin.Y <= BMax.Y && AMax.Y >= BMin.Y
			&& AMin.Z <= BMax.Z && AMax.Z >= BMin.Z;
	}

	/** 给已有元素挂一个显式生成器；PodCount 置 0，避免遗留兜底再插一批矿囊。 */
	template <typename TDistribution>
	TDistribution* AddDistribution(UPlanetGeologyPreset& Preset,
		FPlanetGeologyElementParams& Element)
	{
		Element.PodCount = 0;
		TDistribution* Distribution = NewObject<TDistribution>(&Preset);
		Distribution->Seed = 2468;
		Distribution->bSeedExplicit = true;
		Distribution->VeinThreshold = 0.f;
		Element.Distributions.Add(Distribution);
		return Distribution;
	}

	bool BoundsOverlap(const FBox& A, const FBox& B)
	{
		return A.Min.X <= B.Max.X && A.Max.X >= B.Min.X
			&& A.Min.Y <= B.Max.Y && A.Max.Y >= B.Min.Y
			&& A.Min.Z <= B.Max.Z && A.Max.Z >= B.Min.Z;
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
	"StellarEra.Planet.MineralSurface.ClosedMesh.Pods",
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlanetMineralSurfaceRegionLocalTest,
	"StellarEra.Planet.MineralSurface.RegionLocal",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetMineralSurfaceRegionLocalTest::RunTest(const FString& Parameters)
{
	using namespace PlanetMineralSurfaceTest;

	// 允许域只能从「区域」来。检查体做成 lambda，好让两种形态共用同一套判据。
	auto CheckVerticesInsideRegions = [this](const FPlanetGeologyField& Field,
		int32 CellsPerFeature)
	{
		const PlanetMineralSurface::FBuildParams Params = MakeParams(Field, CellsPerFeature);
		PlanetMineralSurface::FBuildResult Result;
		PlanetMineralSurface::Build(Field, Params, Result);

		for (int32 ElementIndex = 0; ElementIndex < Result.MeshesByElement.Num(); ++ElementIndex)
		{
			const FPlanetMineralMesh& Mesh = Result.MeshesByElement[ElementIndex];
			// 空网格会被下面整块跳过 —— 那样夹具就静默失效了：`GetPods` 回归、区域算错
			// 都照样绿。宁可红。
			TestTrue(*FString::Printf(TEXT("元素 %d 出了网格（空则本测试失效）"), ElementIndex),
				!Mesh.IsEmpty());
			if (Mesh.IsEmpty())
			{
				continue;
			}

			// 该元素所有区域的并集，外扩「1 格」（spec §7.3）。格边按簇内最小特征半径算，
			// 簇里可能混着更小的区域 —— 取该元素最大特征半径当上界就够；再乘 1.5
			// 吸收三线性插值（顶点最多离区域包围盒一格）。
			TArray<FPlanetGeologyRegion> Regions;
			Field.EnumerateRegions(ElementIndex, Regions);
			TestTrue(*FString::Printf(TEXT("元素 %d 报出了区域"), ElementIndex),
				Regions.Num() > 0);

			FBox RegionBounds(ForceInit);
			double MaxRadiusCm = 0.0;
			for (const FPlanetGeologyRegion& Region : Regions)
			{
				const double RadiusCm = double(Region.CharacteristicRadius) * double(SurfaceRadiusCm);
				MaxRadiusCm = FMath::Max(MaxRadiusCm, RadiusCm);
				RegionBounds += FBox(Region.Bounds.Min * SurfaceRadiusCm,
					Region.Bounds.Max * SurfaceRadiusCm);
			}
			// 用与 Build 同一套钳制公式算格边上界，避免夹具变大后上限生效时假红。
			const double BaseCellSizeCm = 2.0 * MaxRadiusCm / double(Params.CellsPerFeature);
			const double CellSizeCm = ExpectedCellSizeCm(MaxRadiusCm,
				RegionBounds.Max - RegionBounds.Min, Params.CellsPerFeature);
			TestTrue(*FString::Printf(TEXT("元素 %d 的夹具不触发格边上限"), ElementIndex),
				CellSizeCm == BaseCellSizeCm);
			RegionBounds = RegionBounds.ExpandBy(FVector(CellSizeCm * 1.5) + FVector::OneVector);

			int32 Outside = 0;
			for (const FVector3f& Vertex : Mesh.VerticesCm)
			{
				if (!RegionBounds.IsInside(FVector(Vertex)))
				{
					++Outside;
				}
			}
			TestEqual(*FString::Printf(TEXT("元素 %d 的顶点全在区域包围盒内（越界 %d）"),
				ElementIndex, Outside), Outside, 0);
		}
	};

	// 夹具 1（囊状）：两块矿囊，区域盒与旧 GetPods 推的盒相同。
	{
		UPlanetGeologyPreset* Preset = MakePreset({
			MakeElement(TEXT("a"), 0.06f, 7),
			MakeElement(TEXT("b"), 0.05f, 8)});
		const auto Field = MakeField(*Preset);
		CheckVerticesInsideRegions(*Field, 12);
	}

	// 夹具 2（片层）：`GetPods` 恒为空 —— 检查体若退回 `GetPods`，这里必然全判越界。
	{
		UPlanetGeologyPreset* Preset = NewObject<UPlanetGeologyPreset>();
		FPlanetGeologyElementParams& Element = Preset->Elements.AddDefaulted_GetRef();
		Element.Id = TEXT("sheet");
		Element.DisplayName = TEXT("薄片");
		UPlanetGeologyLensDistribution* Lens =
			AddDistribution<UPlanetGeologyLensDistribution>(*Preset, Element);
		// 单片、无尺寸抖动：两片相距较远时并集包围盒会大到触发格边上限，与上面那条
		// 「夹具不触发格边上限」断言自相矛盾（实测两片相距约 1.47 归一化）。
		// 夹具的目的只是「让 GetPods 回归变红」—— 单缝照样满足（GetPods 对片层恒空）。
		Lens->SeamCount = 1;
		Lens->SeamLength = 0.12f;
		Lens->SeamWidth = 0.06f;
		Lens->SeamThickness = 0.02f;
		Lens->SizeJitter = 0.f;
		Lens->VeinThreshold = 0.f;

		const auto Field = MakeField(*Preset);
		CheckVerticesInsideRegions(*Field, 12);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlanetMineralSurfaceCellsPerFeatureTest,
	"StellarEra.Planet.MineralSurface.CellsPerFeature",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetMineralSurfaceCellsPerFeatureTest::RunTest(const FString& Parameters)
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlanetMineralSurfaceInvalidParamsTest,
	"StellarEra.Planet.MineralSurface.InvalidParams",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetMineralSurfaceInvalidParamsTest::RunTest(const FString& Parameters)
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
	BadParams.CellsPerFeature = 0;
	PlanetMineralSurface::Build(*Field, BadParams, Result);
	TestEqual(TEXT("CellsPerFeature 0 → 空结果"), Result.MeshesByElement.Num(), 0);

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
		// 一格的上界取「该元素最大矿囊半径 + 该元素矿囊包围盒并集」代入 Build 的
		// 同一套钳制公式（簇内最小半径 ≤ 该元素最大半径，所以这里是上界）。
		FBox ElementBounds(ForceInit);
		double MaxPodRadiusCm = 0.0;
		for (const FPlanetGeologyPod& Pod : Field->GetPods(ElementIndex))
		{
			const FVector CenterCm = Pod.Center * RadiusCm;
			const double PodRadiusCm = double(Pod.Radius) * RadiusCm;
			MaxPodRadiusCm = FMath::Max(MaxPodRadiusCm, PodRadiusCm);
			ElementBounds += FBox(CenterCm - FVector(PodRadiusCm), CenterCm + FVector(PodRadiusCm));
		}
		const double CellSizeBoundCm = ExpectedCellSizeCm(MaxPodRadiusCm,
			ElementBounds.Max - ElementBounds.Min, Params.CellsPerFeature);
		// 夹具一旦触发上限钳制，排除带会随格边变大而扩大、探测点被静默裁掉 ——
		// 测试只会变弱、不会变红。先钉死「钳制不生效」。
		const double BaseCellSizeCm = 2.0 * MaxPodRadiusCm / double(Params.CellsPerFeature);
		TestTrue(*FString::Printf(TEXT("元素 #%d 的夹具不触发格边上限"), ElementIndex),
			CellSizeBoundCm == BaseCellSizeCm);
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

	// 三元素 × 16 囊 = 48 囊：保证多簇、多元素合并与混合元素簇都被走到，
	// 否则「两囊两簇」根本验不到合并顺序与簇级并行。
	FPlanetGeologyElementParams ElementA = MakeElement(TEXT("a"), 0.050f, 11);
	ElementA.PodCount = 16;
	ElementA.PodRadiusJitter = 0.2f;
	FPlanetGeologyElementParams ElementB = MakeElement(TEXT("b"), 0.045f, 22);
	ElementB.PodCount = 16;
	ElementB.PodRadiusJitter = 0.2f;
	FPlanetGeologyElementParams ElementC = MakeElement(TEXT("c"), 0.040f, 33);
	ElementC.PodCount = 16;
	ElementC.PodRadiusJitter = 0.2f;
	UPlanetGeologyPreset* Preset = MakePreset({ElementA, ElementB, ElementC});
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
	Base.CellsPerFeature = 12;

	PlanetMineralSurface::FCacheKey Same = Base;
	TestTrue(TEXT("同值键相等"), Same == Base);

	PlanetMineralSurface::FCacheKey Resolution = Base;
	Resolution.CellsPerFeature = 24;
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlanetMineralSurfaceRadiusInvarianceTest,
	"StellarEra.Planet.MineralSurface.RadiusInvariance",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetMineralSurfaceRadiusInvarianceTest::RunTest(const FString& Parameters)
{
	using namespace PlanetMineralSurfaceTest;

	UPlanetGeologyPreset* Preset = MakePreset({
		MakeElement(TEXT("a"), 0.06f, 11),
		MakeElement(TEXT("b"), 0.05f, 22)});
	const auto Field = MakeField(*Preset);

	const PlanetMineralSurface::FBuildParams Base = MakeParams(*Field, 12);

	// --- (1) 尺度等变性：补充不变量，**不**单独证明分辨率解耦 ---
	// 矿囊存的是归一化坐标，半径翻倍后几何形状完全不变，网格应逐项相同。
	// 但任何尺度等变的实现都能过（包括整球栅格），所以本测试的名字只对它负责；
	// 「分辨率与行星直径解耦」由下面 (a) 的绝对门槛与 (b) 的外来小矿囊隔离把关。
	PlanetMineralSurface::FBuildParams Doubled = Base;
	Doubled.SurfaceRadiusCm = Base.SurfaceRadiusCm * 2.f;

	PlanetMineralSurface::FBuildResult BaseResult;
	PlanetMineralSurface::Build(*Field, Base, BaseResult);
	PlanetMineralSurface::FBuildResult DoubledResult;
	PlanetMineralSurface::Build(*Field, Doubled, DoubledResult);

	if (!TestEqual(TEXT("元素数一致"),
		DoubledResult.MeshesByElement.Num(), BaseResult.MeshesByElement.Num()))
	{
		return false;
	}

	// 先证明夹具真有几何，否则下面的「三角形数相同」会是 0 == 0 的空转。
	TestTrue(TEXT("夹具至少有元素出面"), CountNonEmptyMeshes(BaseResult) > 0);

	for (int32 ElementIndex = 0; ElementIndex < BaseResult.MeshesByElement.Num(); ++ElementIndex)
	{
		TestEqual(*FString::Printf(TEXT("元素 %d 三角形索引数不随半径变"), ElementIndex),
			DoubledResult.MeshesByElement[ElementIndex].Triangles.Num(),
			BaseResult.MeshesByElement[ElementIndex].Triangles.Num());
		TestEqual(*FString::Printf(TEXT("元素 %d 顶点数不随半径变"), ElementIndex),
			DoubledResult.MeshesByElement[ElementIndex].VerticesCm.Num(),
			BaseResult.MeshesByElement[ElementIndex].VerticesCm.Num());
	}

	TestEqual(TEXT("角点数完全不随半径变"), DoubledResult.GridPoints, BaseResult.GridPoints);

	// --- (a) 绝对参照：抓「退回整球栅格」 ---
	// 本夹具两簇各 15³ = 3375 角点，合计约 6750。旧「整球一个栅格」在同参数下
	// 起点就是 41³ = 68921（每元素），按 CellsPerFeature 推则约 240³ ≈ 1.4e7。
	// 20000 是绝对门槛，不依赖任何相对比较。
	AddInfo(FString::Printf(TEXT("RadiusInvariance 夹具 GridPoints = %d"), BaseResult.GridPoints));
	TestTrue(*FString::Printf(TEXT("角点数 %d < 20000（整球栅格 ≥ 68921）"), BaseResult.GridPoints),
		BaseResult.GridPoints < 20000);

	// --- (b) 外来小矿囊不影响本元素：抓「簇内最小半径 → 全局最小半径」 ---
	// 场 A：元素 0 一个 r = 0.20 的矿囊。
	// 场 B：同一个元素 0 同一个矿囊 + 元素 1 一个远离它的 r = 0.01 小矿囊。
	// 按簇定尺时两簇互不相干 ⇒ 元素 0 的网格完全相同；换成全局最小半径 ⇒
	// 格边基准被 0.01 拖小 20 倍，叠加 8M 预算钳制后实际约 16 倍（3333 → ~205 cm）
	// ⇒ 三角形数必然不等。
	FPlanetGeologyElementParams BigParams = MakeElement(TEXT("big"), 0.20f, 11);
	const auto AloneField = MakeField(*MakePreset({BigParams}));

	if (!TestEqual(TEXT("场 A 的元素数"), AloneField->NumElements(), 1)
		|| !TestEqual(TEXT("场 A 的矿囊数"), AloneField->GetPods(0).Num(), 1))
	{
		return false;
	}
	const FPlanetGeologyPod BigPod = AloneField->GetPods(0)[0];

	// 元素 1 的矿囊位置由种子决定：搜一个与 r = 0.20 矿囊 AABB 不相交的种子，
	// 读回实际 Center/Radius 自证，不依赖「应该不会碰」。
	int32 SmallSeed = 0;
	for (int32 Seed = 1; Seed <= 4096 && SmallSeed == 0; ++Seed)
	{
		const FPlanetGeologyElementParams Candidate = MakeElement(TEXT("small"), 0.01f, Seed);
		const auto ProbeField = MakeField(*MakePreset({BigParams, Candidate}));
		const TArray<FPlanetGeologyPod>& ProbePods = ProbeField->GetPods(1);
		if (ProbePods.Num() == 1 && !ArePodAabbsOverlapping(BigPod, ProbePods[0]))
		{
			SmallSeed = Seed;
		}
	}
	if (!TestTrue(TEXT("找到与 R=0.20 矿囊 AABB 不相交的 R=0.01 夹具"), SmallSeed != 0))
	{
		return false;
	}

	const FPlanetGeologyElementParams SmallParams = MakeElement(TEXT("small"), 0.01f, SmallSeed);
	const auto BothField = MakeField(*MakePreset({BigParams, SmallParams}));
	if (!TestEqual(TEXT("场 B 的元素数"), BothField->NumElements(), 2)
		|| !TestEqual(TEXT("场 B 的元素 0 矿囊数"), BothField->GetPods(0).Num(), 1)
		|| !TestEqual(TEXT("场 B 的元素 1 矿囊数"), BothField->GetPods(1).Num(), 1))
	{
		return false;
	}
	const FPlanetGeologyPod& BothBig = BothField->GetPods(0)[0];
	const FPlanetGeologyPod& BothSmall = BothField->GetPods(1)[0];
	TestTrue(TEXT("场 B 的元素 0 矿囊与场 A 完全相同"),
		BothBig.Center.Equals(BigPod.Center, 0.0) && BothBig.Radius == BigPod.Radius);
	TestFalse(TEXT("夹具的两矿囊 AABB 确实不相交"),
		ArePodAabbsOverlapping(BothBig, BothSmall));

	PlanetMineralSurface::FBuildResult AloneResult;
	PlanetMineralSurface::Build(*AloneField, Base, AloneResult);
	PlanetMineralSurface::FBuildResult BothResult;
	PlanetMineralSurface::Build(*BothField, Base, BothResult);

	if (!TestTrue(TEXT("R=0.20 的大矿囊确实出面"),
		AloneResult.MeshesByElement.IsValidIndex(0)
		&& AloneResult.MeshesByElement[0].Triangles.Num() > 0)
		|| !TestEqual(TEXT("场 B 的元素网格数"), BothResult.MeshesByElement.Num(), 2))
	{
		return false;
	}

	TestEqual(TEXT("外来小矿囊不改变本元素三角形数"),
		BothResult.MeshesByElement[0].Triangles.Num(),
		AloneResult.MeshesByElement[0].Triangles.Num());
	TestEqual(TEXT("外来小矿囊不改变本元素占据格数"),
		BothResult.MeshesByElement[0].OccupiedCells,
		AloneResult.MeshesByElement[0].OccupiedCells);
	return true;
}

// 分簇合并是 spec §4.1 的硬性要求 2：同元素的重叠矿囊必须并成一簇，否则重叠处会留下
// 两张互穿的面。矿囊位置由种子决定，所以先搜夹具再断言 —— 两条断言互为对照，
// 缺了「相距很远 = 2 簇」那条，把实现改成「永远一簇」也能让「相交 = 1 簇」通过。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlanetMineralSurfaceClusterMergeTest,
	"StellarEra.Minerals.MineralSurface.ClusterMerge",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetMineralSurfaceClusterMergeTest::RunTest(const FString& Parameters)
{
	using namespace PlanetMineralSurfaceTest;

	// 半径 0.2（不抖动）：相交的 AABB 足够常见，搜索几颗种子就能同时拿到两种夹具。
	const float PodRadius = 0.20f;
	FPlanetGeologyElementParams Params = MakeElement(TEXT("a"), PodRadius, 0);
	Params.PodCount = 2;

	int32 OverlapSeed = 0;
	int32 FarSeed = 0;
	for (int32 Seed = 1; Seed <= 4096 && (OverlapSeed == 0 || FarSeed == 0); ++Seed)
	{
		Params.Seed = Seed;
		const auto ProbeField = MakeField(*MakePreset({Params}));
		const TArray<FPlanetGeologyPod>& Pods = ProbeField->GetPods(0);
		if (Pods.Num() != 2)
		{
			continue;
		}

		const bool bOverlap = ArePodAabbsOverlapping(Pods[0], Pods[1]);
		if (bOverlap)
		{
			OverlapSeed = OverlapSeed != 0 ? OverlapSeed : Seed;
			continue;
		}

		// 「相距很远」：球体本身也不相交，且中心距远大于半径和 —— 防止把
		// 「AABB 刚好擦边不交」当成第二簇的证据。
		const double CenterDistance = (Pods[0].Center - Pods[1].Center).Size();
		const double RadiusSum = double(Pods[0].Radius) + double(Pods[1].Radius);
		if (FarSeed == 0 && CenterDistance > 2.0 * RadiusSum)
		{
			FarSeed = Seed;
		}
	}

	if (!TestTrue(TEXT("找到 AABB 相交的两矿囊夹具"), OverlapSeed != 0)
		|| !TestTrue(TEXT("找到相距很远的两矿囊夹具"), FarSeed != 0))
	{
		return false;
	}
	AddInfo(FString::Printf(TEXT("相交种子 %d，远离种子 %d"), OverlapSeed, FarSeed));

	struct FCase
	{
		const TCHAR* Label;
		int32 Seed;
		bool bExpectOverlap;
		int32 ExpectedClusters;
	};
	const FCase Cases[] = {
		{ TEXT("AABB 相交"), OverlapSeed, true, 1 },
		{ TEXT("相距很远"), FarSeed, false, 2 },
	};

	for (const FCase& Case : Cases)
	{
		Params.Seed = Case.Seed;
		const auto Field = MakeField(*MakePreset({Params}));
		const TArray<FPlanetGeologyPod>& Pods = Field->GetPods(0);
		if (!TestEqual(*FString::Printf(TEXT("%s：夹具是两个矿囊"), Case.Label), Pods.Num(), 2))
		{
			return false;
		}

		// 夹具守卫：按 Build 的判据重新读回，别只信搜索阶段的结论。
		const bool bOverlap = ArePodAabbsOverlapping(Pods[0], Pods[1]);
		if (!TestEqual(*FString::Printf(TEXT("%s：判据与夹具一致"), Case.Label),
			bOverlap ? 1 : 0, Case.bExpectOverlap ? 1 : 0))
		{
			return false;
		}

		PlanetMineralSurface::FBuildResult Result;
		PlanetMineralSurface::Build(*Field, MakeParams(*Field, 8), Result);

		if (!TestEqual(*FString::Printf(TEXT("%s：网格数等于元素数"), Case.Label),
			Result.MeshesByElement.Num(), 1)
			|| !TestEqual(*FString::Printf(TEXT("%s：簇数组与网格对齐"), Case.Label),
				Result.ClustersByElement.Num(), Result.MeshesByElement.Num()))
		{
			return false;
		}
		TestEqual(*FString::Printf(TEXT("%s：元素 0 的簇数"), Case.Label),
			Result.ClustersByElement[0], Case.ExpectedClusters);
	}
	return true;
}

// 钳制的冒烟测试：**单个元素、单片薄透镜**。
// 片层宽而薄（0.30 × 0.30 × 0.002）⇒ 基准格边由 200 cm 的半厚定（约 33 cm），
// 簇盒却由 0.6 的长轴定 ⇒ 两条硬上限（每轴 512 格 / 单任务 8e6 角点）都会触发。
//
// 分簇改成按元素之后，跨元素的「大盒子 + 极小特征」再也不能合成一个任务（那正是
// 慢的根因，见 ElementIsolation 测试），所以夹具也改成单元素单片，直接指着钳制本身。
// 断言的是「不崩、不丢几何、落在上限内」，不是等值面精度。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlanetMineralSurfaceClampSmokeTest,
	"StellarEra.Minerals.MineralSurface.ClampSmoke",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetMineralSurfaceClampSmokeTest::RunTest(const FString& Parameters)
{
	using namespace PlanetMineralSurfaceTest;

	constexpr int32 CellsPerFeature = 12;
	constexpr int64 MaxTotalCorners = 8000000;

	const FPlanetGeologyElementParams Sheet = MakeElement(TEXT("sheet"), 0.002f, 11);
	UPlanetGeologyPreset* Preset = MakePreset({Sheet});
	// MakePreset 按值收参数，生成的必须是预设里那个元素 —— 挂在副本上等于没挂。
	FPlanetGeologyElementParams& Element = Preset->Elements[0];
	UPlanetGeologyLensDistribution* Lens =
		AddDistribution<UPlanetGeologyLensDistribution>(*Preset, Element);
	Lens->SeamCount = 1;
	Lens->SeamLength = 0.30f;
	Lens->SeamWidth = 0.30f;
	Lens->SeamThickness = 0.002f;
	// 抖动与扭曲都关掉：区域包围盒要能被测试自己算出来，否则断言只能靠抄实现。
	Lens->SizeJitter = 0.f;
	Lens->TiltJitterDeg = 0.f;
	Lens->WarpAmplitude = 0.f;
	Lens->DepthBias = 0.f;

	const auto Field = MakeField(*Preset);

	TArray<FPlanetGeologyRegion> Regions;
	Field->EnumerateRegions(0, Regions);
	if (!TestEqual(TEXT("单片透镜报出一个区域"), Regions.Num(), 1))
	{
		return false;
	}

	const double RadiusCm = double(SurfaceRadiusCm);
	const FBox Union(Regions[0].Bounds.Min * RadiusCm, Regions[0].Bounds.Max * RadiusCm);
	const FVector RawExtentCm = Union.Max - Union.Min;
	const double MinRadiusCm = double(Regions[0].CharacteristicRadius) * RadiusCm;
	const double BaseCellSizeCm = 2.0 * MinRadiusCm / double(CellsPerFeature);
	const double CellSizeCm = ExpectedCellSizeCm(MinRadiusCm, RawExtentCm, CellsPerFeature);

	// 夹具守卫：钳制必须真的改变格边，否则本测试只是在跑一张小网格。
	if (!TestTrue(*FString::Printf(TEXT("夹具触发格边钳制（%.1f → %.1f cm）"),
		BaseCellSizeCm, CellSizeCm), CellSizeCm > BaseCellSizeCm * 2.0))
	{
		return false;
	}

	// 用与 Build 同一套取整账重算每轴格数 / 角点数，先证明公式给出的上界真的闭合。
	int64 CornerProduct = 1;
	for (int32 Axis = 0; Axis < 3; ++Axis)
	{
		const double ExtentCm = RawExtentCm[Axis] + 2.0 * CellSizeCm;
		const int32 CellCount = FMath::Max(1, FMath::CeilToInt(ExtentCm / CellSizeCm));
		CornerProduct *= int64(CellCount + 1);
		TestTrue(*FString::Printf(TEXT("轴 %d 格数 %d ≤ 512"), Axis, CellCount),
			CellCount <= 512);
	}
	TestTrue(*FString::Printf(TEXT("单任务角点数 %lld ≤ %lld"),
		CornerProduct, MaxTotalCorners), CornerProduct <= MaxTotalCorners);

	PlanetMineralSurface::FBuildResult Result;
	PlanetMineralSurface::Build(*Field, MakeParams(*Field, CellsPerFeature), Result);
	AddInfo(FString::Printf(TEXT("格边 %.1f → %.1f cm，%lld 个角点，GridPoints = %d"),
		BaseCellSizeCm, CellSizeCm, CornerProduct, Result.GridPoints));

	if (!TestEqual(TEXT("网格数等于元素数"), Result.MeshesByElement.Num(), 1)
		|| !TestEqual(TEXT("簇数组与网格对齐"),
			Result.ClustersByElement.Num(), Result.MeshesByElement.Num()))
	{
		return false;
	}
	TestTrue(TEXT("钳制后仍有网格（不跳簇）"), !Result.MeshesByElement[0].IsEmpty());

	// 诊断字段与钳制本身对得上：一个（元素, 簇）任务，且它撞了上限。
	TestEqual(TEXT("单片单元素 = 一个任务"), Result.PairCount, 1);
	TestEqual(TEXT("这个任务撞了角点上限"), Result.CappedPairs, 1);
	TestEqual(TEXT("元素 0 一个簇"), Result.ClustersByElement[0], 1);
	TestEqual(TEXT("每元素角点之和等于总数"), Result.CornersByElement[0], Result.GridPoints);

	// 单片透镜的区域包围盒**就是**它自己那一簇的盒子，于是块掩码铺满整盒、
	// 没有一块可跳：求值角点数应当**恰好**等于栅格盒子的角点数。
	// 这条同时钉住了块掩码的覆盖算术 —— 漏写会产生小于，重复写会产生大于。
	TestTrue(*FString::Printf(TEXT("没有可跳的块：求值角点 %d == 整盒角点 %lld"),
		Result.GridPoints, CornerProduct), int64(Result.GridPoints) == CornerProduct);
	return true;
}

// 分簇按元素之后，一个元素的网格与其它元素**互不影响**。这条测试直接量的是原来的
// 根因：跨元素分簇会让「一个元素的包围盒」去背「另一个元素的空域」，还会让簇内最小
// 特征半径（例如铀颗粒 0.0015）决定整簇格边 —— 两者叠加把每个任务都顶到 8e6 角点上限。
//
// 夹具刻意选**重叠**的一对：元素 0 一片大薄透镜（盒子大、半厚小），元素 1 一颗落进
// 它盒子里的极小矿囊。按元素分簇时两者各算各的，角点数必须可加、元素 0 的网格必须
// 与「只有元素 0」时逐顶点相同；换回跨元素分簇，格边会被 0.001 拖细 20 倍，两条都挂。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlanetMineralSurfaceElementIsolationTest,
	"StellarEra.Minerals.MineralSurface.ElementIsolation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetMineralSurfaceElementIsolationTest::RunTest(const FString& Parameters)
{
	using namespace PlanetMineralSurfaceTest;

	// 元素 0：单片薄透镜，盒大、特征半径小 —— 正是被「别人家的空域」拖慢的那种几何。
	const FPlanetGeologyElementParams Sheet = MakeElement(TEXT("sheet"), 0.002f, 11);
	UPlanetGeologyPreset* SheetPreset = MakePreset({Sheet});
	FPlanetGeologyElementParams& SheetElement = SheetPreset->Elements[0];
	UPlanetGeologyLensDistribution* Lens =
		AddDistribution<UPlanetGeologyLensDistribution>(*SheetPreset, SheetElement);
	Lens->SeamCount = 1;
	Lens->SeamLength = 0.20f;
	Lens->SeamWidth = 0.20f;
	Lens->SeamThickness = 0.002f;
	Lens->SizeJitter = 0.f;
	Lens->TiltJitterDeg = 0.f;
	Lens->WarpAmplitude = 0.f;
	Lens->DepthBias = 0.f;

	const auto SheetField = MakeField(*SheetPreset);
	TArray<FPlanetGeologyRegion> SheetRegions;
	SheetField->EnumerateRegions(0, SheetRegions);
	if (!TestEqual(TEXT("单片透镜报出一个区域"), SheetRegions.Num(), 1))
	{
		return false;
	}
	const FBox SheetBoundsCm(SheetRegions[0].Bounds.Min * double(SurfaceRadiusCm),
		SheetRegions[0].Bounds.Max * double(SurfaceRadiusCm));

	// 元素 1：极小矿囊，位置由种子定。搜一个**落进**片层盒子里的（这正是旧实现会把
	// 两者并成同一簇、并让 0.002 的格边被 0.001 再拖细一倍的夹具）。
	//
	// 复制进新预设的必须是 `SheetPreset->Elements[0]`，不是上面的 `Sheet`：生成器挂在
	// 预设里的那个元素上（`AddDistribution` 同时把 PodCount 归零），`Sheet` 只是造预设
	// 时用的参数副本，仍带着 PodCount = 1 —— 照抄过去会走遗留兜底多长一颗囊，
	// 合建的元素 0 就不是片层了。
	FPlanetGeologyElementParams Speck = MakeElement(TEXT("speck"), 0.001f, 0);
	int32 SpeckSeed = 0;
	for (int32 Seed = 1; Seed <= 4096 && SpeckSeed == 0; ++Seed)
	{
		Speck.Seed = Seed;
		const auto ProbeField = MakeField(*MakePreset({SheetPreset->Elements[0], Speck}));
		const TArray<FPlanetGeologyPod>& SpeckPods = ProbeField->GetPods(1);
		if (SpeckPods.Num() != 1)
		{
			continue;
		}
		const FBox SpeckBoundsCm = PodAabbCm(SpeckPods[0], double(SurfaceRadiusCm));
		if (BoundsOverlap(SheetBoundsCm, SpeckBoundsCm))
		{
			SpeckSeed = Seed;
		}
	}
	if (!TestTrue(TEXT("找到落进片层包围盒里的极小矿囊夹具"), SpeckSeed != 0))
	{
		return false;
	}
	Speck.Seed = SpeckSeed;

	const auto SpeckField = MakeField(*MakePreset({Speck}));
	const auto BothField = MakeField(*MakePreset({SheetPreset->Elements[0], Speck}));

	const PlanetMineralSurface::FBuildParams Params = MakeParams(*BothField, 12);

	PlanetMineralSurface::FBuildResult SheetOnly;
	PlanetMineralSurface::Build(*SheetField, Params, SheetOnly);
	PlanetMineralSurface::FBuildResult SpeckOnly;
	PlanetMineralSurface::Build(*SpeckField, Params, SpeckOnly);
	PlanetMineralSurface::FBuildResult Both;
	PlanetMineralSurface::Build(*BothField, Params, Both);

	if (!TestEqual(TEXT("单建：元素数"), SheetOnly.MeshesByElement.Num(), 1)
		|| !TestEqual(TEXT("合建：元素数"), Both.MeshesByElement.Num(), 2)
		|| !TestEqual(TEXT("合建：元素 0 一个簇"), Both.ClustersByElement[0], 1)
		|| !TestEqual(TEXT("合建：元素 1 一个簇"), Both.ClustersByElement[1], 1))
	{
		return false;
	}
	// 夹具守卫：两个元素都得真的出面，否则下面的「相同」会是 0 == 0 的空转。
	if (!TestTrue(TEXT("片层的网格非空"), !SheetOnly.MeshesByElement[0].IsEmpty())
		|| !TestTrue(TEXT("极小结点的网格非空"), !SpeckOnly.MeshesByElement[0].IsEmpty()))
	{
		return false;
	}

	AddInfo(FString::Printf(TEXT("角点：片层 %d + 极小 %d = %d，合建 %d；任务数 %d"),
		SheetOnly.GridPoints, SpeckOnly.GridPoints,
		SheetOnly.GridPoints + SpeckOnly.GridPoints, Both.GridPoints, Both.PairCount));

	// ① 可加性：按元素分簇时，合建的求值角点数**恰好**等于两者单建之和。
	TestTrue(*FString::Printf(TEXT("角点可加（%d + %d == %d）"),
		SheetOnly.GridPoints, SpeckOnly.GridPoints, Both.GridPoints),
		int64(Both.GridPoints) == int64(SheetOnly.GridPoints) + int64(SpeckOnly.GridPoints));
	TestEqual(TEXT("一个元素一个任务"), Both.PairCount, 2);

	// ② 元素 0 的网格与单建逐顶点相同：外来元素既不改它的格边，也不改它的角点。
	const FPlanetMineralMesh& SheetAlone = SheetOnly.MeshesByElement[0];
	const FPlanetMineralMesh& SheetBoth = Both.MeshesByElement[0];
	if (!TestEqual(TEXT("元素 0 顶点数"), SheetBoth.VerticesCm.Num(), SheetAlone.VerticesCm.Num())
		|| !TestEqual(TEXT("元素 0 三角形索引数"), SheetBoth.Triangles.Num(), SheetAlone.Triangles.Num()))
	{
		// 数量不同时逐顶点比较没有意义，直接判失败。
		return false;
	}

	int32 VertexMismatches = 0;
	for (int32 Index = 0; Index < SheetAlone.VerticesCm.Num(); ++Index)
	{
		// 逐位相同才是「不影响」：格边、盒子、块掩码只要变一点，顶点就会挪。
		VertexMismatches += SheetBoth.VerticesCm[Index].Equals(SheetAlone.VerticesCm[Index], 0.f)
			? 0 : 1;
	}
	TestEqual(*FString::Printf(TEXT("元素 0 顶点逐位相同（不同 %d 个）"), VertexMismatches),
		VertexMismatches, 0);

	int32 IndexMismatches = 0;
	for (int32 Index = 0; Index < SheetAlone.Triangles.Num(); ++Index)
	{
		IndexMismatches += SheetBoth.Triangles[Index] == SheetAlone.Triangles[Index] ? 0 : 1;
	}
	TestEqual(*FString::Printf(TEXT("元素 0 索引逐项相同（不同 %d 个）"), IndexMismatches),
		IndexMismatches, 0);
	return true;
}

// 稀疏块只少算、不改值：块外恒为 0（判据是每个分布的零支持域 ⊆ 它报出的 Region.Bounds），
// 所以「每个角点都求值」与「跳过空块」必须给出逐顶点、逐索引相同的网格。
// `bSampleEveryCorner` 只给测试用，生产路径恒为稀疏。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlanetMineralSurfaceSparseMatchesDenseTest,
	"StellarEra.Minerals.MineralSurface.SparseMatchesDense",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetMineralSurfaceSparseMatchesDenseTest::RunTest(const FString& Parameters)
{
	using namespace PlanetMineralSurfaceTest;

	// 一条中等长度的脉：簇盒远大于管截面，稀疏剪枝才有东西可跳。
	FPlanetGeologyElementParams Element = MakeElement(TEXT("vein"), 0.02f, 7);
	UPlanetGeologyPreset* Preset = MakePreset({Element});
	FPlanetGeologyElementParams& LiveElement = Preset->Elements[0];
	UPlanetGeologyVeinDistribution* Vein =
		AddDistribution<UPlanetGeologyVeinDistribution>(*Preset, LiveElement);
	Vein->VeinCount = 1;
	Vein->VeinLength = 0.25f;
	Vein->VeinRadius = 0.02f;
	Vein->RadiusJitter = 0.f;
	Vein->BranchCount = 0;
	Vein->DepthBias = 0.f;

	const auto Field = MakeField(*Preset);

	PlanetMineralSurface::FBuildParams SparseParams = MakeParams(*Field, 8);
	PlanetMineralSurface::FBuildParams DenseParams = SparseParams;
	DenseParams.bSampleEveryCorner = true;

	PlanetMineralSurface::FBuildResult Sparse;
	PlanetMineralSurface::Build(*Field, SparseParams, Sparse);
	PlanetMineralSurface::FBuildResult Dense;
	PlanetMineralSurface::Build(*Field, DenseParams, Dense);

	if (!TestEqual(TEXT("网格数等于元素数"), Sparse.MeshesByElement.Num(), 1)
		|| !TestEqual(TEXT("密集路径的元素数"), Dense.MeshesByElement.Num(), 1))
	{
		return false;
	}
	if (!TestTrue(TEXT("夹具非空（稀疏路径出面）"), !Sparse.MeshesByElement[0].IsEmpty()))
	{
		return false;
	}

	AddInfo(FString::Printf(TEXT("求值角点：稀疏 %d vs 密集 %d（省 %d）"),
		Sparse.GridPoints, Dense.GridPoints, Dense.GridPoints - Sparse.GridPoints));

	// 稀疏必须真的少算，否则这条测试对「剪枝被关掉」也会放绿。
	TestTrue(*FString::Printf(TEXT("稀疏确实少算了（%d < %d）"),
		Sparse.GridPoints, Dense.GridPoints), Sparse.GridPoints < Dense.GridPoints);
	// 密集路径的标志：单个任务、单元素时它就是整盒角点数，等于每元素角点统计。
	TestEqual(TEXT("密集路径每元素角点等于总数"), Dense.CornersByElement[0], Dense.GridPoints);

	const FPlanetMineralMesh& SparseMesh = Sparse.MeshesByElement[0];
	const FPlanetMineralMesh& DenseMesh = Dense.MeshesByElement[0];
	if (!TestEqual(TEXT("顶点数相同"), SparseMesh.VerticesCm.Num(), DenseMesh.VerticesCm.Num())
		|| !TestEqual(TEXT("三角形索引数相同"), SparseMesh.Triangles.Num(), DenseMesh.Triangles.Num())
		|| !TestEqual(TEXT("占据格数相同"), SparseMesh.OccupiedCells, DenseMesh.OccupiedCells))
	{
		return false;
	}

	int32 VertexMismatches = 0;
	for (int32 Index = 0; Index < DenseMesh.VerticesCm.Num(); ++Index)
	{
		VertexMismatches += SparseMesh.VerticesCm[Index].Equals(DenseMesh.VerticesCm[Index], 0.f)
			? 0 : 1;
	}
	TestEqual(*FString::Printf(TEXT("顶点逐位相同（不同 %d 个）"), VertexMismatches),
		VertexMismatches, 0);

	int32 IndexMismatches = 0;
	for (int32 Index = 0; Index < DenseMesh.Triangles.Num(); ++Index)
	{
		IndexMismatches += SparseMesh.Triangles[Index] == DenseMesh.Triangles[Index] ? 0 : 1;
	}
	TestEqual(*FString::Printf(TEXT("索引逐项相同（不同 %d 个）"), IndexMismatches),
		IndexMismatches, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlanetMineralSurfaceRegionClusteringTest,
	"StellarEra.Planet.MineralSurface.RegionClustering",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetMineralSurfaceRegionClusteringTest::RunTest(const FString& Parameters)
{
	using namespace PlanetMineralSurfaceTest;

	// 退化输入：空数组、单盒、零尺寸同点盒（CellSize 退化为 0）。零尺寸时函数早退成
	// 「各自成簇」而不是并成一簇 —— 没有格边就无法证明两盒相交，宁可少合。
	{
		TArray<int32> Roots;
		PlanetMineralSurface::ClusterRegionBounds(TArray<FBox>(), Roots);
		TestEqual(TEXT("空输入返回空数组"), Roots.Num(), 0);
	}
	{
		TArray<FBox> Single;
		Single.Add(FBox(FVector(0.0), FVector(1.0)));
		TArray<int32> Roots;
		PlanetMineralSurface::ClusterRegionBounds(Single, Roots);
		if (TestEqual(TEXT("单盒代表数"), Roots.Num(), 1))
		{
			TestEqual(TEXT("单盒代表是自己"), Roots[0], 0);
		}
	}
	{
		TArray<FBox> Degenerate;
		for (int32 Index = 0; Index < 4; ++Index)
		{
			Degenerate.Add(FBox(FVector(0.5), FVector(0.5)));
		}
		TArray<int32> Roots;
		PlanetMineralSurface::ClusterRegionBounds(Degenerate, Roots);
		if (TestEqual(TEXT("零尺寸盒代表数"), Roots.Num(), 4))
		{
			int32 Merged = 0;
			for (int32 Index = 0; Index < Roots.Num(); ++Index)
			{
				Merged += Roots[Index] == Index ? 0 : 1;
			}
			TestEqual(*FString::Printf(TEXT("零尺寸盒各自成簇（被并 %d 个）"), Merged), Merged, 0);
		}
	}
	// 8192 个互不相交的盒子：只验正确性。自动化没有逐测试超时，退化成 O(N²)
	// （约 6700 万次比较）不会变红、只是慢 —— 别把这条当性能守卫。
	{
		TArray<FBox> Many;
		Many.Reserve(8192);
		for (int32 Index = 0; Index < 8192; ++Index)
		{
			const double X = double(Index) * 4.0;
			Many.Add(FBox(FVector(X), FVector(X + 1.0)));
		}
		TArray<int32> Roots;
		PlanetMineralSurface::ClusterRegionBounds(Many, Roots);
		if (TestEqual(TEXT("8192 盒代表数"), Roots.Num(), 8192))
		{
			int32 Wrong = 0;
			for (int32 Index = 0; Index < Roots.Num(); ++Index)
			{
				Wrong += Roots[Index] == Index ? 0 : 1;
			}
			TestEqual(*FString::Printf(TEXT("互不相交的盒各自成簇（错 %d）"), Wrong), Wrong, 0);
		}
	}

	FRandomStream Rng(123456);
	TArray<FBox> Bounds;
	for (int32 Index = 0; Index < 400; ++Index)
	{
		const FVector Center(Rng.FRandRange(-1000.f, 1000.f), Rng.FRandRange(-1000.f, 1000.f),
			Rng.FRandRange(-1000.f, 1000.f));
		const FVector Extent(Rng.FRandRange(1.f, 60.f), Rng.FRandRange(1.f, 60.f),
			Rng.FRandRange(1.f, 60.f));
		Bounds.Add(FBox(Center - Extent, Center + Extent));
	}
	// 三连相交：保证夹具里真的有需要传递合并的簇，不全是 400 个单点簇。
	Bounds.Add(FBox(FVector(0, 0, 0), FVector(10, 10, 10)));
	Bounds.Add(FBox(FVector(5, 5, 5), FVector(15, 15, 15)));
	Bounds.Add(FBox(FVector(12, 12, 12), FVector(20, 20, 20)));

	TArray<int32> ActualRoots;
	PlanetMineralSurface::ClusterRegionBounds(Bounds, ActualRoots);
	if (!TestEqual(TEXT("根数组长度 == 区域数"), ActualRoots.Num(), Bounds.Num()))
	{
		return false;
	}

	// O(N²) 参照实现：判据与 Build 相同（AABB 相交，含边界）。
	TArray<int32> Parents;
	Parents.SetNum(Bounds.Num());
	for (int32 Index = 0; Index < Parents.Num(); ++Index)
	{
		Parents[Index] = Index;
	}
	auto Find = [&Parents](int32 Index)
	{
		while (Parents[Index] != Index)
		{
			Parents[Index] = Parents[Parents[Index]];
			Index = Parents[Index];
		}
		return Index;
	};
	for (int32 A = 0; A < Bounds.Num(); ++A)
	{
		for (int32 B = A + 1; B < Bounds.Num(); ++B)
		{
			if (BoundsOverlap(Bounds[A], Bounds[B]))
			{
				const int32 RootA = Find(A);
				const int32 RootB = Find(B);
				if (RootA != RootB)
				{
					Parents[RootB] = RootA;
				}
			}
		}
	}

	TMap<int32, int32> MinIndexByRoot;
	for (int32 Index = 0; Index < Bounds.Num(); ++Index)
	{
		const int32 Root = Find(Index);
		int32& MinIndex = MinIndexByRoot.FindOrAdd(Root, Index);
		MinIndex = FMath::Min(MinIndex, Index);
	}
	TArray<int32> ReferenceRoots;
	ReferenceRoots.SetNum(Bounds.Num());
	for (int32 Index = 0; Index < Bounds.Num(); ++Index)
	{
		ReferenceRoots[Index] = MinIndexByRoot.FindChecked(Find(Index));
	}

	int32 Mismatches = 0;
	for (int32 Index = 0; Index < Bounds.Num(); ++Index)
	{
		Mismatches += ReferenceRoots[Index] == ActualRoots[Index] ? 0 : 1;
	}
	TestEqual(TEXT("空间哈希分簇 == O(N²) 参照"), Mismatches, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlanetMineralSurfaceClosedMeshAllFormsTest,
	"StellarEra.Planet.MineralSurface.ClosedMesh.AllForms",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetMineralSurfaceClosedMeshAllFormsTest::RunTest(const FString& Parameters)
{
	using namespace PlanetMineralSurfaceTest;

	UPlanetGeologyPreset* Preset = NewObject<UPlanetGeologyPreset>();
	Preset->Appearance.ColorThreshold = 0.05f;
	Preset->Appearance.FadeRange = 0.12f;
	Preset->Appearance.PurityForFullColor = 0.45f;

	FPlanetGeologyElementParams& LensElement = Preset->Elements.AddDefaulted_GetRef();
	LensElement.Id = TEXT("lens");
	LensElement.DisplayName = TEXT("片层");
	UPlanetGeologyLensDistribution* Lens =
		AddDistribution<UPlanetGeologyLensDistribution>(*Preset, LensElement);
	Lens->SeamCount = 4;
	Lens->SeamLength = 0.12f;
	Lens->SeamWidth = 0.06f;
	Lens->SeamThickness = 0.01f;
	Lens->SizeJitter = 0.f;
	Lens->DepthBias = 0.f;

	FPlanetGeologyElementParams& VeinElement = Preset->Elements.AddDefaulted_GetRef();
	VeinElement.Id = TEXT("vein");
	VeinElement.DisplayName = TEXT("脉");
	UPlanetGeologyVeinDistribution* Vein =
		AddDistribution<UPlanetGeologyVeinDistribution>(*Preset, VeinElement);
	Vein->VeinCount = 2;
	Vein->VeinLength = 0.3f;
	Vein->VeinRadius = 0.01f;
	Vein->RadiusJitter = 0.f;
	Vein->BranchCount = 0;
	Vein->DepthBias = 0.f;

	FPlanetGeologyElementParams& GrainElement = Preset->Elements.AddDefaulted_GetRef();
	GrainElement.Id = TEXT("grain");
	GrainElement.DisplayName = TEXT("浸染");
	UPlanetGeologyDisseminatedDistribution* Disseminated =
		AddDistribution<UPlanetGeologyDisseminatedDistribution>(*Preset, GrainElement);
	Disseminated->GrainCount = 24;
	Disseminated->GrainRadius = 0.006f;
	Disseminated->RadiusJitter = 0.f;
	Disseminated->DepthBias = 0.f;

	const auto Field = MakeField(*Preset);
	PlanetMineralSurface::FBuildResult Result;
	PlanetMineralSurface::Build(*Field, MakeParams(*Field, 12), Result);

	if (!TestEqual(TEXT("三个元素各一块网格"), Result.MeshesByElement.Num(), 3))
	{
		return false;
	}
	TestEqual(TEXT("三种形态都出了面"), CountNonEmptyMeshes(Result), 3);

	for (int32 ElementIndex = 0; ElementIndex < Result.MeshesByElement.Num(); ++ElementIndex)
	{
		const FPlanetMineralMesh& Mesh = Result.MeshesByElement[ElementIndex];
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
		TestEqual(*FString::Printf(TEXT("元素 %d（形态夹具）网格封闭（开放边 %d）"),
			ElementIndex, OpenEdges), OpenEdges, 0);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlanetMineralSurfaceGridCapTest,
	"StellarEra.Planet.MineralSurface.GridCap",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetMineralSurfaceGridCapTest::RunTest(const FString& Parameters)
{
	using namespace PlanetMineralSurfaceTest;

	UPlanetGeologyPreset* Preset = NewObject<UPlanetGeologyPreset>();
	Preset->Appearance.ColorThreshold = 0.05f;
	Preset->Appearance.FadeRange = 0.12f;
	Preset->Appearance.PurityForFullColor = 0.45f;

	FPlanetGeologyElementParams& Element = Preset->Elements.AddDefaulted_GetRef();
	Element.Id = TEXT("longvein");
	Element.DisplayName = TEXT("长脉");
	// 半径取得比默认小：基准格边 ∝ 半径，未钳制角点 ∝ (1/半径)²，而钳制后的角点只由
	// 包围盒定 —— 半径越小，「未钳制 / 钳制后」的比值越大，守卫的鉴别力越强。
	//
	// 别指望「512 格/轴」那条：它在本实现里恒不生效（两条上限共用被除数，除数
	// 197 < 510，轴上那条恒被支配）；`VeinLength = 1.0` 也长不到整个球
	// （`GrowChain` 在球面处停）。
	// 所以本夹具触发的是**总角点上限**，断言也按它写。
	UPlanetGeologyVeinDistribution* Vein =
		AddDistribution<UPlanetGeologyVeinDistribution>(*Preset, Element);
	Vein->VeinCount = 1;
	Vein->VeinLength = 1.0f;
	Vein->VeinRadius = 0.004f;
	Vein->RadiusJitter = 0.f;
	Vein->BranchCount = 0;
	Vein->DepthBias = 0.f;

	const auto Field = MakeField(*Preset);
	const PlanetMineralSurface::FBuildParams Params = MakeParams(*Field, 12);
	PlanetMineralSurface::FBuildResult Result;
	PlanetMineralSurface::Build(*Field, Params, Result);

	if (!TestEqual(TEXT("一个元素一块网格"), Result.MeshesByElement.Num(), 1))
	{
		return false;
	}
	TestTrue(TEXT("触发上限后仍然有网格（不跳簇）"), !Result.MeshesByElement[0].IsEmpty());
	// 下面的解析复算用「全部区域并集成一个盒子」，只有单簇时才与 Build 逐簇求和等价。
	// 夹具必须把它钉住：换成两条相距很远的脉，并集乘积会远大于各簇和，(c) 就对
	// 「两条上限都删掉」也放绿。
	TestEqual(TEXT("夹具合成单簇（解析复算的前提）"), Result.ClustersByElement[0], 1);

	// 解析复算未钳制 / 钳制后的角点数：全部从 Region 数据推，不写死数量级。
	// 判据必须挂在**上限公式**上，而不是「一个足够大的常数」—— 常数既可能被夹具
	// 悄悄满足（上一版就是这样），也可能随取整口径变化假红。
	TArray<FPlanetGeologyRegion> Regions;
	Field->EnumerateRegions(0, Regions);
	if (!TestTrue(TEXT("报出了区域"), Regions.Num() > 0))
	{
		return false;
	}

	FBox Bounds(ForceInit);
	double MinRadiusCm = TNumericLimits<double>::Max();
	for (const FPlanetGeologyRegion& Region : Regions)
	{
		// 与 `Build` 的区域过滤保持一致：零特征半径/非法包围盒不参与建面。
		// 不复刻的话，`CountCorners(..., 0)` 会除零，复算集合也与实现不同。
		if (Region.CharacteristicRadius <= 0.f || !Region.Bounds.IsValid)
		{
			continue;
		}
		Bounds += FBox(Region.Bounds.Min * SurfaceRadiusCm, Region.Bounds.Max * SurfaceRadiusCm);
		MinRadiusCm = FMath::Min(MinRadiusCm,
			double(Region.CharacteristicRadius) * double(SurfaceRadiusCm));
	}
	if (!TestTrue(TEXT("至少一个有效区域"), MinRadiusCm < TNumericLimits<double>::Max()))
	{
		return false;
	}
	const FVector RawExtentCm = Bounds.Max - Bounds.Min;
	const double BaseCellSizeCm = 2.0 * MinRadiusCm / double(Params.CellsPerFeature);
	const double ClampedCellSizeCm =
		ExpectedCellSizeCm(MinRadiusCm, RawExtentCm, Params.CellsPerFeature);

	auto CountCorners = [](const FVector& ExtentCm, double CellSizeCm)
	{
		int64 Product = 1;
		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			Product *= int64(FMath::CeilToInt((ExtentCm[Axis] + 2.0 * CellSizeCm) / CellSizeCm)) + 1;
		}
		return Product;
	};
	const int64 UnclampedCorners = CountCorners(RawExtentCm, BaseCellSizeCm);
	const int64 ClampedCorners = CountCorners(RawExtentCm, ClampedCellSizeCm);
	AddInfo(FString::Printf(
		TEXT("基准格边 %.5f → 钳制后 %.5f cm，未钳制角点 %lld → 钳制后 %lld，实际 GridPoints %d"),
		BaseCellSizeCm, ClampedCellSizeCm, UnclampedCorners, ClampedCorners, Result.GridPoints));

	// 前两条是**夹具前置条件**：两个操作数都由测试自己那份公式算出，与 `Build` 无关 ——
	// 实现把上限删掉它们照样绿。它们只保证「这个夹具落在能区分的量级上」。
	TestTrue(*FString::Printf(TEXT("夹具按上限公式会被钳制（格边 %.5f → %.5f cm）"),
		BaseCellSizeCm, ClampedCellSizeCm), ClampedCellSizeCm > BaseCellSizeCm * 1.2);
	TestTrue(*FString::Printf(TEXT("夹具未钳制时角点远超钳制后（%lld vs %lld）"),
		UnclampedCorners, ClampedCorners), UnclampedCorners > 2 * ClampedCorners);
	// 上面那次是**生产路径**（稀疏块）：细脉的簇盒虽然撞满 8e6 上限，真正有矿的块
	// 只占一部分，GridPoints 会明显小于盒角点数 —— 所以它能证明「剪枝在跑」，
	// 却**不能**拿来约束上限公式本身（钳制放宽了它也一样小）。这里只卡严格小于：
	// 剪枝被关掉时两者恰好相等，够区分；再写「小于几倍」就会随随机游走的长短假红。
	TestTrue(*FString::Printf(TEXT("稀疏路径确实少算了（%d < 盒角点 %lld）"),
		Result.GridPoints, ClampedCorners),
		int64(Result.GridPoints) < ClampedCorners);

	// 钳制公式的约束在密集路径上做：`bSampleEveryCorner` 让盒里每个角点都求值，
	// 实测值就应当**逐位落在**解析钳制值上。
	PlanetMineralSurface::FBuildParams DenseParams = Params;
	DenseParams.bSampleEveryCorner = true;
	PlanetMineralSurface::FBuildResult DenseResult;
	PlanetMineralSurface::Build(*Field, DenseParams, DenseResult);
	AddInfo(FString::Printf(TEXT("密集路径实测 %d 角点（解析钳制值 %lld）"),
		DenseResult.GridPoints, ClampedCorners));

	// **只有这一条约束实现**：实测值必须落在解析钳制值上（当前逐位相等）。
	// 容差取 5% 而不是 2× —— 2× 会放过「钳制变松但没删」的退化，例如把除数里的 -3
	// 去掉（格边 129.35→127.41）后仍能通过。
	//
	// 余量提醒：「去掉 -3」这档只比上界高 0.8~1.3%（实测 1,266,540~1,273,206 vs
	// 上界 1,257,043）。现在抓得住是因为取整口径完全确定 —— 改 `CountCorners` 的取整、
	// 改常量或调容差时都要重算这一档，别以为余量宽裕。
	//
	// 这条断言是单边的（只卡上界）：「上限被收得更紧」也是合法状态（GridPoints 更小、
	// 网格非空），不该假红。所以文案写「不超过」，不写「等于」。
	// 注意：TEXT 里的字面百分号要写成 `%%`，否则 UE 的 printf 格式串检查（consteval）
	// 会把「5%（」里的孤立 % 判成非法格式并编译失败；渲染后的文案仍是「5%」。
	TestTrue(*FString::Printf(TEXT("密集路径角点不超过解析钳制值的 5%%（%d vs %lld）"),
		DenseResult.GridPoints, ClampedCorners), DenseResult.GridPoints <= ClampedCorners * 1.05);
	TestTrue(TEXT("密集路径同样出面（对照有效）"), !DenseResult.MeshesByElement[0].IsEmpty());
	return true;
}

#endif
