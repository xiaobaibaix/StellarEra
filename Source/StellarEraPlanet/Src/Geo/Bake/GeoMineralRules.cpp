#include "GeoMineralRules.h"
#include "GeoLithology.h"
#include "GeoNoise.h"
#include "PlanetBakeTypes.h"
#include "PlanetMaterialEval.h"
#include "PlanetRecipeConfig.h"
#include "PlanetShapeEval.h"

namespace
{
	FVector3f GeoMineralRulesRandomDir(FGeoRng& Rng)
	{
		const float Z = Rng.Range(-1.f, 1.f), Phi = Rng.Range(0.f, 2.f * PI), S = FMath::Sqrt(FMath::Max(0.f, 1.f - Z * Z));
		return FVector3f(S * FMath::Cos(Phi), S * FMath::Sin(Phi), Z);
	}

	uint64 GeoMineralRulesHashFloats(uint64 Tag, std::initializer_list<float> Values)
	{
		uint64 H = Tag;
		for (const float V : Values) { H = GeoHash::Combine(H, GeoHash::Float(V)); }
		return H;
	}

	bool GeoMineralRulesNeedStage2(const FGeoScatterContext& Ctx, const TCHAR* Name)
	{
		const FPlanetBakeFields& F = Ctx.Fields;
		if (F.CrustType.Res > 0 && F.CrustAge.Res > 0 && F.ContinentalMask.Res > 0 && F.Elevation.Res > 0) { return true; }
		Ctx.Errors.Add(FString::Printf(TEXT("[地质] 撒矿器「%s」需要阶段 2 的宏观场，但它们为空"), Name));
		return false;
	}

	/**
	 * 遍历某岩性的原语当宿主，顺带给出它的朝向：Blob 没有 Rotation，用单位四元数兜底。
	 * 已撒过矿的原语（MineralId != 0）不能再当宿主，否则矿会长在矿里。
	 */
	void GeoMineralRulesForEachHost(const FPlanetBakeFields& F, uint8 Lithology,
		TFunctionRef<void(const FGeoPrimitive&, const FQuat4f&)> Fn)
	{
		for (const FGeoPrimitive& Host : F.Primitives)
		{
			if (Host.LithologyId != Lithology || Host.MineralId != 0) { continue; }
			Fn(Host, Host.Kind == EGeoPrimitiveKind::OrientedBlob ? Host.Rotation : FQuat4f::Identity);
		}
	}

	/**
	 * 在地表下 [DMin, DMax] 的球壳里按体积均匀取矿体中心；DMax ≤ 0 = 一直到地壳底（莫霍面最高处之上）。
	 * 按 r³ 均匀取：外层球壳体积大，分到的矿就多；按深度均匀取会让矿偏向深处。
	 */
	FVector3f GeoMineralRulesInVolume(FGeoScatterContext& Ctx, const FVector3f& Dir, float DMin, float DMax, float HalfThickness)
	{
		const FPlanetRecipeConfig& C = Ctx.Config;
		const double Surface = C.Radius + double(Ctx.Fields.Elevation.Sample(Dir));
		const double CrustFloor = C.Radius * (1.0 - double(C.Sphere.CrustRatio) + double(C.Sphere.MohoWobbleRatio)) + HalfThickness;
		const double Top = Surface - double(DMin) - HalfThickness;
		const double Bottom = DMax > 0.f ? FMath::Max(Surface - double(DMax) - HalfThickness, CrustFloor) : CrustFloor;
		const double U = double(Ctx.Rng.NextFloat());
		const double R = Bottom >= Top ? Top : FMath::Pow(U * (Top * Top * Top - Bottom * Bottom * Bottom) + Bottom * Bottom * Bottom, 1.0 / 3.0);
		return FVector3f(FVector3d(Dir) * R);
	}
}

