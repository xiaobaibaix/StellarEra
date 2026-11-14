#include "Stage03Terrain.h"
#include "Async/ParallelFor.h"
#include "GeoContinentField.h"
#include "GeoNoise.h"
#include "GeoPlates.h"
#include "PlanetSnapshot.h"

namespace
{
	// 下标 = BoundaryType：哪些边界长山。离散与转换几乎不长，否则 7 块板块长 7 圈山，整颗星像橘子。
	const float Stage03BeltWeight[6] = { 0.f, 0.8f, 0.5f, 1.f, 0.15f, 0.05f };

	void Stage03Tangents(const FVector3f& D, FVector3f& E1, FVector3f& E2)
	{
		const FVector3f Ref = FMath::Abs(D.Z) < 0.9f ? FVector3f::UpVector : FVector3f::ForwardVector;
		E1 = FVector3f::CrossProduct(Ref, D).GetSafeNormal();
		E2 = FVector3f::CrossProduct(D, E1);
	}

	/** 扭的是采样坐标不是高度：扭坐标出「山脊弯了」，扭高度只会把山抹糊。 */
	FVector3f Stage03Warp(uint64 Seed, const FVector3f& D, float Amp)
	{
		const FVector3f Q = D * (1.f / 0.3f);
		const FVector3f Off(GeoNoise::Fbm3(GeoHash::Combine(Seed, 1), Q, 2), GeoNoise::Fbm3(GeoHash::Combine(Seed, 2), Q, 2),
			GeoNoise::Fbm3(GeoHash::Combine(Seed, 3), Q, 2));
		return (D + Off * Amp).GetSafeNormal();
	}

	float Stage03Sq(float X) { return X * X; }

	struct FStage03Sample
	{
		float Mask = 0.f;
		float Relief = 0.f;
		float Trench = 0.f;
		float Wedge = 0.f;
	};
}

