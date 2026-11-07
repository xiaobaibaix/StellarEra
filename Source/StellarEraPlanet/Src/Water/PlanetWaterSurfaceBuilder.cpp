// 水文场 → 水面几何。

#include "PlanetWaterSurfaceBuilder.h"

#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "PlanetBiomeCubemap.h"
#include "PlanetHydrologyGrid.h"

namespace
{
	/** 河流沿切线向两侧额外铺开的余量（cm）。岸线由深度测试给出，不靠精度。 */
	constexpr double RiverMarginCm = 50.0;

	/** 河面整体抬起的量（cm）。避免与河床三角形共面打架（z-fighting）。 */
	constexpr double RiverSurfaceLiftCm = 5.0;

	/** 判定「这里的水位就是海面」的容差（体素）。 */
	constexpr float OceanLevelEpsilon = 1e-3f;

	/**
	 * 每次重建都从一张干净的表开始。不用 Clear()：它不清 Attributes，残留的法线
	 * 槽位会让 AppendElement 的索引与顶点对不上（第二次重建法线全乱）。
	 * UE 5.8 的 FDynamicMesh3 构造函数没有默认参数，bWantTriGroups 必须显式给。
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

	/**
	 * 追加三角形并绑定三个角的径向法线。只 AppendElement 不 SetTriangle 的话法线
	 * 元素一直是 INDEX_NONE，渲染端回退到 (0,1,0) —— 整片水面都朝上打光。
	 * 元素下标 == 顶点下标，所以直接填 (A,B,C)。失败时返回 -1（退化 / 非流形）。
	 */
	int32 AppendWaterTriangle(
		UE::Geometry::FDynamicMesh3& Mesh, const int32 A, const int32 B, const int32 C)
	{
		const int32 TriangleId = Mesh.AppendTriangle(A, B, C);
		if (TriangleId >= 0)
		{
			Mesh.Attributes()->PrimaryNormals()->SetTriangle(
				TriangleId, UE::Geometry::FIndex3i(A, B, C));
		}
		return TriangleId;
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
	if (!Field.IsValid() || Params.OceanResolution < 2 || Params.VoxelSize <= 0.0)
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

	// 顶点按面独立生成：面边界两份顶点在球面上重合，视觉不可见，换来索引简单。
	// 顶点必须按需创建：FDynamicMesh3 会保留孤立顶点，而「水面不得出现在陆地
	// 内部」是按顶点判的 —— 先整面建好再挑等于把整片陆地都算成水面。
	int32 RejectedTriangles = 0;
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
				if (AppendWaterTriangle(OutMesh, A, B, C) < 0)
				{
					++RejectedTriangles;
				}
				if (AppendWaterTriangle(OutMesh, A, C, D) < 0)
				{
					++RejectedTriangles;
				}
			}
		}
	}

	ensureMsgf(RejectedTriangles == 0,
		TEXT("[水面] 海洋有 %d 个三角形被 AppendTriangle 拒绝（退化或非流形），网格会漏洞"),
		RejectedTriangles);
}

