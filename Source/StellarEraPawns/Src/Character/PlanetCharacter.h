// Copyright Voxel Plugin SAS. All Rights Reserved.
// 在体素球体行星表面行走的角色。

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "VoxelViewpointInvokers.h"
#include "PlanetCharacter.generated.h"

class AVoxelPlanetActor;
class UCameraComponent;
class UAnimInstance;
class UPlanetCharacterInputComponent;
class UPlanetEditComponent;
class USkeletalMesh;
class USpringArmComponent;
class UVoxelSimpleInvokerComponent;
class UInputComponent;
struct FInputActionValue;

/**
 * 在体素球体行星表面行走的角色。球面行走没有手写物理：每帧把指向球心的方向喂给
 * UCharacterMovementComponent::SetGravityDirection，CMC 自带的落地/斜坡/台阶/跳跃
 * 就都在「重力空间」里成立。
 */
UCLASS()
class STELLARERAPAWNS_API APlanetCharacter : public ACharacter, public IVoxelViewpointInvokers
{
	GENERATED_BODY()

public:
	APlanetCharacter(const FObjectInitializer& ObjectInitializer);

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	/**
	 * 绑角色自己的输入。引擎只在本地 Possess 时调用，动作表从控制器（`IStellarEraInputSource`）取。
	 */
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;

	/** 键鼠输入。回调与输入状态都在组件里，本类只负责转发绑定。 */
	UPROPERTY(VisibleAnywhere, Category = "Input")
	TObjectPtr<UPlanetCharacterInputComponent> PlayerInput;

	//~ Begin IVoxelViewpointInvokers Interface
	/** 照接口实现，但**不理会** bActive：角色开合只看登船，理由见 cpp。 */
	virtual void SetViewpointInvokersActive(bool bActive) override;
	//~ End IVoxelViewpointInvokers Interface

	/** 球面行走输入：X = 前后，Y = 左右，取值 [-1, 1]。 */
	void Move(const FVector2D& Value);

	/** 相机输入，单位是度。已经乘过本角色的 LookSensitivity（见 OnLookInput）。 */
	void Look(float DeltaYaw, float DeltaPitch);

	/** 滚轮缩放相机臂长，上滚为正。与 Look 分开是因为输入门不同：缩放任何时候都响应。 */
	void AddZoom(float Value);

	/** 切换冲刺。 */
	void SetSprinting(bool bSprinting);

	/** 登船 / 离船。登船时隐藏并关碰撞 —— 附着的 Actor 仍参与碰撞，不关会卡在飞船碰撞体里。 */
	void SetBoarded(bool bInBoarded);

	/**
	 * 是否在船上。**不要**用 `GetAttachParentActor() != nullptr` 判断：角色平时就挂在
	 * 行星下，那条式子在走路时也恒为真。
	 */
	bool IsBoarded() const { return bBoarded; }

	/**
	 * 指定角色骑在哪颗行星上，是**唯一**写本 Actor 附着关系的地方。非登船时
	 * AttachToActor 到行星，平移由附着层级带着走；登船期间父级归飞船，只更新脚下星球。
	 */
	void SetRidePlanet(AVoxelPlanetActor* NewPlanet);

	/** 把角色摆到球面正上方，胶囊底贴住表面，并把朝向摆正。 */
	void SnapToPlanetSurface();

	/** 重找最近的行星；换了就切过去并重新落到新星的地表上。 */
	void ReevaluatePlanet();

	/** 当前所在的行星。BeginPlay 时自动查找最近的一颗。 */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Planet")
	TObjectPtr<AVoxelPlanetActor> PlanetActor;

	/**
	 * 编辑模式组件，**由蓝图子类 BP_PlanetCharacter 挂载**：原生实例没有它，调用方按空处理。
	 */
	UPlanetEditComponent* GetPlanetEditComponent() const;

	/** 查找行星时的最大距离（厘米）。默认 1000 公里，够覆盖任意单颗行星。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet")
	float PlanetSearchRadius = 1.0e8f;

	/** 重新评估「最近的行星」的间隔（秒），0 = 关闭。不重评的话从别的星下船后会一直拿旧星当重力源。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet")
	float PlanetReevaluateInterval = 0.5f;

protected:
	/** 在关卡里找离角色最近的一颗行星；找不到返回 nullptr。 */
	AVoxelPlanetActor* FindNearestPlanet() const;

