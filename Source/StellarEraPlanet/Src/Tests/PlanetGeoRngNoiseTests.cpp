// 确定性是存档只存配方的前提：同种子必须同流，噪声必须连续且有界。
#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "GeoRng.h"
#include "GeoNoise.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGeoRngNoiseTest, "StellarEra.Planet.Geo.RngNoise",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGeoRngNoiseTest::RunTest(const FString& Parameters)
{
	FGeoRng A = FGeoRng::Derive(1337, 1, GeoHash::Str("test"));
	FGeoRng B = FGeoRng::Derive(1337, 1, GeoHash::Str("test"));
	FGeoRng C = FGeoRng::Derive(1337, 2, GeoHash::Str("test"));
	bool bSame = true, bDiffer = false, bInRange = true;
	for (int32 i = 0; i < 1000; ++i)
	{
		const uint64 a = A.NextU64(), b = B.NextU64(), c = C.NextU64();
		bSame &= (a == b);
		bDiffer |= (a != c);
		const float F = A.NextFloat();
		B.NextFloat();
		bInRange &= (F >= 0.f && F < 1.f);
	}
	TestTrue(TEXT("同种子同阶段同用途 → 同一条流"), bSame);
	TestTrue(TEXT("换阶段 → 不同的流"), bDiffer);
	TestTrue(TEXT("NextFloat ∈ [0,1)"), bInRange);

	FGeoRng P = FGeoRng::Derive(7, 0, 0);
	float MinV = 1e9f, MaxV = -1e9f, MaxJump = 0.f, MinF = 1e9f, MaxF = -1e9f;
	bool bDeterministic = true;
	for (int32 i = 0; i < 5000; ++i)
	{
		const FVector3f X(P.Range(-50.f, 50.f), P.Range(-50.f, 50.f), P.Range(-50.f, 50.f));
		const float V = GeoNoise::Value3(99, X);
		bDeterministic &= (V == GeoNoise::Value3(99, X));
		MinV = FMath::Min(MinV, V);
		MaxV = FMath::Max(MaxV, V);
		MaxJump = FMath::Max(MaxJump, FMath::Abs(V - GeoNoise::Value3(99, X + FVector3f(1e-3f, 0.f, 0.f))));
		const float Fb = GeoNoise::Fbm3(99, X, 4);
		MinF = FMath::Min(MinF, Fb);
		MaxF = FMath::Max(MaxF, Fb);
	}
	TestTrue(TEXT("Value3 确定"), bDeterministic);
	TestTrue(TEXT("Value3 ∈ [-1,1]"), MinV >= -1.f && MaxV <= 1.f);
	TestTrue(TEXT("Value3 有实际起伏"), MaxV - MinV > 1.f);
	TestTrue(TEXT("Value3 连续（1e-3 步长跳变 < 0.01）"), MaxJump < 0.01f);
	TestTrue(TEXT("Fbm3 ∈ [-1,1]"), MinF >= -1.f && MaxF <= 1.f);

	TestTrue(TEXT("Fade 端点与中点"), GeoNoise::Fade(0.f) == 0.f && GeoNoise::Fade(1.f) == 1.f && FMath::IsNearlyEqual(GeoNoise::Fade(0.5f), 0.5f, 1e-6f));
	TestTrue(TEXT("Fade 是五次缓动（线性会得 0.25）"), FMath::IsNearlyEqual(GeoNoise::Fade(0.25f), 0.103515625f, 1e-6f));

	bool bLattice = true;
	int32 Differ = 0;
	for (int32 i = 0; i < 200; ++i)
	{
		const int32 Ix = i - 100, Iy = (i * 7) % 53 - 26, Iz = (i * 13) % 41 - 20;
		bLattice &= (GeoNoise::Value3(99, FVector3f(float(Ix), float(Iy), float(Iz))) == GeoNoise::Lattice(99, Ix, Iy, Iz));
		const FVector3f Q(i * 0.37f + 0.1f, i * 0.11f - 3.3f, i * 0.53f + 0.7f);
		Differ += (GeoNoise::Value3(99, Q) != GeoNoise::Value3(100, Q)) ? 1 : 0;
	}
	TestTrue(TEXT("格点处 Value3 等于 Lattice"), bLattice);
	TestTrue(TEXT("换种子 → 噪声不同"), Differ > 190);
	return true;
}

#endif
