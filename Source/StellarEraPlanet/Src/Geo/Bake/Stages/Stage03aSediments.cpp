#include "Stage03aSediments.h"
#include "GeoLithology.h"
#include "GeoNoise.h"
#include "PlanetDebugLayer.h"
#include "PlanetSnapshot.h"

void FStage03aSediments::Bake(FPlanetBakeContext& Ctx) const
{
	FPlanetBakeFields& F = Ctx.Fields;
	const FPlanetStrataParams& P = Ctx.Config.Strata;
	if (!P.bStrata) { return; }
	if (F.ContinentalMask.Res <= 0 || F.Elevation.Res <= 0)
	{
		Ctx.Report.Errors.Add(TEXT("[地质] 阶段 3A：缺阶段 2 的大陆场或阶段 3 的高程场"));
		return;
	}
	const int32 Res = FMath::Clamp(Ctx.Config.FaceRes / 4, 32, 256);
	F.StrataThickness.Init(Res);
	F.StrataType.Init(Res);

	const float R = float(Ctx.Config.Radius);
	const int32 Octaves = FMath::Clamp(P.PatchOctaves, 1, 6);
	const float Freq = FMath::Max(P.PatchFrequency, 0.5f);
	const uint64 PatchSeed = Ctx.Rng.NextU64();
	const uint64 TypeSeed = Ctx.Rng.NextU64();
	const float LandRange = FMath::Max(F.MaxElevation, 1.f);

	F.StrataThickness.Fill([&](const FVector3f& Dir) -> float
	{
		// 只在陆地上盖：洋壳上来的沉积是洋底软泥，不参与这里的岩性。
		if (F.ContinentalMask.Sample(Dir) < 0.5f) { return 0.f; }
		const float E = F.Elevation.Sample(Dir);
		if (E < F.CrustSplitElevation) { return 0.f; }
		// 低地才是沉积盆地：高出海平面超过陆高跨度的一部分就不盖。
		const float Lowland = FMath::Clamp(1.f - E / (LandRange * FMath::Max(P.LowlandCutoff, 0.05f)), 0.f, 1.f);
		if (Lowland <= 0.f) { return 0.f; }
		const float Patch = 0.5f + 0.5f * GeoNoise::Fbm3(PatchSeed, Dir * Freq, Octaves);
		return P.MaxThicknessRatio * R * Lowland * FMath::Clamp(Patch, 0.f, 1.f);
	}, Ctx.Config.bSingleThreadBake);

	F.StrataType.Fill([&](const FVector3f& Dir) -> float
	{
		if (F.StrataThickness.Sample(Dir) < 1.f) { return float(GeoLithology::None); }
		const float Band = 0.5f + 0.5f * GeoNoise::Fbm3(TypeSeed, Dir * Freq * 1.7f, Octaves);
		if (Band < P.SandstoneShare) { return float(GeoLithology::Sandstone); }
		if (Band < P.ShaleShare) { return float(GeoLithology::Shale); }
		return float(GeoLithology::Limestone);
	}, Ctx.Config.bSingleThreadBake);
}

void FStage03aSediments::DescribeDebugLayers(TArray<FGeoDebugLayer>& Out) const
{
	{
		FGeoDebugLayer L;
		L.Id = TEXT("StrataThickness");
		L.DisplayName = TEXT("沉积盖层厚度");
		L.Stage = 9;
		L.Unit = TEXT("体素");
		L.Min = 0.f;
		L.Max = 400.f;
		L.Sample = [](const FPlanetSnapshot& S, const FVector3d& P)
		{
			return S.StrataThickness.Res > 0
				? S.StrataThickness.Sample(FVector3f(P.GetSafeNormal())) : FGeoDebugLayer::NoData();
		};
		Out.Add(MoveTemp(L));
	}
	{
		FGeoDebugLayer L;
		L.Id = TEXT("StrataType");
		L.DisplayName = TEXT("沉积盖层岩性");
		L.Stage = 9;
		L.Kind = EGeoDebugKind::Categorical;
		for (uint8 Id = 0; Id < 3; ++Id)
		{
			const uint8 Litho = uint8(GeoLithology::Sandstone + Id);
			L.Legend.Add({ FString(GeoLithology::DisplayName(Litho)), GeoLithology::DebugColor(Litho) });
		}
		L.Sample = [](const FPlanetSnapshot& S, const FVector3d& P)
		{
			return S.StrataType.Res > 0
				? S.StrataType.SampleNearest(FVector3f(P.GetSafeNormal())) - float(GeoLithology::Sandstone)
				: FGeoDebugLayer::NoData();
		};
		Out.Add(MoveTemp(L));
	}
}

void FStage03aSediments::ExportDebugFields(const FPlanetBakeFields& Fields, TMap<FName, FGeoField2D>& Out) const
{
	if (Fields.StrataThickness.Res > 0) { Out.Add(TEXT("StrataThickness"), Fields.StrataThickness); }
	if (Fields.StrataType.Res > 0) { Out.Add(TEXT("StrataType"), Fields.StrataType); }
}
