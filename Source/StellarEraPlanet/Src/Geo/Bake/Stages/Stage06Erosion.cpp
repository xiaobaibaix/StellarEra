#include "Stage06Erosion.h"
#include "Async/ParallelFor.h"
#include "Algo/Sort.h"
#include "GeoContinentField.h"

void Stage06::BuildGrid(int32 Res, FHydroGrid& Out)
{
	Out.Res = Res;
	const int32 N = Out.Num();
	Out.Nbr.SetNumUninitialized(8 * N);
	Out.Area.SetNumUninitialized(N);
	Out.Dir.SetNumUninitialized(N);
	static const int32 DX[8] = { -1, 0, 1, -1, 1, -1, 0, 1 };
	static const int32 DY[8] = { -1, -1, -1, 0, 0, 1, 1, 1 };
	const auto TexelUV = [Res](int32 X) { return (float(X) + 0.5f) / float(Res) * 2.f - 1.f; };
	double Total = 0.0;
	for (int32 Face = 0; Face < 6; ++Face)
	for (int32 Y = 0; Y < Res; ++Y)
	for (int32 X = 0; X < Res; ++X)
	{
		const int32 Idx = (Face * Res + Y) * Res + X;
		const float U = TexelUV(X), V = TexelUV(Y);
		Out.Dir[Idx] = FGeoField2D::FaceToDir(Face, U, V);
		// 公式同 GeoContinentField::TexelWeight；它要传 FGeoField2D，这里没有场对象，所以不直接调。
		const double W = FMath::Pow(1.0 + double(U) * U + double(V) * V, -1.5);
		Out.Area[Idx] = float(W);
		Total += W;
	}
	for (int32 i = 0; i < N; ++i) { Out.Area[i] = float(double(Out.Area[i]) / Total); }
	ParallelFor(6 * Res, [&](int32 Row)
	{
		const int32 Face = Row / Res, Y = Row % Res;
		for (int32 X = 0; X < Res; ++X)
		{
			const int32 Idx = (Face * Res + Y) * Res + X;
			int32 Cand[8];
			for (int32 k = 0; k < 8; ++k)
			{
				// 超出面的偏移点沿立方体平面延伸，DirToFace 会把它换回真正所在的面。
				const FVector3f D = FGeoField2D::FaceToDir(Face, TexelUV(X + DX[k]), TexelUV(Y + DY[k]));
				int32 F2; float U2, V2;
				FGeoField2D::DirToFace(D, F2, U2, V2);
				const int32 X2 = FMath::Clamp(FMath::FloorToInt((U2 + 1.f) * 0.5f * Res), 0, Res - 1);
				const int32 Y2 = FMath::Clamp(FMath::FloorToInt((V2 + 1.f) * 0.5f * Res), 0, Res - 1);
				Cand[k] = (F2 * Res + Y2) * Res + X2;
			}
			// 面角处偏移点会落回自己：用最近的真实邻居顶替，保持 8 槽。
			int32 Best = -1; float BestDot = -2.f;
			for (int32 k = 0; k < 8; ++k)
			{
				if (Cand[k] == Idx) { continue; }
				const float Dt = FVector3f::DotProduct(Out.Dir[Idx], Out.Dir[Cand[k]]);
				if (Dt > BestDot) { BestDot = Dt; Best = Cand[k]; }
			}
			for (int32 k = 0; k < 8; ++k) { Out.Nbr[8 * Idx + k] = Cand[k] == Idx ? Best : Cand[k]; }
		}
	});
}

namespace
{
	struct FStage06HeapItem { double F; int32 Idx; };
	struct FStage06HeapLess
	{
		bool operator()(const FStage06HeapItem& A, const FStage06HeapItem& B) const { return A.F < B.F || (A.F == B.F && A.Idx < B.Idx); }
	};
}

