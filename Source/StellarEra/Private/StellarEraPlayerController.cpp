// Copyright Voxel Plugin SAS. All Rights Reserved.

#include "StellarEraPlayerController.h"

#include "PlanetCharacter.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"
#include "Engine/LocalPlayer.h"

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

	// WASD 合成一个二维向量：W/S 走 X 轴，A/D 先用 Swizzle 搬到 Y 轴再取反。
	// 修饰器按数组顺序依次作用，所以 A 是“先 Swizzle 再 Negate”。
	MapKey(MappingContext, IA_Move, EKeys::W);
	MapKey(MappingContext, IA_Move, EKeys::S, { MakeNegate(this) });
	MapKey(MappingContext, IA_Move, EKeys::D, { MakeSwizzleYXZ(this) });
	MapKey(MappingContext, IA_Move, EKeys::A, { MakeSwizzleYXZ(this), MakeNegate(this) });

	MapKey(MappingContext, IA_Look, EKeys::Mouse2D);

	MapKey(MappingContext, IA_Jump, EKeys::SpaceBar);
	MapKey(MappingContext, IA_Sprint, EKeys::LeftShift);
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
	const float DeltaPitch = bInvertLookY ? -Axis.Y : Axis.Y;

	if (APlanetCharacter* PawnCharacter = Cast<APlanetCharacter>(GetPawn()))
	{
		PawnCharacter->Look(Axis.X * LookSensitivity, DeltaPitch * LookSensitivity);
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
