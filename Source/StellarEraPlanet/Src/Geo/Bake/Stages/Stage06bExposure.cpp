#include "Stage06bExposure.h"
#include "GeoPrimitive.h"

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

	TArray<float> Lift; Lift.Init(0.f, Num);
	for (int32 i = 0; i < Num; ++i)
	{
		if (IsRock(F.Primitives[i])) { Lift[i] = LiftAt(F.Primitives[i].Center); }
	}
	for (int32 i = 0; i < Num; ++i)
	{
		const FGeoPrimitive& Ore = F.Primitives[i];
		if (Ore.MineralId == 0) { continue; }
		// 宿主 = 中心落在椭球内、归一化距离最小的岩体；平手取编号小者（严格小于才替换）。
		int32 Host = INDEX_NONE;
		float BestD = 1.f;
		for (int32 h = 0; h < Num; ++h)
		{
			const FGeoPrimitive& Pr = F.Primitives[h];
			if (!IsRock(Pr)) { continue; }
			const float D = GeoPrimitiveDistance(Pr, Ore.Center);
			if (D < BestD) { BestD = D; Host = h; }
		}
		Lift[i] = Host != INDEX_NONE ? Lift[Host] : LiftAt(Ore.Center);
	}

	for (int32 i = 0; i < Num; ++i)
	{
		FGeoPrimitive& Pr = F.Primitives[i];
		const FVector3f Dir = Pr.Center.GetSafeNormal();
		if (Dir.IsNearlyZero()) { continue; }
		const float Before = Pr.Center.Size();
		const float Support = GeoPrimitiveSupport(Pr, Dir);
		const float Surface = R + F.Elevation.Sample(Dir);
		Pr.RawDepthBefore = Surface - (Before + Support);

		float L = FMath::Max(Lift[i], 0.f);
		float Ratio;
		if (Before + L + Support > Surface)
		{
			// 只能收回中心：压扁椭球会改形状，而形状与品位属于阶段 4。
			L = FMath::Max(0.f, Surface - Before - Support);
			Ratio = 1.f;
		}
		else
		{
			Ratio = FMath::Clamp(L / FMath::Max(2.f * Support, 1.f), 0.f, 1.f);
		}
		const FVector3f Delta = Dir * L;
		Pr.Center += Delta;
		// 半径不变，只有中心动：包围盒跟着平移，不重算（重算会把浮点误差带进剔除判定）。
		Pr.Bounds = Pr.Bounds.ShiftBy(Delta);
		Pr.Exposure = uint8(FMath::RoundToInt(255.f * Ratio));
	}
}
