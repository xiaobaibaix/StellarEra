// 阶段 4b：地下侵入体。六类岩体都是原语，几何由快照与材质求值消费；这里只管摆在哪、多大。
#include "Stage04bIntrusions.h"
#include "GeoLithology.h"

namespace
{
	/** 顶深 + 竖直半厚的中心：岩体沿 dir 摆，跟着球面走。TopDepth/HalfHeight 占 R。 */
	FVector3f Stage04bCenter(const FPlanetRecipeConfig& C, const FPlanetBakeFields& F, const FVector3f& Dir, float TopDepth, float HalfHeight)
	{
		const double S = C.Radius + double(F.Elevation.Sample(Dir));
		const double Rc = S - double((TopDepth + HalfHeight) * float(C.Radius));
		return FVector3f(FVector3d(Dir) * Rc);
	}
	/** 局部 X 沿 Along、Z 沿 Up：椭圆长轴贴走向，短轴贴球面切向。 */
	FQuat4f Stage04bFrame(const FVector3f& Up, const FVector3f& Along)
	{
		FVector3f X = (Along - Up * FVector3f::DotProduct(Along, Up)).GetSafeNormal();
		if (X.IsNearlyZero())
		{
			const FVector3f Ref = FMath::Abs(Up.Z) < 0.9f ? FVector3f::UpVector : FVector3f::ForwardVector;
			X = FVector3f::CrossProduct(Ref, Up).GetSafeNormal();
		}
		const FVector3f Y = FVector3f::CrossProduct(Up, X);
		return FQuat4f(FMatrix44f(X, Y, Up, FVector3f::ZeroVector)).GetNormalized();
	}
	FGeoPrimitive Stage04bBody(const FVector3f& Center, const FVector3f& Radii, const FQuat4f& Rot, uint8 Litho, bool bOriented)
	{
		FGeoPrimitive P = bOriented ? FGeoPrimitive::MakeOrientedBlob(Center, Radii, Rot, 0.02f)
			: FGeoPrimitive::MakeBlob(Center, Radii, 0.02f);
		P.LithologyId = Litho;
		return P;
	}
	/** 宏观 texel 候选去重：与已收点保持 MinChord 以上，避免一片裂谷撒满。 */
	bool Stage04bFarFrom(const TArray<FVector3f>& L, const FVector3f& D, float MinChord)
	{
		for (const FVector3f& Q : L) { if ((Q - D).Size() < MinChord) { return false; } }
		return true;
	}
	/** 竖直岩体也要一个走向轴：任取一条切向即可，保证 Stage04bFrame 是纯旋转。 */
	FVector3f Stage04bTangent(const FVector3f& D)
	{
		const FVector3f Ref = FMath::Abs(D.Z) < 0.9f ? FVector3f::UpVector : FVector3f::ForwardVector;
		return FVector3f::CrossProduct(Ref, D).GetSafeNormal();
	}
}

