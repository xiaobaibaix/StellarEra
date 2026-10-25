// Copyright Voxel Plugin SAS. All Rights Reserved.

#include "PlanetGeologyPreset.h"

#if WITH_EDITOR
void UPlanetGeologyPreset::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	++Revision;
	OnPresetChanged.Broadcast(this);
}
#endif
