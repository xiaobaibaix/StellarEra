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
#include "PlanetWaterSurfaceBuilder.h"

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
	double WorstNormalDot = 1.0;

	// 法线是逐顶点追加的，元素下标与顶点下标一一对应。
	const UE::Geometry::FDynamicMeshNormalOverlay* Normals = Mesh.Attributes()->PrimaryNormals();

	for (const int32 VertexId : Mesh.VertexIndicesItr())
	{
		const FVector3d Position = Mesh.GetVertex(VertexId);
		const double RadiusCm = Position.Size();
		WorstRadiusError = FMath::Max(WorstRadiusError, FMath::Abs(RadiusCm - ExpectedRadiusCm));

		const FVector3d Direction = Position / FMath::Max(RadiusCm, 1e-9);

		// 法线必须朝外：水面是一张球面片，径向就是精确解。
		const FVector3f Normal = Normals->GetElement(VertexId);
		WorstNormalDot = FMath::Min(WorstNormalDot,
			FVector3d::DotProduct(FVector3d(Normal.X, Normal.Y, Normal.Z), Direction));

		// 陆地内部的顶点必须被裁掉。允许一格余量：跨岸的那一格会被保留下来
		// （水面探进陆地一格，海岸线由交线自然给出）。
		const double AngleFromCap = FMath::Acos(FMath::Clamp(Direction.Z, -1.0, 1.0));
		NearestOnLandCap = FMath::Min(NearestOnLandCap, AngleFromCap);
	}

	TestTrue(FString::Printf(TEXT("所有顶点必须落在海面半径上（最大误差 %.4f cm）"), WorstRadiusError),
		WorstRadiusError < 1.0);
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

	UE::Geometry::FDynamicMesh3 Mesh(true, false, false, false);
	PlanetWaterSurfaceBuilder::BuildLakes(*Field, MakeWaterBuildParams(), Mesh);

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
	TArray<double> PairRadii;
	for (const int32 VertexId : Mesh.VertexIndicesItr())
	{
		const FVector3d Position = Mesh.GetVertex(VertexId);
		bAllSane &= FMath::IsFinite(Position.X) && FMath::IsFinite(Position.Y)
			&& FMath::IsFinite(Position.Z);
		// 河面必须贴着地表那一层：半径落在 [海面, 最高地形] 之间。
		bAllSane &= Position.Size() > double(Field->GetSeaLevelRadius()) * WaterTestVoxelSize;
		PairRadii.Add(Position.Size());
	}
	TestTrue(TEXT("河面顶点必须是有限值且高于海面"), bAllSane);

	// 沿每条河向下游水位单调不增。顶点按「每条折线、每点先左岸后右岸」的顺序追加，
	// 所以先按折线分组、再每两点取平均。
	//
	// 不能跨折线比较：折线之间彼此独立。谷地上这次烘焙出了 296 条折线，上一条的
	// 终点是入海口（≈ 海面）、下一条的源头还在山上（比海面高 3 体素），把它们当
	// 一条河看，单调性必然被打破 —— 那是折线顺序，不是水位错误。
	bool bMonotone = true;
	int32 VertexCursor = 0;
	for (const FPlanetRiverPolyline& River : Field->GetRivers())
	{
		double PreviousRadius = TNumericLimits<double>::Max();
		for (int32 PointIndex = 0; PointIndex < River.Points.Num(); ++PointIndex)
		{
			if (!PairRadii.IsValidIndex(VertexCursor + 1))
			{
				bMonotone = false;
				break;
			}
			const double PairRadius = (PairRadii[VertexCursor] + PairRadii[VertexCursor + 1]) * 0.5;
			VertexCursor += 2;
			bMonotone &= PairRadius <= PreviousRadius + 1e-3;
			PreviousRadius = PairRadius;
		}
	}
	// 每条折线的每个点必须恰好贡献两个顶点，否则上面的分组已经错位、断言失去意义。
	TestEqual(TEXT("顶点数必须等于「折线点数 × 2」（左岸 + 右岸）"),
		VertexCursor, PairRadii.Num());
	TestTrue(TEXT("河面水位必须沿下游单调不增"), bMonotone);

	return true;
}

#endif
