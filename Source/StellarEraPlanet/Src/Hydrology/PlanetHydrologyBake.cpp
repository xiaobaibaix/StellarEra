// 水文烘焙：Priority-Flood + ε 填洼，8 邻接汇流，河道下切，湖泊与河流特征提取。

#include "PlanetHydrologyBake.h"

#include "Async/ParallelFor.h"
#include "PlanetBiomeCubemap.h"
#include "PlanetHydrologyGrid.h"

namespace
{
	/** 最小堆条目：按 Level 升序弹出。 */
	struct FFloodEntry
	{
		double Level = 0.0;
		int32 Cell = INDEX_NONE;
	};

	/**
	 * 手写二叉最小堆。不用 Algo::HeapPush：那个是最大堆语义，方向最容易写反，
	 * 写在手上反而更难错，也少一个 API 版本风险。
	 */
	struct FFloodHeap
	{
		TArray<FFloodEntry> Items;

		bool IsEmpty() const { return Items.Num() == 0; }

		void Push(double Level, int32 Cell)
		{
			int32 Index = static_cast<int32>(Items.AddUninitialized());
			while (Index > 0)
			{
				const int32 Parent = (Index - 1) / 2;
				if (Items[Parent].Level <= Level)
				{
					break;
				}
				Items[Index] = Items[Parent];
				Index = Parent;
			}
			Items[Index].Level = Level;
			Items[Index].Cell = Cell;
		}

		FFloodEntry Pop()
		{
			const FFloodEntry Top = Items[0];
			const FFloodEntry Last = Items.Pop(EAllowShrinking::No);
			const int32 Count = Items.Num();
			if (Count > 0)
			{
				int32 Index = 0;
				while (true)
				{
					int32 Child = Index * 2 + 1;
					if (Child >= Count)
					{
						break;
					}
					if (Child + 1 < Count && Items[Child + 1].Level < Items[Child].Level)
					{
						++Child;
					}
					if (Items[Child].Level >= Last.Level)
					{
						break;
					}
					Items[Index] = Items[Child];
					Index = Child;
				}
				Items[Index] = Last;
			}
			return Top;
		}
	};

	/** 格的状态机。0 = 未入队，1 = 已入队，2 = 已访问。 */
	enum class ECellState : uint8
	{
		Unqueued = 0,
		Queued = 1,
		Visited = 2,
	};

	/**
	 * 有内容的格。四周留一圈幽灵格只为双线性采样在面边上不需要分支；它们不是
	 * 拓扑的一部分，填洼 / 汇流 / 湖 / 河都只在内部格上进行，事后从内部格复制。
	 */
	FORCEINLINE bool IsInterior(int32 X, int32 Y, int32 Resolution)
	{
		return X >= PlanetHydrologyGrid::Padding && X <= Resolution
			&& Y >= PlanetHydrologyGrid::Padding && Y <= Resolution;
	}

	/** 8 邻接的步进：4 个轴向 + 4 个对角。Distance 是格距，D8 的坡度要用。 */
	struct FNeighborStep
	{
		int32 DX;
		int32 DY;
		double Distance;
	};

	constexpr int32 NumNeighbors = 8;
	constexpr double DiagonalDistance = 1.4142135623730951;   // √2

	const FNeighborStep NeighborSteps[NumNeighbors] =
	{
		{ -1, 0, 1.0 }, { 1, 0, 1.0 }, { 0, -1, 1.0 }, { 0, 1, 1.0 },
		{ -1, -1, DiagonalDistance }, { -1, 1, DiagonalDistance },
		{ 1, -1, DiagonalDistance }, { 1, 1, DiagonalDistance },
	};
}

