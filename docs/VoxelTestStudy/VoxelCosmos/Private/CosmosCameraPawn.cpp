// 宇宙相机 —— 实现。

#include "CosmosCameraPawn.h"

#include "Camera/CameraComponent.h"
#include "Components/InputComponent.h"
#include "GameFramework/FloatingPawnMovement.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "NBodySimulator.h"

ACosmosCameraPawn::ACosmosCameraPawn()
{
	PrimaryActorTick.bCanEverTick = true;

	// 速度档位：AU/秒。相邻档约 3.2 倍，接近戴森球计划的手感。
	// 覆盖从贴近地表到横跨整个恒星系。
	SpeedSteps = {
		0.00001f,  // 0 · 1500 km，可贴着星球表面飞
		0.000032f,
		0.0001f,
		0.00032f,
		0.001f,
		0.0032f,
		0.01f,     // 6 · 默认档，约 150 万 km/秒
		0.032f,
		0.1f,
		0.32f,
		1.0f,      // 10 · 横跨太阳系只要几秒
		3.2f,
		10.0f,
	};

	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = false;
	bUseControllerRotationRoll = false;
}

void ACosmosCameraPawn::BeginPlay()
{
	Super::BeginPlay();

	// 相机朝向：初始朝 +Z?不，朝游戏世界的固定朝向即可。
	// 太阳系通常按 XY 平面展开，所以默认俯视。
	if (GetController())
	{
		const FRotator InitialRotation(FRotator(-45.f, -45.f, 0.f));
		if (AController* C = Cast<AController>(GetController()))
		{
			C->SetControlRotation(InitialRotation);
		}
	}
}

///////////////////////////////////////////////////////////////////////////////
// 输入

void ACosmosCameraPawn::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	// 轴：前进后退 / 左右 / 升降
	PlayerInputComponent->BindAxis(TEXT("MoveForward"), this, &ACosmosCameraPawn::MoveForward);
	PlayerInputComponent->BindAxis(TEXT("MoveRight"), this, &ACosmosCameraPawn::MoveRight);
	PlayerInputComponent->BindAxis(TEXT("MoveUp"), this, &ACosmosCameraPawn::MoveUp);

	// 鼠标：右键拖动转视角
	PlayerInputComponent->BindAxis(TEXT("Turn"), this, &ACosmosCameraPawn::LookAtMouse);
	PlayerInputComponent->BindAxis(TEXT("LookUp"), this, &ACosmosCameraPawn::LookAtMouse);

	// 滚轮：调速度档
	PlayerInputComponent->BindAction(TEXT("SpeedUp"), IE_Pressed, this, &ACosmosCameraPawn::OnScrollUp);
	PlayerInputComponent->BindAction(TEXT("SpeedDown"), IE_Pressed, this, &ACosmosCameraPawn::OnScrollDown);

	// 加速 / 减速
	PlayerInputComponent->BindAction(TEXT("Boost"), IE_Pressed, this, &ACosmosCameraPawn::OnBoostPressed);
	PlayerInputComponent->BindAction(TEXT("Boost"), IE_Released, this, &ACosmosCameraPawn::OnBoostReleased);
	PlayerInputComponent->BindAction(TEXT("Precision"), IE_Pressed, this, &ACosmosCameraPawn::OnPrecisionPressed);
	PlayerInputComponent->BindAction(TEXT("Precision"), IE_Released, this, &ACosmosCameraPawn::OnPrecisionReleased);

	// 聚焦
	PlayerInputComponent->BindAction(TEXT("FocusNearest"), IE_Pressed, this, &ACosmosCameraPawn::FocusNearestBody);
	PlayerInputComponent->BindAction(TEXT("CycleTarget"), IE_Pressed, this, &ACosmosCameraPawn::CycleNextFocusTarget);
}

void ACosmosCameraPawn::MoveForward(float Value)
{
	if (FMath::IsNearlyZero(Value))
	{
		return;
	}
	// 输入是 -1..1，乘到速度上后加进累积速度
	AddMovementInput(FVector::ForwardVector, Value);
}

void ACosmosCameraPawn::MoveRight(float Value)
{
	if (FMath::IsNearlyZero(Value))
	{
		return;
	}
	AddMovementInput(FVector::RightVector, Value);
}

void ACosmosCameraPawn::MoveUp(float Value)
{
	if (FMath::IsNearlyZero(Value))
	{
		return;
	}
	AddMovementInput(FVector::UpVector, Value);
}

void ACosmosCameraPawn::LookAtMouse(float X, float Y)
{
	if (!Controller)
	{
		return;
	}

	FRotator Rotation = Controller->GetControlRotation();

	// 鼠标 Y 是上下，X 是左右。取负让移动方向与视角一致。
	float Yaw = X * LookSensitivity;
	float Pitch = Y * LookSensitivity;

	// 限制俯仰角，不要越过天顶
	const float NewPitch = FMath::Clamp(Rotation.Pitch + Pitch, -89.f, 89.f);

	Controller->SetControlRotation(FRotator(NewPitch, Rotation.Yaw + Yaw, 0.f));
}

