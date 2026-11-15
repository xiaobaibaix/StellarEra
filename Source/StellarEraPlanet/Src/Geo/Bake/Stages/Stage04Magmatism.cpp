#include "Stage04Magmatism.h"
#include "GeoContinentField.h"
#include "GeoNoise.h"

void Stage04::PlaceVolcanoes(const FPlanetRecipeConfig&, const FPlanetBakeFields&, TArray<FGeoVolcano>& Out) { Out.Reset(); }
float Stage04::Profile(const FGeoVolcano&, float, float& OutCrater) { OutCrater = 0.f; return 0.f; }

void FStage04Magmatism::Bake(FPlanetBakeContext& Ctx) const
{
	FPlanetBakeFields& F = Ctx.Fields;
	if (F.Elevation.Res <= 0 || F.PlateId.Res <= 0 || F.ReliefType.Res <= 0)
	{
		Ctx.Report.Errors.Add(TEXT("[地质] 阶段 4：缺阶段 2/3 的高程、板块或地形场"));
		return;
	}
	Stage04::PlaceVolcanoes(Ctx.Config, F, F.Volcanoes);
}
void FStage04Magmatism::DescribeDebugLayers(TArray<FGeoDebugLayer>&) const {}
void FStage04Magmatism::ExportDebugFields(const FPlanetBakeFields&, TMap<FName, FGeoField2D>&) const {}
