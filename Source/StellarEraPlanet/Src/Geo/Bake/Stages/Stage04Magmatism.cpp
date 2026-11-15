#include "Stage04Magmatism.h"
#include "Stage04bIntrusions.h"
#include "Async/ParallelFor.h"
#include "GeoContinentField.h"
#include "GeoLithology.h"
#include "GeoNoise.h"
#include "GeoPlates.h"
#include "PlanetMaterialEval.h"
#include "PlanetSnapshot.h"

namespace
{
	FVector3f Stage04Tangent(const FVector3f& D)
	{
		const FVector3f Ref = FMath::Abs(D.Z) < 0.9f ? FVector3f::UpVector : FVector3f::ForwardVector;
		return FVector3f::CrossProduct(Ref, D).GetSafeNormal();
	}
	/** 从 D 出发沿切向 T 走弦长 S：小角度下 normalize(D + T·S) 足够准。 */
	FVector3f Stage04Step(const FVector3f& D, const FVector3f& T, float S) { return (D + T * S).GetSafeNormal(); }

	int32 Stage04PlateAt(const FGeoPlateFrame& Frame, const FPlanetPlateParams& P, const FVector3f& D)
	{
		int32 A, B; float Da, Db;
		GeoPlates::NearestTwo(Frame, GeoPlates::WarpDirection(Frame, D, P), A, B, Da, Db);
		return A;
	}
	/** 侵入体图层的紧凑编号：岩性 Id 9..14 中间有橄榄岩空位，直接当下标会在图例里留黑洞。 */
	uint8 Stage04CompactLitho(uint8 Litho)
	{
		switch (Litho)
		{
		case GeoLithology::Granite:      return 1;
		case GeoLithology::Diorite:      return 2;
		case GeoLithology::Gabbro:       return 3;
		case GeoLithology::Kimberlite:   return 4;
		case GeoLithology::LayeredMafic: return 5;
		case GeoLithology::Carbonatite:  return 6;
		default:                         return 0;
		}
	}
}

