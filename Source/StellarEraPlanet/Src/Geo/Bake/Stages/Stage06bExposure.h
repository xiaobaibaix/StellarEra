// 阶段 7：出露结算。按 6a 的剥蚀深度把岩体与矿体沿径向上抬，矿体跟随宿主。
#pragma once
#include "CoreMinimal.h"
#include "PlanetStage.h"

class STELLARERAPLANET_API FStage06bExposure final : public IPlanetStage
{
public:
	virtual FName GetName() const override { return TEXT("Stage06bExposure"); }
	virtual uint32 GetStageIndex() const override { return 7; }
	virtual EPlanetField GetReads() const override { return EPlanetField::Hydro | EPlanetField::Primitives; }
	virtual EPlanetField GetWrites() const override { return EPlanetField::Exposure; }
	virtual void Bake(FPlanetBakeContext& Ctx) const override;
	virtual void DescribeDebugLayers(TArray<FGeoDebugLayer>& Out) const override;
};
