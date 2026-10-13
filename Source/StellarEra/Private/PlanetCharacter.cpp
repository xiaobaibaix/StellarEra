// Copyright Voxel Plugin SAS. All Rights Reserved.

#include "PlanetCharacter.h"

#include "VoxelPlanetActor.h"
#include "SpaceshipPawn.h"
#include "VoxelComponents/VoxelInvokerComponent.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "UObject/ConstructorHelpers.h"
#include "EngineUtils.h"

APlanetCharacter::APlanetCharacter()
{
	PrimaryActorTick.bCanEverTick = true;

	GetCapsuleComponent()->InitCapsuleSize(42.f, 88.f);

	// 朝向交给 CMC：PhysicsRotation 会把目标旋转换算到重力空间、在那里清零
	// 俯仰与横滚再换回世界空间，因此角色天然垂直于球面。自己再插值一次只会
	// 和它打架。这里把控制器的旋转影响全部关掉，让引擎独占朝向。
	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = false;
	bUseControllerRotationRoll = false;

	// 局部变量不能叫 CharacterMovement：那是 ACharacter 的成员，UE 把变量遮蔽
	// 当错误（C4458）。
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

	CameraPivot = CreateDefaultSubobject<USceneComponent>(TEXT("CameraPivot"));
	CameraPivot->SetupAttachment(GetCapsuleComponent());

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

	VoxelInvoker = CreateDefaultSubobject<UVoxelSimpleInvokerComponent>(TEXT("VoxelInvoker"));
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

	// Pomni 模型。Sketchfab 导出的朝向与缩放通常都不对，校正量由
	// MeshRelativeRotation / MeshScale 两个属性给出——但这两个值在这里读一次就定死了，
	// 在细节面板里改不会生效（角色是 GameMode 用原生类运行时生成的，属性只反映 CDO）。
	// 调参路径：改这两个属性的默认值 → 重编译（约 20 秒）→ 再 PIE。
	// 注意：目录名里的竖线是全角 U+FF5C（｜），不是 ASCII 的 |，别"顺手改正"。
	static ConstructorHelpers::FObjectFinder<USkeletalMesh> CharacterMesh(
		TEXT("/Game/Fab/Blender_3D｜Pomni_3D_Model/pomni_3d_model/SkeletalMeshes/Object_14.Object_14"));
	if (CharacterMesh.Succeeded())
	{
		GetMesh()->SetSkeletalMesh(CharacterMesh.Object);
	}
	else
	{
		// 资源缺失时退回引擎自带立方体，保证角色仍然可见、不静默消失。
		static ConstructorHelpers::FObjectFinder<USkeletalMesh> FallbackMesh(
			TEXT("/Engine/EngineMeshes/SkeletalCube.SkeletalCube"));
		if (FallbackMesh.Succeeded())
		{
			GetMesh()->SetSkeletalMesh(FallbackMesh.Object);
		}
		UE_LOG(LogTemp, Warning,
			TEXT("APlanetCharacter: 找不到角色模型 %s，退回占位网格。"),
			TEXT("/Game/Fab/Blender_3D｜Pomni_3D_Model/pomni_3d_model/SkeletalMeshes/Object_14.Object_14"));
	}

	// 胶囊半高 88：把网格下移到胶囊底，让模型站在地面上而不是穿进地里。
	GetMesh()->SetRelativeLocation(FVector(0.f, 0.f, -88.f));
	GetMesh()->SetRelativeRotation(MeshRelativeRotation);
	GetMesh()->SetRelativeScale3D(FVector(MeshScale));
}

void APlanetCharacter::BeginPlay()
{
	Super::BeginPlay();

	// WalkSpeed 是 EditAnywhere：在蓝图/实例上改过之后这里应用一次，否则要等到
	// 按一次冲刺（SetSprinting）才会生效。
	GetCharacterMovement()->MaxWalkSpeed = WalkSpeed;

	PlanetActor = FindNearestPlanet();
	if (!PlanetActor)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("APlanetCharacter: 关卡里找不到 AVoxelPlanetActor，角色不会获得球面重力。"));
		return;
	}

	// 关卡里没有 PlayerStart 时角色会生成在世界原点；对球心在原点的行星来说
	// 就是生成在实心内部。这里直接摆到球面正上方。
	SnapToPlanetSurface();
}

void APlanetCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (!PlanetActor)
	{
		return;
	}

	const FVector ToCenter = (PlanetActor->GetActorLocation() - GetActorLocation()).GetSafeNormal();
	if (ToCenter.IsNearlyZero())
	{
		// 正好在球心，重力方向无意义，保持上一帧的姿态。
		return;
	}

	// 球面行走的全部物理：告诉 CMC“下”指向球心。落地判定、斜坡滑动、台阶
	// 与跳跃都在重力空间里自动成立，不需要手写球面物理。
	GetCharacterMovement()->SetGravityDirection(ToCenter);

	// 基座沿球面连续平移：求出把上一帧法线转到本帧法线的旋转，作用在基座上。
	// 这样朝向在任何位置都连续，极点处也不会退化。
	const FVector Up = -ToCenter;
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

	// 玩家视角是相对基座的局部旋转：偏航绕法线水平环绕，俯仰绕切向倾斜。
	// 基座连续 ⟹ 画面不会翻滚。
	CameraPivot->SetWorldRotation(
		CameraBaseRotation * FRotator(CameraPitch, CameraYaw, 0.f).Quaternion());

	// 碰撞体是异步流送出来的：第一次踩到地面之前，每帧把角色按回球面，
	// 否则它会在碰撞体生成之前落进球体内部，然后卡在实心里。
	if (!bHasLanded)
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
	// 多抬 1 厘米，避免刚好贴面时被判定为“已经在实体内部”。
	const FVector Location =
		PlanetCenter + Up * (PlanetActor->GetSurfaceRadiusCm() + CapsuleHalfHeight + 1.f);

	SetActorLocation(Location, /*bSweep=*/false);
	SetActorRotation((FQuat::FindBetweenNormals(FVector::UpVector, Up) * GetActorQuat()).Rotator());

	// 摆位之后残留的向下速度会让角色立刻穿进球体，必须清掉。
	GetCharacterMovement()->Velocity = FVector::ZeroVector;

	// 这次瞬移会让基座被平移过一个大跨度，下一帧重新初始化而不是硬搬。
	LastSurfaceUp = FVector::ZeroVector;
}

void APlanetCharacter::Move(const FVector2D& Value)
{
	if (!CameraPivot || Value.IsNearlyZero())
	{
		return;
	}

	// 以相机朝向为基准：W 永远是“往画面里走”。相机枢轴已经被摆成“以球面
	// 法线为上”，所以它的前向天然切于球面。
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

void APlanetCharacter::SetSprinting(bool bSprinting)
{
	GetCharacterMovement()->MaxWalkSpeed = bSprinting ? SprintSpeed : WalkSpeed;
}

void APlanetCharacter::SetBoarded(bool bBoarded)
{
	SetActorHiddenInGame(bBoarded);
	SetActorEnableCollision(!bBoarded);

	// 关掉移动，避免登船期间残留输入继续推动角色（它已经附着在飞船上了）。
	GetCharacterMovement()->StopMovementImmediately();
	GetCharacterMovement()->SetMovementMode(bBoarded ? MOVE_None : MOVE_Walking);

	if (!bBoarded)
	{
		// 离船后重新踩上球面：碰撞体可能已经因为距离太远被回收了。
		bHasLanded = false;
	}
}

ASpaceshipPawn* APlanetCharacter::FindNearestShip() const
{
	ASpaceshipPawn* Nearest = nullptr;
	float NearestDistSq = TNumericLimits<float>::Max();

	for (TActorIterator<ASpaceshipPawn> It(GetWorld()); It; ++It)
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
