#pragma once

#include "CoreMinimal.h"
#include "PlanetBodyChildComponent.h"
#include "StarBodyChildComponent.generated.h"

/**
 * 恒星代理的子 Actor 载体：默认桥换成 UStarParamBridge。
 * 桥必须用 CreateDefaultSubobject 创建，否则实例的 archetype 会指向私有对象、存盘失败。
 */
UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class STELLARERACELESTIAL_API UStarBodyChildComponent : public UPlanetBodyChildComponent
{
	GENERATED_BODY()

public:
	UStarBodyChildComponent();
};