FQuat4f GeoMineralRules::TangentFrame(const FVector3f& Up, float StrikeRadians)
{
	const FVector3f Ref = FMath::Abs(Up.Z) < 0.9f ? FVector3f::UpVector : FVector3f::ForwardVector;
	const FVector3f E = FVector3f::CrossProduct(Ref, Up).GetSafeNormal();
	const FVector3f N = FVector3f::CrossProduct(Up, E);
	const FVector3f X = E * FMath::Cos(StrikeRadians) + N * FMath::Sin(StrikeRadians);
	// Y = Up × X 保证 X × Y = Up，行列式为 +1，是纯旋转。
	const FVector3f Y = FVector3f::CrossProduct(Up, X);
	return FQuat4f(FMatrix44f(X, Y, Up, FVector3f::ZeroVector)).GetNormalized();
}

void FGeoBifRule::Scatter(FGeoScatterContext& Ctx, FGeoScatterOutput& Out) const
{
	if (!GeoMineralRulesNeedStage2(Ctx, GetRuleName())) { return; }
	const FPlanetBakeFields& F = Ctx.Fields;
	const int32 Count = FMath::Clamp(P.Count, 0, 4096);
	int32 Placed = 0;
	for (int32 Try = 0; Try < Count * 400 && Placed < Count; ++Try)
	{
		const FVector3f Dir = GeoMineralRulesRandomDir(Ctx.Rng);
		// 位置由场决定：只有古老陆壳内部才有，种子只决定落在这片区域的哪里。
		if (F.CrustType.SampleNearest(Dir) != 1.f || F.CrustAge.Sample(Dir) < P.AgeMin || F.ContinentalMask.Sample(Dir) < P.ContinentalMin)
		{
			continue;
		}
		const FVector3f Radii(0.5f * P.LengthVoxels, 0.5f * P.WidthVoxels, FMath::Max(0.5f * P.ThicknessVoxels, 0.5f));
		const FQuat4f Rot = GeoMineralRules::TangentFrame(Dir, Ctx.Rng.Range(0.f, 2.f * PI));
		const FVector3f Center = GeoMineralRulesInVolume(Ctx, Dir, P.DepthMinVoxels, P.DepthMaxVoxels, Radii.Z);
		FGeoPrimitive Body = FGeoPrimitive::MakeOrientedBlob(Center, Radii, Rot, 0.1f);
		Body.Grade = RandomGrade(Ctx.Rng);
		Out.Bodies.Add(Body);
		++Placed;
	}
}

uint64 FGeoBifRule::HashParams() const
{
	return GeoMineralRulesHashFloats(GeoHash::Combine(GeoHash::Str("bif"), uint64(P.Count)),
		{ P.AgeMin, P.ContinentalMin, P.LengthVoxels, P.WidthVoxels, P.ThicknessVoxels, P.DepthMinVoxels, P.DepthMaxVoxels });
}

void FGeoVmsRule::Scatter(FGeoScatterContext& Ctx, FGeoScatterOutput& Out) const
{
	if (!GeoMineralRulesNeedStage2(Ctx, GetRuleName())) { return; }
	const FPlanetBakeFields& F = Ctx.Fields;
	if (F.BoundaryType.Res <= 0) { Ctx.Errors.Add(TEXT("[地质] 撒矿器「块状硫化物」需要阶段 2 的 BoundaryType")); return; }
	const int32 Chains = FMath::Clamp(P.Chains, 0, 4096);
	const double R = Ctx.Config.Radius;
	int32 Placed = 0;
	// 离散边界只占星球千分之几的面积：拒绝采样要给足次数。
	for (int32 Try = 0; Try < Chains * 4000 && Placed < Chains; ++Try)
	{
		const FVector3f Dir = GeoMineralRulesRandomDir(Ctx.Rng);
		if (F.BoundaryType.SampleNearest(Dir) != 4.f || F.CrustType.SampleNearest(Dir) != 0.f) { continue; }
		const FQuat4f Frame = GeoMineralRules::TangentFrame(Dir, Ctx.Rng.Range(0.f, 2.f * PI));
		const FVector3f Along = Frame.RotateVector(FVector3f::XAxisVector);
		const int32 Lenses = FMath::RoundToInt(Ctx.Rng.Range(float(FMath::Min(P.LensMin, P.LensMax)), float(FMath::Max(P.LensMin, P.LensMax))));
		for (int32 k = 0; k < Lenses; ++k)
		{
			const FVector3f Q = (Dir + Along * float(double(k) * P.SpacingVoxels / R)).GetSafeNormal();
			// 链子走出洋中脊就截断：矿只能长在脊上。
			if (F.BoundaryType.SampleNearest(Q) != 4.f) { break; }
			const FVector3f Radii(P.LensRadiusVoxels, 0.7f * P.LensRadiusVoxels, FMath::Max(0.5f * P.LensThicknessVoxels, 0.5f));
			FGeoPrimitive Body = FGeoPrimitive::MakeOrientedBlob(GeoMineralRulesInVolume(Ctx, Q, P.DepthMinVoxels, P.DepthMaxVoxels, Radii.Z), Radii,
				GeoMineralRules::TangentFrame(Q, Ctx.Rng.Range(0.f, 2.f * PI)), 0.15f);
			Body.Grade = RandomGrade(Ctx.Rng);
			Out.Bodies.Add(Body);
		}
		++Placed;
	}
}

