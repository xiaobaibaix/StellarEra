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
	AddInfo(FString::Printf(TEXT("板块数 %d"), A.Num()));

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

	// 运动：速度在范围内、中心处速度大小 = Speed 且与径向正交；覆盖生效；边界类型由相对运动定。
	bool bSpeeds = true;
	for (const FGeoPlate& Pl : A.Plates)
	{
		bSpeeds &= Pl.Speed >= C.Plates.SpeedMin && Pl.Speed <= C.Plates.SpeedMax;
		bSpeeds &= FMath::IsNearlyEqual(Pl.VelocityAt(Pl.Center).Size(), Pl.Speed, 1e-3f * FMath::Max(Pl.Speed, 1.f));
		bSpeeds &= FMath::Abs(FVector3f::DotProduct(Pl.VelocityAt(Pl.Center), Pl.Center)) < 1e-3f;
	}
	TestTrue(TEXT("板块速度在范围内且沿球面"), bSpeeds);
	{
		FPlanetRecipeConfig O = C;
		O.Plates.Overrides.Add({ 2, true, 25.f, true, 90.f });
		const FGeoPlateFrame F2 = GeoPlates::Build(O);
		TestTrue(TEXT("覆盖速度生效"), FMath::IsNearlyEqual(F2.Plates[2].Speed, 25.f));
		TestTrue(TEXT("覆盖不改其它板块"), F2.Plates[3].Speed == A.Plates[3].Speed);
		TestTrue(TEXT("覆盖不改板块中心"), F2.Plates[2].Center == A.Plates[2].Center);
		TestTrue(TEXT("覆盖进哈希"), O.Hash() != C.Hash());
	}
	{
		// 两块板块对撞 → 汇聚，背离 → 离散，平行错动 → 转换。
		FGeoPlateFrame T;
		T.Plates.SetNum(2);
		T.Plates[0].Center = FVector3f(0.f, -0.2f, 1.f).GetSafeNormal();
		T.Plates[1].Center = FVector3f(0.f, 0.2f, 1.f).GetSafeNormal();
		const FVector3f Mid(0.f, 0.f, 1.f);
		const auto Set = [&T](int32 i, const FVector3f& V) { T.Plates[i].Omega = FVector3f::CrossProduct(T.Plates[i].Center, V); };
		Set(0, FVector3f(0.f, 5.f, 0.f)); Set(1, FVector3f(0.f, -5.f, 0.f));
		TestEqual(TEXT("对撞 = 汇聚"), int32(GeoPlates::BoundaryMotion(T, Mid, 0, 1, C.Plates).Type), 1);
		Set(0, FVector3f(0.f, -5.f, 0.f)); Set(1, FVector3f(0.f, 5.f, 0.f));
		TestEqual(TEXT("背离 = 离散"), int32(GeoPlates::BoundaryMotion(T, Mid, 0, 1, C.Plates).Type), 4);
		Set(0, FVector3f(5.f, 0.f, 0.f)); Set(1, FVector3f(-5.f, 0.f, 0.f));
		TestEqual(TEXT("错动 = 转换"), int32(GeoPlates::BoundaryMotion(T, Mid, 0, 1, C.Plates).Type), 5);
	}

	// warp 只改方向不改长度
	const FVector3f W = GeoPlates::WarpDirection(A, FVector3f(0.f, 0.f, 1.f), C.Plates);
	TestTrue(TEXT("warp 输出是单位向量"), FMath::Abs(W.Size() - 1.f) < 1e-4f);
	TestTrue(TEXT("warp 确实挪了方向"), !W.Equals(FVector3f(0.f, 0.f, 1.f), 1e-3f));
	return true;
}

#endif
