// Copyright Voxel Plugin SAS. All Rights Reserved.
// 球面角色的运动状态动画实例。

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "PlanetCharacterAnimInstance.generated.h"

/**
 * 把 APlanetCharacter 的移动状态翻译成状态机可读的两个属性。
 *
 * 速度必须投影到重力平面：球面上「前」是切向的，直接用 Velocity.Size()
 * 会把沿半径的跳跃/坠落速度也算成地面速度，Walk/Run 切换会在半空抽风。
 */
UCLASS()
class STELLARERACHARACTER_API UPlanetCharacterAnimInstance : public UAnimInstance
{
	GENERATED_BODY()

public:
	virtual void NativeUpdateAnimation(float DeltaSeconds) override;

	/** 速度在重力平面上的投影大小（厘米/秒）。 */
	UPROPERTY(BlueprintReadOnly, Category = "Locomotion")
	float GroundSpeed = 0.f;

	/** 角色是否在空中。 */
	UPROPERTY(BlueprintReadOnly, Category = "Locomotion")
	bool bIsFalling = false;
};