void Stage04b::PlaceIntrusions(const FPlanetRecipeConfig& Cfg, const FPlanetBakeFields& F, TArray<FGeoPrimitive>& Out)
{
	// 缺阶段 2/3 的场就没有参照系：宁可不放，也不要摆到 R 的裸球面上。
	if (F.PlateId.Res <= 0 || F.Elevation.Res <= 0 || F.ReliefType.Res <= 0 || F.MountainMask.Res <= 0) { return; }
	const FPlanetIntrusionParams& P = Cfg.Intrusion;
	const float R = float(Cfg.Radius);
	const int32 MacroRes = F.PlateId.Res;

	// —— 岩基：同一俯冲对的弧火山先聚成段，段质心下方才是大岩体 ——
	if (P.bBatholiths)
	{
		struct FArcGroup { int32 Plate = -1, Other = -1; TArray<int32> Idx; };
		TArray<FArcGroup> Groups;
		for (int32 i = 0; i < F.Volcanoes.Num(); ++i)
		{
			const FGeoVolcano& V = F.Volcanoes[i];
			if (V.OtherPlate < 0) { continue; }
			FArcGroup* G = Groups.FindByPredicate([&V](const FArcGroup& X) { return X.Plate == V.Plate && X.Other == V.OtherPlate; });
			if (!G) { G = &Groups.AddDefaulted_GetRef(); G->Plate = V.Plate; G->Other = V.OtherPlate; }
			G->Idx.Add(i);
		}
		const float Link = 2.f * Cfg.Volcano.ArcSpacingRatio;
		for (const FArcGroup& G : Groups)
		{
			// 桶内按数组顺序贪心串段：段内任一座够近就并入，否则另起一段。
			TArray<TArray<int32>> Segs;
			for (const int32 i : G.Idx)
			{
				const FVector3f D = F.Volcanoes[i].Dir;
				TArray<int32>* Hit = nullptr;
				for (TArray<int32>& S : Segs)
				{
					for (const int32 j : S)
					{
						if ((F.Volcanoes[j].Dir - D).Size() < Link) { Hit = &S; break; }
					}
					if (Hit) { break; }
				}
				if (Hit) { Hit->Add(i); } else { Segs.Add({ i }); }
			}
			for (const TArray<int32>& S : Segs)
			{
				if (S.Num() < P.BatholithMinVolcanoes) { continue; }
				FVector3f Sum = FVector3f::ZeroVector;
				for (const int32 i : S) { Sum += F.Volcanoes[i].Dir; }
				FVector3f Dir = Sum.GetSafeNormal();
				// 弧绕球一圈时质心会落到原点：退回首座的方向，别把岩基摆到球心。
				if (Dir.IsNearlyZero()) { Dir = F.Volcanoes[S[0]].Dir; }
				const FVector3f Along = F.Volcanoes[S.Last()].Dir - F.Volcanoes[S[0]].Dir;
				const FVector3f Radii(P.BatholithLongRatio * R, P.BatholithShortRatio * R, P.BatholithThickRatio * R);
				Out.Add(Stage04bBody(Stage04bCenter(Cfg, F, Dir, P.BatholithTopDepthRatio, P.BatholithThickRatio), Radii,
					Stage04bFrame(Dir, Along), GeoLithology::Granite, true));
			}
		}
	}

	// —— 岩株：每座弧火山正下方一个，是 4c 斑岩铜的宿主 ——
	if (P.bStocks)
	{
		// 每类各从种子直派生一条流（不串在一根总流上）：关掉一类不会搬动别类的随机数。
		FGeoRng Rng = FGeoRng::Derive(Cfg.Seed, 4, GeoHash::Str("stock"));
		for (const FGeoVolcano& V : F.Volcanoes)
		{
			if (V.OtherPlate < 0) { continue; }
			// 每座都掷一次（只取尺寸抖动）：不按段筛选，随机流消耗与岩基分段无关。
			const float Rad = P.StockRadiusRatio * Rng.Range(0.7f, 1.3f);
			Out.Add(Stage04bBody(Stage04bCenter(Cfg, F, V.Dir, P.StockTopDepthRatio, Rad), FVector3f(Rad * R),
				FQuat4f::Identity, GeoLithology::Diorite, false));
		}
	}

	// —— 岩墙：裂谷宏观 texel 聚成段，每段几条竖直薄板 ——
	if (P.bDikes && P.DikePerRift > 0)
	{
		TArray<FVector3f> Rifts;
		for (int32 Face = 0; Face < 6; ++Face)
		for (int32 Y = 0; Y < MacroRes; ++Y)
		for (int32 X = 0; X < MacroRes; ++X)
		{
			if (F.ReliefType.At(Face, X, Y) != 6.f) { continue; }   // 6 = 大陆裂谷
			const FVector3f D = F.ReliefType.TexelDir(Face, X, Y);
			if (Stage04bFarFrom(Rifts, D, 0.1f)) { Rifts.Add(D); }
		}
		for (const FVector3f& D : Rifts)
		{
			const FVector3f T = Stage04bTangent(D);
			for (int32 k = 0; k < P.DikePerRift; ++k)
			{
				// ±0.01 切向错开：一条裂谷里的两条岩墙不能叠成同一条。
				const FVector3f Axis = (D + T * ((float(k) - 0.5f * float(P.DikePerRift - 1)) * 0.01f)).GetSafeNormal();
				const FVector3f Radii(P.DikeThicknessRatio * R, P.DikeThicknessRatio * R, P.DikeLengthRatio * R);
				Out.Add(Stage04bBody(Stage04bCenter(Cfg, F, Axis, P.DikeTopDepthRatio, P.DikeLengthRatio), Radii,
					Stage04bFrame(Axis, T), GeoLithology::Gabbro, true));
			}
		}
	}

	// 克拉通候选：岩床与层状侵入体共用一次 texel 扫描，各自去重（弦长不同）。
	TArray<FVector3f> SillCand, LayeredCand;
	if (P.bSills || P.bLayeredIntrusions)
	{
		for (int32 Face = 0; Face < 6; ++Face)
		for (int32 Y = 0; Y < MacroRes; ++Y)
		for (int32 X = 0; X < MacroRes; ++X)
		{
			if (F.CrustAge.At(Face, X, Y) <= 0.85f || F.ContinentalMask.At(Face, X, Y) <= 0.6f) { continue; }
			const FVector3f D = F.CrustAge.TexelDir(Face, X, Y);
			if (F.Elevation.Sample(D) <= 0.f) { continue; }   // 陆上：水下没有克拉通
			if (P.bSills && Stage04bFarFrom(SillCand, D, 0.12f)) { SillCand.Add(D); }
			if (P.bLayeredIntrusions && Stage04bFarFrom(LayeredCand, D, 0.2f)) { LayeredCand.Add(D); }
		}
	}

	// —— 岩床：溢流玄武岩台地下必有一片，克拉通按密度再撒 ——
	if (P.bSills)
	{
		for (const FGeoVolcano& V : F.Volcanoes)
		{
			if (V.Kind != EGeoVolcanoKind::FloodBasalt) { continue; }
			const FVector3f Radii(P.SillRadiusRatio * R, P.SillRadiusRatio * R, P.SillThicknessRatio * R);
			Out.Add(Stage04bBody(Stage04bCenter(Cfg, F, V.Dir, P.SillTopDepthRatio, P.SillThicknessRatio), Radii,
				Stage04bFrame(V.Dir, Stage04bTangent(V.Dir)), GeoLithology::Gabbro, true));
		}
		FGeoRng Rng = FGeoRng::Derive(Cfg.Seed, 4, GeoHash::Str("sill"));
		for (const FVector3f& D : SillCand)
		{
			if (Rng.NextFloat() >= P.CratonSillDensity) { continue; }
			const FVector3f Radii(P.SillRadiusRatio * R, P.SillRadiusRatio * R, P.SillThicknessRatio * R);
			Out.Add(Stage04bBody(Stage04bCenter(Cfg, F, D, P.SillTopDepthRatio, P.SillThicknessRatio), Radii,
				Stage04bFrame(D, Stage04bTangent(D)), GeoLithology::Gabbro, true));
		}
	}

	// —— 层状侵入体：克拉通里稀疏撒，一组 LayeredCount 片水平叠置 ——
	if (P.bLayeredIntrusions && P.LayeredCount > 0)
	{
		FGeoRng Rng = FGeoRng::Derive(Cfg.Seed, 4, GeoHash::Str("layered"));
		const FVector3f Radii(P.LayeredRadiusRatio * R, P.LayeredRadiusRatio * R, P.LayeredThicknessRatio * R);
		for (const FVector3f& D : LayeredCand)
		{
			if (Rng.NextFloat() >= 0.15f) { continue; }
			const FQuat4f Rot = Stage04bFrame(D, Stage04bTangent(D));
			for (int32 i = 0; i < P.LayeredCount; ++i)
			{
				const float Top = P.LayeredTopDepthRatio + float(i) * P.LayeredGapRatio;
				Out.Add(Stage04bBody(Stage04bCenter(Cfg, F, D, Top, P.LayeredThicknessRatio), Radii, Rot, GeoLithology::LayeredMafic, true));
			}
		}
	}

	// —— 金伯利岩管：古老克拉通、远离山带，全局限量 ——
	if (P.bKimberlites && P.KimberliteMax > 0)
	{
		FGeoRng Rng = FGeoRng::Derive(Cfg.Seed, 4, GeoHash::Str("kimberlite"));
		TArray<FVector3f> Pipes;
		int32 Placed = 0;
		for (int32 Face = 0; Face < 6 && Placed < P.KimberliteMax; ++Face)
		for (int32 Y = 0; Y < MacroRes && Placed < P.KimberliteMax; ++Y)
		for (int32 X = 0; X < MacroRes && Placed < P.KimberliteMax; ++X)
		{
			if (F.CrustAge.At(Face, X, Y) <= 0.85f || F.ContinentalMask.At(Face, X, Y) <= 0.6f) { continue; }
			if (F.MountainMask.At(Face, X, Y) >= 0.2f) { continue; }
			const FVector3f D = F.CrustAge.TexelDir(Face, X, Y);
			if (F.Elevation.Sample(D) <= 0.f) { continue; }
			if (!Stage04bFarFrom(Pipes, D, 0.05f)) { continue; }
			Pipes.Add(D);
			if (Rng.NextFloat() >= 0.25f) { continue; }
			const FVector3f Radii(P.KimberliteRadiusRatio * R, P.KimberliteRadiusRatio * R, P.KimberliteHalfHeightRatio * R);
			Out.Add(Stage04bBody(Stage04bCenter(Cfg, F, D, P.KimberliteTopDepthRatio, P.KimberliteHalfHeightRatio), Radii,
				Stage04bFrame(D, Stage04bTangent(D)), GeoLithology::Kimberlite, true));
			++Placed;
		}
	}
}
