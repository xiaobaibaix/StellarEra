// Copyright Voxel Plugin SAS. All Rights Reserved.
// 在体素球体行星表面行走的角色。

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "PlanetCharacter.generated.h"

class AVoxelPlanetActor;
class UCameraComponent;
class USpringArmComponent;
class UVoxelSimpleInvokerComponent;

/**
 * 在体素球体行星表面行走的角色。
 *
 * 球面行走没有手写任何物理：每帧把指向球心的方向喂给
 * UCharacterMovementComponent::SetGravityDirection，CMC 自带的落地判定、
 * 斜坡、台阶、跳跃就都在“重力空间”里成立了。
 */
UCLASS()
class APlanetCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	APlanetCharacter();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	/** 当前所在的行星。BeginPlay 时自动查找最近的一颗。 */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Planet")
	TObjectPtr<AVoxelPlanetActor> PlanetActor;

	/** 查找行星时的最大距离（厘米）。默认 1000 公里，够覆盖任意单颗行星。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet")
	float PlanetSearchRadius = 1.0e8f;

protected:
	/** 把角色摆到球面正上方，胶囊底贴住表面，并把朝向摆正。 */
	void SnapToPlanetSurface();

	/** 在关卡里找离角色最近的一颗行星；找不到返回 nullptr。 */
	AVoxelPlanetActor* FindNearestPlanet() const;

	/**
	 * 相机枢轴。每帧被设成“以球面法线为上、按 CameraYaw 绕该轴偏航”的世界旋转。
	 * 弹簧臂挂在它下面，于是相机天然切于球面——这是唯一可行的做法，因为
	 * ControlRotation 是世界空间的，在球体背面绕世界 Z 转会让画面翻滚。
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera")
	TObjectPtr<USceneComponent> CameraPivot;

	/** 第三人称相机臂。相对相机枢轴。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera")
	TObjectPtr<USpringArmComponent> SpringArm;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera")
	TObjectPtr<UCameraComponent> Camera;

	/**
	 * 体素 Invoker。行星自己的 Invoker 显式关掉了碰撞（见 VoxelPlanetActor.cpp），
	 * 所以脚下要生成碰撞体，必须由角色自己带一个开启碰撞的 Invoker。
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Planet")
	TObjectPtr<UVoxelSimpleInvokerComponent> VoxelInvoker;

	/** 相机臂长（厘米）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera")
	float CameraArmLength = 600.f;

	/** 相机水平角（度），绕球面法线。由鼠标输入驱动。 */
	float CameraYaw = 0.f;

	/** 相机俯仰角（度），相对球面切平面。由鼠标输入驱动。 */
	float CameraPitch = 0.f;

	/** 俯仰角下限（度）。 */
	UPROPERTY(EditAnywhere, Category = "Camera")
	float MinCameraPitch = -75.f;

	/** 俯仰角上限（度）。 */
	UPROPERTY(EditAnywhere, Category = "Camera")
	float MaxCameraPitch = 75.f;

private:
	/** 是否已经第一次踩到地面。在此之前每帧把角色按回球面，等碰撞体流送出来。 */
	bool bHasLanded = false;
};
