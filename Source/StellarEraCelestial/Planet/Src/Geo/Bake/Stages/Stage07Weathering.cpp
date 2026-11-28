#include "Stage07Weathering.h"
#include "GeoSoil.h"
#include "PlanetDebugLayer.h"
#include "PlanetSnapshot.h"

void FStage07Weathering::Bake(FPlanetBakeContext& Ctx) const
{
	FPlanetBakeFields& F = Ctx.Fields;
	const FPlanetWeatheringParams& W = Ctx.Config.Weathering;
	if (!W.bWeathering) { return; }
	if (F.Temperature.Res <= 0 || F.Elevation.Res <= 0)
	{
		Ctx.Report.Errors.Add(TEXT("[地质] 阶段 7：缺阶段 5 的气候场或阶段 3 的高程场"));
		return;
	}
	const int32 Res = FMath::Clamp(Ctx.Config.FaceRes / 4, 32, 256);
	F.WeatheringDepth.Init(Res);
	F.SoilType.Init(Res);

	const float R = float(Ctx.Config.Radius);
	const FGeoField2D& Temp = F.Temperature;
	const FGeoField2D& Precip = F.Precipitation;
	const FGeoField2D& Elev = F.Elevation;

	// 坡度用切平面差分：除以弧长才是 rise/run，半径变大时同一座山不会越算越陡。
	F.WeatheringDepth.Fill([&](const FVector3f& Dir) -> float
	{
		if (Elev.Sample(Dir) < F.CrustSplitElevation) { return 0.f; }
		FVector3f T1 = FVector3f::CrossProduct(Dir, FVector3f::UpVector);
		if (T1.SizeSquared() < 1e-6f) { T1 = FVector3f::CrossProduct(Dir, FVector3f::ForwardVector); }
		T1.Normalize();
		const FVector3f T2 = FVector3f::CrossProduct(Dir, T1).GetSafeNormal();
		const float Step = 2.f / float(Res);
		const float Ex = (Elev.Sample((Dir + T1 * Step).GetSafeNormal()) - Elev.Sample((Dir - T1 * Step).GetSafeNormal())) / (2.f * Step * R);
		const float Ey = (Elev.Sample((Dir + T2 * Step).GetSafeNormal()) - Elev.Sample((Dir - T2 * Step).GetSafeNormal())) / (2.f * Step * R);
		// ×60：R=40000 时 30° 坡约 0.0008，先拉到可读量级再夹。
		const float Slope = FMath::Clamp(FMath::Sqrt(Ex * Ex + Ey * Ey) * 60.f, 0.f, 1.f);
		const float Climate = FMath::Clamp(Temp.Sample(Dir), 0.f, 1.f) * W.TemperatureWeight
			+ FMath::Clamp(Precip.Sample(Dir), 0.f, 1.f) * W.PrecipitationWeight;
		return W.MaxDepthRatio * R * FMath::Clamp(Climate * (1.f - W.SlopePenalty * Slope), 0.f, 1.f);
	}, Ctx.Config.bSingleThreadBake);

	F.SoilType.Fill([&](const FVector3f& Dir) -> float
	{
		if (Elev.Sample(Dir) < F.CrustSplitElevation) { return float(GeoSoil::None); }
		const float T = FMath::Clamp(Temp.Sample(Dir), 0.f, 1.f);
		const float P = FMath::Clamp(Precip.Sample(Dir), 0.f, 1.f);
		if (F.WeatheringDepth.Sample(Dir) < 1.f)
		{
			return float(P < 0.2f ? GeoSoil::Arid : GeoSoil::None);
		}
		if (T > W.LateriteTemp && P > W.LateritePrecip) { return float(GeoSoil::Laterite); }
		if (T > W.KaolinTemp && P > 0.4f) { return float(GeoSoil::Kaolin); }
		if (P < 0.2f) { return float(GeoSoil::Arid); }
		return float(GeoSoil::Temperate);
	}, Ctx.Config.bSingleThreadBake);
}

void FStage07Weathering::DescribeDebugLayers(TArray<FGeoDebugLayer>& Out) const
{
	{
		FGeoDebugLayer L;
		L.Id = TEXT("WeatheringDepth");
		L.DisplayName = TEXT("风化壳厚度");
		L.Stage = 8;
		L.Unit = TEXT("体素");
		L.Min = 0.f;
		L.Max = 80.f;
		L.Sample = [](const FPlanetSnapshot& S, const FVector3d& P)
		{
			return S.WeatheringDepth.Res > 0
				? S.WeatheringDepth.Sample(FVector3f(P.GetSafeNormal())) : FGeoDebugLayer::NoData();
		};
		Out.Add(MoveTemp(L));
	}
	{
		FGeoDebugLayer L;
		L.Id = TEXT("SoilType");
		L.DisplayName = TEXT("土壤类型");
		L.Stage = 8;
		L.Kind = EGeoDebugKind::Categorical;
		for (uint8 Id = 0; Id < GeoSoil::Count; ++Id)
		{
			L.Legend.Add({ FString(GeoSoil::DisplayName(Id)), GeoSoil::DebugColor(Id) });
		}
		L.Sample = [](const FPlanetSnapshot& S, const FVector3d& P)
		{
			return S.SoilType.Res > 0
				? S.SoilType.SampleNearest(FVector3f(P.GetSafeNormal())) : FGeoDebugLayer::NoData();
		};
		Out.Add(MoveTemp(L));
	}
}

void FStage07Weathering::ExportDebugFields(const FPlanetBakeFields& Fields, TMap<FName, FGeoField2D>& Out) const
{
	if (Fields.WeatheringDepth.Res > 0) { Out.Add(TEXT("WeatheringDepth"), Fields.WeatheringDepth); }
	if (Fields.SoilType.Res > 0) { Out.Add(TEXT("SoilType"), Fields.SoilType); }
}
