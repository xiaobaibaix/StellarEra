// Copyright Voxel Plugin SAS. All Rights Reserved.

#include "SpaceshipInputComponent.h"

#include "EnhancedInputComponent.h"
#include "GameFramework/HUD.h"
#include "GameFramework/PlayerController.h"
#include "InputActionValue.h"
#include "SpaceshipPawn.h"
#include "StellarEraInputActions.h"
#include "StellarEraNoticeSink.h"

ASpaceshipPawn* USpaceshipInputComponent::GetPawn() const
{
	return Cast<ASpaceshipPawn>(GetOwner());
}

void USpaceshipInputComponent::Bind(UInputComponent* PlayerInputComponent)
{
	ASpaceshipPawn* Pawn = GetPawn();
	UEnhancedInputComponent* EnhancedInput = Cast<UEnhancedInputComponent>(PlayerInputComponent);
	IStellarEraInputSource* Source = Pawn ? Cast<IStellarEraInputSource>(Pawn->GetController()) : nullptr;
	if (!Pawn || !EnhancedInput || !Source)
	{
		// 静默解绑等于「飞船对键鼠完全没反应」——留一条 Error，别让它变成哑故障。
		UE_LOG(LogTemp, Error, TEXT("[Input] ASpaceshipPawn 绑定失败：Pawn=%s EnhancedInput=%s InputSource=%s Controller=%s"),
			*GetNameSafe(Pawn),
			EnhancedInput ? TEXT("是") : TEXT("否"),
			Source ? TEXT("是") : TEXT("否"),
			*GetNameSafe(Pawn ? Pawn->GetController() : nullptr));
		return;
	}

	const FStellarEraInputActions& Input = Source->GetStellarEraInputActions();

	EnhancedInput->BindAction(Input.Look, ETriggerEvent::Triggered, this, &USpaceshipInputComponent::OnLookInput);
	EnhancedInput->BindAction(Input.Thrust, ETriggerEvent::Triggered, this, &USpaceshipInputComponent::OnThrustInput);
	EnhancedInput->BindAction(Input.Thrust, ETriggerEvent::Completed, this, &USpaceshipInputComponent::OnThrustInput);
	EnhancedInput->BindAction(Input.Roll, ETriggerEvent::Triggered, this, &USpaceshipInputComponent::OnRollInput);
	EnhancedInput->BindAction(Input.Roll, ETriggerEvent::Completed, this, &USpaceshipInputComponent::OnRollInput);
	EnhancedInput->BindAction(Input.Vertical, ETriggerEvent::Triggered, this, &USpaceshipInputComponent::OnVerticalInput);
	EnhancedInput->BindAction(Input.Vertical, ETriggerEvent::Completed, this, &USpaceshipInputComponent::OnVerticalInput);
	EnhancedInput->BindAction(Input.Boost, ETriggerEvent::Started, this, &USpaceshipInputComponent::OnBoostStarted);
	EnhancedInput->BindAction(Input.Boost, ETriggerEvent::Completed, this, &USpaceshipInputComponent::OnBoostCompleted);

	// 只绑 Started：按住 Tab 只切一次，不会随按键重复翻转状态。B 同理。
	EnhancedInput->BindAction(Input.Cruise, ETriggerEvent::Started, this, &USpaceshipInputComponent::OnCruiseToggle);
	EnhancedInput->BindAction(Input.DebugViewToggle, ETriggerEvent::Started, this, &USpaceshipInputComponent::OnDebugViewToggle);

	// 只绑 Triggered：滚轮每格是一次独立冲量，不滚就不产生值，没有 Completed 可收。
	EnhancedInput->BindAction(Input.Zoom, ETriggerEvent::Triggered, this, &USpaceshipInputComponent::OnZoomInput);

	// 绑定成功也给一条 Verbose：「没报错」不等于「绑上了」，无头跑时用它当正证据。
	UE_LOG(LogTemp, Verbose, TEXT("[Input] ASpaceshipPawn(%s) 已绑定飞船输入"), *Pawn->GetName());
}

