#include "Stage02Plates.h"
#include "Async/ParallelFor.h"
#include "GeoContinentField.h"
#include "GeoPlates.h"
#include "GeoNoise.h"

namespace
{
	/** 立体角加权的均值与标准差：框架与细节两层要先各自归一，CoastRoughness 才是无量纲比值。 */
	void Stage02WeightedStats(const FGeoField2D& F, double& OutMean, double& OutStd)
	{
		double SW = 0.0, S1 = 0.0, S2 = 0.0;
		for (int32 Face = 0; Face < 6; ++Face)
		for (int32 Y = 0; Y < F.Res; ++Y)
		for (int32 X = 0; X < F.Res; ++X)
		{
			const double W = GeoContinentField::TexelWeight(F, X, Y), V = F.At(Face, X, Y);
			SW += W; S1 += W * V; S2 += W * V * V;
		}
		OutMean = S1 / FMath::Max(SW, 1e-30);
		OutStd = FMath::Sqrt(FMath::Max(S2 / FMath::Max(SW, 1e-30) - OutMean * OutMean, 1e-24));
	}
}

void FStage02Plates::Bake(FPlanetBakeContext& Ctx) const
{
	const FPlanetPlateParams& P = Ctx.Config.Plates;
	const FPlanetContinentParams& K = Ctx.Config.Continents;
	const bool bSingleThread = Ctx.Config.bSingleThreadBake;
	FPlanetBakeFields& Fields = Ctx.Fields;

	Fields.PlateFrame = GeoPlates::Build(Ctx.Config);
	const FGeoPlateFrame& Frame = Fields.PlateFrame;

	const int32 FaceRes = Ctx.Config.FaceRes;
	const int32 MacroRes = FMath::Max(32, FaceRes / 4);

	// 大陆场 F = 框架/σf + 粗糙度·细节/σd：全球一张连续场，海陆个数由场自己长出来。
	const GeoContinentField::FFrame Harmonics = GeoContinentField::BuildFrame(Ctx.Config);
	const uint64 DetailSeed = GeoContinentField::DetailSeed(Ctx.Config);
	FGeoField2D FrameField, DetailField;
	FrameField.Init(FaceRes);
	FrameField.Fill([&Harmonics](const FVector3f& Dir) { return GeoContinentField::EvalFrame(Harmonics, Dir); }, bSingleThread);
	DetailField.Init(FaceRes);
	DetailField.Fill([DetailSeed, &K](const FVector3f& Dir) { return GeoContinentField::EvalDetail(DetailSeed, K, Dir); }, bSingleThread);

	double FrameMean, FrameStd, DetailMean, DetailStd;
	Stage02WeightedStats(FrameField, FrameMean, FrameStd);
	Stage02WeightedStats(DetailField, DetailMean, DetailStd);
	const float InvFrame = float(1.0 / FrameStd), DetailScale = float(double(K.CoastRoughness) / DetailStd);
	const float DetailOffset = float(DetailMean);

	FGeoField2D Continent;
	Continent.Init(FaceRes);
	for (int32 Face = 0; Face < 6; ++Face)
	for (int32 Y = 0; Y < FaceRes; ++Y)
	for (int32 X = 0; X < FaceRes; ++X)
	{
		Continent.At(Face, X, Y) = FrameField.At(Face, X, Y) * InvFrame
			+ (DetailField.At(Face, X, Y) - DetailOffset) * DetailScale;
	}
	FrameField = FGeoField2D();
	DetailField = FGeoField2D();

	const GeoContinentField::FWeightedCdf Cdf = GeoContinentField::BuildCdf(Continent);
	Fields.Elevation.Init(FaceRes);
	ParallelFor(6 * FaceRes, [&](int32 Row)
	{
		const int32 Face = Row / FaceRes, Y = Row % FaceRes;
		for (int32 X = 0; X < FaceRes; ++X)
		{
			Fields.Elevation.At(Face, X, Y) = GeoContinentField::Elevation(Cdf.Rank(Continent.At(Face, X, Y)), K);
		}
	}, bSingleThread ? EParallelForFlags::ForceSingleThread : EParallelForFlags::None);
	// 排名→高程在场值变化快处会把整段陆坡压进一个 texel（实测坡度 1.7）；陆坡落差 360 体素
	// 要摊到 ~700 体素宽才缓，à-trous 偏移必须到 16 texel（≈830 体素）。
	static const int32 SmoothOffsets[] = { 1, 2, 4, 8, 16, 8, 4, 2, 1 };
	GeoContinentField::SmoothKeepSign(Fields.Elevation, SmoothOffsets, bSingleThread);

	double LandW = 0.0, TotalW = 0.0;
	float MinE = TNumericLimits<float>::Max(), MaxE = -TNumericLimits<float>::Max();
	for (int32 Face = 0; Face < 6; ++Face)
	for (int32 Y = 0; Y < FaceRes; ++Y)
	for (int32 X = 0; X < FaceRes; ++X)
	{
		const float E = Fields.Elevation.At(Face, X, Y);
		const double W = GeoContinentField::TexelWeight(Fields.Elevation, X, Y);
		TotalW += W;
		LandW += E > 0.f ? W : 0.0;
		MinE = FMath::Min(MinE, E);
		MaxE = FMath::Max(MaxE, E);
	}
	Fields.LandAreaFrac = float(LandW / FMath::Max(TotalW, 1e-30));
	Fields.MinElevation = MinE;
	Fields.MaxElevation = MaxE;
	Fields.CrustSplitElevation = -K.ShelfDepthRatio * K.OceanDepthVoxels;
	if (FMath::Abs(Fields.LandAreaFrac - K.LandFrac) > 0.02f)
	{
		Ctx.Report.Errors.Add(FString::Printf(TEXT("[地质] 阶段 2：陆地占比 %.3f 偏离目标 %.3f 超过 0.02"),
			Fields.LandAreaFrac, K.LandFrac));
	}

	// 大陆度 M：排名在海岸 ±0.05 内平滑过渡，供阶段 3+ 当软遮罩用。
	const float Coast = 1.f - K.LandFrac;
	FGeoField2D MacroM;
	MacroM.Init(MacroRes);
	MacroM.Fill([&Continent, &Cdf, Coast](const FVector3f& Dir)
	{
		return FMath::SmoothStep(Coast - 0.05f, Coast + 0.05f, Cdf.Rank(Continent.Sample(Dir)));
	}, bSingleThread);
	FGeoField2D MacroGrad;
	MacroGrad.Init(MacroRes);
	MacroGrad.Fill([&MacroM, MacroRes](const FVector3f& Dir)
	{
		int32 Face; float U, V;
		FGeoField2D::DirToFace(Dir, Face, U, V);
		const float Du = 2.f / float(MacroRes);
		const FVector3f Up = FGeoField2D::FaceToDir(Face, U + Du, V), Um = FGeoField2D::FaceToDir(Face, U - Du, V);
		const FVector3f Vp = FGeoField2D::FaceToDir(Face, U, V + Du), Vm = FGeoField2D::FaceToDir(Face, U, V - Du);
		const float GX = FMath::Abs(MacroM.Sample(Up) - MacroM.Sample(Um)) / FMath::Max((Up - Um).Size(), 1e-6f);
		const float GY = FMath::Abs(MacroM.Sample(Vp) - MacroM.Sample(Vm)) / FMath::Max((Vp - Vm).Size(), 1e-6f);
		return FMath::Max(GX, GY);
	}, bSingleThread);

	// 8 张宏观场一次循环写完：每 texel 的 warp 与 Fbm3 是阶段里最贵的部分，拆四次 Fill 就重算四遍。
	FGeoField2D* Macro[8] = { &Fields.PlateId, &Fields.BoundaryType, &Fields.BoundaryDistance, &Fields.CrustAge,
		&Fields.ContinentalMask, &Fields.ContinentalGrad, &Fields.CrustType, &Fields.CrustThickness };
	for (FGeoField2D* Field : Macro) { Field->Init(MacroRes); }

	const float Band = P.BoundaryBandRatio;
	const float AgeScale = FMath::Max(P.AgeScaleRatio, 1e-6f);
	for (int32 Face = 0; Face < 6; ++Face)
	for (int32 Y = 0; Y < MacroRes; ++Y)
	for (int32 X = 0; X < MacroRes; ++X)
	{
		const FVector3f Dir = Fields.PlateId.TexelDir(Face, X, Y);
		const FVector3f W = GeoPlates::WarpDirection(Frame, Dir, P.WarpAmp);
		int32 Near, Second; float D1, D2;
		GeoPlates::NearestTwo(Frame, W, Near, Second, D1, D2);
		const float B = 0.5f * (D2 - D1);
		const float M = MacroM.At(Face, X, Y);
		const float E = Fields.Elevation.Sample(Dir);
		const float Age = FMath::Clamp(B / AgeScale, 0.f, 1.f);

		Fields.PlateId.At(Face, X, Y) = float(Near);
		Fields.BoundaryDistance.At(Face, X, Y) = B;
		Fields.BoundaryType.At(Face, X, Y) = B > Band ? 0.f : float(Frame.BoundaryTypeBetween(Near, Second));
		// 陆区地壳年龄取高：真实大陆核部更老，这一版只做近似。
		Fields.CrustAge.At(Face, X, Y) = M > 0.5f ? FMath::Max(Age, 0.75f + 0.25f * M) : Age;
		Fields.ContinentalMask.At(Face, X, Y) = M;
		Fields.ContinentalGrad.At(Face, X, Y) = MacroGrad.At(Face, X, Y);
		// CrustType：0 洋壳 / 1 陆壳 / 2 陆架，分界与快照的 CrustSplitElevation 同源。
		Fields.CrustType.At(Face, X, Y) = E > 0.f ? 1.f : (E >= Fields.CrustSplitElevation ? 2.f : 0.f);
		Fields.CrustThickness.At(Face, X, Y) = FMath::Clamp(0.1f + 0.9f * M, 0.f, 1.f);
	}
}
