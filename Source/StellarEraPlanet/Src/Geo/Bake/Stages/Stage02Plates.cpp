#include "Stage02Plates.h"
#include "Async/ParallelFor.h"
#include "GeoContinentField.h"
#include "GeoPlates.h"
#include "GeoNoise.h"
#include "PlanetDebugColor.h"
#include "PlanetSnapshot.h"

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
			Fields.Elevation.At(Face, X, Y) = GeoContinentField::Elevation(Cdf.Rank(Continent.At(Face, X, Y)), K, Ctx.Config.Radius);
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
	Fields.CrustSplitElevation = -K.ShelfDepthRatio * K.OceanDepthRatio * float(Ctx.Config.Radius);
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
		const FVector3f W = GeoPlates::WarpDirection(Frame, Dir, P);
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

namespace
{
	/** 调试场只在 bKeepDebugFields 的快照里有；缺了返回无数据，不报错。 */
	float Stage02DebugField(const FPlanetSnapshot& S, const TCHAR* Name, const FVector3d& P, bool bNearest)
	{
		const FGeoField2D* F = S.DebugFields.Find(FName(Name));
		if (!F || F->Res <= 0) { return FGeoDebugLayer::NoData(); }
		const FVector3f Dir(P.GetSafeNormal());
		return bNearest ? F->SampleNearest(Dir) : F->Sample(Dir);
	}

	FGeoDebugLayer Stage02FieldLayer(const TCHAR* Id, const TCHAR* Name, EGeoDebugKind Kind, bool bNearest)
	{
		FGeoDebugLayer L;
		L.Id = Id;
		L.DisplayName = Name;
		L.Stage = 2;
		L.Kind = Kind;
		L.Sample = [Id, bNearest](const FPlanetSnapshot& S, const FVector3d& P) { return Stage02DebugField(S, Id, P, bNearest); };
		return L;
	}
}

void FStage02Plates::DescribeDebugLayers(TArray<FGeoDebugLayer>& Out) const
{
	{
		FGeoDebugLayer L;
		L.Id = TEXT("Elevation");
		L.DisplayName = TEXT("高程 / 海陆");
		L.Stage = 2;
		L.Unit = TEXT("体素");
		L.Sample = [](const FPlanetSnapshot& S, const FVector3d& P)
		{
			return S.Elevation.Res > 0 ? S.Elevation.Sample(FVector3f(P.GetSafeNormal())) : FGeoDebugLayer::NoData();
		};
		L.Range = [](const FPlanetSnapshot& S)
		{
			return FVector2f(float(S.MinSurfaceRadius - S.R0), float(S.MaxSurfaceRadius - S.R0));
		};
		L.Colorize = [](const FPlanetSnapshot& S, float V)
		{
			return PlanetDebugColor::FromElevation(V, float(S.R0 - S.MinSurfaceRadius), float(S.MaxSurfaceRadius - S.R0),
				S.CrustSplitElevation);
		};
		Out.Add(MoveTemp(L));
	}
	{
		FGeoDebugLayer L = Stage02FieldLayer(TEXT("PlateId"), TEXT("板块编号"), EGeoDebugKind::Categorical, true);
		for (int32 i = 0; i < 15; ++i)
		{
			// 色相按约 137° 跳：相邻编号颜色差最大，不会两块同色。
			const uint8 Hue = uint8(FMath::Fmod(float(i) * 97.f, 256.f));
			L.Legend.Add({ FString::Printf(TEXT("板块 %d"), i), FLinearColor::MakeFromHSV8(Hue, 170, 230).ToFColor(true) });
		}
		Out.Add(MoveTemp(L));
	}
	{
		FGeoDebugLayer L = Stage02FieldLayer(TEXT("BoundaryType"), TEXT("板块边界类型"), EGeoDebugKind::Categorical, true);
		L.Legend = {
			{ TEXT("非边界"), FColor(55, 55, 60) },
			{ TEXT("汇聚：洋-陆（俯冲）"), FColor(225, 70, 45) },
			{ TEXT("汇聚：洋-洋（岛弧）"), FColor(245, 160, 40) },
			{ TEXT("汇聚：陆-陆（碰撞）"), FColor(170, 50, 170) },
			{ TEXT("离散（洋中脊 / 裂谷）"), FColor(60, 145, 245) },
			{ TEXT("转换断层"), FColor(80, 205, 95) } };
		Out.Add(MoveTemp(L));
	}
	{
		FGeoDebugLayer L = Stage02FieldLayer(TEXT("CrustType"), TEXT("地壳类型"), EGeoDebugKind::Categorical, true);
		L.Legend = { { TEXT("洋壳"), FColor(40, 70, 130) }, { TEXT("陆壳"), FColor(95, 150, 70) }, { TEXT("陆架"), FColor(115, 195, 225) } };
		Out.Add(MoveTemp(L));
	}
	{
		FGeoDebugLayer L = Stage02FieldLayer(TEXT("ContinentalMask"), TEXT("大陆度"), EGeoDebugKind::Continuous, false);
		L.Unit = TEXT("0 海 … 1 陆");
		Out.Add(MoveTemp(L));
	}
	{
		FGeoDebugLayer L = Stage02FieldLayer(TEXT("CrustAge"), TEXT("地壳年龄（相对）"), EGeoDebugKind::Continuous, false);
		L.Unit = TEXT("0 新 … 1 老");
		Out.Add(MoveTemp(L));
	}
	{
		FGeoDebugLayer L = Stage02FieldLayer(TEXT("CrustThickness"), TEXT("地壳厚度（相对）"), EGeoDebugKind::Continuous, false);
		L.Unit = TEXT("0 薄 … 1 厚");
		Out.Add(MoveTemp(L));
	}
}

void FStage02Plates::ExportDebugFields(const FPlanetBakeFields& Fields, TMap<FName, FGeoField2D>& Out) const
{
	Out.Add(TEXT("PlateId"), Fields.PlateId);
	Out.Add(TEXT("BoundaryType"), Fields.BoundaryType);
	Out.Add(TEXT("CrustType"), Fields.CrustType);
	Out.Add(TEXT("ContinentalMask"), Fields.ContinentalMask);
	Out.Add(TEXT("CrustAge"), Fields.CrustAge);
	Out.Add(TEXT("CrustThickness"), Fields.CrustThickness);
}
