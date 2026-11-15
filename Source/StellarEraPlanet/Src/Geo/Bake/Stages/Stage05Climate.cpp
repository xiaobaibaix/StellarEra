#include "Stage05Climate.h"
#include "Async/ParallelFor.h"
#include "PlanetSnapshot.h"

float Stage05::WindAzimuth(float AbsLatDeg)
{
	// 信风与极地东风都向西，共用一个方位角：只存角度就必须靠纬度才能区分这两带。
	return AbsLatDeg < 30.f ? 270.f : (AbsLatDeg < 60.f ? 90.f : 270.f);
}

FVector3f Stage05::WindTangent(const FVector3f& Dir, float AzimuthDeg)
{
	const FVector3f D = Dir.GetSafeNormal();
	if (D.IsNearlyZero()) { return FVector3f::ZeroVector; }
	// 极点附近北向切线退化，没有可定义的风向。
	if (1.f - FMath::Abs(D.Z) < 1e-4f) { return FVector3f::ZeroVector; }
	const FVector3f North = (FVector3f::UpVector - D * D.Z).GetSafeNormal();
	// 东 = N × dir，与 GeoPlates 的板块方位角同一套；写成 dir × N 会把整张风带镜像。
	const FVector3f East = FVector3f::CrossProduct(North, D);
	const float Az = FMath::DegreesToRadians(AzimuthDeg);
	return North * FMath::Cos(Az) + East * FMath::Sin(Az);
}

int32 Stage05::BiomeFor(float T, float P, const FPlanetClimateParams& Params)
{
	// 顺序是契约：同一对温/降水只许落到一个群系，重排一行就会改掉整张图。
	if (T < Params.BiomeIceTemp) { return 0; }
	if (P < Params.BiomeDesertPrecip) { return 1; }
	if (T < Params.BiomeTundraTemp) { return 2; }
	if (T < Params.BiomeForestTemp && P > Params.BiomeForestPrecip) { return 3; }
	if (T > Params.BiomeRainTemp && P > Params.BiomeRainPrecip) { return 4; }
	if (T > Params.BiomeSavannaTemp) { return 5; }
	if (P > Params.BiomeTemperatePrecip) { return 6; }
	return 7;
}

