// 阶段 3B：构造变形变质。断层带、褶皱场、变质级，给「造山型金 / 石墨 / 大理岩」定位。
#pragma once
#include "CoreMinimal.h"
#include "PlanetStage.h"

class STELLARERAPLANET_API FStage03bTectonics final : public IPlanetStage
{
public:
	virtual FName GetName() const override { return TEXT("Stage03bTectonics"); }
	virtual uint32 GetStageIndex() const override { return 10; }
	/** 紧挨 3A 之后、阶段 4 之前：变质级要早于侵入体与撒矿。 */
	virtual float GetOrder() const override { return 3.6f; }
	virtual EPlanetField GetReads() const override { return EPlanetField::Plates | EPlanetField::Relief; }
	virtual EPlanetField GetWrites() const override { return EPlanetField::Tectonics; }
	virtual void Bake(FPlanetBakeContext& Ctx) const override;
	virtual void DescribeDebugLayers(TArray<FGeoDebugLayer>& Out) const override;
	virtual void ExportDebugFields(const FPlanetBakeFields& Fields, TMap<FName, FGeoField2D>& Out) const override;
};
