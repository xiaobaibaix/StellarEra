// Copyright Voxel Plugin SAS. All Rights Reserved.

#include "PlanetCharacter.h"

#include "PlanetCharacterMovementComponent.h"
#include "PlanetEditComponent.h"
#include "VoxelPlanetActor.h"
#include "AlwaysLocalVoxelInvokerComponent.h"
#include "VoxelViewpointInvokers.h"
#include "VoxelViewpointInvokerUtils.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerStart.h"
#include "GameFramework/SpringArmComponent.h"
#include "Animation/AnimInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "UObject/ConstructorHelpers.h"
#include "EngineUtils.h"
#include "CollisionQueryParams.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "PlanetCharacterInputComponent.h"
#include "SpaceshipPawn.h"

namespace
{
	// 占位网格的资产路径：构造函数和 ApplyCharacterMesh 都要用，写在一处免得对不上。
	const TCHAR* FallbackMeshPath = TEXT("/Engine/EngineMeshes/SkeletalCube.SkeletalCube");

	/**
	 * 角色的「屏幕射线 → 编辑目标」：普通碰撞射线。角色贴地、射线只打一千米，
	 * LineTrace 天然尊重体素碰撞也被地形挡住；星图离地表几千公里、碰撞只有几十米，
	 * 必须换解析器 —— 这正是把解析器做成可注入的原因。
	 * 语义：Visibility 优先，整条打空退 WorldStatic；命中 Actor 必须是脚下这颗星。
	 */
	bool TraceTargetForCharacter(
		APlanetCharacter& Character,
		AVoxelPlanetActor* Planet,
		const FVector& RayOrigin,
		const FVector& RayDirection,
		const float MaxDistanceCm,
		FPlanetEditTarget& OutTarget)
	{
		if (!IsValid(Planet) || !Planet->IsCreated())
		{
			return false;
		}

		const UWorld* World = Character.GetWorld();
		if (!World)
		{
			return false;
		}

		FCollisionQueryParams Params(
			SCENE_QUERY_STAT(PlanetEditBrush), /*bTraceComplex=*/true);
		Params.AddIgnoredActor(&Character);

		const FVector End = RayOrigin + RayDirection * MaxDistanceCm;

		FHitResult Hit;
		if (!World->LineTraceSingleByChannel(Hit, RayOrigin, End, ECC_Visibility, Params))
		{
			Hit = FHitResult();
			if (!World->LineTraceSingleByChannel(Hit, RayOrigin, End, ECC_WorldStatic, Params))
			{
				return false;
			}
		}

		if (Hit.GetActor() != Planet)
		{
			return false;
		}

		OutTarget.Planet = Planet;
		OutTarget.HitLocation = Hit.Location;
		OutTarget.HitNormal = Hit.ImpactNormal;
		return true;
	}
}