void Stage04::PlaceVolcanoes(const FPlanetRecipeConfig& Cfg, const FPlanetBakeFields& F, TArray<FGeoVolcano>& Out)
{
	Out.Reset();
	const FPlanetVolcanoParams& V = Cfg.Volcano;
	const float R = float(Cfg.Radius);
	const FGeoPlateFrame& Frame = F.PlateFrame;
	const int32 MacroRes = F.PlateId.Res;
	const float SpeedRef = FMath::Max(Cfg.Plates.SpeedMax, 1e-3f);

	// —— 层状火山：汇聚边界上盘一侧，泊松盘沿弧排开 ——
	if (V.bArcVolcanoes && V.StratoBaseRatio > 0.f)
	{
		FGeoRng Rng = FGeoRng::Derive(Cfg.Seed, 4, GeoHash::Str("arc"));
		struct FCand { FVector3f Dir; int32 Upper, Lower; float Rate; uint64 Key; };
		TArray<FCand> Cands;
		for (int32 Face = 0; Face < 6; ++Face)
		for (int32 Y = 0; Y < MacroRes; ++Y)
		for (int32 X = 0; X < MacroRes; ++X)
		{
			const float Type = F.BoundaryType.At(Face, X, Y);
			if (Type != 1.f && Type != 2.f) { continue; }   // 3 = 陆陆碰撞：没有俯冲，不长弧火山
			const FVector3f D0 = F.PlateId.TexelDir(Face, X, Y);
			int32 A, B; float Da, Db;
			GeoPlates::NearestTwo(Frame, GeoPlates::WarpDirection(Frame, D0, Cfg.Plates), A, B, Da, Db);
			const FGeoBoundaryMotion Mot = GeoPlates::BoundaryMotion(Frame, D0, A, B, Cfg.Plates);
			if (Mot.Type != 1) { continue; }
			// 上盘：大陆性更强的一侧（洋洋俯冲取编号小者，保证确定）。
			const FVector3f Toward = (Frame.Plates[B].Center - Frame.Plates[A].Center).GetSafeNormal();
			const FVector3f PA = Stage04Step(D0, -(Toward - D0 * FVector3f::DotProduct(Toward, D0)).GetSafeNormal(), V.ArcOffsetRatio);
			const FVector3f PB = Stage04Step(D0, (Toward - D0 * FVector3f::DotProduct(Toward, D0)).GetSafeNormal(), V.ArcOffsetRatio);
			const float MA = F.ContinentalMask.Sample(PA), MB = F.ContinentalMask.Sample(PB);
			const bool bAUpper = FMath::Abs(MA - MB) > 0.1f ? MA > MB : A < B;
			const FVector3f P = bAUpper ? PA : PB;
			const int32 Upper = bAUpper ? A : B, Lower = bAUpper ? B : A;
			if (Stage04PlateAt(Frame, Cfg.Plates, P) != Upper) { continue; }   // 偏移后跨到别的板块：边界太弯，丢掉
			Cands.Add({ P, Upper, Lower, FMath::Clamp(Mot.Convergence / SpeedRef, 0.f, 1.f), Rng.NextU64() });
		}
		// 随机顺序贪心泊松盘：接受半径带 ±30% 抖动。
		Cands.Sort([](const FCand& L, const FCand& Rr) { return L.Key < Rr.Key; });
		for (const FCand& Cd : Cands)
		{
			const float Gap = V.ArcSpacingRatio * (0.7f + 0.6f * float(Cd.Key >> 40) / 16777216.f);
			bool bFar = true;
			for (const FGeoVolcano& Vo : Out) { if (Vo.OtherPlate >= 0 && (Vo.Dir - Cd.Dir).Size() < Gap) { bFar = false; break; } }
			if (!bFar) { continue; }
			FGeoVolcano Vo;
			Vo.Dir = Cd.Dir; Vo.Kind = EGeoVolcanoKind::Strato; Vo.Plate = Cd.Upper; Vo.OtherPlate = Cd.Lower;
			const float Sz = Rng.Range(0.75f, 1.25f);
			Vo.BaseRadius = V.StratoBaseRatio * Sz;
			Vo.Height = V.StratoHeightRatio * R * Sz * FMath::Lerp(0.7f, 1.f, Cd.Rate);
			Vo.CraterRadius = V.StratoCraterRatio * Sz;
			Vo.CraterDepth = V.CraterDepthRatio * R;
			// 挤得越快越可能活跃：比例由 ArcActiveFraction 定，速率只是排序权重。
			Vo.bActive = Rng.NextFloat() < FMath::Clamp(V.ArcActiveFraction * (0.5f + Cd.Rate), 0.f, 1.f);
			Vo.Age = Vo.bActive ? Rng.Range(0.f, 0.3f) : Rng.Range(0.3f, 1.f);
			Out.Add(Vo);
		}
	}

	// —— 盾状火山：地幔柱热点，链顺着板块运动方向变老 ——
	if (V.bHotspots && V.HotspotCount > 0 && V.ShieldBaseRatio > 0.f)
	{
		FGeoRng Rng = FGeoRng::Derive(Cfg.Seed, 4, GeoHash::Str("hotspot"));
		const int32 Len = FMath::Max(V.HotspotChainLength, 1);
		for (int32 h = 0; h < V.HotspotCount; ++h)
		{
			const float Z = Rng.Range(-1.f, 1.f), Phi = Rng.Range(0.f, 2.f * PI), S = FMath::Sqrt(1.f - Z * Z);
			FVector3f P(S * FMath::Cos(Phi), S * FMath::Sin(Phi), Z);
			const int32 Plate = Stage04PlateAt(Frame, Cfg.Plates, P);
			const FGeoPlate& Pl = Frame.Plates[Plate];
			// 板块几乎不动：链方向不定，只放热点正上方一座。
			const int32 N = Pl.VelocityAt(P).Size() < 0.5f * Cfg.Plates.SpeedMin ? 1 : Len;
			for (int32 k = 0; k < N; ++k)
			{
				if (k > 0)
				{
					const FVector3f Vel = Pl.VelocityAt(P);
					if (Vel.IsNearlyZero()) { break; }
					P = Stage04Step(P, Vel.GetSafeNormal(), V.HotspotSpacingRatio);
					if (Stage04PlateAt(Frame, Cfg.Plates, P) != Plate) { break; }   // 漂进别的板块：链断
				}
				FGeoVolcano Vo;
				Vo.Dir = P; Vo.Kind = EGeoVolcanoKind::Shield; Vo.Plate = Plate; Vo.Chain = h; Vo.ChainIndex = k;
				Vo.Age = N > 1 ? float(k) / float(Len - 1) : 0.f;
				Vo.bActive = k <= 1;
				const float Sz = Rng.Range(0.85f, 1.15f) * FMath::Lerp(1.f, 0.7f, Vo.Age);
				Vo.BaseRadius = V.ShieldBaseRatio * Sz;
				Vo.Height = V.ShieldHeightRatio * R * Sz;
				Vo.CraterRadius = 0.08f * Vo.BaseRadius;
				Vo.CraterDepth = 0.5f * V.CraterDepthRatio * R;
				Out.Add(Vo);
			}
		}
	}

	// —— 溢流玄武岩：大陆裂谷宏观 texel 聚成段，每段按概率出一片 ——
	if (V.bFloodBasalt && V.FloodBasaltChance > 0.f && V.FloodBasaltSizeRatio > 0.f)
	{
		FGeoRng Rng = FGeoRng::Derive(Cfg.Seed, 4, GeoHash::Str("flood"));
		TArray<FVector3f> Placed;
		for (int32 Face = 0; Face < 6; ++Face)
		for (int32 Y = 0; Y < MacroRes; ++Y)
		for (int32 X = 0; X < MacroRes; ++X)
		{
			if (F.ReliefType.At(Face, X, Y) != 6.f) { continue; }
			const FVector3f D = F.ReliefType.TexelDir(Face, X, Y);
			// 一段 ≈ 两个台地宽：段内只掷一次骰子。
			bool bNew = true;
			for (const FVector3f& Q : Placed) { if ((Q - D).Size() < 4.f * V.FloodBasaltSizeRatio) { bNew = false; break; } }
			if (!bNew) { continue; }
			Placed.Add(D);
			if (Rng.NextFloat() >= V.FloodBasaltChance) { continue; }
			FGeoVolcano Vo;
			Vo.Dir = D; Vo.Kind = EGeoVolcanoKind::FloodBasalt; Vo.Plate = Stage04PlateAt(Frame, Cfg.Plates, D);
			Vo.BaseRadius = V.FloodBasaltSizeRatio * Rng.Range(0.8f, 1.2f);
			Vo.Height = V.FloodBasaltHeightRatio * R;
			Vo.Age = Rng.Range(0.2f, 1.f);
			Out.Add(Vo);
		}
	}

	// —— 破火山口：在层状 + 盾状中抽底半径大于中位数的，就地改写 ——
	if (V.bCalderas && V.CalderaFraction > 0.f)
	{
		FGeoRng Rng = FGeoRng::Derive(Cfg.Seed, 4, GeoHash::Str("caldera"));
		TArray<float> Sizes;
		for (const FGeoVolcano& Vo : Out) { if (Vo.Kind == EGeoVolcanoKind::Strato || Vo.Kind == EGeoVolcanoKind::Shield) { Sizes.Add(Vo.BaseRadius); } }
		Sizes.Sort();
		const float Median = Sizes.Num() ? Sizes[Sizes.Num() / 2] : 0.f;
		for (FGeoVolcano& Vo : Out)
		{
			if (Vo.Kind != EGeoVolcanoKind::Strato && Vo.Kind != EGeoVolcanoKind::Shield) { continue; }
			// 每座都掷骰（即使不够大），保证随机流消耗与尺寸无关。
			const bool bHit = Rng.NextFloat() < 2.f * V.CalderaFraction;
			if (!bHit || Vo.BaseRadius < Median) { continue; }
			Vo.Kind = EGeoVolcanoKind::Caldera;
			Vo.CraterRadius = FMath::Min(V.CalderaRadiusRatio, 0.6f * Vo.BaseRadius);
			Vo.CraterDepth = V.CalderaDepthRatio * R;
		}
	}

	// —— 火山渣锥：每座活火山周围 1–2 个底半径处 ——
	if (V.bCinderCones && V.CinderPerVolcano > 0 && V.CinderBaseRatio > 0.f)
	{
		FGeoRng Rng = FGeoRng::Derive(Cfg.Seed, 4, GeoHash::Str("cinder"));
		const int32 Parents = Out.Num();
		for (int32 i = 0; i < Parents; ++i)
		{
			const FGeoVolcano Pv = Out[i];
			if (!Pv.bActive || Pv.Kind == EGeoVolcanoKind::CinderCone || Pv.Kind == EGeoVolcanoKind::FloodBasalt) { continue; }
			const FVector3f T1 = Stage04Tangent(Pv.Dir), T2 = FVector3f::CrossProduct(Pv.Dir, T1);
			for (int32 k = 0; k < V.CinderPerVolcano; ++k)
			{
				const float Ang = Rng.Range(0.f, 2.f * PI), Dist = Pv.BaseRadius * Rng.Range(1.f, 2.f);
				FGeoVolcano Vo;
				Vo.Dir = Stage04Step(Pv.Dir, T1 * FMath::Cos(Ang) + T2 * FMath::Sin(Ang), Dist);
				Vo.Kind = EGeoVolcanoKind::CinderCone; Vo.Plate = Pv.Plate; Vo.bActive = true; Vo.Age = 0.f;
				const float Sz = Rng.Range(0.7f, 1.3f);
				Vo.BaseRadius = V.CinderBaseRatio * Sz;
				Vo.Height = V.CinderHeightRatio * R * Sz;
				Vo.CraterRadius = 0.25f * Vo.BaseRadius;
				Vo.CraterDepth = 0.3f * Vo.Height;
				Out.Add(Vo);
			}
		}
	}

	for (FGeoVolcano& Vo : Out) { Vo.BaseElevation = F.Elevation.Sample(Vo.Dir); }
}
float Stage04::Profile(const FGeoVolcano& V, float Chord, float& OutCrater)
{
	OutCrater = 0.f;
	if (V.BaseRadius <= 0.f || Chord >= V.BaseRadius) { return 0.f; }
	const float D = Chord / V.BaseRadius;
	const float C = V.CraterRadius / V.BaseRadius;
	// 陆上老火山：削低、坡变缓、火口被填浅。真实剥蚀留阶段 6。
	const float Wear = (V.BaseElevation > 0.f && V.Age > 0.5f) ? V.Age : 0.f;
	const float H = V.Height * (1.f - 0.5f * Wear);
	// 火口内取火口边缘的锥高：顶部是平台，火口才挖得下去（否则锥尖比火口深还高）。
	const float Dc = FMath::Max(D, C);
	const auto Cone = [&](float X) { return H * FMath::Pow(1.f - X, FMath::Lerp(1.6f, 1.1f, Wear)); };
	const auto Dome = [&](float X) { return H * FMath::Pow(FMath::Max(1.f - X * X, 0.f), 1.5f); };
	float Up = 0.f;
	switch (V.Kind)
	{
	case EGeoVolcanoKind::Strato:      Up = Cone(Dc); break;
	case EGeoVolcanoKind::Shield:      Up = Dome(Dc); break;
	case EGeoVolcanoKind::FloodBasalt: Up = H * (1.f - FMath::SmoothStep(0.9f, 1.f, D)); break;
	case EGeoVolcanoKind::CinderCone:  Up = H * (1.f - Dc); break;
	case EGeoVolcanoKind::Caldera:
		Up = (V.Chain >= 0 ? Dome(Dc) : Cone(Dc))
			+ 0.15f * H * FMath::Exp(-FMath::Square((D - C) / FMath::Max(0.15f * C, 1e-4f)));   // 外环壁
		break;
	default: break;
	}
	if (C > 0.f && D < C)
	{
		const float T = D / C;
		const float Depth = V.CraterDepth * (1.f - 0.7f * Wear);
		// 破火山口是平底洼地；普通火口是碗。
		OutCrater = V.Kind == EGeoVolcanoKind::Caldera ? Depth * (1.f - FMath::SmoothStep(0.75f, 1.f, T)) : Depth * (1.f - T * T);
	}
	return Up;
}

