#include "GeoMineralScatterer.h"
#include "GeoMineralCatalog.h"
#include "GeoMineralRules.h"
#include "GeoSurfaceDepositRules.h"
#include "PlanetRecipe.h"

TArray<FName> UGeoMineralScatterer::GetMineralOptions() const
{
	TArray<FName> Out;
	const UPlanetRecipe* Recipe = Cast<UPlanetRecipe>(GetOuter());
	if (Recipe && Recipe->MineralCatalog)
	{
		for (const FGeoMineralDef& D : Recipe->MineralCatalog->Minerals) { Out.Add(D.Id); }
	}
	return Out;
}

TSharedPtr<IGeoMineralRule> UGeoBifScatterer::MakeRule() const
{
	TSharedPtr<FGeoBifRule> R = MakeShared<FGeoBifRule>();
	R->P = { Count, AgeMin, ContinentalMin, LengthVoxels, WidthVoxels, ThicknessVoxels, DepthMinVoxels, DepthMaxVoxels };
	return R;
}

TSharedPtr<IGeoMineralRule> UGeoVmsScatterer::MakeRule() const
{
	TSharedPtr<FGeoVmsRule> R = MakeShared<FGeoVmsRule>();
	R->P = { Chains, LensMin, LensMax, SpacingVoxels, LensRadiusVoxels, LensThicknessVoxels, DepthMinVoxels, DepthMaxVoxels };
	return R;
}

TSharedPtr<IGeoMineralRule> UGeoNoduleScatterer::MakeRule() const
{
	TSharedPtr<FGeoNoduleRule> R = MakeShared<FGeoNoduleRule>();
	R->P = { AgeMin, ContinentalMax, Coverage, PatchFrequency, DepthVoxels };
	return R;
}

TSharedPtr<IGeoMineralRule> UGeoChromiteScatterer::MakeRule() const
{
	TSharedPtr<FGeoChromiteRule> R = MakeShared<FGeoChromiteRule>();
	R->P = { Chains, PodMin, PodMax, SpacingVoxels, PodRadiusVoxels, PodThicknessVoxels, DepthMinVoxels, DepthMaxVoxels };
	return R;
}

TSharedPtr<IGeoMineralRule> UGeoSulfurScatterer::MakeRule() const
{
	TSharedPtr<FGeoSulfurRule> R = MakeShared<FGeoSulfurRule>();
	R->P = { RadiusRatio, DepthVoxels };
	return R;
}

TSharedPtr<IGeoMineralRule> UGeoEpithermalScatterer::MakeRule() const
{
	TSharedPtr<FGeoEpithermalRule> R = MakeShared<FGeoEpithermalRule>();
	R->P = { DepthMinRatio, DepthMaxRatio, VeinCount, VeinRadiusRatio, VeinThicknessRatio, SpreadRatio };
	return R;
}

TSharedPtr<IGeoMineralRule> UGeoPorphyryScatterer::MakeRule() const
{
	TSharedPtr<FGeoPorphyryRule> R = MakeShared<FGeoPorphyryRule>();
	R->P = { DepthMinRatio, DepthMaxRatio, RadiusRatio, PerStock };
	return R;
}

TSharedPtr<IGeoMineralRule> UGeoTinTungstenScatterer::MakeRule() const
{
	TSharedPtr<FGeoTinTungstenRule> R = MakeShared<FGeoTinTungstenRule>();
	R->P = { PerBatholith, LengthRatio, RadiusRatio, ThicknessRatio, TopMinRatio, TopMaxRatio };
	return R;
}

TSharedPtr<IGeoMineralRule> UGeoPegmatiteScatterer::MakeRule() const
{
	TSharedPtr<FGeoPegmatiteRule> R = MakeShared<FGeoPegmatiteRule>();
	R->P = { PerBatholith, RadiusRatio, ShellMin, ShellMax };
	return R;
}

TSharedPtr<IGeoMineralRule> UGeoCopperNickelScatterer::MakeRule() const
{
	TSharedPtr<FGeoCopperNickelRule> R = MakeShared<FGeoCopperNickelRule>();
	R->P = { PerHost, WidthScale, ThicknessScale };
	return R;
}

TSharedPtr<IGeoMineralRule> UGeoChromiumPGEScatterer::MakeRule() const
{
	TSharedPtr<FGeoChromiumPGERule> R = MakeShared<FGeoChromiumPGERule>();
	R->P = { PerSlab, ThicknessScale };
	return R;
}

TSharedPtr<IGeoMineralRule> UGeoDiamondScatterer::MakeRule() const
{
	TSharedPtr<FGeoDiamondRule> R = MakeShared<FGeoDiamondRule>();
	R->P = { PerPipe, RadiusRatio, FertileChance };
	return R;
}

TSharedPtr<IGeoMineralRule> UGeoRareEarthScatterer::MakeRule() const
{
	TSharedPtr<FGeoRareEarthRule> R = MakeShared<FGeoRareEarthRule>();
	R->P = { PerBody, RadiusScale };
	return R;
}