// SetDefaultSubobjectClass 必须写在初始化列表里（UObjectGlobals.h 有 assert）。ACharacter
// 构造会按 CharacterMovementComponentName 建 CMC，这里先换成自己的子类，Super 跑完
// GetCharacterMovement() 拿到的就是 UPlanetCharacterMovementComponent。
APlanetCharacter::APlanetCharacter(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.SetDefaultSubobjectClass<UPlanetCharacterMovementComponent>(
		ACharacter::CharacterMovementComponentName))
{
	PrimaryActorTick.bCanEverTick = true;

	GetCapsuleComponent()->InitCapsuleSize(42.f, 88.f);

	// 角色平时挂在行星下（见 SetRidePlanet），而行星大小靠 Actor 缩放做：子物体默认
	// 继承父级缩放，一有人缩放星球，胶囊、网格、相机臂与调用器的厘米量就跟着打折。
	// 让根组件忽略父级缩放（位置与旋转仍跟随），世界缩放恒为 1。
	GetCapsuleComponent()->SetAbsolute(/*bAbsoluteLocation=*/false,
		/*bAbsoluteRotation=*/false, /*bAbsoluteScale=*/true);

	// 朝向交给 CMC：PhysicsRotation 会把目标旋转换算到重力空间、在那里清零
	// 俯仰与横滚再换回世界空间，因此角色天然垂直于球面。自己再插值一次只会
	// 和它打架。这里把控制器的旋转影响全部关掉，让引擎独占朝向。
	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = false;
	bUseControllerRotationRoll = false;

	// 局部变量不能叫 CharacterMovement：那是 ACharacter 的成员，遮蔽会报 C4458。
	UCharacterMovementComponent* Movement = GetCharacterMovement();
	Movement->bOrientRotationToMovement = true;
	Movement->RotationRate = FRotator(0.f, 540.f, 0.f);
	Movement->JumpZVelocity = 600.f;
	Movement->AirControl = 0.35f;
	Movement->MaxWalkSpeed = WalkSpeed;
	Movement->MaxStepHeight = 60.f;
	// 球面是连续曲面，落地判定要给一点容差，否则下坡时会被判成“悬空”而抖。
	Movement->PerchRadiusThreshold = 0.f;
	Movement->LedgeCheckThreshold = 4.f;

	// 关掉「离开底座时把底座自己的速度加进角色 Velocity」：CMC 在 Walking→Falling 时
	// 会把行星的 km/s 速度注入角色，而 FormerBaseVelocityDecayHalfLife 默认 0、注入值
	// 不衰减，一离地就被发射。走路/摩擦/地板/台阶/跳跃都不走这条路，关掉无副作用。
	Movement->bImpartBaseVelocityX = false;
	Movement->bImpartBaseVelocityY = false;
	Movement->bImpartBaseVelocityZ = false;
	Movement->bImpartBaseAngularVelocity = false;

	CameraPivot = CreateDefaultSubobject<USceneComponent>(TEXT("CameraPivot"));
	CameraPivot->SetupAttachment(GetCapsuleComponent());
	// 位置跟随胶囊，旋转必须**不**跟随：枢轴每帧写世界旋转，而 SetWorldRotation 存的
	// 是相对父级的旋转，角色拐弯（bOrientRotationToMovement）时父级一转就带着枢轴甩。
	// 绝对旋转让 CalcNewComponentToWorld 直接取枢轴自己的旋转。
	CameraPivot->SetUsingAbsoluteRotation(true);

	SpringArm = CreateDefaultSubobject<USpringArmComponent>(TEXT("SpringArm"));
	SpringArm->SetupAttachment(CameraPivot);
	// 相机由 CameraPivot 驱动，绝不走 ControlRotation：那是世界空间的。
	SpringArm->bUsePawnControlRotation = false;
	// bInheritPitch/Yaw/Roll 必须保持引擎默认的 true。置 false 并不是“忽略父级”，
	// 而是拿弹簧臂自己的局部相对旋转覆盖该轴——那是单位阵，DesiredRot 会被清零成
	// 世界单位旋转，CameraPivot 每帧算出的朝向被完全抹掉，相机永远盯着世界 +X。
	SpringArm->TargetArmLength = CameraArmLength;
	SpringArm->SocketOffset = FVector(0.f, 0.f, 120.f);
	SpringArm->bDoCollisionTest = true;

	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(SpringArm, USpringArmComponent::SocketName);
	Camera->bUsePawnControlRotation = false;

	// 用 AlwaysLocal 版：进星图时控制器 Possess 星图相机，普通调用器会当场掉出 LOD
	// 触发整棵八叉树重建。详见 AlwaysLocalVoxelInvokerComponent.h。
	VoxelInvoker = CreateDefaultSubobject<UAlwaysLocalVoxelInvokerComponent>(TEXT("VoxelInvoker"));
	VoxelInvoker->SetupAttachment(GetCapsuleComponent());
	// LODToSet = 0 表示最精细，这是插件对“贴着表面的 Pawn”的约定用法。
	VoxelInvoker->bUseForLOD = true;
	VoxelInvoker->LODToSet = 0;
	VoxelInvoker->LODRange = 20000.f;
	VoxelInvoker->bUseForCollisions = true;
	VoxelInvoker->CollisionsRange = 20000.f;
	// 导航、事件、优先级本任务用不到，关掉省算力。
	VoxelInvoker->bUseForNavmesh = false;
	VoxelInvoker->bUseForEvents = false;
	VoxelInvoker->bUseForPriorities = false;

	// 200 米以外的过渡环。主 Invoker 只管到 200 米（叶块 32 · VoxelSize），再外面只有
	// 行星自带 OrbitLOD（这颗星 LOD 7，叶块 2 公里），中间一档都没有，两档近似不同会
	// 让粗层从细表面里戳出来。每圈 LODToSet +2、LODRange 约 ×4：加起来只多几百块。
	// 全部用 AlwaysLocal 版，理由同上。
	static constexpr struct
	{
		int32 LODToSet;
		float LODRange;
		const TCHAR* Name;
	} FarRings[] =
	{
		{ 2,   40000.f, TEXT("VoxelInvokerLOD2") }, // 400 米
		{ 4,  150000.f, TEXT("VoxelInvokerLOD4") }, // 1.5 公里
		{ 6,  600000.f, TEXT("VoxelInvokerLOD6") }, // 6 公里
		// 再外面交给行星自带的 OrbitLOD，这颗星是 LOD 8。
	};

	for (const auto& Ring : FarRings)
	{
		UAlwaysLocalVoxelInvokerComponent* NewInvoker =
			CreateDefaultSubobject<UAlwaysLocalVoxelInvokerComponent>(Ring.Name);
		NewInvoker->SetupAttachment(GetCapsuleComponent());
		NewInvoker->bUseForLOD = true;
		NewInvoker->LODToSet = Ring.LODToSet;
		NewInvoker->LODRange = Ring.LODRange;
		// 碰撞、导航、事件、优先级全关：这些环只负责把 LOD 阶梯填平。
		NewInvoker->bUseForCollisions = false;
		NewInvoker->bUseForNavmesh = false;
		NewInvoker->bUseForEvents = false;
		NewInvoker->bUseForPriorities = false;

		FarVoxelInvokers.Add(NewInvoker);
	}

	// 行星编辑组件不在 C++ 创建，由 BP_PlanetCharacter 挂载；原生实例没有，调用方按空处理。

	// C++ 只给一个引擎骷髅方块兜底，让裸放的 APlanetCharacter 不至于隐形。真实模型由子蓝图的
	// CharacterMesh / MeshRelativeLocation / MeshRelativeRotation / MeshScale 提供。
	// 这里不能写项目资产路径：蓝图 CDO 构造时先跑本构造函数，之后才加载蓝图里存的值。
	static ConstructorHelpers::FObjectFinder<USkeletalMesh> FallbackMesh(FallbackMeshPath);
	if (FallbackMesh.Succeeded())
	{
		GetMesh()->SetSkeletalMesh(FallbackMesh.Object);
	}
	else
	{
		// 连占位网格都拿不到会得到完全不可见的角色，宁可吵一句也不静默失败。
		UE_LOG(LogTemp, Warning,
			TEXT("APlanetCharacter: 找不到占位网格 %s，Mesh 将没有模型。"), FallbackMeshPath);
	}

	PlayerInput = CreateDefaultSubobject<UPlanetCharacterInputComponent>(TEXT("PlayerInput"));
}

