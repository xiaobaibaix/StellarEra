#include "Stage02Plates.h"
#include "GeoPlates.h"
#include "GeoNoise.h"

namespace
{
	// 单核高斯团：D 为到核的有符号距离（方向单位），Sigma 已乘 α。
	FORCEINLINE float CoreContribution(float D, float Sigma)
	{
		return FMath::Exp(-(D * D) / (2.f * Sigma * Sigma));
	}

	/** Σ 高斯团再钳到 [0,1]：多个核重叠时叠加成整片大陆，而不是各自截顶。 */
	FORCEINLINE float ContinentalMaskAt(const TArray<FGeoContinentCore>& Cores, float Alpha, float SigmaRatio, const FVector3f& Dir)
	{
		float M = 0.f;
		for (const FGeoContinentCore& Core : Cores)
		{
			const float Radius = Alpha * Core.RadiusRatio;
			const float Sigma = FMath::Max(SigmaRatio * Radius, 1e-6f);
			M += CoreContribution((Dir - Core.Center).Size() - Radius, Sigma);
		}
		return FMath::Clamp(M, 0.f, 1.f);
	}

	/** 陆地占比：64×64/面 上数 M>0.5；只看二分方向，成本可忽略。 */
	float LandFracAt(const FGeoPlateFrame& Frame, float Alpha, float SigmaRatio, bool bSingleThread)
	{
		FGeoField2D Probe;
		Probe.Init(64);
		Probe.Fill([&Frame, Alpha, SigmaRatio](const FVector3f& Dir)
		{
			return ContinentalMaskAt(Frame.Cores, Alpha, SigmaRatio, Dir);
		}, bSingleThread);

		int32 Land = 0;
		for (int32 Face = 0; Face < 6; ++Face)
		for (int32 Y = 0; Y < 64; ++Y)
		for (int32 X = 0; X < 64; ++X)
		{
			if (Probe.At(Face, X, Y) > 0.5f) { ++Land; }
		}
		return float(Land) / float(6 * 64 * 64);
	}
}