uint64 FGeoVmsRule::HashParams() const
{
	return GeoMineralRulesHashFloats(GeoHash::Combine(GeoHash::Str("vms"), uint64(P.Chains) | (uint64(P.LensMin) << 20) | (uint64(P.LensMax) << 40)),
		{ P.SpacingVoxels, P.LensRadiusVoxels, P.LensThicknessVoxels, P.DepthMinVoxels, P.DepthMaxVoxels });
}

void FGeoNoduleRule::Scatter(FGeoScatterContext& Ctx, FGeoScatterOutput& Out) const
{
	if (!GeoMineralRulesNeedStage2(Ctx, GetRuleName())) { return; }
	const FPlanetBakeFields& F = Ctx.Fields;
	FGeoMineralOverlay O;
	O.DepthVoxels = FMath::Max(P.DepthVoxels, 0.5f);
	O.Grade = RandomGrade(Ctx.Rng);
	const uint64 Seed = Ctx.Rng.NextU64();
	const float Threshold = 1.f - FMath::Clamp(P.Coverage, 0.f, 1.f);
	O.Mask.Init(F.CrustType.Res);
	const float Split = F.CrustSplitElevation;
	O.Mask.Fill([&F, this, Seed, Threshold, Split](const FVector3f& Dir)
	{
		// 全部用平滑判据：最近邻取宏观分类图会让斑块边缘成锯齿。高程是全分辨率的，洋底判据用它。
		const float Ocean = 1.f - FMath::SmoothStep(1.5f * Split, Split, F.Elevation.Sample(Dir));
		const float Old = FMath::SmoothStep(P.AgeMin - 0.05f, P.AgeMin + 0.05f, F.CrustAge.Sample(Dir));
		const float Far = 1.f - FMath::SmoothStep(P.ContinentalMax, P.ContinentalMax + 0.05f, F.ContinentalMask.Sample(Dir));
		const float Env = Ocean * Old * Far;
		if (Env <= 0.f) { return 0.f; }
		// 斑块噪声决定「这片老洋底里哪些地方有结核」；阈值随覆盖率走，覆盖率只是近似值。
		const float N = 0.5f * (GeoNoise::Fbm3(Seed, Dir * P.PatchFrequency, 3) + 1.f);
		return Env * FMath::SmoothStep(Threshold - 0.05f, Threshold + 0.05f, N);
	}, Ctx.Config.bSingleThreadBake);
	Out.Overlays.Add(MoveTemp(O));
}

uint64 FGeoNoduleRule::HashParams() const
{
	return GeoMineralRulesHashFloats(GeoHash::Str("nodule"), { P.AgeMin, P.ContinentalMax, P.Coverage, P.PatchFrequency, P.DepthVoxels });
}

