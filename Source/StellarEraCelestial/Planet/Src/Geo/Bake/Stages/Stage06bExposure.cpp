#include "Stage06bExposure.h"
#include "GeoPrimitive.h"
#include "PlanetSnapshot.h"

void FStage06bExposure::Bake(FPlanetBakeContext& Ctx) const
{
	FPlanetBakeFields& F = Ctx.Fields;
	if (F.ErosionDepth.Res <= 0)
	{
		Ctx.Report.Errors.Add(TEXT("[地质] 阶段 7：缺剥蚀深度场"));
		return;
	}
	if (F.Elevation.Res <= 0)
	{
		Ctx.Report.Errors.Add(TEXT("[地质] 阶段 7：缺高程场"));
		return;
	}
	const FPlanetExposureParams& P = Ctx.Config.Exposure;
	if (!P.bExposure) { return; }

	const int32 Num = F.Primitives.Num();
	if (Num == 0) { return; }
	const float R = float(Ctx.Config.Radius);
	const float MaxLift = P.MaxLiftRatio * R;
	const auto LiftAt = [&](const FVector3f& Center)
	{
		const FVector3f Dir = Center.GetSafeNormal();
		return Dir.IsNearlyZero() ? 0.f : FMath::Clamp(P.LiftScale * F.ErosionDepth.Sample(Dir), 0.f, MaxLift);
	};
	const auto IsRock = [](const FGeoPrimitive& Pr) { return Pr.MineralId == 0 && Pr.LithologyId != 0; };
	// 蚀变晕描述的就是宿主岩体本身：分开抬会让「晕」留在原地、描述几百体素外的空气。
	const auto IsHalo = [](const FGeoPrimitive& Pr) { return Pr.MineralId == 0 && Pr.LithologyId == 0 && Pr.ClueId != 0; };

	const auto IsFollower = [&](const FGeoPrimitive& Pr)
	{
		return Pr.MineralId != 0 || IsHalo(Pr);
	};

	// 实际位移按原语记录：跟随者要复制宿主的整条位移向量（含宿主的夹取），只抄模长会甩开。
	TArray<FVector3f> Delta; Delta.Init(FVector3f::ZeroVector, Num);
	const auto Place = [&](int32 i, const FVector3f& Want)
	{
		FGeoPrimitive& Pr = F.Primitives[i];
		const FVector3f Dir = Pr.Center.GetSafeNormal();
		if (Dir.IsNearlyZero()) { return; }
		const float Support = GeoPrimitiveSupport(Pr, Dir);
		const float Surface = R + F.Elevation.Sample(Dir);
		Pr.RawDepthBefore = Surface - (Pr.Center.Size() + Support);

		FVector3f D = Want;
		float Ratio;
		const float NewTop = (Pr.Center + D).Size() + Support;
		if (NewTop > Surface)
		{
			// 只能沿自身径向收回中心：压扁椭球会改形状，而形状与品位属于阶段 4。
			D -= Dir * (NewTop - Surface);
			Ratio = 1.f;
		}
		else
		{
			Ratio = FMath::Clamp(D.Size() / FMath::Max(2.f * Support, 1.f), 0.f, 1.f);
		}
		Pr.Center += D;
		// 半径不变，只有中心动：包围盒跟着平移，不重算（重算会把浮点误差带进剔除判定）。
		Pr.Bounds = Pr.Bounds.ShiftBy(D);
		Pr.Exposure = uint8(FMath::RoundToInt(255.f * Ratio));
		Delta[i] = D;
	};

	// 宿主必须在任何原语搬动之前定下来：拿搬过的岩体去匹配，会把矿分给另一具岩体。
	TArray<int32> HostOf; HostOf.Init(INDEX_NONE, Num);
	for (int32 i = 0; i < Num; ++i)
	{
		const FGeoPrimitive& Follower = F.Primitives[i];
		if (IsRock(Follower) || (Follower.MineralId == 0 && !IsHalo(Follower))) { continue; }
		// 宿主 = 中心落在椭球内、归一化距离最小的岩体；平手取编号小者（严格小于才替换）。
		float BestD = 1.f;
		for (int32 h = 0; h < Num; ++h)
		{
			const FGeoPrimitive& Pr = F.Primitives[h];
			if (!IsRock(Pr)) { continue; }
			const float D = GeoPrimitiveDistance(Pr, Follower.Center);
			if (D < BestD) { BestD = D; HostOf[i] = h; }
		}
	}

	// 宿主先落位：跟随者读到的必须是它们的最终位移。
	for (int32 i = 0; i < Num; ++i)
	{
		const FGeoPrimitive& Pr = F.Primitives[i];
		if (!IsRock(Pr)) { continue; }
		Place(i, Pr.Center.GetSafeNormal() * LiftAt(Pr.Center));
	}
	for (int32 i = 0; i < Num; ++i)
	{
		const FGeoPrimitive& Follower = F.Primitives[i];
		if (IsRock(Follower) || (Follower.MineralId == 0 && !IsHalo(Follower))) { continue; }
		Place(i, HostOf[i] != INDEX_NONE ? Delta[HostOf[i]] : Follower.Center.GetSafeNormal() * LiftAt(Follower.Center));
	}
	for (int32 i = 0; i < Num; ++i)
	{
		const FGeoPrimitive& Pr = F.Primitives[i];
		if (IsRock(Pr) || IsFollower(Pr)) { continue; }
		Place(i, FVector3f::ZeroVector);
	}
}

