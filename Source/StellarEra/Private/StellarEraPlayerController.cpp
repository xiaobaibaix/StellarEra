// Copyright Voxel Plugin SAS. All Rights Reserved.

#include "StellarEraPlayerController.h"

#include "PlanetCharacter.h"
#include "SpaceshipPawn.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"
#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "EngineUtils.h"

namespace
{
	/** 造一个运行时输入动作。ValueType 决定回调里 Get<T>() 的模板参数。 */
	UInputAction* MakeAction(UObject* Outer, const TCHAR* Name, EInputActionValueType ValueType)
	{
		UInputAction* Action = NewObject<UInputAction>(Outer, Name);
		Action->ValueType = ValueType;
		return Action;
	}

	/** 把一个按键映射到动作，并按顺序挂上修饰器。 */
	void MapKey(
		UInputMappingContext* Context,
		UInputAction* Action,
		const FKey& Key,
		const TArray<UInputModifier*>& Modifiers = TArray<UInputModifier*>())
	{
		FEnhancedActionKeyMapping& Mapping = Context->MapKey(Action, Key);
		for (UInputModifier* Modifier : Modifiers)
		{
			Mapping.Modifiers.Add(Modifier);
		}
	}

	UInputModifier* MakeNegate(UObject* Outer)
	{
		return NewObject<UInputModifierNegate>(Outer);
	}

	/** Swizzle(YXZ) 把按键的 X 轴值搬到 Y 轴，用于把 WASD 合成一个二维向量。 */
	UInputModifier* MakeSwizzleYXZ(UObject* Outer)
	{
		UInputModifierSwizzleAxis* Swizzle = NewObject<UInputModifierSwizzleAxis>(Outer);
		Swizzle->Order = EInputAxisSwizzle::YXZ;
		return Swizzle;
	}
}

AStellarEraPlayerController::AStellarEraPlayerController()
{
	// 输入组件由 DefaultInput.ini 指定为 EnhancedInput 版本；这里不额外设置。
	// 若 DefaultInput.ini 缺失，Cast<UEnhancedInputComponent> 会失败。
}

void AStellarEraPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();

	BuildInputAssets();

	UEnhancedInputComponent* EnhancedInput = Cast<UEnhancedInputComponent>(InputComponent);
	if (!EnhancedInput)
	{
		UE_LOG(LogTemp, Error,
			TEXT("AStellarEraPlayerController: InputComponent 不是 UEnhancedInputComponent，"
				 "请检查 Config/DefaultInput.ini 的 DefaultInputComponentClass。"));
		return;
	}

	EnhancedInput->BindAction(IA_Move, ETriggerEvent::Triggered, this, &AStellarEraPlayerController::OnMove);
	EnhancedInput->BindAction(IA_Look, ETriggerEvent::Triggered, this, &AStellarEraPlayerController::OnLook);
	EnhancedInput->BindAction(IA_Jump, ETriggerEvent::Started, this, &AStellarEraPlayerController::OnJump);
	EnhancedInput->BindAction(IA_Sprint, ETriggerEvent::Started, this, &AStellarEraPlayerController::OnSprintStarted);
	EnhancedInput->BindAction(IA_Sprint, ETriggerEvent::Completed, this, &AStellarEraPlayerController::OnSprintCompleted);

	EnhancedInput->BindAction(IA_Thrust, ETriggerEvent::Triggered, this, &AStellarEraPlayerController::OnThrust);
	EnhancedInput->BindAction(IA_Thrust, ETriggerEvent::Completed, this, &AStellarEraPlayerController::OnThrust);
	EnhancedInput->BindAction(IA_Roll, ETriggerEvent::Triggered, this, &AStellarEraPlayerController::OnRoll);
	EnhancedInput->BindAction(IA_Roll, ETriggerEvent::Completed, this, &AStellarEraPlayerController::OnRoll);
	EnhancedInput->BindAction(IA_Vertical, ETriggerEvent::Triggered, this, &AStellarEraPlayerController::OnVertical);
	EnhancedInput->BindAction(IA_Vertical, ETriggerEvent::Completed, this, &AStellarEraPlayerController::OnVertical);

	EnhancedInput->BindAction(IA_Interact, ETriggerEvent::Started, this, &AStellarEraPlayerController::OnInteract);
}

