#include "Stage01Sphere.h"
#include "GeoLithology.h"
#include "GeoNoise.h"
#include "PlanetMaterialEval.h"
#include "PlanetShapeEval.h"
#include "PlanetSnapshot.h"

namespace
{
	// 莫霍面以外是相变或成分界面，起伏比莫霍面小一个量级（附录 01-sphere.md）。
	constexpr float DeepWobbleRatio = 0.002f;
}

void FStage01Sphere::Bake(FPlanetBakeContext& Ctx) const
{
	const FPlanetSphereParams& S = Ctx.Config.Sphere;
	const float Bottoms[6] = { S.CrustRatio, S.UpperMantleRatio, S.TransitionRatio, S.MantleOuterRatio, S.OuterCoreRatio, 1.f };
	const uint8 Ids[6] = { GeoLithology::Crust, GeoLithology::UpperMantle, GeoLithology::Transition,
		GeoLithology::LowerMantle, GeoLithology::OuterCore, GeoLithology::InnerCore };
	// 关掉莫霍面就等于把地壳/地幔界面拉直：层级与岩性照旧，只把起伏置 0。
	const bool bMoho = S.bMoho;
	const float MohoAmp = bMoho ? S.MohoWobbleRatio : 0.f;
	const float Wobbles[6] = { MohoAmp, DeepWobbleRatio, DeepWobbleRatio, DeepWobbleRatio, DeepWobbleRatio, 0.f };

	float Top = 0.f;
	for (int32 i = 0; i < 6; ++i)
	{
		if (Bottoms[i] <= Top)
		{
			Ctx.Report.Errors.Add(FString::Printf(TEXT("[地质] 阶段 1：第 %d 层底界 %.3f 不大于上一层 %.3f"), i, Bottoms[i], Top));
			return;
		}
		Top = Bottoms[i];
	}
	Top = 0.f;
	for (int32 i = 0; i < 6; ++i)
	{
		const float Thickness = Bottoms[i] - Top;
		const float NextThickness = i < 5 ? Bottoms[i + 1] - Bottoms[i] : 1.f;
		// 起伏超过上下两层半厚的较小者，界面会越过相邻界面，层序翻转。
		if (Wobbles[i] >= 0.5f * FMath::Min(Thickness, NextThickness))
		{
			Ctx.Report.Errors.Add(FString::Printf(TEXT("[地质] 阶段 1：第 %d 层起伏 %.3f 超过相邻层半厚"), i, Wobbles[i]));
			return;
		}
		Ctx.Fields.Radial.Layers.Add({ Bottoms[i], Ids[i], Wobbles[i] });
		Top = Bottoms[i];
	}

	const uint64 NoiseSeed = Ctx.Rng.NextU64();
	const int32 Octaves = FMath::Clamp(S.WobbleOctaves, 1, 6);
	Ctx.Fields.MohoWobble.Init(FMath::Clamp(Ctx.Config.FaceRes / 4, 32, 256));
	// 方向乘 2：整个球面约 2 个低频波长，莫霍面起伏是大陆尺度的。
	Ctx.Fields.MohoWobble.Fill([NoiseSeed, Octaves, bMoho](const FVector3f& Dir)
	{
		return bMoho ? GeoNoise::Fbm3(NoiseSeed, Dir * 2.f, Octaves) : 0.f;
	}, Ctx.Config.bSingleThreadBake);
}

void FStage01Sphere::DescribeDebugLayers(TArray<FGeoDebugLayer>& Out) const
{
	{
		FGeoDebugLayer L;
		L.Id = TEXT("Lithology");
		L.DisplayName = TEXT("圈层 / 岩性");
		L.Stage = 1;
		L.Domain = EGeoDebugDomain::Volume;
		L.Kind = EGeoDebugKind::Categorical;
		// 只列到内核：陆壳 / 洋壳在调试显示里并成「地壳」。
		for (uint8 Id = 0; Id < GeoLithology::ContinentalCrust; ++Id)
		{
			L.Legend.Add({ FString(GeoLithology::DisplayName(Id)), GeoLithology::DebugColor(Id) });
		}
		// 与体素材质同一条求值路径：看到的就是挖下去会遇到的。
		L.Sample = [](const FPlanetSnapshot& S, const FVector3d& P)
		{
			if (PlanetShapeEval::Value(S, P) > 0.f) { return FGeoDebugLayer::NoData(); }
			const uint8 Id = PlanetMaterialEval::Sample(S, P).Lithology;
			return float(Id >= GeoLithology::ContinentalCrust ? GeoLithology::Crust : Id);
		};
		Out.Add(MoveTemp(L));
	}
	{
		FGeoDebugLayer L;
		L.Id = TEXT("MohoWobble");
		L.DisplayName = TEXT("莫霍面起伏");
		L.Stage = 1;
		L.Min = -1.f;
		L.Max = 1.f;
		L.Unit = TEXT("× 起伏幅度");
		L.Sample = [](const FPlanetSnapshot& S, const FVector3d& P)
		{
			return S.MohoWobble.Res > 0 ? S.MohoWobble.Sample(FVector3f(P.GetSafeNormal())) : FGeoDebugLayer::NoData();
		};
		Out.Add(MoveTemp(L));
	}
}