void FGeoChromiteRule::Scatter(FGeoScatterContext& Ctx, FGeoScatterOutput& Out) const
{
	if (!GeoMineralRulesNeedStage2(Ctx, GetRuleName())) { return; }
	const FPlanetBakeFields& F = Ctx.Fields;
	if (F.MountainMask.Res <= 0) { Ctx.Errors.Add(TEXT("[地质] 撒矿器「蛇绿岩铬铁矿」需要阶段 3 的 MountainMask")); return; }
	const int32 Chains = FMath::Clamp(P.Chains, 0, 4096);
	const double R = Ctx.Config.Radius;
	int32 Placed = 0;
	for (int32 Try = 0; Try < Chains * 4000 && Placed < Chains; ++Try)
	{
		const FVector3f Dir = GeoMineralRulesRandomDir(Ctx.Rng);
		// 缝合带 = 陆上的碰撞造山带核部：洋壳碎片被挤上陆才叫蛇绿岩。
		if (F.MountainMask.Sample(Dir) < 0.5f || F.Elevation.Sample(Dir) <= 0.f) { continue; }
		const FVector3f Along = GeoMineralRules::TangentFrame(Dir, Ctx.Rng.Range(0.f, 2.f * PI)).RotateVector(FVector3f::XAxisVector);
		const int32 Pods = FMath::RoundToInt(Ctx.Rng.Range(float(FMath::Min(P.PodMin, P.PodMax)), float(FMath::Max(P.PodMin, P.PodMax))));
		for (int32 k = 0; k < Pods; ++k)
		{
			const FVector3f Q = (Dir + Along * float(double(k) * P.SpacingVoxels / R)).GetSafeNormal();
			if (F.MountainMask.Sample(Q) < 0.5f) { break; }
			const FVector3f Radii(P.PodRadiusVoxels, 0.5f * P.PodRadiusVoxels, FMath::Max(0.5f * P.PodThicknessVoxels, 0.5f));
			FGeoPrimitive Body = FGeoPrimitive::MakeOrientedBlob(GeoMineralRulesInVolume(Ctx, Q, P.DepthMinVoxels, P.DepthMaxVoxels, Radii.Z),
				Radii, GeoMineralRules::TangentFrame(Q, Ctx.Rng.Range(0.f, 2.f * PI)), 0.15f);
			Body.Grade = RandomGrade(Ctx.Rng);
			Out.Bodies.Add(Body);
		}
		++Placed;
	}
}

uint64 FGeoChromiteRule::HashParams() const
{
	return GeoMineralRulesHashFloats(GeoHash::Combine(GeoHash::Str("chromite"), uint64(P.Chains) | (uint64(P.PodMin) << 20) | (uint64(P.PodMax) << 40)),
		{ P.SpacingVoxels, P.PodRadiusVoxels, P.PodThicknessVoxels, P.DepthMinVoxels, P.DepthMaxVoxels });
}

void FGeoSulfurRule::Scatter(FGeoScatterContext& Ctx, FGeoScatterOutput& Out) const
{
	const FPlanetBakeFields& F = Ctx.Fields;
	if (F.Elevation.Res <= 0)
	{
		Ctx.Errors.Add(FString::Printf(TEXT("[地质] 撒矿器「%s」需要阶段 3 的高程场"), GetRuleName()));
		return;
	}
	// 只有活火山喷气：死火山与溢流玄武岩台地没有火口，也没有硫磺结壳。
	TArray<TPair<FVector3f, float>> Craters;
	for (const FGeoVolcano& V : F.Volcanoes)
	{
		if (V.bActive && V.CraterRadius > 0.f) { Craters.Emplace(V.Dir, FMath::Max(V.CraterRadius, P.RadiusRatio)); }
	}
	if (Craters.IsEmpty()) { return; }   // 一颗没有活火山的星球 = 不产硫磺，不是配置错误

	FGeoMineralOverlay O;
	O.DepthVoxels = FMath::Clamp(P.DepthVoxels, 2.f, 400.f);
	O.Grade = RandomGrade(Ctx.Rng);
	O.Mask.Init(FMath::Max(128, F.Elevation.Res / 2));
	O.Mask.Fill([&Craters](const FVector3f& Dir)
	{
		float Chord = TNumericLimits<float>::Max(), Radius = 0.f;
		for (const TPair<FVector3f, float>& Cr : Craters)
		{
			const float D = (Cr.Key - Dir).Size();
			if (D < Chord) { Chord = D; Radius = Cr.Value; }
		}
		// 0.8–1.2 倍平滑：遮罩在这个分辨率下每座火口只有一两格，硬边会切成一圈方块。
		return 1.f - FMath::SmoothStep(Radius * 0.8f, Radius * 1.2f, Chord);
	}, Ctx.Config.bSingleThreadBake);
	Out.Overlays.Add(MoveTemp(O));
}

