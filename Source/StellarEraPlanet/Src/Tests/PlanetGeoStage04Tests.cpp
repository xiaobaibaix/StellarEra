// 阶段 4：管线接线（点表进快照、配方哈希对火山参数敏感）+ 放点（间距 / 上盘 / 热点链 / 活性 / 确定性 / 尺度不变 / 开关）
// + 剖面叠加（火口 / 陆地占比 / 宏观场）。
#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "GeoContinentField.h"
#include "GeoPlates.h"
#include "GeoRng.h"
#include "PlanetBakePipeline.h"
#include "Stage02Plates.h"
#include "Stage03Terrain.h"
#include "Stage04Magmatism.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	// 跑阶段 2→3→4；返回阶段 3 之后、阶段 4 之前的高程。
	FGeoField2D Stage04TestBake(const FPlanetRecipeConfig& C, FPlanetBakeFields& F, FPlanetBakeReport& R, bool bRunStage4 = true)
	{
		FPlanetBakeContext C2{ C, F, FGeoRng::Derive(C.Seed, 2, 0), R }; FStage02Plates().Bake(C2);
		FPlanetBakeContext C3{ C, F, FGeoRng::Derive(C.Seed, 3, 0), R }; FStage03Terrain().Bake(C3);
		const FGeoField2D Before = F.Elevation;
		if (bRunStage4) { FPlanetBakeContext C4{ C, F, FGeoRng::Derive(C.Seed, 4, 0), R }; FStage04Magmatism().Bake(C4); }
		return Before;
	}
	int32 Stage04Count(const FPlanetBakeFields& F, EGeoVolcanoKind K)
	{
		int32 N = 0; for (const FGeoVolcano& V : F.Volcanoes) { N += V.Kind == K ? 1 : 0; } return N;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGeoStage04PipelineTest, "StellarEra.Planet.Geo.Stage04.Pipeline",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGeoStage04PipelineTest::RunTest(const FString& Parameters)
{
	FPlanetRecipeConfig C;
	C.FaceRes = 256;
	FPlanetBakeReport R;
	const TSharedPtr<const FPlanetSnapshot> Snap = FPlanetBakePipeline::MakeDefault().Bake(C, R);
	TestTrue(FString::Printf(TEXT("烘焙成功（%s）"), *FString::Join(R.Errors, TEXT(" | "))), Snap.IsValid());
	TestTrue(TEXT("阶段 4 在管线里"), R.StageSeconds.ContainsByPredicate([](const TPair<FName, double>& P) { return P.Key == TEXT("Stage04Magmatism"); }));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGeoStage04PlacementTest, "StellarEra.Planet.Geo.Stage04.Placement",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGeoStage04PlacementTest::RunTest(const FString& Parameters)
{
	FPlanetRecipeConfig C;
	C.FaceRes = 512;
	FPlanetBakeFields F; FPlanetBakeReport R;
	Stage04TestBake(C, F, R);
	TestFalse(TEXT("无错误"), R.HasErrors());
	const FPlanetVolcanoParams& Vp = C.Volcano;
	const int32 NS = Stage04Count(F, EGeoVolcanoKind::Strato), NH = Stage04Count(F, EGeoVolcanoKind::Shield),
		NF = Stage04Count(F, EGeoVolcanoKind::FloodBasalt), NC = Stage04Count(F, EGeoVolcanoKind::Caldera), NK = Stage04Count(F, EGeoVolcanoKind::CinderCone);
	AddInfo(FString::Printf(TEXT("层状 %d，盾状 %d，溢流 %d，破火山口 %d，渣锥 %d"), NS, NH, NF, NC, NK));
	TestTrue(TEXT("有层状火山"), NS > 0);
	TestTrue(TEXT("有盾状火山"), NH > 0);
	TestTrue(TEXT("有破火山口"), NC > 0);
	TestTrue(TEXT("有渣锥"), NK > 0);

	// 弧火山间距：同一俯冲对上的层状火山（含塌成破火山口的弧火山），最近邻距离。
	TArray<float> Nearest;
	for (int32 i = 0; i < F.Volcanoes.Num(); ++i)
	{
		const FGeoVolcano& A = F.Volcanoes[i];
		if (A.OtherPlate < 0) { continue; }
		float Best = TNumericLimits<float>::Max();
		for (int32 j = 0; j < F.Volcanoes.Num(); ++j)
		{
			const FGeoVolcano& B = F.Volcanoes[j];
			if (i == j || B.OtherPlate < 0) { continue; }
			Best = FMath::Min(Best, (A.Dir - B.Dir).Size());
		}
		if (Best < 10.f) { Nearest.Add(Best); }
	}
	Nearest.Sort();
	if (Nearest.Num() > 0)
	{
		const float Med = Nearest[Nearest.Num() / 2];
		AddInfo(FString::Printf(TEXT("弧火山最近邻：最小 %.4f，中位 %.4f（间距 %.4f）"), Nearest[0], Med, Vp.ArcSpacingRatio));
		TestTrue(TEXT("弧火山最小间距 ≥ 0.65×"), Nearest[0] >= 0.65f * Vp.ArcSpacingRatio);
		TestTrue(TEXT("弧火山中位间距 ∈ [0.8, 1.9]×"), Med >= 0.8f * Vp.ArcSpacingRatio && Med <= 1.9f * Vp.ArcSpacingRatio);
	}

	// 上盘：弧火山所在板块 ≠ 俯冲板块，且在 PlateId 场上确实落在自己板块。
	int32 ArcN = 0, ArcUpper = 0;
	for (const FGeoVolcano& V : F.Volcanoes)
	{
		if (V.OtherPlate < 0) { continue; }
		++ArcN;
		ArcUpper += (V.Plate != V.OtherPlate && int32(F.PlateId.SampleNearest(V.Dir)) == V.Plate) ? 1 : 0;
	}
	TestEqual(TEXT("弧火山都在上盘自己的板块上"), ArcUpper, ArcN);

	// 热点链：年龄单调、方向顺着板块运动。
	TMap<int32, TArray<const FGeoVolcano*>> Chains;
	for (const FGeoVolcano& V : F.Volcanoes) { if (V.Chain >= 0) { Chains.FindOrAdd(V.Chain).Add(&V); } }
	int32 Steps = 0, Aligned = 0, Ordered = 0;
	for (auto& Pair : Chains)
	{
		TArray<const FGeoVolcano*>& L = Pair.Value;
		L.Sort([](const FGeoVolcano& A, const FGeoVolcano& B) { return A.ChainIndex < B.ChainIndex; });
		for (int32 k = 1; k < L.Num(); ++k)
		{
			++Steps;
			Ordered += L[k]->Age > L[k - 1]->Age ? 1 : 0;
			const FVector3f Vel = F.PlateFrame.Plates[L[k - 1]->Plate].VelocityAt(L[k - 1]->Dir).GetSafeNormal();
			const FVector3f Step = (L[k]->Dir - L[k - 1]->Dir).GetSafeNormal();
			Aligned += FVector3f::DotProduct(Vel, Step) > 0.5f ? 1 : 0;
		}
	}
	TestTrue(TEXT("至少一条热点链长 ≥ 2"), Steps > 0);
	TestEqual(TEXT("链上年龄单调递增"), Ordered, Steps);
	TestEqual(TEXT("链顺着板块运动方向（夹角 < 60°）"), Aligned, Steps);

	// 活跃：层状 / 链上前两座 / 渣锥（渣锥只围活火山）/ 由上述塌成的破火山口。
	int32 BadActive = 0;
	for (const FGeoVolcano& V : F.Volcanoes)
	{
		if (!V.bActive) { continue; }
		const bool bOk = V.Kind == EGeoVolcanoKind::Strato || V.Kind == EGeoVolcanoKind::CinderCone
			|| (V.Chain >= 0 && V.ChainIndex <= 1) || (V.Kind == EGeoVolcanoKind::Caldera && V.OtherPlate >= 0);
		BadActive += bOk ? 0 : 1;
	}
	TestEqual(TEXT("活跃火山只在弧上或热点附近"), BadActive, 0);

	// 确定性。
	FPlanetBakeFields F2; FPlanetBakeReport R2;
	Stage04TestBake(C, F2, R2);
	TestTrue(TEXT("同配方点表逐项相等"), F.Volcanoes == F2.Volcanoes);

	// 尺度不变：半径减半，个数与方向单位尺寸不变，高度减半。
	FPlanetRecipeConfig Half = C; Half.Radius = C.Radius * 0.5;
	FPlanetBakeFields Fh; FPlanetBakeReport Rh;
	Stage04TestBake(Half, Fh, Rh);
	TestEqual(TEXT("半径减半火山数不变"), Fh.Volcanoes.Num(), F.Volcanoes.Num());
	if (Fh.Volcanoes.Num() == F.Volcanoes.Num() && F.Volcanoes.Num() > 0)
	{
		const FGeoVolcano& A = F.Volcanoes[0]; const FGeoVolcano& B = Fh.Volcanoes[0];
		TestTrue(TEXT("底半径（方向单位）不变"), FMath::IsNearlyEqual(A.BaseRadius, B.BaseRadius, 1e-5f));
		TestTrue(TEXT("高度减半"), FMath::IsNearlyEqual(B.Height, 0.5f * A.Height, 0.01f * A.Height + 1e-3f));
	}

	// 开关：关掉一种 → 该类型 0 座。
	const auto Off = [&](TFunctionRef<void(FPlanetVolcanoParams&)> Set, EGeoVolcanoKind K, const TCHAR* Name)
	{
		FPlanetRecipeConfig X = C; Set(X.Volcano);
		FPlanetBakeFields Fx; FPlanetBakeReport Rx; Stage04TestBake(X, Fx, Rx);
		TestEqual(FString::Printf(TEXT("关闭%s → 0 座"), Name), Stage04Count(Fx, K), 0);
	};
	Off([](FPlanetVolcanoParams& P) { P.bArcVolcanoes = false; }, EGeoVolcanoKind::Strato, TEXT("弧火山"));
	Off([](FPlanetVolcanoParams& P) { P.bHotspots = false; }, EGeoVolcanoKind::Shield, TEXT("热点"));
	Off([](FPlanetVolcanoParams& P) { P.bFloodBasalt = false; }, EGeoVolcanoKind::FloodBasalt, TEXT("溢流玄武岩"));
	Off([](FPlanetVolcanoParams& P) { P.bCalderas = false; }, EGeoVolcanoKind::Caldera, TEXT("破火山口"));
	Off([](FPlanetVolcanoParams& P) { P.bCinderCones = false; }, EGeoVolcanoKind::CinderCone, TEXT("渣锥"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGeoStage04ReliefTest, "StellarEra.Planet.Geo.Stage04.Relief",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGeoStage04ReliefTest::RunTest(const FString& Parameters)
{
	FPlanetRecipeConfig C;
	C.FaceRes = 1024;   // 层状火口半径约 2.6 texel，512 下只有 1.3 个
	FPlanetBakeFields F; FPlanetBakeReport R;
	const FGeoField2D Before = Stage04TestBake(C, F, R);
	TestFalse(TEXT("无错误"), R.HasErrors());
	const float Texel = 2.f / float(C.FaceRes);

	// 火口：中心比火口外一圈的平均低。
	int32 Checked = 0, Pit = 0;
	for (const FGeoVolcano& V : F.Volcanoes)
	{
		if (V.CraterRadius < 2.f * Texel || V.Kind == EGeoVolcanoKind::CinderCone) { continue; }
		const FVector3f T1 = FVector3f::CrossProduct(FMath::Abs(V.Dir.Z) < 0.9f ? FVector3f::UpVector : FVector3f::ForwardVector, V.Dir).GetSafeNormal();
		const FVector3f T2 = FVector3f::CrossProduct(V.Dir, T1);
		float Rim = 0.f;
		for (int32 k = 0; k < 8; ++k)
		{
			const float A = 2.f * PI * float(k) / 8.f;
			Rim += 0.125f * F.Elevation.Sample((V.Dir + (T1 * FMath::Cos(A) + T2 * FMath::Sin(A)) * (1.2f * V.CraterRadius)).GetSafeNormal());
		}
		++Checked;
		const float Center = F.Elevation.Sample(V.Dir);
		if (Center < Rim)
		{
			++Pit;
		}
		else
		{
			int32 Neighbors = 0;
			float BeforeRim = 0.f;
			for (const FGeoVolcano& W : F.Volcanoes) { Neighbors += (W.Dir - V.Dir).Size() < 2.f * W.BaseRadius ? 1 : 0; }
			for (int32 k = 0; k < 8; ++k)
			{
				const float A = 2.f * PI * float(k) / 8.f;
				BeforeRim += 0.125f * Before.Sample((V.Dir + (T1 * FMath::Cos(A) + T2 * FMath::Sin(A)) * (1.2f * V.CraterRadius)).GetSafeNormal());
			}
			AddInfo(FString::Printf(TEXT("未凹下：类型 %d 火口半径 %.4f 邻近火山 %d 中心 %.1f 边缘 %.1f 阶段3中心 %.1f 阶段3边缘 %.1f"),
				int32(V.Kind), V.CraterRadius, Neighbors, Center, Rim, Before.Sample(V.Dir), BeforeRim));
		}
	}
	AddInfo(FString::Printf(TEXT("火口检查 %d 座，凹下 %d 座"), Checked, Pit));
	TestTrue(TEXT("有可检查的火口"), Checked > 0);
	TestEqual(TEXT("火口底都低于火口边缘"), Pit, Checked);

	// 陆地占比与「只抬不挖穿」。
	FPlanetBakeFields F3; FPlanetBakeReport R3;
	Stage04TestBake(C, F3, R3, false);
	float MaxPit = 0.f;
	for (const FGeoVolcano& V : F.Volcanoes) { MaxPit = FMath::Max(MaxPit, V.CraterDepth); }
	int32 Below = 0, LandLost = 0;
	for (int32 Face = 0; Face < 6; ++Face) for (int32 Y = 0; Y < Before.Res; ++Y) for (int32 X = 0; X < Before.Res; ++X)
	{
		const float E3 = Before.At(Face, X, Y), E4 = F.Elevation.At(Face, X, Y);
		Below += E4 < E3 - MaxPit - 1.f ? 1 : 0;
		LandLost += (E3 > 0.f && E4 <= 0.f) ? 1 : 0;
	}
	AddInfo(FString::Printf(TEXT("陆地 %.4f → %.4f；火山增量峰值 %.0f"), F3.LandAreaFrac, F.LandAreaFrac, F.MaxElevation - F3.MaxElevation));
	TestTrue(TEXT("陆地占比变化 < 0.01"), FMath::Abs(F.LandAreaFrac - F3.LandAreaFrac) < 0.01f);
	TestEqual(TEXT("没有 texel 被挖得比火口还深"), Below, 0);
	TestEqual(TEXT("陆上火山不把陆地压到海面下"), LandLost, 0);
	TestTrue(TEXT("宏观场已写"), F.VolcanoMask.Res == F.PlateId.Res && F.VolcanicType.Res == F.PlateId.Res);
	return true;
}

#endif
