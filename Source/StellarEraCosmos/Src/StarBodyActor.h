#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "StarBodyActor.generated.h"

class USceneComponent;
class UStaticMeshComponent;
class UCosmosOrbitComponent;
class UStarBodyComponent;

/**
 * 一颗恒星：一个自发光球 + 两个组件。
 *
 * 这个类必须在 Cosmos：恒星身份跨两个模块（物理 UCosmosOrbitComponent 在 Cosmos，
 * 视觉/大气 UStarBodyComponent 在 Planet），而 Planet 不允许依赖 Cosmos，只有
 * Cosmos 能同时引用两者。因此本头文件只前置声明 UStarBodyComponent，完整类型只在
 * .cpp 里 include（与 Planet 为 Private 依赖的约定一致）。
 *
 * 做成 Actor 类而不是关卡里拼组件：骨架组件缺一个就静默坏（少了 Orbit 不参与引力、
 * 少了 StarBody 大气不认它），按「Actor 组件归属口径」这种骨架写在 C++ 里。
 * 不 Tick、不自己位移：位置由 UCosmosSimulationSubsystem 每帧写回。
 */
UCLASS()
class STELLARERACOSMOS_API AStarBodyActor : public AActor
{
	GENERATED_BODY()

public:
	AStarBodyActor();

	/**
	 * 空场景根。不能是网格：恒星的视觉缩放（×200000）会沿层级传给挂在根下的星图
	 * 相机（BP 的 ChildActor）与轨迹线组件。缩放只让 StarMesh 吃。
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "StellarEra|Star")
	TObjectPtr<USceneComponent> StarRoot;

	/** 恒星本体。挂在 StarRoot 下，相对缩放决定恒星的物理外观大小。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "StellarEra|Star")
	TObjectPtr<UStaticMeshComponent> StarMesh;

	/** N 体物理身份：质量与初速在关卡实例里填。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "StellarEra|Star")
	TObjectPtr<UCosmosOrbitComponent> Orbit;

	/** 视觉 / 大气身份：大气组件靠它认出「这是一颗恒星」。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "StellarEra|Star")
	TObjectPtr<UStarBodyComponent> StarBody;
};