uint64 FGeoSulfurRule::HashParams() const
{
	return GeoMineralRulesHashFloats(GeoHash::Str("sulfur"), { P.RadiusRatio, P.DepthVoxels });
}

void FGeoEpithermalRule::Scatter(FGeoScatterContext& Ctx, FGeoScatterOutput& Out) const
{
	const FPlanetBakeFields& F = Ctx.Fields;
	if (F.Elevation.Res <= 0)
	{
		Ctx.Errors.Add(FString::Printf(TEXT("[地质] 撒矿器「%s」需要阶段 3 的高程场"), GetRuleName()));
		return;
	}
	const double R = Ctx.Config.Radius;
	const int32 Veins = FMath::Clamp(P.VeinCount, 1, 16);
	const float Radius = FMath::Max(P.VeinRadiusRatio, 1e-4f) * float(R);
	const float Thickness = FMath::Max(P.VeinThicknessRatio, 1e-4f) * float(R);
	for (const FGeoVolcano& V : F.Volcanoes)
	{
		// 同一批活火山：死火山没有热液系统，没有火口的溢流玄武岩没有上升通道。
		if (!V.bActive || V.CraterRadius <= 0.f) { continue; }
		const FVector3f Dir = V.Dir.GetSafeNormal();
		const double Surf = R + double(F.Elevation.Sample(Dir));
		const FQuat4f Frame = GeoMineralRules::TangentFrame(Dir, Ctx.Rng.Range(0.f, 2.f * PI));
		const FVector3f Along = Frame.RotateVector(FVector3f::XAxisVector);
		// 水平散布是整簇脉体的总宽：沿走向均分，最外侧两条各占半个间距。
		const float Spacing = Veins > 1 ? P.SpreadRatio * V.CraterRadius / float(Veins - 1) : 0.f;
		for (int32 k = 0; k < Veins; ++k)
		{
			const FVector3f Q = (Dir + Along * (float(k) - 0.5f * float(Veins - 1)) * Spacing).GetSafeNormal();
			// 每条脉各自掷深度：脉群是斜列的一簇，不是一层平板。
			const double Center = Surf - double(Ctx.Rng.Range(P.DepthMinRatio, P.DepthMaxRatio)) * R;
			FGeoPrimitive Body = FGeoPrimitive::MakeOrientedBlob(FVector3f(FVector3d(Q) * Center),
				FVector3f(Radius, Radius, Thickness), Frame, 0.06f);
			Body.Grade = RandomGrade(Ctx.Rng);
			Out.Bodies.Add(Body);
		}
	}
}

uint64 FGeoEpithermalRule::HashParams() const
{
	return GeoMineralRulesHashFloats(GeoHash::Combine(GeoHash::Str("epithermal"), uint64(FMath::Max(P.VeinCount, 0))),
		{ P.DepthMinRatio, P.DepthMaxRatio, P.VeinRadiusRatio, P.VeinThicknessRatio, P.SpreadRatio });
}

