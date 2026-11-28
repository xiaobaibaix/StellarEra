#include "PlanetBodyChildComponent.h"

#include "PlanetParamBridge.h"
#include "VoxelPlanetActor.h"
#include "Engine/World.h"

UPlanetBodyChildComponent::UPlanetBodyChildComponent()
{
	PrimaryComponentTick.bCanEverTick = false;

	// 默认行星桥；挂恒星代理时把 Bridge 换成 UStarParamBridge。
	Bridge = CreateDefaultSubobject<UPlanetParamBridge>(TEXT("PlanetParamBridge"));
}

void UPlanetBodyChildComponent::CreateChildActor(TFunction<void(AActor*)> CustomizerFunc)
{
	Super::CreateChildActor([this, CustomizerFunc](AActor* Child)
	{
		if (CustomizerFunc)
		{
			CustomizerFunc(Child);
		}

		if (Bridge && Bridge->SupportsChild(Child))
		{
			Bridge->ApplyToChild(Child);
		}

		// 编辑器里立刻重建预览世界；PIE 里让子 Actor 的 BeginPlay 用新参数建。
		if (const UWorld* World = GetWorld();
			World && World->WorldType == EWorldType::Editor)
		{
			if (AVoxelPlanetActor* Planet = Cast<AVoxelPlanetActor>(Child))
			{
				Planet->RebuildPlanet();
			}
		}
	});
}

#if WITH_EDITOR
void UPlanetBodyChildComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	const FName ChangedProperty = PropertyChangedEvent.GetPropertyName();
	if (ChangedProperty == GET_MEMBER_NAME_CHECKED(UPlanetBodyChildComponent, Bridge))
	{
		ApplyBridgeToChild();
	}
}
#endif

void UPlanetBodyChildComponent::ApplyBridgeToChild()
{
	AActor* Child = GetChildActor();
	if (!Child || !Bridge || !Bridge->SupportsChild(Child))
	{
		return;
	}

	Bridge->ApplyToChild(Child);

#if WITH_EDITOR
	if (const UWorld* World = GetWorld();
		World && World->WorldType == EWorldType::Editor)
	{
		if (AVoxelPlanetActor* Planet = Cast<AVoxelPlanetActor>(Child))
		{
			Planet->RebuildPlanet();
		}
	}
#endif
}
