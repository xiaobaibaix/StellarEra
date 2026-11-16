// 预算与值域保守性只有全尺寸能验：小分辨率下两者都会假通过，验收口径必须跑默认配方。
#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "PlanetBakePipeline.h"
#include "PlanetShapeEval.h"
#include "GeoRng.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	FVector3f AcceptanceRandomUnitDir(FGeoRng& Rng)
	{
		const float Z = Rng.Range(-1.f, 1.f), Phi = Rng.Range(0.f, 2.f * PI), S = FMath::Sqrt(1.f - Z * Z);
		return FVector3f(S * FMath::Cos(Phi), S * FMath::Sin(Phi), Z);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGeoStage02AcceptanceTest, "StellarEra.Planet.Geo.Stage02Acceptance",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGeoStage02AcceptanceTest::RunTest(const FString& Parameters)
{
	FPlanetRecipeConfig C;   // 默认即 R=40000 / FaceRes=1024 / Seed=1337；降规格会让下面的阈值失去意义
	FPlanetBakeReport Report;
	const double T0 = FPlatformTime::Seconds();
	const TSharedPtr<const FPlanetSnapshot> Snap = FPlanetBakePipeline::MakeDefault().Bake(C, Report);
	const double Seconds = FPlatformTime::Seconds() - T0;

	if (!TestTrue(TEXT("全尺寸烘焙成功"), Snap.IsValid())) { return false; }
	TestFalse(FString::Printf(TEXT("烘焙报告无错误（%s）"), *FString::Join(Report.Errors, TEXT(" | "))), Report.HasErrors());

	const double MB = double(Snap->GetAllocatedSize()) / 1048576.0;
	const double Band = (Snap->MaxSurfaceRadius - Snap->MinSurfaceRadius) / Snap->R0;
	// 预算覆盖整条默认管线（阶段 1–7）；墙钟对机器负载敏感（空闲 7.7 s，另开游戏 + 语言服务器时 13–16 s），取 20 s。
	TestTrue(FString::Printf(TEXT("烘焙 %.2f s ≤ 20 s"), Seconds), Seconds <= 20.0);
	TestTrue(FString::Printf(TEXT("快照 %.1f MB ≤ 60 MB"), MB), Snap->GetAllocatedSize() <= 60ull * 1024 * 1024);
	TestEqual(FString::Printf(TEXT("Elevation 全分辨率（Res=%d）"), Snap->Elevation.Res), Snap->Elevation.Res, 1024);
	TestTrue(FString::Printf(TEXT("地表上下界包住 R0：Min=%.1f < R0=%.1f < Max=%.1f"),
		Snap->MinSurfaceRadius, Snap->R0, Snap->MaxSurfaceRadius),
		Snap->MinSurfaceRadius < Snap->R0 && Snap->R0 < Snap->MaxSurfaceRadius);
	TestTrue(FString::Printf(TEXT("高程带宽 %.2f%% R < 15%%"), Band * 100.0), Band < 0.15);
	AddInfo(FString::Printf(TEXT("烘焙 %.2f s，快照 %.1f MB，地表 [%.1f, %.1f]，R0 %.1f"),
		Seconds, MB, Snap->MinSurfaceRadius, Snap->MaxSurfaceRadius, Snap->R0));
	for (const TPair<FName, double>& St : Report.StageSeconds) { AddInfo(FString::Printf(TEXT("阶段耗时 %s %.3f s"), *St.Key.ToString(), St.Value)); }
	const TPair<FName, double>* Stage6 = Report.StageSeconds.FindByPredicate([](const TPair<FName, double>& P) { return P.Key == TEXT("Stage06Erosion"); });
	TestTrue(TEXT("报告里有阶段 6 的计时"), Stage6 != nullptr);
	if (Stage6) { TestTrue(FString::Printf(TEXT("阶段 6 %.3f s ≤ 1.5 s"), Stage6->Value), Stage6->Value <= 1.5); }
	const TPair<FName, double>* Stage6b = Report.StageSeconds.FindByPredicate([](const TPair<FName, double>& P) { return P.Key == TEXT("Stage06bExposure"); });
	TestTrue(TEXT("报告里有阶段 6b 的计时"), Stage6b != nullptr);
	if (Stage6b) { TestTrue(FString::Printf(TEXT("阶段 6b %.3f s ≤ 0.5 s"), Stage6b->Value), Stage6b->Value <= 0.5); }

	FGeoRng Rng = FGeoRng::Derive(C.Seed, 5, 0);

	// 早退的正确性前提：盒子保守界一旦不包含实测值，体素线程会把本该实心的格子挖空。
	{
		bool bCovers = true;
		float MinV = TNumericLimits<float>::Max(), MaxV = -TNumericLimits<float>::Max();
		for (int32 i = 0; i < 100000; ++i)
		{
			const FVector3d BoxCenter = FVector3d(AcceptanceRandomUnitDir(Rng)) * (Snap->R0 + double(Rng.Range(-800.f, 800.f)));
			const FVector3d Half(64.0);
			const FGeoRange Range = PlanetShapeEval::ValueRange(*Snap, BoxCenter - Half, BoxCenter + Half);
			const FVector3d P(BoxCenter.X + double(Rng.Range(-64.f, 64.f)),
				BoxCenter.Y + double(Rng.Range(-64.f, 64.f)), BoxCenter.Z + double(Rng.Range(-64.f, 64.f)));
			const float V = PlanetShapeEval::Value(*Snap, P);
			MinV = FMath::Min(MinV, V);
			MaxV = FMath::Max(MaxV, V);
			bCovers &= Range.Contains(V);
		}
		TestTrue(FString::Printf(TEXT("1e5 个点都落在 ValueRange 内（实测 V ∈ [%.1f, %.1f]）"), MinV, MaxV), bCovers);
		AddInfo(FString::Printf(TEXT("ValueRange 抽样实测 V ∈ [%.1f, %.1f]"), MinV, MaxV));
	}

	// 均匀方向抽样本身就是面积加权：与目标差 > 0.02 说明烘焙侧的加权写错了。
	{
		const int32 Probes = 65536;
		int32 Land = 0;
		for (int32 i = 0; i < Probes; ++i)
		{
			Land += Snap->Elevation.Sample(AcceptanceRandomUnitDir(Rng)) > 0.f ? 1 : 0;
		}
		const double LandFrac = double(Land) / double(Probes);
		TestTrue(FString::Printf(TEXT("陆地占比 %.4f ≈ 0.30 ± 0.02"), LandFrac), FMath::Abs(LandFrac - 0.30) <= 0.02);
		AddInfo(FString::Printf(TEXT("陆地占比 %.4f（%d/%d 个方向 E>0）"), LandFrac, Land, Probes));
	}

	// 穿越次数：8 条大圆 × 512 步，只卡下限；上限随格局参数变。
	{
		int32 WorstCrossings = 0;
		FString PerCircle;
		for (int32 Circle = 0; Circle < 8; ++Circle)
		{
			FVector3f B1(Rng.Range(-1.f, 1.f), Rng.Range(-1.f, 1.f), Rng.Range(-1.f, 1.f));
			B1 = B1.IsNearlyZero() ? FVector3f::UpVector : B1.GetSafeNormal();
			const FVector3f Ref = FMath::Abs(B1.Z) < 0.9f ? FVector3f::UpVector : FVector3f::ForwardVector;
			const FVector3f B2 = FVector3f::CrossProduct(B1, Ref).GetSafeNormal();

			int32 Crossings = 0;
			bool bPrevLand = Snap->Elevation.Sample(B1) > 0.f;
			for (int32 i = 1; i <= 512; ++i)
			{
				const float A = 2.f * PI * float(i) / 512.f;
				const bool bLand = Snap->Elevation.Sample(B1 * FMath::Cos(A) + B2 * FMath::Sin(A)) > 0.f;
				if (bLand != bPrevLand) { ++Crossings; }
				bPrevLand = bLand;
			}
			PerCircle += FString::Printf(TEXT("%d "), Crossings);
			WorstCrossings = FMath::Max(WorstCrossings, Crossings);
		}
		TestTrue(FString::Printf(TEXT("8 条大圆最差穿越 %d ≥ 2"), WorstCrossings), WorstCrossings >= 2);
		AddInfo(FString::Printf(TEXT("8 条大圆穿越次数：%s（最差 %d）"), *PerCircle, WorstCrossings));
	}

	return true;
}

#endif
