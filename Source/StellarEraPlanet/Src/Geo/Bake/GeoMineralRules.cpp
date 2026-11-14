#include "GeoMineralRules.h"
#include "GeoNoise.h"
#include "PlanetBakeTypes.h"
#include "PlanetMaterialEval.h"
#include "PlanetRecipeConfig.h"
#include "PlanetShapeEval.h"

namespace
{
	FVector3f GeoMineralRulesRandomDir(FGeoRng& Rng)
	{
		const float Z = Rng.Range(-1.f, 1.f), Phi = Rng.Range(0.f, 2.f * PI), S = FMath::Sqrt(FMath::Max(0.f, 1.f - Z * Z));
		return FVector3f(S * FMath::Cos(Phi), S * FMath::Sin(Phi), Z);
	}

	uint64 GeoMineralRulesHashFloats(uint64 Tag, std::initializer_list<float> Values)
	{
		uint64 H = Tag;
		for (const float V : Values) { H = GeoHash::Combine(H, GeoHash::Float(V)); }
		return H;
	}

	bool GeoMineralRulesNeedStage2(const FGeoScatterContext& Ctx, const TCHAR* Name)
	{
		const FPlanetBakeFields& F = Ctx.Fields;
		if (F.CrustType.Res > 0 && F.CrustAge.Res > 0 && F.ContinentalMask.Res > 0 && F.Elevation.Res > 0) { return true; }
		Ctx.Errors.Add(FString::Printf(TEXT("[地质] 撒矿器「%s」需要阶段 2 的宏观场，但它们为空"), Name));
		return false;
	}

	/**
	 * 在地表下 [DMin, DMax] 的球壳里按体积均匀取矿体中心；DMax ≤ 0 = 一直到地壳底（莫霍面最高处之上）。
	 * 按 r³ 均匀取：外层球壳体积大，分到的矿就多；按深度均匀取会让矿偏向深处。
	 */
	FVector3f GeoMineralRulesInVolume(FGeoScatterContext& Ctx, const FVector3f& Dir, float DMin, float DMax, float HalfThickness)
	{
		const FPlanetRecipeConfig& C = Ctx.Config;
		const double Surface = C.Radius + double(Ctx.Fields.Elevation.Sample(Dir));
		const double CrustFloor = C.Radius * (1.0 - double(C.Sphere.CrustRatio) + double(C.Sphere.MohoWobbleRatio)) + HalfThickness;
		const double Top = Surface - double(DMin) - HalfThickness;
		const double Bottom = DMax > 0.f ? FMath::Max(Surface - double(DMax) - HalfThickness, CrustFloor) : CrustFloor;
		const double U = double(Ctx.Rng.NextFloat());
		const double R = Bottom >= Top ? Top : FMath::Pow(U * (Top * Top * Top - Bottom * Bottom * Bottom) + Bottom * Bottom * Bottom, 1.0 / 3.0);
		return FVector3f(FVector3d(Dir) * R);
	}
}

FQuat4f GeoMineralRules::TangentFrame(const FVector3f& Up, float StrikeRadians)
{
	const FVector3f Ref = FMath::Abs(Up.Z) < 0.9f ? FVector3f::UpVector : FVector3f::ForwardVector;
	const FVector3f E = FVector3f::CrossProduct(Ref, Up).GetSafeNormal();
	const FVector3f N = FVector3f::CrossProduct(Up, E);
	const FVector3f X = E * FMath::Cos(StrikeRadians) + N * FMath::Sin(StrikeRadians);
	// Y = Up × X 保证 X × Y = Up，行列式为 +1，是纯旋转。
	const FVector3f Y = FVector3f::CrossProduct(Up, X);
	return FQuat4f(FMatrix44f(X, Y, Up, FVector3f::ZeroVector)).GetNormalized();
}

void FGeoBifRule::Scatter(FGeoScatterContext& Ctx, FGeoScatterOutput& Out) const
{
	if (!GeoMineralRulesNeedStage2(Ctx, GetRuleName())) { return; }
	const FPlanetBakeFields& F = Ctx.Fields;
	const int32 Count = FMath::Clamp(P.Count, 0, 4096);
	int32 Placed = 0;
	for (int32 Try = 0; Try < Count * 400 && Placed < Count; ++Try)
	{
		const FVector3f Dir = GeoMineralRulesRandomDir(Ctx.Rng);
		// 位置由场决定：只有古老陆壳内部才有，种子只决定落在这片区域的哪里。
		if (F.CrustType.SampleNearest(Dir) != 1.f || F.CrustAge.Sample(Dir) < P.AgeMin || F.ContinentalMask.Sample(Dir) < P.ContinentalMin)
		{
			continue;
		}
		const FVector3f Radii(0.5f * P.LengthVoxels, 0.5f * P.WidthVoxels, FMath::Max(0.5f * P.ThicknessVoxels, 0.5f));
		const FQuat4f Rot = GeoMineralRules::TangentFrame(Dir, Ctx.Rng.Range(0.f, 2.f * PI));
		const FVector3f Center = GeoMineralRulesInVolume(Ctx, Dir, P.DepthMinVoxels, P.DepthMaxVoxels, Radii.Z);
		FGeoPrimitive Body = FGeoPrimitive::MakeOrientedBlob(Center, Radii, Rot, 0.1f);
		Body.Grade = RandomGrade(Ctx.Rng);
		Out.Bodies.Add(Body);
		++Placed;
	}
}

