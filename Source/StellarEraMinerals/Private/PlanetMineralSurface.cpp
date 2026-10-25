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

	// 分簇顺序决定顶点缓冲区布局。TMap 的迭代顺序在同一份数据下确实是确定的，
	// 但那是「哈希恰好稳定」而不是机制 —— 显式按簇内最小 PodIndex 排序，
	// 让两次构建的顶点顺序由排序规则保证。（Members 内的下标本来就递增。）
	ClusterMembers.Sort([](const TArray<int32>& A, const TArray<int32>& B)
	{
		return A[0] < B[0];
	});

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

		double CellSize = 2.0 * double(MinRadiusCm) / double(Params.CellsPerPod);

		// 每轴格数上限。超限时只能放大格边（分辨率下降），不能让引擎去改 CubeSize ——
		// 引擎的 SetDimensions 会在 MaxDim > SafetyMaxDimension（4096）时静默改写
		// CubeSize，而 Implicit 闭包捕获的是我们自己算的 CellSize，两者错位后
		// 求值点与顶点就落在不同空间，只表现为「矿形怪」。
		constexpr int32 MaxCellsPerAxis = 512;
		const double PaddedExtent = FMath::Max3(
			Box.Max.X - Box.Min.X,
			Box.Max.Y - Box.Min.Y,
			Box.Max.Z - Box.Min.Z) + 2.0 * CellSize;
		CellSize = FMath::Max(CellSize, PaddedExtent / double(MaxCellsPerAxis));

		// 外扩一格，并把盒子拉到整数格。引擎按 (int)(Width / CubeSize) + 1 定**格数**
		// （截断）：Width == CellCount * CellSize 在 double 下可能被截成 CellCount - 1，
		// 最外一圈就会被裁掉。这里多给一格，无论截断与否都安全（边界外那层角点场值
		// 恒 0，网格盒比矿囊包围盒至少外扩一格）。
		const FVector PaddedMin = Box.Min - FVector(CellSize);
		const FVector PaddedMax = Box.Max + FVector(CellSize);
		const FIntVector CellCount(
			FMath::Max(1, FMath::CeilToInt((PaddedMax.X - PaddedMin.X) / CellSize)),
			FMath::Max(1, FMath::CeilToInt((PaddedMax.Y - PaddedMin.Y) / CellSize)),
			FMath::Max(1, FMath::CeilToInt((PaddedMax.Z - PaddedMin.Z) / CellSize)));

		const FVector BoxMin = PaddedMin;
		const FVector BoxMax = BoxMin + FVector(
			double(CellCount.X + 1) * CellSize,
			double(CellCount.Y + 1) * CellSize,
			double(CellCount.Z + 1) * CellSize);
		const FIntVector CornerCount = CellCount + FIntVector(1);

		// 512 为什么安全：格边最大只放大到「外扩后最长边 / 512」，于是每轴格数
		// ≤ 512 + 2（两端外扩各一格），远低于引擎的 SafetyMaxDimension（4096）——
		// SetDimensions 永远不会走到改写 CubeSize 的分支，Implicit 的 CellSize 与引擎一致。
		checkf(FMath::Max3(CellCount.X, CellCount.Y, CellCount.Z) < 4096,
			TEXT("矿囊簇格数 %d×%d×%d 触及 FMarchingCubes 的 SafetyMaxDimension"),
			CellCount.X, CellCount.Y, CellCount.Z);

		// 角点数用 int64 求积：格数上限只挡单轴，512³ 仍会溢出 int32。超过 6400 万
		// 就跳过该簇并留一条 Warning —— 不崩、也不静默出半个网格。
		const int64 TotalCorners64 =
			int64(CornerCount.X) * int64(CornerCount.Y) * int64(CornerCount.Z);
		if (TotalCorners64 > 64000000)
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[矿物] 跳过过大的矿囊簇：%lld 个角点（%d × %d × %d，格边 %.1f cm）"),
				TotalCorners64, CornerCount.X, CornerCount.Y, CornerCount.Z, CellSize);
			return;
		}
		const int32 TotalCorners = int32(TotalCorners64);

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

			TArray<float> Norms;
			// 下面逐下标写满，无需先清零。
			Norms.SetNumUninitialized(TotalCorners);

			// 内层串行：每次只是纯函数求值，切分开销与工作同量级；并行度由外层
			// 「按簇并行」提供（确定性也因此与内层无关）。
			for (int32 LinearIndex = 0; LinearIndex < TotalCorners; ++LinearIndex)
			{
				const int32 X = LinearIndex % CornerCount.X;
				const int32 Y = (LinearIndex / CornerCount.X) % CornerCount.Y;
				const int32 Z = LinearIndex / (CornerCount.X * CornerCount.Y);

				const FVector P(
					BoxMin.X + double(X) * CellSize,
					BoxMin.Y + double(Y) * CellSize,
					BoxMin.Z + double(Z) * CellSize);

				Norms[LinearIndex] = Field.SampleElementNormAtNormalized(ElementIndex, P / RadiusCm);
			}

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
			// ElementIndex 来自 FPodInstance，取值恒在 [0, ElementCount)，而
			// MeshesByElement 的长度就是 ElementCount —— 无需再判越界。
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
