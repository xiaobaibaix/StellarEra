#pragma once

#include "CoreMinimal.h"
#include "Components/ChildActorComponent.h"
#include "ChildActorParamBridge.h"
#include "PlanetBodyChildComponent.generated.h"

/**
 * 天体代理的子 Actor 载体：只持有一个参数桥，由桥负责把参数写进子 Actor。
 * ChildActorComponent 的模板属于类、关卡实例不可编辑；桥是 Instanced 的，随实例保存。
 */
UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class STELLARERACELESTIAL_API UPlanetBodyChildComponent : public UChildActorComponent
{
	GENERATED_BODY()

public:
	UPlanetBodyChildComponent();

	/** 子 Actor 的参数映射桥（行星/恒星各一个子类）；Instanced，逐实例可调。 */
	UPROPERTY(EditAnywhere, Instanced, Category = "Celestial",
		meta = (DisplayName = "Bridge", ShowInnerProperties))
	TObjectPtr<UChildActorParamBridge> Bridge;

	/** 把桥上的参数写进子 Actor；编辑器里同时重建世界。 */
	UFUNCTION(BlueprintCallable, Category = "Celestial")
	void ApplyBridgeToChild();

	//~ Begin UChildActorComponent Interface
	virtual void CreateChildActor(TFunction<void(AActor*)> CustomizerFunc = nullptr) override;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
	//~ End UChildActorComponent Interface
};