TSharedPtr<FPlanetHydrologyField> FPlanetHydrologyBake::Bake(
	FHeightFn HeightFn,
	const FPlanetHydrologyParams& Params,
	double Radius)
{
	const int32 Resolution = Params.Resolution;
	if (Resolution < 4)
	{
		return nullptr;
	}

	const int32 CellCount = PlanetHydrologyGrid::CellCount(Resolution);
	const float SeaLevelRadius = float(Radius + double(Params.Amplitude) * double(Params.SeaLevel));
	const float MinFlow = FMath::Max(Params.MinRiverFlow, 1.f);
	// MaxCarveDepth 夹到 ≥ 1：小于 1 会让 Clamp 的下界高于上界（断言）。
	const float MaxCarve = FMath::Max(Params.MaxCarveDepth, 1.f);

	// --- 1. 高度栅格（并行；每格一次 HeightFn） ---
	TArray<float> Height;
	Height.SetNumUninitialized(CellCount);
	TArray<uint8> InteriorMask;
	InteriorMask.SetNumUninitialized(CellCount);
	ParallelFor(CellCount, [&](int32 Cell)
	{
		int32 Face = 0, X = 0, Y = 0;
		PlanetHydrologyGrid::DecodeCell(Cell, Resolution, Face, X, Y);
		const FVector Dir = PlanetHydrologyGrid::CellDirection(Face, X, Y, Resolution);
		Height[Cell] = float(Radius + double(Params.Amplitude) * double(HeightFn(Dir)));
		InteriorMask[Cell] = IsInterior(X, Y, Resolution) ? 1 : 0;
	});

	// --- 2. 8 邻接表 ---
	//
	// 每格 8 个槽（-1 = 空）。跨面靠「格坐标外插一格 → 求方向 → 找最近的格」，
	// 对角步进同理，墙角附近不需单独分支；夹错 1 格只让水位换一条近乎重合的路。
	// 构造完必须对称化：填洼只往未访问的邻居推，不对称会让某些格永远不被访问，
	// 表现为湖里出现水位恒为 FLT_MAX 的洞。幽灵格只用于采样，不当节点。
	TArray<int32> Neighbors;
	Neighbors.Init(INDEX_NONE, CellCount * NumNeighbors);
	for (int32 Cell = 0; Cell < CellCount; ++Cell)
	{
		int32 Face = 0, X = 0, Y = 0;
		PlanetHydrologyGrid::DecodeCell(Cell, Resolution, Face, X, Y);
		for (int32 Side = 0; Side < NumNeighbors; ++Side)
		{
			const FVector Dir = PlanetBiomeCubemap::FaceUVToDirection(
				Face,
				PlanetHydrologyGrid::CellToU(X + NeighborSteps[Side].DX, Resolution),
				PlanetHydrologyGrid::CellToU(Y + NeighborSteps[Side].DY, Resolution));
			const int32 Target = PlanetHydrologyGrid::DirectionToCell(Dir, Resolution);
			Neighbors[Cell * NumNeighbors + Side] = (Target == Cell) ? INDEX_NONE : Target;
		}
	}
	{
		int32* const Slots = Neighbors.GetData();
		for (int32 Cell = 0; Cell < CellCount; ++Cell)
		{
			if (InteriorMask[Cell] == 0)
			{
				continue;
			}
			for (int32 Side = 0; Side < NumNeighbors; ++Side)
			{
				const int32 Target = Slots[Cell * NumNeighbors + Side];
				if (Target == INDEX_NONE || InteriorMask[Target] == 0)
				{
					continue;
				}
				int32* TargetSlots = Slots + Target * NumNeighbors;
				bool bReciprocated = false;
				for (int32 Index = 0; Index < NumNeighbors; ++Index)
				{
					bReciprocated |= (TargetSlots[Index] == Cell);
				}
				if (bReciprocated)
				{
					continue;
				}
				for (int32 Index = 0; Index < NumNeighbors; ++Index)
				{
					if (TargetSlots[Index] == INDEX_NONE)
					{
						TargetSlots[Index] = Cell;
						break;
					}
				}
			}
		}
	}

	// --- 3. 海洋 = **所有**低于海平面的格 ---
	//
	// 旧规则「只把最大连通块当海、其余闭塞盆地抬成内陆湖」在程序化地形上会产出
	// 上千片贴着海面的假湖，渲成陆地上糊的一块块亮蓝补丁（碗缘只是噪声级的坎）。
	// 现在低于海平面就是海；真正的湖（盆底高于海面）不受影响，仍由填洼抬到溢出
	// 高度。代价是死海式水体被当成海，程序化行星上可接受。
	TArray<int32> SeedCells;
	SeedCells.Reserve(CellCount / 4);
	TArray<uint8> OceanMask;
	OceanMask.Init(0, CellCount);
	for (int32 Cell = 0; Cell < CellCount; ++Cell)
	{
		if (InteriorMask[Cell] != 0 && Height[Cell] < SeaLevelRadius)
		{
			SeedCells.Add(Cell);
			OceanMask[Cell] = 1;
		}
	}

	// --- 4. Priority-Flood + ε ---
	TArray<float> Water;
	Water.Init(TNumericLimits<float>::Max(), CellCount);
	TArray<ECellState> State;
	State.Init(ECellState::Unqueued, CellCount);
	TArray<int32> PopOrder;
	PopOrder.Reserve(CellCount);
	FFloodHeap Heap;

	// 种子：全部低于海面的格。海面本身作为它们的起始水位 —— 海是平的，
	// 所以它们直接以 SeaLevelRadius 入队，不参与 ε 阶梯（否则同是海面却高低不平）。
	for (const int32 Cell : SeedCells)
	{
		Heap.Push(double(SeaLevelRadius), Cell);
		State[Cell] = ECellState::Queued;
	}
	// 整颗星都高于海平面（内流盆地）：拿最低的一格当种子，水只进湖不出海。
	if (SeedCells.Num() == 0)
	{
		int32 Lowest = INDEX_NONE;
		for (int32 Cell = 0; Cell < CellCount; ++Cell)
		{
			if (InteriorMask[Cell] != 0 && (Lowest == INDEX_NONE || Height[Cell] < Height[Lowest]))
			{
				Lowest = Cell;
			}
		}
		if (Lowest != INDEX_NONE)
		{
			Heap.Push(double(Height[Lowest]), Lowest);
			State[Lowest] = ECellState::Queued;
		}
	}

	// 每格只入队一次：弹出顺序按水位**非递减**，所以第一次入队时的 Level 就是它能
	// 收到的最小水位，之后的推送都不更优。这同时把堆的上限压到格数。
	while (!Heap.IsEmpty())
	{
		const FFloodEntry Entry = Heap.Pop();
		if (State[Entry.Cell] == ECellState::Visited)
		{
			continue;
		}
		State[Entry.Cell] = ECellState::Visited;
		Water[Entry.Cell] = float(Entry.Level);
		PopOrder.Add(Entry.Cell);

		for (int32 Side = 0; Side < NumNeighbors; ++Side)
		{
			const int32 Next = Neighbors[Entry.Cell * NumNeighbors + Side];
			if (Next == INDEX_NONE || InteriorMask[Next] == 0 || State[Next] != ECellState::Unqueued)
			{
				continue;
			}
			State[Next] = ECellState::Queued;
			if (double(Height[Next]) < Entry.Level)
			{
				// 邻居比自己低 → 淹到自己这一层，再抬 ε。ε 就是「排水阶梯」：
				// 湖面因此不是绝对平的，平地也能解出唯一流向。
				Heap.Push(Entry.Level + double(Params.Epsilon), Next);
			}
			else
			{
				Heap.Push(double(Height[Next]), Next);
			}
		}
	}

	// 兜底：邻接若有断裂（理论上不该有），没访问到的格按「没水」处理，
	// 而不是留着 FLT_MAX —— 后者会让水面几何长到 3e38。
	for (int32 Cell = 0; Cell < CellCount; ++Cell)
	{
		if (InteriorMask[Cell] != 0 && State[Cell] != ECellState::Visited)
		{
			Water[Cell] = Height[Cell];
		}
	}

	// --- 5. 流向（D8）：最陡下降 ---
	//
	// 取单位格距水位落差最大的邻居（轴向 1、对角 √2）且要求严格更低。不能只比
	// 「谁水位低」：float32 的 1 ULP 与缓坡上轴向/对角落差同量级，舍入能翻盘，
	// 流线逐格抖动、汇流攒不起来，实测整条干流会消失。严格更低保证不成环；
	// 坡度相等（湖面 ε 阶梯常见）取下标最小者，保证确定性。
	TArray<int32> FlowTarget;
	FlowTarget.Init(INDEX_NONE, CellCount);
	for (int32 Cell = 0; Cell < CellCount; ++Cell)
	{
		if (InteriorMask[Cell] == 0 || OceanMask[Cell] != 0)
		{
			continue;  // 海洋格是水体本身，不产生河流
		}
		int32 Best = INDEX_NONE;
		double BestSlope = 0.0;
		for (int32 Side = 0; Side < NumNeighbors; ++Side)
		{
			const int32 Next = Neighbors[Cell * NumNeighbors + Side];
			if (Next == INDEX_NONE || InteriorMask[Next] == 0 || Water[Next] >= Water[Cell])
			{
				continue;
			}
			const double Slope =
				(double(Water[Cell]) - double(Water[Next])) / NeighborSteps[Side].Distance;
			if (Best == INDEX_NONE || Slope > BestSlope || (Slope == BestSlope && Next < Best))
			{
				BestSlope = Slope;
				Best = Next;
			}
		}
		FlowTarget[Cell] = Best;
	}

	// --- 6. 汇流量：按水位降序累加 ---
	// PopOrder 是水位升序，倒着走就是降序；而流向总是指向更低的水位（更早弹出），
	// 所以倒序扫一遍就够了，不需要额外排序。
	TArray<double> Accum;
	Accum.Init(1.0, CellCount);
	for (int32 Index = PopOrder.Num() - 1; Index >= 0; --Index)
	{
		const int32 Cell = PopOrder[Index];
		const int32 Target = FlowTarget[Cell];
		if (Target != INDEX_NONE)
		{
			Accum[Target] += Accum[Cell];
		}
	}

	// --- 7. 湖 / 海 / 河 的分类与下切 ---
	TArray<float> CarveDepth;
	CarveDepth.Init(0.f, CellCount);
	TArray<float> RiverStrength;
	RiverStrength.Init(0.f, CellCount);
	TArray<uint8> IsRiver;
	IsRiver.Init(0, CellCount);

	for (int32 Cell = 0; Cell < CellCount; ++Cell)
	{
		if (InteriorMask[Cell] == 0)
		{
			continue;
		}
		// 用掩码而不是「水位贴着海面」：海的地位由「地形低于海面」定义，与填洼
		// 解出来的水位数值无关（两者本应一致，但掩码是唯一事实来源）。
		const bool bOcean = OceanMask[Cell] != 0;
		const bool bLake = !bOcean && double(Water[Cell]) > double(Height[Cell]) + double(Params.Epsilon) * 1.5;
		if (bOcean || bLake || Accum[Cell] < double(MinFlow))
		{
			continue;
		}
		// 下切深度按设计 §4.3 的式子：CarveDepth = clamp(1 + 2·√(Accum/MinRiverFlow), 1, Max)。
		// 阈值处就挖 1 体素 —— 「有水但看不见沟」是最难受的观感；4 倍阈值附近饱和。
		const float Carve = FMath::Clamp(
			1.f + 2.f * FMath::Sqrt(float(Accum[Cell]) / MinFlow), 1.f, MaxCarve);
		CarveDepth[Cell] = Carve;
		// 强度只是下切深度的归一化，**不另立一条曲线** —— 两处各自定义迟早对不上。
		RiverStrength[Cell] = FMath::Clamp(
			(Carve - 1.f) / FMath::Max(MaxCarve - 1.f, 1e-3f), 0.f, 1.f);
		IsRiver[Cell] = 1;
	}

	// --- 8. 幽灵格：从相邻的内部格复制 ---
	//
	// 双线性采样落在面边会读到这一圈，值必须与邻面一致。复制源取第一个内部邻居，
	// 接缝两侧最多差一格（实测跨面跳变 < 0.1 体素）。不反查方向：角上会被夹回
	// 本面、退化成自环。
	for (int32 Cell = 0; Cell < CellCount; ++Cell)
	{
		if (InteriorMask[Cell] != 0)
		{
			continue;
		}
		int32 Source = INDEX_NONE;
		for (int32 Side = 0; Side < NumNeighbors; ++Side)
		{
			const int32 Next = Neighbors[Cell * NumNeighbors + Side];
			if (Next != INDEX_NONE && InteriorMask[Next] != 0)
			{
				Source = Next;
				break;
			}
		}
		// 理论上不会走到这里（每个幽灵格至少有一个内部邻居）；真发生了就按地形高度，
		// 不能留 FLT_MAX。
		Water[Cell] = (Source != INDEX_NONE) ? Water[Source] : Height[Cell];
		CarveDepth[Cell] = (Source != INDEX_NONE) ? CarveDepth[Source] : 0.f;
		RiverStrength[Cell] = (Source != INDEX_NONE) ? RiverStrength[Source] : 0.f;
	}

	// --- 9. 湖泊连通块 → FPlanetLakeBasin ---
	//
	// 「湖」= 水位高于地形的连通块，海被排除。每片湖记湖心方向（各格平均）、
	// 水位（块内最小 = 溢出高度）、角半径（最大夹角 + 2 格余量，探进岸线）。
	TArray<uint8> LakeVisited;
	LakeVisited.Init(0, CellCount);
	TArray<FPlanetLakeBasin> Lakes;
	TArray<int32> Stack;
	TArray<int32> MemberCells;
	const double CellAngle = (PI * 0.5) / double(Resolution);   // 一格的角距（近似）

	// 诊断用：每片湖的格数与「湖面 − 海面」（体素）。观感上的「大片蓝色补丁」到底
	// 是贴着海面的内陆海还是山间的真湖，必须能从日志判断，不能只靠截图猜。
	TArray<int32> LakeCellCounts;
	TArray<float> LakeLevelOffsets;
	TArray<float> LakeMaxDepths;   // 湖面 − 块内最低地形（体素）
	TArray<uint8> LakeFloorBelowSea;

	const auto CellDirectionAt = [&](int32 Cell)
	{
		int32 Face = 0, X = 0, Y = 0;
		PlanetHydrologyGrid::DecodeCell(Cell, Resolution, Face, X, Y);
		return PlanetHydrologyGrid::CellDirection(Face, X, Y, Resolution);
	};
	const auto IsLakeCell = [&](int32 Cell)
	{
		return OceanMask[Cell] == 0
			&& double(Water[Cell]) > double(Height[Cell]) + double(Params.Epsilon) * 1.5;
	};

	for (int32 Start = 0; Start < CellCount; ++Start)
	{
		if (InteriorMask[Start] == 0 || !IsLakeCell(Start) || LakeVisited[Start] != 0)
		{
			continue;
		}

		Stack.Reset();
		MemberCells.Reset();
		Stack.Add(Start);
		LakeVisited[Start] = 1;

		while (Stack.Num() > 0)
		{
			const int32 Cell = Stack.Pop(EAllowShrinking::No);
			MemberCells.Add(Cell);

			for (int32 Side = 0; Side < NumNeighbors; ++Side)
			{
				const int32 Next = Neighbors[Cell * NumNeighbors + Side];
				if (Next == INDEX_NONE || InteriorMask[Next] == 0
					|| LakeVisited[Next] != 0 || !IsLakeCell(Next))
				{
					continue;
				}
				LakeVisited[Next] = 1;
				Stack.Add(Next);
			}
		}

		// 太小的水洼不留：它们只会让水面几何多出几十个碎片。
		constexpr int32 MinLakeCells = 8;
		if (MemberCells.Num() < MinLakeCells)
		{
			continue;
		}

		// 第一趟：湖心与水位。水位取块内最小 = 洪水进入这片洼地时的水位（≈ 溢出
		// 高度）；取最大会拿到 ε 阶梯在湖心的累积，大湖上能高出水口好几体素，
		// 水面会浮在碗缘之上。
		FVector DirSum = FVector::ZeroVector;
		double LakeLevel = TNumericLimits<double>::Max();
		double LakeFloor = TNumericLimits<double>::Max();
		for (const int32 Cell : MemberCells)
		{
			DirSum += CellDirectionAt(Cell);
			LakeLevel = FMath::Min(LakeLevel, double(Water[Cell]));
			LakeFloor = FMath::Min(LakeFloor, double(Height[Cell]));
		}
		const FVector Centroid = DirSum.GetSafeNormal();
		if (Centroid.IsNearlyZero())
		{
			continue;
		}

		// 第二趟：最大角距（只在这片湖的格上算，不做全图扫描）。
		double MaxAngle = 0.0;
		for (const int32 Cell : MemberCells)
		{
			const double Cosine = FMath::Clamp(
				double(FVector::DotProduct(Centroid, CellDirectionAt(Cell))), -1.0, 1.0);
			MaxAngle = FMath::Max(MaxAngle, FMath::Acos(Cosine));
		}

		FPlanetLakeBasin Basin;
		Basin.CentroidDir = Centroid;
		Basin.WaterLevelRadius = float(LakeLevel);
		Basin.AngularRadius = float(MaxAngle + double(PlanetLakeCapMarginCells) * CellAngle);
		Lakes.Add(Basin);

		LakeCellCounts.Add(MemberCells.Num());
		LakeLevelOffsets.Add(float(LakeLevel - double(SeaLevelRadius)));
		LakeMaxDepths.Add(float(LakeLevel - LakeFloor));
		LakeFloorBelowSea.Add(LakeFloor < double(SeaLevelRadius) ? 1 : 0);
	}

	// --- 9b. 诊断：海 / 湖的规模与水位分布 ---
	//
	// 一行日志回答：海与湖各占多少格，以及每片湖面比海面高多少（贴海面的假湖与
	// 山间真湖观感完全不同，必须能从日志判断）。
	{
		int32 InteriorCount = 0;
		int32 OceanCount = 0;
		int32 LakeCellCount = 0;
		for (int32 Cell = 0; Cell < CellCount; ++Cell)
		{
			if (InteriorMask[Cell] == 0)
			{
				continue;
			}
			++InteriorCount;
			if (OceanMask[Cell] != 0)
			{
				++OceanCount;
			}
			else if (IsLakeCell(Cell))
			{
				++LakeCellCount;
			}
		}

		const float NearTolerance = FMath::Max(1.f, Params.Amplitude * 0.05f);
		int32 NearSea = 0;
		int32 MidSea = 0;
		for (const float Offset : LakeLevelOffsets)
		{
			if (Offset <= NearTolerance)
			{
				++NearSea;
			}
			else if (Offset <= NearTolerance * 4.f)
			{
				++MidSea;
			}
		}
		int32 Biggest = INDEX_NONE;
		int32 BelowSeaLakes = 0;
		int32 BelowSeaLakeCells = 0;
		int32 VeryShallowLakes = 0;
		int32 ShallowLakes = 0;
		for (int32 Index = 0; Index < LakeCellCounts.Num(); ++Index)
		{
			if (Biggest == INDEX_NONE || LakeCellCounts[Index] > LakeCellCounts[Biggest])
			{
				Biggest = Index;
			}
			if (LakeFloorBelowSea[Index] != 0)
			{
				++BelowSeaLakes;
				BelowSeaLakeCells += LakeCellCounts[Index];
			}
			if (LakeMaxDepths[Index] < 5.f)
			{
				++VeryShallowLakes;
			}
			else if (LakeMaxDepths[Index] < 15.f)
			{
				++ShallowLakes;
			}
		}

		UE_LOG(LogTemp, Display,
			TEXT("[水文] res=%d Amplitude=%.1f SeaLevel=%.2f 海面 %.1f 体素：内部 %d 格，"
				"海洋 %d 格（%.1f%%），湖 %d 格（%.1f%%），湖 %d 片"
				"（观感阈值 %.1f 体素：贴海面 %d 片 / %.1f 体素内 %d 片 / 更高 %d 片），"
				"最大湖 %d 格、高出海面 %.1f 体素；盆底低于海面的湖 %d 片 / %d 格，"
			"最大水深 < 5 体素 %d 片、5–15 体素 %d 片"),
			Resolution, Params.Amplitude, Params.SeaLevel, SeaLevelRadius, InteriorCount,
			OceanCount, 100.0 * double(OceanCount) / double(FMath::Max(InteriorCount, 1)),
			LakeCellCount, 100.0 * double(LakeCellCount) / double(FMath::Max(InteriorCount, 1)),
			Lakes.Num(), NearTolerance, NearSea, NearTolerance * 4.f, NearSea + MidSea,
			LakeLevelOffsets.Num() - NearSea - MidSea,
			Biggest == INDEX_NONE ? 0 : LakeCellCounts[Biggest],
			Biggest == INDEX_NONE ? 0.f : LakeLevelOffsets[Biggest],
			BelowSeaLakes, BelowSeaLakeCells, VeryShallowLakes, ShallowLakes);
	}

	// --- 10. 河流折线 ---
	// 上游计数：没有上游的河格就是源头。
	TArray<int32> UpstreamCount;
	UpstreamCount.Init(0, CellCount);
	for (int32 Cell = 0; Cell < CellCount; ++Cell)
	{
		if (IsRiver[Cell] != 0 && FlowTarget[Cell] != INDEX_NONE && IsRiver[FlowTarget[Cell]] != 0)
		{
			++UpstreamCount[FlowTarget[Cell]];
		}
	}

	TArray<FPlanetRiverPolyline> Rivers;
	TArray<int32> Path;
	for (int32 Start = 0; Start < CellCount; ++Start)
	{
		if (IsRiver[Start] == 0 || UpstreamCount[Start] > 0)
		{
			continue;
		}

		// 不设长度上限：FlowTarget 指向**严格更低**的水位，走一趟必然终止
		// （每步至少降一个 ε 或一段地形落差），截断只会让干流在半山腰断头。
		Path.Reset();
		int32 Cell = Start;
		while (Cell != INDEX_NONE && IsRiver[Cell] != 0)
		{
			Path.Add(Cell);
			Cell = FlowTarget[Cell];
		}
		if (Path.Num() < 4)
		{
			continue;   // 太短的不算河
		}

		// 入水口：while 退出时的 Cell 就是最后一个河格的流向目标。末点水位必须取
		// 它，而不是最后一个河格自身 —— 后者比接收水面高 1–3 体素，河带末端会
		// 悬在空中。
		float EndWaterLevel = Water[Path.Last()];
		if (Cell != INDEX_NONE && InteriorMask[Cell] != 0)
		{
			EndWaterLevel = Water[Cell];
		}

		FPlanetRiverPolyline Polyline;
		for (int32 PathIndex = 0; PathIndex < Path.Num(); ++PathIndex)
		{
			const int32 PathCell = Path[PathIndex];
			int32 Face = 0, X = 0, Y = 0;
			PlanetHydrologyGrid::DecodeCell(PathCell, Resolution, Face, X, Y);

			FPlanetRiverPoint Point;
			Point.Dir = PlanetHydrologyGrid::CellDirection(Face, X, Y, Resolution);
			Point.WaterLevelRadius =
				(PathIndex + 1 == Path.Num()) ? EndWaterLevel : Water[PathCell];
			Point.WidthVoxels = FMath::Clamp(
				FMath::Sqrt(float(Accum[PathCell]) / MinFlow) * Params.RiverWidthScale, 1.f, 8.f);
			Polyline.Points.Add(Point);
		}
		Rivers.Add(MoveTemp(Polyline));
	}

	return FPlanetHydrologyField::Create(
		Resolution,
		SeaLevelRadius,
		// 交给场的是**夹过**的上限：下切循环用的就是 MaxCarve，两边必须同一个数，
		// 否则 MaxCarveDepth < 1 时场的包络会偏小 —— Task 5 的地形值域上界直接吃它。
		MaxCarve,
		MoveTemp(Water),
		MoveTemp(CarveDepth),
		MoveTemp(RiverStrength),
		MoveTemp(Lakes),
		MoveTemp(Rivers));
}