void AStellarEraPlayerController::BeginPlay()
{
	Super::BeginPlay();

	// SetupInputComponent 通常先于 BeginPlay 跑完，但这里再兜一次底。
	BuildInputAssets();

	if (UEnhancedInputLocalPlayerSubsystem* Subsystem =
		ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer()))
	{
		Subsystem->AddMappingContext(MappingContext, /*Priority=*/0);
	}
}

void AStellarEraPlayerController::BuildInputAssets()
{
	if (MappingContext)
	{
		return;
	}

	MappingContext = NewObject<UInputMappingContext>(this, TEXT("IMC_StellarEra"));

	IA_Move = MakeAction(this, TEXT("IA_Move"), EInputActionValueType::Axis2D);
	IA_Look = MakeAction(this, TEXT("IA_Look"), EInputActionValueType::Axis2D);
	IA_Jump = MakeAction(this, TEXT("IA_Jump"), EInputActionValueType::Boolean);
	IA_Sprint = MakeAction(this, TEXT("IA_Sprint"), EInputActionValueType::Boolean);

	IA_Thrust = MakeAction(this, TEXT("IA_Thrust"), EInputActionValueType::Axis1D);
	IA_Roll = MakeAction(this, TEXT("IA_Roll"), EInputActionValueType::Axis1D);
	IA_Vertical = MakeAction(this, TEXT("IA_Vertical"), EInputActionValueType::Axis1D);
	IA_Interact = MakeAction(this, TEXT("IA_Interact"), EInputActionValueType::Boolean);

	// WASD 合成一个二维向量：W/S 走 X 轴，A/D 先用 Swizzle 搬到 Y 轴再取反。
	// 修饰器按数组顺序依次作用，所以 A 是“先 Swizzle 再 Negate”。
	MapKey(MappingContext, IA_Move, EKeys::W);
	MapKey(MappingContext, IA_Move, EKeys::S, { MakeNegate(this) });
	MapKey(MappingContext, IA_Move, EKeys::D, { MakeSwizzleYXZ(this) });
	MapKey(MappingContext, IA_Move, EKeys::A, { MakeSwizzleYXZ(this), MakeNegate(this) });

	MapKey(MappingContext, IA_Look, EKeys::Mouse2D);

	MapKey(MappingContext, IA_Jump, EKeys::SpaceBar);
	MapKey(MappingContext, IA_Sprint, EKeys::LeftShift);

	// 飞船：W/S 主推力，D/A 横滚，Space/LeftControl 升降。与角色的 WASD
	// 共用按键没问题——同一个 IMC 里的动作互不吞输入，分派由 Pawn 类型决定。
	MapKey(MappingContext, IA_Thrust, EKeys::W);
	MapKey(MappingContext, IA_Thrust, EKeys::S, { MakeNegate(this) });
	MapKey(MappingContext, IA_Roll, EKeys::D);
	MapKey(MappingContext, IA_Roll, EKeys::A, { MakeNegate(this) });
	MapKey(MappingContext, IA_Vertical, EKeys::SpaceBar);
	MapKey(MappingContext, IA_Vertical, EKeys::LeftControl, { MakeNegate(this) });

	MapKey(MappingContext, IA_Interact, EKeys::E);
}

void AStellarEraPlayerController::OnMove(const FInputActionValue& Value)
{
	// 在飞船里时 WASD 归 IA_Thrust / IA_Roll 管，这里直接不响应。
	// 局部变量不能叫 Character：那是 AController 的成员，UE 把变量遮蔽当错误（C4458）。
	if (APlanetCharacter* PawnCharacter = Cast<APlanetCharacter>(GetPawn()))
	{
		PawnCharacter->Move(Value.Get<FVector2D>());
	}
}