void FStage03Terrain::Bake(FPlanetBakeContext& Ctx) const
{
	const FPlanetRecipeConfig& Cfg = Ctx.Config;
	const FPlanetTerrainParams& T = Cfg.Terrain;
	FPlanetBakeFields& F = Ctx.Fields;
	if (F.Elevation.Res <= 0 || F.PlateId.Res <= 0 || F.ContinentalMask.Res <= 0 || F.PlateFrame.Num() == 0)
	{
		Ctx.Report.Errors.Add(TEXT("[地质] 阶段 3：缺阶段 2 的高程或板块场"));
		return;
	}
	const double R = Cfg.Radius;
	const EParallelForFlags PF = Cfg.bSingleThreadBake ? EParallelForFlags::ForceSingleThread : EParallelForFlags::None;
	const int32 Res = F.Elevation.Res, MacroRes = F.PlateId.Res;
	const FGeoPlateFrame& Frame = F.PlateFrame;
	FGeoRng Rng = Ctx.Rng;
	const uint64 WarpSeed = Rng.NextU64(), RidgeSeed = Rng.NextU64(), SegSeed = Rng.NextU64(), PlainSeed = Rng.NextU64();
	const uint64 TrenchSeed = Rng.NextU64();

	// 宏观带场：与阶段 2 同一个 warp，山带才和板块边界对齐。
	FGeoField2D BeltDist, BeltWeight, Subduct;
	BeltDist.Init(MacroRes); BeltWeight.Init(MacroRes); Subduct.Init(MacroRes);
	ParallelFor(6 * MacroRes, [&](int32 Row)
	{
		const int32 Face = Row / MacroRes, Y = Row % MacroRes;
		for (int32 X = 0; X < MacroRes; ++X)
		{
			const FVector3f W = GeoPlates::WarpDirection(Frame, BeltDist.TexelDir(Face, X, Y), Cfg.Plates);
			int32 Near, Second; float D1, D2;
			GeoPlates::NearestTwo(Frame, W, Near, Second, D1, D2);
			const uint8 Type = Frame.BoundaryTypeBetween(Near, Second);
			BeltDist.At(Face, X, Y) = 0.5f * (D2 - D1);
			// 只取「汇聚 / 离散 / 转换」：阶段 2 的洋陆分类按板块种类定，与大陆场无关；海陆改由本点大陆度判。
			const bool bConvergent = Type >= 1 && Type <= 3;
			BeltWeight.At(Face, X, Y) = bConvergent ? 1.f : (Type < 6 ? Stage03BeltWeight[Type] : 0.f);
			Subduct.At(Face, X, Y) = bConvergent ? 1.f : 0.f;
		}
	}, PF);

	const float BeltW = FMath::Max(T.BeltWidthRatio, 1e-4f), TrenchW = FMath::Max(T.TrenchWidthRatio, 1e-4f);
	const float AMtn = float(T.MountainHeightRatio * R), ATrench = float(T.TrenchDepthRatio * R), APlain = float(T.PlainReliefRatio * R);
	const float LandH = FMath::Max(float(Cfg.Continents.LandHeightRatio * R), 1.f);
	const float OceanD = float(Cfg.Continents.OceanDepthRatio * R);
	const float L0 = FMath::Max(T.RidgeWavelengthRatio, 1e-4f);
	// octave 数由分辨率定、不由半径定：最细一层波长至少 4 个 texel，再细是白算（也保证缩放不变）。
	const int32 Octaves = FMath::Clamp(1 + FMath::FloorToInt(FMath::Log2(L0 * float(Res) / 6.3f)), 1, 6);
	const float GradEps = 2.f / float(MacroRes);

	const FGeoField2D Base = F.Elevation;
	const auto Eval = [&](const FVector3f& Dir, float E0) -> FStage03Sample
	{
		FStage03Sample Out;
		const float B = BeltDist.Sample(Dir);
		const float M = F.ContinentalMask.Sample(Dir);
		const FVector3f Wp = Stage03Warp(WarpSeed, Dir, T.WarpRatio);
		const float Profile = FMath::Exp(-Stage03Sq(B / (0.5f * BeltW)));
		const float Wt = BeltWeight.Sample(Dir);
		if (Profile * Wt > 1e-4f)
		{
			// 沿走向分段：有垭口、有高段，不是一条等高的巧克力棒。
			const float Seg = 0.4f + 0.6f * FMath::Clamp(0.5f + 0.5f * GeoNoise::Fbm3(SegSeed, Wp * (1.f / (1.2f * BeltW)), 2), 0.f, 1.f);
			// 洋侧只留三成：洋陆俯冲的山长在陆侧，洋洋俯冲只出岛弧。
			Out.Mask = Wt * Profile * Seg * FMath::Lerp(0.3f, 1.f, FMath::SmoothStep(0.3f, 0.7f, M));
		}
		float Mountain = 0.f;
		if (Out.Mask > 1e-3f)
		{
			FVector3f E1, E2;
			Stage03Tangents(Dir, E1, E2);
			const float Gx = BeltDist.Sample((Dir + E1 * GradEps).GetSafeNormal()) - BeltDist.Sample((Dir - E1 * GradEps).GetSafeNormal());
			const float Gy = BeltDist.Sample((Dir + E2 * GradEps).GetSafeNormal()) - BeltDist.Sample((Dir - E2 * GradEps).GetSafeNormal());
			const FVector3f N = E1 * Gx + E2 * Gy;
			FVector3f Q = Wp;
			if (!N.IsNearlyZero(1e-8f))
			{
				// 走向 = 边界距离梯度的垂直方向；沿走向压缩 3 倍，山脊顺着边界拉长。
				const FVector3f S = FVector3f::CrossProduct(Dir, N.GetSafeNormal());
				Q = Wp - S * ((2.f / 3.f) * FVector3f::DotProduct(Wp, S));
			}
			float H = 0.f, Amp = 1.f, Norm = 0.f, Freq = 1.f / L0;
			for (int32 k = 0; k < Octaves; ++k)
			{
				// ridged = 1 − |n|，再平方让脊更尖、谷更宽；普通 fBm 圆润像沙丘。
				const float Ridge = 1.f - FMath::Abs(GeoNoise::Value3(GeoHash::Combine(RidgeSeed, uint64(k)), Q * Freq));
				H += Amp * Ridge * Ridge;
				Norm += Amp;
				Amp *= 0.5f;
				Freq *= 2.f;
			}
			Mountain = Out.Mask * AMtn * H / FMath::Max(Norm, 1e-6f);
		}
		// 平原起伏只抬不降、靠海岸淡出：降了会把海岸线挪走，阶段 2 的陆地占比就失效了。
		float Plain = 0.f;
		if (E0 > 0.f && APlain > 0.f)
		{
			Plain = APlain * (0.5f + 0.5f * GeoNoise::Fbm3(PlainSeed, Wp * 20.f, 3)) * FMath::Clamp(E0 / (0.25f * LandH), 0.f, 1.f);
		}
		const float Sub = Subduct.Sample(Dir);
		if (Sub > 1e-3f && E0 < 0.f)
		{
			// 沿走向分段：有深段有浅段、有宽有窄，不是一条等深的笔画。
			const FVector3f TQ = Wp * (1.f / (6.f * TrenchW));
			const float TDepth = 0.35f + 0.65f * FMath::Clamp(0.5f + 0.5f * GeoNoise::Fbm3(TrenchSeed, TQ, 3), 0.f, 1.f);
			const float TWidth = 0.7f + 0.6f * FMath::Clamp(0.5f + 0.5f * GeoNoise::Fbm3(GeoHash::Combine(TrenchSeed, 7), TQ, 3), 0.f, 1.f);
			// 横断面不对称：向陆一侧陡、向洋一侧缓。离边界越远大陆度越高 = 陆侧（两梯度同向）。
			FVector3f E1, E2;
			Stage03Tangents(Dir, E1, E2);
			const auto Grad = [&](const FGeoField2D& Fd)
			{
				return FVector2f(Fd.Sample((Dir + E1 * GradEps).GetSafeNormal()) - Fd.Sample((Dir - E1 * GradEps).GetSafeNormal()),
					Fd.Sample((Dir + E2 * GradEps).GetSafeNormal()) - Fd.Sample((Dir - E2 * GradEps).GetSafeNormal()));
			};
			const FVector2f GB = Grad(BeltDist), GM = Grad(F.ContinentalMask);
			const float Cos = FVector2f::DotProduct(GB, GM) / FMath::Max(GB.Size() * GM.Size(), 1e-8f);
			const float Side = FMath::Lerp(1.4f, 0.6f, FMath::SmoothStep(-0.3f, 0.3f, Cos));
			const float Raw = ATrench * Sub * TDepth * FMath::Exp(-Stage03Sq(B / (0.5f * TrenchW * TWidth * Side)))
				* (1.f - FMath::SmoothStep(0.1f, 0.5f, M));
			// 沉积充填：洋底越浅（越靠陆架 / 陆坡）泥沙来源越近、填得越多；深海平原段几乎不填。
			// 阶段 6 有了真实入海泥沙后应改读它。
			const float Near = 1.f - FMath::SmoothStep(0.55f, 0.95f, -E0 / FMath::Max(OceanD, 1.f));
			Out.Trench = Raw * (1.f - FMath::Clamp(T.TrenchSedimentFill, 0.f, 1.f) * Near);
			// 海沟带里不许留洋侧山体：原来靠海沟深度抵消，填浅后山体会把洋底顶出海面。
			Mountain *= 1.f - FMath::Clamp(Raw / FMath::Max(0.3f * ATrench, 1.f), 0.f, 1.f);
			// 增生楔：陆侧离边界约 1.5 个海沟宽处的低缓隆起；封顶在本地水深的三成，保证始终在水下。
			if (T.TrenchWedge > 0.f && Cos > 0.f)
			{
				const float Wb = (FMath::Abs(B) - 1.5f * TrenchW) / TrenchW;
				const float Bump = 0.15f * ATrench * T.TrenchWedge * Sub * TDepth * FMath::Exp(-Wb * Wb) * FMath::Clamp(Cos, 0.f, 1.f);
				Out.Wedge = FMath::Min(Bump, 0.3f * (-E0));
			}
		}
		Out.Relief = Mountain + Plain - Out.Trench + Out.Wedge;
		return Out;
	};

	FGeoField2D Raw, TrenchAmt;
	Raw.Init(Res);
	TrenchAmt.Init(Res);
	ParallelFor(6 * Res, [&](int32 Row)
	{
		const int32 Face = Row / Res, Y = Row % Res;
		for (int32 X = 0; X < Res; ++X)
		{
			const FStage03Sample Sm = Eval(Raw.TexelDir(Face, X, Y), Base.At(Face, X, Y));
			Raw.At(Face, X, Y) = Sm.Relief;
			TrenchAmt.At(Face, X, Y) = Sm.Trench;
		}
	}, PF);

	FGeoField2D H;
	H.Init(Res);
	float RawMax = 0.f;
	for (int32 Face = 0; Face < 6; ++Face)
	for (int32 Y = 0; Y < Res; ++Y)
	for (int32 X = 0; X < Res; ++X)
	{
		H.At(Face, X, Y) = Base.At(Face, X, Y) + Raw.At(Face, X, Y);
		RawMax = FMath::Max(RawMax, Raw.At(Face, X, Y));
	}

	// 热力侵蚀：只在陡于休止角处向邻域均值扩散，不碰水（流水要降水，放这里会与阶段 5 成环）。
	const float Talus = FMath::Tan(FMath::DegreesToRadians(FMath::Clamp(T.TalusAngleDeg, 1.f, 89.f)));
	const float Lambda = FMath::Clamp(T.ErosionStrength, 0.f, 1.f);
	const float Du = 2.f / float(Res);
	for (int32 It = 0; It < FMath::Clamp(T.ErosionIterations, 0, 50); ++It)
	{
		const FGeoField2D Src = H;
		ParallelFor(6 * Res, [&](int32 Row)
		{
			const int32 Face = Row / Res, Y = Row % Res;
			const float V = (float(Y) + 0.5f) / float(Res) * 2.f - 1.f;
			for (int32 X = 0; X < Res; ++X)
			{
				const float U = (float(X) + 0.5f) / float(Res) * 2.f - 1.f;
				const FVector3f D0 = FGeoField2D::FaceToDir(Face, U, V);
				const FVector3f Dn = FGeoField2D::FaceToDir(Face, U + Du, V);
				const float N[4] = { Src.Sample(Dn), Src.Sample(FGeoField2D::FaceToDir(Face, U - Du, V)),
					Src.Sample(FGeoField2D::FaceToDir(Face, U, V + Du)), Src.Sample(FGeoField2D::FaceToDir(Face, U, V - Du)) };
				const float Hc = Src.At(Face, X, Y);
				float Mean = 0.f, MaxDiff = 0.f;
				for (const float Nv : N) { Mean += 0.25f * Nv; MaxDiff = FMath::Max(MaxDiff, FMath::Abs(Nv - Hc)); }
				const float Arc = float(R) * FMath::Max((Dn - D0).Size(), 1e-9f);
				float Out = MaxDiff / Arc > Talus ? Hc + Lambda * (Mean - Hc) : Hc;
				// 保号：侵蚀不许把海岸线推过海平面。
				const float Orig = Base.At(Face, X, Y) + Raw.At(Face, X, Y);
				Out = Orig > 0.f ? FMath::Max(Out, 0.01f) : FMath::Min(Out, 0.f);
				H.At(Face, X, Y) = Out;
			}
		}, PF);
	}

	// 海沟坍塌：只在海沟带内、用更低的休止角多扩散几遍，两壁削缓成宽浅槽。扩散结果落在邻居最小与最大值之间，
	// 海沟底不会被抬过周围洋底；再钳到 ≤ 0 保证始终在水下。
	const int32 SlumpIters = FMath::RoundToInt(FMath::Clamp(T.TrenchSlump, 0.f, 1.f) * 12.f);
	const float SlumpTalus = FMath::Tan(FMath::DegreesToRadians(8.f));
	const float SlumpZone = 0.05f * FMath::Max(ATrench, 1.f);
	for (int32 It = 0; It < SlumpIters; ++It)
	{
		const FGeoField2D Src = H;
		ParallelFor(6 * Res, [&](int32 Row)
		{
			const int32 Face = Row / Res, Y = Row % Res;
			const float V = (float(Y) + 0.5f) / float(Res) * 2.f - 1.f;
			for (int32 X = 0; X < Res; ++X)
			{
				if (TrenchAmt.At(Face, X, Y) < SlumpZone) { continue; }
				const float U = (float(X) + 0.5f) / float(Res) * 2.f - 1.f;
				const FVector3f D0 = FGeoField2D::FaceToDir(Face, U, V);
				const FVector3f Dn = FGeoField2D::FaceToDir(Face, U + Du, V);
				const float N[4] = { Src.Sample(Dn), Src.Sample(FGeoField2D::FaceToDir(Face, U - Du, V)),
					Src.Sample(FGeoField2D::FaceToDir(Face, U, V + Du)), Src.Sample(FGeoField2D::FaceToDir(Face, U, V - Du)) };
				const float Hc = Src.At(Face, X, Y);
				float Mean = 0.f, MaxDiff = 0.f;
				for (const float Nv : N) { Mean += 0.25f * Nv; MaxDiff = FMath::Max(MaxDiff, FMath::Abs(Nv - Hc)); }
				const float Arc = float(R) * FMath::Max((Dn - D0).Size(), 1e-9f);
				if (MaxDiff / Arc > SlumpTalus) { H.At(Face, X, Y) = FMath::Min(Hc + 0.35f * (Mean - Hc), 0.f); }
			}
		}, PF);
	}

	float DeltaMax = 0.f, MinE = TNumericLimits<float>::Max(), MaxE = -TNumericLimits<float>::Max();
	double LandW = 0.0, TotalW = 0.0;
	for (int32 Face = 0; Face < 6; ++Face)
	for (int32 Y = 0; Y < Res; ++Y)
	for (int32 X = 0; X < Res; ++X)
	{
		const float E = H.At(Face, X, Y);
		DeltaMax = FMath::Max(DeltaMax, E - Base.At(Face, X, Y));
		MinE = FMath::Min(MinE, E);
		MaxE = FMath::Max(MaxE, E);
		const double Wt = GeoContinentField::TexelWeight(H, X, Y);
		TotalW += Wt;
		LandW += E > 0.f ? Wt : 0.0;
	}
	F.Elevation = MoveTemp(H);
	F.MinElevation = MinE;
	F.MaxElevation = MaxE;
	F.LandAreaFrac = float(LandW / FMath::Max(TotalW, 1e-30));
	F.ReliefRawMax = RawMax;
	F.ReliefDeltaMax = DeltaMax;

	// 宏观输出：给阶段 4/6/7 判火山弧、剥蚀强度、盆地。类型由高程与遮蔽联合判，不另起一套分类。
	F.MountainMask.Init(MacroRes); F.ReliefDelta.Init(MacroRes); F.ReliefType.Init(MacroRes);
	const float CrustSplit = F.CrustSplitElevation;
	ParallelFor(6 * MacroRes, [&](int32 Row)
	{
		const int32 Face = Row / MacroRes, Y = Row % MacroRes;
		for (int32 X = 0; X < MacroRes; ++X)
		{
			const FVector3f Dir = F.MountainMask.TexelDir(Face, X, Y);
			const float E0 = Base.Sample(Dir), E = F.Elevation.Sample(Dir);
			const FStage03Sample S = Eval(Dir, E0);
			const float Delta = E - E0;
			float Type = 2.f;
			if (E < CrustSplit) { Type = S.Trench > 0.3f * ATrench ? 5.f : 0.f; }
			else if (E <= 0.f) { Type = 1.f; }
			else if (S.Mask > 0.3f && Delta > 0.25f * AMtn) { Type = 4.f; }
			else if (Delta > FMath::Max(0.05f * AMtn, 2.f * APlain)) { Type = 3.f; }
			F.MountainMask.At(Face, X, Y) = S.Mask;
			F.ReliefDelta.At(Face, X, Y) = Delta;
			F.ReliefType.At(Face, X, Y) = Type;
		}
	}, PF);
}

