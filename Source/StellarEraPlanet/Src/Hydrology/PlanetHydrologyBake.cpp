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
	// 每格 8 个槽（-1 = 空），共 8 × 6 × (N+2)² × 4 B —— N = 512 时约 51 MB，
	// 烘焙结束即释放。
	//
	// 跨面那一半靠「把格坐标外插一格 → 求方向 → 找最近的格」得到；对角步进同样只把
	// 两个坐标各外插一格再求方向，因此墙角附近的对角跨面不需要单独分支。相邻面的
	// 边界格与外插出来的方向只差半格，即使最近邻偶尔夹错 1 格，水位也只是换了一条
	// 几乎重合的路传播。
	//
	// 构造完必须**对称化**：填洼只往「未访问的邻居」推，邻接不对称会让某些格永远
	// 不被访问到，表现为湖里出现几个水位恒为 FLT_MAX 的洞。
	//
	// 幽灵格不参与：它们只用于采样，把它们当节点会让邻接表在面边上多出一堆
	// 「只进不出」的单向边（谁也不知道一度被谁指到）。它们的事在第 8 步统一处理。
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
	// 旧规则是「只把低于海平面区域里最大的连通块当海」，其余低于海面的闭塞盆地交给
	// 填洼抬到溢出高度、成为内陆湖（死海那种）。它的水文推导没错，但在程序化地形上
	// 产出的东西不是湖：DA_Terrain_Earth 的真实参数下（res 512、Amplitude 200）是
	// 1131 片湖、占内部格 11.7%，其中 779 片、90% 的湖格盆底都在海面以下，湖面只比
	// 海面高零点几体素 —— 分隔它们与海的「碗缘」只是噪声级别的一道坎。这些水体在海面
	// 附近渲成几百片孤立的球冠，观感就是陆地上糊了一块块亮蓝补丁。
	//
	// 所以判据回到最朴素的一条：**低于海平面就是海**。一颗星只有一个海面。真正的湖
	// （盆底高于海面的洼地）不受影响 —— 它们不参与种子，仍由 Priority-Flood 抬到
	// 溢出高度（见设计 §9.1）。代价是死海式「盆底低于海面却不与海连通」的水体被当成
	// 海；程序化行星上这是可接受的取舍，且更符合直觉（往下走到底就是海）。
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
	// 规格 §3 / §4.3 定的是 D8：8 个候选方向让流线贴着地形的真实走向，而不是被锁到
	// 网格轴上（D4 只有四个方向，缓坡上的流域会被拉成「之」字形）。
	//
	// 选法用 D8 的标准定义：**单位格距的水位落差最大**的邻居（ΔW / Distance，轴向 1、
	// 对角 √2），且要求水位严格更低。不能只比「谁的水位低」：半径 ~1e3 体素时 float32
	// 的 1 ULP ≈ 6e-5 体素，与缓坡上轴向/对角之间的真实落差同量级，对角只要在舍入上
	// 占一个 ULP 的便宜就能翻盘，流线因此逐格抖动、汇流量攒不起来 —— 实测（见修复报告
	// 的变异实验）整条干流消失，BakeRiver 的「谷底必须有下切」直接变红。
	// 除以格距后轴向的坡度天然比对角陡一倍多，判定不再受舍入支配。
	//
	// 严格更低仍然成立（坡度为正 ⇒ 水位严格下降）⇒ 不会成环。
	// 坡度完全相等（湖面 ε 阶梯上常见）时取格下标最小的邻居，保证确定性。
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
	// 「湖」= 水位高于地形的连通块，海被排除在外（海的水位严格等于海面）。
	// 每片湖记三个量：湖心方向（各格方向的平均）、水位（块内最小水位 = 溢出高度）、角半径
	// （格到湖心的最大夹角 + 2 格余量 —— 水面片刻意探进岸线，见设计 §6.1）。
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

		// 第一趟：湖心与水位。
		//
		// 水位取块内**最小**水位 = 洪水进入这片洼地时的水位（≈ 溢出高度 + ε），
		// 而取最大会拿到 ε 阶梯在湖水最深处累积出来的顶点：每向湖心一步抬 ε，
		// 大湖上能比出水口高好几体素，水面几何会浮在碗缘之上（设计 §6.1 要的是
		// 一片平的水面，§9.1 要的是溢出高度）。
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
	// 一行日志回答两个问题：水面里海占多少、湖占多少；以及每片湖的湖面比海面高多少
	// —— 「贴着海面的大片内陆湖」与「山间的真湖」在观感上是两回事。
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

		// 入水口：while 退出时的 Cell 就是最后一个河格的流向目标 —— 湖格、海格，
		// 或是一片没有汇流量、水面等于地形的干地。折线末点的水位必须取**它**的
		// 水位（Sample().WaterLevelRadius 在格心就是 Water[Cell]），而不是最后
		// 一个河格自身的水位：后者比接收水面高 1–3 体素，河带末端会悬在空中。
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