TSharedPtr<IGeoMineralRule> UGeoPlacerScatterer::MakeRule() const
{
	TSharedPtr<FGeoPlacerRule> R = MakeShared<FGeoPlacerRule>();
	R->P.MinSourceExposure = MinSourceExposure;
	R->P.MaxSteps = MaxSteps;
	R->P.StepStride = StepStride;
	R->P.MaxBodiesPerSource = MaxBodiesPerSource;
	R->P.MinFlowAccumRatio = MinFlowAccumRatio;
	R->P.HalfWidthRatio = HalfWidthRatio;
	R->P.ThicknessVoxels = ThicknessVoxels;
	R->P.DecayPerVoxel = DecayPerVoxel;
	R->P.bMouthOnly = bMouthOnly;
	R->P.MouthCells = MouthCells;
	R->P.Hard = Hard;
	// 源矿按名字引用目录：找不到只跳过该名字，不能让整条烘焙失败。
	const UPlanetRecipe* Recipe = Cast<UPlanetRecipe>(GetOuter());
	const UGeoMineralCatalog* Cat = Recipe ? Recipe->MineralCatalog.Get() : nullptr;
	if (!Cat)
	{
		UE_LOG(LogTemp, Warning, TEXT("[地质] 撒矿器 %s 没有矿物目录，源矿列表为空"), *GetClass()->GetName());
		return R;
	}
	for (const FName& Name : Sources)
	{
		const int32 Index = Cat->FindIndex(Name);
		if (Index == INDEX_NONE || Index >= 255)
		{
			UE_LOG(LogTemp, Warning, TEXT("[地质] 撒矿器 %s 的源矿「%s」不在矿物目录里，跳过"), *GetClass()->GetName(), *Name.ToString());
			continue;
		}
		R->P.SourceMineralIds.Add(uint8(Index + 1));
	}
	return R;
}

TSharedPtr<IGeoMineralRule> UGeoBasinDepositScatterer::MakeRule() const
{
	TSharedPtr<FGeoBasinDepositRule> R = MakeShared<FGeoBasinDepositRule>();
	R->P.MaxPrecip = MaxPrecip;
	R->P.MinBasinCells = MinBasinCells;
	R->P.RingInner = RingInner;
	R->P.RingOuter = RingOuter;
	R->P.BodiesPerBasin = BodiesPerBasin;
	R->P.RadiusRatio = RadiusRatio;
	R->P.ThicknessVoxels = ThicknessVoxels;
	R->P.bRequireVolcano = bRequireVolcano;
	R->P.VolcanoRadiusRatio = VolcanoRadiusRatio;
	R->P.Hard = Hard;
	return R;
}

TSharedPtr<IGeoMineralRule> UGeoCoalScatterer::MakeRule() const
{
	TSharedPtr<FGeoCoalRule> R = MakeShared<FGeoCoalRule>();
	R->P.MinPrecip = MinPrecip;
	R->P.MaxElevRatio = MaxElevRatio;
	R->P.MaxSlope = MaxSlope;
	R->P.MaxContinental = MaxContinental;
	R->P.PatchCount = PatchCount;
	R->P.RadiusRatio = RadiusRatio;
	R->P.ThicknessVoxels = ThicknessVoxels;
	R->P.DepthVoxels = DepthVoxels;
	R->P.Hard = Hard;
	return R;
}

TSharedPtr<IGeoMineralRule> UGeoBlanketScatterer::MakeRule() const
{
	TSharedPtr<FGeoBlanketRule> R = MakeShared<FGeoBlanketRule>();
	R->P.Host = EGeoBlanketHost(uint8(Host));
	R->P.HostValue = HostValue;
	R->P.MinGrade = MinGrade;
	R->P.Threshold = Threshold;
	R->P.Chance = Chance;
	R->P.DepthVoxels = DepthVoxels;
	R->Stage = uint32(RuleStage);
	return R;
}

TSharedPtr<IGeoMineralRule> UGeoSupergeneScatterer::MakeRule() const
{
	TSharedPtr<FGeoSupergeneRule> R = MakeShared<FGeoSupergeneRule>();
	R->HostMineral = HostMineral;
	R->DepthBelowSurface = DepthBelowSurface;
	R->ThicknessVoxels = ThicknessVoxels;
	R->HostMargin = HostMargin;
	R->Stage = 8;
	return R;
}

TSharedPtr<IGeoMineralRule> UGeoGossanScatterer::MakeRule() const
{
	TSharedPtr<FGeoGossanRule> R = MakeShared<FGeoGossanRule>();
	R->HostMineral = HostMineral;
	R->MaxDepthVoxels = MaxDepthVoxels;
	R->CapDepthVoxels = CapDepthVoxels;
	R->RadiusScale = RadiusScale;
	R->Stage = 8;
	return R;
}
