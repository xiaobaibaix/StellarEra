// 大陆场纯函数：球谐奇偶性、确定性、加权 CDF 与高程曲线的端点/单调性。
#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "GeoContinentField.h"
#include "GeoRng.h"
#include "Stage02Plates.h"
#include "PlanetDebugColor.h"

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
		TestTrue(TEXT("海岸处高程 0"), FMath::Abs(GeoContinentField::Elevation(Coast, P, 40000.0)) < 1e-3f);
		TestTrue(TEXT("Rank 0 = −OceanDepth"), FMath::IsNearlyEqual(GeoContinentField::Elevation(0.f, P, 40000.0), -P.OceanDepthRatio * 40000.f, 1e-2f));
		TestTrue(TEXT("Rank 1 = +LandHeight"), FMath::IsNearlyEqual(GeoContinentField::Elevation(1.f, P, 40000.0), P.LandHeightRatio * 40000.f, 1e-2f));
		bool bMono = true;
		float Prev = GeoContinentField::Elevation(0.f, P, 40000.0);
		for (int32 i = 1; i <= 4000; ++i)
		{
			const float E = GeoContinentField::Elevation(float(i) / 4000.f, P, 40000.0);
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

	// 调试色五档互不相同，海是蓝调、陆不是。
	{
		const float Es[5] = { -470.f, -20.f, 2.f, 60.f, 230.f };
		FColor Cs[5];
		for (int32 i = 0; i < 5; ++i) { Cs[i] = PlanetDebugColor::FromElevation(Es[i], 480.f, 240.f, -48.f); }
		for (int32 i = 0; i < 5; ++i) for (int32 j = i + 1; j < 5; ++j) { TestTrue(FString::Printf(TEXT("调试色 %d≠%d"), i, j), Cs[i] != Cs[j]); }
		TestTrue(TEXT("深海偏蓝"), Cs[0].B > Cs[0].R);
		TestTrue(TEXT("陆架偏蓝"), Cs[1].B > Cs[1].R);
		TestTrue(TEXT("平原不偏蓝"), Cs[3].G > Cs[3].B);
	}
	return true;
}

namespace
{
	struct FContinentFieldPattern
	{
		int32 Major = 0;          // 面积 ≥ 1% 星球的陆块数
		float LargestShare = 0.f; // 最大陆块 / 全部陆地
		int32 RingSuspects = 0;   // 质心方向落在海里的主陆块数
	};

	int32 ContinentFieldIndex(int32 N, int32 Face, int32 X, int32 Y) { return (Face * N + Y) * N + X; }

	/** 跨面邻接：越界的 (U,V) 交给 FaceToDir→DirToFace 落到相邻面。 */
	int32 ContinentFieldNeighbor(int32 N, int32 Face, int32 X, int32 Y, int32 DX, int32 DY)
	{
		const float U = (float(X + DX) + 0.5f) / float(N) * 2.f - 1.f;
		const float V = (float(Y + DY) + 0.5f) / float(N) * 2.f - 1.f;
		int32 F2; float U2, V2;
		FGeoField2D::DirToFace(FGeoField2D::FaceToDir(Face, U, V), F2, U2, V2);
		const int32 X2 = FMath::Clamp(int32((U2 + 1.f) * 0.5f * N), 0, N - 1);
		const int32 Y2 = FMath::Clamp(int32((V2 + 1.f) * 0.5f * N), 0, N - 1);
		return ContinentFieldIndex(N, F2, X2, Y2);
	}

