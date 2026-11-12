// 阶段 2：板块、陆块、宏观高程。第一个改密度的阶段。
#pragma once
#include "CoreMinimal.h"
#include "PlanetStage.h"

class FStage02Plates final : public IPlanetStage
{
public:
	virtual FName GetName() const override { return TEXT("Stage02Plates"); }
	virtual uint32 GetStageIndex() const override { return 2; }
	virtual EPlanetField GetReads() const override { return EPlanetField::Radial; }
	virtual EPlanetField GetWrites() const override
	{
		return EPlanetField::Plates | EPlanetField::Continental | EPlanetField::Elevation;
	}
	virtual void Bake(FPlanetBakeContext& Ctx) const override;
};
