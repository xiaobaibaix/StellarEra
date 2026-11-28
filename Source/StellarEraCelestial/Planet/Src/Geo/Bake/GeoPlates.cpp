#include "GeoPlates.h"
#include "GeoNoise.h"

namespace
{
	/** Fibonacci 球面等面积点 + 单轴抖动：不抖动时 7 块板块呈规则条纹。 */
	FVector3f FibonacciPoint(int32 Index, int32 Count, FGeoRng& Rng)
	{
		const float Z = 1.f - 2.f * (float(Index) + 0.5f) / float(Count);
		const float Theta = float(Index) * 2.39996323f;
		const float S = FMath::Sqrt(FMath::Max(0.f, 1.f - Z * Z));
		const FVector3f Dir(S * FMath::Cos(Theta), S * FMath::Sin(Theta), Z);

		// 半个点间距：再大两两点会互相换位。
		const float Amp = 0.4f * FMath::Sqrt(4.f * PI / float(Count));
		FVector3f Offset(0.f);
		Offset[int32(Rng.NextU64() % 3ull)] = Rng.Range(-Amp, Amp);
		return (Dir + Offset).GetSafeNormal();
	}
}

FGeoPlateFrame GeoPlates::Build(const FPlanetRecipeConfig& Config)
{
	const FPlanetPlateParams& P = Config.Plates;
	FGeoPlateFrame Frame;

	FGeoRng Rng = FGeoRng::Derive(Config.Seed, 2, GeoHash::Str("plates"));
	Frame.WarpSeed = Rng.NextU64();

	const int32 N = FMath::Clamp(P.PlateCount, 3, 15);
	Frame.Plates.SetNum(N);
	for (int32 i = 0; i < N; ++i)
	{
		FGeoPlate& Plate = Frame.Plates[i];
		Plate.Center = FibonacciPoint(i, N, Rng);
		// 均匀抽 log 空间：线性抽权重的面积差不够，出不了巨型板块。
		Plate.Weight = P.PlateWeightMin * FMath::Pow(P.PlateWeightMax / P.PlateWeightMin, Rng.NextFloat());
	}

	TArray<EGeoPlateKind> Kinds;
	Kinds.Reserve(N);
	const int32 EachPlate = FMath::FloorToInt(float(N) * 3.f / 7.f);
	for (int32 i = 0; i < EachPlate; ++i) { Kinds.Add(EGeoPlateKind::Continental); }
	for (int32 i = 0; i < EachPlate; ++i) { Kinds.Add(EGeoPlateKind::Oceanic); }
	while (Kinds.Num() < N) { Kinds.Add(EGeoPlateKind::Mixed); }
	// Fisher–Yates：同一 Rng 洗牌，顺序也被配方唯一确定。
	for (int32 i = N - 1; i > 0; --i)
	{
		Swap(Kinds[i], Kinds[int32(Rng.NextU64() % uint64(i + 1))]);
	}
	for (int32 i = 0; i < N; ++i) { Frame.Plates[i].Kind = Kinds[i]; }

	// 运动用独立随机流：加运动不改变板块中心与权重（板块形状不变）。
	FGeoRng Motion = FGeoRng::Derive(Config.Seed, 2, GeoHash::Str("plate-motion"));
	const float SMin = FMath::Max(0.f, FMath::Min(P.SpeedMin, P.SpeedMax)), SMax = FMath::Max(P.SpeedMin, P.SpeedMax);
	for (int32 i = 0; i < N; ++i)
	{
		FGeoPlate& Plate = Frame.Plates[i];
		float Speed = Motion.Range(SMin, SMax);
		float Azimuth = Motion.Range(0.f, 360.f);
		for (const FPlanetPlateOverride& O : P.Overrides)
		{
			if (O.Plate != i) { continue; }
			if (O.bSpeed) { Speed = FMath::Max(O.Speed, 0.f); }
			if (O.bDirection) { Azimuth = O.DirectionDeg; }
		}
		const FVector3f C = Plate.Center;
		const FVector3f Ref = FMath::Abs(C.Z) < 0.99f ? FVector3f::UpVector : FVector3f::ForwardVector;
		const FVector3f North = (Ref - C * FVector3f::DotProduct(Ref, C)).GetSafeNormal();
		const FVector3f East = FVector3f::CrossProduct(North, C);
		const float Az = FMath::DegreesToRadians(Azimuth);
		const FVector3f Vel = North * FMath::Cos(Az) + East * FMath::Sin(Az);
		// (C × V) × C = V：这样构造的欧拉极让板块中心处的速度恰好是 Speed · V。
		Plate.Omega = FVector3f::CrossProduct(C, Vel) * Speed;
		Plate.Speed = Speed;
	}

	return Frame;
}