UPlanetEditComponent* APlanetCharacter::GetPlanetEditComponent() const
{
	// 组件由蓝图挂载，这里现查不缓存：缓存时机在编辑器世界里不可靠，调用开销可忽略。
	return FindComponentByClass<UPlanetEditComponent>();
}

void APlanetCharacter::ApplyCharacterMesh()
{
	USkeletalMeshComponent* MeshComponent = GetMesh();
	if (!MeshComponent)
	{
		return;
	}

	// 有就用蓝图给的，没有就退回占位网格；不能直接 return，否则清空后旧模型会残留在 Mesh 上。
	USkeletalMesh* MeshToUse = CharacterMesh
		? CharacterMesh.Get()
		: LoadObject<USkeletalMesh>(nullptr, FallbackMeshPath);

	MeshComponent->SetSkeletalMesh(MeshToUse);
	MeshComponent->SetRelativeLocation(MeshRelativeLocation);
	MeshComponent->SetRelativeRotation(MeshRelativeRotation);
	MeshComponent->SetRelativeScale3D(FVector(MeshScale));

	// 动画类同样是 EditAnywhere、构造期读不到蓝图值；SetAnimClass 已弃用，用 SetAnimInstanceClass。
	MeshComponent->SetAnimInstanceClass(CharacterAnimClass.Get());
}

