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
	C.Plates.SpeedMin = PlateSpeedMin;
	C.Plates.SpeedMax = PlateSpeedMax;
	C.Plates.TransformShearRatio = TransformShearRatio;
	for (const FGeoPlateOverride& O : PlateOverrides)
	{
		C.Plates.Overrides.Add({ O.PlateIndex, O.bOverrideSpeed, O.Speed, O.bOverrideDirection, O.DirectionDeg });
	}
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
	C.Terrain.SpeedWidthEffect = SpeedWidthEffect;
	C.Terrain.SpeedHeightEffect = SpeedHeightEffect;
	C.Terrain.MidOceanRidgeHeightRatio = MidOceanRidgeHeightRatio;
	C.Terrain.MidOceanRidgeWidthRatio = MidOceanRidgeWidthRatio;
	C.Terrain.RiftDepthRatio = RiftDepthRatio;
	C.Terrain.RiftWidthRatio = RiftWidthRatio;
	C.Terrain.OldOrogenHeightRatio = OldOrogenHeightRatio;
	C.Terrain.OldOrogenDensity = OldOrogenDensity;
	C.Terrain.OldOrogenWidthRatio = OldOrogenWidthRatio;
	C.Terrain.OldOrogenSpurStrength = OldOrogenSpurStrength;
	C.Terrain.OldOrogenCoverage = OldOrogenCoverage;
	C.Terrain.OldOrogenGapFraction = OldOrogenGapFraction;
	C.Terrain.OldOrogenWidthVariation = OldOrogenWidthVariation;
	C.Volcano.bArcVolcanoes = bArcVolcanoes;
	C.Volcano.bHotspots = bHotspots;
	C.Volcano.bFloodBasalt = bFloodBasalt;
	C.Volcano.bCalderas = bCalderas;
	C.Volcano.bCinderCones = bCinderCones;
	C.Volcano.StratoBaseRatio = StratoBaseRatio;
	C.Volcano.StratoHeightRatio = StratoHeightRatio;
	C.Volcano.StratoCraterRatio = StratoCraterRatio;
	C.Volcano.CraterDepthRatio = CraterDepthRatio;
	C.Volcano.ArcSpacingRatio = ArcSpacingRatio;
	C.Volcano.ArcOffsetRatio = ArcOffsetRatio;
	C.Volcano.ArcActiveFraction = ArcActiveFraction;
	C.Volcano.ShieldBaseRatio = ShieldBaseRatio;
	C.Volcano.ShieldHeightRatio = ShieldHeightRatio;
	C.Volcano.HotspotCount = HotspotCount;
	C.Volcano.HotspotSpacingRatio = HotspotSpacingRatio;
	C.Volcano.HotspotChainLength = HotspotChainLength;
	C.Volcano.FloodBasaltChance = FloodBasaltChance;
	C.Volcano.FloodBasaltSizeRatio = FloodBasaltSizeRatio;
	C.Volcano.FloodBasaltHeightRatio = FloodBasaltHeightRatio;
	C.Volcano.CalderaFraction = CalderaFraction;
	C.Volcano.CalderaRadiusRatio = CalderaRadiusRatio;
	C.Volcano.CalderaDepthRatio = CalderaDepthRatio;
	C.Volcano.CinderPerVolcano = CinderPerVolcano;
	C.Volcano.CinderBaseRatio = CinderBaseRatio;
	C.Volcano.CinderHeightRatio = CinderHeightRatio;
	C.Intrusion.bBatholiths = bBatholiths;
	C.Intrusion.bStocks = bStocks;
	C.Intrusion.bDikes = bDikes;
	C.Intrusion.bSills = bSills;
	C.Intrusion.bLayeredIntrusions = bLayeredIntrusions;
	C.Intrusion.bKimberlites = bKimberlites;
	C.Intrusion.bAlteration = bAlteration;
	C.Intrusion.BatholithLongRatio = BatholithLongRatio;
	C.Intrusion.BatholithShortRatio = BatholithShortRatio;
	C.Intrusion.BatholithThickRatio = BatholithThickRatio;
	C.Intrusion.BatholithTopDepthRatio = BatholithTopDepthRatio;
	C.Intrusion.BatholithMinVolcanoes = BatholithMinVolcanoes;
	C.Intrusion.StockRadiusRatio = StockRadiusRatio;
	C.Intrusion.StockTopDepthRatio = StockTopDepthRatio;
	C.Intrusion.DikePerRift = DikePerRift;
	C.Intrusion.DikeThicknessRatio = DikeThicknessRatio;
	C.Intrusion.DikeLengthRatio = DikeLengthRatio;
	C.Intrusion.DikeTopDepthRatio = DikeTopDepthRatio;
	C.Intrusion.SillRadiusRatio = SillRadiusRatio;
	C.Intrusion.SillThicknessRatio = SillThicknessRatio;
	C.Intrusion.SillTopDepthRatio = SillTopDepthRatio;
	C.Intrusion.CratonSillDensity = CratonSillDensity;
	C.Intrusion.LayeredCount = LayeredCount;
	C.Intrusion.LayeredRadiusRatio = LayeredRadiusRatio;
	C.Intrusion.LayeredThicknessRatio = LayeredThicknessRatio;
	C.Intrusion.LayeredGapRatio = LayeredGapRatio;
	C.Intrusion.LayeredTopDepthRatio = LayeredTopDepthRatio;
	C.Intrusion.KimberliteRadiusRatio = KimberliteRadiusRatio;
	C.Intrusion.KimberliteHalfHeightRatio = KimberliteHalfHeightRatio;
	C.Intrusion.KimberliteTopDepthRatio = KimberliteTopDepthRatio;
	C.Intrusion.KimberliteMax = KimberliteMax;
	C.Intrusion.bCarbonatites = bCarbonatites;
	C.Intrusion.CarbonatiteRadiusRatio = CarbonatiteRadiusRatio;
	C.Intrusion.CarbonatiteTopDepthRatio = CarbonatiteTopDepthRatio;
	C.Intrusion.CarbonatiteChance = CarbonatiteChance;
	C.Intrusion.CarbonatiteSpacingRatio = CarbonatiteSpacingRatio;
	C.Intrusion.AlterHaloScale = AlterHaloScale;
	C.Intrusion.AlterKeratiticBand = AlterKeratiticBand;
	C.Intrusion.AlterSericiticBand = AlterSericiticBand;
	C.Intrusion.AlterArgillicBand = AlterArgillicBand;
	C.Climate.ClimateRes = ClimateRes;
	C.Climate.AxialTiltDeg = AxialTiltDeg;
	C.Climate.EquatorTemp = EquatorTemp;
	C.Climate.PoleTemp = PoleTemp;
	C.Climate.LapseRate = LapseRate;
	C.Climate.Continentality = Continentality;
	C.Climate.BaseEquator = BaseEquator;
	C.Climate.BaseDry = BaseDry;
	C.Climate.BaseMid = BaseMid;
	C.Climate.BasePole = BasePole;
	C.Climate.OceanFalloff = OceanFalloff;
	C.Climate.RainShadow = RainShadow;
	C.Climate.ShadowSteps = ShadowSteps;
	C.Climate.ShadowStride = ShadowStride;
	C.Climate.PrecipBias = PrecipBias;
	C.Climate.BiomeIceTemp = BiomeIceTemp;
	C.Climate.BiomeDesertPrecip = BiomeDesertPrecip;
	C.Climate.BiomeTundraTemp = BiomeTundraTemp;
	C.Climate.BiomeForestTemp = BiomeForestTemp;
	C.Climate.BiomeForestPrecip = BiomeForestPrecip;
	C.Climate.BiomeRainTemp = BiomeRainTemp;
	C.Climate.BiomeRainPrecip = BiomeRainPrecip;
	C.Climate.BiomeSavannaTemp = BiomeSavannaTemp;
	C.Climate.BiomeTemperatePrecip = BiomeTemperatePrecip;
	C.Hydro.bRivers = bRivers;
	C.Hydro.RiverThreshold = RiverThreshold;
	C.Hydro.CarveRatio = CarveRatio;
	C.Hydro.CarveMaxRatio = CarveMaxRatio;
	C.Hydro.ValleyWidthRatio = ValleyWidthRatio;
	C.Hydro.SlopeRef = SlopeRef;
	C.Hydro.ErosionRatio = ErosionRatio;
	C.Hydro.EndorheicPrecip = EndorheicPrecip;
	C.Hydro.FlatSlopeRatio = FlatSlopeRatio;
	C.Hydro.MountainErosionWeight = MountainErosionWeight;
	C.Exposure.bExposure = bExposure;
	C.Exposure.LiftScale = LiftScale;
	C.Exposure.MaxLiftRatio = MaxLiftRatio;
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
