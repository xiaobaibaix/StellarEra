// Copyright Voxel Plugin SAS. All Rights Reserved.

#include "PlanetCharacter.h"

#include "VoxelPlanetActor.h"
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
	Movement->MaxWalkSpeed = 600.f;
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
	SpringArm->bInheritPitch = false;
	SpringArm->bInheritYaw = false;
	SpringArm->bInheritRoll = false;
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

	// 占位网格：随引擎发布的立方体，保证没有美术资源时角色也看得见。
	// 导入真实模型后替换这里的路径（见 Task 5）。
	static ConstructorHelpers::FObjectFinder<USkeletalMesh> PlaceholderMesh(
		TEXT("/Engine/EngineMeshes/SkeletalCube.SkeletalCube"));
	if (PlaceholderMesh.Succeeded())
	{
		GetMesh()->SetSkeletalMesh(PlaceholderMesh.Object);
	}
	// 胶囊半高 88：把网格下移到胶囊底，让立方体站在地面上而不是穿进地里。
	GetMesh()->SetRelativeLocation(FVector(0.f, 0.f, -88.f));
	GetMesh()->SetRelativeScale3D(FVector(0.7f));
}

void APlanetCharacter::BeginPlay()
{
	Super::BeginPlay();

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

	// 相机枢轴：以球面法线为“上”，再按玩家偏航绕这根轴上转。
	const FQuat AlignToSurface = FQuat::FindBetweenNormals(FVector::UpVector, -ToCenter);
	CameraPivot->SetWorldRotation(
		AlignToSurface * FRotator(CameraPitch, CameraYaw, 0.f).Quaternion());

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
}
