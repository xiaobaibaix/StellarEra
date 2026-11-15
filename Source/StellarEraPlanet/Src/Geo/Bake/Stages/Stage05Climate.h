// 阶段 5：气候。温度与风带；降水 / 群系在后续任务补。
#pragma once
#include "CoreMinimal.h"
#include "PlanetStage.h"

class STELLARERAPLANET_API FStage05Climate final : public IPlanetStage
{
public:
	virtual FName GetName() const override { return TEXT("Stage05Climate"); }
	virtual uint32 GetStageIndex() const override { return 5; }
	virtual EPlanetField GetReads() const override { return EPlanetField::Elevation | EPlanetField::Continental; }
	virtual EPlanetField GetWrites() const override { return EPlanetField::Climate; }
	virtual void Bake(FPlanetBakeContext& Ctx) const override;
	virtual void DescribeDebugLayers(TArray<FGeoDebugLayer>& Out) const override;
	virtual void ExportDebugFields(const FPlanetBakeFields& Fields, TMap<FName, FGeoField2D>& Out) const override;
};
