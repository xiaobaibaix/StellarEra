// Copyright Voxel Plugin SAS. All Rights Reserved.

#include "SpaceshipPawn.h"

#include "VoxelPlanetActor.h"
#include "Camera/CameraComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/FloatingPawnMovement.h"
#include "GameFramework/SpringArmComponent.h"
#include "UObject/ConstructorHelpers.h"
#include "EngineUtils.h"

ASpaceshipPawn::ASpaceshipPawn()
{
	PrimaryActorTick.bCanEverTick = true;

	Collision = CreateDefaultSubobject<USphereComponent>(TEXT("Collision"));
	Collision->InitSphereRadius(300.f);
	Collision->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Collision->SetCollisionObjectType(ECC_Pawn);
	Collision->SetCollisionResponseToAllChannels(ECR_Block);
	SetRootComponent(Collision);

	FloatingMovement = CreateDefaultSubobject<UFloatingPawnMovement>(TEXT("FloatingMovement"));
	FloatingMovement->MaxSpeed = 8000.f;
	FloatingMovement->Acceleration = 4000.f;
	FloatingMovement->Deceleration = 2000.f;
	FloatingMovement->TurningBoost = 8.f;

	SpringArm = CreateDefaultSubobject<USpringArmComponent>(TEXT("SpringArm"));
	SpringArm->SetupAttachment(Collision);
	SpringArm->bUsePawnControlRotation = false;
	SpringArm->bInheritPitch = true;
	SpringArm->bInheritYaw = true;
	// 不继承横滚：机身可以翻滚，画面不该跟着倒过来。
	SpringArm->bInheritRoll = false;
	SpringArm->TargetArmLength = 1800.f;
	SpringArm->SocketOffset = FVector(0.f, 0.f, 400.f);
	SpringArm->bDoCollisionTest = true;

	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(SpringArm, USpringArmComponent::SocketName);
	Camera->bUsePawnControlRotation = false;

	Hull = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Hull"));
	Hull->SetupAttachment(Collision);
	// 外壳只负责好看，碰撞交给根部的球体。
	Hull->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Hull->SetRelativeScale3D(FVector(6.f, 2.f, 1.f));

	// 占位外壳：随引擎发布的立方体。导入真实模型后替换这里的路径（见 Task 5）。
	static ConstructorHelpers::FObjectFinder<UStaticMesh> PlaceholderHull(
		TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (PlaceholderHull.Succeeded())
	{
		Hull->SetStaticMesh(PlaceholderHull.Object);
	}
}

void ASpaceshipPawn::BeginPlay()
{
	Super::BeginPlay();

	// 自动回正需要一个重力参考：贴着行星时对齐到球面法线，太空中对齐到世界 Z。
	PlanetActor = FindNearestPlanet();
}

AVoxelPlanetActor* ASpaceshipPawn::FindNearestPlanet() const
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

FVector ASpaceshipPawn::GetAutoLevelUp() const
{
	if (PlanetActor)
	{
		const FVector Radial =
			(GetActorLocation() - PlanetActor->GetActorLocation()).GetSafeNormal();
		if (!Radial.IsNearlyZero())
		{
			return Radial;
		}
	}
	return FVector::UpVector;
}

void ASpaceshipPawn::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// --- 1. 角速度衰减 ---
	// 鼠标给的是冲量（见 Look），这里让角速度平滑地衰减回 0。两者的平衡
	// 就是“甩一下鼠标，飞船转一会儿再停下”的手感。
	YawRate = FMath::FInterpTo(YawRate, 0.f, DeltaSeconds, AngularDamping);
	PitchRate = FMath::FInterpTo(PitchRate, 0.f, DeltaSeconds, AngularDamping);

	// --- 2. 应用旋转 ---
	FRotator DeltaRotation(PitchRate * DeltaSeconds, YawRate * DeltaSeconds, 0.f);
	DeltaRotation.Roll = RollInput * MaxRollRate * DeltaSeconds;
	AddActorLocalRotation(DeltaRotation);

	// --- 3. 自动回正：只把横滚拉回水平，俯仰留给玩家 ---
	if (AutoLevelSpeed > 0.f)
	{
		const FQuat Current = GetActorQuat();
		const FVector Forward = Current.GetForwardVector();
		// 参考“上”投影到垂直于机身前向的平面，就是机身“上”应该朝向的方向。
		// 参考方向贴着行星时是球面法线，太空中才是世界 Z —— 若一律用世界 Z，
		// 飞船在赤道附近会直接滚转 90°，落地射线随之指偏，IsLanded() 永远为假。
		FVector DesiredUp = FVector::VectorPlaneProject(GetAutoLevelUp(), Forward);
		if (DesiredUp.Normalize())
		{
			const FQuat Correction = FQuat::FindBetweenNormals(Current.GetUpVector(), DesiredUp);
			const float AngleRad = Correction.GetAngle();
			if (AngleRad > KINDA_SMALL_NUMBER)
			{
				// 限制每秒最多转 AutoLevelSpeed 度，避免生硬地瞬间弹正。
				const float MaxRad = FMath::DegreesToRadians(AutoLevelSpeed) * DeltaSeconds;
				const float Alpha = FMath::Min(1.f, MaxRad / AngleRad);
				AddActorWorldRotation(FQuat::Slerp(FQuat::Identity, Correction, Alpha));
			}
		}
	}

	// --- 4. 位移：推力交给 UFloatingPawnMovement，它自带扫掠与加减速 ---
	FloatingMovement->AddInputVector(GetActorForwardVector() * ThrustInput);
	FloatingMovement->AddInputVector(GetActorUpVector() * VerticalInput);
}

void ASpaceshipPawn::SetThrust(float Value)
{
	ThrustInput = FMath::Clamp(Value, -1.f, 1.f);
}

void ASpaceshipPawn::SetRoll(float Value)
{
	RollInput = FMath::Clamp(Value, -1.f, 1.f);
}

void ASpaceshipPawn::SetVertical(float Value)
{
	VerticalInput = FMath::Clamp(Value, -1.f, 1.f);
}

void ASpaceshipPawn::Look(float DeltaYaw, float DeltaPitch)
{
	// 冲量模型：鼠标位移直接累加进角速度。甩一下鼠标，飞船会转一会儿再
	// 慢慢停下，而不是跟着鼠标立刻静止——这就是街机飞行的惯性感。
	YawRate = FMath::Clamp(YawRate + DeltaYaw * MouseImpulseScale, -MaxYawRate, MaxYawRate);
	PitchRate = FMath::Clamp(PitchRate + DeltaPitch * MouseImpulseScale, -MaxPitchRate, MaxPitchRate);
}

bool ASpaceshipPawn::IsLanded() const
{
	if (GetVelocity().Size() > LandingSpeedThreshold)
	{
		return false;
	}

	// 沿机身“下”方打射线，而不是世界下方：飞船停在球体侧面时世界下方是空的。
	const FVector Start = GetActorLocation();
	const FVector End = Start - GetActorUpVector() * LandingProbeDistance;

	FCollisionQueryParams Params(SCENE_QUERY_STAT(SpaceshipLanding), /*bTraceComplex=*/false, this);
	FHitResult Hit;
	return GetWorld()->LineTraceSingleByChannel(Hit, Start, End, ECC_WorldStatic, Params);
}
