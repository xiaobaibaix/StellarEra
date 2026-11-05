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
class UPlanetEditComponent;
class USkeletalMesh;
class USpringArmComponent;
class UVoxelSimpleInvokerComponent;
class UInputComponent;
struct FInputActionValue;

/**
 * 在体素球体行星表面行走的角色。
 *
 * 球面行走没有手写任何物理：每帧把指向球心的方向喂给
 * UCharacterMovementComponent::SetGravityDirection，CMC 自带的落地判定、
 * 斜坡、台阶、跳跃就都在“重力空间”里成立了。
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
	 * 绑角色自己的输入。
	 *
	 * 引擎只在**本地 Possess 这个 Pawn** 时调用它，所以「谁被操控，谁就响应输入」
	 * 由 Pawn 栈完成 —— 控制器里不再有 `Cast<APlanetCharacter>`。动作表从控制器
	 * （`IStellarEraInputSource`）取，因为 IMC/IA 是运行期构造的、归属控制器。
	 */
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;

	//~ Begin IVoxelViewpointInvokers Interface
	virtual void SetViewpointInvokersActive(bool bActive) override;
	//~ End IVoxelViewpointInvokers Interface

	/** 球面行走输入：X = 前后，Y = 左右，取值 [-1, 1]。 */
	void Move(const FVector2D& Value);

	/** 相机输入，单位是度。已经乘过本角色的 LookSensitivity（见 OnLookInput）。 */
	void Look(float DeltaYaw, float DeltaPitch);

	/**
	 * 滚轮缩放相机臂长。Value 是滚轮轴值，上滚为正。
	 *
	 * 与 Look 分开是因为两者的输入门不同：Look 只在按住右键时才有值
	 * （见 AStellarEraPlayerController::OnLook），缩放任何时候都响应。
	 */
	void AddZoom(float Value);

	/** 切换冲刺。 */
	void SetSprinting(bool bSprinting);

	/**
	 * 登船 / 离船。登船时隐藏并关掉碰撞——附着的 Actor 仍然参与碰撞，
	 * 不关的话角色的胶囊会卡在飞船的碰撞体里。
	 */
	void SetBoarded(bool bInBoarded);

	/**
	 * 是否在船上。
	 *
	 * **不要**再用 `GetAttachParentActor() != nullptr` 判断登船：角色平时就挂在
	 * 行星下（见 SetRidePlanet），那条式子在走路时也恒为真。
	 */
	bool IsBoarded() const { return bBoarded; }

	/**
	 * 指定角色骑在哪颗行星上。这是**唯一**写本 Actor 附着关系的地方。
	 *
	 * 非登船状态下把角色 AttachToActor 到行星，此后行星平移多少、角色就跟着
	 * 平移多少 —— 由引擎的附着层级负责，不再依赖
	 * UCosmosSimulationSubsystem::CarryRiders 每帧推位移。
	 *
	 * 登船期间父级归飞船，本函数只更新「脚下是哪颗星」。
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
	 * 编辑模式组件。**由蓝图子类 BP_PlanetCharacter 挂载**（2026-10-31 从构造函数
	 * 移出）：原生 APlanetCharacter 实例没有它，编辑功能随之消失 —— 这是「可选
	 * 玩法由蓝图提供」的预期结果，调用方全部按可能为空处理。
	 *
	 * 定义在 .cpp：本头只前向声明 UPlanetEditComponent，`FindComponentByClass`
	 * 需要完整类型，不能内联在头里。
	 */
	UPlanetEditComponent* GetPlanetEditComponent() const;

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
	// --- 输入回调（SetupPlayerInputComponent 里绑定） ---

	/** WASD。在飞船里时走不到这里 —— 飞船有自己的输入组件。 */
	void OnMoveInput(const FInputActionValue& Value);

	/** 鼠标位移。只在按住右键时真正转头，见 bLookHeld。 */
	void OnLookInput(const FInputActionValue& Value);

	/**
	 * 右键按下 / 松开。两个用途：
	 *   1. 开关视角（按住才转头，鼠标划过屏幕不会拖走画面）；
	 *   2. 编辑模式下「点放」方块 —— 按下预览切到「放」，松开时位移小于阈值才真正放置。
	 */
	void OnLookRightStarted(const FInputActionValue& Value);
	void OnLookRightCompleted(const FInputActionValue& Value);

	void OnJumpInput(const FInputActionValue& Value);
	void OnSprintStarted(const FInputActionValue& Value);
	void OnSprintCompleted(const FInputActionValue& Value);

	/**
	 * 滚轮。编辑模式下改笔刷尺寸（相机臂长保持不动，挖洞时要盯着脚下）；
	 * 其余时候缩相机臂长。
	 */
	void OnZoomInput(const FInputActionValue& Value);

	/** 当前打开的编辑组件；不在编辑模式、或蓝图没挂组件时为空。 */
	UPlanetEditComponent* GetEditComponentInEditMode() const;

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

	/**
	 * 200 米之外的过渡环，只做 LOD。
	 *
	 * 没有它们的话，角色身边 200 米以内是 LOD 0，再外面就是行星自带 Invoker 的
	 * OrbitLOD（这颗星 4 公里一块），中间一档都没有 —— 两档分辨率的表面会互相
	 * 穿插，看起来就是「大片三角面横穿地形」。
	 *
	 * **不要开碰撞**：碰撞那条细分路径不读 LODToSet，锁死 Height 0，开在公里级的
	 * 盒子上会直接把块数顶爆。
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

public:
	/**
	 * 运动动画蓝图类。留空则角色没有动画（保持参考姿势）。
	 *
	 * 走 C++ 属性而不是在蓝图里直接设 Mesh 的 Anim Class：CharacterMesh0 是
	 * ACharacter 的原生继承组件，不在蓝图的 SCS 节点里，蓝图侧的组件工具找不到它。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Character|Animation")
	TSubclassOf<UAnimInstance> CharacterAnimClass;

	/** 面部附属网格挂到身体网格的哪根骨骼上。留空则不改挂。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Character|Animation")
	FName FaceAttachBone = TEXT("Head_50");

private:
	/**
	 * 按「是当前视点 ∧ 没在船上」开关脚下的体素调用器。
	 *
	 * 两条规则必须合成一个谓词：分开写会互相拆台 —— SetBoarded(false) 会把
	 * 非当前视点的角色重新打开，而登船期间角色跟着飞船高速平移，
	 * LODToSet = 0 的 200 米盒子每帧都要重网格化。
	 *
	 * @param bBoardedIn 调用方当时知道的登船状态。三处调用点（SetBoarded /
	 *                   视点切换 / BeginPlay）都读成员 bBoarded。
	 */
	void ApplyViewpointInvokerState(bool bBoardedIn);

	/** 本 Pawn 是否当前视点。初值 false —— OnRegister 会自动启用，BeginPlay 要按它关掉。 */
	bool bIsViewpoint = false;

	/**
	 * 登船状态。**SetBoarded 是唯一写入口**。
	 *
	 * 以前这个状态是从附着关系现推的（GetAttachParentActor() != nullptr），
	 * 因为当时只有登船会设附着父级。角色改为平时挂在行星下之后那条等价关系
	 * 不成立了 —— 走路时父级就是行星 —— 所以必须存成显式的成员。
	 */
	bool bBoarded = false;

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

	/**
	 * 把挂在身体网格下、但使用不同骨架的附属网格（面部）改挂到 FaceAttachBone。
	 *
	 * 面部网格 Object_10 / Object_21 的骨架分别是 Face_30 / GLTF_created_0，
	 * 与身体的 Object_14_Skeleton 不同。它们原本直接挂在 CharacterMesh0 下，
	 * 身体动画不会带着它们走 —— 头一转，脸还留在原地。
	 */
	void AttachFaceMeshesToHead();

	/** 关卡里有没有 PlayerStart（含 PIE 的 APlayerStartPIE）。 */
	bool HasPlayerStartInLevel() const;

	/**
	 * 出生点由关卡里的 PlayerStart 决定，而不是「摆到球面正上方」。
	 *
	 * BeginPlay 时探测一次并缓存：PlayerStart 是作者摆的，运行期不会凭空出现。
	 *
	 * 为真时 Tick 里那段「落地前每帧按回球面」的兜底要一并关掉。那段兜底存在的
	 * 理由是「没有 PlayerStart 时角色生成在世界原点，也就是球心实心里」；有出生点
	 * 时这个前提不成立，再按回去反而会把角色从出生点拽到地表 —— PlayerStart 摆在
	 * 离地稍高的地方，开局登船的距离判据（BoardingRadius = 600 cm）就不成立了。
	 */
	bool bSpawnedAtPlayerStart = false;

	/** 是否已经第一次踩到地面。在此之前每帧把角色按回球面，等碰撞体流送出来。 */
	bool bHasLanded = false;

	/** 距离下次重新评估最近行星还剩多少秒。 */
	float PlanetReevaluateTimer = 0.f;

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

	/**
	 * 基准臂长。BeginPlay 里抄自当时生效的 CameraArmLength —— 那是 EditAnywhere，
	 * 蓝图或关卡实例改过的值构造函数读不到。滚轮改的是相对它的偏移，不是臂长本身，
	 * 这样蓝图设的值不会被滚轮冲掉。
	 */
	float BaseCameraArmLength = 0.f;

	/** 滚轮累计的目标偏移（厘米）。夹的是它而不是最终臂长，理由见 AddZoom。 */
	float TargetZoomOffset = 0.f;

	/** 每帧朝 TargetZoomOffset 平滑，再写进 SpringArm->TargetArmLength。 */
	float ZoomOffset = 0.f;

	// --- 输入状态 ---

	/** 鼠标灵敏度（度/像素）。 */
	UPROPERTY(EditAnywhere, Category = "Input")
	float LookSensitivity = 1.2f;

	/** 上下视角反向。觉得鼠标上下颠倒时把它改成 true。 */
	UPROPERTY(EditAnywhere, Category = "Input")
	bool bInvertLookY = false;

	/** 右键是否按住。松开时视角不响应鼠标，走动时相机不会被拖走。 */
	bool bLookHeld = false;

	/** 编辑模式右键按下的屏幕位置。松开时用它区分「点击放置」与「拖拽转视角」。 */
	FVector2D EditRightPressPosition = FVector2D::ZeroVector;

	/** 右键按下时是否记下了编辑位置（不是编辑模式 / 没拿到鼠标时为 false）。 */
	bool bEditRightPressValid = false;
};