void FGeoPorphyryRule::Scatter(FGeoScatterContext& Ctx, FGeoScatterOutput& Out) const
{
	const FPlanetBakeFields& F = Ctx.Fields;
	// 只依赖岩株原语本身的几何：没有侵入体就没有宿主，不产矿，不是配置错误。
	if (F.Primitives.IsEmpty()) { return; }
	const double R = Ctx.Config.Radius;
	const float Radius = FMath::Max(P.RadiusRatio, 1e-4f) * float(R);
	const int32 PerStock = FMath::Clamp(P.PerStock, 1, 8);
	const float ShiftMin = FMath::Min(P.DepthMinRatio, P.DepthMaxRatio);
	const float ShiftMax = FMath::Max(P.DepthMinRatio, P.DepthMaxRatio);
	for (const FGeoPrimitive& Stock : F.Primitives)
	{
		if (Stock.LithologyId != GeoLithology::Diorite) { continue; }
		const FVector3f Dir = FVector3f(Stock.Center).GetSafeNormal();
		// 岩株顶面由原语自身算出：这里的规则只认识几何，不重复读阶段 4b 的配置。
		const double StockTop = double(Stock.Center.Size()) + double(Stock.Radii.GetMax());
		for (int32 k = 0; k < PerStock; ++k)
		{
			// 下移量从区间下限起、按个数铺到上限。默认下限(0.012) > 半径(0.01)，
			// 顶面自然压在岩株顶面之下，岩株顶不会整颗被浸染体包住。
			const float Shift = PerStock > 1 ? FMath::Lerp(ShiftMin, ShiftMax, float(k) / float(PerStock - 1)) : ShiftMin;
			FGeoPrimitive Body = FGeoPrimitive::MakeBlob(FVector3f(FVector3d(Dir) * (StockTop - double(Shift) * R)),
				FVector3f(Radius), 0.25f);
			Body.Grade = RandomGrade(Ctx.Rng);
			Out.Bodies.Add(Body);
		}
	}
}

uint64 FGeoPorphyryRule::HashParams() const
{
	return GeoMineralRulesHashFloats(GeoHash::Combine(GeoHash::Str("porphyry"), uint64(FMath::Max(P.PerStock, 0))),
		{ P.DepthMinRatio, P.DepthMaxRatio, P.RadiusRatio });
}

void FGeoTinTungstenRule::Scatter(FGeoScatterContext& Ctx, FGeoScatterOutput& Out) const
{
	const float R = float(Ctx.Config.Radius);
	const int32 PerHost = FMath::Clamp(P.PerBatholith, 1, 16);
	const FVector3f Radii(FMath::Max(P.LengthRatio, 1e-4f) * R, FMath::Max(P.RadiusRatio, 1e-4f) * R, FMath::Max(P.ThicknessRatio, 1e-4f) * R);
	const float TopMin = FMath::Min(P.TopMinRatio, P.TopMaxRatio);
	const float TopMax = FMath::Max(P.TopMinRatio, P.TopMaxRatio);
	// 只依赖岩基原语本身的几何：没有侵入体就没有宿主，不产矿，不是配置错误。
	GeoMineralRulesForEachHost(Ctx.Fields, GeoLithology::Granite, [&](const FGeoPrimitive& Host, const FQuat4f& Rot)
	{
		for (int32 k = 0; k < PerHost; ++k)
		{
			// 顶面壳层：局部 z 取比例、x/y 在岩基截面里散开；脉的长轴就是宿主局部 X（沿弧走向）。
			const FVector3f Local(Host.Radii.X * Ctx.Rng.Range(-0.5f, 0.5f), Host.Radii.Y * Ctx.Rng.Range(-0.5f, 0.5f),
				Host.Radii.Z * Ctx.Rng.Range(TopMin, TopMax));
			FGeoPrimitive Body = FGeoPrimitive::MakeOrientedBlob(Host.Center + Rot.RotateVector(Local), Radii, Rot, 0.06f);
			Body.Grade = RandomGrade(Ctx.Rng);
			Out.Bodies.Add(Body);
		}
	});
}

uint64 FGeoTinTungstenRule::HashParams() const
{
	return GeoMineralRulesHashFloats(GeoHash::Combine(GeoHash::Str("tintungsten"), uint64(FMath::Max(P.PerBatholith, 0))),
		{ P.LengthRatio, P.RadiusRatio, P.ThicknessRatio, P.TopMinRatio, P.TopMaxRatio });
}

