// 水文场 → 水面几何。

#include "PlanetWaterSurfaceBuilder.h"

#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "PlanetBiomeCubemap.h"

namespace
{
	/** 河流沿切线向两侧额外铺开的余量（cm）。岸线由深度测试给出，不靠精度。 */
	constexpr double RiverMarginCm = 50.0;

	/** 河面整体抬起的量（cm）。避免与河床三角形共面打架（z-fighting）。 */
	constexpr double RiverSurfaceLiftCm = 5.0;

	/** 判定「这里的水位就是海面」的容差（体素）。 */
	constexpr float OceanLevelEpsilon = 1e-3f;

	/**
	 * 每次重建都从一张干净的表开始。
	 *
	 * 不用 OutMesh.Clear()：它不清 Attributes，残留的法线槽位会让 AppendElement
	 * 的索引与顶点对不上（表现为第一次重建正常、第二次法线全乱）。
	 *
	 * 第四个参数是 bWantTriGroups：UE 5.8 的 FDynamicMesh3 构造函数没有默认值，
	 * 少写一个参数直接编译失败。
	 */
	void ResetMesh(UE::Geometry::FDynamicMesh3& Mesh)
	{
		Mesh = UE::Geometry::FDynamicMesh3(/*bWantNormals=*/true, /*bWantColors=*/false,
			/*bWantUVs=*/false, /*bWantTriGroups=*/false);
		Mesh.EnableAttributes();
	}

	/** 加一个水面顶点：位置 = 方向 × 半径(cm)，法线 = 方向。 */
	int32 AddWaterVertex(UE::Geometry::FDynamicMesh3& Mesh, const FVector& Direction, const double RadiusCm)
	{
		const int32 Index = Mesh.AppendVertex(
			FVector3d(Direction.X * RadiusCm, Direction.Y * RadiusCm, Direction.Z * RadiusCm));
		Mesh.Attributes()->PrimaryNormals()->AppendElement(
			FVector3f(Direction.X, Direction.Y, Direction.Z));
		return Index;
	}

	/** 切平面的两个正交基。选哪个轴当参考只影响顶点顺序，不影响形状。 */
	void MakeTangentBasis(const FVector& Normal, FVector& OutT1, FVector& OutT2)
	{
		const FVector Reference = (FMath::Abs(Normal.Z) < 0.9) ? FVector::UpVector : FVector::RightVector;
		OutT1 = FVector::CrossProduct(Normal, Reference).GetSafeNormal();
		OutT2 = FVector::CrossProduct(Normal, OutT1).GetSafeNormal();
	}
}

