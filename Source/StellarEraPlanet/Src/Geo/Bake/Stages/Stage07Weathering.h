// 阶段 7：表生风化。只算「最外面这层皮有多厚、是什么土」，不改几何、不抬地形。
#pragma once
#include "CoreMinimal.h"
#include "PlanetStage.h"

class STELLARERAPLANET_API FStage07Weathering final : public IPlanetStage
{
public:
	virtual FName GetName() const override { return TEXT("Stage07Weathering"); }
	virtual uint32 GetStageIndex() const override { return 8; }
	virtual EPlanetField GetReads() const override { return EPlanetField::Climate | EPlanetField::Elevation; }
	virtual EPlanetField GetWrites() const override { return EPlanetField::Weathering; }
	virtual void Bake(FPlanetBakeContext& Ctx) const override;
	virtual void DescribeDebugLayers(TArray<FGeoDebugLayer>& Out) const override;
	virtual void ExportDebugFields(const FPlanetBakeFields& Fields, TMap<FName, FGeoField2D>& Out) const override;
};
