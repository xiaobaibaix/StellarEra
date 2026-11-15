#include "GeoContinentField.h"
#include "Async/ParallelFor.h"
#include "GeoNoise.h"
#include "GeoRng.h"

namespace
{
	constexpr int32 ContinentCdfBins = 65536;
	constexpr int32 ContinentMaxDegree = 12;

	/** 连带勒让德 P_l^m(x)（不含 Condon–Shortley 相位，实球谐里符号无意义）。 */
	void ContinentLegendre(int32 L, double X, double Out[ContinentMaxDegree + 1][ContinentMaxDegree + 1])
	{
		const double S = FMath::Sqrt(FMath::Max(0.0, 1.0 - X * X));
		Out[0][0] = 1.0;
		for (int32 m = 1; m <= L; ++m) { Out[m][m] = Out[m - 1][m - 1] * double(2 * m - 1) * S; }
		for (int32 m = 0; m < L; ++m) { Out[m + 1][m] = X * double(2 * m + 1) * Out[m][m]; }
		for (int32 m = 0; m <= L; ++m)
		{
			for (int32 l = m + 2; l <= L; ++l)
			{
				Out[l][m] = (X * double(2 * l - 1) * Out[l - 1][m] - double(l + m - 1) * Out[l - 2][m]) / double(l - m);
			}
		}
	}

	double ContinentNorm(int32 L, int32 M)
	{
		double Ratio = 1.0;   // (l−m)!/(l+m)!
		for (int32 k = L - M + 1; k <= L + M; ++k) { Ratio /= double(k); }
		return FMath::Sqrt(double(2 * L + 1) / (4.0 * PI) * Ratio);
	}

	float ContinentSmooth(float T) { T = FMath::Clamp(T, 0.f, 1.f); return T * T * (3.f - 2.f * T); }
}

GeoContinentField::FFrame GeoContinentField::BuildFrame(const FPlanetRecipeConfig& Config)
{
	const FPlanetContinentParams& P = Config.Continents;
	FFrame Frame;
	Frame.Degree = FMath::Clamp(P.FrameDegree, 1, ContinentMaxDegree);
	Frame.Coeffs.SetNumZeroed((Frame.Degree + 1) * (Frame.Degree + 1));

	FGeoRng Rng = FGeoRng::Derive(Config.Seed, 2, GeoHash::Str("continent-frame"));
	// 只取 [⌈L/2⌉, L] 这段阶：从 l=1 叠起时 l=1 的半球偏置永远占最大份额，阶数调多高都是一块大陆。
	const int32 MinDegree = FMath::Max(1, (Frame.Degree + 1) / 2);
	for (int32 l = MinDegree; l <= Frame.Degree; ++l)
	{
		// 每阶总功率 ∝ l^-decay，摊到 2l+1 个系数上；否则高阶系数多、天然盖过低阶。
		const double Sigma = FMath::Sqrt(FMath::Pow(double(l), -double(P.FrameSpectralDecay)) / double(2 * l + 1));
		for (int32 m = -l; m <= l; ++m)
		{
			const double U1 = FMath::Max(double(Rng.NextFloat()), 1e-7), U2 = double(Rng.NextFloat());
			const double Gauss = FMath::Sqrt(-2.0 * FMath::Loge(U1)) * FMath::Cos(2.0 * PI * U2);
			Frame.Coeffs[l * l + l + m] = Gauss * Sigma;
		}
	}
	return Frame;
}

float GeoContinentField::EvalFrame(const FFrame& Frame, const FVector3f& Dir)
{
	const FVector3d D = FVector3d(Dir).GetSafeNormal();
	const double Phi = FMath::Atan2(D.Y, D.X);
	double Pl[ContinentMaxDegree + 1][ContinentMaxDegree + 1];
	ContinentLegendre(Frame.Degree, D.Z, Pl);

	double Sum = 0.0;
	for (int32 l = 1; l <= Frame.Degree; ++l)
	{
		Sum += Frame.Coeffs[l * l + l] * ContinentNorm(l, 0) * Pl[l][0];
		for (int32 m = 1; m <= l; ++m)
		{
			const double K = UE_DOUBLE_SQRT_2 * ContinentNorm(l, m) * Pl[l][m];
			Sum += Frame.Coeffs[l * l + l + m] * K * FMath::Cos(double(m) * Phi);
			Sum += Frame.Coeffs[l * l + l - m] * K * FMath::Sin(double(m) * Phi);
		}
	}
	return float(Sum);
}

uint64 GeoContinentField::DetailSeed(const FPlanetRecipeConfig& Config)
{
	return FGeoRng::Derive(Config.Seed, 2, GeoHash::Str("continent-detail")).NextU64();
}

float GeoContinentField::EvalDetail(uint64 Seed, const FPlanetContinentParams& P, const FVector3f& Dir)
{
	// 先扭方向再取 fBm：不扭的 fBm 海岸是各向同性的「斑点」，扭过才有拉长的半岛与海湾。
	const FVector3f Q = Dir * (0.5f * P.DetailFrequency);
	const FVector3f Warp(
		GeoNoise::Fbm3(GeoHash::Combine(Seed, 1), Q, 2),
		GeoNoise::Fbm3(GeoHash::Combine(Seed, 2), Q, 2),
		GeoNoise::Fbm3(GeoHash::Combine(Seed, 3), Q, 2));
	const FVector3f W = (Dir + P.DetailWarpAmp * Warp).GetSafeNormal();
	return GeoNoise::Fbm3(Seed, W * P.DetailFrequency, FMath::Max(1, P.DetailOctaves));
}