void PlanetWaterSurfaceBuilder::BuildOcean(
	const FPlanetHydrologyField& Field,
	const FBuildParams& Params,
	UE::Geometry::FDynamicMesh3& OutMesh)
{
	ResetMesh(OutMesh);
	if (!Field.IsValid() || Params.OceanResolution < 2)
	{
		return;
	}

	const int32 Resolution = Params.OceanResolution;
	const double WaterRadiusCm = double(Field.GetSeaLevelRadius()) * Params.VoxelSize;
	const double SeaLevelRadius = double(Field.GetSeaLevelRadius());

	const auto IsOceanAt = [&Field, SeaLevelRadius](const FVector& Direction)
	{
		return double(Field.Sample(Direction).WaterLevelRadius)
			<= SeaLevelRadius + double(OceanLevelEpsilon);
	};
	const auto DirectionAt = [Resolution](int32 Face, int32 X, int32 Y)
	{
		return PlanetBiomeCubemap::FaceUVToDirection(
			Face, float(X) / float(Resolution), float(Y) / float(Resolution));
	};

	// 顶点按面独立生成：面边界处两份顶点在球面上完全重合，视觉不可见，
	// 换来的是索引简单、不需要跨面查找。
	//
	// 顶点必须**按需**创建，不能先把整面 (N+1)² 个都建出来再挑格子：FDynamicMesh3
	// 会保留没有三角形引用的孤立顶点，而「水面不得出现在陆地内部」这条契约是按
	// 顶点判的 —— 先建后挑等于把整片陆地都算成水面。跨格共用的角因此记在
	// CornerVertex 里，同一格角只建一次。
	for (int32 Face = 0; Face < PlanetBiomeCubemap::NumFaces; ++Face)
	{
		TArray<int32> CornerVertex;
		CornerVertex.Init(INDEX_NONE, (Resolution + 1) * (Resolution + 1));

		const auto CornerAt = [&](int32 X, int32 Y)
		{
			const int32 Slot = Y * (Resolution + 1) + X;
			if (CornerVertex[Slot] == INDEX_NONE)
			{
				CornerVertex[Slot] = AddWaterVertex(
					OutMesh, DirectionAt(Face, X, Y), WaterRadiusCm);
			}
			return CornerVertex[Slot];
		};

		for (int32 Y = 0; Y < Resolution; ++Y)
		{
			for (int32 X = 0; X < Resolution; ++X)
			{
				// 四个角**只要有一个**是海就画这一格：水面向陆地底下探进一格，
				// 海岸线由「水面与地形的交线」自然给出，而不是按格裁出的锯齿。
				const bool bAnyOcean =
					IsOceanAt(DirectionAt(Face, X, Y))
					|| IsOceanAt(DirectionAt(Face, X + 1, Y))
					|| IsOceanAt(DirectionAt(Face, X, Y + 1))
					|| IsOceanAt(DirectionAt(Face, X + 1, Y + 1));
				if (!bAnyOcean)
				{
					continue;
				}

				const int32 A = CornerAt(X, Y);
				const int32 B = CornerAt(X + 1, Y);
				const int32 C = CornerAt(X + 1, Y + 1);
				const int32 D = CornerAt(X, Y + 1);
				// 绕序在双面材质下不影响可见性；光照用法线，不看法线方向与绕序。
				OutMesh.AppendTriangle(A, B, C);
				OutMesh.AppendTriangle(A, C, D);
			}
		}
	}
}

void PlanetWaterSurfaceBuilder::BuildLakes(
	const FPlanetHydrologyField& Field,
	const FBuildParams& Params,
	UE::Geometry::FDynamicMesh3& OutMesh)
{
	ResetMesh(OutMesh);
	if (!Field.IsValid())
	{
		return;
	}

	// 一格的角距：把「湖的角半径」换算成环数，让湖面的顶点密度与水文栅格同阶。
	// 再密一倍没有意义 —— 湖的水位与边界本来就只精确到一格。
	const double CellAngle = (PI * 0.5) / double(FMath::Max(Field.GetResolution(), 1));

	for (const FPlanetLakeBasin& Lake : Field.GetLakes())
	{
		const double WaterRadiusCm = double(Lake.WaterLevelRadius) * Params.VoxelSize;
		const FVector Center = Lake.CentroidDir.GetSafeNormal();
		if (Center.IsNearlyZero() || Lake.AngularRadius <= 0.f)
		{
			continue;
		}

		FVector T1;
		FVector T2;
		MakeTangentBasis(Center, T1, T2);

		const int32 Rings = FMath::Clamp(
			FMath::CeilToInt(double(Lake.AngularRadius) / CellAngle), 2, 64);
		const int32 Segments = FMath::Clamp(Rings * 4, 8, 256);

		// 中心一圈扇形，往外每环一圈四边形。
		const int32 CenterVertex = AddWaterVertex(OutMesh, Center, WaterRadiusCm);

		TArray<int32> PreviousRing;
		TArray<int32> CurrentRing;
		for (int32 Ring = 1; Ring <= Rings; ++Ring)
		{
			const double Theta = double(Lake.AngularRadius) * double(Ring) / double(Rings);
			const double SinTheta = FMath::Sin(Theta);
			const double CosTheta = FMath::Cos(Theta);

			CurrentRing.Reset();
			for (int32 Segment = 0; Segment < Segments; ++Segment)
			{
				const double Phi = 2.0 * PI * double(Segment) / double(Segments);
				const FVector Direction = (Center * CosTheta
					+ (T1 * FMath::Cos(Phi) + T2 * FMath::Sin(Phi)) * SinTheta).GetSafeNormal();
				CurrentRing.Add(AddWaterVertex(OutMesh, Direction, WaterRadiusCm));
			}

			if (Ring == 1)
			{
				for (int32 Segment = 0; Segment < Segments; ++Segment)
				{
					OutMesh.AppendTriangle(CenterVertex,
						CurrentRing[Segment], CurrentRing[(Segment + 1) % Segments]);
				}
			}
			else
			{
				for (int32 Segment = 0; Segment < Segments; ++Segment)
				{
					const int32 Next = (Segment + 1) % Segments;
					OutMesh.AppendTriangle(PreviousRing[Segment], CurrentRing[Segment], CurrentRing[Next]);
					OutMesh.AppendTriangle(PreviousRing[Segment], CurrentRing[Next], PreviousRing[Next]);
				}
			}

			Swap(PreviousRing, CurrentRing);
		}
	}
}