uint64 FGeoBifRule::HashParams() const
{
	return GeoMineralRulesHashFloats(GeoHash::Combine(GeoHash::Str("bif"), uint64(P.Count)),
		{ P.AgeMin, P.ContinentalMin, P.LengthVoxels, P.WidthVoxels, P.ThicknessVoxels, P.DepthMinVoxels, P.DepthMaxVoxels });
}

void FGeoVmsRule::Scatter(FGeoScatterContext& Ctx, FGeoScatterOutput& Out) const
{
	if (!GeoMineralRulesNeedStage2(Ctx, GetRuleName())) { return; }
	const FPlanetBakeFields& F = Ctx.Fields;
	if (F.BoundaryType.Res <= 0) { Ctx.Errors.Add(TEXT("[地质] 撒矿器「块状硫化物」需要阶段 2 的 BoundaryType")); return; }
	const int32 Chains = FMath::Clamp(P.Chains, 0, 4096);
	const double R = Ctx.Config.Radius;
	int32 Placed = 0;
	// 离散边界只占星球千分之几的面积：拒绝采样要给足次数。
	for (int32 Try = 0; Try < Chains * 4000 && Placed < Chains; ++Try)
	{
		const FVector3f Dir = GeoMineralRulesRandomDir(Ctx.Rng);
		if (F.BoundaryType.SampleNearest(Dir) != 4.f || F.CrustType.SampleNearest(Dir) != 0.f) { continue; }
		const FQuat4f Frame = GeoMineralRules::TangentFrame(Dir, Ctx.Rng.Range(0.f, 2.f * PI));
		const FVector3f Along = Frame.RotateVector(FVector3f::XAxisVector);
		const int32 Lenses = FMath::RoundToInt(Ctx.Rng.Range(float(FMath::Min(P.LensMin, P.LensMax)), float(FMath::Max(P.LensMin, P.LensMax))));
		for (int32 k = 0; k < Lenses; ++k)
		{
			const FVector3f Q = (Dir + Along * float(double(k) * P.SpacingVoxels / R)).GetSafeNormal();
			// 链子走出洋中脊就截断：矿只能长在脊上。
			if (F.BoundaryType.SampleNearest(Q) != 4.f) { break; }
			const FVector3f Radii(P.LensRadiusVoxels, 0.7f * P.LensRadiusVoxels, FMath::Max(0.5f * P.LensThicknessVoxels, 0.5f));
			FGeoPrimitive Body = FGeoPrimitive::MakeOrientedBlob(GeoMineralRulesInVolume(Ctx, Q, P.DepthMinVoxels, P.DepthMaxVoxels, Radii.Z), Radii,
				GeoMineralRules::TangentFrame(Q, Ctx.Rng.Range(0.f, 2.f * PI)), 0.15f);
			Body.Grade = RandomGrade(Ctx.Rng);
			Out.Bodies.Add(Body);
		}
		++Placed;
	}
}

uint64 FGeoVmsRule::HashParams() const
{
	return GeoMineralRulesHashFloats(GeoHash::Combine(GeoHash::Str("vms"), uint64(P.Chains) | (uint64(P.LensMin) << 20) | (uint64(P.LensMax) << 40)),
		{ P.SpacingVoxels, P.LensRadiusVoxels, P.LensThicknessVoxels, P.DepthMinVoxels, P.DepthMaxVoxels });
}

void FGeoNoduleRule::Scatter(FGeoScatterContext& Ctx, FGeoScatterOutput& Out) const
{
	if (!GeoMineralRulesNeedStage2(Ctx, GetRuleName())) { return; }
	const FPlanetBakeFields& F = Ctx.Fields;
	FGeoMineralOverlay O;
	O.DepthVoxels = FMath::Max(P.DepthVoxels, 0.5f);
	O.Grade = RandomGrade(Ctx.Rng);
	const uint64 Seed = Ctx.Rng.NextU64();
	const float Threshold = 1.f - FMath::Clamp(P.Coverage, 0.f, 1.f);
	O.Mask.Init(F.CrustType.Res);
	O.Mask.Fill([&F, this, Seed, Threshold](const FVector3f& Dir)
	{
		if (F.CrustType.SampleNearest(Dir) != 0.f || F.CrustAge.Sample(Dir) < P.AgeMin || F.ContinentalMask.Sample(Dir) > P.ContinentalMax)
		{
			return 0.f;
		}
		// 斑块噪声决定「这片老洋底里哪些地方有结核」；阈值随覆盖率走，覆盖率只是近似值。
		const float N = 0.5f * (GeoNoise::Fbm3(Seed, Dir * P.PatchFrequency, 3) + 1.f);
		return FMath::SmoothStep(Threshold - 0.05f, Threshold + 0.05f, N);
	}, Ctx.Config.bSingleThreadBake);
	Out.Overlays.Add(MoveTemp(O));
}

