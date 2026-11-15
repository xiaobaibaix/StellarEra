// 阶段 4：岩浆作用。4a 只做火山地形：放点（弧 / 热点链 / 裂谷 / 破火山口 / 渣锥）→ 剖面叠到最终高程。
#pragma once
#include "CoreMinimal.h"
#include "PlanetStage.h"

class STELLARERAPLANET_API FStage04Magmatism final : public IPlanetStage
{
public:
	virtual FName GetName() const override { return TEXT("Stage04Magmatism"); }
	virtual uint32 GetStageIndex() const override { return 4; }
	virtual EPlanetField GetReads() const override
	{
		return EPlanetField::Plates | EPlanetField::Continental | EPlanetField::Elevation | EPlanetField::Relief;
	}
	virtual EPlanetField GetWrites() const override { return EPlanetField::Elevation | EPlanetField::Volcanoes; }
	virtual void Bake(FPlanetBakeContext& Ctx) const override;
	virtual void DescribeDebugLayers(TArray<FGeoDebugLayer>& Out) const override;
	virtual void ExportDebugFields(const FPlanetBakeFields& Fields, TMap<FName, FGeoField2D>& Out) const override;
};

namespace Stage04
{
	/** 只读阶段 2/3 的场，按配方放出全部火山（含破火山口改写与渣锥）。 */
	STELLARERAPLANET_API void PlaceVolcanoes(const FPlanetRecipeConfig& Cfg, const FPlanetBakeFields& F, TArray<FGeoVolcano>& Out);
	/** Chord = 到火山中心的弦长（方向单位）。返回锥体增量（≥0，体素）；OutCrater = 要减去的火口深（≥0）。 */
	STELLARERAPLANET_API float Profile(const FGeoVolcano& V, float Chord, float& OutCrater);
}
