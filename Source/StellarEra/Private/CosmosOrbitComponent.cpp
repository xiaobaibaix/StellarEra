// Copyright Voxel Plugin SAS. All Rights Reserved.

#include "CosmosOrbitComponent.h"

#include "CosmosSimulationSubsystem.h"
#include "Engine/World.h"

UCosmosOrbitComponent::UCosmosOrbitComponent()
{
	// 本组件自己不 tick：推进由 UCosmosSimulationSubsystem 统一做。
	PrimaryComponentTick.bCanEverTick = false;
}

void UCosmosOrbitComponent::BeginPlay()
{
	Super::BeginPlay();

	// 组件自己找上门注册，而不是让子系统去遍历全关卡的 Actor：
	// 遍历全世界既慢，又会漏掉运行期生成的 Actor。
	if (const UWorld* OwningWorld = GetWorld())
	{
		if (UCosmosSimulationSubsystem* CosmosSubsystem = OwningWorld->GetSubsystem<UCosmosSimulationSubsystem>())
		{
			CosmosSubsystem->RegisterBody(this);
		}
	}
}

void UCosmosOrbitComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (const UWorld* OwningWorld = GetWorld())
	{
		if (UCosmosSimulationSubsystem* CosmosSubsystem = OwningWorld->GetSubsystem<UCosmosSimulationSubsystem>())
		{
			CosmosSubsystem->UnregisterBody(this);
		}
	}

	Super::EndPlay(EndPlayReason);
}
