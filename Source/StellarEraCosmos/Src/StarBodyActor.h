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
 * ── 为什么这个类在 Cosmos 而不在 Planet ────────────────────────
 * 恒星的身份跨两个模块：物理身份是 UCosmosOrbitComponent（本模块），
 * 视觉 / 大气身份是 UStarBodyComponent（Planet 模块）。Planet 不允许依赖 Cosmos，
 * 所以只有 Cosmos 能同时引用两者。
 *
 * 因此本头文件**只前置声明** UStarBodyComponent，完整类型只在 .cpp 里 include ——
 * 与 StellarEraCosmos.Build.cs 里「Planet 是 Private 依赖」的约定一致。
 * 下游模块 include 本头只会看到前置声明，够用。
 *
 * ── 为什么是一个 Actor 类而不是在关卡里拼组件 ──────────────────
 * 骨架组件缺一个就静默坏：少了 UCosmosOrbitComponent 恒星不参与引力，少了
 * UStarBodyComponent 大气不认它。按 CLAUDE.md「Actor 组件归属口径」，这种骨架
 * 写在 C++ 里，蓝图子类 / 关卡实例只改外观。
 *
 * 不 Tick、不自己位移：位置由 UCosmosSimulationSubsystem 每帧写回。
 */
UCLASS()
class STELLARERACOSMOS_API AStarBodyActor : public AActor
{
	GENERATED_BODY()

public:
	AStarBodyActor();

	/**
	 * 空场景根。
	 *
	 * 根不能是网格：恒星的视觉缩放（见构造函数，×200000）会沿层级传给挂在根下的
	 * 星图相机（BP 的 ChildActor）与轨迹线组件，把相机和轨迹一起放大。缩放只让
	 * StarMesh 吃，其余组件挂在根上保持 1 倍。
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
