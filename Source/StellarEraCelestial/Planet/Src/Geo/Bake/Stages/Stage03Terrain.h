// 阶段 3：宏观地形。在阶段 2 的陆台 / 洋盆上叠山带、海沟、平原起伏，再做热力侵蚀。尺度全部占半径比例。
#pragma once
#include "CoreMinimal.h"
#include "PlanetStage.h"

class STELLARERACELESTIAL_API FStage03Terrain final : public IPlanetStage
{
public:
	virtual FName GetName() const override { return TEXT("Stage03Terrain"); }
	virtual uint32 GetStageIndex() const override { return 3; }
	virtual EPlanetField GetReads() const override
	{
		return EPlanetField::Plates | EPlanetField::Continental | EPlanetField::Elevation;
	}
	virtual EPlanetField GetWrites() const override { return EPlanetField::Elevation | EPlanetField::Relief; }
	virtual void Bake(FPlanetBakeContext& Ctx) const override;
	virtual void DescribeDebugLayers(TArray<FGeoDebugLayer>& Out) const override;
	virtual void ExportDebugFields(const FPlanetBakeFields& Fields, TMap<FName, FGeoField2D>& Out) const override;
};
