// Copyright Voxel Plugin SAS. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "PlanetCharacterMovementComponent.generated.h"

/**
 * 行星角色的移动组件，存在的唯一理由：关掉引擎的「移动底座携带」
 * （UCharacterMovementComponent::UpdateBasedMovement）。行星以 km/s 移动、距世界原点约
 * 8×10⁵ cm，为「几米/秒的电梯平台」设计的底座逻辑会把角色发射出去，或第一次生效时
 * 单帧瞬移约 8×10⁵ cm。关掉后角色由行星附着关系搬运（SetRidePlanet），CarryRiders 只兜底。
 * 底座本身照旧由 CMC 设置，MOVE_Walking、地板、摩擦、台阶、跳跃完全不受影响。
 */
UCLASS()
class STELLARERAPAWNS_API UPlanetCharacterMovementComponent : public UCharacterMovementComponent
{
	GENERATED_BODY()

public:
	/**
	 * 空实现：引擎靠它把角色跟着移动底座搬走，在行星上会瞬移或发射；搬运改由附着层级负责。
	 * 不要顺手去动 OldBaseLocation —— SaveBaseLocation() 会紧跟本函数维护它。
	 */
	virtual void UpdateBasedMovement(float DeltaSeconds) override;
};
