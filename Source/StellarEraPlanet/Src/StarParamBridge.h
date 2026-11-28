#pragma once

#include "CoreMinimal.h"
#include "ChildActorParamBridge.h"
#include "StarParamBridge.generated.h"

/**
 * 恒星的参数桥：目前只映射本体网格缩放（恒星大小）。
 * 不引用 AStarBodyActor，避免 Planet 反向依赖 Cosmos。
 */
UCLASS(EditInlineNew, DefaultToInstanced)
class STELLARERAPLANET_API UStarParamBridge : public UChildActorParamBridge
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category = "Star", meta = (DisplayName = "MeshScale", ClampMin = "0.001"))
	float MeshScale = 1.f;

	//~ Begin UChildActorParamBridge Interface
	virtual void ApplyToChild(AActor* Child) override;
	virtual bool SupportsChild(const AActor* Child) const override;
	//~ End UChildActorParamBridge Interface
};
