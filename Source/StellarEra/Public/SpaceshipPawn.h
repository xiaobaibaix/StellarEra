// Copyright Voxel Plugin SAS. All Rights Reserved.
// 街机式太空飞船。

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "SpaceshipPawn.generated.h"

class APlanetCharacter;
class AVoxelPlanetActor;
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

	virtual void BeginPlay() override;
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

	/** 角色是否近到可以登船。 */
	UFUNCTION(BlueprintPure, Category = "Spaceship")
	bool CanBoardFrom(const FVector& CharacterLocation) const;

	/** 记录登船的角色；传 nullptr 表示离船。 */
	void SetBoardedCharacter(APlanetCharacter* Character);

	/** 当前在船上的角色；没有则为 nullptr。 */
	APlanetCharacter* GetBoardedCharacter() const { return BoardedCharacter; }

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

	/**
	 * 自动回正的重力参考行星。BeginPlay 时自动查找最近的一颗；找不到（纯太空关卡）
	 * 时自动回正退回世界 Z。软依赖：缺了它飞船照常飞。
	 */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Spaceship")
	TObjectPtr<AVoxelPlanetActor> PlanetActor;

	/** 查找行星时的最大距离（厘米）。默认 1000 公里，够覆盖任意单颗行星。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spaceship")
	float PlanetSearchRadius = 1.0e8f;

	/** 判定“已落地”的速度阈值（厘米/秒）。 */
	UPROPERTY(EditAnywhere, Category = "Spaceship|Landing")
	float LandingSpeedThreshold = 200.f;

	/** 落地射线的长度（厘米）。 */
	UPROPERTY(EditAnywhere, Category = "Spaceship|Landing")
	float LandingProbeDistance = 600.f;

	/** 登船触发半径（厘米）。 */
	UPROPERTY(EditAnywhere, Category = "Spaceship|Boarding")
	float BoardingRadius = 600.f;

	/** 模型相对碰撞球的额外旋转。用于校正模型朝向。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spaceship|Mesh")
	FRotator MeshRelativeRotation = FRotator::ZeroRotator;

	/** 模型相对碰撞球的额外缩放。用于把模型缩放到 6 米左右。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spaceship|Mesh")
	float MeshScale = 1.f;

private:
	float ThrustInput = 0.f;
	float RollInput = 0.f;
	float VerticalInput = 0.f;
	float YawRate = 0.f;
	float PitchRate = 0.f;

	/** 在关卡里找离飞船最近的一颗行星；找不到返回 nullptr。 */
	AVoxelPlanetActor* FindNearestPlanet() const;

	/**
	 * 自动回正的目标“上”：有行星时取该行星的径向外侧（飞船停在球面任何位置都
	 * 贴合当地水平面），太空中退回世界 Z。
	 */
	FVector GetAutoLevelUp() const;

	/** 当前在船上的角色。 */
	UPROPERTY(Transient)
	TObjectPtr<APlanetCharacter> BoardedCharacter;
};
