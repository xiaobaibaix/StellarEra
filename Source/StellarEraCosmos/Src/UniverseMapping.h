#pragma once

#include "CoreMinimal.h"
#include "CosmosTypes.h"

/**
 * 大尺度渲染映射：仿真（物理 AU）→ 表现空间（世界厘米）的相似变换。
 * 无状态、不读 dt；近场恒等，远场统一乘 FarScale。
 */
struct STELLARERACOSMOS_API FUniverseMapping
{
	/** 真实 1 AU 的厘米数；复用 CosmosUnits，避免第二份物理常量漂移。 */
	static constexpr double PhysicalCmPerAu = CosmosUnits::AuToCentimeter;

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

/** 当前天体：最近天体进入近场即抢占；否则已选中天体在退出带内保留。 */
inline int32 PickCurrentBody(const TArray<double>& DistancesCm, int32 PreviousIndex,
	const FUniverseMapping& Mapping, double ExitScale = 2.0)
{
	const double EnterThreshold = Mapping.NearDistanceCm();

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

	// 近场抢占：飞船已贴着某颗时，不能被上一颗的退出带压掉。
	if (Nearest != INDEX_NONE && NearestDistance <= EnterThreshold)
	{
		return Nearest;
	}

	if (DistancesCm.IsValidIndex(PreviousIndex) && DistancesCm[PreviousIndex] <= EnterThreshold * ExitScale)
	{
		return PreviousIndex;
	}

	return INDEX_NONE;
}

/** 各天体到飞船的距离（厘米）；无地表体与 ExcludeIndex 记无穷，永不参与锚点选择。 */
inline TArray<double> SurfaceDistancesCm(const TArray<FVector3d>& Positions, const TArray<double>& RadiiAu,
	const FVector3d& ShipPositionAu, double CmPerAu, int32 ExcludeIndex = INDEX_NONE)
{
	TArray<double> Out;
	Out.Reserve(Positions.Num());
	for (int32 Index = 0; Index < Positions.Num(); ++Index)
	{
		const bool bHasSurface = RadiiAu.IsValidIndex(Index) && RadiiAu[Index] > 0.0;
		Out.Add(bHasSurface && Index != ExcludeIndex
			? FVector3d(Positions[Index] - ShipPositionAu).Size() * CmPerAu
			: TNumericLimits<double>::Max());
	}
	return Out;
}

/** 物理初态：开启覆盖时用显式 AU 位置，否则沿用关卡摆放换算出的求解器位置。 */
inline FVector3d ResolveInitialPosition(const FVector3d& PlacementAu, const FVector& InitialAu, bool bUseInitialAu)
{
	return bUseInitialAu ? FVector3d(InitialAu) : PlacementAu;
}

/** 表现空间写回：近场（当前天体）按物理厘米 1:1，远场按渲染标定压缩，均以飞船为远场中心。 */
inline FVector ToRenderWorld(const FVector3d& BodyAu, const FVector3d& AnchorAu, const FVector3d& ShipAu,
	const FVector& AnchorWorld, const FVector& ShipWorld, bool bNear,
	double PhysicalCmPerAu, double RenderCmPerAu)
{
	return bNear
		? AnchorWorld + FVector((BodyAu - AnchorAu) * PhysicalCmPerAu)
		: ShipWorld + FVector((BodyAu - ShipAu) * RenderCmPerAu);
}