void AStellarEraPlayerController::OnLook(const FInputActionValue& Value)
{
	const FVector2D Axis = Value.Get<FVector2D>();
	const float DeltaYaw = Axis.X * LookSensitivity;
	const float DeltaPitch = (bInvertLookY ? -Axis.Y : Axis.Y) * LookSensitivity;

	if (APlanetCharacter* PawnCharacter = Cast<APlanetCharacter>(GetPawn()))
	{
		PawnCharacter->Look(DeltaYaw, DeltaPitch);
	}
	else if (ASpaceshipPawn* Ship = Cast<ASpaceshipPawn>(GetPawn()))
	{
		Ship->Look(DeltaYaw, DeltaPitch);
	}
}

void AStellarEraPlayerController::OnJump(const FInputActionValue& Value)
{
	if (APlanetCharacter* PawnCharacter = Cast<APlanetCharacter>(GetPawn()))
	{
		PawnCharacter->Jump();
	}
}

void AStellarEraPlayerController::OnSprintStarted(const FInputActionValue& Value)
{
	if (APlanetCharacter* PawnCharacter = Cast<APlanetCharacter>(GetPawn()))
	{
		PawnCharacter->SetSprinting(true);
	}
}

void AStellarEraPlayerController::OnSprintCompleted(const FInputActionValue& Value)
{
	if (APlanetCharacter* PawnCharacter = Cast<APlanetCharacter>(GetPawn()))
	{
		PawnCharacter->SetSprinting(false);
	}
}

void AStellarEraPlayerController::OnThrust(const FInputActionValue& Value)
{
	// Completed 事件里 Value 是 0，正好用来在松手时归零。
	if (ASpaceshipPawn* Ship = Cast<ASpaceshipPawn>(GetPawn()))
	{
		Ship->SetThrust(Value.Get<float>());
	}
}

void AStellarEraPlayerController::OnRoll(const FInputActionValue& Value)
{
	if (ASpaceshipPawn* Ship = Cast<ASpaceshipPawn>(GetPawn()))
	{
		Ship->SetRoll(Value.Get<float>());
	}
}

void AStellarEraPlayerController::OnVertical(const FInputActionValue& Value)
{
	if (ASpaceshipPawn* Ship = Cast<ASpaceshipPawn>(GetPawn()))
	{
		Ship->SetVertical(Value.Get<float>());
	}
}

void AStellarEraPlayerController::DebugPossessShip()
{
	// 已经在飞船里就换回角色，否则找一艘最近的飞船坐进去。
	if (ASpaceshipPawn* CurrentShip = Cast<ASpaceshipPawn>(GetPawn()))
	{
		// 真登船（走 E 流程）时不接管：角色此时是隐藏、附着在飞船上、MOVE_None 的，
		// 直接 possess 回来会让飞船的 BoardedCharacter 仍然指着它，状态不一致。
		// 这种情况请按 E 走正常的离船流程。
		if (CurrentShip->GetBoardedCharacter())
		{
			UE_LOG(LogTemp, Warning,
				TEXT("AStellarEraPlayerController: 角色已正常登船，请按 E 离船，"
					 "不要用 DebugPossessShip 切回。"));
			return;
		}

		for (TActorIterator<APlanetCharacter> It(GetWorld()); It; ++It)
		{
			Possess(*It);
			return;
		}
		return;
	}

	APawn* CurrentPawn = GetPawn();
	if (!CurrentPawn)
	{
		return;
	}

	ASpaceshipPawn* NearestShip = nullptr;
	float NearestDistSq = TNumericLimits<float>::Max();
	for (TActorIterator<ASpaceshipPawn> It(GetWorld()); It; ++It)
	{
		const float DistSq = FVector::DistSquared(It->GetActorLocation(), CurrentPawn->GetActorLocation());
		if (DistSq < NearestDistSq)
		{
			NearestDistSq = DistSq;
			NearestShip = *It;
		}
	}

	if (NearestShip)
	{
		Possess(NearestShip);
	}
}