	FContinentFieldPattern ContinentFieldAnalyze(const FGeoField2D& Elevation, int32 N)
	{
		FGeoField2D Grid;
		Grid.Init(N);
		TArray<uint8> Land; Land.SetNumZeroed(6 * N * N);
		TArray<double> W; W.SetNumZeroed(6 * N * N);
		TArray<FVector3f> Dir; Dir.SetNum(6 * N * N);
		double TotalW = 0.0, LandW = 0.0;
		for (int32 Face = 0; Face < 6; ++Face)
		for (int32 Y = 0; Y < N; ++Y)
		for (int32 X = 0; X < N; ++X)
		{
			const int32 I = ContinentFieldIndex(N, Face, X, Y);
			Dir[I] = Grid.TexelDir(Face, X, Y);
			W[I] = GeoContinentField::TexelWeight(Grid, X, Y);
			Land[I] = Elevation.Sample(Dir[I]) > 0.f ? 1 : 0;
			TotalW += W[I];
			LandW += Land[I] ? W[I] : 0.0;
		}

		FContinentFieldPattern Out;
		TArray<int32> Label; Label.Init(-1, 6 * N * N);
		TArray<int32> Stack;
		double Largest = 0.0;
		for (int32 Seed = 0; Seed < Label.Num(); ++Seed)
		{
			if (!Land[Seed] || Label[Seed] >= 0) { continue; }
			double Area = 0.0; FVector3d Centroid(0.0);
			Stack.Reset(); Stack.Add(Seed); Label[Seed] = Seed;
			while (Stack.Num())
			{
				const int32 I = Stack.Pop(EAllowShrinking::No);
				Area += W[I]; Centroid += FVector3d(Dir[I]) * W[I];
				const int32 Face = I / (N * N), Y = (I / N) % N, X = I % N;
				const int32 Nb[4] = { ContinentFieldNeighbor(N, Face, X, Y, 1, 0), ContinentFieldNeighbor(N, Face, X, Y, -1, 0),
					ContinentFieldNeighbor(N, Face, X, Y, 0, 1), ContinentFieldNeighbor(N, Face, X, Y, 0, -1) };
				for (const int32 J : Nb) { if (Land[J] && Label[J] < 0) { Label[J] = Seed; Stack.Add(J); } }
			}
			Largest = FMath::Max(Largest, Area);
			if (Area >= 0.01 * TotalW)
			{
				++Out.Major;
				const FVector3f C = FVector3f(Centroid.GetSafeNormal());
				Out.RingSuspects += Elevation.Sample(C) > 0.f ? 0 : 1;
			}
		}
		Out.LargestShare = float(Largest / FMath::Max(LandW, 1e-30));
		return Out;
	}

