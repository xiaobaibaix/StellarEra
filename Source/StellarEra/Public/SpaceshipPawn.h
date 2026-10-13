// Copyright Voxel Plugin SAS. All Rights Reserved.
// 街机式太空飞船。

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "SpaceshipPawn.generated.h"

class UCameraComponent;
class UFloatingPawnMovement;
class USphereComponent;
class USpringArmComponent;
class UStaticMeshComponent;

/**
 * 街机式太空飞船。
 *
 * 位移交给 UFloatingPawnMovement（自带扫掠、加速度、阻尼），手感写在 Tick 里：
 * 鼠标给的是角速度冲量而不是直接设朝向，松手后指数衰减——这是“开飞机”的
 * 惯性感来源，也是《戴森球计划》伊卡洛斯的做法。
 */
UCLASS()
class ASpaceshipPawn : public APawn
{
	GENERATED_BODY()

public:
	ASpaceshipPawn();

	virtual void Tick(float DeltaSeconds) override;

	/** 主推力输入（-1 = 反推，+1 = 前进）。 */
	void SetThrust(float Value);

	/** 横滚输入（-1 / +1）。 */
	void SetRoll(float Value);

	/** 升降输入（-1 = 降，+1 = 升）。 */
	void SetVertical(float Value);

	/** 鼠标输入（度）。以冲量形式累加进角速度，松手后衰减。 */
	void Look(float DeltaYaw, float DeltaPitch);

	/** 速度低于阈值且机身下方命中地面时为真。 */
	UFUNCTION(BlueprintPure, Category = "Spaceship")
	bool IsLanded() const;

protected:
	/** 碰撞球：既是根组件也是 UFloatingPawnMovement 扫掠用的形状。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Spaceship")
	TObjectPtr<USphereComponent> Collision;

	/** 外壳网格。占位用引擎立方体，导入真实模型后替换。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Spaceship")
	TObjectPtr<UStaticMeshComponent> Hull;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Spaceship")
	TObjectPtr<UFloatingPawnMovement> FloatingMovement;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera")
	TObjectPtr<USpringArmComponent> SpringArm;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera")
	TObjectPtr<UCameraComponent> Camera;

	/** 偏航角速度上限（度/秒）。 */
	UPROPERTY(EditAnywhere, Category = "Spaceship|Handling")
	float MaxYawRate = 90.f;

	/** 俯仰角速度上限（度/秒）。 */
	UPROPERTY(EditAnywhere, Category = "Spaceship|Handling")
	float MaxPitchRate = 70.f;

	/** 横滚角速度（度/秒），由 A/D 直接驱动，不带惯性。 */
	UPROPERTY(EditAnywhere, Category = "Spaceship|Handling")
	float MaxRollRate = 120.f;

	/** 鼠标位移到角速度的换算系数（度/秒 每度）。越大越灵敏。 */
	UPROPERTY(EditAnywhere, Category = "Spaceship|Handling")
	float MouseImpulseScale = 6.f;

	/** 角速度衰减速率（FInterpTo 速率）。越小惯性越久。 */
	UPROPERTY(EditAnywhere, Category = "Spaceship|Handling")
	float AngularDamping = 3.5f;

	/** 自动回正速率（度/秒）。只回正横滚，不动玩家给的俯仰。0 = 关闭。 */
	UPROPERTY(EditAnywhere, Category = "Spaceship|Handling")
	float AutoLevelSpeed = 45.f;

	/** 判定“已落地”的速度阈值（厘米/秒）。 */
	UPROPERTY(EditAnywhere, Category = "Spaceship|Landing")
	float LandingSpeedThreshold = 200.f;

	/** 落地射线的长度（厘米）。 */
	UPROPERTY(EditAnywhere, Category = "Spaceship|Landing")
	float LandingProbeDistance = 600.f;

private:
	float ThrustInput = 0.f;
	float RollInput = 0.f;
	float VerticalInput = 0.f;
	float YawRate = 0.f;
	float PitchRate = 0.f;
};
