#include "Stage05Climate.h"
#include "Async/ParallelFor.h"
#include "PlanetSnapshot.h"

namespace
{
	/** 风吹向的方位角（度，0 = 北，90 = 东）：按 |lat| 三分带。 */
	float Stage05WindAzimuth(float AbsLatDeg)
	{
		// 信风与极地东风都向西，共用一个方位角：只存角度就必须靠纬度才能区分这两带。
		return AbsLatDeg < 30.f ? 270.f : (AbsLatDeg < 60.f ? 90.f : 270.f);
	}

	/** 调试图层读烘焙期的中间场；缺了返回「无数据」，不崩。 */
	float Stage05DebugField(const FPlanetSnapshot& S, const TCHAR* Name, const FVector3d& P, bool bNearest)
	{
		const FGeoField2D* Fd = S.DebugFields.Find(FName(Name));
		if (!Fd || Fd->Res <= 0) { return FGeoDebugLayer::NoData(); }
		const FVector3f Dir(P.GetSafeNormal());
		if (Dir.IsNearlyZero()) { return FGeoDebugLayer::NoData(); }
		return bNearest ? Fd->SampleNearest(Dir) : Fd->Sample(Dir);
	}
}

void FStage05Climate::Bake(FPlanetBakeContext& Ctx) const
{
	FPlanetBakeFields& F = Ctx.Fields;
	if (F.Elevation.Res <= 0 || F.ContinentalMask.Res <= 0)
	{
		Ctx.Report.Errors.Add(TEXT("[地质] 阶段 5：缺阶段 2/3 的高程或大陆度场"));
		return;
	}
	const FPlanetRecipeConfig& Cfg = Ctx.Config;
	const FPlanetClimateParams& P = Cfg.Climate;
	const EParallelForFlags PF = Cfg.bSingleThreadBake ? EParallelForFlags::ForceSingleThread : EParallelForFlags::None;
	const int32 Res = FMath::Max(P.ClimateRes, 1);
	F.Temperature.Init(Res);
	F.Precipitation.Init(Res);
	F.WindDir.Init(Res);
	F.Biome.Init(Res);
	F.ClimateLand.Init(Res);

	const float R = float(Cfg.Radius);
	// 高程以 0.08R 为满分：超过它温度不再继续掉，避免最高峰把整块大陆压到冰点。
	const float LapseScale = 1.f / FMath::Max(0.08f * R, 1e-6f);
	ParallelFor(6 * Res, [&](int32 Row)
	{
		const int32 Face = Row / Res, Y = Row % Res;
		for (int32 X = 0; X < Res; ++X)
		{
			const FVector3f Dir = F.WindDir.TexelDir(Face, X, Y);
			const float Z = FMath::Clamp(Dir.Z, -1.f, 1.f);
			const float E = F.Elevation.Sample(Dir);
			const float M = F.ContinentalMask.Sample(Dir);
			// Insolation = 1 − sin²(lat)，而 lat = asin(Z)，所以直接用 1 − Z²。
			const float T = P.PoleTemp + (P.EquatorTemp - P.PoleTemp) * (1.f - Z * Z)
				- P.LapseRate * FMath::Clamp(E * LapseScale, 0.f, 1.f)
				+ P.Continentality * (M - 0.5f);
			F.Temperature.At(Face, X, Y) = FMath::Clamp(T, 0.f, 1.f);
			F.WindDir.At(Face, X, Y) = Stage05WindAzimuth(FMath::RadiansToDegrees(FMath::Asin(Z)));
			F.ClimateLand.At(Face, X, Y) = E > 0.f ? 1.f : 0.f;
		}
	}, PF);
}

void FStage05Climate::DescribeDebugLayers(TArray<FGeoDebugLayer>& Out) const
{
	{
		FGeoDebugLayer L;
		L.Id = TEXT("ClimateTemperature"); L.DisplayName = TEXT("温度"); L.Stage = 5;
		L.Unit = TEXT("0 冷 … 1 热");
		L.Sample = [](const FPlanetSnapshot& S, const FVector3d& P) { return Stage05DebugField(S, TEXT("Temperature"), P, false); };
		// 默认连续色带是紫→黄，温度要蓝→红：低纬一眼是红的。
		L.Colorize = [](const FPlanetSnapshot&, float V)
		{
			return FMath::Lerp(FLinearColor(0.10f, 0.25f, 0.90f), FLinearColor(0.90f, 0.12f, 0.05f), FMath::Clamp(V, 0.f, 1.f)).ToFColor(false);
		};
		Out.Add(MoveTemp(L));
	}
	{
		FGeoDebugLayer L;
		L.Id = TEXT("ClimateWind"); L.DisplayName = TEXT("风带"); L.Stage = 5;
		L.Kind = EGeoDebugKind::Categorical;
		L.Legend = { { TEXT("无风"), FColor(70, 70, 70) }, { TEXT("信风（向西）"), FColor(60, 170, 220) },
			{ TEXT("西风（向东）"), FColor(80, 200, 110) }, { TEXT("极地东风（向西）"), FColor(200, 140, 220) } };
		// 场里只有方位角：信风与极地东风角度相同，分类必须再叠纬度。
		L.Sample = [](const FPlanetSnapshot& S, const FVector3d& P) -> float
		{
			const FGeoField2D* Fd = S.DebugFields.Find(FName(TEXT("WindDir")));
			if (!Fd || Fd->Res <= 0) { return FGeoDebugLayer::NoData(); }
			const FVector3f Dir(P.GetSafeNormal());
			if (Dir.IsNearlyZero()) { return FGeoDebugLayer::NoData(); }
			const float Az = Fd->Sample(Dir);
			const float AbsLat = FMath::RadiansToDegrees(FMath::Abs(FMath::Asin(FMath::Clamp(Dir.Z, -1.f, 1.f))));
			const bool bWest = FMath::Abs(FMath::FindDeltaAngleDegrees(Az, 270.f)) <= 10.f;
			const bool bEast = FMath::Abs(FMath::FindDeltaAngleDegrees(Az, 90.f)) <= 10.f;
			if (AbsLat < 30.f) { return bWest ? 1.f : 0.f; }
			if (AbsLat < 60.f) { return bEast ? 2.f : 0.f; }
			return bWest ? 3.f : 0.f;
		};
		Out.Add(MoveTemp(L));
	}
}

void FStage05Climate::ExportDebugFields(const FPlanetBakeFields& Fields, TMap<FName, FGeoField2D>& Out) const
{
	if (Fields.Temperature.Res > 0) { Out.Add(TEXT("Temperature"), Fields.Temperature); }
	if (Fields.WindDir.Res > 0) { Out.Add(TEXT("WindDir"), Fields.WindDir); }
}
