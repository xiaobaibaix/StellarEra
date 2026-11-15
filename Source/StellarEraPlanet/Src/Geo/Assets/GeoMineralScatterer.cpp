#include "GeoMineralScatterer.h"
#include "GeoMineralCatalog.h"
#include "GeoMineralRules.h"
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
