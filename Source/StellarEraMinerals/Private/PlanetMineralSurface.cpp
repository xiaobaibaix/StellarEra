// Copyright Voxel Plugin SAS. All Rights Reserved.

#include "PlanetMineralSurface.h"

#include "PlanetGeologyField.h"

#include "Async/ParallelFor.h"
#include "Generators/MarchingCubes.h"
#include "HAL/PlatformTime.h"

namespace
{
	/** 一个待处理的矿囊（厘米空间）。 */
	struct FPodInstance
	{
		int32 ElementIndex = INDEX_NONE;
		FVector CenterCm = FVector::ZeroVector;
		float RadiusCm = 0.f;
	};

	/** 并查集：把邻近的矿囊合并成同一个局部栅格。 */
	struct FDisjointSet
	{
		TArray<int32> Parent;

		void Init(int32 Count)
		{
			Parent.SetNum(Count);
			for (int32 Index = 0; Index < Count; ++Index)
			{
				Parent[Index] = Index;
			}
		}

		int32 Find(int32 Index)
		{
			while (Parent[Index] != Index)
			{
				Parent[Index] = Parent[Parent[Index]];
				Index = Parent[Index];
			}
			return Index;
		}

		void Union(int32 A, int32 B)
		{
			const int32 RootA = Find(A);
			const int32 RootB = Find(B);
			if (RootA != RootB)
			{
				Parent[RootB] = RootA;
			}
		}
	};

	bool AreAabbsOverlapping(const FBox& A, const FBox& B)
	{
		return A.Min.X <= B.Max.X && A.Max.X >= B.Min.X
			&& A.Min.Y <= B.Max.Y && A.Max.Y >= B.Min.Y
			&& A.Min.Z <= B.Max.Z && A.Max.Z >= B.Min.Z;
	}
}

