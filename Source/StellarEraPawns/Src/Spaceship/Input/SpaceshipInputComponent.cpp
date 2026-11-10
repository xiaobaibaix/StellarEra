// Copyright Voxel Plugin SAS. All Rights Reserved.

#include "SpaceshipInputComponent.h"

#include "EnhancedInputComponent.h"
#include "GameFramework/HUD.h"
#include "GameFramework/PlayerController.h"
#include "InputActionValue.h"
#include "PlanetCharacter.h"
#include "SessionCommands.h"
#include "SpaceshipInputActions.h"
#include "SpaceshipPawn.h"
#include "StarMapCameraPawn.h"
#include "StellarEraInputSource.h"
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

	// 只取飞船自己那张表：别的 Pawn 的动作不归这里管；E / M 也在这张表里。
	const FSpaceshipInputActions& Input = Source->GetSpaceshipInputActions();

	EnhancedInput->BindAction(Input.Look, ETriggerEvent::Triggered, this, &USpaceshipInputComponent::OnLookInput);
	EnhancedInput->BindAction(Input.Thrust, ETriggerEvent::Triggered, this, &USpaceshipInputComponent::OnThrustInput);
	EnhancedInput->BindAction(Input.Thrust, ETriggerEvent::Completed, this, &USpaceshipInputComponent::OnThrustInput);
	EnhancedInput->BindAction(Input.Roll, ETriggerEvent::Triggered, this, &USpaceshipInputComponent::OnRollInput);
	EnhancedInput->BindAction(Input.Roll, ETriggerEvent::Completed, this, &USpaceshipInputComponent::OnRollInput);
	EnhancedInput->BindAction(Input.Vertical, ETriggerEvent::Triggered, this, &USpaceshipInputComponent::OnVerticalInput);
	EnhancedInput->BindAction(Input.Vertical, ETriggerEvent::Completed, this, &USpaceshipInputComponent::OnVerticalInput);
	EnhancedInput->BindAction(Input.Boost, ETriggerEvent::Started, this, &USpaceshipInputComponent::OnBoostStarted);
	EnhancedInput->BindAction(Input.Boost, ETriggerEvent::Completed, this, &USpaceshipInputComponent::OnBoostCompleted);

	// 只绑 Started：按住不放只切一次，不会随按键重复翻转状态。Tab / B 同理。
	EnhancedInput->BindAction(Input.Cruise, ETriggerEvent::Started, this, &USpaceshipInputComponent::OnCruiseToggle);
	EnhancedInput->BindAction(Input.FreeLookToggle, ETriggerEvent::Started, this, &USpaceshipInputComponent::OnFreeLookToggle);
	EnhancedInput->BindAction(Input.LookActivate, ETriggerEvent::Started, this, &USpaceshipInputComponent::OnLookRightStarted);
	EnhancedInput->BindAction(Input.LookActivate, ETriggerEvent::Completed, this, &USpaceshipInputComponent::OnLookRightCompleted);
	EnhancedInput->BindAction(Input.DebugViewToggle, ETriggerEvent::Started, this, &USpaceshipInputComponent::OnDebugViewToggle);

	// 只绑 Triggered：滚轮每格是一次独立冲量，不滚就不产生值，没有 Completed 可收。
	EnhancedInput->BindAction(Input.Zoom, ETriggerEvent::Triggered, this, &USpaceshipInputComponent::OnZoomInput);

	// M 进星图、E 离船：两条都在飞船表里，由飞船自己接。
	EnhancedInput->BindAction(Input.StarMapToggle, ETriggerEvent::Started, this, &USpaceshipInputComponent::OnStarMapToggle);
	EnhancedInput->BindAction(Input.Interact, ETriggerEvent::Started, this, &USpaceshipInputComponent::OnInteract);

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

	// 驾驶时不设右键门（鼠标就是操纵杆）；自由视角光标可见，按住右键才环绕。
	if (Pawn->IsFreeLookActive() && !bLookHeld)
	{
		return;
	}

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

	// 切换在画面上不留痕迹，不提示的话玩家只能靠"好像还在飞"来猜按没按到。
	ShowNotice(Pawn->IsPreserveVelocity() ? TEXT("巡航：开") : TEXT("巡航：关"));
}

void USpaceshipInputComponent::OnFreeLookToggle(const FInputActionValue& Value)
{
	ASpaceshipPawn* Pawn = GetPawn();
	if (!Pawn)
	{
		return;
	}

	// 光标显隐由控制器在 PlayerTick 里跟着 IsFreeLookActive 切。
	Pawn->ToggleFreeLook();
	bLookHeld = false;
	ShowNotice(Pawn->IsFreeLookActive() ? TEXT("视角：自由") : TEXT("视角：驾驶"));
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

void USpaceshipInputComponent::OnStarMapToggle(const FInputActionValue& Value)
{
	ASpaceshipPawn* Pawn = GetPawn();
	APlayerController* PC = Pawn ? Cast<APlayerController>(Pawn->GetController()) : nullptr;
	if (!Pawn || !PC)
	{
		return;
	}

	// 「从这艘飞船能借用哪台星图相机」由控制器回答；飞船自己什么都不用挤出来。
	IStellarEraSessionCommands* Commands = Cast<IStellarEraSessionCommands>(PC);
	AStarMapCameraPawn* Camera = Commands ? Commands->FindStarMapCameraFor(Pawn) : nullptr;
	if (!Camera)
	{
		return;
	}

	// 进场（含 Possess）归相机自己。飞船没有编辑模式，不用像角色那样先退出。
	Camera->EnterStarMap(Pawn, PC);
}

void USpaceshipInputComponent::OnInteract(const FInputActionValue& Value)
{
	ASpaceshipPawn* Pawn = GetPawn();
	APlayerController* PC = Pawn ? Cast<APlayerController>(Pawn->GetController()) : nullptr;
	if (!Pawn || !PC)
	{
		return;
	}

	// 空中不能下船：船体没碰到地面时 IsLanded 为假；速度不限。
	if (!Pawn->IsLanded())
	{
		return;
	}

	// 舱里的角色（没有就生成一个）由控制器给 —— 生成用哪个类配在控制器上。
	IStellarEraSessionCommands* Commands = Cast<IStellarEraSessionCommands>(PC);
	APlanetCharacter* Character = Commands ? Commands->GetOrSpawnCrewCharacter(Pawn) : nullptr;
	if (!Character)
	{
		return;
	}

	// 摘人 / 摆位 / 贴回球面归飞船，换控制对象归控制器。
	Pawn->DisembarkCrew(Character);
	PC->Possess(Character);
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
