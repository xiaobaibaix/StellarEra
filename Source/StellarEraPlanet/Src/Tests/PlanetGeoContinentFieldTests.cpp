// 大陆场纯函数：球谐奇偶性、确定性、加权 CDF 与高程曲线的端点/单调性。
#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "GeoContinentField.h"
#include "GeoRng.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	FVector3f ContinentFieldRandomDir(FGeoRng& Rng)
	{
		const float Z = Rng.Range(-1.f, 1.f), Phi = Rng.Range(0.f, 2.f * PI), S = FMath::Sqrt(1.f - Z * Z);
		return FVector3f(S * FMath::Cos(Phi), S * FMath::Sin(Phi), Z);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGeoContinentFieldTest, "StellarEra.Planet.Geo.ContinentField",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGeoContinentFieldTest::RunTest(const FString& Parameters)
{
	FGeoRng Rng = FGeoRng::Derive(31, 0, 0);

	// l=1 是奇函数：F(d) = −F(−d)。球谐写错（归一化或 m 下标）最先在这里露馅。
	{
		FPlanetRecipeConfig C;
		C.Continents.FrameDegree = 1;
		const GeoContinentField::FFrame F = GeoContinentField::BuildFrame(C);
		float Worst = 0.f, MaxAbs = 0.f;
		for (int32 i = 0; i < 200; ++i)
		{
			const FVector3f D = ContinentFieldRandomDir(Rng);
			const float A = GeoContinentField::EvalFrame(F, D);
			Worst = FMath::Max(Worst, FMath::Abs(A + GeoContinentField::EvalFrame(F, -D)));
			MaxAbs = FMath::Max(MaxAbs, FMath::Abs(A));
		}
		TestTrue(FString::Printf(TEXT("l=1 奇对称（最差 %.2e）"), Worst), Worst < 1e-5f);
		TestTrue(FString::Printf(TEXT("l=1 非零（max %.3f）"), MaxAbs), MaxAbs > 1e-3f);
	}

	// 实球谐正交归一：单系数为 1 时 ∫Y² dΩ = 1，蒙特卡罗均值 × 4π ≈ 1。
	{
		GeoContinentField::FFrame F;
		F.Degree = 4;
		int32 Bad = 0;
		for (int32 Index = 1; Index < 25; ++Index)
		{
			F.Coeffs.Init(0.0, 25);
			F.Coeffs[Index] = 1.0;
			double Sum = 0.0;
			FGeoRng R2 = FGeoRng::Derive(77, 0, uint64(Index));
			const int32 N = 40000;
			for (int32 i = 0; i < N; ++i)
			{
				const float V = GeoContinentField::EvalFrame(F, ContinentFieldRandomDir(R2));
				Sum += double(V) * V;
			}
			const double Norm = Sum / N * 4.0 * PI;
			if (FMath::Abs(Norm - 1.0) > 0.06) { ++Bad; AddInfo(FString::Printf(TEXT("系数 %d 范数 %.3f"), Index, Norm)); }
		}
		TestEqual(TEXT("24 个基函数都归一（±6%）"), Bad, 0);
	}

	// 确定性与种子敏感。
	{
		FPlanetRecipeConfig A, B = A, S = A;
		S.Seed = A.Seed + 1;
		const GeoContinentField::FFrame FA = GeoContinentField::BuildFrame(A), FB = GeoContinentField::BuildFrame(B);
		const GeoContinentField::FFrame FS = GeoContinentField::BuildFrame(S);
		TestTrue(TEXT("同配方系数逐位一致"), FA.Coeffs == FB.Coeffs);
		TestTrue(TEXT("换种子系数不同"), FA.Coeffs != FS.Coeffs);
		TestEqual(TEXT("Degree 4 → 25 个系数"), FA.Coeffs.Num(), 25);
		TestEqual(TEXT("l=0 系数恒 0"), FA.Coeffs[0], 0.0);
		const FVector3f D = FVector3f(0.3f, -0.5f, 0.8f).GetSafeNormal();
		const uint64 Seed = GeoContinentField::DetailSeed(A);
		TestEqual(TEXT("细节噪声确定"), GeoContinentField::EvalDetail(Seed, A.Continents, D),
			GeoContinentField::EvalDetail(Seed, A.Continents, D));
	}

	// 高程曲线：海岸处 0、两端为 −OceanDepth / +LandHeight、单调不减。
	{
		const FPlanetContinentParams P;
		const float Coast = 1.f - P.LandFrac;
		TestTrue(TEXT("海岸处高程 0"), FMath::Abs(GeoContinentField::Elevation(Coast, P)) < 1e-3f);
		TestTrue(TEXT("Rank 0 = −OceanDepth"), FMath::IsNearlyEqual(GeoContinentField::Elevation(0.f, P), -P.OceanDepthVoxels, 1e-2f));
		TestTrue(TEXT("Rank 1 = +LandHeight"), FMath::IsNearlyEqual(GeoContinentField::Elevation(1.f, P), P.LandHeightVoxels, 1e-2f));
		bool bMono = true;
		float Prev = GeoContinentField::Elevation(0.f, P);
		for (int32 i = 1; i <= 4000; ++i)
		{
			const float E = GeoContinentField::Elevation(float(i) / 4000.f, P);
			bMono &= E >= Prev - 1e-4f;
			Prev = E;
		}
		TestTrue(TEXT("高程曲线单调不减"), bMono);
	}

	// 加权 CDF：线性场的排名单调、端点 0/1；面中心权重 > 面角。
	{
		FGeoField2D Field;
		Field.Init(64);
		Field.Fill([](const FVector3f& D) { return D.Z; }, true);
		const GeoContinentField::FWeightedCdf Cdf = GeoContinentField::BuildCdf(Field);
		TestTrue(TEXT("Rank(Min) ≈ 0"), Cdf.Rank(Cdf.Min) < 1e-3f);
		TestTrue(TEXT("Rank(Max) = 1"), FMath::IsNearlyEqual(Cdf.Rank(Cdf.Max), 1.f, 1e-6f));
		TestTrue(TEXT("Rank 单调"), Cdf.Rank(-0.5f) < Cdf.Rank(0.f) && Cdf.Rank(0.f) < Cdf.Rank(0.5f));
		// z 在球面上按面积均匀分布（阿基米德），加权排名应 ≈ (z+1)/2；不加权会明显偏。
		TestTrue(FString::Printf(TEXT("Rank(0) ≈ 0.5（%.4f）"), Cdf.Rank(0.f)), FMath::Abs(Cdf.Rank(0.f) - 0.5f) < 0.01f);
		TestTrue(FString::Printf(TEXT("Rank(0.5) ≈ 0.75（%.4f）"), Cdf.Rank(0.5f)), FMath::Abs(Cdf.Rank(0.5f) - 0.75f) < 0.01f);
		TestTrue(TEXT("面中心权重 > 面角"),
			GeoContinentField::TexelWeight(Field, 32, 32) > GeoContinentField::TexelWeight(Field, 0, 0));
	}
	return true;
}

#endif