void APlanetCharacter::AttachFaceMeshesToHead()
{
	USkeletalMeshComponent* BodyMesh = GetMesh();
	const USkeletalMesh* BodyAsset =
		BodyMesh ? BodyMesh->GetSkeletalMeshAsset() : nullptr;
	const USkeleton* BodySkeleton = BodyAsset ? BodyAsset->GetSkeleton() : nullptr;
	if (!BodySkeleton || FaceAttachBone.IsNone())
	{
		return;
	}

	TArray<USkeletalMeshComponent*> MeshComponents;
	GetComponents<USkeletalMeshComponent>(MeshComponents);
	for (USkeletalMeshComponent* MeshComponent : MeshComponents)
	{
		// 只处理直接挂在身体网格下、且骨架不同的附属网格（面部）。
		if (!MeshComponent || MeshComponent == BodyMesh)
		{
			continue;
		}
		if (MeshComponent->GetAttachParent() != BodyMesh)
		{
			continue;
		}

		const USkeletalMesh* OtherAsset = MeshComponent->GetSkeletalMeshAsset();
		const USkeleton* OtherSkeleton =
			OtherAsset ? OtherAsset->GetSkeleton() : nullptr;
		if (OtherSkeleton == BodySkeleton)
		{
			continue;
		}

		// KeepWorldTransform：改挂瞬间脸不能跳位置。
		MeshComponent->AttachToComponent(
			BodyMesh, FAttachmentTransformRules::KeepWorldTransform, FaceAttachBone);
	}
}

void APlanetCharacter::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	// 外观在这里套用：OnConstruction 是编辑器里能读到蓝图覆盖值的第一个时机，改完立刻可见。
	ApplyCharacterMesh();
}

