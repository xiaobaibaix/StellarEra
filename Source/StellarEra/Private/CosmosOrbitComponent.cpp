// Copyright Voxel Plugin SAS. All Rights Reserved.

#include "CosmosOrbitComponent.h"

#include "CosmosSimulationSubsystem.h"
#include "CosmosTypes.h"
#include "Engine/World.h"

UCosmosOrbitComponent::UCosmosOrbitComponent()
{
	// 组件只存参数，不 Tick：位置/速度由读取方（星图 UI）按需从 Actor 与
	// 求解器换算，不再为了两个回显字段每帧跑一遍。
	PrimaryComponentTick.bCanEverTick = false;
}

void UCosmosOrbitComponent::PostLoad()
{
	Super::PostLoad();

	// 旧字段非零 = 这份数据存于单位变更之前（2026-10-19）。
	// 新字段仍是构造默认值 → 还没迁移过。两者都查，保证重复 PostLoad 幂等。
	if (MassSolar > 0.0)
	{
		if (MassEarth == 1.0)
		{
			MassEarth = CosmosUnits::SolarMassToEarthMass(MassSolar);
		}
		MassSolar = 0.0;
	}

	if (!InitialVelocity.IsZero())
	{
		if (InitialVelocityKmPerSecond.IsZero())
		{
			InitialVelocityKmPerSecond =
				CosmosUnits::AuPerYearToKilometerPerSecond(InitialVelocity);
		}
		InitialVelocity = FVector::ZeroVector;
	}
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
