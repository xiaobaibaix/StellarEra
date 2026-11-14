#include "GeoMineralCatalog.h"

int32 UGeoMineralCatalog::FindIndex(FName Id) const
{
	return Minerals.IndexOfByPredicate([Id](const FGeoMineralDef& D) { return D.Id == Id; });
}
