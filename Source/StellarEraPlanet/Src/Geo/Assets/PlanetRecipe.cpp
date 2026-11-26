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
	// 关掉莫霍面时要把幅度一起归零：撒矿的地壳底钳制只读这个值，只填零场会让两者对不上。
	C.Sphere.MohoWobbleRatio = bRunStage1Moho ? MohoWobbleRatio : 0.f;
	C.Sphere.WobbleOctaves = WobbleOctaves;
	C.Sphere.bMoho = bRunStage1Moho;
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
	// 子折叠开关折成 0：默认全开时与旧配置逐位相同，哈希不变、不触发重烘。
	C.Terrain.MountainHeightRatio = bMountains ? MountainHeightRatio : 0.f;
	C.Terrain.TrenchDepthRatio = bTrenches ? TrenchDepthRatio : 0.f;
	C.Terrain.PlainReliefRatio = bPlains ? PlainReliefRatio : 0.f;
	C.Terrain.BeltWidthRatio = BeltWidthRatio;
	C.Terrain.RidgeWavelengthRatio = RidgeWavelengthRatio;
	C.Terrain.ErosionIterations = bErosion ? ErosionIterations : 0;
	C.Terrain.ErosionStrength = ErosionStrength;
	C.Terrain.TalusAngleDeg = TalusAngleDeg;
	C.Terrain.TrenchSedimentFill = TrenchSedimentFill;
	C.Terrain.TrenchSlump = TrenchSlump;
	C.Terrain.TrenchWedge = TrenchWedge;
	C.Terrain.SpeedWidthEffect = SpeedWidthEffect;
	C.Terrain.SpeedHeightEffect = SpeedHeightEffect;
	C.Terrain.MidOceanRidgeHeightRatio = bRidgeRift ? MidOceanRidgeHeightRatio : 0.f;
	C.Terrain.MidOceanRidgeWidthRatio = MidOceanRidgeWidthRatio;
	C.Terrain.RiftDepthRatio = bRidgeRift ? RiftDepthRatio : 0.f;
	C.Terrain.RiftWidthRatio = RiftWidthRatio;
	C.Terrain.OldOrogenHeightRatio = bOldOrogen ? OldOrogenHeightRatio : 0.f;
	C.Terrain.OldOrogenDensity = OldOrogenDensity;
	C.Terrain.OldOrogenWidthRatio = OldOrogenWidthRatio;
	C.Terrain.OldOrogenSpurStrength = OldOrogenSpurStrength;
	C.Terrain.OldOrogenCoverage = OldOrogenCoverage;
	C.Terrain.OldOrogenGapFraction = OldOrogenGapFraction;
	C.Terrain.OldOrogenWidthVariation = OldOrogenWidthVariation;
	C.Volcano.bVolcanoes = bRunStage4Volcano;
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
	C.Intrusion.bIntrusions = bRunStage4Intrusion;
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
	C.Strata.bStrata = bRunStage9;
	C.Strata.MaxThicknessRatio = MaxStrataThicknessRatio;
	C.Strata.LowlandCutoff = StrataLowlandCutoff;
	C.Strata.SandstoneShare = StrataSandstoneShare;
	C.Strata.ShaleShare = StrataShaleShare;
	C.Tectonics.bTectonics = bRunStage10;
	C.Tectonics.FaultWidthRatio = FaultWidthRatio;
	C.Tectonics.FoldWavelengthRatio = FoldWavelengthRatio;
	C.Tectonics.MetamorphicDepthRatio = MetamorphicDepthRatio;
	C.Tectonics.MarbleThreshold = MarbleThreshold;
	C.Tectonics.OrogenBoost = OrogenBoost;
	C.Weathering.bWeathering = bStage7WeatheringEnabled;
	C.Weathering.MaxDepthRatio = WeatheringMaxDepthRatio;
	C.Weathering.TemperatureWeight = WeatheringTemperatureWeight;
	C.Weathering.PrecipitationWeight = WeatheringPrecipitationWeight;
	C.Weathering.SlopePenalty = WeatheringSlopePenalty;
	C.Weathering.LateriteTemp = LateriteTemp;
	C.Weathering.LateritePrecip = LateritePrecip;
	C.Weathering.KaolinTemp = KaolinTemp;
	// 阶段级开关统一由分类总开关决定；出露(6b)/盖层(3A)/变质(3B) 仍是各自的子开关。
	const bool StageOn[10] = { bStage1SphereEnabled, bStage2PlatesEnabled, bStage3TerrainEnabled, bStage4MagmaEnabled,
		bStage5ClimateEnabled, bStage6HydroEnabled, bRunStage7, bStage7WeatheringEnabled, bRunStage9, bRunStage10 };
	// 顶层分类总开关：阶段 3 分类同时管地形/盖层/变质，阶段 6 分类同时管水文/出露。
	const bool CategoryOn[10] = { bStage1SphereEnabled, bStage2PlatesEnabled, bStage3TerrainEnabled, bStage4MagmaEnabled,
		bStage5ClimateEnabled, bStage6HydroEnabled, bStage6HydroEnabled, bStage7WeatheringEnabled,
		bStage3TerrainEnabled, bStage3TerrainEnabled };
	const uint32 ScatterStages = (bScatterStage2 ? 1u << 1 : 0u) | (bScatterStage3 ? 1u << 2 : 0u)
		| (bScatterStage4 ? 1u << 3 : 0u) | (bScatterStage7 ? 1u << 6 : 0u)
		| (bScatterStage8 ? 1u << 7 : 0u) | (bScatterStage9 ? 1u << 8 : 0u) | (bScatterStage10 ? 1u << 9 : 0u);
	for (uint32 i = 0; i < 10; ++i)
	{
		if (!StageOn[i] || !CategoryOn[i]) { C.DisabledStages |= (1u << i); }
	}
	if (MineralCatalog)
	{
		for (const FGeoMineralDef& D : MineralCatalog->Minerals)
		{
			C.Minerals.Add({ D.DisplayName.IsEmpty() ? D.Id.ToString() : D.DisplayName, D.DebugColor.ToFColorSRGB() });
		}
	}
	// 各阶段列表只是面板上的组织方式；运行期仍按规则自己的阶段过开关，旧数据在 PostLoad 里拆进来。
	const TArray<TObjectPtr<UGeoMineralScatterer>>* const ScatterLists[] = {
		&ScatterStage2Minerals, &ScatterStage3Minerals, &ScatterStage4Minerals, &ScatterStage7Minerals,
		&ScatterStage8Minerals, &ScatterStage9Minerals, &ScatterStage10Minerals };
	for (const TArray<TObjectPtr<UGeoMineralScatterer>>* List : ScatterLists)
	for (const UGeoMineralScatterer* S : *List)
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
		// 撒矿按阶段开关：规则挂在哪一段，就由那一段的 bScatterStageN 决定。
		if (!((ScatterStages >> (Rule->GetStage() - 1)) & 1u)) { continue; }
		Rule->MineralId = uint8(Index + 1);
		Rule->SeedOffset = uint32(S->SeedOffset);
		Rule->GradeMin = uint8(FMath::Clamp(S->GradeMin, 1, 255));
		Rule->GradeMax = uint8(FMath::Clamp(S->GradeMax, 1, 255));
		C.MineralRules.Add(Rule);
	}
	return C;
}

void UPlanetRecipe::PostLoad()
{
	Super::PostLoad();
	if (MineralScatterers.IsEmpty()) { return; }
	// 旧版只有一个大数组：按规则阶段拆到各阶段列表；规则阶段只有 MakeRule 之后才知道。
	for (UGeoMineralScatterer* S : MineralScatterers)
	{
		const TSharedPtr<IGeoMineralRule> Rule = S ? S->MakeRule() : nullptr;
		if (!Rule) { continue; }
		TArray<TObjectPtr<UGeoMineralScatterer>>* Dest = nullptr;
		switch (Rule->GetStage())
		{
		case 2: Dest = &ScatterStage2Minerals; break;
		case 3: Dest = &ScatterStage3Minerals; break;
		case 4: Dest = &ScatterStage4Minerals; break;
		case 7: Dest = &ScatterStage7Minerals; break;
		case 8: Dest = &ScatterStage8Minerals; break;
		case 9: Dest = &ScatterStage9Minerals; break;
		case 10: Dest = &ScatterStage10Minerals; break;
		default: break;
		}
		if (Dest) { Dest->Add(S); }
	}
	MineralScatterers.Reset();
}