FVector3f GeoPlates::WarpDirection(const FGeoPlateFrame& Frame, const FVector3f& Dir, const FPlanetPlateParams& P)
{
	if (Dir.IsNearlyZero()) { return Dir; }
	// 两级：大尺度弯折出凸角与凹湾，小尺度在弯折后的坐标上再加锯齿。实测（FaceRes 1024）：0.5 / 2 / 0.08 / 10 边界长 1.5 倍、飞地 0.2%；再大飞地陡增。
	const FVector3f Q = Dir * P.WarpFrequency;
	const int32 Oct = FMath::Clamp(P.WarpOctaves, 1, 8);
	const FVector3f Big(
		GeoNoise::Fbm3(Frame.WarpSeed ^ 0ull, Q, Oct),
		GeoNoise::Fbm3(Frame.WarpSeed ^ 1ull, Q, Oct),
		GeoNoise::Fbm3(Frame.WarpSeed ^ 2ull, Q, Oct));
	FVector3f W = Dir + P.WarpAmp * Big;
	W = W.IsNearlyZero() ? Dir : W.GetSafeNormal();
	if (P.DetailWarpAmp > 0.f)
	{
		const FVector3f Q2 = W * P.DetailWarpFrequency;
		const FVector3f Small(
			GeoNoise::Fbm3(Frame.WarpSeed ^ 10ull, Q2, 3),
			GeoNoise::Fbm3(Frame.WarpSeed ^ 11ull, Q2, 3),
			GeoNoise::Fbm3(Frame.WarpSeed ^ 12ull, Q2, 3));
		const FVector3f W2 = W + P.DetailWarpAmp * Small;
		W = W2.IsNearlyZero() ? W : W2.GetSafeNormal();
	}
	return W;
}

void GeoPlates::NearestTwo(const FGeoPlateFrame& Frame, const FVector3f& Dir,
	int32& OutNear, int32& OutSecond, float& OutNearDist, float& OutSecondDist)
{
	OutNear = 0;
	OutSecond = 0;
	OutNearDist = TNumericLimits<float>::Max();
	OutSecondDist = TNumericLimits<float>::Max();
	for (int32 p = 0; p < Frame.Plates.Num(); ++p)
	{
		const FGeoPlate& Plate = Frame.Plates[p];
		const float D = (Dir - Plate.Center).Size() / Plate.Weight;
		// 严格小于：平手时索引小者留在最近位，保证稳定。
		if (D < OutNearDist)
		{
			OutSecondDist = OutNearDist;
			OutSecond = OutNear;
			OutNearDist = D;
			OutNear = p;
		}
		else if (D < OutSecondDist)
		{
			OutSecondDist = D;
			OutSecond = p;
		}
	}
}

FGeoBoundaryMotion GeoPlates::BoundaryMotion(const FGeoPlateFrame& Frame, const FVector3f& Dir, int32 A, int32 B, const FPlanetPlateParams& P)
{
	FGeoBoundaryMotion Out;
	if (!Frame.Plates.IsValidIndex(A) || !Frame.Plates.IsValidIndex(B) || A == B) { return Out; }
	const FGeoPlate& PA = Frame.Plates[A];
	const FGeoPlate& PB = Frame.Plates[B];
	const FVector3f Chord = PB.Center - PA.Center;
	const FVector3f N = (Chord - Dir * FVector3f::DotProduct(Chord, Dir)).GetSafeNormal();
	const FVector3f Rel = PB.VelocityAt(Dir) - PA.VelocityAt(Dir);
	const float Normal = FVector3f::DotProduct(Rel, N);
	Out.Convergence = -Normal;
	Out.Shear = (Rel - N * Normal).Size();
	if (Out.Shear > FMath::Max(P.TransformShearRatio, 0.f) * FMath::Abs(Out.Convergence)) { Out.Type = 5; }
	else { Out.Type = Out.Convergence > 0.f ? 1 : 4; }
	return Out;
}

uint64 FGeoPlateFrame::Hash() const
{
	uint64 H = GeoHash::Combine(WarpSeed, uint64(Plates.Num()));
	for (const FGeoPlate& P : Plates)
	{
		H = GeoHash::Combine(H, GeoHash::Float(P.Center.X));
		H = GeoHash::Combine(H, GeoHash::Float(P.Center.Y));
		H = GeoHash::Combine(H, GeoHash::Float(P.Center.Z));
		H = GeoHash::Combine(H, GeoHash::Float(P.Weight));
		H = GeoHash::Combine(H, uint64(P.Kind));
		const float M[4] = { P.Omega.X, P.Omega.Y, P.Omega.Z, P.Speed };
		for (const float V : M) { H = GeoHash::Combine(H, GeoHash::Float(V)); }
	}
	return H;
}