void FStage04Magmatism::Bake(FPlanetBakeContext& Ctx) const
{
	FPlanetBakeFields& F = Ctx.Fields;
	if (F.Elevation.Res <= 0 || F.PlateId.Res <= 0 || F.ReliefType.Res <= 0)
	{
		Ctx.Report.Errors.Add(TEXT("[地质] 阶段 4：缺阶段 2/3 的高程、板块或地形场"));
		return;
	}
	Stage04::PlaceVolcanoes(Ctx.Config, F, F.Volcanoes);

	const FPlanetRecipeConfig& Cfg = Ctx.Config;
	const EParallelForFlags PF = Cfg.bSingleThreadBake ? EParallelForFlags::ForceSingleThread : EParallelForFlags::None;
	const int32 Res = F.Elevation.Res, MacroRes = F.PlateId.Res;
	const uint64 NoiseSeed = FGeoRng::Derive(Cfg.Seed, 4, GeoHash::Str("relief")).NextU64();

	// 每个宏观 texel 记下可能影响它的火山：逐 texel 遍历全部火山在 1024² 下太慢。
	TArray<TArray<int32>> Buckets; Buckets.SetNum(6 * MacroRes * MacroRes);
	// 方向只算一次：按火山逐个重算是 V×393k 次 normalize，这里只付 393k 次。
	TArray<FVector3f> MacroDirs; MacroDirs.SetNum(6 * MacroRes * MacroRes);
	for (int32 Face = 0; Face < 6; ++Face) for (int32 Y = 0; Y < MacroRes; ++Y) for (int32 X = 0; X < MacroRes; ++X)
	{
		MacroDirs[(Face * MacroRes + Y) * MacroRes + X] = F.PlateId.TexelDir(Face, X, Y);
	}
	const float MacroTexel = 2.f / float(MacroRes) * 1.5f;   // 半个对角线有余
	for (int32 i = 0; i < F.Volcanoes.Num(); ++i)
	{
		const FGeoVolcano& Vo = F.Volcanoes[i];
		const float Reach = Vo.BaseRadius * 1.2f + MacroTexel;   // 1.2：外环壁与噪声余量
		for (int32 T = 0; T < MacroDirs.Num(); ++T)
		{
			if ((MacroDirs[T] - Vo.Dir).Size() < Reach) { Buckets[T].Add(i); }
		}
	}
	const auto BucketOf = [&](const FVector3f& D) -> const TArray<int32>&
	{
		int32 Fc; float U, Vv; FGeoField2D::DirToFace(D, Fc, U, Vv);
		const int32 X = FMath::Clamp(int32((U + 1.f) * 0.5f * MacroRes), 0, MacroRes - 1), Y = FMath::Clamp(int32((Vv + 1.f) * 0.5f * MacroRes), 0, MacroRes - 1);
		return Buckets[(Fc * MacroRes + Y) * MacroRes + X];
	};
	// 火山之间取 max 再与地形取 max：相邻锥不叠出双倍高，山高过锥顶时锥被埋。
	// Height 从 BaseElevation 起算（见 GeoVolcano.h），锥体不随局部地形倾斜——
	// 否则陡坡上坡下的地面比火口底还高，火口不成其为火口。
	const auto Eval = [&](const FVector3f& D, float E0, float& OutUp, float& OutPit, int32& OutTop, float& OutMask, float& OutSurface)
	{
		OutUp = 0.f; OutPit = 0.f; OutTop = -1; OutMask = 0.f; OutSurface = -TNumericLimits<float>::Max();
		for (const int32 i : BucketOf(D))
		{
			const FGeoVolcano& Vo = F.Volcanoes[i];
			const float Chord = (D - Vo.Dir).Size();
			float Pit;
			float Up = Stage04::Profile(Vo, Chord, Pit);
			if (Up <= 0.f && Pit <= 0.f) { continue; }
			// 沟谷用的是各向同性 Value3 噪声；规格里的放射状沟没实现（实施期漂移）。火口附近不加，
			// 免得噪声盖过火口深度。
			const float Near = FMath::SmoothStep(Vo.CraterRadius, 2.f * Vo.CraterRadius + 1e-5f, Chord);
			const FVector3f Q = D * (1.f / FMath::Max(Vo.BaseRadius, 1e-4f));
			const float Nz = GeoNoise::Fbm3(GeoHash::Combine(NoiseSeed, uint64(i)), Q * 1.5f, 2);
			const float Gully = 1.f - 0.25f * FMath::Square(GeoNoise::Value3(GeoHash::Combine(NoiseSeed, uint64(i) + 7777), Q * 6.f));
			Up *= 1.f + Near * (0.15f * Nz - (1.f - Gully));
			const float Mask = 1.f - Chord / Vo.BaseRadius;
			if (Mask > OutMask) { OutMask = Mask; OutTop = i; }
			OutUp = FMath::Max(OutUp, Up);
			OutPit = FMath::Max(OutPit, Pit);
			// 只在外圈把基面过渡回地形：否则下坡侧留下基面平台，足印边界是一道 1 texel 断崖。
			// 过渡带要宽：山高 0.05R，火山中心与足印边缘的高差可达上千体素，窄带会把台阶留到足印内部。
			const float Skirt = FMath::SmoothStep(0.65f, 1.f, Chord / Vo.BaseRadius);
			OutSurface = FMath::Max(OutSurface, FMath::Lerp(Vo.BaseElevation + Up, E0, Skirt));
		}
	};

	FGeoField2D& H = F.Elevation;
	ParallelFor(6 * Res, [&](int32 Row)
	{
		const int32 Face = Row / Res, Y = Row % Res;
		for (int32 X = 0; X < Res; ++X)
		{
			const float E0 = H.At(Face, X, Y);
			float Up, Pit, Mask, Surface; int32 Top;
			Eval(H.TexelDir(Face, X, Y), E0, Up, Pit, Top, Mask, Surface);
			if (Top < 0) { continue; }
			float E = FMath::Max(E0, Surface) - Pit;
			// 只有洋底火山能改海岸线；陆上火口挖不穿海平面。
			if (E0 > 0.f) { E = FMath::Max(E, FMath::Min(E0, 1.f)); }
			H.At(Face, X, Y) = E;
		}
	}, PF);

	// 统计量要重算：阶段 3 的上下界被火山改了，快照值域靠它。
	float MinE = TNumericLimits<float>::Max(), MaxE = -TNumericLimits<float>::Max();
	double LandW = 0.0, TotalW = 0.0;
	for (int32 Face = 0; Face < 6; ++Face) for (int32 Y = 0; Y < Res; ++Y) for (int32 X = 0; X < Res; ++X)
	{
		const float E = H.At(Face, X, Y);
		MinE = FMath::Min(MinE, E); MaxE = FMath::Max(MaxE, E);
		const double W = GeoContinentField::TexelWeight(H, X, Y);
		TotalW += W; LandW += E > 0.f ? W : 0.0;
	}
	F.MinElevation = MinE; F.MaxElevation = MaxE;
	F.LandAreaFrac = float(LandW / FMath::Max(TotalW, 1e-30));

	// 宏观场只喂调试图层：ExportDebugFields 仅在 bKeepDebugFields 时被调用，运行时烘焙不必付这份钱。
	if (Cfg.bKeepDebugFields)
	{
		F.VolcanoMask.Init(MacroRes); F.VolcanicType.Init(MacroRes); F.VolcanicAge.Init(MacroRes);
		F.VolcanoActive.Init(MacroRes); F.VolcanoDelta.Init(MacroRes);
		ParallelFor(6 * MacroRes, [&](int32 Row)
		{
			const int32 Face = Row / MacroRes, Y = Row % MacroRes;
			for (int32 X = 0; X < MacroRes; ++X)
			{
				const FVector3f Dir = F.VolcanoMask.TexelDir(Face, X, Y);
				float Up, Pit, Mask, Surface; int32 Top;
				// 宏观图层只用 Mask/Up/Pit，Surface 被丢弃：基面 E0 不必采样。
				Eval(Dir, 0.f, Up, Pit, Top, Mask, Surface);
				const FGeoVolcano* Vo = Top >= 0 ? &F.Volcanoes[Top] : nullptr;
				F.VolcanoMask.At(Face, X, Y) = Mask;
				F.VolcanicType.At(Face, X, Y) = Vo ? float(uint8(Vo->Kind)) : 0.f;
				F.VolcanicAge.At(Face, X, Y) = Vo ? Vo->Age : 0.f;
				F.VolcanoActive.At(Face, X, Y) = Vo && Vo->bActive ? 1.f : 0.f;
				F.VolcanoDelta.At(Face, X, Y) = Up - Pit;
			}
		}, PF);
	}

	// 4b 读的是叠完火山的最终高程：侵入体顶深要相对真实地表量，否则弧下的岩株会埋在锥里。
	// 阈值无条件写：关掉蚀变时没有晕原语，阈值不影响任何求值。
	Stage04b::PlaceIntrusions(Cfg, F, F.Primitives);
	F.AlterBands[0] = Cfg.Intrusion.AlterKeratiticBand;
	F.AlterBands[1] = Cfg.Intrusion.AlterSericiticBand;
	F.AlterBands[2] = Cfg.Intrusion.AlterArgillicBand;
}
namespace
{
	float Stage04DebugField(const FPlanetSnapshot& S, const TCHAR* Name, const FVector3d& P, bool bNearest)
	{
		const FGeoField2D* Fd = S.DebugFields.Find(FName(Name));
		if (!Fd || Fd->Res <= 0) { return FGeoDebugLayer::NoData(); }
		const FVector3f Dir(P.GetSafeNormal());
		return bNearest ? Fd->SampleNearest(Dir) : Fd->Sample(Dir);
	}
}