void FGeoPegmatiteRule::Scatter(FGeoScatterContext& Ctx, FGeoScatterOutput& Out) const
{
	const float R = float(Ctx.Config.Radius);
	const int32 PerHost = FMath::Clamp(P.PerBatholith, 1, 16);
	const float Radius = FMath::Max(P.RadiusRatio, 1e-4f) * R;
	const float ShellMin = FMath::Min(P.ShellMin, P.ShellMax);
	const float ShellMax = FMath::Max(P.ShellMin, P.ShellMax);
	GeoMineralRulesForEachHost(Ctx.Fields, GeoLithology::Granite, [&](const FGeoPrimitive& Host, const FQuat4f& Rot)
	{
		for (int32 k = 0; k < PerHost; ++k)
		{
			// 边缘壳层：方向随机后按宿主三条半轴缩放，团块才贴着岩基外缘而不是堆在中心。
			const FVector3f Dir = GeoMineralRulesRandomDir(Ctx.Rng);
			const FVector3f Local = Dir * Host.Radii * Ctx.Rng.Range(ShellMin, ShellMax);
			FGeoPrimitive Body = FGeoPrimitive::MakeBlob(Host.Center + Rot.RotateVector(Local), FVector3f(Radius), 0.1f);
			Body.Grade = RandomGrade(Ctx.Rng);
			Out.Bodies.Add(Body);
		}
	});
}

uint64 FGeoPegmatiteRule::HashParams() const
{
	return GeoMineralRulesHashFloats(GeoHash::Combine(GeoHash::Str("pegmatite"), uint64(FMath::Max(P.PerBatholith, 0))),
		{ P.RadiusRatio, P.ShellMin, P.ShellMax });
}

void FGeoCopperNickelRule::Scatter(FGeoScatterContext& Ctx, FGeoScatterOutput& Out) const
{
	const int32 PerHost = FMath::Clamp(P.PerHost, 1, 8);
	const float Width = FMath::Max(P.WidthScale, 1e-3f);
	const float Thickness = FMath::Clamp(P.ThicknessScale, 1e-3f, 0.5f);
	GeoMineralRulesForEachHost(Ctx.Fields, GeoLithology::Gabbro, [&](const FGeoPrimitive& Host, const FQuat4f& Rot)
	{
		const FVector3f Radii(Host.Radii.X * Width, Host.Radii.Y * Width, Host.Radii.Z * Thickness);
		// 中心下移最深 1−Thickness：矿体底正好压住宿主底板，整体不越出宿主。
		const float InsetLo = FMath::Min(0.5f, 1.f - Thickness);
		const float InsetHi = FMath::Max(0.5f, 1.f - Thickness);
		for (int32 k = 0; k < PerHost; ++k)
		{
			// 底板附近：硫化物熔体比硅酸盐重，在宿主下半部聚成水平铺开的扁平体。
			const FVector3f Local(0.f, 0.f, -Host.Radii.Z * Ctx.Rng.Range(InsetLo, InsetHi));
			FGeoPrimitive Body = FGeoPrimitive::MakeOrientedBlob(Host.Center + Rot.RotateVector(Local), Radii, Rot, 0.05f);
			Body.Grade = RandomGrade(Ctx.Rng);
			Out.Bodies.Add(Body);
		}
	});
}

uint64 FGeoCopperNickelRule::HashParams() const
{
	return GeoMineralRulesHashFloats(GeoHash::Combine(GeoHash::Str("coppernickel"), uint64(FMath::Max(P.PerHost, 0))),
		{ P.WidthScale, P.ThicknessScale });
}

