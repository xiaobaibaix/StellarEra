#include "StarBodyComponent.h"

#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "EngineUtils.h"

UStarBodyComponent::UStarBodyComponent()
{
	// 只存参数，不参与 Tick。
	PrimaryComponentTick.bCanEverTick = false;
}

UStarBodyComponent* UStarBodyComponent::FindNearest(const UWorld* World, const FVector& FromLocation)
{
	if (!World)
	{
		return nullptr;
	}

	UStarBodyComponent* Nearest = nullptr;
	double NearestSquared = TNumericLimits<double>::Max();

	// TActorIterator 只收非 const UWorld*。这里不改世界，const_cast 是有意的。
	for (TActorIterator<AActor> It(const_cast<UWorld*>(World)); It; ++It)
	{
		UStarBodyComponent* Star = It->FindComponentByClass<UStarBodyComponent>();
		if (!Star)
		{
			continue;
		}

		const double Squared = FVector::DistSquared(Star->GetOwner()->GetActorLocation(), FromLocation);
		if (Squared < NearestSquared)
		{
			NearestSquared = Squared;
			Nearest = Star;
		}
	}

	return Nearest;
}
