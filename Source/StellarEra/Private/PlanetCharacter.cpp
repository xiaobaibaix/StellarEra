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

namespace
{
	// 占位网格的资产路径。构造函数和 ApplyCharacterMesh 都要用，写在一处，
	// 免得两边的字符串对不上。
	const TCHAR* FallbackMeshPath = TEXT("/Engine/EngineMeshes/SkeletalCube.SkeletalCube");
}

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

	// C++ 只给一个引擎骷髅方块兜底，让裸放的 APlanetCharacter 不至于隐形。
	//
	// 真实模型不在这里。它属于「具体表现」，由子蓝图 BP_PlanetCharacter 在
	// Class Defaults 里填 CharacterMesh / MeshRelativeLocation /
	// MeshRelativeRotation / MeshScale 提供，换模型、调朝向都不必重新编译。
	//
	// 也正因为如此，这里不能写项目资产路径：蓝图 CDO 构造时会先跑一遍这个
	// 构造函数，之后才加载蓝图里存的值，构造期读到的永远是 C++ 默认值。
	// 真正套用那四个属性的地方是 ApplyCharacterMesh。
	static ConstructorHelpers::FObjectFinder<USkeletalMesh> FallbackMesh(FallbackMeshPath);
	if (FallbackMesh.Succeeded())
	{
		GetMesh()->SetSkeletalMesh(FallbackMesh.Object);
	}
	else
	{
		// 连引擎骷髅方块都拿不到时会得到一个没有网格、完全不可见的角色。
		// 宁可吵一句，也不要静默失败。
		UE_LOG(LogTemp, Warning,
			TEXT("APlanetCharacter: 找不到占位网格 %s，Mesh 将没有模型。"), FallbackMeshPath);
	}
}

void APlanetCharacter::ApplyCharacterMesh()
{
	USkeletalMeshComponent* MeshComponent = GetMesh();
	if (!MeshComponent)
	{
		return;
	}

	// 有就用蓝图给的，没有就退回占位网格。
	//
	// 这里不能直接 return —— 在编辑器里把 CharacterMesh 清空后，上一次套用的模型
	// 会留在 Mesh 上，看起来像"属性没生效"。显式放回占位网格才是所见即所得。
	USkeletalMesh* MeshToUse = CharacterMesh
		? CharacterMesh.Get()
		: LoadObject<USkeletalMesh>(nullptr, FallbackMeshPath);

	MeshComponent->SetSkeletalMesh(MeshToUse);
	MeshComponent->SetRelativeLocation(MeshRelativeLocation);
	MeshComponent->SetRelativeRotation(MeshRelativeRotation);
	MeshComponent->SetRelativeScale3D(FVector(MeshScale));
}

void APlanetCharacter::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	// 外观在这里套用，而不是构造函数里：OnConstruction 是编辑器里能读到蓝图
	// 覆盖值的第一个时机，在蓝图 Class Defaults 里改 CharacterMesh 立刻能在视口看到。
	ApplyCharacterMesh();
}

void APlanetCharacter::BeginPlay()
{
	Super::BeginPlay();

	// WalkSpeed 是 EditAnywhere：在蓝图/实例上改过之后这里应用一次，否则要等到
	// 按一次冲刺（SetSprinting）才会生效。
	GetCharacterMovement()->MaxWalkSpeed = WalkSpeed;

	// 再套一次：角色由 GameMode 按 DefaultPawnClass 运行时生成，实例的
	// OnConstruction 未必在蓝图属性就位之后跑过。重复调用是幂等的。
	ApplyCharacterMesh();

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
	// 按方向取地表：有地形时地表在基准球面上下浮动，用基准半径会把角色塞进
	// 山体里（振幅 8 体素 = 800cm，山峰比基准面高 1600cm）。
	// 多抬 1 厘米，避免刚好贴面时被判定为“已经在实体内部”。
	const FVector Location =
		PlanetCenter + Up * (PlanetActor->GetSurfaceRadiusCmAtDirection(Up) + CapsuleHalfHeight + 1.f);

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
