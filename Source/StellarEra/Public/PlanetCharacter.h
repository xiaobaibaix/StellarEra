// Copyright Voxel Plugin SAS. All Rights Reserved.
// 在体素球体行星表面行走的角色。

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "PlanetCharacter.generated.h"

class ASpaceshipPawn;
class AVoxelPlanetActor;
class UCameraComponent;
class USkeletalMesh;
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

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	/** 球面行走输入：X = 前后，Y = 左右，取值 [-1, 1]。 */
	void Move(const FVector2D& Value);

	/** 相机输入，单位是度。由 PlayerController 乘完灵敏度后传进来。 */
	void Look(float DeltaYaw, float DeltaPitch);

	/** 切换冲刺。 */
	void SetSprinting(bool bSprinting);

	/**
	 * 登船 / 离船。登船时隐藏并关掉碰撞——附着的 Actor 仍然参与碰撞，
	 * 不关的话角色的胶囊会卡在飞船的碰撞球里。
	 */
	void SetBoarded(bool bBoarded);

	/** 找离角色最近的飞船；找不到返回 nullptr。 */
	ASpaceshipPawn* FindNearestShip() const;

	/** 把角色摆到球面正上方，胶囊底贴住表面，并把朝向摆正。 */
	void SnapToPlanetSurface();

	/** 当前所在的行星。BeginPlay 时自动查找最近的一颗。 */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Planet")
	TObjectPtr<AVoxelPlanetActor> PlanetActor;

	/** 查找行星时的最大距离（厘米）。默认 1000 公里，够覆盖任意单颗行星。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet")
	float PlanetSearchRadius = 1.0e8f;

	/**
	 * 重新评估「最近的行星」的间隔（秒）。默认 0.5。设 0 关闭。
	 *
	 * 行星在动，最近的一颗会换人。最典型的场景：开着飞船去另一颗星球、落地、
	 * 下船 —— 角色的 BeginPlay 早在母星上就跑完了，不重评的话它会一直拿旧星
	 * 当重力源，一脚踩空直接被推向十几公里外的旧星（表现为「向上掉进太空」）。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet")
	float PlanetReevaluateInterval = 0.5f;

protected:
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

	/** 行走速度（厘米/秒）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet")
	float WalkSpeed = 600.f;

	/** 冲刺速度（厘米/秒）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet")
	float SprintSpeed = 1200.f;

	/**
	 * 角色模型。留空则退回引擎自带的骷髅方块占位。
	 *
	 * 这是「具体表现」，默认留给子蓝图 BP_PlanetCharacter 在 Class Defaults 里指定，
	 * C++ 不硬编码任何项目资产路径 —— 换模型不必重新编译。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Character|Mesh")
	TObjectPtr<USkeletalMesh> CharacterMesh;

	/**
	 * 模型相对胶囊的额外位移。默认 -88 = 胶囊半高（见构造函数里的
	 * InitCapsuleSize），把网格下移到胶囊底，让模型站在地面上而不是穿进地里。
	 * 改胶囊尺寸的话这个值要跟着改。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Character|Mesh")
	FVector MeshRelativeLocation = FVector(0.f, 0.f, -88.f);

	/** 模型相对胶囊的额外旋转。用于校正 Sketchfab 模型的朝向。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Character|Mesh")
	FRotator MeshRelativeRotation = FRotator(0.f, -90.f, 0.f);

	/** 模型相对胶囊的额外缩放。用于校正 Sketchfab 模型的 100 倍缩放。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Character|Mesh")
	float MeshScale = 0.7f;

private:
	/**
	 * 把 CharacterMesh / MeshRelativeLocation / MeshRelativeRotation / MeshScale
	 * 套用到 GetMesh() 上。四个属性是一组，换模型只改这一组，不必重新编译。
	 *
	 * 必须在 OnConstruction 与 BeginPlay 里调用，不能只在构造函数里做：
	 * 蓝图 CDO 构造时会先跑一遍 C++ 构造函数，之后才加载蓝图里存的值，
	 * 构造期读到的永远是 C++ 默认值 —— 在蓝图 Class Defaults 里改这四个属性
	 * 就会静默失效。本类的 WalkSpeed 出于同样的原因在 BeginPlay 里再套用一次。
	 *
	 * 顺带一个坑：不要在子蓝图里靠「覆盖 Mesh 组件的 SkeletalMesh」来换模型。
	 * 那种覆盖记在 UInheritableComponentHandler 里，父类一旦重编译就会被
	 * 重新生成掉，模型会无声地退回占位网格。Mesh 还是 ACharacter 自带的继承
	 * 组件，比自建组件更容易踩这一脚。
	 */
	void ApplyCharacterMesh();

	/** 是否已经第一次踩到地面。在此之前每帧把角色按回球面，等碰撞体流送出来。 */
	bool bHasLanded = false;

	/** 距离下次重新评估最近行星还剩多少秒。 */
	float PlanetReevaluateTimer = 0.f;

	/** 重找最近的行星；换了就切过去并重新落到新星的地表上。 */
	void ReevaluatePlanet();

	/**
	 * 相机枢轴的“球面对齐基座”，不含玩家视角。
	 *
	 * 每帧沿球面平行移动（parallel transport）上一帧的基座，而不是用
	 * FQuat::FindBetweenNormals 重新构造：后者的扭转参考是任意选的，走到世界 Z 轴
	 * 的正对面时旋转轴退化成 0/0，画面会瞬间翻转。
	 */
	FQuat CameraBaseRotation = FQuat::Identity;

	/** 上一帧的球面法线，用于把基座平移到本帧。 */
	FVector LastSurfaceUp = FVector::ZeroVector;
};