void ACosmosCameraPawn::OnScrollUp()
{
	SpeedStep = FMath::Min(SpeedStep + SpeedStepPerScroll, SpeedSteps.Num() - 1);
}

void ACosmosCameraPawn::OnScrollDown()
{
	SpeedStep = FMath::Max(SpeedStep - SpeedStepPerScroll, 0);
}

void ACosmosCameraPawn::OnBoostPressed()   { CurrentSpeedMultiplier = BoostMultiplier; }
void ACosmosCameraPawn::OnBoostReleased()  { CurrentSpeedMultiplier = 1.f; }
void ACosmosCameraPawn::OnPrecisionPressed()  { CurrentSpeedMultiplier = PrecisionMultiplier; }
void ACosmosCameraPawn::OnPrecisionReleased() { CurrentSpeedMultiplier = 1.f; }

///////////////////////////////////////////////////////////////////////////////
// 每帧移动

void ACosmosCameraPawn::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// 把速度档位换算成世界单位（厘米/秒）
	const float SpeedAuPerSec = GetCurrentSpeedAu();
	const float SpeedCmPerSec = float(SpeedAuPerSec * CosmosUnits::AuToCentimeter);

	// 受阻尼影响的平滑速度，让停止有惯性
	const FVector TargetVelocity = GetPendingMovementInputVector() * SpeedCmPerSec;
	SmoothedVelocity = FMath::Lerp(SmoothedVelocity, TargetVelocity, 1.f - Damping);

	if (!SmoothedVelocity.IsNearlyZero())
	{
		AddActorWorldOffset(SmoothedVelocity * DeltaSeconds, false);
	}

	// 自动对准焦点目标
	if (FocusTarget.IsValid())
	{
		const FVector ToTarget = FocusTarget->GetActorLocation() - GetActorLocation();
		const FRotator Desired = ToTarget.Rotation();
		const FRotator Current = GetControlRotation();

		const FRotator Blended = FMath::RInterpTo(
			Current, Desired, DeltaSeconds, AutoAlignDegreesPerSecond);

		if (Controller)
		{
			Controller->SetControlRotation(Blended);
		}

		// 目标跑远了就停止自动对准，让玩家自己控制
		if (ToTarget.Size() > FocusDistanceAu * CosmosUnits::AuToCentimeter * 3.f)
		{
			FocusTarget.Reset();
		}
	}
}

float ACosmosCameraPawn::GetCurrentSpeedAu() const
{
	if (!SpeedSteps.IsValidIndex(SpeedStep))
	{
		return 0.01f;
	}
	return SpeedSteps[SpeedStep] * CurrentSpeedMultiplier;
}

///////////////////////////////////////////////////////////////////////////////
// 聚焦

void ACosmosCameraPawn::LookAtActor(AActor* Target)
{
	if (!Target || !Controller)
	{
		return;
	}

	const FVector Direction = (Target->GetActorLocation() - GetActorLocation()).GetSafeNormal();
	Controller->SetControlRotation(Direction.Rotation());
}

void ACosmosCameraPawn::SnapToActor(AActor* Target)
{
	if (!Target)
	{
		return;
	}

	// 保持当前朝向，把位置挪到目标前方
	const FRotator CurrentRotation = GetControlRotation();
	const FVector BackOffset = -CurrentRotation.Vector() * FocusDistanceAu * CosmosUnits::AuToCentimeter;

	SetActorLocation(Target->GetActorLocation() + BackOffset);
	FocusTarget = Target;
}

void ACosmosCameraPawn::FocusNearestBody()
{
	AController* C = Cast<AController>(GetController());
	if (!C)
	{
		return;
	}

	const FVector SelfLocation = GetActorLocation();
	AActor* Nearest = nullptr;
	double BestDistSq = TNumericLimits<double>::Max();

	for (const TWeakObjectPtr<AActor>& Weak : FocusTargets)
	{
		AActor* Candidate = Weak.Get();
		if (!Candidate)
		{
			continue;
		}
		const double DistSq = double(FVector::DistSquared(Candidate->GetActorLocation(), SelfLocation));
		if (DistSq < BestDistSq)
		{
			BestDistSq = DistSq;
			Nearest = Candidate;
		}
	}

	if (Nearest)
	{
		FocusTarget = Nearest;
		LookAtActor(Nearest);
	}
}

void ACosmosCameraPawn::CycleNextFocusTarget()
{
	// 清掉已失效的引用
	FocusTargets.RemoveAll([](const TWeakObjectPtr<AActor>& W)
	{
		return !W.IsValid();
	});

	if (FocusTargets.IsEmpty())
	{
		return;
	}

	FocusIndex = (FocusIndex + 1) % FocusTargets.Num();
	AActor* Target = FocusTargets[FocusIndex].Get();

	if (Target)
	{
		FocusTarget = Target;
		LookAtActor(Target);
	}
}

void ACosmosCameraPawn::RegisterFocusTarget(AActor* Actor)
{
	if (Actor && !FocusTargets.Contains(Actor))
	{
		FocusTargets.Add(Actor);
	}
}