	/** 相机枢轴，每帧设为「以球面法线为上、按 CameraYaw 偏航」的世界旋转。ControlRotation 是
	 * 世界空间的，在球体背面绕世界 Z 转会让画面翻滚。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera")
	TObjectPtr<USceneComponent> CameraPivot;

	/** 第三人称相机臂。相对相机枢轴。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera")
	TObjectPtr<USpringArmComponent> SpringArm;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera")
	TObjectPtr<UCameraComponent> Camera;

	/** 体素 Invoker：行星自己的 Invoker 关掉了碰撞，脚下要生成碰撞体必须由角色自己带一个。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Planet")
	TObjectPtr<UVoxelSimpleInvokerComponent> VoxelInvoker;

	/**
	 * 200 米之外的过渡环，只做 LOD：没有它们 LOD 0 直接跳到行星 OrbitLOD（4 公里一块），
	 * 两档表面互相穿插。**不要开碰撞**：碰撞细分不读 LODToSet，开在公里级盒子上会顶爆块数。
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Planet")
	TArray<TObjectPtr<UVoxelSimpleInvokerComponent>> FarVoxelInvokers;

	/** 相机臂长（厘米）。滚轮缩放的基准值，见 BaseCameraArmLength。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera")
	float CameraArmLength = 600.f;

	/** 滚轮每格的臂长变化量（厘米）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera")
	float ZoomStep = 200.f;

	/** 相机臂长下限（厘米）。滚轮拉到最近也不会比它更短。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera")
	float MinCameraArmLength = 300.f;

	/** 相机臂长上限（厘米）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera")
	float MaxCameraArmLength = 3000.f;

	/** 缩放的平滑速度，越大越跟手。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera")
	float ZoomInterpSpeed = 8.f;

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

	/** 角色模型。留空退回引擎占位；具体表现留给子蓝图，C++ 不硬编码资产路径。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Character|Mesh")
	TObjectPtr<USkeletalMesh> CharacterMesh;

	/**
	 * 模型相对胶囊的额外位移。默认 -88 = 胶囊半高（构造里的 InitCapsuleSize），
	 * 把网格下移到胶囊底，模型才站在地面上；改胶囊尺寸要跟着改。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Character|Mesh")
	FVector MeshRelativeLocation = FVector(0.f, 0.f, -88.f);

	/** 模型相对胶囊的额外旋转。用于校正 Sketchfab 模型的朝向。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Character|Mesh")
	FRotator MeshRelativeRotation = FRotator(0.f, -90.f, 0.f);

	/** 模型相对胶囊的额外缩放。用于校正 Sketchfab 模型的 100 倍缩放。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Character|Mesh")
	float MeshScale = 0.7f;

public:
	/**
	 * 运动动画蓝图类，留空则保持参考姿势。走 C++ 属性而不是设 Mesh 的 Anim Class：
	 * CharacterMesh0 是原生继承组件，不在蓝图的 SCS 节点里，蓝图工具找不到它。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Character|Animation")
	TSubclassOf<UAnimInstance> CharacterAnimClass;

	/** 面部附属网格挂到身体网格的哪根骨骼上。留空则不改挂。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Character|Animation")
	FName FaceAttachBone = TEXT("Head_50");

private:
	/**
	 * 按「是否登船」开关自己身上全部调用器。视点不是条件：进星图时视点归相机，但人还
	 * 站在地表上，回收脚下碰撞块等于让重力把人拽进行星内部。
	 */
	void RefreshInvokerEnabledState();

	/**
	 * 登船状态，**SetBoarded 是唯一写入口**。不能从附着关系现推：角色平时就挂在
	 * 行星下，走路时 GetAttachParentActor() 也非空。
	 */
	bool bBoarded = false;

	/**
	 * 把 CharacterMesh / MeshRelativeLocation / MeshRelativeRotation / MeshScale 套到 GetMesh() 上。
	 * 构造期读到的是 C++ 默认值，必须在 OnConstruction 与 BeginPlay 调用；也别在子蓝图里覆盖
	 * Mesh 组件的 SkeletalMesh，父类重编译会重新生成、模型退回占位网格。
	 */
	void ApplyCharacterMesh();

	/**
	 * 把挂在身体网格下、但用不同骨架的面部网格改挂到 FaceAttachBone。它们原本直接挂在
	 * CharacterMesh0 下，身体动画带不动 —— 头一转脸还留在原地。
	 */
	void AttachFaceMeshesToHead();

	/** 关卡里有没有 PlayerStart（含 PIE 的 APlayerStartPIE）。 */
	bool HasPlayerStartInLevel() const;

	/**
	 * 出生点由关卡里的 PlayerStart 决定，而不是「摆到球面正上方」；BeginPlay 探测一次并缓存。
	 * 为真时 Tick 里「落地前每帧按回球面」的兜底要关掉：有出生点时再按回去会把角色从
	 * 出生点拽到地表，PlayerStart 稍高一点就让开局登船的距离判据不成立。
	 */
	bool bSpawnedAtPlayerStart = false;

	/** 是否已经第一次踩到地面。在此之前每帧把角色按回球面，等碰撞体流送出来。 */
	bool bHasLanded = false;

	/** 距离下次重新评估最近行星还剩多少秒。 */
	float PlanetReevaluateTimer = 0.f;

	/**
	 * 相机枢轴的「球面对齐基座」，不含玩家视角。每帧沿球面平行移动上一帧的基座，而不是用
	 * FindBetweenNormals 重建：后者扭转参考任意，走到世界 Z 正对面时轴退化成 0/0、画面翻转。
	 */
	FQuat CameraBaseRotation = FQuat::Identity;

	/** 上一帧的球面法线，用于把基座平移到本帧。 */
	FVector LastSurfaceUp = FVector::ZeroVector;

	/**
	 * 基准臂长，BeginPlay 里抄自当时的 CameraArmLength（EditAnywhere，构造函数读不到蓝图值）。
	 * 滚轮改的是相对它的偏移，蓝图设的值不会被冲掉。
	 */
	float BaseCameraArmLength = 0.f;

	/** 滚轮累计的目标偏移（厘米）。夹的是它而不是最终臂长，理由见 AddZoom。 */
	float TargetZoomOffset = 0.f;

	/** 每帧朝 TargetZoomOffset 平滑，再写进 SpringArm->TargetArmLength。 */
	float ZoomOffset = 0.f;
};
