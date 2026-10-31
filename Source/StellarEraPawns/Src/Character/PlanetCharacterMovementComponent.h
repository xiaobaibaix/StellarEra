// Copyright Voxel Plugin SAS. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "PlanetCharacterMovementComponent.generated.h"

/**
 * 行星角色的移动组件。
 *
 * 存在的唯一理由：把引擎的「移动底座携带」（UCharacterMovementComponent::UpdateBasedMovement）
 * 关掉。
 *
 * 行星以 km/s 移动、且距世界原点约 8×10⁵ cm，引擎那套为「几米/秒的电梯平台」设计的
 * 底座逻辑在这里会踩两个坑（详见
 * docs/superpowers/specs/2026-10-17-planet-rider-design.md §3）：
 *
 *   1. 离开底座时 ApplyImpartedMovementBaseVelocity() 把**底座自己的速度**加进角色的
 *      Velocity（CharacterMovementComponent.cpp:1463 → :2747）。行星速度是 km/s，
 *      角色被发射。这一条靠 APlanetCharacter 构造函数里的四个 bImpartBase* 开关关掉。
 *   2. 底座第一次变有效那一帧，LinearDelta = NewBaseLocation - OldBaseLocation
 *      （CharacterMovementComponent.cpp:2621），而 OldBaseLocation 初值是零向量
 *      （同文件 :832），于是位移量等于底座的绝对世界坐标 —— 单帧把胶囊瞬移约 8×10⁵ cm。
 *      这一条只能靠重写本函数关掉。
 *
 * 两条都关掉之后，角色的搬运全部交给 UCosmosSimulationSubsystem::CarryRiders，恰好一处。
 * 底座本身照旧由 CMC 设置，所以 MOVE_Walking、地板判定、摩擦、台阶、跳跃这些语义
 * **完全不受影响** —— 本类只拿走「底座会移动我」这一件事。
 */
UCLASS()
class STELLARERAPAWNS_API UPlanetCharacterMovementComponent : public UCharacterMovementComponent
{
	GENERATED_BODY()

public:
	/**
	 * 空实现。引擎靠它把角色跟着移动的底座一起搬走；在行星上这会把角色瞬移或发射出去，
	 * 改由 CarryRiders 承担。
	 *
	 * 不要顺手去动 OldBaseLocation：SaveBaseLocation()（同文件 :3201）在
	 * PostPhysicsTickComponent（同文件 :1834）里紧跟本函数调用，本函数空实现不影响
	 * 它对 OldBaseLocation 的正常维护。
	 */
	virtual void UpdateBasedMovement(float DeltaSeconds) override;
};