namespace
{
	/** 点所在原语的归一化距离；不在任何原语内返回 -1。只读快照，多线程安全。 */
	float Stage06bNearestPrimitive(const FPlanetSnapshot& S, const FVector3f& Pf, bool bOreOnly, const FGeoPrimitive** OutPrim)
	{
		float Best = -1.f;
		const TArray<FGeoPrimitive>& Prims = S.Primitives.GetPrimitives();
		S.Primitives.QueryPoint(Pf, [&](int32 i)
		{
			const FGeoPrimitive& Pr = Prims[i];
			if (bOreOnly && Pr.MineralId == 0) { return; }
			const float D = GeoPrimitiveDistance(Pr, Pf);
			// BVH 只保证点在 AABB 内：旋转椭球的盒角在椭球外，不吃掉 D ≥ 1 会读到体外空气。
			if (D >= 1.f) { return; }
			if (Best < 0.f || D < Best) { Best = D; *OutPrim = &Pr; }
		});
		return Best;
	}
}

void FStage06bExposure::DescribeDebugLayers(TArray<FGeoDebugLayer>& Out) const
{
	{
		FGeoDebugLayer L;
		L.Id = TEXT("Exposure"); L.DisplayName = TEXT("出露程度（地下）"); L.Stage = 7;
		L.Domain = EGeoDebugDomain::Volume;
		L.Unit = TEXT("0 未出露 … 1 完全出露");
		// 出露比例是单变量：黑 → 白，别用默认五段彩虹，它会让 0.5 和 1 看起来像不同量纲。
		L.Colorize = [](const FPlanetSnapshot&, float V)
		{
			const uint8 C = uint8(FMath::RoundToInt(255.f * FMath::Clamp(V, 0.f, 1.f)));
			return FColor(C, C, C, 255);
		};
		// 只读快照原语：暴露出露是三维属性，剖面与地表叠加都走同一个 Sample。
		L.Sample = [](const FPlanetSnapshot& S, const FVector3d& P)
		{
			if (S.Primitives.IsEmpty() || S.IsTooDeepForPrimitives(P)) { return 0.f; }
			const FGeoPrimitive* Prim = nullptr;
			if (Stage06bNearestPrimitive(S, FVector3f(P), false, &Prim) < 0.f) { return 0.f; }
			return float(Prim->Exposure) / 255.f;
		};
		Out.Add(MoveTemp(L));
	}
	{
		FGeoDebugLayer L;
		L.Id = TEXT("ExposedOre"); L.DisplayName = TEXT("矿体出露（地下）"); L.Stage = 7;
		L.Domain = EGeoDebugDomain::Volume;
		L.Kind = EGeoDebugKind::Categorical;
		// 0 与「无矿」同色：先把未出露的矿藏藏起来，调参时只剩真正露头的。
		L.Legend = { { TEXT("未出露"), FColor(70, 70, 70) },
			{ TEXT("部分出露"), FColor(230, 190, 70) },
			{ TEXT("完全出露"), FColor(255, 255, 255) } };
		L.DefaultHidden = { 0 };
		L.Sample = [](const FPlanetSnapshot& S, const FVector3d& P)
		{
			if (S.Primitives.IsEmpty() || S.IsTooDeepForPrimitives(P)) { return 0.f; }
			const FGeoPrimitive* Prim = nullptr;
			if (Stage06bNearestPrimitive(S, FVector3f(P), true, &Prim) < 0.f) { return 0.f; }
			return Prim->Exposure == 255 ? 2.f : (Prim->Exposure > 0 ? 1.f : 0.f);
		};
		Out.Add(MoveTemp(L));
	}
}
