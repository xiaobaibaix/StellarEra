#include "Stage03bTectonics.h"
#include "GeoNoise.h"
#include "GeoPlates.h"
#include "PlanetDebugLayer.h"
#include "PlanetSnapshot.h"

void FStage03bTectonics::Bake(FPlanetBakeContext& Ctx) const
{
	FPlanetBakeFields& F = Ctx.Fields;
	const FPlanetTectonicParams& T = Ctx.Config.Tectonics;
	if (!T.bTectonics) { return; }
	if (F.PlateId.Res <= 0 || F.MountainMask.Res <= 0)
	{
		Ctx.Report.Errors.Add(TEXT("[地质] 阶段 3B：缺阶段 2 的板块场或阶段 3 的造山场"));
		return;
	}
	const int32 Res = FMath::Clamp(Ctx.Config.FaceRes / 4, 32, 256);
	F.FaultMask.Init(Res);
	F.FoldField.Init(Res);
	F.MetamorphicGrade.Init(Res);

	const float Width = FMath::Max(T.FaultWidthRatio, 1e-4f);
	const float Wavelength = FMath::Max(T.FoldWavelengthRatio, 1e-4f);
	const uint64 FoldSeed = Ctx.Rng.NextU64();

	F.FaultMask.Fill([&](const FVector3f& Dir) -> float
	{
		// 断层出现在有剪切或挤压的边界上：离散边界只裂谷，不成断层带。
		const float Type = F.BoundaryType.SampleNearest(Dir);
		const bool bShear = Type > 4.5f || Type < 3.5f;
		if (!bShear) { return 0.f; }
		const float B = F.BoundaryDistance.Sample(Dir);
		return FMath::Clamp(1.f - B / Width, 0.f, 1.f);
	}, Ctx.Config.bSingleThreadBake);

	F.FoldField.Fill([&](const FVector3f& Dir) -> float
	{
		return GeoNoise::Fbm3(FoldSeed, Dir * (1.f / Wavelength), 2);
	}, Ctx.Config.bSingleThreadBake);

	F.MetamorphicGrade.Fill([&](const FVector3f& Dir) -> float
	{
		// 造山带越强、越靠近断层，变质级越高；这是地表级，深度由材质求值另算。
		const float Orogen = FMath::Clamp(F.MountainMask.Sample(Dir), 0.f, 1.f);
		const float Fault = F.FaultMask.Sample(Dir);
		return FMath::Clamp(Orogen * T.OrogenBoost + Fault * 0.4f, 0.f, 1.f);
	}, Ctx.Config.bSingleThreadBake);
}

void FStage03bTectonics::DescribeDebugLayers(TArray<FGeoDebugLayer>& Out) const
{
	{
		FGeoDebugLayer L;
		L.Id = TEXT("FaultMask");
		L.DisplayName = TEXT("断层带");
		L.Stage = 10;
		L.Min = 0.f;
		L.Max = 1.f;
		L.Sample = [](const FPlanetSnapshot& S, const FVector3d& P)
		{
			return S.FaultMask.Res > 0
				? S.FaultMask.Sample(FVector3f(P.GetSafeNormal())) : FGeoDebugLayer::NoData();
		};
		Out.Add(MoveTemp(L));
	}
	{
		FGeoDebugLayer L;
		L.Id = TEXT("MetamorphicGrade");
		L.DisplayName = TEXT("变质级");
		L.Stage = 10;
		L.Min = 0.f;
		L.Max = 1.f;
		L.Sample = [](const FPlanetSnapshot& S, const FVector3d& P)
		{
			return S.MetamorphicGrade.Res > 0
				? S.MetamorphicGrade.Sample(FVector3f(P.GetSafeNormal())) : FGeoDebugLayer::NoData();
		};
		Out.Add(MoveTemp(L));
	}
}

void FStage03bTectonics::ExportDebugFields(const FPlanetBakeFields& Fields, TMap<FName, FGeoField2D>& Out) const
{
	if (Fields.FaultMask.Res > 0) { Out.Add(TEXT("FaultMask"), Fields.FaultMask); }
	if (Fields.FoldField.Res > 0) { Out.Add(TEXT("FoldField"), Fields.FoldField); }
	if (Fields.MetamorphicGrade.Res > 0) { Out.Add(TEXT("MetamorphicGrade"), Fields.MetamorphicGrade); }
}
