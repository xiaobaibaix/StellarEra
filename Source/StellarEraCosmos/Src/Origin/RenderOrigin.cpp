#include "RenderOrigin.h"

#include "NBodySimulator.h"

void FRenderOrigin::SelectAnchor(FNBodySimulator& Simulator)
{
	AnchorIndex = INDEX_NONE;

	for (int32 Index = 0; Index < Simulator.GetNumBodies(); ++Index)
	{
		const FCosmosBodyState& Body = Simulator.GetBody(Index);
		if (Body.Radius <= 0.0 || !Body.OwnerActor.IsValid())
		{
			continue;
		}

		if (AnchorIndex == INDEX_NONE || Body.Mass > Simulator.GetBody(AnchorIndex).Mass)
		{
			AnchorIndex = Index;
		}
	}

	if (AnchorIndex == INDEX_NONE)
	{
		// 没有地表天体（纯质点系统）：不重定位，保持旧行为。
		return;
	}

	// 锚点钉在关卡摆放处，开局不跳；之后原点跟着它走，它才看起来一动不动。
	AnchorWorld = Simulator.GetBody(AnchorIndex).OwnerActor->GetActorLocation();
	Apply(Simulator);

	UE_LOG(LogTemp, Log,
		TEXT("[Cosmos] 渲染原点 = 天体 %d（%s），世界位置 (%.1f, %.1f, %.1f) 厘米；此后世界坐标不再随轨道平移"),
		AnchorIndex, *GetNameSafe(Simulator.GetBody(AnchorIndex).OwnerActor.Get()),
		AnchorWorld.X, AnchorWorld.Y, AnchorWorld.Z);
}

void FRenderOrigin::Apply(FNBodySimulator& Simulator, const FVector3d* AnchorPositionOverride) const
{
	if (AnchorIndex == INDEX_NONE)
	{
		return;
	}

	Simulator.SetRenderOrigin(
		AnchorPositionOverride ? *AnchorPositionOverride
		                       : Simulator.GetBody(AnchorIndex).Position,
		AnchorWorld);
}

void FRenderOrigin::SetOrigin(const FVector3d& InGameUnits, const FVector& InWorld)
{
	OriginGameUnits = InGameUnits;
	AnchorWorld = InWorld;
}

FVector FRenderOrigin::ToWorldLocation(const FVector3d& GameUnitPosition, const double CmPerAu,
                                       const bool bGameUnits) const
{
	// 先减原点再乘：在 AU 量级上做减法，double 还有 15 位有效数字，不会把厘米级细节吃成噪声。
	// 没设原点时两个字段都是零，减零对浮点是精确的，行为与旧版逐位相同。
	const FVector3d Relative = GameUnitPosition - OriginGameUnits;
	if (bGameUnits)
	{
		return FVector(Relative) * CmPerAu + AnchorWorld;
	}
	return FVector(Relative) + AnchorWorld;
}

FVector3d FRenderOrigin::ToGameUnitPosition(const FVector& WorldLocation, const double CmPerAu,
                                            const bool bGameUnits) const
{
	const FVector Relative = WorldLocation - AnchorWorld;
	if (bGameUnits)
	{
		return FVector3d(Relative) / CmPerAu + OriginGameUnits;
	}
	return FVector3d(Relative) + OriginGameUnits;
}
