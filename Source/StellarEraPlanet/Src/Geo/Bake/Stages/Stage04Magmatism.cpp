#include "Stage04Magmatism.h"
#include "GeoContinentField.h"
#include "GeoNoise.h"
#include "GeoPlates.h"

namespace
{
	FVector3f Stage04Tangent(const FVector3f& D)
	{
		const FVector3f Ref = FMath::Abs(D.Z) < 0.9f ? FVector3f::UpVector : FVector3f::ForwardVector;
		return FVector3f::CrossProduct(Ref, D).GetSafeNormal();
	}
	/** 从 D 出发沿切向 T 走弦长 S：小角度下 normalize(D + T·S) 足够准。 */
	FVector3f Stage04Step(const FVector3f& D, const FVector3f& T, float S) { return (D + T * S).GetSafeNormal(); }

	bool Stage04FarFromAll(const TArray<FGeoVolcano>& L, const FVector3f& D, float MinChord)
	{
		for (const FGeoVolcano& V : L) { if ((V.Dir - D).Size() < MinChord) { return false; } }
		return true;
	}

	int32 Stage04PlateAt(const FGeoPlateFrame& Frame, const FPlanetPlateParams& P, const FVector3f& D)
	{
		int32 A, B; float Da, Db;
		GeoPlates::NearestTwo(Frame, GeoPlates::WarpDirection(Frame, D, P), A, B, Da, Db);
		return A;
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
			const float Gap = V.ArcSpacingRatio * (0.85f + 0.3f * float(Cd.Key >> 40) / 16777216.f);
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
float Stage04::Profile(const FGeoVolcano&, float, float& OutCrater) { OutCrater = 0.f; return 0.f; }

void FStage04Magmatism::Bake(FPlanetBakeContext& Ctx) const
{
	FPlanetBakeFields& F = Ctx.Fields;
	if (F.Elevation.Res <= 0 || F.PlateId.Res <= 0 || F.ReliefType.Res <= 0)
	{
		Ctx.Report.Errors.Add(TEXT("[地质] 阶段 4：缺阶段 2/3 的高程、板块或地形场"));
		return;
	}
	Stage04::PlaceVolcanoes(Ctx.Config, F, F.Volcanoes);
}
void FStage04Magmatism::DescribeDebugLayers(TArray<FGeoDebugLayer>&) const {}
void FStage04Magmatism::ExportDebugFields(const FPlanetBakeFields&, TMap<FName, FGeoField2D>&) const {}