void PlanetWaterSurfaceBuilder::BuildRivers(
	const FPlanetHydrologyField& Field,
	const FBuildParams& Params,
	UE::Geometry::FDynamicMesh3& OutMesh)
{
	ResetMesh(OutMesh);
	if (!Field.IsValid() || Params.PlanetRadiusVoxels <= 0.0)
	{
		return;
	}

	// 厘米 → 球面角。河宽是沿途变化的，所以逐点换算、不做全局近似。
	const double PlanetRadiusCm = Params.PlanetRadiusVoxels * Params.VoxelSize;

	for (const FPlanetRiverPolyline& River : Field.GetRivers())
	{
		const int32 NumPoints = River.Points.Num();
		if (NumPoints < 2)
		{
			continue;
		}

		TArray<int32> LeftBank;
		TArray<int32> RightBank;

		for (int32 Index = 0; Index < NumPoints; ++Index)
		{
			const FPlanetRiverPoint& Point = River.Points[Index];
			const FVector Direction = Point.Dir.GetSafeNormal();
			if (Direction.IsNearlyZero())
			{
				break;
			}

			// 切线：前后点的方向差，再投影回切平面（球面上相邻方向的差本来就在
			// 切平面里，投影是防浮点噪声）。
			const int32 PreviousIndex = FMath::Max(Index - 1, 0);
			const int32 NextIndex = FMath::Min(Index + 1, NumPoints - 1);
			FVector Tangent = River.Points[NextIndex].Dir - River.Points[PreviousIndex].Dir;
			Tangent -= FVector::DotProduct(Tangent, Direction) * Direction;
			if (!Tangent.Normalize())
			{
				break;   // 退化：整条河退成一个点，后面的都不可信
			}
			const FVector Side = FVector::CrossProduct(Direction, Tangent).GetSafeNormal();

			const double HalfWidthCm =
				double(Point.WidthVoxels) * 0.5 * Params.VoxelSize + RiverMarginCm;
			const double HalfAngle = HalfWidthCm / PlanetRadiusCm;

			// 河面比水位再抬 5 cm：河床被下切过，理论上不共面，但河岸的顶点
			// 恰好落在体素面上，抬一点可以彻底避免 z-fighting。
			const double SurfaceRadiusCm =
				double(Point.WaterLevelRadius) * Params.VoxelSize + RiverSurfaceLiftCm;

			LeftBank.Add(AddWaterVertex(
				OutMesh, (Direction - Side * HalfAngle).GetSafeNormal(), SurfaceRadiusCm));
			RightBank.Add(AddWaterVertex(
				OutMesh, (Direction + Side * HalfAngle).GetSafeNormal(), SurfaceRadiusCm));
		}

		for (int32 Index = 0; Index + 1 < LeftBank.Num(); ++Index)
		{
			OutMesh.AppendTriangle(LeftBank[Index], RightBank[Index], RightBank[Index + 1]);
			OutMesh.AppendTriangle(LeftBank[Index], RightBank[Index + 1], LeftBank[Index + 1]);
		}
	}
}
