#pragma once

#include "CoreMinimal.h"

/**
 * 大尺度渲染映射：仿真（物理 AU）→ 表现空间（世界厘米）的相似变换。
 * 无状态、不读 dt；近场恒等，远场统一乘 FarScale。
 */
struct STELLARERACOSMOS_API FUniverseMapping
{
	/** 真实 1 AU 的厘米数。仿真/物理换算用。 */
	static constexpr double PhysicalCmPerAu = 1.496e13;

	/** 表现空间标定：1 AU 渲染成多少世界厘米（沿用现有观感 10 km）。 */
	double RenderCmPerAu = 1.0e6;

	/** 远场最小可渲染距离（厘米）。k·d 低于它之前必须切回近场。 */
	double MinFarDistanceCm = 1.0e5;

	double FarScale() const { return RenderCmPerAu / PhysicalCmPerAu; }
	double NearDistanceCm() const { return MinFarDistanceCm / FarScale(); }
	bool IsNear(double DistanceCm) const { return DistanceCm <= NearDistanceCm(); }
	double ScaleFor(bool bNear) const { return bNear ? 1.0 : FarScale(); }

	FVector ToRender(const FVector3d& RelativeCm, bool bNear) const
	{
		return bNear ? FVector(RelativeCm) : FVector(RelativeCm * FarScale());
	}

	FVector3d ToSim(const FVector& RelativeScene, bool bNear) const
	{
		return bNear ? FVector3d(RelativeScene) : FVector3d(RelativeScene) / FarScale();
	}
};

/** 当前天体：最近且进入近场；已选中时用 ExitScale 放宽阈值，避免边界抖动。 */
inline int32 PickCurrentBody(const TArray<double>& DistancesCm, int32 PreviousIndex,
	const FUniverseMapping& Mapping, double ExitScale = 2.0)
{
	int32 Nearest = INDEX_NONE;
	double NearestDistance = TNumericLimits<double>::Max();
	for (int32 Index = 0; Index < DistancesCm.Num(); ++Index)
	{
		if (DistancesCm[Index] < NearestDistance)
		{
			NearestDistance = DistancesCm[Index];
			Nearest = Index;
		}
	}
	if (Nearest == INDEX_NONE)
	{
		return INDEX_NONE;
	}

	const bool bKeepPrevious = PreviousIndex != INDEX_NONE && DistancesCm.IsValidIndex(PreviousIndex);
	const double Threshold = bKeepPrevious
		? Mapping.NearDistanceCm() * ExitScale
		: Mapping.NearDistanceCm();

	return NearestDistance <= Threshold ? Nearest : INDEX_NONE;
}
