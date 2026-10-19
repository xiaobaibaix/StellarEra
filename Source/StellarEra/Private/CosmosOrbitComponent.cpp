// Copyright Voxel Plugin SAS. All Rights Reserved.

#include "CosmosOrbitComponent.h"

#include "CosmosSimulationSubsystem.h"
#include "CosmosTypes.h"
#include "Engine/World.h"

UCosmosOrbitComponent::UCosmosOrbitComponent()
{
	// 物理推进仍归 UCosmosSimulationSubsystem 统一做。本组件开 Tick 只为了刷新
	// 三个只读回显（AU / km/s / M☉）—— 它在 PostPhysics 跑，读到的是本帧写回
	// 之后的最终状态；它不碰求解器、不写物理。
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PostPhysics;
	bTickInEditor = true;   // 组件级开关：编辑器态也要刷新只读回显
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

void UCosmosOrbitComponent::TickComponent(float DeltaTime, ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	MassSolarReadout = MassEarth * CosmosUnits::EarthMass;
	SolverVelocityKmPerSecond = InitialVelocityKmPerSecond;

	double CmPerAu = 0.0;
	if (const UWorld* World = GetWorld())
	{
		if (const UCosmosSimulationSubsystem* Subsystem =
			World->GetSubsystem<UCosmosSimulationSubsystem>())
		{
			CmPerAu = Subsystem->CentimetersPerAu;

			if (SimulatorIndex != INDEX_NONE)
			{
				SolverVelocityKmPerSecond = CosmosUnits::AuPerYearToKilometerPerSecond(
					Subsystem->GetBodyVelocityGameUnits(SimulatorIndex));
			}
		}
	}

	// 编辑器世界没有 Cosmos 子系统（DoesSupportWorldType 只放 Game/PIE），
	// 退回同一份配置的 CDO —— 不猜标定值。
	if (CmPerAu <= 0.0)
	{
		CmPerAu = GetDefault<UCosmosSimulationSubsystem>()->CentimetersPerAu;
	}

	if (const AActor* Owner = GetOwner())
	{
		if (CmPerAu > 0.0)
		{
			PositionAu = Owner->GetActorLocation() / CmPerAu;
		}
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
