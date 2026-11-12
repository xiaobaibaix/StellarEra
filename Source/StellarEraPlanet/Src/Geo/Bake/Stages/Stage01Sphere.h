// 阶段 1：圈层与莫霍面起伏。不产生地表起伏，只回答「深度比例 → 岩性」。
#pragma once
#include "CoreMinimal.h"
#include "PlanetStage.h"

class FStage01Sphere final : public IPlanetStage
{
public:
	virtual FName GetName() const override { return TEXT("Stage01Sphere"); }
	virtual uint32 GetStageIndex() const override { return 1; }
	virtual EPlanetField GetReads() const override { return EPlanetField::None; }
	virtual EPlanetField GetWrites() const override { return EPlanetField::Radial | EPlanetField::MohoWobble; }
	virtual void Bake(FPlanetBakeContext& Ctx) const override;
};
