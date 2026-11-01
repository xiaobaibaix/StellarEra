// 水面几何的回归测试。
//
// 三条用例各用一条解析地形，断言「该有的顶点有、不该有的顶点没有、半径对得上」。
// 不检查三角面的数量与形状 —— 那是观感问题，由 PIE 验收（Task 9）负责。
//
// 本文件里的匿名名字空间 helper 都带 WaterTest 前缀：unity build 会把多个
// *Tests.cpp 塞进同一个 TU，匿名名字空间随之合并，PlanetHydrologyBakeTests.cpp
// 已经有 BowlHeight / ValleyHeight 了。

#include "CoreMinimal.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "Misc/AutomationTest.h"
#include "PlanetHydrologyBake.h"
#include "PlanetHydrologyGrid.h"
#include "PlanetWaterSurfaceBuilder.h"
#include "VoxelPlanetNoiseTerrain.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr double WaterTestRadius = 1000.0;
	constexpr double WaterTestVoxelSize = 100.0;

	FPlanetHydrologyParams MakeWaterTestParams()
	{
		FPlanetHydrologyParams Params;
		Params.SeaLevel = -0.3f;
		Params.Amplitude = 8.f;
		Params.MinRiverFlow = 24.f;
		Params.MaxCarveDepth = 4.f;
		Params.Resolution = 64;
		return Params;
	}

	PlanetWaterSurfaceBuilder::FBuildParams MakeWaterBuildParams()
	{
		PlanetWaterSurfaceBuilder::FBuildParams Params;
		Params.VoxelSize = WaterTestVoxelSize;
		Params.PlanetRadiusVoxels = WaterTestRadius;
		Params.OceanResolution = 48;
		return Params;
	}

	/** 0.4 rad 以内的高台（陆地），其余全是海。 */
	float WaterTestCapHeight(const FVector& Dir)
	{
		const float Angle = FMath::Acos(FMath::Clamp(float(Dir.Z), -1.f, 1.f));
		return (Angle < 0.4f) ? 0.5f : -0.5f;
	}

	/** 与 Task 3 的碗一致：碗里蓄湖、碗外是海。 */
	float WaterTestBowlHeight(const FVector& Dir)
	{
		const float Angle = FMath::Acos(FMath::Clamp(float(Dir.Z), -1.f, 1.f));
		constexpr float BowlAngle = 0.3f;
		if (Angle > BowlAngle)
		{
			return -0.5f;
		}
		const float T = Angle / BowlAngle;
		return -0.5f + 0.45f * T * T;
	}

	/** 与 Task 3 的谷一致：谷底长河。 */
	float WaterTestValleyHeight(const FVector& Dir)
	{
		const double SafeZ = FMath::Clamp(double(Dir.Z), -1.0, 1.0);
		const double Angle = FMath::Acos(SafeZ);
		const double Lon = FMath::Atan2(double(Dir.Y), double(Dir.X));
		return float(-0.8 + 1.3 * (Angle / PI) + 0.4 * FMath::Square(Lon / PI));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraWaterOceanTest,
	"StellarEra.Planet.Water.Ocean",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraWaterOceanTest::RunTest(const FString& Parameters)
{
	const TSharedPtr<FPlanetHydrologyField> Field =
		FPlanetHydrologyBake::Bake(&WaterTestCapHeight, MakeWaterTestParams(), WaterTestRadius);
	TestTrue(TEXT("烘焙必须成功"), Field.IsValid() && Field->IsValid());
	if (!Field.IsValid() || !Field->IsValid())
	{
		return false;
	}

	UE::Geometry::FDynamicMesh3 Mesh(true, false, false, false);
	PlanetWaterSurfaceBuilder::BuildOcean(*Field, MakeWaterBuildParams(), Mesh);

	TestTrue(FString::Printf(TEXT("海洋必须有三角形（实际 %d）"), Mesh.TriangleCount()),
		Mesh.TriangleCount() > 0);
	TestTrue(TEXT("海洋必须有顶点"), Mesh.MaxVertexID() > 0);

	const double ExpectedRadiusCm = double(Field->GetSeaLevelRadius()) * WaterTestVoxelSize;
	double WorstRadiusError = 0.0;
	double NearestOnLandCap = TNumericLimits<double>::Max();

	for (const int32 VertexId : Mesh.VertexIndicesItr())
	{
		const FVector3d Position = Mesh.GetVertex(VertexId);
		const double RadiusCm = Position.Size();
		WorstRadiusError = FMath::Max(WorstRadiusError, FMath::Abs(RadiusCm - ExpectedRadiusCm));

		const FVector3d Direction = Position / FMath::Max(RadiusCm, 1e-9);

		// 陆地内部的顶点必须被裁掉。允许一格余量：跨岸的那一格会被保留下来
		// （水面探进陆地一格，海岸线由交线自然给出）。
		const double AngleFromCap = FMath::Acos(FMath::Clamp(Direction.Z, -1.0, 1.0));
		NearestOnLandCap = FMath::Min(NearestOnLandCap, AngleFromCap);
	}

	// 法线必须**绑在三角形上**，而且按三角形读。只往法线层 AppendElement 而不
	// SetTriangle 时，三角形的三个元素一直是 INDEX_NONE，渲染端回退到顶点法线
	// (0,1,0) —— 球面上等于整片水面都朝上打光；而按顶点读孤立元素仍是漂亮的
	// 径向值，正好看不见这个错。所以这里逐三角形断言 IsSetTriangle，并从三角形
	// 上取角元素比对径向方向。
	const UE::Geometry::FDynamicMeshNormalOverlay* Normals = Mesh.Attributes()->PrimaryNormals();
	int32 UnboundTriangles = 0;
	double WorstNormalDot = 1.0;
	for (const int32 TriangleId : Mesh.TriangleIndicesItr())
	{
		if (Normals == nullptr || !Normals->IsSetTriangle(TriangleId))
		{
			++UnboundTriangles;
			continue;
		}
		const UE::Geometry::FIndex3i Triangle = Mesh.GetTriangle(TriangleId);
		for (int32 Corner = 0; Corner < 3; ++Corner)
		{
			const int32 VertexId = Triangle[Corner];
			const FVector3d Position = Mesh.GetVertex(VertexId);
			const FVector3d Direction = Position / FMath::Max(Position.Size(), 1e-9);
			const FVector3f Normal = Normals->GetElementAtVertex(TriangleId, VertexId);
			WorstNormalDot = FMath::Min(WorstNormalDot,
				FVector3d::DotProduct(FVector3d(Normal.X, Normal.Y, Normal.Z), Direction));
		}
	}

	AddInfo(FString::Printf(TEXT("海洋：三角形 %d，未绑定法线 %d，最差法线点积 %.6f，最近陆地顶点 %.4f rad"),
		Mesh.TriangleCount(), UnboundTriangles, WorstNormalDot, NearestOnLandCap));

	TestTrue(FString::Printf(TEXT("所有顶点必须落在海面半径上（最大误差 %.4f cm）"), WorstRadiusError),
		WorstRadiusError < 1.0);
	TestEqual(TEXT("每个三角形都必须绑定法线元素（否则渲染端回退到 (0,1,0)）"),
		UnboundTriangles, 0);
	TestTrue(FString::Printf(TEXT("法线必须朝外（最差点积 %.4f）"), WorstNormalDot),
		WorstNormalDot > 0.99);
	TestTrue(FString::Printf(TEXT("陆地内部（角距 < 0.35）不得有水面顶点（最近的 %.4f rad）"),
		NearestOnLandCap), NearestOnLandCap > 0.35);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraWaterLakeTest,
	"StellarEra.Planet.Water.Lake",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraWaterLakeTest::RunTest(const FString& Parameters)
{
	const TSharedPtr<FPlanetHydrologyField> Field =
		FPlanetHydrologyBake::Bake(&WaterTestBowlHeight, MakeWaterTestParams(), WaterTestRadius);
	if (!TestTrue(TEXT("烘焙必须成功"), Field.IsValid() && Field->IsValid()))
	{
		return false;
	}
	if (!TestEqual(TEXT("碗里必须恰好一片湖"), Field->GetLakes().Num(), 1))
	{
		return false;
	}

	// 有余量裁剪时湖冠必须跟着岸走：传地形高度回调（与地形模块的 GetRawOffset 同源）。
	PlanetWaterSurfaceBuilder::FBuildParams BuildParams = MakeWaterBuildParams();
	BuildParams.TerrainHeightFn = &WaterTestBowlHeight;
	BuildParams.TerrainAmplitude = MakeWaterTestParams().Amplitude;

	UE::Geometry::FDynamicMesh3 Mesh(true, false, false, false);
	PlanetWaterSurfaceBuilder::BuildLakes(*Field, BuildParams, Mesh);

	TestTrue(FString::Printf(TEXT("湖泊必须有三角形（实际 %d）"), Mesh.TriangleCount()),
		Mesh.TriangleCount() > 0);

	const FPlanetLakeBasin& Lake = Field->GetLakes()[0];
	const double ExpectedRadiusCm = double(Lake.WaterLevelRadius) * WaterTestVoxelSize;
	double WorstRadiusError = 0.0;
	double FarthestAngle = 0.0;

	for (const int32 VertexId : Mesh.VertexIndicesItr())
	{
		const FVector3d Position = Mesh.GetVertex(VertexId);
		const double RadiusCm = Position.Size();
		WorstRadiusError = FMath::Max(WorstRadiusError, FMath::Abs(RadiusCm - ExpectedRadiusCm));

		const FVector3d Direction = Position / FMath::Max(RadiusCm, 1e-9);
		const double Cosine = FMath::Clamp(
			FVector3d::DotProduct(Direction, FVector3d(Lake.CentroidDir)), -1.0, 1.0);
		FarthestAngle = FMath::Max(FarthestAngle, FMath::Acos(Cosine));
	}

	TestTrue(FString::Printf(TEXT("湖面顶点必须落在湖的水位上（最大误差 %.4f cm）"), WorstRadiusError),
		WorstRadiusError < 1.0);
	TestTrue(FString::Printf(TEXT("湖面不得超出湖的角半径 %.4f（实测最远 %.4f）"),
		Lake.AngularRadius, FarthestAngle),
		FarthestAngle <= double(Lake.AngularRadius) + 1e-3);

	// 湖的**真实范围**只到「烘焙的最大角距」；`AngularRadius` 在它之上又加了 2 格，
	// 那 2 格是刻意探进岸里的余量。岸外地形若低于湖的水位，水面的这圈余量就会
	// 悬在空中（碗形夹具里表现为高出海面的一圈）。所以：余量只允许出现在地形
	// 高过水位、水面埋进地里的地方。
	const double CellAngle = (PI * 0.5) / double(MakeWaterTestParams().Resolution);
	const double InnerAngle = FMath::Max(
		double(Lake.AngularRadius) - double(PlanetLakeCapMarginCells) * CellAngle, 0.0);
	int32 ExposedMarginVertices = 0;
	double FarthestExposedAngle = 0.0;
	for (const int32 VertexId : Mesh.VertexIndicesItr())
	{
		const FVector3d Position = Mesh.GetVertex(VertexId);
		const double RadiusCm = Position.Size();
		const FVector3d Direction = Position / FMath::Max(RadiusCm, 1e-9);
		const double Angle = FMath::Acos(FMath::Clamp(
			FVector3d::DotProduct(Direction, FVector3d(Lake.CentroidDir)), -1.0, 1.0));
		if (Angle <= InnerAngle + 1e-4)
		{
			continue;
		}
		const double TerrainRadius = WaterTestRadius + double(MakeWaterTestParams().Amplitude)
			* double(WaterTestBowlHeight(FVector(Direction)));
		if (TerrainRadius < double(Lake.WaterLevelRadius))
		{
			++ExposedMarginVertices;
			FarthestExposedAngle = FMath::Max(FarthestExposedAngle, Angle);
		}
	}

	// 反过来也要钉住：真实湖域（InnerAngle 以内，即烘焙的最大角距）**每个方位**都必须
	// 被水面盖到。只断言「没有悬空余量」的话，误裁到湖域内部照样绿。
	//
	// 重建构建器的环/段划分（同一套公式与同一切平面基），取「InnerAngle 以内最后
	// 一环」，要求每个方位段上的最大极角都不小于它。
	const int32 Rings = FMath::Clamp(
		FMath::CeilToInt(double(Lake.AngularRadius) / CellAngle), 2, 64);
	const int32 Segments = FMath::Clamp(Rings * 4, 8, 256);
	int32 LastInnerRing = 0;
	for (int32 Ring = 1; Ring <= Rings; ++Ring)
	{
		if (double(Lake.AngularRadius) * double(Ring) / double(Rings) <= InnerAngle + 1e-9)
		{
			LastInnerRing = Ring;
		}
	}
	const double LastInnerAngle =
		double(Lake.AngularRadius) * double(LastInnerRing) / double(Rings);

	const FVector LakeCenter = Lake.CentroidDir.GetSafeNormal();
	const FVector Reference =
		(FMath::Abs(LakeCenter.Z) < 0.9) ? FVector::UpVector : FVector::RightVector;
	const FVector LakeT1 = FVector::CrossProduct(LakeCenter, Reference).GetSafeNormal();
	const FVector LakeT2 = FVector::CrossProduct(LakeCenter, LakeT1).GetSafeNormal();

	TArray<double> MaxAnglePerSegment;
	MaxAnglePerSegment.Init(0.0, Segments);
	for (const int32 VertexId : Mesh.VertexIndicesItr())
	{
		const FVector3d Position = Mesh.GetVertex(VertexId);
		const double RadiusCm = Position.Size();
		const FVector Direction = Position / FMath::Max(RadiusCm, 1e-9);
		const double Theta = FMath::Acos(FMath::Clamp(
			FVector::DotProduct(Direction, LakeCenter), -1.0, 1.0));
		const double Phi = FMath::Atan2(
			FVector::DotProduct(Direction, LakeT2), FVector::DotProduct(Direction, LakeT1));
		int32 Segment = FMath::RoundToInt(Phi / (2.0 * PI / double(Segments)));
		Segment = ((Segment % Segments) + Segments) % Segments;
		MaxAnglePerSegment[Segment] = FMath::Max(MaxAnglePerSegment[Segment], Theta);
	}
	int32 UncoveredSegments = 0;
	for (const double SegmentMaxAngle : MaxAnglePerSegment)
	{
		if (SegmentMaxAngle < LastInnerAngle - 1e-3)
		{
			++UncoveredSegments;
		}
	}

	AddInfo(FString::Printf(
		TEXT("碗：角半径 %.4f，真实范围 %.4f，内侧最后一环 %.4f，实测最远 %.4f，未覆盖段 %d，水位 %.3f 体素"),
		Lake.AngularRadius, InnerAngle, LastInnerAngle, FarthestAngle,
		UncoveredSegments, Lake.WaterLevelRadius));
	TestEqual(FString::Printf(TEXT("湖泊余量不得悬空（最远悬空角 %.4f rad）"), FarthestExposedAngle),
		ExposedMarginVertices, 0);
	TestEqual(TEXT("真实湖域的每个方位都必须被水面覆盖"), UncoveredSegments, 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraWaterRiverTest,
	"StellarEra.Planet.Water.River",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraWaterRiverTest::RunTest(const FString& Parameters)
{
	const TSharedPtr<FPlanetHydrologyField> Field =
		FPlanetHydrologyBake::Bake(&WaterTestValleyHeight, MakeWaterTestParams(), WaterTestRadius);
	if (!TestTrue(TEXT("烘焙必须成功"), Field.IsValid() && Field->IsValid()))
	{
		return false;
	}

	UE::Geometry::FDynamicMesh3 Mesh(true, false, false, false);
	PlanetWaterSurfaceBuilder::BuildRivers(*Field, MakeWaterBuildParams(), Mesh);

	TestTrue(FString::Printf(TEXT("河网必须有三角形（实际 %d）"), Mesh.TriangleCount()),
		Mesh.TriangleCount() > 0);

	bool bAllSane = true;
	TArray<FVector3d> Positions;
	Positions.Reserve(Mesh.VertexCount());
	for (const int32 VertexId : Mesh.VertexIndicesItr())
	{
		const FVector3d Position = Mesh.GetVertex(VertexId);
		bAllSane &= FMath::IsFinite(Position.X) && FMath::IsFinite(Position.Y)
			&& FMath::IsFinite(Position.Z);
		// 河面必须贴着地表那一层：半径落在 [海面, 最高地形] 之间。
		bAllSane &= Position.Size() > double(Field->GetSeaLevelRadius()) * WaterTestVoxelSize;
		Positions.Add(Position);
	}
	TestTrue(TEXT("河面顶点必须是有限值且高于海面"), bAllSane);

	// 每个顶点的半径必须来自「方向最近的那个折线点」：河带只在切平面上偏一点，
	// 最近点就是它自己的格。把别处的水位用到这里（或者顶点顺序错位）会在这里现形。
	//
	// 不再按顶点顺序断言「沿下游单调不增」：烘焙对每个源头各给一条折线、每条都画到
	// 入海口（谷地上这次是 296 条），构建器又把干流去重了 —— 顶点顺序里没有
	// 「一条河」的连续分组可言。水位单调是折线本身的性质（BakeRiver 已经钉住），
	// 这里钉的是「顶点 ↔ 折线点」的对应关系。
	TArray<FVector> PointDirections;
	TArray<double> PointSurfaceRadiiCm;
	for (const FPlanetRiverPolyline& River : Field->GetRivers())
	{
		for (const FPlanetRiverPoint& Point : River.Points)
		{
			PointDirections.Add(Point.Dir.GetSafeNormal());
			// 与构建器的 RiverSurfaceLiftCm 对齐（5 cm）：河面比水位抬一点防 z-fighting。
			PointSurfaceRadiiCm.Add(double(Point.WaterLevelRadius) * WaterTestVoxelSize + 5.0);
		}
	}

	double WorstRadiusError = 0.0;
	double WorstNearestAngle = 0.0;
	for (const FVector3d& Position : Positions)
	{
		const FVector3d Direction = Position.GetSafeNormal();
		int32 BestIndex = INDEX_NONE;
		double BestDot = -1.0;
		for (int32 Index = 0; Index < PointDirections.Num(); ++Index)
		{
			const double Dot = FVector3d::DotProduct(Direction, FVector3d(PointDirections[Index]));
			if (Dot > BestDot)
			{
				BestDot = Dot;
				BestIndex = Index;
			}
		}
		if (BestIndex == INDEX_NONE)
		{
			WorstRadiusError = TNumericLimits<double>::Max();
			continue;
		}
		WorstNearestAngle = FMath::Max(WorstNearestAngle,
			FMath::Acos(FMath::Clamp(BestDot, -1.0, 1.0)));
		WorstRadiusError = FMath::Max(WorstRadiusError,
			FMath::Abs(Position.Size() - PointSurfaceRadiiCm[BestIndex]));
	}

	const double CellAngle = (PI * 0.5) / double(FMath::Max(Field->GetResolution(), 1));
	TestTrue(TEXT("必须有折线点可供比对"), PointDirections.Num() > 0);
	TestTrue(FString::Printf(TEXT("每个顶点都必须在半格内找到自己的折线点（最远 %.4f rad，半格 %.4f）"),
		WorstNearestAngle, 0.5 * CellAngle),
		WorstNearestAngle < 0.5 * CellAngle);
	TestTrue(FString::Printf(TEXT("顶点半径必须等于最近折线点的水位（最大误差 %.4f cm）"), WorstRadiusError),
		WorstRadiusError < 1e-3);

	// 干流只画一次：任意两个河面顶点都不能近到重合。修复前每条折线各自重画整条
	// 干流，干流顶点两两相距 0 cm。阈值取 1% 格长 —— 支流在汇入点的两岸与干流
	// 只差一个切向偏角（实测约 2% 格长），那是正常的接缝，不是重复条带。
	double MinimumVertexDistanceCm = TNumericLimits<double>::Max();
	for (int32 Left = 0; Left < Positions.Num(); ++Left)
	{
		for (int32 Right = Left + 1; Right < Positions.Num(); ++Right)
		{
			MinimumVertexDistanceCm = FMath::Min(
				MinimumVertexDistanceCm, FVector3d::Distance(Positions[Left], Positions[Right]));
		}
	}
	const double CellArcCm =
		CellAngle * WaterTestRadius * WaterTestVoxelSize;
	AddInfo(FString::Printf(TEXT("河流：三角形 %d，顶点 %d，任意两顶点最小距离 %.3f cm（一格 %.1f cm）"),
		Mesh.TriangleCount(), Positions.Num(), MinimumVertexDistanceCm, CellArcCm));
	TestTrue(FString::Printf(
		TEXT("不得有重合的河带：任意两顶点距离必须 > 1%% 格长 %.2f cm（实测最小 %.3f cm）"),
		CellArcCm * 0.01, MinimumVertexDistanceCm),
		MinimumVertexDistanceCm > CellArcCm * 0.01);

	return true;
}

/**
 * 真实地形端到端：DA_Terrain_Earth 的参数 + HydrologyResolution = 128。
 *
 * 这是 F1 的回归门：海平面默认值若退回「削平阈值」时代的 −0.35，连通海洋只剩
 * 0.01% 表面积，这里的「>= 25%」会立刻变红。合成地形（碗/谷/台）抓不到这个缺口，
 * 因为它们的「海」是解析写死的 −0.5。
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraWaterEndToEndTest,
	"StellarEra.Planet.Water.EndToEnd",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraWaterEndToEndTest::RunTest(const FString& Parameters)
{
	static const TCHAR* TerrainPath =
		TEXT("/Game/Code/Planet/Geology/TerrainData/DA_Terrain_Earth.DA_Terrain_Earth");
	UVoxelPlanetNoiseTerrain* Terrain = LoadObject<UVoxelPlanetNoiseTerrain>(nullptr, TerrainPath);
	if (!TestNotNull(TEXT("DA_Terrain_Earth 必须能加载（真实地形参数）"), Terrain))
	{
		return false;
	}

	FPlanetWaterConfig Config;
	Terrain->GetWaterConfig(Config);
	Config.Params.Resolution = 128;

	// UniverseMap 的行星半径（体素）。海面与格占比都按它算。
	constexpr double PlanetRadiusVoxels = 10000.0;
	const TSharedPtr<FPlanetHydrologyField> Field = FPlanetHydrologyBake::Bake(
		[Terrain](const FVector& UnitDir) { return Terrain->GetRawOffset(UnitDir); },
		Config.Params, PlanetRadiusVoxels);
	if (!TestTrue(TEXT("真实地形必须烘焙成功"), Field.IsValid() && Field->IsValid()))
	{
		return false;
	}

	// 海洋格 = 水位贴着海面的格 = 烘焙选中的最大连通块（其他低于海面的闭塞
	// 盆地会被填洼抬到溢出高度，成为湖）。
	const int32 Resolution = Config.Params.Resolution;
	const int32 CellCount = PlanetHydrologyGrid::CellCount(Resolution);
	const TArray<float>& Water = Field->GetWaterLevelGrid();
	const float SeaLevelRadius = Field->GetSeaLevelRadius();
	int32 InteriorTotal = 0;
	int32 OceanCells = 0;
	for (int32 Cell = 0; Cell < CellCount; ++Cell)
	{
		int32 Face = 0, X = 0, Y = 0;
		PlanetHydrologyGrid::DecodeCell(Cell, Resolution, Face, X, Y);
		if (X < PlanetHydrologyGrid::Padding || X > Resolution
			|| Y < PlanetHydrologyGrid::Padding || Y > Resolution)
		{
			continue;
		}
		++InteriorTotal;
		if (Water[Cell] <= SeaLevelRadius + 1e-3f)
		{
			++OceanCells;
		}
	}
	const double OceanFraction =
		double(OceanCells) / double(FMath::Max(InteriorTotal, 1));

	AddInfo(FString::Printf(
		TEXT("端到端（res=%d）：SeaLevel=%.2f，海面 %.2f 体素，海洋格 %d/%d = %.2f%%，湖 %d"),
		Resolution, Config.Params.SeaLevel, SeaLevelRadius, OceanCells, InteriorTotal,
		OceanFraction * 100.0, Field->GetLakes().Num()));

	// 实测 45.26%（final-fix-report 的 SeaLevel 扫描）。留足余量只钉下限，
	// 同时钉上限防止「海吞掉全部陆地」这种反向调参。
	TestTrue(FString::Printf(TEXT("连通海洋必须覆盖星球的 25%%–60%%（实测 %.2f%%）"),
		OceanFraction * 100.0), OceanFraction >= 0.25 && OceanFraction <= 0.60);
	TestTrue(FString::Printf(TEXT("必须仍有内陆湖（实测 %d 片）"), Field->GetLakes().Num()),
		Field->GetLakes().Num() > 0);

	// 河网必须在**生产分辨率**下验：128 下实测 0 条河（MinRiverFlow = 24 格、
	// 最短河 4 格，格距翻倍后所有汇流路径都短于 4 格 —— 这是分辨率效应，不是缺陷）。
	// 256 下 5 条（太脆），512 下 757 条。DA_Terrain_Earth 的 HydrologyResolution
	// 就是 512，所以这也是水面真正运行的分辨率。
	const int32 ProductionResolution = FMath::Max(Terrain->HydrologyResolution, 128);
	FPlanetHydrologyParams ProductionParams = Config.Params;
	ProductionParams.Resolution = ProductionResolution;
	const TSharedPtr<FPlanetHydrologyField> ProductionField = FPlanetHydrologyBake::Bake(
		[Terrain](const FVector& UnitDir) { return Terrain->GetRawOffset(UnitDir); },
		ProductionParams, PlanetRadiusVoxels);
	if (!TestTrue(TEXT("生产分辨率下必须烘焙成功"),
		ProductionField.IsValid() && ProductionField->IsValid()))
	{
		return false;
	}
	TestTrue(FString::Printf(TEXT("必须仍有河（res=%d，实测 %d 条）"),
		ProductionResolution, ProductionField->GetRivers().Num()),
		ProductionField->GetRivers().Num() > 0);

	// 三块几何都要真的出东西 —— 与组件生产路径同一套参数（VoxelSize 100，
	// OceanResolution 96，湖冠带地形高度回调）。
	PlanetWaterSurfaceBuilder::FBuildParams BuildParams;
	BuildParams.VoxelSize = 100.0;
	BuildParams.PlanetRadiusVoxels = PlanetRadiusVoxels;
	BuildParams.OceanResolution = 96;
	BuildParams.TerrainHeightFn =
		[Terrain](const FVector& UnitDir) { return Terrain->GetRawOffset(UnitDir); };
	BuildParams.TerrainAmplitude = Config.Params.Amplitude;

	UE::Geometry::FDynamicMesh3 OceanMesh(true, false, false, false);
	PlanetWaterSurfaceBuilder::BuildOcean(*Field, BuildParams, OceanMesh);
	TestTrue(FString::Printf(TEXT("海洋网格必须有三角形（实际 %d）"), OceanMesh.TriangleCount()),
		OceanMesh.TriangleCount() > 0);

	UE::Geometry::FDynamicMesh3 LakeMesh(true, false, false, false);
	PlanetWaterSurfaceBuilder::BuildLakes(*Field, BuildParams, LakeMesh);
	TestTrue(FString::Printf(TEXT("湖泊网格必须有三角形（实际 %d）"), LakeMesh.TriangleCount()),
		LakeMesh.TriangleCount() > 0);

	UE::Geometry::FDynamicMesh3 RiverMesh(true, false, false, false);
	PlanetWaterSurfaceBuilder::BuildRivers(*ProductionField, BuildParams, RiverMesh);
	TestTrue(FString::Printf(TEXT("河流网格必须有三角形（实际 %d）"), RiverMesh.TriangleCount()),
		RiverMesh.TriangleCount() > 0);

	return true;
}

#endif
