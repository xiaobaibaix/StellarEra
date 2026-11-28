#include "StarBodyChildComponent.h"

#include "StarParamBridge.h"
#include "PlanetSunLightComponent.h"
#include "Engine/World.h"
#include "UObject/UObjectIterator.h"

UStarBodyChildComponent::UStarBodyChildComponent()
{
	// 基类构造里建的是行星桥，这里换成恒星桥。
	Bridge = CreateDefaultSubobject<UStarParamBridge>(TEXT("StarParamBridge"));
}

#if WITH_EDITOR
void UStarBodyChildComponent::CreateChildActor(TFunction<void(AActor*)> CustomizerFunc)
{
	Super::CreateChildActor(CustomizerFunc);

	const UWorld* World = GetWorld();
	if (!World || World->IsGameWorld())
	{
		return;
	}

	// 恒星子 Actor 就位后，让同一世界里所有行星的太阳灯立刻重算（不依赖组件 Tick）。
	for (TObjectIterator<UPlanetSunLightComponent> It; It; ++It)
	{
		if (It->GetWorld() == World)
		{
			It->RefreshLightingNow();
		}
	}
}
#endif
