#include "ChildActorParamBridge.h"

#include "PlanetBodyChildComponent.h"

#if WITH_EDITOR
void UChildActorParamBridge::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	if (UPlanetBodyChildComponent* Component = GetTypedOuter<UPlanetBodyChildComponent>())
	{
		Component->ApplyBridgeToChild();
	}
}
#endif