void APlanetCharacter::BeginPlay()
{
	Super::BeginPlay();

	// WalkSpeed 是 EditAnywhere，这里应用一次，否则要等到按一次冲刺才生效。
	GetCharacterMovement()->MaxWalkSpeed = WalkSpeed;

	// 再套一次：角色运行时生成，实例的 OnConstruction 未必在蓝图属性就位后跑过；重复调用幂等。
	ApplyCharacterMesh();

	// 面部网格用不同骨架，必须在身体网格有模型之后才能挂到 Head_50。
	AttachFaceMeshesToHead();

	// 臂长同理：CameraArmLength 是 EditAnywhere，构造期读不到蓝图值。基准臂长只在此抄
	// 一次，之后滚轮改的是相对它的偏移（见 AddZoom 与 Tick），不会冲掉蓝图设的值。
	if (SpringArm)
	{
		SpringArm->TargetArmLength = CameraArmLength;
		BaseCameraArmLength = CameraArmLength;
	}

	// 解析器必须在「找不到行星」的早退之前装好：它读 PlanetActor 的当前值而非安装时快照，
	// 所以先装后查没有时序问题；反过来一旦此刻没找到行星，早退就把安装也跳过了。
	if (UPlanetEditComponent* Edit = GetPlanetEditComponent())
	{
		Edit->SetTargetResolver(
			[this](const FVector& RayOrigin, const FVector& RayDirection,
				const float MaxDistanceCm, FPlanetEditTarget& OutTarget)
			{
				return TraceTargetForCharacter(
					*this, PlanetActor.Get(), RayOrigin, RayDirection, MaxDistanceCm, OutTarget);
			});
	}

	// 调用器在 OnRegister 里自动启用，那时没人告诉过我们是不是视点，这里按当前状态重应用。
	// 必须早于下面「找不到行星」的早退，否则角色会在没被 Possess 时一直开着调用器。
	ApplyViewpointInvokerState(bBoarded);

	// 关卡里就附着在飞船上的角色 = 舱里的船员，必须先认下来：否则下面的 SetRidePlanet 会把它
	// 改挂到行星下，飞船的 GetAttachedActors 从此找不到人，落地按 E 只能静默失败。
	if (ASpaceshipPawn* Ship = Cast<ASpaceshipPawn>(GetAttachParentActor()))
	{
		Ship->Board(this);
	}

	// 走 SetRidePlanet 而不是直接赋值：它同时把角色挂到行星下，平移随附着层级走，
	// 不再依赖 CarryRiders 每帧推位移。
	SetRidePlanet(FindNearestPlanet());

	if (UPlanetEditComponent* Edit = GetPlanetEditComponent())
	{
		// 网格只跟角色走。星图 Pawn 从不调这个，所以星图里没有地表网格。
		Edit->SetGridPlanet(PlanetActor);
	}

	if (!PlanetActor)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("APlanetCharacter: 关卡里找不到 AVoxelPlanetActor，角色不会获得球面重力。"));
		return;
	}

	// 出生点以关卡里的 PlayerStart 为准：有它时角色已被引擎摆好，再吸附等于作废出生点
	// （飞船也摆在同一 PlayerStart 上，吸到地表会让开局登船失败）。没有时才摆位 ——
	// 角色会生成在世界原点，对球心在原点的行星就是生成在实心内部。
	bSpawnedAtPlayerStart = HasPlayerStartInLevel();
	if (!bSpawnedAtPlayerStart)
	{
		SnapToPlanetSurface();
	}
}

bool APlanetCharacter::HasPlayerStartInLevel() const
{
	// APlayerStartPIE 派生自 APlayerStart，「Play From Here」同样算数。
	for (TActorIterator<APlayerStart> It(GetWorld()); It; ++It)
	{
		return true;
	}
	return false;
}

void APlanetCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// 滚轮缩放。放在行星判空之前：臂长与重力无关。这里是**唯一**写 TargetArmLength
	// 的地方，别处直接赋值会在下一帧被冲掉。
	if (SpringArm)
	{
		ZoomOffset = FMath::FInterpTo(ZoomOffset, TargetZoomOffset, DeltaSeconds, ZoomInterpSpeed);
		SpringArm->TargetArmLength = FMath::Clamp(
			BaseCameraArmLength + ZoomOffset, MinCameraArmLength, MaxCameraArmLength);
	}

	// 行星在动，最近的一颗会换人；开局找不到行星时也要有后续帧找回的机会。
	// 登船期间不重评：角色挂在飞船下跨星球移动，重评会把它瞬移到新星地表。
	// 判据是 bBoarded，**不是** GetAttachParentActor()：角色平时就挂在行星下，
	// 用父级会把「走路」误当成「在船上」，重评从此再也不会发生。
	if (!bBoarded && PlanetReevaluateInterval > 0.f)
	{
		PlanetReevaluateTimer -= DeltaSeconds;
		if (PlanetReevaluateTimer <= 0.f)
		{
			PlanetReevaluateTimer = PlanetReevaluateInterval;
			ReevaluatePlanet();
		}
	}

	// 「上」的来源：有行星就是球面法线，没有就退回角色自己的 up。
	// 相机必须放在行星判空**之前**算：以前跟着一起 return，导致无行星关卡里鼠标
	// 拖不动相机（CameraYaw/Pitch 一直在累加却没人消费）。相机不该以脚下有没有行星为前提。
	FVector Up;
	if (PlanetActor)
	{
		const FVector ToCenter = (PlanetActor->GetActorLocation() - GetActorLocation()).GetSafeNormal();
		if (ToCenter.IsNearlyZero())
		{
			// 正好在球心，重力方向无意义，保持上一帧的姿态。
			return;
		}

		// 球面行走的全部物理：告诉 CMC“下”指向球心。落地判定、斜坡滑动、台阶
		// 与跳跃都在重力空间里自动成立，不需要手写球面物理。
		GetCharacterMovement()->SetGravityDirection(ToCenter);
		Up = -ToCenter;
	}
	else
	{
		// 没有行星：用角色自己的 up。平地上它就是世界 up，于是下面的基座退化成
		// 一个恒定的世界空间基座 —— 偏航仍是绕竖直轴水平环绕，俯仰仍是上下看。
		Up = GetActorUpVector();
	}

	// 基座沿球面连续平移：把上一帧法线转到本帧法线的旋转作用在基座上，任何位置都连续、
	// 极点不退化；平地上 Up 不变，旋转是单位阵，基座恒定。
	if (LastSurfaceUp.IsNearlyZero())
	{
		// 第一帧：任取一个切向参考建基，保证“上”就是球面法线。
		FVector Reference = FVector::VectorPlaneProject(GetActorForwardVector(), Up).GetSafeNormal();
		if (Reference.IsNearlyZero())
		{
			Reference = FVector::VectorPlaneProject(FVector::ForwardVector, Up).GetSafeNormal();
		}
		if (Reference.IsNearlyZero())
		{
			Reference = FVector::VectorPlaneProject(FVector::RightVector, Up).GetSafeNormal();
		}
		CameraBaseRotation = FRotationMatrix::MakeFromXZ(Reference, Up).ToQuat();
	}
	else
	{
		CameraBaseRotation = FQuat::FindBetweenNormals(LastSurfaceUp, Up) * CameraBaseRotation;
	}
	LastSurfaceUp = Up;

	// 玩家视角是相对基座的局部旋转；基座连续 ⟹ 画面不会翻滚。
	CameraPivot->SetWorldRotation(
		CameraBaseRotation * FRotator(CameraPitch, CameraYaw, 0.f).Quaternion());

	// 以下只对行星有意义：纯地形关卡里角色由地形碰撞支撑，不该被按到球面上。
	if (!PlanetActor)
	{
		return;
	}

	// 碰撞体是异步流送出来的：第一次踩到地面前每帧把角色按回球面，否则它会落进球体
	// 卡在实心里。出生点来自 PlayerStart 时整段跳过（见 bSpawnedAtPlayerStart）。
	if (!bSpawnedAtPlayerStart && !bHasLanded)
	{
		if (GetCharacterMovement()->IsMovingOnGround())
		{
			bHasLanded = true;
		}
		else
		{
			SnapToPlanetSurface();
		}
	}
}

AVoxelPlanetActor* APlanetCharacter::FindNearestPlanet() const
{
	AVoxelPlanetActor* Nearest = nullptr;
	float NearestDistSq = FMath::Square(PlanetSearchRadius);

	for (TActorIterator<AVoxelPlanetActor> It(GetWorld()); It; ++It)
	{
		const float DistSq = FVector::DistSquared(It->GetActorLocation(), GetActorLocation());
		if (DistSq < NearestDistSq)
		{
			NearestDistSq = DistSq;
			Nearest = *It;
		}
	}
	return Nearest;
}