uint64 FGeoNoduleRule::HashParams() const
{
	return GeoMineralRulesHashFloats(GeoHash::Str("nodule"), { P.AgeMin, P.ContinentalMax, P.Coverage, P.PatchFrequency, P.DepthVoxels });
}

namespace
{
	TArray<FGeoDebugLegendEntry> GeoMineralRulesLegend(const FPlanetSnapshot& S)
	{
		TArray<FGeoDebugLegendEntry> L;
		L.Add({ TEXT("无矿"), FColor(70, 70, 76) });
		for (int32 i = 0; i < S.Minerals.Num(); ++i) { L.Add({ S.Minerals[i].Name, S.Minerals[i].Color }); }
		return L;
	}

	FColor GeoMineralRulesColor(const FPlanetSnapshot& S, float V)
	{
		const int32 Id = FMath::RoundToInt(V);
		return Id <= 0 ? FColor(70, 70, 76) : S.MineralColor(uint8(FMath::Min(Id, 255)));
	}
}

void GeoMineralRules::DescribeDebugLayers(TArray<FGeoDebugLayer>& Out)
{
	{
		FGeoDebugLayer L;
		L.Id = TEXT("Mineral");
		L.DisplayName = TEXT("矿体（地下）");
		L.Stage = 0;
		L.Domain = EGeoDebugDomain::Volume;
		L.Kind = EGeoDebugKind::Categorical;
		L.DynamicLegend = &GeoMineralRulesLegend;
		L.Colorize = &GeoMineralRulesColor;
		L.DefaultHidden = { 0 };
		L.Sample = [](const FPlanetSnapshot& S, const FVector3d& P)
		{
			return PlanetShapeEval::Value(S, P) > 0.f ? FGeoDebugLayer::NoData() : float(PlanetMaterialEval::Sample(S, P).Mineral);
		};
		// 矿体只有几十到几百体素，整颗星的步进步长约 200 体素，必漏：逐个解析求光线与椭球的交点。
		L.RayHits = [](const FPlanetSnapshot& S, const FVector3d& O, const FVector3d& D, TArray<TPair<double, float>>& Hits)
		{
			for (const FGeoPrimitive& Pr : S.Primitives.GetPrimitives())
			{
				if (Pr.MineralId == 0) { continue; }
				const FQuat4d Rot(Pr.Kind == EGeoPrimitiveKind::OrientedBlob ? Pr.Rotation : FQuat4f::Identity);
				const FVector3d Radii(Pr.Radii);
				const FVector3d Lo = Rot.UnrotateVector(O - FVector3d(Pr.Center)) / Radii;
				const FVector3d Ld = Rot.UnrotateVector(D) / Radii;
				const double A = Ld.SizeSquared(), B = 2.0 * FVector3d::DotProduct(Lo, Ld), C = Lo.SizeSquared() - 1.0;
				const double Disc = B * B - 4.0 * A * C;
				if (Disc < 0.0 || A <= 0.0) { continue; }
				const double T = (-B - FMath::Sqrt(Disc)) / (2.0 * A);
				if (T > 0.0) { Hits.Emplace(T, float(Pr.MineralId)); }
			}
		};
		Out.Add(MoveTemp(L));
	}
	{
		FGeoDebugLayer L;
		L.Id = TEXT("SurfaceMineral");
		L.DisplayName = TEXT("地表矿化");
		L.Stage = 0;
		L.Domain = EGeoDebugDomain::Surface;
		L.Kind = EGeoDebugKind::Categorical;
		L.DynamicLegend = &GeoMineralRulesLegend;
		L.Colorize = &GeoMineralRulesColor;
		// 地表下 1.5 体素：覆层与刚出露的矿体都算，玩家铲一下就能挖到的那一层。
		L.Sample = [](const FPlanetSnapshot& S, const FVector3d& P)
		{
			const FVector3f Dir(P.GetSafeNormal());
			return float(PlanetMaterialEval::Sample(S, FVector3d(Dir) * (PlanetShapeEval::SurfaceRadius(S, Dir) - 1.5)).Mineral);
		};
		Out.Add(MoveTemp(L));
	}
}
