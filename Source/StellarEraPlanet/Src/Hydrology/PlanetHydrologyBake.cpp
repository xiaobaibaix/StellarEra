// 水文烘焙：Priority-Flood + ε 填洼，4 邻接汇流，河道下切，湖泊与河流特征提取。

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
	 * 手写二叉最小堆。
	 *
	 * 不用 Algo::HeapPush：那个是**最大堆语义**（比较器为「大于」时先弹最大的），
	 * 这里最容易被写反的就是方向，写在手上 30 行反而更难错，也少一个 API 版本风险。
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
	 * 有内容的格。
	 *
	 * 四周各留一圈幽灵格，只为让双线性采样在面边上不需要分支；它们不是拓扑的一部分
	 * —— 填洼、汇流、湖泊与河流都只在内部格上进行，事后幽灵格从相邻的内部格复制。
	 */
	FORCEINLINE bool IsInterior(int32 X, int32 Y, int32 Resolution)
	{
		return X >= PlanetHydrologyGrid::Padding && X <= Resolution
			&& Y >= PlanetHydrologyGrid::Padding && Y <= Resolution;
	}
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

	// --- 2. 4 邻接表 ---
	//
	// 每格 4 个槽（-1 = 空），共 4 × 6 × (N+2)² × 4 B —— N = 512 时约 25 MB，
	// 烘焙结束即释放。
	//
	// 跨面那一半靠「把格坐标外插一格 → 求方向 → 找最近的格」得到。相邻面的边界格与
	// 外插出来的方向只差半格，即使最近邻偶尔夹错 1 格，水位也只是换了一条几乎重合的
	// 路传播。
	//
	// 构造完必须**对称化**：填洼只往「未访问的邻居」推，邻接不对称会让某些格永远
	// 不被访问到，表现为湖里出现几个水位恒为 FLT_MAX 的洞。
	//
	// 幽灵格不参与：它们只用于采样，把它们当节点会让邻接表在面边上多出一堆
	// 「只进不出」的单向边（谁也不知道一度被谁指到）。它们的事在第 8 步统一处理。
	static const int32 NeighborOffsets[4][2] = { {-1, 0}, {1, 0}, {0, -1}, {0, 1} };

	TArray<int32> Neighbors;
	Neighbors.Init(INDEX_NONE, CellCount * 4);
	for (int32 Cell = 0; Cell < CellCount; ++Cell)
	{
		int32 Face = 0, X = 0, Y = 0;
		PlanetHydrologyGrid::DecodeCell(Cell, Resolution, Face, X, Y);
		for (int32 Side = 0; Side < 4; ++Side)
		{
			const FVector Dir = PlanetBiomeCubemap::FaceUVToDirection(
				Face,
				PlanetHydrologyGrid::CellToU(X + NeighborOffsets[Side][0], Resolution),
				PlanetHydrologyGrid::CellToU(Y + NeighborOffsets[Side][1], Resolution));
			const int32 Target = PlanetHydrologyGrid::DirectionToCell(Dir, Resolution);
			Neighbors[Cell * 4 + Side] = (Target == Cell) ? INDEX_NONE : Target;
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
			for (int32 Side = 0; Side < 4; ++Side)
			{
				const int32 Target = Slots[Cell * 4 + Side];
				if (Target == INDEX_NONE || InteriorMask[Target] == 0)
				{
					continue;
				}
				int32* TargetSlots = Slots + Target * 4;
				bool bReciprocated = false;
				for (int32 Index = 0; Index < 4; ++Index)
				{
					bReciprocated |= (TargetSlots[Index] == Cell);
				}
				if (bReciprocated)
				{
					continue;
				}
				for (int32 Index = 0; Index < 4; ++Index)
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

	// --- 3. 海洋 = 海平面以下的**最大连通块** ---
	//
	// 为什么不是「所有低于海平面的格都当种子」：被碗缘围住的洼地底部同样可以低于
	// 海平面，却不与外海连通。全当种子会让它抬到海面高度，而正确的水位是它的
	// **溢出高度**（碗缘）—— 湖泊因此永远长不出来。只把与外海连通的那一块当海，
	// 其余低于海平面的闭塞盆地交给填洼抬到溢出高度，成为内陆湖（见设计 §9.1）。
	//
	// 取最大块而不是「全部」也保证了确定性：地形给定时连通块的大小是确定的。
	TArray<uint8> BelowSea;
	BelowSea.SetNumUninitialized(CellCount);
	for (int32 Cell = 0; Cell < CellCount; ++Cell)
	{
		BelowSea[Cell] = (InteriorMask[Cell] != 0 && Height[Cell] < SeaLevelRadius) ? 1 : 0;
	}

	TArray<uint8> ComponentSeen;
	ComponentSeen.Init(0, CellCount);
	TArray<int32> SeedCells;
	TArray<int32> ComponentCells;
	TArray<int32> ComponentStack;
	for (int32 Start = 0; Start < CellCount; ++Start)
	{
		if (BelowSea[Start] == 0 || ComponentSeen[Start] != 0)
		{
			continue;
		}
		ComponentCells.Reset();
		ComponentStack.Reset();
		ComponentStack.Add(Start);
		ComponentSeen[Start] = 1;

		while (ComponentStack.Num() > 0)
		{
			const int32 Cell = ComponentStack.Pop(EAllowShrinking::No);
			ComponentCells.Add(Cell);
			for (int32 Side = 0; Side < 4; ++Side)
			{
				const int32 Next = Neighbors[Cell * 4 + Side];
				if (Next == INDEX_NONE || BelowSea[Next] == 0 || ComponentSeen[Next] != 0)
				{
					continue;
				}
				ComponentSeen[Next] = 1;
				ComponentStack.Add(Next);
			}
		}

		if (ComponentCells.Num() > SeedCells.Num())
		{
			Swap(SeedCells, ComponentCells);
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

	// 种子：外海。海面本身作为它们的起始水位 —— 海是平的。
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

		for (int32 Side = 0; Side < 4; ++Side)
		{
			const int32 Next = Neighbors[Entry.Cell * 4 + Side];
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

	// --- 5. 流向（D4）：选水位最低的邻居 ---
	//
	// 4 邻接而不是 8：水文场每面 512 格，1 km 半径的行星上一格在厘米量级，
	// 4 邻接的「锯齿」看不出来，而 8 邻接要把跨面邻接表翻一倍。
	TArray<int32> FlowTarget;
	FlowTarget.Init(INDEX_NONE, CellCount);
	for (int32 Cell = 0; Cell < CellCount; ++Cell)
	{
		if (InteriorMask[Cell] == 0 || Height[Cell] < SeaLevelRadius)
		{
			continue;  // 海洋格是水体本身，不产生河流
		}
		int32 Best = INDEX_NONE;
		float BestWater = Water[Cell];
		for (int32 Side = 0; Side < 4; ++Side)
		{
			const int32 Next = Neighbors[Cell * 4 + Side];
			if (Next != INDEX_NONE && InteriorMask[Next] != 0 && Water[Next] < BestWater)
			{
				BestWater = Water[Next];
				Best = Next;
			}
		}
		FlowTarget[Cell] = Best;  // 严格小于 ⇒ 不会成环
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
		const bool bOcean = Water[Cell] <= SeaLevelRadius + 1e-3f;
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
	// 双线性采样落在面边上时会读到这一圈，值必须与邻面一致，接缝才不会跳。
	// 复制源取邻接表里第一个内部邻居 —— 它离幽灵格只差一格，接缝两侧因此最多差
	// 一格的量（实测跨面跳变 < 0.1 体素，BakeRiver 的阈值是 2）。不用反查方向：
	// 角上的反查会被夹回本面，退化成自环。
	for (int32 Cell = 0; Cell < CellCount; ++Cell)
	{
		if (InteriorMask[Cell] != 0)
		{
			continue;
		}
		int32 Source = INDEX_NONE;
		for (int32 Side = 0; Side < 4; ++Side)
		{
			const int32 Next = Neighbors[Cell * 4 + Side];
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
	// 「湖」= 水位高于地形的连通块，海被排除在外（海的水位严格等于海面）。
	// 每片湖记三个量：湖心方向（各格方向的平均）、水位（块内最大水位）、角半径
	// （格到湖心的最大夹角 + 2 格余量 —— 水面片刻意探进岸线，见设计 §6.1）。
	TArray<uint8> LakeVisited;
	LakeVisited.Init(0, CellCount);
	TArray<FPlanetLakeBasin> Lakes;
	TArray<int32> Stack;
	TArray<int32> MemberCells;
	const double CellAngle = (PI * 0.5) / double(Resolution);   // 一格的角距（近似）

	const auto CellDirectionAt = [&](int32 Cell)
	{
		int32 Face = 0, X = 0, Y = 0;
		PlanetHydrologyGrid::DecodeCell(Cell, Resolution, Face, X, Y);
		return PlanetHydrologyGrid::CellDirection(Face, X, Y, Resolution);
	};
	const auto IsLakeCell = [&](int32 Cell)
	{
		const bool bOcean = Water[Cell] <= SeaLevelRadius + 1e-3f;
		return !bOcean
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

			for (int32 Side = 0; Side < 4; ++Side)
			{
				const int32 Next = Neighbors[Cell * 4 + Side];
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

		// 第一趟：湖心与水位。
		FVector DirSum = FVector::ZeroVector;
		double MaxWater = -TNumericLimits<double>::Max();
		for (const int32 Cell : MemberCells)
		{
			DirSum += CellDirectionAt(Cell);
			MaxWater = FMath::Max(MaxWater, double(Water[Cell]));
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
		Basin.WaterLevelRadius = float(MaxWater);
		Basin.AngularRadius = float(MaxAngle + 2.0 * CellAngle);
		Lakes.Add(Basin);
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

		Path.Reset();
		int32 Cell = Start;
		constexpr int32 MaxCellsPerRiver = 256;
		while (Cell != INDEX_NONE && IsRiver[Cell] != 0 && Path.Num() < MaxCellsPerRiver)
		{
			Path.Add(Cell);
			Cell = FlowTarget[Cell];
		}
		if (Path.Num() < 4)
		{
			continue;   // 太短的不算河
		}

		FPlanetRiverPolyline Polyline;
		for (const int32 PathCell : Path)
		{
			int32 Face = 0, X = 0, Y = 0;
			PlanetHydrologyGrid::DecodeCell(PathCell, Resolution, Face, X, Y);

			FPlanetRiverPoint Point;
			Point.Dir = PlanetHydrologyGrid::CellDirection(Face, X, Y, Resolution);
			Point.WaterLevelRadius = Water[PathCell];
			Point.WidthVoxels = FMath::Clamp(
				FMath::Sqrt(float(Accum[PathCell]) / MinFlow) * Params.RiverWidthScale, 1.f, 8.f);
			Polyline.Points.Add(Point);
		}
		Rivers.Add(MoveTemp(Polyline));
	}

	return FPlanetHydrologyField::Create(
		Resolution,
		SeaLevelRadius,
		Params.MaxCarveDepth,
		MoveTemp(Water),
		MoveTemp(CarveDepth),
		MoveTemp(RiverStrength),
		MoveTemp(Lakes),
		MoveTemp(Rivers));
}
