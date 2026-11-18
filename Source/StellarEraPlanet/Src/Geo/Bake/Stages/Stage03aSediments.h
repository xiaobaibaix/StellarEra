// 阶段 3A：沉积盖层。给低地盖一层砂岩 / 页岩 / 石灰岩，只写厚度与类型，不动高程。
#pragma once
#include "CoreMinimal.h"
#include "PlanetStage.h"

class STELLARERAPLANET_API FStage03aSediments final : public IPlanetStage
{
public:
	virtual FName GetName() const override { return TEXT("Stage03aSediments"); }
	virtual uint32 GetStageIndex() const override { return 9; }
	/** 插在阶段 3 与阶段 4 之间：构造岩体与矿脉要能穿透盖层，顺序必须在它们之前。 */
	virtual float GetOrder() const override { return 3.5f; }
	virtual EPlanetField GetReads() const override { return EPlanetField::Continental | EPlanetField::Elevation; }
	virtual EPlanetField GetWrites() const override { return EPlanetField::Strata; }
	virtual void Bake(FPlanetBakeContext& Ctx) const override;
	virtual void DescribeDebugLayers(TArray<FGeoDebugLayer>& Out) const override;
	virtual void ExportDebugFields(const FPlanetBakeFields& Fields, TMap<FName, FGeoField2D>& Out) const override;
};