void FGeoChromiumPGERule::Scatter(FGeoScatterContext& Ctx, FGeoScatterOutput& Out) const
{
	const int32 PerSlab = FMath::Clamp(P.PerSlab, 1, 8);
	const float Thickness = FMath::Max(P.ThicknessScale, 1e-3f);
	GeoMineralRulesForEachHost(Ctx.Fields, GeoLithology::LayeredMafic, [&](const FGeoPrimitive& Host, const FQuat4f& Rot)
	{
		const FVector3f Radii(Host.Radii.X, Host.Radii.Y, Host.Radii.Z * Thickness);
		for (int32 k = 0; k < PerSlab; ++k)
		{
			// 同中心同朝向：铬铁矿在层状岩体里按重力分异成层，横向铺满整片薄板。
			FGeoPrimitive Body = FGeoPrimitive::MakeOrientedBlob(Host.Center, Radii, Rot, 0.05f);
			Body.Grade = RandomGrade(Ctx.Rng);
			Out.Bodies.Add(Body);
		}
	});
}

uint64 FGeoChromiumPGERule::HashParams() const
{
	return GeoMineralRulesHashFloats(GeoHash::Combine(GeoHash::Str("chromiumpge"), uint64(FMath::Max(P.PerSlab, 0))),
		{ P.ThicknessScale });
}

namespace
{
	TArray<FGeoDebugLegendEntry> GeoMineralRulesLegend(const FPlanetSnapshot& S)
	{
		TArray<FGeoDebugLegendEntry> L;
		L.Add({ TEXT("无矿"), FColor(70, 70, 76) });
		for (int32 i = 0; i < S.Minerals.Num(); ++i) { L.Add({ S.Minerals[i].Name, S.Minerals[i].Color }); }
		return L;
	}

	FColor GeoMineralRulesColor(const FPlanetSnapshot& S, float V)
	{
		const int32 Id = FMath::RoundToInt(V);
		return Id <= 0 ? FColor(70, 70, 76) : S.MineralColor(uint8(FMath::Min(Id, 255)));
	}
}

void GeoMineralRules::DescribeDebugLayers(TArray<FGeoDebugLayer>& Out)
{
	{
		FGeoDebugLayer L;
		L.Id = TEXT("Mineral");
		L.DisplayName = TEXT("矿体（地下）");
		L.Stage = 0;
		L.Domain = EGeoDebugDomain::Volume;
		L.Kind = EGeoDebugKind::Categorical;
		L.DynamicLegend = &GeoMineralRulesLegend;
		L.Colorize = &GeoMineralRulesColor;
		L.DefaultHidden = { 0 };
		L.Sample = [](const FPlanetSnapshot& S, const FVector3d& P)
		{
			return (PlanetShapeEval::Value(S, P) > 0.f || S.IsTooDeepForPrimitives(P)) ? FGeoDebugLayer::NoData() : float(PlanetMaterialEval::Sample(S, P).Mineral);
		};
		// 矿体只有几十到几百体素，整颗星的步进步长约 200 体素，必漏：逐个解析求光线与椭球的交点。
		L.RayHits = [](const FPlanetSnapshot& S, const FVector3d& O, const FVector3d& D, TArray<TPair<double, float>>& Hits)
		{
			for (const FGeoPrimitive& Pr : S.Primitives.GetPrimitives())
			{
				if (Pr.MineralId == 0) { continue; }
				// 先过包围球：矿体几百个，绝大多数不在这条光线上，省一次二次方程。
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
				if (T > 0.0) { Hits.Emplace(T, float(Pr.MineralId)); }
			}
		};
		Out.Add(MoveTemp(L));
	}
	{
		FGeoDebugLayer L;
		L.Id = TEXT("SurfaceMineral");
		L.DisplayName = TEXT("地表矿化");
		L.Stage = 0;
		L.Domain = EGeoDebugDomain::Surface;
		L.Kind = EGeoDebugKind::Categorical;
		L.DynamicLegend = &GeoMineralRulesLegend;
		L.Colorize = &GeoMineralRulesColor;
		// 地表下 1.5 体素：覆层与刚出露的矿体都算，玩家铲一下就能挖到的那一层。
		L.Sample = [](const FPlanetSnapshot& S, const FVector3d& P)
		{
			const FVector3f Dir(P.GetSafeNormal());
			return float(PlanetMaterialEval::Sample(S, FVector3d(Dir) * (PlanetShapeEval::SurfaceRadius(S, Dir) - 1.5)).Mineral);
		};
		Out.Add(MoveTemp(L));
	}
}