void PlanetWaterSurfaceBuilder::BuildLakes(
	const FPlanetHydrologyField& Field,
	const FBuildParams& Params,
	UE::Geometry::FDynamicMesh3& OutMesh)
{
	ResetMesh(OutMesh);
	if (!Field.IsValid() || Params.VoxelSize <= 0.0)
	{
		return;
	}

	// 一格的角距：湖面顶点密度与水文栅格同阶，再密没有意义。
	const double CellAngle = (PI * 0.5) / double(FMath::Max(Field.GetResolution(), 1));

	// 没有地形高度就裁不了余量（旧行为：整张球冠铺满 AngularRadius）。
	const bool bClipMargin = Params.TerrainHeightFn && Params.PlanetRadiusVoxels > 0.0;

	int32 RejectedTriangles = 0;
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

		// 每段（方位角）能保留到第几环；bClipMargin 时砍掉岸外的余量环。
		// 湖的真实范围只到 AngularRadius − PlanetLakeCapMarginCells 格。余量探进
		// 岸里，只有地形高过水位时才安全（水面埋地里由深度测试裁掉）；地形更低时
		// 水面悬空。所以按段从内往外走，遇到第一环地形低于水位就停。
		TArray<int32> KeepRing;
		KeepRing.Init(Rings, Segments);
		if (bClipMargin)
		{
			const double InnerAngle = FMath::Max(
				double(Lake.AngularRadius) - double(PlanetLakeCapMarginCells) * CellAngle, 0.0);

			for (int32 Segment = 0; Segment < Segments; ++Segment)
			{
				const double Phi = 2.0 * PI * double(Segment) / double(Segments);
				const double SinPhi = FMath::Sin(Phi);
				const double CosPhi = FMath::Cos(Phi);

				for (int32 Ring = 1; Ring <= Rings; ++Ring)
				{
					const double Theta = double(Lake.AngularRadius) * double(Ring) / double(Rings);
					if (Theta <= InnerAngle)
					{
						continue;   // 湖内：烘焙保证这里水面下有地形
					}

					const FVector Direction = (Center * FMath::Cos(Theta)
						+ (T1 * CosPhi + T2 * SinPhi) * FMath::Sin(Theta)).GetSafeNormal();
					const double TerrainRadius = double(Params.PlanetRadiusVoxels)
						+ double(Params.TerrainAmplitude) * double(Params.TerrainHeightFn(Direction));

					if (TerrainRadius < double(Lake.WaterLevelRadius))
					{
						KeepRing[Segment] = Ring - 1;
						break;
					}
				}
			}
		}

		// 顶点按需创建：裁剪后每段边缘是锯齿状的，先整环建好会留下看不见的孤立顶点。
		TMap<int32, int32> RingVertices;   // 键 = Ring * Segments + Segment
		int32 CenterVertex = INDEX_NONE;

		const auto GetCenterVertex = [&]()
		{
			if (CenterVertex == INDEX_NONE)
			{
				CenterVertex = AddWaterVertex(OutMesh, Center, WaterRadiusCm);
			}
			return CenterVertex;
		};
		const auto GetRingVertex = [&](const int32 Ring, const int32 Segment)
		{
			const int32 Key = Ring * Segments + Segment;
			if (const int32* Existing = RingVertices.Find(Key))
			{
				return *Existing;
			}

			const double Theta = double(Lake.AngularRadius) * double(Ring) / double(Rings);
			const double Phi = 2.0 * PI * double(Segment) / double(Segments);
			const FVector Direction = (Center * FMath::Cos(Theta)
				+ (T1 * FMath::Cos(Phi) + T2 * FMath::Sin(Phi)) * FMath::Sin(Theta)).GetSafeNormal();
			const int32 Vertex = AddWaterVertex(OutMesh, Direction, WaterRadiusCm);
			RingVertices.Add(Key, Vertex);
			return Vertex;
		};

		// 中心一圈扇形，往外每环一圈四边形。
		for (int32 Ring = 1; Ring <= Rings; ++Ring)
		{
			if (Ring == 1)
			{
				for (int32 Segment = 0; Segment < Segments; ++Segment)
				{
					const int32 Next = (Segment + 1) % Segments;
					if (KeepRing[Segment] < 1 || KeepRing[Next] < 1)
					{
						continue;
					}
					if (AppendWaterTriangle(OutMesh, GetCenterVertex(),
						GetRingVertex(1, Segment), GetRingVertex(1, Next)) < 0)
					{
						++RejectedTriangles;
					}
				}
			}
			else
			{
				for (int32 Segment = 0; Segment < Segments; ++Segment)
				{
					const int32 Next = (Segment + 1) % Segments;
					// 任一角的段在这一环已经停了，这一格四边形就不完整（缺一条边），
					// 整格丢掉；外侧的段本来就比内侧短。
					if (KeepRing[Segment] < Ring || KeepRing[Next] < Ring)
					{
						continue;
					}
					if (AppendWaterTriangle(OutMesh,
						GetRingVertex(Ring - 1, Segment), GetRingVertex(Ring, Segment),
						GetRingVertex(Ring, Next)) < 0)
					{
						++RejectedTriangles;
					}
					if (AppendWaterTriangle(OutMesh,
						GetRingVertex(Ring - 1, Segment), GetRingVertex(Ring, Next),
						GetRingVertex(Ring - 1, Next)) < 0)
					{
						++RejectedTriangles;
					}
				}
			}
		}
	}

	ensureMsgf(RejectedTriangles == 0,
		TEXT("[水面] 湖泊有 %d 个三角形被 AppendTriangle 拒绝（退化或非流形），网格会漏洞"),
		RejectedTriangles);
}