void AStellarEraPlayerController::PlayerTick(float DeltaTime)
{
	Super::PlayerTick(DeltaTime);

	// 提示只在“站在飞船旁”时出现，所以每帧重算一次距离。
	if (APlanetCharacter* PawnCharacter = Cast<APlanetCharacter>(GetPawn()))
	{
		ASpaceshipPawn* Ship = PawnCharacter->FindNearestShip();
		if (Ship && Ship->CanBoardFrom(PawnCharacter->GetActorLocation()))
		{
			ShowPrompt(TEXT("按 E 登船"));
		}
	}
	else if (ASpaceshipPawn* Ship = Cast<ASpaceshipPawn>(GetPawn()))
	{
		// 只有真的载着角色时才提示离船。DebugPossessShip 直接 possess 飞船时
		// GetBoardedCharacter() 为空，否则会出现「显示了提示但按 E 什么都不发生」。
		if (Ship->GetBoardedCharacter() && Ship->IsLanded())
		{
			ShowPrompt(TEXT("按 E 离船"));
		}
	}
}

void AStellarEraPlayerController::OnInteract(const FInputActionValue& Value)
{
	if (APlanetCharacter* PawnCharacter = Cast<APlanetCharacter>(GetPawn()))
	{
		ASpaceshipPawn* Ship = PawnCharacter->FindNearestShip();
		if (Ship && Ship->CanBoardFrom(PawnCharacter->GetActorLocation()))
		{
			BoardShip(Ship);
		}
		return;
	}

	if (ASpaceshipPawn* Ship = Cast<ASpaceshipPawn>(GetPawn()))
	{
		// 空中不能下船：速度没降到阈值以下、或下方没有地面时 IsLanded 为假。
		if (Ship->IsLanded())
		{
			LeaveShip(Ship);
		}
	}
}

void AStellarEraPlayerController::BoardShip(ASpaceshipPawn* Ship)
{
	APlanetCharacter* PawnCharacter = Cast<APlanetCharacter>(GetPawn());
	if (!Ship || !PawnCharacter)
	{
		return;
	}

	// 附着用 KeepWorldTransform：相对变换是从当前世界变换推导出来的，与 SetBoarded
	// 的先后无关——后者只切可见性、碰撞和移动模式，不碰变换。顺序只是为了读起来顺。
	PawnCharacter->AttachToActor(Ship, FAttachmentTransformRules::KeepWorldTransform);
	PawnCharacter->SetBoarded(true);
	Ship->SetBoardedCharacter(PawnCharacter);

	Possess(Ship);
}

void AStellarEraPlayerController::LeaveShip(ASpaceshipPawn* Ship)
{
	APlanetCharacter* PawnCharacter = Ship ? Ship->GetBoardedCharacter() : nullptr;
	if (!Ship || !PawnCharacter)
	{
		return;
	}

	PawnCharacter->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
	PawnCharacter->SetBoarded(false);
	Ship->SetBoardedCharacter(nullptr);

	// 摆到飞船旁边而不是原地：原地正好在飞船的碰撞球里，会立刻被挤开。
	const FVector ExitLocation = Ship->GetActorLocation() + Ship->GetActorRightVector() * 400.f;
	PawnCharacter->SetActorLocation(ExitLocation, /*bSweep=*/false);

	// 再按当地球面法线摆正。登船期间角色的朝向是冻结的（bOrientRotationToMovement
	// 只在有加速度时旋转，而附着时没有加速度），不重新对齐的话，在行星另一侧下船
	// 会看到角色侧躺在地上。这一步同时把角色按到球面上，并重置相机基座，
	// 避免下船瞬间镜头甩一下。
	PawnCharacter->SnapToPlanetSurface();

	Possess(PawnCharacter);
}

void AStellarEraPlayerController::ShowPrompt(const FString& Text)
{
	if (!GEngine)
	{
		return;
	}

	// duration 0 = 只显示这一帧。固定 key 让屏幕上永远只有一行，不会堆积。
	static const uint64 PromptMessageKey = 0x5745'4C43'4F4D'4521ULL;
	GEngine->AddOnScreenDebugMessage(PromptMessageKey, 0.f, FColor::Yellow, Text);
}