namespace
{
	float Stage03DebugField(const FPlanetSnapshot& S, const TCHAR* Name, const FVector3d& P, bool bNearest)
	{
		const FGeoField2D* Fd = S.DebugFields.Find(FName(Name));
		if (!Fd || Fd->Res <= 0) { return FGeoDebugLayer::NoData(); }
		const FVector3f Dir(P.GetSafeNormal());
		return bNearest ? Fd->SampleNearest(Dir) : Fd->Sample(Dir);
	}
}

void FStage03Terrain::DescribeDebugLayers(TArray<FGeoDebugLayer>& Out) const
{
	{
		FGeoDebugLayer L;
		L.Id = TEXT("MountainMask");
		L.DisplayName = TEXT("山带遮蔽");
		L.Stage = 3;
		L.Unit = TEXT("0 无 … 1 主山带");
		L.Sample = [](const FPlanetSnapshot& S, const FVector3d& P) { return Stage03DebugField(S, TEXT("MountainMask"), P, false); };
		Out.Add(MoveTemp(L));
	}
	{
		FGeoDebugLayer L;
		L.Id = TEXT("ReliefDelta");
		L.DisplayName = TEXT("地形增量（山 / 海沟）");
		L.Stage = 3;
		L.Unit = TEXT("体素");
		L.Sample = [](const FPlanetSnapshot& S, const FVector3d& P) { return Stage03DebugField(S, TEXT("ReliefDelta"), P, false); };
		L.Range = [](const FPlanetSnapshot& S) { return FVector2f(float(-0.04 * S.R0), float(0.04 * S.R0)); };
		// 按半径比例着色：蓝 = 下切（海沟），白 = 不变，棕 = 抬升（山）。
		L.Colorize = [](const FPlanetSnapshot& S, float V)
		{
			const float T = FMath::Clamp(V / float(0.04 * S.R0), -1.f, 1.f);
			const FLinearColor Mid(0.92f, 0.92f, 0.9f), Up(0.45f, 0.25f, 0.1f), Dn(0.1f, 0.25f, 0.7f);
			return (T >= 0.f ? FMath::Lerp(Mid, Up, T) : FMath::Lerp(Mid, Dn, -T)).ToFColor(false);
		};
		Out.Add(MoveTemp(L));
	}
	{
		FGeoDebugLayer L;
		L.Id = TEXT("ReliefType");
		L.DisplayName = TEXT("地形类型");
		L.Stage = 3;
		L.Kind = EGeoDebugKind::Categorical;
		L.Legend = {
			{ TEXT("洋盆"), FColor(30, 60, 140) }, { TEXT("陆架"), FColor(110, 180, 220) }, { TEXT("平原"), FColor(110, 170, 80) },
			{ TEXT("丘陵"), FColor(190, 170, 90) }, { TEXT("高山"), FColor(140, 80, 50) }, { TEXT("海沟"), FColor(10, 15, 60) },
			{ TEXT("裂谷"), FColor(200, 60, 60) } };
		L.Sample = [](const FPlanetSnapshot& S, const FVector3d& P) { return Stage03DebugField(S, TEXT("ReliefType"), P, true); };
		Out.Add(MoveTemp(L));
	}
}

void FStage03Terrain::ExportDebugFields(const FPlanetBakeFields& Fields, TMap<FName, FGeoField2D>& Out) const
{
	Out.Add(TEXT("MountainMask"), Fields.MountainMask);
	Out.Add(TEXT("ReliefDelta"), Fields.ReliefDelta);
	Out.Add(TEXT("ReliefType"), Fields.ReliefType);
}
