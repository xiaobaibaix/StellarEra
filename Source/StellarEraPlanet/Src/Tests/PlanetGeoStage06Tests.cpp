// 阶段 6（水系）：网格对称与面积、填洼 / 流向 / 内流盆地 / 汇流累积的网络性质。
#include "CoreMinimal.h"
#include "HAL/PlatformTime.h"
#include "Misc/AutomationTest.h"
#include "GeoRng.h"
#include "PlanetBakePipeline.h"
#include "Stage02Plates.h"
#include "Stage03Terrain.h"
#include "Stage04Magmatism.h"
#include "Stage05Climate.h"
#include "Stage06Erosion.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	/** 跑 2→3→4→5，不跑 6：测试要自己调 SolveFlow 核对内部量。 */
	FPlanetBakeFields Stage06UpToClimate(const FPlanetRecipeConfig& C, FPlanetBakeReport& R)
	{
		FPlanetBakeFields F;
		FPlanetBakeContext C2{ C, F, FGeoRng::Derive(C.Seed, 2, 0), R }; FStage02Plates().Bake(C2);
		FPlanetBakeContext C3{ C, F, FGeoRng::Derive(C.Seed, 3, 0), R }; FStage03Terrain().Bake(C3);
		FPlanetBakeContext C4{ C, F, FGeoRng::Derive(C.Seed, 4, 0), R }; FStage04Magmatism().Bake(C4);
		FPlanetBakeContext C5{ C, F, FGeoRng::Derive(C.Seed, 5, 0), R }; FStage05Climate().Bake(C5);
		return F;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGeoStage06GridTest, "StellarEra.Planet.Geo.Stage06.Grid",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGeoStage06GridTest::RunTest(const FString& Parameters)
{
	Stage06::FHydroGrid G;
	Stage06::BuildGrid(64, G);
	const int32 N = G.Num();
	if (!TestEqual(TEXT("格子数"), N, 6 * 64 * 64)) { return false; }
	double AreaSum = 0.0;
	int32 BadSlot = 0, Asym = 0, Checked = 0, ZeroIn = 0;
	TArray<int32> InDeg; InDeg.Init(0, N);
	for (int32 i = 0; i < N; ++i)
	{
		AreaSum += G.Area[i];
		TSet<int32> Uniq;
		for (int32 k = 0; k < 8; ++k)
		{
			const int32 n = G.Nbr[8 * i + k];
			if (n < 0 || n >= N || n == i) { ++BadSlot; continue; }
			Uniq.Add(n);
		}
		for (const int32 n : Uniq) { ++InDeg[n]; }
		// 去重后恰好 8 个才算「非重复」邻域；面角格子只有 7 个，不参与对称检查。
		if (Uniq.Num() < 8) { continue; }
		for (const int32 n : Uniq)
		{
			++Checked;
			bool bBack = false;
			for (int32 k = 0; k < 8; ++k) { bBack |= G.Nbr[8 * n + k] == i; }
			if (!bBack) { ++Asym; }
		}
	}
	for (int32 i = 0; i < N; ++i) { ZeroIn += InDeg[i] < 1 ? 1 : 0; }
	TestEqual(TEXT("每格（含面角）入度 ≥ 1"), ZeroIn, 0);
	TestEqual(TEXT("每个槽都是有效的他格"), BadSlot, 0);
	TestTrue(FString::Printf(TEXT("面积和 = 1（%.6f）"), AreaSum), FMath::Abs(AreaSum - 1.0) < 1e-4);
	AddInfo(FString::Printf(TEXT("邻居对称检查 %d 对，不对称 %d"), Checked, Asym));
	TestEqual(TEXT("非重复邻居互为邻居"), Asym, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGeoStage06FlowNetworkTest, "StellarEra.Planet.Geo.Stage06.FlowNetwork",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGeoStage06FlowNetworkTest::RunTest(const FString& Parameters)
{
	FPlanetRecipeConfig C;
	C.FaceRes = 256;
	FPlanetBakeReport R;
	FPlanetBakeFields F = Stage06UpToClimate(C, R);
	if (!TestFalse(FString::Printf(TEXT("无错误（%s）"), *FString::Join(R.Errors, TEXT(" | "))), R.HasErrors())) { return false; }

	// 阶段本体：计时 + 字段落盘。
	FPlanetBakeFields F6 = F;
	FPlanetBakeContext C6{ C, F6, FGeoRng::Derive(C.Seed, 6, 0), R };
	const double T0 = FPlatformTime::Seconds();
	FStage06Erosion().Bake(C6);
	const double Sec = FPlatformTime::Seconds() - T0;
	TestFalse(TEXT("阶段 6 无错误"), R.HasErrors());
	const int32 Res = FMath::Max(64, C.FaceRes / 4);
	TestEqual(TEXT("FlowAccum 分辨率"), F6.FlowAccum.Res, Res);
	TestEqual(TEXT("BasinMask 分辨率"), F6.BasinMask.Res, Res);
	TestEqual(TEXT("ErosionDepth 已 Init"), F6.ErosionDepth.Res, Res);
	TestEqual(TEXT("RiverMask 已 Init"), F6.RiverMask.Res, Res);

	Stage06::FHydroGrid G;
	Stage06::BuildGrid(Res, G);
	const int32 N = G.Num();
	TArray<float> E, Pr;
	E.SetNumUninitialized(N); Pr.SetNumUninitialized(N);
	for (int32 i = 0; i < N; ++i) { E[i] = F.Elevation.Sample(G.Dir[i]); Pr[i] = F.Precipitation.Sample(G.Dir[i]); }
	Stage06::FHydroFlow Fl;
	Stage06::SolveFlow(G, E, Pr, float(C.Radius), C.Hydro.FlatSlopeRatio, C.Hydro.EndorheicPrecip, Fl);

	// 沿 Down 走到底；返回终点格。
	const auto Terminal = [&](const TArray<int32>& Down, int32 Start)
	{
		int32 c = Start, Steps = 0;
		while (Down[c] >= 0 && Steps <= N) { c = Down[c]; ++Steps; }
		return Steps > N ? -1 : c;
	};
	int32 Land = 0, Stuck = 0, LakeLost = 0, LakeToOcean = 0, EndoEscape = 0;
	for (int32 i = 0; i < N; ++i)
	{
		if (E[i] <= 0.f) { continue; }
		++Land;
		const int32 t = Terminal(Fl.Down, i);
		if (t < 0 || !(E[t] <= 0.f || Fl.Basin[t] == 2)) { ++Stuck; }
		if (Fl.Basin[i] == 1)
		{
			// 湖盆下游若落进某个内流盆地的集水区，水就终止在那里：这是已接受的情形。
			if (t >= 0 && E[t] <= 0.f) { ++LakeToOcean; }
			else if (t < 0 || Fl.Basin[t] != 2) { ++LakeLost; }
		}
		// 内流盆地切断后不得再有路径通到海洋。
		if (Fl.Basin[i] == 2 && (t < 0 || E[t] <= 0.f || Fl.Basin[t] != 2)) { ++EndoEscape; }
	}
	TestEqual(TEXT("每个陆格都流到海洋或内流盆地"), Stuck, 0);
	TestEqual(TEXT("湖盆格都流到海洋或内流盆地"), LakeLost, 0);
	TestEqual(TEXT("内流盆地切断后到不了海洋"), EndoEscape, 0);

	// 守恒：所有根（Down<0 的格）的汇流之和 = 陆地面积×降水总量，每份水只能终止一次。
	double RootSum = 0.0, LandAP = 0.0;
	for (int32 i = 0; i < N; ++i)
	{
		if (Fl.Down[i] < 0) { RootSum += Fl.Acc[i]; }
		if (E[i] > 0.f) { LandAP += double(G.Area[i]) * Pr[i]; }
	}
	TestTrue(FString::Printf(TEXT("汇流守恒（根之和 %.8f，陆地总量 %.8f）"), RootSum, LandAP),
		FMath::Abs(RootSum - LandAP) <= 1e-4 * LandAP);

	// 按切断后的 Down 重算内流盆地集水区平均降水；上游若有别的内流盆地，平均值允许偏离分类时的数。
	TArray<int32> Order;
	Order.SetNumUninitialized(N);
	for (int32 i = 0; i < N; ++i) { Order[i] = i; }
	Order.Sort([&Fl](int32 A, int32 B) { return Fl.Filled[A] > Fl.Filled[B] || (Fl.Filled[A] == Fl.Filled[B] && A < B); });
	TArray<double> AP, AA;
	AP.Init(0.0, N); AA.Init(0.0, N);
	for (int32 i = 0; i < N; ++i) { if (E[i] > 0.f) { AP[i] = double(G.Area[i]) * Pr[i]; AA[i] = G.Area[i]; } }
	for (const int32 i : Order) { if (Fl.Down[i] >= 0) { AP[Fl.Down[i]] += AP[i]; AA[Fl.Down[i]] += AA[i]; } }
	// 切断前，哪些内流盆地的水会流进别的内流盆地：它们下游的盆地分类用了「多算的集水区」。
	TSet<int32> HasUpstreamEndo;
	for (int32 b = 0; b < Fl.Exits.Num(); ++b)
	{
		if (Fl.Exits[b].Num() == 0 || Fl.Basin[Fl.Exits[b][0]] != 2) { continue; }
		for (const int32 e : Fl.Exits[b])
		{
			int32 c = Fl.DownPreCut[e], Steps = 0;
			while (c >= 0 && Steps++ <= N)
			{
				if (Fl.BasinId[c] >= 0 && Fl.BasinId[c] != b && Fl.Basin[c] == 2) { HasUpstreamEndo.Add(Fl.BasinId[c]); }
				c = Fl.DownPreCut[c];
			}
		}
	}
	int32 Lakes = 0, Endo = 0, BadExit = 0, BadEndo = 0, Changed = 0;
	for (int32 b = 0; b < Fl.Exits.Num(); ++b)
	{
		const TArray<int32>& Ex = Fl.Exits[b];
		if (Ex.Num() == 0) { ++BadExit; continue; }
		if (Fl.Basin[Ex[0]] == 2)
		{
			++Endo;
			double SP = 0.0, SA = 0.0;
			for (const int32 e : Ex) { if (Fl.Down[e] != -1) { ++BadEndo; } SP += AP[e]; SA += AA[e]; }
			const float Post = SA > 0.0 ? float(SP / SA) : 0.f;
			if (FMath::Abs(Post - Fl.BasinMeanPrecip[b]) > 1e-6f) { ++Changed; }
			if (!(Post < C.Hydro.EndorheicPrecip) && !HasUpstreamEndo.Contains(b)) { ++BadEndo; }
		}
		else
		{
			++Lakes;
			for (const int32 e : Ex) { if (Fl.Down[e] < 0) { ++BadExit; } }
		}
	}
	TestEqual(TEXT("每个湖盆有溢出格且溢出格有下游"), BadExit, 0);
	TestEqual(TEXT("内流盆地出口已切断，切断后集水区平均降水仍低于阈值（或位于另一内流盆地下游）"), BadEndo, 0);
	AddInfo(FString::Printf(TEXT("内流盆地 %d 个中，切断后平均降水与分类时不同的 %d 个；湖盆格直达海洋 %d"), Endo, Changed, LakeToOcean));

	// 阶段写出的字段与求解一致，且归一化后在 0–1。
	float MaxAcc = 0.f;
	int32 FieldMismatch = 0;
	for (int32 Face = 0; Face < 6; ++Face)
	for (int32 Y = 0; Y < Res; ++Y)
	for (int32 X = 0; X < Res; ++X)
	{
		const int32 i = (Face * Res + Y) * Res + X;
		MaxAcc = FMath::Max(MaxAcc, F6.FlowAccum.At(Face, X, Y));
		if (F6.BasinMask.At(Face, X, Y) != float(Fl.Basin[i])) { ++FieldMismatch; }
	}
	TestEqual(TEXT("BasinMask 与求解一致"), FieldMismatch, 0);
	TestTrue(FString::Printf(TEXT("FlowAccum 在 (0,1]（最大 %.5f）"), MaxAcc), MaxAcc > 0.f && MaxAcc <= 1.0001f);
	AddInfo(FString::Printf(TEXT("陆格 %d，湖盆 %d，内流盆地 %d，最大汇流 %.5f，阶段 6 用时 %.3f s"), Land, Lakes, Endo, MaxAcc, Sec));

	// 没有海洋的星球：不崩，且全部格子最终落到唯一出口。
	TArray<float> Dry, Wet;
	Dry.SetNumUninitialized(N); Wet.Init(0.5f, N);
	for (int32 i = 0; i < N; ++i) { Dry[i] = 100.f + 50.f * G.Dir[i].Z; }
	Stage06::FHydroFlow Fl2;
	Stage06::SolveFlow(G, Dry, Wet, float(C.Radius), C.Hydro.FlatSlopeRatio, C.Hydro.EndorheicPrecip, Fl2);
	int32 Roots = 0;
	for (int32 i = 0; i < N; ++i) { Roots += Fl2.Down[i] < 0 ? 1 : 0; }
	TestEqual(TEXT("无海洋星球只有一个排水出口"), Roots, 1);
	return true;
}

#endif