void FStage02Plates::Bake(FPlanetBakeContext& Ctx) const
{
	const FPlanetPlateParams& P = Ctx.Config.Plates;
	const bool bSingleThread = Ctx.Config.bSingleThreadBake;
	FPlanetBakeFields& Fields = Ctx.Fields;

	Fields.PlateFrame = GeoPlates::Build(Ctx.Config);
	const FGeoPlateFrame& Frame = Fields.PlateFrame;
	if (Frame.Cores.IsEmpty())
	{
		Ctx.Report.Errors.Add(TEXT("[地质] 阶段 2：陆核列表为空"));
		return;
	}

	// α 由陆地占比反解：核半径 = α·r，二分 10 次后区间宽度 < 0.01。
	float Lo = P.AlphaMin, Hi = P.AlphaMax;
	for (int32 i = 0; i < 10; ++i)
	{
		const float Mid = 0.5f * (Lo + Hi);
		if (LandFracAt(Frame, Mid, P.SigmaRatio, bSingleThread) < P.TargetLandFrac) { Lo = Mid; }
		else { Hi = Mid; }
	}
	const float Alpha = 0.5f * (Lo + Hi);
	// 用最终 α 重算：最后一次迭代的采样点在区间端点，不是收敛值。
	Fields.LandAreaFrac = LandFracAt(Frame, Alpha, P.SigmaRatio, bSingleThread);
	if (FMath::Abs(Fields.LandAreaFrac - P.TargetLandFrac) > 0.05f)
	{
		Ctx.Report.Errors.Add(FString::Printf(TEXT("[地质] 阶段 2：陆地占比 %.3f 偏离目标 %.3f 超过 0.05"),
			Fields.LandAreaFrac, P.TargetLandFrac));
	}

	const int32 FaceRes = Ctx.Config.FaceRes;
	const int32 MacroRes = FMath::Max(32, FaceRes / 4);

	// M 与梯度只在本阶段内部用；对外输出的是 MacroRes 的辅助场。
	FGeoField2D MFull;
	MFull.Init(FaceRes);
	MFull.Fill([&Frame, Alpha, &P](const FVector3f& Dir)
	{
		return ContinentalMaskAt(Frame.Cores, Alpha, P.SigmaRatio, Dir);
	}, bSingleThread);

	FGeoField2D GradFull;
	GradFull.Init(FaceRes);
	GradFull.Fill([&MFull, FaceRes](const FVector3f& Dir)
	{
		int32 Face; float U, V;
		FGeoField2D::DirToFace(Dir, Face, U, V);
		const float Du = 2.f / float(FaceRes);
		// 不钳 U：钳了边界 texel 的两个邻居会重合，梯度恒 0，海岸线在立方体棱上断开。
		const FVector3f Up = FGeoField2D::FaceToDir(Face, U + Du, V);
		const FVector3f Um = FGeoField2D::FaceToDir(Face, U - Du, V);
		const FVector3f Vp = FGeoField2D::FaceToDir(Face, U, V + Du);
		const FVector3f Vm = FGeoField2D::FaceToDir(Face, U, V - Du);
		const float GX = FMath::Abs(MFull.Sample(Up) - MFull.Sample(Um)) / FMath::Max((Up - Um).Size(), 1e-6f);
		const float GY = FMath::Abs(MFull.Sample(Vp) - MFull.Sample(Vm)) / FMath::Max((Vp - Vm).Size(), 1e-6f);
		return FMath::Max(GX, GY);
	}, bSingleThread);

	const float WR = FMath::Max(P.SlopeWidthVoxels / float(Ctx.Config.Radius), 1e-6f);
	Fields.StyleHeight.Init(MacroRes);
	Fields.StyleHeight.Fill([&Frame, &P, WR](const FVector3f& Dir)
	{
		// 与 PlateId 用同一个 warp 后的方向，否则风格台阶会和板块边界错开。
		const FVector3f W = GeoPlates::WarpDirection(Frame, Dir, P.WarpAmp);
		TArray<float, TInlineAllocator<16>> Dists;
		Dists.SetNumUninitialized(Frame.Plates.Num());
		float DMin = TNumericLimits<float>::Max();
		for (int32 p = 0; p < Frame.Plates.Num(); ++p)
		{
			Dists[p] = (W - Frame.Plates[p].Center).Size() / Frame.Plates[p].Weight;
			DMin = FMath::Min(DMin, Dists[p]);
		}
		// softmax 混合：ΣW ≡ 1，三节点处三块板块自动共担，不会塌。
		float SumW = 0.f, SumH = 0.f;
		for (int32 p = 0; p < Frame.Plates.Num(); ++p)
		{
			const FGeoPlate& Plate = Frame.Plates[p];
			const float WeightP = FMath::Exp(-(Dists[p] - DMin) / WR);
			const float Noise = GeoNoise::Fbm3(Plate.StyleSeed, W * (2.f * P.StyleNoiseWavelengths), 2);
			SumW += WeightP;
			SumH += WeightP * (Plate.StyleOffset + Plate.StyleAmp * Noise);
		}
		return SumH / SumW;
	}, bSingleThread);

	const float AUp = P.LandTableVoxels;
	const float ADown = P.OceanBasinVoxels;
	const float WS = P.SlopeWidthVoxels / float(Ctx.Config.Radius);
	Fields.Elevation.Init(FaceRes);
	// h_base 与风格项一次写完：分两次 Fill 会白算一遍 h_base，逐位等价。
	Fields.Elevation.Fill([&MFull, &GradFull, &Fields, AUp, ADown, WS](const FVector3f& Dir)
	{
		const float M = MFull.Sample(Dir);
		const float T = (M - 0.5f) / FMath::Max(GradFull.Sample(Dir), 1e-4f);
		const float Up = AUp * FMath::SmoothStep(0.f, WS, T);
		const float Dn = ADown * FMath::SmoothStep(0.f, WS, -T);
		return Up - Dn + Fields.StyleHeight.Sample(Dir);
	}, bSingleThread);

	// 8 张宏观场一次循环写完：每 texel 的 warp 与 Fbm3 是阶段里最贵的部分，拆四次 Fill 就重算四遍。
	FGeoField2D* Macro[8] = { &Fields.PlateId, &Fields.BoundaryType, &Fields.BoundaryDistance, &Fields.CrustAge,
		&Fields.ContinentalMask, &Fields.ContinentalGrad, &Fields.CrustType, &Fields.CrustThickness };
	for (FGeoField2D* Field : Macro) { Field->Init(MacroRes); }

	const float Band = P.BoundaryBandRatio;
	const float AgeScale = FMath::Max(P.AgeScaleRatio, 1e-6f);
	const float OceanHalf = 0.5f * P.OceanBasinVoxels;
	for (int32 Face = 0; Face < 6; ++Face)
	for (int32 Y = 0; Y < MacroRes; ++Y)
	for (int32 X = 0; X < MacroRes; ++X)
	{
		const FVector3f Dir = Fields.PlateId.TexelDir(Face, X, Y);
		const FVector3f W = GeoPlates::WarpDirection(Frame, Dir, P.WarpAmp);
		int32 Near, Second; float D1, D2;
		GeoPlates::NearestTwo(Frame, W, Near, Second, D1, D2);
		const float B = 0.5f * (D2 - D1);
		const float M = MFull.Sample(Dir);
		const float E = Fields.Elevation.Sample(Dir);
		const float Age = FMath::Clamp(B / AgeScale, 0.f, 1.f);

		Fields.PlateId.At(Face, X, Y) = float(Near);
		Fields.BoundaryDistance.At(Face, X, Y) = B;
		Fields.BoundaryType.At(Face, X, Y) = B > Band ? 0.f : float(Frame.BoundaryTypeBetween(Near, Second));
		// 陆区地壳年龄取高：真实大陆核部更老，这一版只做近似。
		Fields.CrustAge.At(Face, X, Y) = M > 0.5f ? FMath::Max(Age, 0.75f + 0.25f * M) : Age;
		Fields.ContinentalMask.At(Face, X, Y) = M;
		Fields.ContinentalGrad.At(Face, X, Y) = GradFull.Sample(Dir);
		// CrustType：0 洋壳 / 1 陆壳 / 2 陆坡，分界与快照的 CrustSplitElevation = −A_down/2 一致。
		Fields.CrustType.At(Face, X, Y) = E > 0.f ? 1.f : (E < -OceanHalf ? 0.f : 2.f);
		Fields.CrustThickness.At(Face, X, Y) = FMath::Clamp(0.1f + 0.9f * M, 0.f, 1.f);
	}

	// Fill 回调是并行的，min/max 只能等全部写完后串行扫（在回调里累加会竞争）。
	float MinE = TNumericLimits<float>::Max(), MaxE = -TNumericLimits<float>::Max();
	for (int32 Face = 0; Face < 6; ++Face)
	for (int32 Y = 0; Y < FaceRes; ++Y)
	for (int32 X = 0; X < FaceRes; ++X)
	{
		const float E = Fields.Elevation.At(Face, X, Y);
		MinE = FMath::Min(MinE, E);
		MaxE = FMath::Max(MaxE, E);
	}
	Fields.MinElevation = MinE;
	Fields.MaxElevation = MaxE;
}