void Stage06::SolveFlow(const FHydroGrid& G, const TArray<float>& E, const TArray<float>& Precip,
	float Radius, float FlatSlopeRatio, float EndorheicPrecip, FHydroFlow& Out)
{
	const int32 N = G.Num();
	// 角格距取全脸平均 90°/Res，与阶段 5 的步长口径一致；Filled 用 double，否则 ε 小于大高程的 ulp 会被吞。
	const double Eps = double(FlatSlopeRatio) * double(Radius) * (PI * 0.5 / double(G.Res));
	Out.Filled.Init(TNumericLimits<double>::Max(), N);
	TArray<uint8> Visited; Visited.SetNumZeroed(N);
	TArray<uint8> bSeed; bSeed.SetNumZeroed(N);
	TArray<FStage06HeapItem> Heap;
	int32 SeedCount = 0;
	for (int32 i = 0; i < N; ++i)
	{
		if (E[i] <= 0.f) { Visited[i] = 1; bSeed[i] = 1; Out.Filled[i] = E[i]; Heap.Add({ double(E[i]), i }); ++SeedCount; }
	}
	if (SeedCount == 0)
	{
		// 没有海洋：全局最低格当出口，否则整颗星没有任何排水基准面。
		int32 Min = 0;
		for (int32 i = 1; i < N; ++i) { if (E[i] < E[Min]) { Min = i; } }
		Visited[Min] = 1; bSeed[Min] = 1; Out.Filled[Min] = E[Min]; Heap.Add({ double(E[Min]), Min });
	}
	Heap.Heapify(FStage06HeapLess());
	while (Heap.Num() > 0)
	{
		FStage06HeapItem Top;
		Heap.HeapPop(Top, FStage06HeapLess(), EAllowShrinking::No);
		for (int32 k = 0; k < 8; ++k)
		{
			const int32 n = G.Nbr[8 * Top.Idx + k];
			if (Visited[n]) { continue; }
			Visited[n] = 1;
			Out.Filled[n] = FMath::Max(double(E[n]), Top.F + Eps);
			Heap.HeapPush({ Out.Filled[n], n }, FStage06HeapLess());
		}
	}

	// 邻居表若漏了某格（入度 0），它不会被洪水访问，Filled 仍是哨兵值，下游判断会悄悄出错。
	for (int32 i = 0; i < N; ++i) { if (!ensureMsgf(Visited[i], TEXT("阶段 6：格子 %d 未被填洼访问"), i)) { Out.Filled[i] = E[i]; } }

	// D8：Filled 最低的邻居；平手取编号小者（先排序再只在严格更低时替换）。
	Out.Down.Init(-1, N);
	for (int32 i = 0; i < N; ++i)
	{
		if (bSeed[i]) { continue; }
		int32 Ns[8];
		for (int32 k = 0; k < 8; ++k) { Ns[k] = G.Nbr[8 * i + k]; }
		Algo::Sort(MakeArrayView(Ns, 8));
		int32 Best = -1; double BestF = Out.Filled[i];
		for (int32 k = 0; k < 8; ++k)
		{
			if (Out.Filled[Ns[k]] < BestF) { BestF = Out.Filled[Ns[k]]; Best = Ns[k]; }
		}
		Out.Down[i] = Best;
	}

	// 拓扑序：Filled 降序，平手编号升序；下游严格更低，一遍扫描即可。
	TArray<int32> Order; Order.SetNumUninitialized(N);
	for (int32 i = 0; i < N; ++i) { Order[i] = i; }
	Algo::Sort(Order, [&Out](int32 A, int32 B)
	{
		return Out.Filled[A] > Out.Filled[B] || (Out.Filled[A] == Out.Filled[B] && A < B);
	});
	TArray<double> AccP, AccA;
	const auto Accumulate = [&]()
	{
		AccP.Init(0.0, N); AccA.Init(0.0, N);
		for (int32 i = 0; i < N; ++i)
		{
			// 海洋只收河流入海量，不贡献自己的降水。
			if (E[i] > 0.f) { AccP[i] = double(G.Area[i]) * Precip[i]; AccA[i] = G.Area[i]; }
		}
		for (const int32 i : Order)
		{
			const int32 d = Out.Down[i];
			if (d >= 0) { AccP[d] += AccP[i]; AccA[d] += AccA[i]; }
		}
	};
	Accumulate();

	// 洼地 = Filled > E 的陆格的 8 连通块。
	Out.BasinId.Init(-1, N);
	Out.Basin.Init(0, N);
	Out.DownPreCut = Out.Down;
	Out.Exits.Reset(); Out.BasinMeanPrecip.Reset();
	// 超过 2ε 才算洼地：只被 ε 坡道抬高的平坦高原不是湖。
	const auto bDeep = [&](int32 i) { return !bSeed[i] && E[i] > 0.f && Out.Filled[i] - double(E[i]) > 2.0 * Eps; };
	TArray<int32> Stack;
	for (int32 s = 0; s < N; ++s)
	{
		if (!bDeep(s) || Out.BasinId[s] >= 0) { continue; }
		const int32 Id = Out.Exits.Num();
		TArray<int32> Cells;
		Stack.Reset(); Stack.Add(s); Out.BasinId[s] = Id;
		while (Stack.Num() > 0)
		{
			const int32 c = Stack.Pop(EAllowShrinking::No);
			Cells.Add(c);
			for (int32 k = 0; k < 8; ++k)
			{
				const int32 n = G.Nbr[8 * c + k];
				if (bDeep(n) && Out.BasinId[n] < 0) { Out.BasinId[n] = Id; Stack.Add(n); }
			}
		}
		Cells.Sort();
		TArray<int32>& Ex = Out.Exits.AddDefaulted_GetRef();
		double SumP = 0.0, SumA = 0.0;
		for (const int32 c : Cells)
		{
			const int32 d = Out.Down[c];
			if (d < 0 || Out.BasinId[d] != Id)
			{
				Ex.Add(c);
				// 每个洼地格恰好经由一个溢出格出去，溢出格的累积和就是整个集水区。
				SumP += AccP[c]; SumA += AccA[c];
			}
		}
		const float Mean = SumA > 0.0 ? float(SumP / SumA) : 0.f;
		Out.BasinMeanPrecip.Add(Mean);
		// 按切断前的累积判定：位于内流盆地下游的湖盆集水区含被切走的水，6a 接受这点偏差。
		const bool bEndo = Mean < EndorheicPrecip;
		for (const int32 c : Cells) { Out.Basin[c] = bEndo ? 2 : 1; }
		if (bEndo) { for (const int32 c : Ex) { Out.Down[c] = -1; } }
	}
	// 切断内流出口后重算，下游不再收到盆地里的水。
	Accumulate();
	Out.Acc = AccP;
	Out.TotalAreaPrecip = 0.0;
	for (int32 i = 0; i < N; ++i) { Out.TotalAreaPrecip += double(G.Area[i]) * Precip[i]; }
}