void APlanetCharacter::SnapToPlanetSurface()
{
	if (!PlanetActor)
	{
		return;
	}

	const FVector PlanetCenter = PlanetActor->GetActorLocation();

	FVector Up = (GetActorLocation() - PlanetCenter).GetSafeNormal();
	if (Up.IsNearlyZero())
	{
		// 生成点恰好落在球心：随便挑一个方向，至少保证不卡在实心里。
		Up = FVector::UpVector;
	}

	const float CapsuleHalfHeight = GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	// 按方向取地表：有地形时地表在基准球面上下浮动（振幅 8 体素），用基准半径会把
	// 角色塞进山体。多抬 1 厘米，避免贴面时被判成「已在实体内部」。
	const FVector Location =
		PlanetCenter + Up * (PlanetActor->GetSurfaceRadiusCmAtDirection(Up) + CapsuleHalfHeight + 1.f);

	SetActorLocation(Location, /*bSweep=*/false);
	SetActorRotation((FQuat::FindBetweenNormals(FVector::UpVector, Up) * GetActorQuat()).Rotator());

	// 摆位之后残留的向下速度会让角色立刻穿进球体，必须清掉。
	GetCharacterMovement()->Velocity = FVector::ZeroVector;

	// 这次瞬移会让基座被平移过一个大跨度，下一帧重新初始化而不是硬搬。
	LastSurfaceUp = FVector::ZeroVector;
}

void APlanetCharacter::SetRidePlanet(AVoxelPlanetActor* NewPlanet)
{
	// 登船期间父级归飞船，不抢，只更新「脚下是哪颗星」。
	if (bBoarded)
	{
		PlanetActor = NewPlanet;
		return;
	}

	AActor* CurrentParent = GetAttachParentActor();
	if (CurrentParent != NewPlanet)
	{
		// KeepWorldTransform：换父级这一瞬间角色不能跳位，否则玩家会看到闪一下。
		if (CurrentParent)
		{
			DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
		}
		if (NewPlanet)
		{
			AttachToActor(NewPlanet, FAttachmentTransformRules::KeepWorldTransform);
		}
	}
	PlanetActor = NewPlanet;
}

void APlanetCharacter::ReevaluatePlanet()
{
	AVoxelPlanetActor* Nearest = FindNearestPlanet();
	const bool bPlanetChanged = (Nearest != PlanetActor);

	// 无条件调：星球没换时它也会把丢失的附着补回来（下船之后就是这种状态）。
	SetRidePlanet(Nearest);

	if (!bPlanetChanged)
	{
		return;
	}

	// 换星：清掉「已落地」，新星地表还没流送出来，让 Tick 末尾的兜底重新把角色按到球面。
	bHasLanded = false;

	if (PlanetActor)
	{
		// 内部会把 LastSurfaceUp 清零，相机基座下一帧重建。
		SnapToPlanetSurface();
	}

	// 编辑网格跟着角色走：组件内部会先把旧星的网格收掉再点亮新星。
	if (UPlanetEditComponent* Edit = GetPlanetEditComponent())
	{
		Edit->SetGridPlanet(PlanetActor);
	}
}

void APlanetCharacter::Move(const FVector2D& Value)
{
	if (!CameraPivot || Value.IsNearlyZero())
	{
		return;
	}

	// 以相机朝向为基准：枢轴已摆成「球面法线为上」，前向天然切于球面。
	const FVector Desired =
		CameraPivot->GetForwardVector() * Value.X + CameraPivot->GetRightVector() * Value.Y;

	// 相机俯仰会让前向偏出切平面，投影回重力平面，否则输入会把角色往天上推。
	const FVector GravityDir = GetCharacterMovement()->GetGravityDirection();
	const FVector OnFloor = FVector::VectorPlaneProject(Desired, GravityDir);
	if (OnFloor.IsNearlyZero())
	{
		return;
	}

	AddMovementInput(OnFloor.GetSafeNormal(), FMath::Min(1.f, OnFloor.Size()));
}