void PlanetWaterSurfaceBuilder::BuildRivers(
	const FPlanetHydrologyField& Field,
	const FBuildParams& Params,
	UE::Geometry::FDynamicMesh3& OutMesh)
{
	ResetMesh(OutMesh);
	if (!Field.IsValid() || Params.PlanetRadiusVoxels <= 0.0 || Params.VoxelSize <= 0.0)
	{
		return;
	}

	// 厘米 → 球面角。河宽是沿途变化的，所以逐点换算、不做全局近似。
	const double PlanetRadiusCm = Params.PlanetRadiusVoxels * Params.VoxelSize;
	const int32 Resolution = Field.GetResolution();

	// 每个水文栅格的河岸顶点。烘焙给每个源头一条折线、每条都画到入海口，干流每格
	// 同时属于所有上游折线；各建一套顶点会重合出两层条带（半透明下表现为
	// z-fighting）。所以一格只发一次：每条只画到第一个已被画过的格（含它，支流
	// 要接上干流），之后归先画的那条。
	TMap<int32, FIntPoint> EmittedBanks;

	int32 RejectedTriangles = 0;
	for (const FPlanetRiverPolyline& River : Field.GetRivers())
	{
		const int32 NumPoints = River.Points.Num();
		if (NumPoints < 2)
		{
			continue;
		}

		// 前缀长度：第一个已发射的格（含）就是尽头。起点若落在干流上，前缀只有一格，
		// 画不出三角形，也就不建任何顶点 —— 孤立顶点会让一切按顶点判的断言失真。
		int32 PrefixLength = NumPoints;
		for (int32 Index = 0; Index < NumPoints; ++Index)
		{
			const int32 Cell = PlanetHydrologyGrid::DirectionToCell(
				River.Points[Index].Dir, Resolution);
			if (EmittedBanks.Contains(Cell))
			{
				PrefixLength = Index + 1;
				break;
			}
		}
		if (PrefixLength < 2)
		{
			continue;
		}

		// 先只算方向、不建顶点：切线退化之后的部分不画，那些点也就不会留下孤立顶点。
		TArray<FVector> LeftDirections;
		TArray<FVector> RightDirections;
		TArray<double> SurfaceRadiiCm;
		TArray<int32> Cells;
		LeftDirections.Reserve(PrefixLength);
		RightDirections.Reserve(PrefixLength);
		SurfaceRadiiCm.Reserve(PrefixLength);
		Cells.Reserve(PrefixLength);

		for (int32 Index = 0; Index < PrefixLength; ++Index)
		{
			const FPlanetRiverPoint& Point = River.Points[Index];
			const FVector Direction = Point.Dir.GetSafeNormal();
			if (Direction.IsNearlyZero())
			{
				break;
			}

			// 切线：前后点的方向差再投影回切平面（球面相邻方向的差本就在切平面里，
			// 投影是防浮点噪声）。
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

			// 河面比水位再抬 5 cm：河岸顶点恰好落在体素面上，抬一点彻底避免 z-fighting。
			SurfaceRadiiCm.Add(
				double(Point.WaterLevelRadius) * Params.VoxelSize + RiverSurfaceLiftCm);
			LeftDirections.Add((Direction - Side * HalfAngle).GetSafeNormal());
			RightDirections.Add((Direction + Side * HalfAngle).GetSafeNormal());
			Cells.Add(PlanetHydrologyGrid::DirectionToCell(Direction, Resolution));
		}

		if (LeftDirections.Num() < 2)
		{
			continue;   // 一个段都画不出来：不要留下没有三角形引用的顶点
		}

		TArray<int32> LeftBank;
		TArray<int32> RightBank;
		LeftBank.Reserve(LeftDirections.Num());
		RightBank.Reserve(LeftDirections.Num());
		for (int32 Index = 0; Index < LeftDirections.Num(); ++Index)
		{
			const int32 Left = AddWaterVertex(OutMesh, LeftDirections[Index], SurfaceRadiiCm[Index]);
			const int32 Right = AddWaterVertex(OutMesh, RightDirections[Index], SurfaceRadiiCm[Index]);
			LeftBank.Add(Left);
			RightBank.Add(Right);

			// 汇入点所在的格已经属于干流：保留干流那一对，别被支流覆盖。
			if (!EmittedBanks.Contains(Cells[Index]))
			{
				EmittedBanks.Add(Cells[Index], FIntPoint(Left, Right));
			}
		}

		for (int32 Index = 0; Index + 1 < LeftBank.Num(); ++Index)
		{
			if (AppendWaterTriangle(OutMesh,
				LeftBank[Index], RightBank[Index], RightBank[Index + 1]) < 0)
			{
				++RejectedTriangles;
			}
			if (AppendWaterTriangle(OutMesh,
				LeftBank[Index], RightBank[Index + 1], LeftBank[Index + 1]) < 0)
			{
				++RejectedTriangles;
			}
		}
	}

	ensureMsgf(RejectedTriangles == 0,
		TEXT("[水面] 河流有 %d 个三角形被 AppendTriangle 拒绝（退化或非流形），网格会漏洞"),
		RejectedTriangles);
}