void FStage06Erosion::Bake(FPlanetBakeContext& Ctx) const
{
	FPlanetBakeFields& F = Ctx.Fields;
	if (F.Elevation.Res <= 0 || F.Precipitation.Res <= 0)
	{
		Ctx.Report.Errors.Add(TEXT("[地质] 阶段 6：缺高程或降水场"));
		return;
	}
	const FPlanetRecipeConfig& Cfg = Ctx.Config;
	const FPlanetHydroParams& P = Cfg.Hydro;
	const EParallelForFlags PF = Cfg.bSingleThreadBake ? EParallelForFlags::ForceSingleThread : EParallelForFlags::None;
	const int32 Res = FMath::Max(64, Cfg.FaceRes / 4);
	Stage06::FHydroGrid Grid;
	Stage06::BuildGrid(Res, Grid);
	const int32 N = Grid.Num();
	TArray<float> E, Pr;
	E.SetNumUninitialized(N); Pr.SetNumUninitialized(N);
	ParallelFor(6 * Res, [&](int32 Row)
	{
		for (int32 i = Row * Res; i < (Row + 1) * Res; ++i)
		{
			E[i] = F.Elevation.Sample(Grid.Dir[i]);
			Pr[i] = F.Precipitation.Sample(Grid.Dir[i]);
		}
	}, PF);

	Stage06::FHydroFlow Flow;
	Stage06::SolveFlow(Grid, E, Pr, float(Cfg.Radius), P.FlatSlopeRatio, P.EndorheicPrecip, Flow);

	F.FlowAccum.Init(Res); F.BasinMask.Init(Res);
	F.ErosionDepth.Init(Res); F.RiverMask.Init(Res);
	const double Inv = Flow.TotalAreaPrecip > 0.0 ? 1.0 / Flow.TotalAreaPrecip : 0.0;
	const double R = Cfg.Radius;
	const float Thr = FMath::Max(P.RiverThreshold, 1e-9f);
	const float W = P.MountainErosionWeight;
	// 河谷半宽上限：分桶只覆盖端点格 2 圈邻格，半宽超过覆盖半径会在桶边界处被硬切。
	const float HalfWidthCap = 1.2f * 2.f / float(Res);

	struct FSegment { FVector3f A, AB; float InvLen2, HalfWidth, Carve; };
	TArray<FSegment> Segs;
	TArray<float> CellCarve; CellCarve.Init(0.f, N);
	int32 RiverCells = 0;
	float MaxCarve = 0.f;
	for (int32 i = 0; i < N; ++i)
	{
		const int32 Face = i / (Res * Res), Y = (i / Res) % Res, X = i % Res;
		const bool bLand = E[i] > 0.f;
		const float An = float(Flow.Acc[i] * Inv);
		F.FlowAccum.At(Face, X, Y) = An;
		F.BasinMask.At(Face, X, Y) = float(Flow.Basin[i]);
		if (!bLand) { continue; }
		const int32 D = Flow.Down[i];
		float Slope = 0.f;
		float ArcLen = 0.f;
		if (D >= 0)
		{
			const float Dot = FMath::Clamp(FVector3f::DotProduct(Grid.Dir[i], Grid.Dir[D]), -1.f, 1.f);
			ArcLen = FMath::Acos(Dot);
			if (ArcLen > 1e-9f) { Slope = float((Flow.Filled[i] - Flow.Filled[D]) / (R * ArcLen)); }
		}
		float Carve = 0.f;
		if (P.bRivers)
		{
			F.RiverMask.At(Face, X, Y) = FMath::SmoothStep(Thr, 4.f * Thr, An);
			if (An > Thr && D >= 0)
			{
				++RiverCells;
				const float Ratio = FMath::Sqrt(An / Thr);
				Carve = FMath::Min(float(P.CarveRatio * R * Ratio * FMath::Sqrt(FMath::Max(Slope, 0.f) / P.SlopeRef)),
					float(P.CarveMaxRatio * R));
				MaxCarve = FMath::Max(MaxCarve, Carve);
				const FVector3f AB = Grid.Dir[D] - Grid.Dir[i];
				const float L2 = AB.SizeSquared();
				if (Carve > 0.f && L2 > 1e-12f)
				{
					Segs.Add({ Grid.Dir[i], AB, 1.f / L2, FMath::Min(P.ValleyWidthRatio * Ratio, HalfWidthCap), Carve });
				}
			}
		}
		CellCarve[i] = Carve;
		const float Mtn = F.MountainMask.Sample(Grid.Dir[i]);
		F.ErosionDepth.At(Face, X, Y) = float(P.ErosionRatio * R) * Pr[i] * FMath::Clamp(Slope / P.SlopeRef, 0.f, 2.f)
			* ((1.f - W) + W * Mtn) + Carve;
	}

	if (Segs.Num() > 0)
	{
		// CSR 分桶：每段进两端格及其 2 圈邻格；texel 只查自己所在宏观格的桶。
		TArray<int32> Count; Count.Init(0, N + 1);
		TArray<int32> Mark; Mark.Init(-1, N);
		TArray<int32> Cells;
		const auto Touch = [&](const FSegment& Sg, TFunctionRef<void(int32)> Emit)
		{
			// 段端点格由方向反查，避免另存下游编号。
			int32 Ring[2];
			for (int32 e = 0; e < 2; ++e)
			{
				int32 Fc; float U, V;
				FGeoField2D::DirToFace(e == 0 ? Sg.A : (Sg.A + Sg.AB), Fc, U, V);
				const int32 X = FMath::Clamp(FMath::FloorToInt((U + 1.f) * 0.5f * Res), 0, Res - 1);
				const int32 Y = FMath::Clamp(FMath::FloorToInt((V + 1.f) * 0.5f * Res), 0, Res - 1);
				Ring[e] = (Fc * Res + Y) * Res + X;
			}
			Cells.Reset();
			for (int32 e = 0; e < 2; ++e)
			{
				Cells.AddUnique(Ring[e]);
				for (int32 k = 0; k < 8; ++k)
				{
					const int32 n1 = Grid.Nbr[8 * Ring[e] + k];
					Cells.AddUnique(n1);
					for (int32 m = 0; m < 8; ++m) { Cells.AddUnique(Grid.Nbr[8 * n1 + m]); }
				}
			}
			for (const int32 c : Cells) { Emit(c); }
		};
		for (int32 s = 0; s < Segs.Num(); ++s) { Touch(Segs[s], [&](int32 c) { ++Count[c + 1]; }); }
		for (int32 c = 0; c < N; ++c) { Count[c + 1] += Count[c]; }
		TArray<int32> Fill; Fill.Init(0, N);
		TArray<int32> Items; Items.SetNumUninitialized(Count[N]);
		for (int32 s = 0; s < Segs.Num(); ++s) { Touch(Segs[s], [&](int32 c) { Items[Count[c] + Fill[c]++] = s; }); }

		const int32 Full = F.Elevation.Res;
		ParallelFor(6 * Full, [&](int32 Row)
		{
			const int32 Face = Row / Full, Y = Row % Full;
			const int32 My = ((2 * Y + 1) * Res) / (2 * Full);
			for (int32 X = 0; X < Full; ++X)
			{
				const float E0 = F.Elevation.At(Face, X, Y);
				if (E0 <= 0.f) { continue; }
				const int32 Mx = ((2 * X + 1) * Res) / (2 * Full);
				const int32 Cell = (Face * Res + My) * Res + Mx;
				const int32 B = Count[Cell], En = Count[Cell + 1];
				if (B == En) { continue; }
				const FVector3f Pd = F.Elevation.TexelDir(Face, X, Y);
				float Best = 0.f;
				for (int32 k = B; k < En; ++k)
				{
					const FSegment& Sg = Segs[Items[k]];
					const float T = FMath::Clamp(FVector3f::DotProduct(Pd - Sg.A, Sg.AB) * Sg.InvLen2, 0.f, 1.f);
					const float Dd = (Pd - (Sg.A + Sg.AB * T)).Size();
					if (Dd >= Sg.HalfWidth) { continue; }
					const float Q = Dd / Sg.HalfWidth;
					const float Fall = 1.f - Q * Q * (3.f - 2.f * Q);
					Best = FMath::Max(Best, Sg.Carve * Fall * Fall);
				}
				// 与阶段 4 同口径：刻谷不得把陆地挖穿海平面。
				if (Best > 0.f) { F.Elevation.At(Face, X, Y) = FMath::Max(E0 - Best, FMath::Min(E0, 1.f)); }
			}
		}, PF);

		// 行内局部量、按行序归约：与线程数无关，结果逐位确定。
		struct FRowStat { float Min, Max; double Land, Total; };
		TArray<FRowStat> Rows; Rows.SetNumUninitialized(6 * Full);
		ParallelFor(6 * Full, [&](int32 Row)
		{
			const int32 Face = Row / Full, Y = Row % Full;
			FRowStat St{ TNumericLimits<float>::Max(), -TNumericLimits<float>::Max(), 0.0, 0.0 };
			for (int32 X = 0; X < Full; ++X)
			{
				const float Ev = F.Elevation.At(Face, X, Y);
				St.Min = FMath::Min(St.Min, Ev); St.Max = FMath::Max(St.Max, Ev);
				const double Wt = GeoContinentField::TexelWeight(F.Elevation, X, Y);
				St.Total += Wt; St.Land += Ev > 0.f ? Wt : 0.0;
			}
			Rows[Row] = St;
		}, PF);
		float MinE = TNumericLimits<float>::Max(), MaxE = -TNumericLimits<float>::Max();
		double LandW = 0.0, TotalW = 0.0;
		for (const FRowStat& St : Rows) { MinE = FMath::Min(MinE, St.Min); MaxE = FMath::Max(MaxE, St.Max); LandW += St.Land; TotalW += St.Total; }
		F.MinElevation = MinE; F.MaxElevation = MaxE;
		F.LandAreaFrac = float(LandW / FMath::Max(TotalW, 1e-30));
	}
	UE_LOG(LogTemp, Log, TEXT("[地质] 阶段 6：河道格 %d，线段 %d，最大下切 %.2f 体素（%.5f R）"), RiverCells, Segs.Num(), MaxCarve, MaxCarve / R);
}