void APlanetCharacter::Look(float DeltaYaw, float DeltaPitch)
{
	// 偏航绕球面法线累积；俯仰钳制在一个不会翻过去的范围内。
	CameraYaw = FRotator::NormalizeAxis(CameraYaw + DeltaYaw);
	CameraPitch = FMath::Clamp(CameraPitch + DeltaPitch, MinCameraPitch, MaxCameraPitch);
}

void APlanetCharacter::AddZoom(float Value)
{
	// 夹的是**目标偏移**而不是最终臂长。只夹最终值的话，玩家滚过头会攒下一堆
	// 被 Clamp 吃掉的无效偏移，往回滚要先"还债"——前几格看起来像没反应。
	// ASpaceshipPawn::AddZoom 是同一套做法，两边保持一致。
	const float MinOffset = MinCameraArmLength - BaseCameraArmLength;
	// 防御 MinCameraArmLength > MaxCameraArmLength 的误配：Clamp 要求 Min <= Max。
	const float MaxOffset = FMath::Max(MaxCameraArmLength - BaseCameraArmLength, MinOffset);

	// 取负：Value > 0 是滚轮上滚 = 拉近 = 摇臂变短。
	TargetZoomOffset = FMath::Clamp(TargetZoomOffset - Value * ZoomStep, MinOffset, MaxOffset);
}

// 输入：由本角色自己绑定，只有被 Possess 时引擎才启用这个输入组件。

void APlanetCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	// 引擎只在本地 Possess 这个 Pawn 时调用这里，真正的绑定与回调都在组件里。
	if (PlayerInput)
	{
		PlayerInput->Bind(PlayerInputComponent);
	}
}

void APlanetCharacter::SetSprinting(bool bSprinting)
{
	GetCharacterMovement()->MaxWalkSpeed = bSprinting ? SprintSpeed : WalkSpeed;
}

void APlanetCharacter::SetBoarded(bool bInBoarded)
{
	bBoarded = bInBoarded;

	SetActorHiddenInGame(bBoarded);
	SetActorEnableCollision(!bBoarded);

	// 关掉移动，避免登船期间残留输入继续推动角色（它已经附着在飞船上了）。
	GetCharacterMovement()->StopMovementImmediately();
	GetCharacterMovement()->SetMovementMode(bBoarded ? MOVE_None : MOVE_Walking);

	// 登船后角色仍挂在飞船上跟着高速平移，LODToSet = 0 + 200 米盒子每帧重网格化
	// 是飞船三档之外的一块大头；和「是不是当前视点」交给同一个谓词。
	ApplyViewpointInvokerState(bBoarded);

	if (!bBoarded)
	{
		// 离船后重新踩上球面：碰撞体可能已经因为距离太远被回收了。
		bHasLanded = false;

		// 离船时可能已到另一颗星旁，清零计时器让下一帧立刻重找，别拿着旧星重力等半秒。
		PlanetReevaluateTimer = 0.f;

		// 摘掉飞船后角色无父级，先挂回上次已知的星，紧接着的 ReevaluatePlanet
		// 会把它纠正成真正最近的一颗。
		SetRidePlanet(PlanetActor);
	}
}

void APlanetCharacter::SetViewpointInvokersActive(const bool bActive)
{
	bIsViewpoint = bActive;
	ApplyViewpointInvokerState(bBoarded);
}

void APlanetCharacter::ApplyViewpointInvokerState(const bool bBoardedIn)
{
	// 「参与」= 是当前视点 ∧ 没在船上。
	const bool bActive = bIsViewpoint && !bBoardedIn;
	VoxelViewpointInvokers::SetEnabled(VoxelInvoker, bActive);

	// 过渡环与主 Invoker 同进同退：留下一个就会让「只有被 Possess 的 Pawn 起作用」失效。
	for (const TObjectPtr<UVoxelSimpleInvokerComponent>& Invoker : FarVoxelInvokers)
	{
		VoxelViewpointInvokers::SetEnabled(Invoker, bActive);
	}
}