void USpaceshipInputComponent::OnLookInput(const FInputActionValue& Value)
{
	ASpaceshipPawn* Pawn = GetPawn();
	if (!Pawn)
	{
		return;
	}

	// 飞船不设「按住右键」的门：鼠标就是它的操纵杆，要按住右键才能转向等于飞不动。
	const FVector2D Axis = Value.Get<FVector2D>();
	Pawn->Look(Axis.X * LookSensitivity, (bInvertLookY ? -Axis.Y : Axis.Y) * LookSensitivity);
}

void USpaceshipInputComponent::OnThrustInput(const FInputActionValue& Value)
{
	ASpaceshipPawn* Pawn = GetPawn();
	if (!Pawn)
	{
		return;
	}

	// Completed 事件里 Value 是 0，正好用来在松手时归零。
	Pawn->SetThrust(Value.Get<float>());
}

void USpaceshipInputComponent::OnRollInput(const FInputActionValue& Value)
{
	ASpaceshipPawn* Pawn = GetPawn();
	if (!Pawn)
	{
		return;
	}

	Pawn->SetRoll(Value.Get<float>());
}

void USpaceshipInputComponent::OnVerticalInput(const FInputActionValue& Value)
{
	ASpaceshipPawn* Pawn = GetPawn();
	if (!Pawn)
	{
		return;
	}

	Pawn->SetVertical(Value.Get<float>());
}

void USpaceshipInputComponent::OnBoostStarted(const FInputActionValue& Value)
{
	ASpaceshipPawn* Pawn = GetPawn();
	if (!Pawn)
	{
		return;
	}

	Pawn->SetBoost(true);
}

void USpaceshipInputComponent::OnBoostCompleted(const FInputActionValue& Value)
{
	ASpaceshipPawn* Pawn = GetPawn();
	if (!Pawn)
	{
		return;
	}

	Pawn->SetBoost(false);
}

void USpaceshipInputComponent::OnCruiseToggle(const FInputActionValue& Value)
{
	ASpaceshipPawn* Pawn = GetPawn();
	if (!Pawn)
	{
		return;
	}

	Pawn->TogglePreserveVelocity();

	// 切换本身在画面上不留任何痕迹（速度保持与否要飞一段才感觉得到），所以弹一条
	// 提示。没有它，玩家按了 Tab 只能靠"好像还在飞"来猜，按没按到都分不清。
	ShowNotice(Pawn->IsPreserveVelocity() ? TEXT("巡航：开") : TEXT("巡航：关"));
}

void USpaceshipInputComponent::OnDebugViewToggle(const FInputActionValue& Value)
{
	ASpaceshipPawn* Pawn = GetPawn();
	if (!Pawn)
	{
		return;
	}

	Pawn->ToggleDebugViewMode();

	ShowNotice(Pawn->IsDebugViewMode() ? TEXT("飞船调试视角：开") : TEXT("飞船调试视角：关"));
}

void USpaceshipInputComponent::OnZoomInput(const FInputActionValue& Value)
{
	ASpaceshipPawn* Pawn = GetPawn();
	if (!Pawn)
	{
		return;
	}

	Pawn->AddZoom(Value.Get<float>());
}

void USpaceshipInputComponent::ShowNotice(const FString& Text) const
{
	// HUD 在 UI 模块（本模块的下游），只能走接口。没有 HUD 时静默跳过。
	const ASpaceshipPawn* Pawn = GetPawn();
	const APlayerController* PC = Pawn ? Cast<APlayerController>(Pawn->GetController()) : nullptr;
	IStellarEraNoticeSink* Sink = PC ? Cast<IStellarEraNoticeSink>(PC->GetHUD()) : nullptr;
	if (Sink)
	{
		Sink->ShowNotice(Text);
	}
}
