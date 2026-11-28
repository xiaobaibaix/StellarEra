#include "GeoSurfaceDepositRules.h"
#include "GeoLithology.h"
#include "GeoNoise.h"
#include "PlanetBakeTypes.h"

uint64 FGeoBlanketParams::HashParams() const
{
	uint64 H = uint64(uint8(Host)) | (uint64(uint32(HostValue)) << 8);
	H = GeoHash::Combine(H, GeoHash::Float(MinGrade));
	H = GeoHash::Combine(H, GeoHash::Float(Threshold));
	H = GeoHash::Combine(H, GeoHash::Float(Chance));
	return GeoHash::Combine(H, GeoHash::Float(DepthVoxels));
}

uint64 FGeoBlanketRule::HashParams() const
{
	return GeoHash::Combine(P.HashParams(), uint64(Stage));
}

void FGeoBlanketRule::Scatter(FGeoScatterContext& Ctx, FGeoScatterOutput& Out) const
{
	const FPlanetBakeFields& F = Ctx.Fields;
	const FGeoField2D* Src = nullptr;
	switch (P.Host)
	{
	case EGeoBlanketHost::StrataLithology:
	case EGeoBlanketHost::Marble: Src = &F.StrataType; break;
	case EGeoBlanketHost::Soil: Src = &F.SoilType; break;
	case EGeoBlanketHost::Metamorphic: Src = &F.MetamorphicGrade; break;
	case EGeoBlanketHost::Fault: Src = &F.FaultMask; break;
	}
	if (!Src || Src->Res <= 0) { return; }

	FGeoMineralOverlay O;
	O.DepthVoxels = P.DepthVoxels;
	O.Grade = RandomGrade(Ctx.Rng);
	O.Mask.Init(Src->Res);
	const uint64 Seed = Ctx.Rng.NextU64();
	O.Mask.Fill([&](const FVector3f& Dir) -> float
	{
		const float V = Src->SampleNearest(Dir);
		bool bOk = false;
		switch (P.Host)
		{
		case EGeoBlanketHost::StrataLithology:
		case EGeoBlanketHost::Soil:
			bOk = FMath::RoundToInt(V) == P.HostValue;
			break;
		case EGeoBlanketHost::Metamorphic:
		case EGeoBlanketHost::Fault:
			bOk = V >= P.MinGrade;
			break;
		case EGeoBlanketHost::Marble:
			// 盖层存的是石灰岩，变质后材质侧才变大理岩；这里两者都要满足。
			bOk = FMath::RoundToInt(V) == GeoLithology::Limestone && F.MetamorphicGrade.Res > 0
				&& F.MetamorphicGrade.SampleNearest(Dir) > P.Threshold;
			break;
		}
		if (!bOk) { return 0.f; }
		const float N = 0.5f + 0.5f * GeoNoise::Fbm3(Seed, Dir * 6.f, 3);
		return N < P.Chance ? 1.f : 0.f;
	}, Ctx.Config.bSingleThreadBake);
	Out.Overlays.Add(MoveTemp(O));
}

uint64 FGeoSupergeneRule::HashParams() const
{
	uint64 H = GeoHash::Combine(GeoHash::Str("supergene"), GeoHash::Str(TCHAR_TO_ANSI(*HostMineral.ToString())));
	H = GeoHash::Combine(H, GeoHash::Float(DepthBelowSurface));
	H = GeoHash::Combine(H, GeoHash::Float(ThicknessVoxels));
	H = GeoHash::Combine(H, GeoHash::Float(HostMargin));
	return GeoHash::Combine(H, uint64(Stage));
}

void FGeoSupergeneRule::Scatter(FGeoScatterContext& Ctx, FGeoScatterOutput& Out) const
{
	const uint8 Host = GeoSurfaceDeposit::FindMineralByName(Ctx, HostMineral);
	if (Host == 0) { return; }
	const FPlanetBakeFields& F = Ctx.Fields;
	const double R = Ctx.Config.Radius;
	for (const FGeoPrimitive& P : F.Primitives)
	{
		if (P.MineralId != Host) { continue; }
		const FVector3f Dir = P.Center.GetSafeNormal();
		const float E = F.Elevation.Res > 0 ? F.Elevation.Sample(Dir) : 0.f;
		const double BandR = R + double(E) - double(DepthBelowSurface) - 0.5 * double(ThicknessVoxels);
		FGeoPrimitive Band = FGeoPrimitive::MakeBlob(FVector3f(Dir * float(BandR)),
			FVector3f(FMath::Max(P.Radii.X, 1.f) * HostMargin, FMath::Max(P.Radii.Y, 1.f) * HostMargin, ThicknessVoxels * 0.5f),
			0.15f);
		Band.Grade = RandomGrade(Ctx.Rng);
		Out.Bodies.Add(Band);
	}
}

