// 阶段 2 让地表不再是球面：三入口同源、值域覆盖、岩性分叉三件事必须同时成立。
#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "PlanetBakePipeline.h"
#include "PlanetShapeEval.h"
#include "PlanetMaterialEval.h"
#include "GeoLithology.h"
#include "GeoRng.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	FVector3f RandomUnitDir(FGeoRng& Rng)
	{
		const float Z = Rng.Range(-1.f, 1.f), Phi = Rng.Range(0.f, 2.f * PI), S = FMath::Sqrt(1.f - Z * Z);
		return FVector3f(S * FMath::Cos(Phi), S * FMath::Sin(Phi), Z);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGeoEvalStage02Test, "StellarEra.Planet.Geo.Eval.Stage02",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGeoEvalStage02Test::RunTest(const FString& Parameters)
{
	FPlanetRecipeConfig Config;
	Config.FaceRes = 256;   // 测速度；全尺寸在 Task 5
	FPlanetBakeReport Report;
	const TSharedPtr<const FPlanetSnapshot> Snap = FPlanetBakePipeline::MakeDefault().Bake(Config, Report);
	if (!TestTrue(TEXT("默认管线（阶段 1–5）烘焙成功"), Snap.IsValid())) { return false; }
	TestEqual(TEXT("默认管线含六个阶段"), FPlanetBakePipeline::MakeDefault().Sorted().Num(), 6);

	FGeoRng Rng = FGeoRng::Derive(21, 0, 0);

	// 三入口同源：SurfaceRadius 与 Value 的零点必须一致，否则网格与求值核分叉。
	{
		float Worst = 0.f;
		for (int32 i = 0; i < 100; ++i)
		{
			const FVector3f Dir = RandomUnitDir(Rng);
			const double Rs = PlanetShapeEval::SurfaceRadius(*Snap, Dir);
			Worst = FMath::Max(Worst, FMath::Abs(PlanetShapeEval::Value(*Snap, FVector3d(Dir) * Rs)));
		}
		TestTrue(FString::Printf(TEXT("100 个方向地表处 |Value| < 0.05（最差 %.2e）"), Worst), Worst < 0.05f);
	}

	// 值域覆盖：盒子跨陆地台面与洋盆底，实测密度必须落在 ValueRange 内。
	{
		bool bCovers = true;
		float MinV = TNumericLimits<float>::Max(), MaxV = -TNumericLimits<float>::Max();
		for (int32 i = 0; i < 10000; ++i)
		{
			const FVector3d C = FVector3d(RandomUnitDir(Rng)) * (Snap->R0 + double(Rng.Range(-800.f, 800.f)));
			const FVector3d Half(64.0);
			const FGeoRange Range = PlanetShapeEval::ValueRange(*Snap, C - Half, C + Half);
			const FVector3d P(C.X + double(Rng.Range(-64.f, 64.f)),
				C.Y + double(Rng.Range(-64.f, 64.f)), C.Z + double(Rng.Range(-64.f, 64.f)));
			const float V = PlanetShapeEval::Value(*Snap, P);
			MinV = FMath::Min(MinV, V);
			MaxV = FMath::Max(MaxV, V);
			bCovers &= Range.Contains(V);
		}
		TestTrue(FString::Printf(TEXT("1e4 个点都落在 ValueRange 内（实测 V ∈ [%.1f, %.1f]）"), MinV, MaxV), bCovers);
	}

	TestTrue(FString::Printf(TEXT("高程非平凡：Min=%.1f < R0=%.1f < Max=%.1f"),
		Snap->MinSurfaceRadius, Snap->R0, Snap->MaxSurfaceRadius),
		Snap->MinSurfaceRadius < Snap->R0 && Snap->R0 < Snap->MaxSurfaceRadius);

	// 岩性分叉：地表下 20 体素仍在地壳层里，岩性由该方向的海平面判据决定。
	{
		int32 Land = 0, Ocean = 0, BadLand = 0, BadOcean = 0;
		for (int32 i = 0; i < 4000; ++i)
		{
			const FVector3f Dir = RandomUnitDir(Rng);
			const float E = Snap->Elevation.Sample(Dir);
			if (E <= 50.f && E >= -300.f) { continue; }
			const FVector3d P = FVector3d(Dir) * (PlanetShapeEval::SurfaceRadius(*Snap, Dir) - 20.0);
			const uint8 Lithology = PlanetMaterialEval::Sample(*Snap, P).Lithology;
			if (E > 50.f)
			{
				++Land;
				BadLand += Lithology != GeoLithology::ContinentalCrust ? 1 : 0;
			}
			else
			{
				++Ocean;
				BadOcean += Lithology != GeoLithology::OceanicCrust ? 1 : 0;
			}
		}
		TestTrue(FString::Printf(TEXT("陆地样本 ≥ 50（实得 %d）"), Land), Land >= 50);
		TestTrue(FString::Printf(TEXT("洋壳样本 ≥ 50（实得 %d）"), Ocean), Ocean >= 50);
		TestEqual(TEXT("Elevation > +50 处都是陆壳"), BadLand, 0);
		TestEqual(TEXT("Elevation < -300 处都是洋壳"), BadOcean, 0);

		int32 LandDirs = 0;
		const int32 Probes = 4096;
		for (int32 i = 0; i < Probes; ++i)
		{
			LandDirs += Snap->Elevation.Sample(RandomUnitDir(Rng)) > 0.f ? 1 : 0;
		}
		AddInfo(FString::Printf(TEXT("陆地 (E>+50) 样本 %d，洋壳 (E<-300) 样本 %d，E>0 方向占比 %.3f"),
			Land, Ocean, float(LandDirs) / float(Probes)));
	}

	// 快照哈希敏感：陆地占比进配方 → 大陆形状变 → ContentHash 必须变。
	{
		FPlanetRecipeConfig Lower = Config;
		Lower.Continents.LandFrac = 0.25f;
		FPlanetBakeReport LowerReport;
		const TSharedPtr<const FPlanetSnapshot> LowerSnap = FPlanetBakePipeline::MakeDefault().Bake(Lower, LowerReport);
		TestTrue(TEXT("改陆地占比仍能烘焙"), LowerSnap.IsValid());
		if (LowerSnap)
		{
			TestTrue(TEXT("改陆地占比快照哈希变"), LowerSnap->ContentHash != Snap->ContentHash);
		}
	}

	AddInfo(FString::Printf(TEXT("上下界 [%.1f, %.1f]，R0 %.1f，快照 %.2f MB（预算 60 MB）"),
		Snap->MinSurfaceRadius, Snap->MaxSurfaceRadius, Snap->R0,
		Snap->GetAllocatedSize() / 1048576.0));
	return true;
}

#endif
