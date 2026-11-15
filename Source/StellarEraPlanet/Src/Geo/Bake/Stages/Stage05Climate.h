// 阶段 5：气候。温度、风带、降水（含逆风雨影）；群系在后续任务补。
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

namespace Stage05
{
	/** 风吹向的方位角（度，0 = 北，90 = 东）。信风与极地东风都是 270°。 */
	STELLARERAPLANET_API float WindAzimuth(float AbsLatDeg);
	/** 方位角 → 球面切向单位向量（风实际吹向）；极点处切向退化，返回零向量。 */
	STELLARERAPLANET_API FVector3f WindTangent(const FVector3f& Dir, float AzimuthDeg);
}