uint64 FGeoGossanRule::HashParams() const
{
	uint64 H = GeoHash::Combine(GeoHash::Str("gossan"), GeoHash::Str(TCHAR_TO_ANSI(*HostMineral.ToString())));
	H = GeoHash::Combine(H, GeoHash::Float(MaxDepthVoxels));
	H = GeoHash::Combine(H, GeoHash::Float(CapDepthVoxels));
	return GeoHash::Combine(H, uint64(Stage));
}

void FGeoGossanRule::Scatter(FGeoScatterContext& Ctx, FGeoScatterOutput& Out) const
{
	const uint8 Host = GeoSurfaceDeposit::FindMineralByName(Ctx, HostMineral);
	if (Host == 0) { return; }
	const FPlanetBakeFields& F = Ctx.Fields;
	const double R = Ctx.Config.Radius;
	for (const FGeoPrimitive& P : F.Primitives)
	{
		if (P.MineralId != Host) { continue; }
		const FVector3f Dir = P.Center.GetSafeNormal();
		const float E = F.Elevation.Res > 0 ? F.Elevation.Sample(Dir) : 0.f;
		const double SurfaceR = R + double(E);
		// 宿主顶面（按包围球保守估计）离地表多深；没剥蚀到它就还没有铁帽。
		const double Depth = SurfaceR - (double(P.Center.Size()) + double(P.Radii.GetMax()));
		if (Depth > double(MaxDepthVoxels)) { continue; }
		FGeoPrimitive Cap = FGeoPrimitive::MakeBlob(FVector3f(Dir * float(SurfaceR - double(CapDepthVoxels) * 0.5)),
			FVector3f(FMath::Max(P.Radii.X, 1.f) * RadiusScale, FMath::Max(P.Radii.Y, 1.f) * RadiusScale, CapDepthVoxels * 0.5f),
			0.2f);
		Cap.Grade = RandomGrade(Ctx.Rng);
		Out.Bodies.Add(Cap);
	}
}

uint64 FGeoSkarnRule::HashParams() const
{
	uint64 H = GeoHash::Combine(GeoHash::Float(RadiusScale), GeoHash::Float(ThicknessVoxels));
	return GeoHash::Combine(H, uint64(Stage));
}

void FGeoSkarnRule::Scatter(FGeoScatterContext& Ctx, FGeoScatterOutput& Out) const
{
	const FPlanetBakeFields& F = Ctx.Fields;
	if (F.StrataType.Res <= 0) { return; }
	for (const FGeoPrimitive& P : F.Primitives)
	{
		// 只有酸性 / 中酸性侵入体与碳酸盐岩接触才成矽卡岩。
		if (P.LithologyId != GeoLithology::Granite && P.LithologyId != GeoLithology::Diorite) { continue; }
		const FVector3f Dir = P.Center.GetSafeNormal();
		if (FMath::RoundToInt(F.StrataType.SampleNearest(Dir)) != GeoLithology::Limestone) { continue; }
		FGeoPrimitive Ring = FGeoPrimitive::MakeBlob(
			FVector3f(Dir * float(P.Center.Size() + double(P.Radii.Z))),
			FVector3f(FMath::Max(P.Radii.X, 1.f) * RadiusScale, FMath::Max(P.Radii.Y, 1.f) * RadiusScale, ThicknessVoxels * 0.5f),
			0.25f);
		Ring.Grade = RandomGrade(Ctx.Rng);
		Out.Bodies.Add(Ring);
	}
}

namespace GeoSurfaceDeposit
{
	uint8 FindMineralByName(const FGeoScatterContext& Ctx, const FName& Name)
	{
		const FString Want = Name.ToString();
		for (int32 i = 0; i < Ctx.Config.Minerals.Num(); ++i)
		{
			if (Ctx.Config.Minerals[i].Name == Want) { return uint8(i + 1); }
		}
		return 0;
	}
}