double GeoContinentField::TexelWeight(const FGeoField2D& Field, int32 X, int32 Y)
{
	const double U = (double(X) + 0.5) / double(Field.Res) * 2.0 - 1.0;
	const double V = (double(Y) + 0.5) / double(Field.Res) * 2.0 - 1.0;
	return FMath::Pow(1.0 + U * U + V * V, -1.5);
}

GeoContinentField::FWeightedCdf GeoContinentField::BuildCdf(const FGeoField2D& Field)
{
	FWeightedCdf Cdf;
	Cdf.Min = TNumericLimits<float>::Max();
	Cdf.Max = -TNumericLimits<float>::Max();
	for (int32 Face = 0; Face < 6; ++Face)
	for (int32 Y = 0; Y < Field.Res; ++Y)
	for (int32 X = 0; X < Field.Res; ++X)
	{
		const float V = Field.At(Face, X, Y);
		Cdf.Min = FMath::Min(Cdf.Min, V);
		Cdf.Max = FMath::Max(Cdf.Max, V);
	}

	Cdf.Cum.SetNumZeroed(ContinentCdfBins);
	const double Span = FMath::Max(double(Cdf.Max) - double(Cdf.Min), 1e-12);
	for (int32 Face = 0; Face < 6; ++Face)
	for (int32 Y = 0; Y < Field.Res; ++Y)
	for (int32 X = 0; X < Field.Res; ++X)
	{
		const double T = (double(Field.At(Face, X, Y)) - Cdf.Min) / Span;
		const int32 Bin = FMath::Clamp(int32(T * ContinentCdfBins), 0, ContinentCdfBins - 1);
		Cdf.Cum[Bin] += TexelWeight(Field, X, Y);
	}
	double Total = 0.0;
	for (double& C : Cdf.Cum) { Total += C; C = Total; }
	for (double& C : Cdf.Cum) { C /= FMath::Max(Total, 1e-30); }
	return Cdf;
}

float GeoContinentField::FWeightedCdf::Rank(float V) const
{
	if (Cum.IsEmpty() || Max <= Min) { return 0.5f; }
	const double T = FMath::Clamp((double(V) - Min) / (double(Max) - Min), 0.0, 1.0) * Cum.Num();
	const int32 Bin = FMath::Clamp(int32(T), 0, Cum.Num() - 1);
	const double Lo = Bin > 0 ? Cum[Bin - 1] : 0.0;
	return float(Lo + (Cum[Bin] - Lo) * FMath::Clamp(T - double(Bin), 0.0, 1.0));
}

float GeoContinentField::Elevation(float Rank, const FPlanetContinentParams& P, double Radius)
{
	const float LandH = float(P.LandHeightRatio * Radius), OceanD = float(P.OceanDepthRatio * Radius);
	const float Coast = 1.f - FMath::Clamp(P.LandFrac, 0.01f, 0.99f);
	if (Rank >= Coast)
	{
		const float T = (Rank - Coast) / (1.f - Coast);
		return LandH * FMath::Pow(FMath::Clamp(T, 0.f, 1.f), FMath::Max(P.LandCurvePower, 0.01f));
	}

	// S = 当前点以上（更浅）的海洋面积占比：陆架 → 陆坡 → 深海平原依次铺开。
	const float S = (Coast - Rank) / Coast;
	const float Shelf = FMath::Clamp(P.ShelfFrac, 0.f, 0.9f);
	const float Slope = FMath::Clamp(P.SlopeFrac, 0.01f, 1.f - Shelf);
	const float ShelfBottom = P.ShelfDepthRatio * OceanD;
	const float AbyssTop = FMath::Max(P.AbyssRatio * OceanD, ShelfBottom);
	if (S <= Shelf)
	{
		return -ShelfBottom * (Shelf > 0.f ? S / Shelf : 1.f);
	}
	if (S <= Shelf + Slope)
	{
		return -FMath::Lerp(ShelfBottom, AbyssTop, ContinentSmooth((S - Shelf) / Slope));
	}
	const float Rest = FMath::Max(1.f - Shelf - Slope, 1e-6f);
	return -FMath::Lerp(AbyssTop, OceanD, FMath::Clamp((S - Shelf - Slope) / Rest, 0.f, 1.f));
}

void GeoContinentField::SmoothKeepSign(FGeoField2D& Field, TConstArrayView<int32> Offsets, bool bSingleThread)
{
	const FGeoField2D Original = Field;
	const int32 Res = Field.Res;
	for (const int32 Offset : Offsets)
	{
		const FGeoField2D Src = Field;
		const float Du = 2.f * float(Offset) / float(Res);
		ParallelFor(6 * Res, [&](int32 Row)
		{
			const int32 Face = Row / Res, Y = Row % Res;
			for (int32 X = 0; X < Res; ++X)
			{
				const float U = (float(X) + 0.5f) / float(Res) * 2.f - 1.f, V = (float(Y) + 0.5f) / float(Res) * 2.f - 1.f;
				const float Sum = Src.Sample(FGeoField2D::FaceToDir(Face, U + Du, V)) + Src.Sample(FGeoField2D::FaceToDir(Face, U - Du, V))
					+ Src.Sample(FGeoField2D::FaceToDir(Face, U, V + Du)) + Src.Sample(FGeoField2D::FaceToDir(Face, U, V - Du));
				const float Blurred = 0.5f * Src.At(Face, X, Y) + 0.125f * Sum;
				// 符号钳住：模糊会把海岸两侧互相拉过零点，陆地占比与海岸线就漂了。
				Field.At(Face, X, Y) = Original.At(Face, X, Y) > 0.f ? FMath::Max(Blurred, 0.01f) : FMath::Min(Blurred, 0.f);
			}
		}, bSingleThread ? EParallelForFlags::ForceSingleThread : EParallelForFlags::None);
	}
}
