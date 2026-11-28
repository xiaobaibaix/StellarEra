#include "StarBodyChildComponent.h"

#include "StarParamBridge.h"

UStarBodyChildComponent::UStarBodyChildComponent()
{
	// 基类构造里建的是行星桥，这里换成恒星桥。
	Bridge = CreateDefaultSubobject<UStarParamBridge>(TEXT("StarParamBridge"));
}