void PlanetMineralSurface::Build(const FPlanetGeologyField& Field, const FBuildParams& Params,
	FBuildResult& Out)
{
	Out.MeshesByElement.Reset();
	Out.GridPoints = 0;
	Out.BuildSeconds = 0.0;

	const double RadiusCm = double(Params.SurfaceRadiusCm);
	if (Field.IsEmpty() || RadiusCm <= 0.0 || Params.VoxelSizeCm <= 0.f
		|| Params.CellsPerPod < 4)
	{
		return;
	}

	const double StartSeconds = FPlatformTime::Seconds();
	const int32 ElementCount = Field.NumElements();
	Out.MeshesByElement.SetNum(ElementCount);

	// --- 1. 收集所有矿囊（厘米空间） ---
	TArray<FPodInstance> Pods;
	for (int32 ElementIndex = 0; ElementIndex < ElementCount; ++ElementIndex)
	{
		for (const FPlanetGeologyPod& Pod : Field.GetPods(ElementIndex))
		{
			if (Pod.Radius <= 0.f)
			{
				continue;
			}
			FPodInstance& Instance = Pods.AddDefaulted_GetRef();
			Instance.ElementIndex = ElementIndex;
			Instance.CenterCm = Pod.Center * RadiusCm;
			Instance.RadiusCm = Pod.Radius * float(RadiusCm);
		}
	}
	if (Pods.Num() == 0)
	{
		Out.BuildSeconds = FPlatformTime::Seconds() - StartSeconds;
		return;
	}

	TArray<FBox> PodBounds;
	PodBounds.Reserve(Pods.Num());
	for (const FPodInstance& Pod : Pods)
	{
		PodBounds.Add(FBox(Pod.CenterCm - FVector(Pod.RadiusCm),
			Pod.CenterCm + FVector(Pod.RadiusCm)));
	}

	// --- 2. 分簇：未外扩的 AABB 相交即同簇（并查集） ---
	FDisjointSet Clusters;
	Clusters.Init(Pods.Num());
	for (int32 A = 0; A < Pods.Num(); ++A)
	{
		for (int32 B = A + 1; B < Pods.Num(); ++B)
		{
			if (AreAabbsOverlapping(PodBounds[A], PodBounds[B]))
			{
				Clusters.Union(A, B);
			}
		}
	}

	TMap<int32, TArray<int32>> MembersByRoot;
	for (int32 Index = 0; Index < Pods.Num(); ++Index)
	{
		MembersByRoot.FindOrAdd(Clusters.Find(Index)).Add(Index);
	}

	TArray<TArray<int32>> ClusterMembers;
	ClusterMembers.Reserve(MembersByRoot.Num());
	for (TPair<int32, TArray<int32>>& Pair : MembersByRoot)
	{
		ClusterMembers.Add(MoveTemp(Pair.Value));
	}

	// --- 3. 每簇一套局部栅格，并行抽面 ---
	struct FClusterMesh
	{
		int32 ElementIndex = INDEX_NONE;
		FPlanetMineralMesh Mesh;
		int32 GridPoints = 0;
	};

	TArray<TArray<FClusterMesh>> ClusterResults;
	ClusterResults.SetNum(ClusterMembers.Num());

	ParallelFor(ClusterMembers.Num(), [&](int32 ClusterIndex)
	{
		const TArray<int32>& Members = ClusterMembers[ClusterIndex];

		FBox Box(ForceInit);
		float MinRadiusCm = TNumericLimits<float>::Max();
		for (const int32 PodIndex : Members)
		{
			Box += PodBounds[PodIndex];
			MinRadiusCm = FMath::Min(MinRadiusCm, Pods[PodIndex].RadiusCm);
		}
		if (MinRadiusCm <= 0.f || !Box.IsValid)
		{
			return;
		}

		const double CellSize = 2.0 * double(MinRadiusCm) / double(Params.CellsPerPod);

		// 外扩一格，并把盒子拉到整数格：FMarchingCubes 按 Width/CubeSize + 1 定角点数。
		const FVector PaddedMin = Box.Min - FVector(CellSize);
		const FVector PaddedMax = Box.Max + FVector(CellSize);
		const FIntVector CellCount(
			FMath::Max(1, FMath::CeilToInt((PaddedMax.X - PaddedMin.X) / CellSize)),
			FMath::Max(1, FMath::CeilToInt((PaddedMax.Y - PaddedMin.Y) / CellSize)),
			FMath::Max(1, FMath::CeilToInt((PaddedMax.Z - PaddedMin.Z) / CellSize)));

		const FVector BoxMin = PaddedMin;
		const FVector BoxMax = BoxMin + FVector(
			double(CellCount.X) * CellSize,
			double(CellCount.Y) * CellSize,
			double(CellCount.Z) * CellSize);
		const FIntVector CornerCount = CellCount + FIntVector(1);

		// 本簇涉及哪些元素
		TSet<int32> ElementSet;
		for (const int32 PodIndex : Members)
		{
			ElementSet.Add(Pods[PodIndex].ElementIndex);
		}

		TArray<FClusterMesh>& Results = ClusterResults[ClusterIndex];
		Results.SetNum(ElementSet.Num());

		int32 ResultIndex = 0;
		for (const int32 ElementIndex : ElementSet)
		{
			FClusterMesh& Target = Results[ResultIndex];
			Target.ElementIndex = ElementIndex;

			const int32 TotalCorners = CornerCount.X * CornerCount.Y * CornerCount.Z;
			TArray<float> Norms;
			Norms.SetNumZeroed(TotalCorners);

			ParallelFor(TotalCorners, [&](int32 LinearIndex)
			{
				const int32 X = LinearIndex % CornerCount.X;
				const int32 Y = (LinearIndex / CornerCount.X) % CornerCount.Y;
				const int32 Z = LinearIndex / (CornerCount.X * CornerCount.Y);

				const FVector P(
					BoxMin.X + double(X) * CellSize,
					BoxMin.Y + double(Y) * CellSize,
					BoxMin.Z + double(Z) * CellSize);

				Norms[LinearIndex] = Field.SampleElementNormAtNormalized(ElementIndex, P / RadiusCm);
			});

			UE::Geometry::FMarchingCubes Marching;
			Marching.Bounds.Min = UE::Math::TVector<double>(BoxMin.X, BoxMin.Y, BoxMin.Z);
			Marching.Bounds.Max = UE::Math::TVector<double>(BoxMax.X, BoxMax.Y, BoxMax.Z);
			Marching.CubeSize = CellSize;
			Marching.IsoValue = double(Params.IsoValue);
			Marching.bEnableValueCaching = false;
			// FMarchingCubes 的并行路径用 atomic 计数器分配顶点下标、按线程完成顺序填
			// 分片数组，顶点顺序不确定（见旧 Deterministic 测试的排序注释）。观测视图要求
			// 两次构建逐顶点、逐索引相同，所以簇内串行；并行度由外层「按簇并行」提供。
			Marching.bParallelCompute = false;
			Marching.Implicit = [&Norms, CornerCount, BoxMin, CellSize](
				UE::Math::TVector<double> P) -> double
			{
				const double GX = FMath::Clamp((P.X - BoxMin.X) / CellSize, 0.0, double(CornerCount.X - 1));
				const double GY = FMath::Clamp((P.Y - BoxMin.Y) / CellSize, 0.0, double(CornerCount.Y - 1));
				const double GZ = FMath::Clamp((P.Z - BoxMin.Z) / CellSize, 0.0, double(CornerCount.Z - 1));

				const int32 X0 = FMath::Min(int32(GX), CornerCount.X - 2);
				const int32 Y0 = FMath::Min(int32(GY), CornerCount.Y - 2);
				const int32 Z0 = FMath::Min(int32(GZ), CornerCount.Z - 2);
				const double FX = GX - double(X0);
				const double FY = GY - double(Y0);
				const double FZ = GZ - double(Z0);

				auto At = [&Norms, CornerCount](int32 InX, int32 InY, int32 InZ) -> double
				{
					return double(Norms[(InZ * CornerCount.Y + InY) * CornerCount.X + InX]);
				};

				const double C00 = FMath::Lerp(At(X0, Y0, Z0), At(X0 + 1, Y0, Z0), FX);
				const double C10 = FMath::Lerp(At(X0, Y0 + 1, Z0), At(X0 + 1, Y0 + 1, Z0), FX);
				const double C01 = FMath::Lerp(At(X0, Y0, Z0 + 1), At(X0 + 1, Y0, Z0 + 1), FX);
				const double C11 = FMath::Lerp(At(X0, Y0 + 1, Z0 + 1), At(X0 + 1, Y0 + 1, Z0 + 1), FX);
				const double C0 = FMath::Lerp(C00, C10, FY);
				const double C1 = FMath::Lerp(C01, C11, FY);
				return FMath::Lerp(C0, C1, FZ);
			};

			Marching.Generate();

			Target.GridPoints = TotalCorners;
			Target.Mesh.VerticesCm.Reserve(Marching.Vertices.Num());
			for (const UE::Math::TVector<double>& Vertex : Marching.Vertices)
			{
				// 球外角点恒为 0、内侧角点为正：插值会把顶点落到球外，
				// 而观察体的半透明壳画在 R 上，逐顶点径向夹回。
				const double LengthCm = Vertex.Size();
				const double ScaleFactor = LengthCm > RadiusCm ? RadiusCm / LengthCm : 1.0;
				Target.Mesh.VerticesCm.Add(FVector3f(
					float(Vertex.X * ScaleFactor),
					float(Vertex.Y * ScaleFactor),
					float(Vertex.Z * ScaleFactor)));
			}

			Target.Mesh.Triangles.Reserve(Marching.Triangles.Num() * 3);
			for (const UE::Geometry::FIndex3i& Triangle : Marching.Triangles)
			{
				Target.Mesh.Triangles.Add(Triangle.A);
				Target.Mesh.Triangles.Add(Triangle.B);
				Target.Mesh.Triangles.Add(Triangle.C);
			}

			int32 Occupied = 0;
			for (const float Norm : Norms)
			{
				Occupied += Norm > Params.IsoValue ? 1 : 0;
			}
			Target.Mesh.OccupiedCells = Occupied;

			++ResultIndex;
		}
	});

	// --- 4. 合并各簇结果：顶点下标要按已合并的数量偏移 ---
	for (const TArray<FClusterMesh>& Results : ClusterResults)
	{
		for (const FClusterMesh& Result : Results)
		{
			if (!Out.MeshesByElement.IsValidIndex(Result.ElementIndex))
			{
				continue;
			}

			FPlanetMineralMesh& Mesh = Out.MeshesByElement[Result.ElementIndex];
			const int32 VertexOffset = Mesh.VerticesCm.Num();
			Mesh.VerticesCm.Append(Result.Mesh.VerticesCm);
			Mesh.Triangles.Reserve(Mesh.Triangles.Num() + Result.Mesh.Triangles.Num());
			for (const int32 Index : Result.Mesh.Triangles)
			{
				Mesh.Triangles.Add(Index + VertexOffset);
			}
			Mesh.OccupiedCells += Result.Mesh.OccupiedCells;
			Out.GridPoints += Result.GridPoints;
		}
	}

	// 顶点顺序里没有用到的元素保持空网格（观察体按元素数校验）。
	Out.BuildSeconds = FPlatformTime::Seconds() - StartSeconds;
}
