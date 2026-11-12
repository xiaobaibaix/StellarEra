// BVH 漏掉基元不会报错，只表现为「矿变少」；值域估窄会让插件整块跳过、表面出洞。两条都只能靠对拍守住。
#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "GeoPrimitive.h"
#include "GeoPrimitiveIndex.h"
#include "GeoRng.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGeoPrimitiveTest, "StellarEra.Planet.Geo.Primitive",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGeoPrimitiveTest::RunTest(const FString& Parameters)
{
	FGeoRng Rng = FGeoRng::Derive(42, 0, 0);
	const auto RandomPoint = [&Rng](float Extent)
	{
		return FVector3f(Rng.Range(-Extent, Extent), Rng.Range(-Extent, Extent), Rng.Range(-Extent, Extent));
	};

	const FGeoPrimitive Blob = FGeoPrimitive::MakeBlob(FVector3f(10.f, 0.f, 0.f), FVector3f(4.f, 4.f, 2.f), 0.05f);
	TestEqual(TEXT("团块中心强度 1"), GeoPrimitiveEvaluate(Blob, FVector3f(10.f, 0.f, 0.f)), 1.f);
	TestEqual(TEXT("团块外强度 0"), GeoPrimitiveEvaluate(Blob, FVector3f(10.f, 0.f, 2.5f)), 0.f);

	bool bRangeCovers = true;
	for (int32 i = 0; i < 10000; ++i)
	{
		const FVector3f C = RandomPoint(16.f);
		const float H = Rng.Range(0.5f, 6.f);
		const FBox3f Box(C - FVector3f(H), C + FVector3f(H));
		const FGeoRange R = GeoPrimitiveRange(Blob, Box);
		const FVector3f P(Rng.Range(Box.Min.X, Box.Max.X), Rng.Range(Box.Min.Y, Box.Max.Y), Rng.Range(Box.Min.Z, Box.Max.Z));
		bRangeCovers &= R.Contains(GeoPrimitiveEvaluate(Blob, P));
	}
	TestTrue(TEXT("值域覆盖盒内任意采样"), bRangeCovers);

	TArray<FGeoPrimitive> Prims;
	for (int32 i = 0; i < 1000; ++i)
	{
		const FVector3f Radii(Rng.Range(1.f, 8.f), Rng.Range(1.f, 8.f), Rng.Range(1.f, 8.f));
		Prims.Add(FGeoPrimitive::MakeBlob(RandomPoint(500.f), Radii, 0.05f));
	}
	FGeoPrimitiveIndex Index;
	Index.Build(Prims);

	bool bMatch = true;
	int32 Hits = 0;
	for (int32 i = 0; i < 1000; ++i)
	{
		const FVector3f P = (i % 2 == 0) ? Prims[i].Center : RandomPoint(500.f);
		TArray<int32> Brute, Fast;
		for (int32 k = 0; k < Prims.Num(); ++k) { if (Prims[k].Bounds.IsInsideOrOn(P)) { Brute.Add(k); } }
		Index.QueryPoint(P, [&Fast](int32 K) { Fast.Add(K); });
		Brute.Sort(); Fast.Sort();
		bMatch &= (Brute == Fast);
		Hits += Brute.Num();
	}
	TestTrue(TEXT("BVH 点查询 == 暴力遍历"), bMatch);
	TestTrue(TEXT("对拍样本里确实有命中"), Hits >= 500);

	FGeoPrimitiveIndex Again;
	Again.Build(Prims);
	TestTrue(TEXT("同输入同哈希"), Index.Hash() == Again.Hash());
	return true;
}

#endif
