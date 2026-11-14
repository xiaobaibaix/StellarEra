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
