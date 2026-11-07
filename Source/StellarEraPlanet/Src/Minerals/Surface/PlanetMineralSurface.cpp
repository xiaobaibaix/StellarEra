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

	// 格边 = 最大轴长：相交对至少共享一格，两边的包围盒都覆盖它，再逐个判相交就不会漏 union。
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

	// 根归一成簇内最小下标：TMap 迭代顺序不是机制，不归一化两次构建的簇序（进而顶点顺序）会跟着哈希走。
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
	Out.CornersByElement.Reset();
	Out.GridPoints = 0;
	Out.SampleSeconds = 0.0;
	Out.MarchSeconds = 0.0;
	Out.JobSeconds = 0.0;
	Out.PairCount = 0;
	Out.CappedPairs = 0;
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
	Out.CornersByElement.SetNum(ElementCount);

	// --- 1. 按元素收集区域（厘米空间） ---
	// 必须按元素分别分簇：跨元素分簇会让一个元素的包围盒背别人的空域（长脉把途经的片层、浸染颗粒全并进来），
	// 而簇内最小特征半径把格边压到最细，每个（簇, 元素）对都撞满 8e6 角点上限（实测 2.68 亿角点只换 476 万三角形）。
	TArray<TArray<int32>> RegionIndicesByElement;
	RegionIndicesByElement.SetNum(ElementCount);

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
			const int32 RegionIndex = Regions.AddDefaulted();
			FRegionInstance& Instance = Regions[RegionIndex];
			Instance.ElementIndex = ElementIndex;
			Instance.BoundsCm = FBox(
				Region.Bounds.Min * RadiusCm, Region.Bounds.Max * RadiusCm);
			Instance.RadiusCm = Region.CharacteristicRadius * float(RadiusCm);
			RegionIndicesByElement[ElementIndex].Add(RegionIndex);
		}
	}
	if (Regions.Num() == 0)
	{
		Out.BuildSeconds = FPlatformTime::Seconds() - StartSeconds;
		return;
	}

	// --- 2. 每个元素各自分簇：一对（元素, 簇）= 一个并行建面任务 ---
	struct FBuildJob
	{
		int32 ElementIndex = INDEX_NONE;
		TArray<int32> Members;
	};
	TArray<FBuildJob> Jobs;
	for (int32 ElementIndex = 0; ElementIndex < ElementCount; ++ElementIndex)
	{
		const TArray<int32>& OwnRegionIndices = RegionIndicesByElement[ElementIndex];
		if (OwnRegionIndices.Num() == 0)
		{
			continue;
		}

		TArray<FBox> OwnBounds;
		OwnBounds.Reserve(OwnRegionIndices.Num());
		for (const int32 RegionIndex : OwnRegionIndices)
		{
			OwnBounds.Add(Regions[RegionIndex].BoundsCm);
		}

		// 传进去的是本元素自己的包围盒列表，所以簇划分里不可能出现别的元素。
		TArray<int32> LocalRoots;
		ClusterRegionBounds(OwnBounds, LocalRoots);

		TMap<int32, TArray<int32>> MembersByLocalRoot;
		for (int32 LocalIndex = 0; LocalIndex < LocalRoots.Num(); ++LocalIndex)
		{
			MembersByLocalRoot.FindOrAdd(LocalRoots[LocalIndex]).Add(LocalIndex);
		}

		// 根 = 簇内最小本地下标（ClusterRegionBounds 已归一），升序即确定性顺序。
		TArray<int32> SortedRoots;
		MembersByLocalRoot.GetKeys(SortedRoots);
		SortedRoots.Sort();

		for (const int32 Root : SortedRoots)
		{
			FBuildJob& Job = Jobs.AddDefaulted_GetRef();
			Job.ElementIndex = ElementIndex;
			for (const int32 LocalIndex : MembersByLocalRoot.FindChecked(Root))
			{
				Job.Members.Add(OwnRegionIndices[LocalIndex]);
			}
		}
	}
	if (Jobs.Num() == 0)
	{
		Out.BuildSeconds = FPlatformTime::Seconds() - StartSeconds;
		return;
	}

	// --- 3. 每个任务一套局部栅格，并行抽面 ---
	struct FJobResult
	{
		int32 ElementIndex = INDEX_NONE;
		FPlanetMineralMesh Mesh;
		int32 GridPoints = 0;
		bool bCapped = false;
		double SampleSeconds = 0.0;
		double MarchSeconds = 0.0;
		double JobSeconds = 0.0;
	};
	TArray<FJobResult> JobResults;
	JobResults.SetNum(Jobs.Num());

	ParallelFor(Jobs.Num(), [&](int32 JobIndex)
	{
		const FBuildJob& Job = Jobs[JobIndex];
		FJobResult& Result = JobResults[JobIndex];
		const double JobStartSeconds = FPlatformTime::Seconds();

		FBox Box(ForceInit);
		float MinRadiusCm = TNumericLimits<float>::Max();
		for (const int32 RegionIndex : Job.Members)
		{
			Box += Regions[RegionIndex].BoundsCm;
			MinRadiusCm = FMath::Min(MinRadiusCm, Regions[RegionIndex].RadiusCm);
		}
		if (MinRadiusCm <= 0.f || !Box.IsValid)
		{
			return;
		}
		Result.ElementIndex = Job.ElementIndex;

		// 先按 CellsPerFeature 定基准格边，再钳到每轴 ≤ 512 格、单任务 ≤ 8e6 角点；触发时放大格边（分辨率下降），绝不跳过。
		// 格数 = ceil(extent/CellSize) + 2、角点 = 格数 + 1，所以除数先减 3，否则取整会顶破（pow(8e6,1/3)≈200 实际得 203³）。不靠引擎兜底：SetDimensions 在 MaxDim > 4096 时会静默改写 CubeSize。
		constexpr int32 MaxCellsPerAxis = 512;
		constexpr int64 MaxTotalCorners = 8000000;
		const double BaseCellSize = 2.0 * double(MinRadiusCm) / double(Params.CellsPerFeature);
		double CellSize = BaseCellSize;
		const FVector PaddedExtent(
			(Box.Max.X - Box.Min.X) + 2.0 * CellSize,
			(Box.Max.Y - Box.Min.Y) + 2.0 * CellSize,
			(Box.Max.Z - Box.Min.Z) + 2.0 * CellSize);
		const double ExtentMax = PaddedExtent.GetMax();
		CellSize = FMath::Max(CellSize, ExtentMax / double(MaxCellsPerAxis - 2));
		CellSize = FMath::Max(CellSize, ExtentMax
			/ (FMath::Pow(static_cast<double>(MaxTotalCorners), 1.0 / 3.0) - 3.0));
		Result.bCapped = CellSize > BaseCellSize * (1.0 + 1e-12);

		// 外扩一格并把盒子拉到整数格：引擎按 (int)(Width / CubeSize) + 1 定格数（截断），Width == CellCount * CellSize 可能被截成 CellCount - 1，最外一圈就被裁掉。
		// 余量安全依赖各分布的零支持域 ⊆ Region.Bounds（Bounds 外恒 0），越界角点由 Implicit 的 Clamp 落回最后一个已采样平面。
		// 改任何分布的 bounds 算法（片层外扩、脉分段包围盒、浸染抖动后半径）都必须复核：算小会在观测视图上开洞；簇间只保证 AABB 不相交，同元素其它簇可能落进本簇余量。
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

		// 角点数用 int64 求积再收窄：经 MaxTotalCorners 钳制后每轴 ≤ 200，乘积 ≤ 8e6，不可能溢出；留着防将来改上限时静默溢出。
		const int64 TotalCorners64 =
			int64(CornerCount.X) * int64(CornerCount.Y) * int64(CornerCount.Z);
		checkf(TotalCorners64 <= int64(TNumericLimits<int32>::Max()),
			TEXT("矿囊簇角点数 %lld 溢出 int32"), TotalCorners64);
		const int32 TotalCorners = int32(TotalCorners64);

		// 稀疏剪枝跳过的角点必须留 0 —— 那里场值本来就是 0，清零与求值是同一个结果。
		TArray<float> Norms;
		Norms.SetNumZeroed(TotalCorners);

		const double SampleStartSeconds = FPlatformTime::Seconds();
		int32 EvaluatedCorners = 0;

		if (Params.bSampleEveryCorner)
		{
			for (int32 Z = 0; Z < CornerCount.Z; ++Z)
			{
				const double PZ = BoxMin.Z + double(Z) * CellSize;
				for (int32 Y = 0; Y < CornerCount.Y; ++Y)
				{
					const double PY = BoxMin.Y + double(Y) * CellSize;
					float* const Row = Norms.GetData() + (Z * CornerCount.Y + Y) * CornerCount.X;
					for (int32 X = 0; X < CornerCount.X; ++X)
					{
						const FVector P(BoxMin.X + double(X) * CellSize, PY, PZ);
						Row[X] = Field.SampleElementNormAtNormalized(Job.ElementIndex, P / RadiusCm);
					}
				}
			}
			EvaluatedCorners = TotalCorners;
		}
		else
		{
			// 块掩码：与本簇任一区域包围盒（外扩一格）不相交的块整块恒为 0，角点一次都不用求，依据同为「零支持域 ⊆ Region.Bounds」。
			// 块边用 8 格：太小则掩码/循环开销摊不薄，太大则薄片（常常只有一两格厚）周围的空块会被算进来。经验值。
			constexpr int32 BlockCells = 8;
			const FIntVector BlockCount(
				FMath::DivideAndRoundUp(CellCount.X, BlockCells),
				FMath::DivideAndRoundUp(CellCount.Y, BlockCells),
				FMath::DivideAndRoundUp(CellCount.Z, BlockCells));

			TArray<uint8> BlockMask;
			BlockMask.SetNumZeroed(BlockCount.X * BlockCount.Y * BlockCount.Z);

			// 角点下标 → 块下标。最后一块拥有到最后一个角点，所以超出的下标并回末块。
			auto BlockOfCorner = [BlockCells](int32 CornerIndex, int32 BlockCountAlongAxis)
			{
				return FMath::Min(CornerIndex / BlockCells, BlockCountAlongAxis - 1);
			};

			for (const int32 RegionIndex : Job.Members)
			{
				const FBox& RegionBounds = Regions[RegionIndex].BoundsCm;
				// 外扩一格：区域边界那一层角点也要算，等值面才封得住口。
				const FIntVector FirstCorner(
					FMath::Clamp(FMath::FloorToInt((RegionBounds.Min.X - BoxMin.X) / CellSize),
						0, CornerCount.X - 1),
					FMath::Clamp(FMath::FloorToInt((RegionBounds.Min.Y - BoxMin.Y) / CellSize),
						0, CornerCount.Y - 1),
					FMath::Clamp(FMath::FloorToInt((RegionBounds.Min.Z - BoxMin.Z) / CellSize),
						0, CornerCount.Z - 1));
				const FIntVector LastCorner(
					FMath::Clamp(FMath::CeilToInt((RegionBounds.Max.X - BoxMin.X) / CellSize),
						0, CornerCount.X - 1),
					FMath::Clamp(FMath::CeilToInt((RegionBounds.Max.Y - BoxMin.Y) / CellSize),
						0, CornerCount.Y - 1),
					FMath::Clamp(FMath::CeilToInt((RegionBounds.Max.Z - BoxMin.Z) / CellSize),
						0, CornerCount.Z - 1));

				const FIntVector FirstBlock(
					BlockOfCorner(FirstCorner.X, BlockCount.X),
					BlockOfCorner(FirstCorner.Y, BlockCount.Y),
					BlockOfCorner(FirstCorner.Z, BlockCount.Z));
				const FIntVector LastBlock(
					BlockOfCorner(LastCorner.X, BlockCount.X),
					BlockOfCorner(LastCorner.Y, BlockCount.Y),
					BlockOfCorner(LastCorner.Z, BlockCount.Z));

				for (int32 BZ = FirstBlock.Z; BZ <= LastBlock.Z; ++BZ)
				{
					for (int32 BY = FirstBlock.Y; BY <= LastBlock.Y; ++BY)
					{
						const int32 RowBase = (BZ * BlockCount.Y + BY) * BlockCount.X;
						for (int32 BX = FirstBlock.X; BX <= LastBlock.X; ++BX)
						{
							BlockMask[RowBase + BX] = 1;
						}
					}
				}
			}

			// 块 b 拥有角点 [b*B, b*B+B)，最后一块到 CornerCount-1 —— 每个角点恰好被写一次。
			for (int32 BZ = 0; BZ < BlockCount.Z; ++BZ)
			{
				const int32 Z0 = BZ * BlockCells;
				const int32 Z1 = (BZ + 1 == BlockCount.Z) ? CornerCount.Z : Z0 + BlockCells;
				for (int32 BY = 0; BY < BlockCount.Y; ++BY)
				{
					const int32 Y0 = BY * BlockCells;
					const int32 Y1 = (BY + 1 == BlockCount.Y) ? CornerCount.Y : Y0 + BlockCells;
					const int32 MaskRowBase = (BZ * BlockCount.Y + BY) * BlockCount.X;
					for (int32 BX = 0; BX < BlockCount.X; ++BX)
					{
						if (BlockMask[MaskRowBase + BX] == 0)
						{
							continue;
						}
						const int32 X0 = BX * BlockCells;
						const int32 X1 = (BX + 1 == BlockCount.X) ? CornerCount.X : X0 + BlockCells;

						for (int32 Z = Z0; Z < Z1; ++Z)
						{
							const double PZ = BoxMin.Z + double(Z) * CellSize;
							for (int32 Y = Y0; Y < Y1; ++Y)
							{
								const double PY = BoxMin.Y + double(Y) * CellSize;
								float* const Row =
									Norms.GetData() + (Z * CornerCount.Y + Y) * CornerCount.X;
								for (int32 X = X0; X < X1; ++X)
								{
									const FVector P(BoxMin.X + double(X) * CellSize, PY, PZ);
									Row[X] = Field.SampleElementNormAtNormalized(
										Job.ElementIndex, P / RadiusCm);
								}
							}
						}
						EvaluatedCorners += (X1 - X0) * (Y1 - Y0) * (Z1 - Z0);
					}
				}
			}
		}
		Result.SampleSeconds = FPlatformTime::Seconds() - SampleStartSeconds;

		const double MarchStartSeconds = FPlatformTime::Seconds();

		UE::Geometry::FMarchingCubes Marching;
		Marching.Bounds.Min = UE::Math::TVector<double>(BoxMin.X, BoxMin.Y, BoxMin.Z);
		Marching.Bounds.Max = UE::Math::TVector<double>(BoxMax.X, BoxMax.Y, BoxMax.Z);
		Marching.CubeSize = CellSize;
		Marching.IsoValue = double(Params.IsoValue);
		// 角点值缓存：不开的话引擎每格重算 4 次 Implicit（三线性插值，8 次读取 + 7 次 Lerp），大盒子上与真正求值同量级。输出逐位不变。
		Marching.bEnableValueCaching = true;
		// FMarchingCubes 的并行路径用 atomic 分配顶点下标、按线程完成顺序填数组，顶点顺序不确定；
		// 观测视图要求两次构建逐顶点一致，所以任务内串行，并行度由外层「按任务并行」提供。
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
		Result.MarchSeconds = FPlatformTime::Seconds() - MarchStartSeconds;

		Result.GridPoints = EvaluatedCorners;
		Result.Mesh.VerticesCm.Reserve(Marching.Vertices.Num());
		for (const UE::Math::TVector<double>& Vertex : Marching.Vertices)
		{
			// 球外角点恒 0、内侧为正：插值会把顶点落到球外，逐顶点径向夹回，因为观察体的半透明壳画在 R 上。
			const double LengthCm = Vertex.Size();
			const double ScaleFactor = LengthCm > RadiusCm ? RadiusCm / LengthCm : 1.0;
			Result.Mesh.VerticesCm.Add(FVector3f(
				float(Vertex.X * ScaleFactor),
				float(Vertex.Y * ScaleFactor),
				float(Vertex.Z * ScaleFactor)));
		}

		Result.Mesh.Triangles.Reserve(Marching.Triangles.Num() * 3);
		for (const UE::Geometry::FIndex3i& Triangle : Marching.Triangles)
		{
			Result.Mesh.Triangles.Add(Triangle.A);
			Result.Mesh.Triangles.Add(Triangle.B);
			Result.Mesh.Triangles.Add(Triangle.C);
		}

		int32 Occupied = 0;
		for (const float Norm : Norms)
		{
			Occupied += Norm > Params.IsoValue ? 1 : 0;
		}
		Result.Mesh.OccupiedCells = Occupied;

		Result.JobSeconds = FPlatformTime::Seconds() - JobStartSeconds;
	});

	// --- 4. 合并各任务结果：顶点下标要按已合并的数量偏移 ---
	// 顺序 = 任务顺序（元素升序 × 簇升序），所以顶点顺序由数据决定，不跟线程完成顺序走。
	for (const FJobResult& Result : JobResults)
	{
		if (Result.ElementIndex == INDEX_NONE)
		{
			continue;
		}
		++Out.PairCount;
		Out.CappedPairs += Result.bCapped ? 1 : 0;
		Out.SampleSeconds += Result.SampleSeconds;
		Out.MarchSeconds += Result.MarchSeconds;
		Out.JobSeconds += Result.JobSeconds;

		// ElementIndex 恒在 [0, ElementCount)，MeshesByElement 长度就是 ElementCount —— 无需再判越界。
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
		Out.CornersByElement[Result.ElementIndex] += Result.GridPoints;
		++Out.ClustersByElement[Result.ElementIndex];
	}

	// 没有矿囊的元素保持空网格（观察体按元素数校验）。
	Out.BuildSeconds = FPlatformTime::Seconds() - StartSeconds;
}