void FStage04Magmatism::DescribeDebugLayers(TArray<FGeoDebugLayer>& Out) const
{
	{
		FGeoDebugLayer L;
		L.Id = TEXT("VolcanicType"); L.DisplayName = TEXT("火山类型"); L.Stage = 4;
		L.Kind = EGeoDebugKind::Categorical;
		// 下标 = EGeoVolcanoKind。
		L.Legend = { { TEXT("无"), FColor(70, 70, 70) }, { TEXT("层状火山"), FColor(200, 50, 30) }, { TEXT("盾状火山"), FColor(230, 140, 40) },
			{ TEXT("溢流玄武岩"), FColor(90, 60, 90) }, { TEXT("破火山口"), FColor(250, 220, 60) }, { TEXT("火山渣锥"), FColor(140, 30, 30) } };
		L.Sample = [](const FPlanetSnapshot& S, const FVector3d& P) { return Stage04DebugField(S, TEXT("VolcanicType"), P, true); };
		Out.Add(MoveTemp(L));
	}
	{
		FGeoDebugLayer L;
		L.Id = TEXT("VolcanoActive"); L.DisplayName = TEXT("活火山"); L.Stage = 4;
		L.Kind = EGeoDebugKind::Categorical;
		L.Legend = { { TEXT("休眠 / 无"), FColor(70, 70, 70) }, { TEXT("活跃"), FColor(255, 60, 20) } };
		L.Sample = [](const FPlanetSnapshot& S, const FVector3d& P) { return Stage04DebugField(S, TEXT("VolcanoActive"), P, true); };
		Out.Add(MoveTemp(L));
	}
	{
		FGeoDebugLayer L;
		L.Id = TEXT("VolcanicAge"); L.DisplayName = TEXT("火山年龄"); L.Stage = 4;
		L.Unit = TEXT("0 新 … 1 老");
		L.Sample = [](const FPlanetSnapshot& S, const FVector3d& P) { return Stage04DebugField(S, TEXT("VolcanicAge"), P, true); };
		L.Range = [](const FPlanetSnapshot&) { return FVector2f(0.f, 1.f); };
		Out.Add(MoveTemp(L));
	}
	{
		FGeoDebugLayer L;
		L.Id = TEXT("VolcanoDelta"); L.DisplayName = TEXT("火山锥厚度（相对锥底）"); L.Stage = 4;
		L.Unit = TEXT("体素");
		// 相对各自锥底 BaseElevation 的厚度，不是相对阶段 3 地面的净抬升。
		L.Sample = [](const FPlanetSnapshot& S, const FVector3d& P) { return Stage04DebugField(S, TEXT("VolcanoDelta"), P, false); };
		L.Range = [](const FPlanetSnapshot& S) { return FVector2f(float(-0.01 * S.R0), float(0.015 * S.R0)); };
		Out.Add(MoveTemp(L));
	}
	{
		FGeoDebugLayer L;
		L.Id = TEXT("IntrusionType"); L.DisplayName = TEXT("侵入体类型（地下）"); L.Stage = 4;
		L.Domain = EGeoDebugDomain::Volume;
		L.Kind = EGeoDebugKind::Categorical;
		// 岩性 Id 9..14 中间有橄榄岩空位，直接当下标会留一个黑洞，所以图例改用紧凑编号。
		L.Legend = { { TEXT("无"), FColor(70, 70, 70) },
			{ TEXT("花岗岩（岩基）"), GeoLithology::DebugColor(GeoLithology::Granite) },
			{ TEXT("闪长岩（岩株）"), GeoLithology::DebugColor(GeoLithology::Diorite) },
			{ TEXT("辉长岩（岩墙 / 岩床）"), GeoLithology::DebugColor(GeoLithology::Gabbro) },
			{ TEXT("金伯利岩"), GeoLithology::DebugColor(GeoLithology::Kimberlite) },
			{ TEXT("层状镁铁岩"), GeoLithology::DebugColor(GeoLithology::LayeredMafic) },
			{ TEXT("碳酸岩"), GeoLithology::DebugColor(GeoLithology::Carbonatite) } };
		// 只读快照原语：剖面与 3D 步进都走 Sample，不需要宏观调试场。
		L.Sample = [](const FPlanetSnapshot& S, const FVector3d& P)
		{
			if (S.Primitives.IsEmpty() || S.IsTooDeepForPrimitives(P)) { return 0.f; }
			const FVector3f Pf(P);
			float Best = TNumericLimits<float>::Max(); uint8 Litho = 0;
			const TArray<FGeoPrimitive>& Prims = S.Primitives.GetPrimitives();
			S.Primitives.QueryPoint(Pf, [&](int32 i)
			{
				const FGeoPrimitive& Pr = Prims[i];
				if (Pr.LithologyId < GeoLithology::Granite) { return; }
				const float D = GeoPrimitiveDistance(Pr, Pf);
				// BVH 只保证点落在 AABB 里：旋转椭球的盒角在椭球外，不夹 D<1 会画出体外岩性。
				if (D >= 1.f) { return; }
				if (D < Best) { Best = D; Litho = Pr.LithologyId; }
			});
			return float(Stage04CompactLitho(Litho));
		};
		// 与矿体同病：岩墙 / 岩床只有 ~160 体素，3D 步进步长 200–400 会把它们跨过去，必须解析求交。
		L.DefaultHidden = { 0 };
		L.RayHits = [](const FPlanetSnapshot& S, const FVector3d& O, const FVector3d& D, TArray<TPair<double, float>>& Hits)
		{
			for (const FGeoPrimitive& Pr : S.Primitives.GetPrimitives())
			{
				if (Pr.LithologyId < GeoLithology::Granite) { continue; }
				// 先过包围球：绝大多数岩体不在这条光线上，省一次二次方程。
				const FVector3d OC = FVector3d(Pr.Center) - O;
				const double Rs = double(Pr.Radii.GetMax());
				const double Proj = FVector3d::DotProduct(OC, D);
				if (Proj < -Rs) { continue; }
				if (OC.SizeSquared() - Proj * Proj > Rs * Rs) { continue; }
				const FQuat4d Rot(Pr.Kind == EGeoPrimitiveKind::OrientedBlob ? Pr.Rotation : FQuat4f::Identity);
				const FVector3d Radii(Pr.Radii);
				const FVector3d Lo = Rot.UnrotateVector(O - FVector3d(Pr.Center)) / Radii;
				const FVector3d Ld = Rot.UnrotateVector(D) / Radii;
				const double A = Ld.SizeSquared(), B = 2.0 * FVector3d::DotProduct(Lo, Ld), C = Lo.SizeSquared() - 1.0;
				const double Disc = B * B - 4.0 * A * C;
				if (Disc < 0.0 || A <= 0.0) { continue; }
				const double T = (-B - FMath::Sqrt(Disc)) / (2.0 * A);
				if (T > 0.0) { Hits.Emplace(T, float(Stage04CompactLitho(Pr.LithologyId))); }
			}
		};
		Out.Add(MoveTemp(L));
	}
	{
		FGeoDebugLayer L;
		L.Id = TEXT("AlterationClue"); L.DisplayName = TEXT("蚀变分带（地下）"); L.Stage = 4;
		L.Domain = EGeoDebugDomain::Volume;
		L.Kind = EGeoDebugKind::Categorical;
		// 「无」用中性灰：GeoClue::DebugColor(0) 是品红，会把整片未蚀变内部糊成一片。
		L.Legend = { { TEXT("无"), FColor(70, 70, 70) },
			{ FString(GeoClue::DisplayName(GeoClue::Keratitic)), GeoClue::DebugColor(GeoClue::Keratitic) },
			{ FString(GeoClue::DisplayName(GeoClue::Sericitic)), GeoClue::DebugColor(GeoClue::Sericitic) },
			{ FString(GeoClue::DisplayName(GeoClue::Argillic)), GeoClue::DebugColor(GeoClue::Argillic) },
			{ FString(GeoClue::DisplayName(GeoClue::Propylitic)), GeoClue::DebugColor(GeoClue::Propylitic) } };
		// 与体素材质走同一条求值路径：查看器看到的分带就是挖下去会遇到的。
		L.Sample = [](const FPlanetSnapshot& S, const FVector3d& P) { return S.IsTooDeepForPrimitives(P) ? 0.f : float(PlanetMaterialEval::Sample(S, P).Clue); };
		Out.Add(MoveTemp(L));
	}
	{
		FGeoDebugLayer L;
		L.Id = TEXT("AlterationStrength"); L.DisplayName = TEXT("蚀变强度（地下）"); L.Stage = 4;
		L.Domain = EGeoDebugDomain::Volume;
		L.Min = 0.f; L.Max = 15.f;
		L.Sample = [](const FPlanetSnapshot& S, const FVector3d& P) { return S.IsTooDeepForPrimitives(P) ? 0.f : float(PlanetMaterialEval::Sample(S, P).ClueStrength); };
		Out.Add(MoveTemp(L));
	}
}

void FStage04Magmatism::ExportDebugFields(const FPlanetBakeFields& Fields, TMap<FName, FGeoField2D>& Out) const
{
	Out.Add(TEXT("VolcanicType"), Fields.VolcanicType);
	Out.Add(TEXT("VolcanoActive"), Fields.VolcanoActive);
	Out.Add(TEXT("VolcanicAge"), Fields.VolcanicAge);
	Out.Add(TEXT("VolcanoDelta"), Fields.VolcanoDelta);
}
