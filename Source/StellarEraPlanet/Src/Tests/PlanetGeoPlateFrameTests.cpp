// 板块框架是阶段 2 的根：加权 Voronoi 决定「谁在哪」，这里守住确定性与覆盖性。
#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "GeoPlates.h"
#include "GeoRng.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGeoPlateFrameTest, "StellarEra.Planet.Geo.PlateFrame",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGeoPlateFrameTest::RunTest(const FString& Parameters)
{
	FPlanetRecipeConfig C;                       // 默认 R=40000、PlateCount=7、Seed=1337
	const FGeoPlateFrame A = GeoPlates::Build(C);
	const FGeoPlateFrame B = GeoPlates::Build(C);
	TestEqual(TEXT("板块数"), A.Num(), 7);
	TestTrue(TEXT("同配方两次构建逐位一致"), A.Hash() == B.Hash());
	TestTrue(TEXT("板块种子不全同"), A.Plates[0].Center != A.Plates[1].Center);
	TestTrue(TEXT("核数在 6–16"), A.Cores.Num() >= 6 && A.Cores.Num() <= 16);
	TestTrue(TEXT("核半径 ≥ 0.05R"), A.Cores[0].RadiusRatio >= 0.05f);
	AddInfo(FString::Printf(TEXT("核数 %d，板块数 %d"), A.Cores.Num(), A.Num()));

	// 覆盖性：任意方向都落到一块板块，且每块都被访问到；最大板块占比不能吞掉整颗星
	TArray<int32> Hits; Hits.SetNumZeroed(A.Num());
	FGeoRng Rng = FGeoRng::Derive(3, 0, 0);
	int32 MaxHits = 0;
	for (int32 i = 0; i < 4000; ++i)
	{
		const float Z = Rng.Range(-1.f, 1.f), Phi = Rng.Range(0.f, 2.f * PI), S = FMath::Sqrt(1.f - Z * Z);
		const FVector3f D(S * FMath::Cos(Phi), S * FMath::Sin(Phi), Z);
		int32 Near, Second; float DN, DS;
		GeoPlates::NearestTwo(A, D, Near, Second, DN, DS);
		TestTrue(TEXT("最近板块下标合法"), Near >= 0 && Near < A.Num());
		TestTrue(TEXT("次近距离不小于最近"), DS >= DN);
		++Hits[Near];
	}
	for (int32 p = 0; p < A.Num(); ++p)
	{
		TestTrue(FString::Printf(TEXT("板块 %d 有覆盖"), p), Hits[p] > 0);
		MaxHits = FMath::Max(MaxHits, Hits[p]);
	}
	const float MaxShare = float(MaxHits) / 4000.f;
	TestTrue(FString::Printf(TEXT("最大板块占比 %.2f ∈ [0.10, 0.55]"), MaxShare), MaxShare > 0.10f && MaxShare < 0.55f);
	AddInfo(FString::Printf(TEXT("最大板块占比 %.3f（%d/4000）"), MaxShare, MaxHits));

	// 边界类型都在 1–5
	bool bTypes = true;
	for (int32 p = 0; p < A.Num(); ++p) for (int32 q = p + 1; q < A.Num(); ++q)
	{ const uint8 T = A.BoundaryTypeBetween(p, q); bTypes &= (T >= 1 && T <= 5); }
	TestTrue(TEXT("边界类型都在 1–5"), bTypes);

	// warp 只改方向不改长度
	const FVector3f W = GeoPlates::WarpDirection(A, FVector3f(0.f, 0.f, 1.f), C.Plates.WarpAmp);
	TestTrue(TEXT("warp 输出是单位向量"), FMath::Abs(W.Size() - 1.f) < 1e-4f);
	TestTrue(TEXT("warp 确实挪了方向"), !W.Equals(FVector3f(0.f, 0.f, 1.f), 1e-3f));
	return true;
}

#endif
