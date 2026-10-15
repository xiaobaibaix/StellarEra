// Copyright Voxel Plugin SAS. All Rights Reserved.

#include "CosmosOrbitComponent.h"

UCosmosOrbitComponent::UCosmosOrbitComponent()
{
	// 本组件自己不 tick：推进由 UCosmosSimulationSubsystem 统一做。
	PrimaryComponentTick.bCanEverTick = false;
}
