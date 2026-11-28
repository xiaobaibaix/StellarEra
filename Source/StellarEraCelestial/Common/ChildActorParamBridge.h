#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "ChildActorParamBridge.generated.h"

class AActor;

/**
 * 子 Actor 参数映射的多态管道：每个子 Actor 类型一个子类，组件只调 ApplyToChild，
 * 不再认识具体子类型。Instanced 挂载 → 每个代理实例可各自调参。
 */
UCLASS(Abstract, EditInlineNew, DefaultToInstanced)
class STELLARERACELESTIAL_API UChildActorParamBridge : public UObject
{
	GENERATED_BODY()

public:
	/** 把本对象上的参数写进子 Actor；不负责重建，由组件决定何时重建。 */
	virtual void ApplyToChild(AActor* Child) {}

	/** 从子 Actor 回读当前值（编辑器“从子对象拉取”用）。 */
	virtual void PullFromChild(AActor* Child) {}

	/** 这个桥能否驱动该子 Actor。 */
	virtual bool SupportsChild(const AActor* Child) const { return true; }

#if WITH_EDITOR
	// 改桥上的参数时通知所属组件重新应用（组件负责重建世界）。
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
};