	FPlanetBakeFields ContinentFieldBake(int32 Degree, float Roughness, int32 FaceRes)
	{
		FPlanetRecipeConfig C;
		C.FaceRes = FaceRes;
		C.Continents.FrameDegree = Degree;
		C.Continents.CoastRoughness = Roughness;
		FPlanetBakeFields F;
		FPlanetBakeReport R;
		FPlanetBakeContext Ctx{ C, F, FGeoRng::Derive(C.Seed, 2, 0), R };
		FStage02Plates().Bake(Ctx);
		return F;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGeoContinentFieldPatternTest, "StellarEra.Planet.Geo.ContinentField.Pattern",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGeoContinentFieldPatternTest::RunTest(const FString& Parameters)
{
	const FContinentFieldPattern P1 = ContinentFieldAnalyze(ContinentFieldBake(1, 0.35f, 256).Elevation, 128);
	const FContinentFieldPattern P4 = ContinentFieldAnalyze(ContinentFieldBake(4, 0.35f, 256).Elevation, 128);
	const FContinentFieldPattern P8 = ContinentFieldAnalyze(ContinentFieldBake(8, 0.8f, 256).Elevation, 128);
	AddInfo(FString::Printf(TEXT("Degree1: 主陆块 %d，最大占 %.3f，质心在海 %d"), P1.Major, P1.LargestShare, P1.RingSuspects));
	AddInfo(FString::Printf(TEXT("Degree4: 主陆块 %d，最大占 %.3f，质心在海 %d"), P4.Major, P4.LargestShare, P4.RingSuspects));
	AddInfo(FString::Printf(TEXT("Degree8: 主陆块 %d，最大占 %.3f，质心在海 %d"), P8.Major, P8.LargestShare, P8.RingSuspects));

	TestTrue(TEXT("超大陆：最大陆块 ≥ 80% 陆地"), P1.LargestShare >= 0.8f);
	TestTrue(TEXT("群岛主陆块多于类地球"), P8.Major > P4.Major);
	TestTrue(TEXT("类地球最大陆块 < 超大陆"), P4.LargestShare < P1.LargestShare);
	TestTrue(TEXT("环形陆地回归：类地球主陆块质心不在海里"), P4.RingSuspects * 2 <= P4.Major);

	// 全尺寸坡度与接缝：相邻 texel 高差 / 弧长（体素）。旧模型陆坡处约 3（≈70°）。
	const FPlanetBakeFields Full = ContinentFieldBake(4, 0.35f, 1024);
	const FGeoField2D& E = Full.Elevation;
	const double Radius = 40000.0;
	float MaxSlope = 0.f, MaxSeam = 0.f, MaxInner = 0.f, SteepE0 = 0.f, SteepE1 = 0.f, SteepArc = 0.f;
	TArray<float> Slopes;
	Slopes.Reserve(6 * E.Res * E.Res);
	for (int32 Face = 0; Face < 6; ++Face)
	for (int32 Y = 0; Y < E.Res; ++Y)
	for (int32 X = 0; X + 1 < E.Res; ++X)
	{
		const FVector3f A = E.TexelDir(Face, X, Y), B = E.TexelDir(Face, X + 1, Y);
		const float Arc = float(Radius) * FMath::Acos(FMath::Clamp(FVector3f::DotProduct(A, B), -1.f, 1.f));
		const float D = FMath::Abs(E.At(Face, X + 1, Y) - E.At(Face, X, Y));
		Slopes.Add(D / FMath::Max(Arc, 1e-3f));
		if (D / FMath::Max(Arc, 1e-3f) > MaxSlope)
		{
			MaxSlope = D / FMath::Max(Arc, 1e-3f);
			SteepE0 = E.At(Face, X, Y); SteepE1 = E.At(Face, X + 1, Y); SteepArc = Arc;
		}
		MaxInner = FMath::Max(MaxInner, D);
	}
	// 棱上：沿每条面边界取边上 texel 方向两侧各外推半 texel 采样。
	for (int32 Face = 0; Face < 6; ++Face)
	for (int32 Y = 0; Y < E.Res; ++Y)
	{
		const float V = (float(Y) + 0.5f) / float(E.Res) * 2.f - 1.f, Du = 1.f / float(E.Res);
		const FVector3f In = FGeoField2D::FaceToDir(Face, 1.f - Du, V), Out = FGeoField2D::FaceToDir(Face, 1.f + Du, V);
		MaxSeam = FMath::Max(MaxSeam, FMath::Abs(E.Sample(In) - E.Sample(Out)));
	}
	AddInfo(FString::Printf(TEXT("全尺寸：最大坡度 %.3f（高差/水平），面内相邻最大高差 %.2f，棱两侧最大高差 %.2f"),
		MaxSlope, MaxInner, MaxSeam));
	Slopes.Sort();
	const float P99 = Slopes[int32(0.99 * (Slopes.Num() - 1))];
	AddInfo(FString::Printf(TEXT("坡度 99 分位 %.3f"), P99));
	AddInfo(FString::Printf(TEXT("最陡处：E %.1f → %.1f，水平 %.1f 体素"), SteepE0, SteepE1, SteepArc));
	// 实测（2026-11-14）：最大 0.70 出在陆架小岛的海岸（符号钳住留下 20 m 台阶），99 分位 0.19；旧陆核模型约 3。
	TestTrue(FString::Printf(TEXT("无陡崖：最大坡度 %.3f < 1.0"), MaxSlope), MaxSlope < 1.0f);
	TestTrue(FString::Printf(TEXT("坡度 99 分位 %.3f < 0.3"), P99), P99 < 0.3f);
	TestTrue(FString::Printf(TEXT("无接缝：棱两侧 %.2f ≤ 3×面内 %.2f + 1"), MaxSeam, MaxInner), MaxSeam <= 3.f * MaxInner + 1.f);
	return true;
}

#endif
