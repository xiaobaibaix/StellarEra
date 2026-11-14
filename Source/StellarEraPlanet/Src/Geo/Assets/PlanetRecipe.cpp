// 游戏线程把 UObject 字段拷成 POD，烘焙期不再碰 UObject。
#include "PlanetRecipe.h"
#include "GeoMineralCatalog.h"
#include "GeoMineralScatterer.h"

FPlanetRecipeConfig UPlanetRecipe::ToConfig(double RadiusVoxels) const
{
	check(IsInGameThread());
	FPlanetRecipeConfig C;
	C.Radius = RadiusVoxels;
	// 面板是 int32；经 uint32 扩宽，负种子也得到稳定的 64 位值。
	C.Seed = uint64(uint32(Seed));
	C.FaceRes = FaceRes;
	C.Sphere.CrustRatio = CrustRatio;
	C.Sphere.UpperMantleRatio = UpperMantleRatio;
	C.Sphere.TransitionRatio = TransitionRatio;
	C.Sphere.MantleOuterRatio = MantleOuterRatio;
	C.Sphere.OuterCoreRatio = OuterCoreRatio;
	C.Sphere.MohoWobbleRatio = MohoWobbleRatio;
	C.Sphere.WobbleOctaves = WobbleOctaves;
	// 只覆盖暴露项，其余形状常数保持 FPlanetContinentParams 默认。
	C.Plates.PlateCount = PlateCount;
	C.Plates.WarpAmp = BoundaryWarp;
	C.Plates.DetailWarpAmp = BoundaryDetail;
	C.Continents.FrameDegree = FrameDegree;
	C.Continents.LandFrac = LandFrac;
	C.Continents.CoastRoughness = CoastRoughness;
	C.Continents.ShelfFrac = ShelfFrac;
	C.Continents.LandHeightRatio = LandHeightRatio;
	C.Continents.OceanDepthRatio = OceanDepthRatio;
	C.Terrain.MountainHeightRatio = MountainHeightRatio;
	C.Terrain.TrenchDepthRatio = TrenchDepthRatio;
	C.Terrain.PlainReliefRatio = PlainReliefRatio;
	C.Terrain.BeltWidthRatio = BeltWidthRatio;
	C.Terrain.RidgeWavelengthRatio = RidgeWavelengthRatio;
	C.Terrain.ErosionIterations = ErosionIterations;
	C.Terrain.ErosionStrength = ErosionStrength;
	C.Terrain.TalusAngleDeg = TalusAngleDeg;
	C.Terrain.TrenchSedimentFill = TrenchSedimentFill;
	C.Terrain.TrenchSlump = TrenchSlump;
	C.Terrain.TrenchWedge = TrenchWedge;
	if (MineralCatalog)
	{
		for (const FGeoMineralDef& D : MineralCatalog->Minerals)
		{
			C.Minerals.Add({ D.DisplayName.IsEmpty() ? D.Id.ToString() : D.DisplayName, D.DebugColor.ToFColorSRGB() });
		}
	}
	for (const UGeoMineralScatterer* S : MineralScatterers)
	{
		if (!S || !S->bEnabled) { continue; }
		const int32 Index = MineralCatalog ? MineralCatalog->FindIndex(S->Mineral) : INDEX_NONE;
		if (Index == INDEX_NONE || Index >= 255)
		{
			UE_LOG(LogTemp, Warning, TEXT("[地质] 配方 %s：撒矿器 %s 的矿物「%s」不在矿物目录里，跳过"),
				*GetName(), *S->GetClass()->GetName(), *S->Mineral.ToString());
			continue;
		}
		TSharedPtr<IGeoMineralRule> Rule = S->MakeRule();
		if (!Rule) { continue; }
		Rule->MineralId = uint8(Index + 1);
		Rule->SeedOffset = uint32(S->SeedOffset);
		Rule->GradeMin = uint8(FMath::Clamp(S->GradeMin, 1, 255));
		Rule->GradeMax = uint8(FMath::Clamp(S->GradeMax, 1, 255));
		C.MineralRules.Add(Rule);
	}
	return C;
}
