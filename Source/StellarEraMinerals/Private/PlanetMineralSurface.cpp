// Copyright Voxel Plugin SAS. All Rights Reserved.

#include "PlanetMineralSurface.h"

#include "PlanetGeologyField.h"

#include "Async/ParallelFor.h"
#include "Generators/MarchingCubes.h"
#include "HAL/PlatformTime.h"

namespace
{
	/** 一个待处理的区域（厘米空间）。 */
	struct FRegionInstance
	{
		int32 ElementIndex = INDEX_NONE;
		FBox BoundsCm = FBox(ForceInit);
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

void PlanetMineralSurface::ClusterRegionBounds(const TArray<FBox>& BoundsCm,
	TArray<int32>& OutClusterRoots)
{
	OutClusterRoots.Reset();
	const int32 RegionCount = BoundsCm.Num();
	if (RegionCount == 0)
	{
		return;
	}
	OutClusterRoots.SetNum(RegionCount);
	for (int32 Index = 0; Index < RegionCount; ++Index)
	{
		OutClusterRoots[Index] = Index; // 尚未合并时每个区域自成一簇
	}

	// 格边 = 最大轴长。相交对至少共享一个点，该点落在一格里，两边的包围盒都覆盖
	// 那一格 —— 所以只要把「同格」再逐个判相交，就不可能漏 union。
	double CellSize = 0.0;
	for (const FBox& Bounds : BoundsCm)
	{
		CellSize = FMath::Max(CellSize, double(Bounds.GetSize().GetMax()));
	}
	if (CellSize <= 0.0)
	{
		return;
	}

	FDisjointSet Clusters;
	Clusters.Init(RegionCount);

	TMap<FIntVector, TArray<int32>> Grid;
	for (int32 Index = 0; Index < RegionCount; ++Index)
	{
		const FIntVector MinCell = PlanetGeologyToCell(BoundsCm[Index].Min, CellSize);
		const FIntVector MaxCell = PlanetGeologyToCell(BoundsCm[Index].Max, CellSize);
		for (int32 X = MinCell.X; X <= MaxCell.X; ++X)
		{
			for (int32 Y = MinCell.Y; Y <= MaxCell.Y; ++Y)
			{
				for (int32 Z = MinCell.Z; Z <= MaxCell.Z; ++Z)
				{
					Grid.FindOrAdd(FIntVector(X, Y, Z)).Add(Index);
				}
			}
		}
	}

	for (TPair<FIntVector, TArray<int32>>& Pair : Grid)
	{
		TArray<int32>& Members = Pair.Value;
		Members.Sort([&BoundsCm](const int32 A, const int32 B)
		{
			return BoundsCm[A].Min.X < BoundsCm[B].Min.X;
		});

		for (int32 A = 0; A < Members.Num(); ++A)
		{
			for (int32 B = A + 1; B < Members.Num(); ++B)
			{
				// 按 Min.X 升序：一旦越过 A 的 Max.X，后面的都不可能与 A 相交。
				if (BoundsCm[Members[B]].Min.X > BoundsCm[Members[A]].Max.X)
				{
					break;
				}
				if (AreAabbsOverlapping(BoundsCm[Members[A]], BoundsCm[Members[B]]))
				{
					Clusters.Union(Members[A], Members[B]);
				}
			}
		}
	}

	// 根归一成簇内最小下标：TMap 迭代顺序不是机制，不归一化的话两次构建的
	// 簇序（进而顶点顺序）会跟着哈希走。
	TMap<int32, int32> MinIndexByRoot;
	for (int32 Index = 0; Index < RegionCount; ++Index)
	{
		const int32 Root = Clusters.Find(Index);
		int32& MinIndex = MinIndexByRoot.FindOrAdd(Root, Index);
		MinIndex = FMath::Min(MinIndex, Index);
	}
	for (int32 Index = 0; Index < RegionCount; ++Index)
	{
		OutClusterRoots[Index] = MinIndexByRoot.FindChecked(Clusters.Find(Index));
	}
}

void PlanetMineralSurface::Build(const FPlanetGeologyField& Field, const FBuildParams& Params,
	FBuildResult& Out)
{
	Out.MeshesByElement.Reset();
	Out.ClustersByElement.Reset();
	Out.GridPoints = 0;
	Out.BuildSeconds = 0.0;

	const double RadiusCm = double(Params.SurfaceRadiusCm);
	if (Field.IsEmpty() || RadiusCm <= 0.0 || Params.VoxelSizeCm <= 0.f
		|| Params.CellsPerFeature < 4)
	{
		return;
	}

	const double StartSeconds = FPlatformTime::Seconds();
	const int32 ElementCount = Field.NumElements();
	Out.MeshesByElement.SetNum(ElementCount);
	Out.ClustersByElement.SetNum(ElementCount);

	// --- 1. 收集所有区域（厘米空间） ---
	TArray<FRegionInstance> Regions;
	for (int32 ElementIndex = 0; ElementIndex < ElementCount; ++ElementIndex)
	{
		TArray<FPlanetGeologyRegion> ElementRegions;
		Field.EnumerateRegions(ElementIndex, ElementRegions);
		for (const FPlanetGeologyRegion& Region : ElementRegions)
		{
			if (Region.CharacteristicRadius <= 0.f || Region.Bounds.IsValid == 0)
			{
				continue;
			}
			FRegionInstance& Instance = Regions.AddDefaulted_GetRef();
			Instance.ElementIndex = ElementIndex;
			Instance.BoundsCm = FBox(
				Region.Bounds.Min * RadiusCm, Region.Bounds.Max * RadiusCm);
			Instance.RadiusCm = Region.CharacteristicRadius * float(RadiusCm);
		}
	}
	if (Regions.Num() == 0)
	{
		Out.BuildSeconds = FPlatformTime::Seconds() - StartSeconds;
		return;
	}

	TArray<FBox> RegionBounds;
	RegionBounds.Reserve(Regions.Num());
	for (const FRegionInstance& Region : Regions)
	{
		RegionBounds.Add(Region.BoundsCm);
	}

	// --- 2. 分簇：未外扩的 AABB 相交即同簇（空间哈希 + sweep-and-prune） ---
	TArray<int32> ClusterRoots;
	PlanetMineralSurface::ClusterRegionBounds(RegionBounds, ClusterRoots);

	TMap<int32, TArray<int32>> MembersByRoot;
	for (int32 Index = 0; Index < Regions.Num(); ++Index)
	{
		MembersByRoot.FindOrAdd(ClusterRoots[Index]).Add(Index);
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
		for (const int32 RegionIndex : Members)
		{
			Box += RegionBounds[RegionIndex];
			MinRadiusCm = FMath::Min(MinRadiusCm, Regions[RegionIndex].RadiusCm);
		}
		if (MinRadiusCm <= 0.f || !Box.IsValid)
		{
			return;
		}

		// 先按 CellsPerFeature 定基准格边；再保证每轴不超过 MaxCellsPerAxis、
		// 单簇单元素总角点不超过 MaxTotalCorners。触发时放大格边（分辨率下降），
		// 绝不跳过 —— 跳过会让一整簇区域凭空消失，比糊更坏。
		//
		// 两个上限都是**硬上限**：CellCount = ceil(extent/CellSize) + 2（外扩的两格），
		// CornerCount = CellCount + 1，所以除数要先减掉这 3 格/角点 —— 否则取整会把
		// 上限顶破几个百分点（例：pow(8e6, 1/3) ≈ 200 时实际得到 203³ = 8,365,427）。
		//
		// 为什么不能只靠引擎自己的兜底：FMarchingCubes::SetDimensions 在
		// MaxDim > SafetyMaxDimension（4096）时会静默改写 CubeSize，而 Implicit
		// 闭包捕获的是我们自己算的 CellSize，两者错位后求值点与顶点就落在不同
		// 空间，只表现为「矿形怪」。
		constexpr int32 MaxCellsPerAxis = 512;
		constexpr int64 MaxTotalCorners = 8000000;
		double CellSize = 2.0 * double(MinRadiusCm) / double(Params.CellsPerFeature);
		const FVector PaddedExtent(
			(Box.Max.X - Box.Min.X) + 2.0 * CellSize,
			(Box.Max.Y - Box.Min.Y) + 2.0 * CellSize,
			(Box.Max.Z - Box.Min.Z) + 2.0 * CellSize);
		const double ExtentMax = PaddedExtent.GetMax();
		CellSize = FMath::Max(CellSize, ExtentMax / double(MaxCellsPerAxis - 2));
		CellSize = FMath::Max(CellSize, ExtentMax
			/ (FMath::Pow(static_cast<double>(MaxTotalCorners), 1.0 / 3.0) - 3.0));

		// 外扩一格，并把盒子拉到整数格。引擎按 (int)(Width / CubeSize) + 1 定**格数**
		// （截断）：Width == CellCount * CellSize 在 double 下可能被截成 CellCount - 1，
		// 最外一圈就会被裁掉。这里多给一格，无论截断与否都留有余量。
		//
		// 余量安全的前提在**别的模块**：引擎的角点会越过我们采样的 CornerCount，
		// 越过的那几层由 Implicit 的 Clamp 落回最后一个已采样平面；而这一平面在
		// **本簇**矿囊的包围盒之外，SampleElementNormAtNormalized 在那里返回 0 是
		// 因为 PlanetGeologyField 的 PodInfluence 在 d ≥ r 处**严格**返回 0
		// （无浮点残渣）。低于等值线（0 < IsoValue）就不会有穿越 —— 这里依赖
		// 那个前提，改动 PodInfluence 的边界行为时必须回来复核。
		//
		// 注意范围：簇之间只保证 AABB 不相交，不保证相隔一格。同一元素**其它簇**的
		// 矿囊可以离本簇盒子不到一格，其包围盒与该「最后采样平面」相交时那一层角点
		// 场值就不为 0 —— 封闭性结论只对本簇矿囊成立。默认预设（半径集中在
		// 0.038~0.05）下这很罕见，但 PodRadius / PodCount 调大会让簇更多、格边更宽，
		// 概率随之上长 —— README 里「先把 PodRadius 调大」的建议因此不是零代价的。
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

		// 由上面两条硬上限推出：每轴格数 ≤ 512、角点 ≤ 513，远低于引擎的 4096。
		checkf(FMath::Max3(CellCount.X, CellCount.Y, CellCount.Z) < 4096,
			TEXT("矿囊簇格数 %d×%d×%d 触及 FMarchingCubes 的 SafetyMaxDimension"),
			CellCount.X, CellCount.Y, CellCount.Z);

		// 角点数用 int64 求积再收窄成 int32。这条 checkf 在构造上不可能触发：
		// 经 MaxTotalCorners 钳制后每轴角点 ≤ 200，乘积 ≤ 200³ = 8,000,000，
		// 远低于 int32 上限。留着是防将来有人改上限或外扩量时静默溢出。
		const int64 TotalCorners64 =
			int64(CornerCount.X) * int64(CornerCount.Y) * int64(CornerCount.Z);
		checkf(TotalCorners64 <= int64(TNumericLimits<int32>::Max()),
			TEXT("矿囊簇角点数 %lld 溢出 int32"), TotalCorners64);
		const int32 TotalCorners = int32(TotalCorners64);

		// 本簇涉及哪些元素
		TSet<int32> ElementSet;
		for (const int32 RegionIndex : Members)
		{
			ElementSet.Add(Regions[RegionIndex].ElementIndex);
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
			// ElementIndex 来自 FRegionInstance，取值恒在 [0, ElementCount)，而
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
			// 一个 (簇, 元素) 条目 = 该元素参与的一次分簇。
			++Out.ClustersByElement[Result.ElementIndex];
		}
	}

	// 顶点顺序里没有用到的元素保持空网格（观察体按元素数校验）。
	Out.BuildSeconds = FPlatformTime::Seconds() - StartSeconds;
}