namespace
{
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
	// 雨影的抬升以 0.02R 为满分；一步跨得再高也只扣一份，防止贴壁的 texel 一步扣光。
	const float RiseScale = 1.f / FMath::Max(0.02f * R, 1e-6f);
	// texel 步长换算成角步长：立方体面中心 90°/Res，与气候场分辨率同源。
	const float StepRad = float(FMath::Max(P.ShadowStride, 1)) * (PI * 0.5f) / float(Res);
	const float CosStep = FMath::Cos(StepRad), SinStep = FMath::Sin(StepRad);
	// 方向只取决于网格，不该借某一张数据场的 TexelDir 去问。
	const auto TexelDir = [Res](int32 Face, int32 X, int32 Y)
	{
		const float U = (float(X) + 0.5f) / float(Res) * 2.f - 1.f;
		const float V = (float(Y) + 0.5f) / float(Res) * 2.f - 1.f;
		return FGeoField2D::FaceToDir(Face, U, V);
	};
	ParallelFor(6 * Res, [&](int32 Row)
	{
		const int32 Face = Row / Res, Y = Row % Res;
		for (int32 X = 0; X < Res; ++X)
		{
			const FVector3f Dir = TexelDir(Face, X, Y);
			const float Z = FMath::Clamp(Dir.Z, -1.f, 1.f);
			const float E = F.Elevation.Sample(Dir);
			const float M = F.ContinentalMask.Sample(Dir);
			const float AbsLatDeg = FMath::RadiansToDegrees(FMath::Asin(FMath::Abs(Z)));
			// Insolation = 1 − sin²(lat)，而 lat = asin(Z)，所以直接用 1 − Z²。
			const float T = P.PoleTemp + (P.EquatorTemp - P.PoleTemp) * (1.f - Z * Z)
				- P.LapseRate * FMath::Clamp(E * LapseScale, 0.f, 1.f)
				+ P.Continentality * (M - 0.5f);
			const float TStore = FMath::Clamp(T, 0.f, 1.f);
			F.Temperature.At(Face, X, Y) = TStore;

			const float Az = Stage05::WindAzimuth(AbsLatDeg);
			F.WindDir.At(Face, X, Y) = Az;

			// 四段纬度基准：0° 赤道 → 30° 干旱带 → 45° 中纬 → 90° 极区。
			float Precip;
			if (AbsLatDeg < 30.f)
			{
				Precip = FMath::Lerp(P.BaseEquator, P.BaseDry, FMath::SmoothStep(0.f, 30.f, AbsLatDeg));
			}
			else if (AbsLatDeg < 45.f)
			{
				Precip = FMath::Lerp(P.BaseDry, P.BaseMid, FMath::SmoothStep(30.f, 45.f, AbsLatDeg));
			}
			else
			{
				Precip = FMath::Lerp(P.BaseMid, P.BasePole, FMath::SmoothStep(45.f, 90.f, AbsLatDeg));
			}
			// 大陆度越高离海越远、水汽越少：M=1 的内陆扣满，海岸与海洋（M≈0）不扣。
			Precip *= 1.f - P.OceanFalloff * M;

			// 雨影是本阶段唯一的非局部算法：沿逆风方向回溯，翻过一段抬升就扣一份水汽。
			const FVector3f W = Stage05::WindTangent(Dir, Az);
			if (P.ShadowSteps > 0 && P.RainShadow > 0.f && !W.IsNearlyZero())
			{
				// 绕大圆平面法向逐步旋转，避免用固定切向累加导致轨迹漂出球面直线。
				const FVector3f Axis = FVector3f::CrossProduct(Dir, -W).GetSafeNormal();
				FVector3f Q = Dir;
				// 起点也必须走最近邻：整条回溯用一个采样器，否则首步的陆/海判断与后续不一致。
				float PrevE = F.Elevation.SampleNearest(Dir);
				for (int32 Step = 0; Step < P.ShadowSteps; ++Step)
				{
					Q = Q * CosStep + FVector3f::CrossProduct(Axis, Q) * SinStep;
					// 撞海与否是离散判断：用最近 texel。双线性会把陆/海混出半个 texel 的模糊带，
					// 海岸线在哪一侧取决于不随半径缩放的上游下限，回溯步数就会整段跳。
					const float QE = F.Elevation.SampleNearest(Q);
					const float Rise = QE - PrevE;
					if (Rise > 0.f) { Precip *= 1.f - P.RainShadow * FMath::Clamp(Rise * RiseScale, 0.f, 1.f); }
					// 海上水汽充足：回溯到海面就没有再扣的理由。
					if (QE <= 0.f) { break; }
					PrevE = QE;
				}
			}
			const float PStore = FMath::Clamp(Precip + P.PrecipBias, 0.f, 1.f);
			F.Precipitation.At(Face, X, Y) = PStore;
			// 群系只用存下来的 T/P 判：图层、测试与阶段 6 读到的必须是同一对输入。
			F.Biome.At(Face, X, Y) = float(Stage05::BiomeFor(TStore, PStore, P));
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
		L.Id = TEXT("ClimatePrecipitation"); L.DisplayName = TEXT("降水"); L.Stage = 5;
		L.Unit = TEXT("0 干 … 1 湿");
		L.Sample = [](const FPlanetSnapshot& S, const FVector3d& P) { return Stage05DebugField(S, TEXT("Precipitation"), P, false); };
		// 默认色带紫→黄不适合表达「干湿」：褐→蓝绿，一眼分出雨影与干旱带。
		L.Colorize = [](const FPlanetSnapshot&, float V)
		{
			return FMath::Lerp(FLinearColor(0.35f, 0.22f, 0.10f), FLinearColor(0.10f, 0.55f, 0.55f), FMath::Clamp(V, 0.f, 1.f)).ToFColor(false);
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
			// 风带是分段常数：双线性在 30°/60° 接缝会混出 180°，被读成「无风」。
			const float Az = Fd->SampleNearest(Dir);
			if (Stage05::WindTangent(Dir, Az).IsNearlyZero()) { return 0.f; }
			const float AbsLat = FMath::RadiansToDegrees(FMath::Abs(FMath::Asin(FMath::Clamp(Dir.Z, -1.f, 1.f))));
			const bool bWest = FMath::Abs(FMath::FindDeltaAngleDegrees(Az, 270.f)) <= 10.f;
			const bool bEast = FMath::Abs(FMath::FindDeltaAngleDegrees(Az, 90.f)) <= 10.f;
			if (AbsLat < 30.f) { return bWest ? 1.f : 0.f; }
			if (AbsLat < 60.f) { return bEast ? 2.f : 0.f; }
			return bWest ? 3.f : 0.f;
		};
		Out.Add(MoveTemp(L));
	}
	{
		FGeoDebugLayer L;
		L.Id = TEXT("Biome"); L.DisplayName = TEXT("生物群系"); L.Stage = 5;
		L.Kind = EGeoDebugKind::Categorical;
		L.Legend = {
			{ TEXT("冰原"), FColor(240, 246, 255) }, { TEXT("沙漠"), FColor(226, 202, 130) },
			{ TEXT("苔原"), FColor(150, 160, 150) }, { TEXT("针叶林"), FColor(28, 84, 58) },
			{ TEXT("雨林"), FColor(12, 168, 92) }, { TEXT("稀树草原"), FColor(196, 186, 74) },
			{ TEXT("温带林"), FColor(92, 148, 70) }, { TEXT("草原"), FColor(184, 214, 118) } };
		// 分类必须最近邻：双线性会把两个相邻群系的编号混成不存在的第三个。
		L.Sample = [](const FPlanetSnapshot& S, const FVector3d& P) { return Stage05DebugField(S, TEXT("Biome"), P, true); };
		Out.Add(MoveTemp(L));
	}
}

void FStage05Climate::ExportDebugFields(const FPlanetBakeFields& Fields, TMap<FName, FGeoField2D>& Out) const
{
	if (Fields.Temperature.Res > 0) { Out.Add(TEXT("Temperature"), Fields.Temperature); }
	if (Fields.Precipitation.Res > 0) { Out.Add(TEXT("Precipitation"), Fields.Precipitation); }
	if (Fields.WindDir.Res > 0) { Out.Add(TEXT("WindDir"), Fields.WindDir); }
	if (Fields.Biome.Res > 0) { Out.Add(TEXT("Biome"), Fields.Biome); }
}
