// Copyright Voxel Plugin SAS. All Rights Reserved.

#include "PlanetCharacterInputComponent.h"

#include "EnhancedInputComponent.h"
#include "GameFramework/PlayerController.h"
#include "InputActionValue.h"
#include "InputCoreTypes.h"
#include "PlanetCharacter.h"
#include "PlanetEditComponent.h"
#include "StellarEraInputActions.h"

APlanetCharacter* UPlanetCharacterInputComponent::GetPawn() const
{
	return Cast<APlanetCharacter>(GetOwner());
}

void UPlanetCharacterInputComponent::Bind(UInputComponent* PlayerInputComponent)
{
	APlanetCharacter* Pawn = GetPawn();
	UEnhancedInputComponent* EnhancedInput = Cast<UEnhancedInputComponent>(PlayerInputComponent);
	IStellarEraInputSource* Source = Pawn ? Cast<IStellarEraInputSource>(Pawn->GetController()) : nullptr;
	if (!Pawn || !EnhancedInput || !Source)
	{
		// 静默解绑等于「角色对键鼠完全没反应」，是这里最难查的失败模式：留一条 Error。
		UE_LOG(LogTemp, Error, TEXT("[Input] APlanetCharacter 绑定失败：Pawn=%s EnhancedInput=%s InputSource=%s Controller=%s"),
			*GetNameSafe(Pawn),
			EnhancedInput ? TEXT("是") : TEXT("否"),
			Source ? TEXT("是") : TEXT("否"),
			*GetNameSafe(Pawn ? Pawn->GetController() : nullptr));
		return;
	}

	// 只取角色自己那张表：别的 Pawn 的动作不归这里管，也就不可能绑错。
	const FPlanetCharacterInputActions& Input = Source->GetStellarEraInputActions().Character;

	EnhancedInput->BindAction(Input.Move, ETriggerEvent::Triggered, this, &UPlanetCharacterInputComponent::OnMoveInput);
	EnhancedInput->BindAction(Input.Look, ETriggerEvent::Triggered, this, &UPlanetCharacterInputComponent::OnLookInput);
	EnhancedInput->BindAction(Input.LookActivate, ETriggerEvent::Started, this, &UPlanetCharacterInputComponent::OnLookRightStarted);
	EnhancedInput->BindAction(Input.LookActivate, ETriggerEvent::Completed, this, &UPlanetCharacterInputComponent::OnLookRightCompleted);
	EnhancedInput->BindAction(Input.Jump, ETriggerEvent::Started, this, &UPlanetCharacterInputComponent::OnJumpInput);
	EnhancedInput->BindAction(Input.Sprint, ETriggerEvent::Started, this, &UPlanetCharacterInputComponent::OnSprintStarted);
	EnhancedInput->BindAction(Input.Sprint, ETriggerEvent::Completed, this, &UPlanetCharacterInputComponent::OnSprintCompleted);

	// 只绑 Triggered：滚轮每格是一次独立冲量，不滚就不产生值，没有 Completed 可收。
	EnhancedInput->BindAction(Input.Zoom, ETriggerEvent::Triggered, this, &UPlanetCharacterInputComponent::OnZoomInput);

	// 编辑模式是「这个 Pawn 自己的组件」，所以 G 与笔刷尺寸跟着角色走。
	// 只绑 Started：Axis1D 的 Triggered 在按住时每帧触发一次，笔刷会瞬间冲到上限。
	EnhancedInput->BindAction(Input.EditModeToggle, ETriggerEvent::Started, this, &UPlanetCharacterInputComponent::OnEditModeToggle);
	EnhancedInput->BindAction(Input.BrushSize, ETriggerEvent::Started, this, &UPlanetCharacterInputComponent::OnBrushSize);

	// 绑定成功也给一条 Verbose：「没报错」不等于「绑上了」，无头跑时用它当正证据。
	UE_LOG(LogTemp, Verbose, TEXT("[Input] APlanetCharacter(%s) 已绑定角色输入"), *Pawn->GetName());
}

UPlanetEditComponent* UPlanetCharacterInputComponent::GetEditComponentInEditMode() const
{
	const APlanetCharacter* Pawn = GetPawn();
	UPlanetEditComponent* Edit = Pawn ? Pawn->GetPlanetEditComponent() : nullptr;
	return Edit && Edit->IsEditMode() ? Edit : nullptr;
}

void UPlanetCharacterInputComponent::OnMoveInput(const FInputActionValue& Value)
{
	APlanetCharacter* Pawn = GetPawn();
	if (!Pawn)
	{
		return;
	}

	Pawn->Move(Value.Get<FVector2D>());
}

void UPlanetCharacterInputComponent::OnLookInput(const FInputActionValue& Value)
{
	APlanetCharacter* Pawn = GetPawn();
	if (!Pawn)
	{
		return;
	}

	// 视角只在按住右键时跟鼠标动。不按的时候鼠标划过屏幕不带画面，角色自己拐弯
	// （A/D 横移时 bOrientRotationToMovement 转的身）也不会把相机拖走。
	if (!bLookHeld)
	{
		return;
	}

	const FVector2D Axis = Value.Get<FVector2D>();
	Pawn->Look(Axis.X * LookSensitivity, (bInvertLookY ? -Axis.Y : Axis.Y) * LookSensitivity);
}

void UPlanetCharacterInputComponent::OnLookRightStarted(const FInputActionValue& Value)
{
	APlanetCharacter* Pawn = GetPawn();
	if (!Pawn)
	{
		return;
	}

	bLookHeld = true;

	UPlanetEditComponent* Edit = GetEditComponentInEditMode();
	if (!Edit)
	{
		return;
	}

	// 右键按住期间预览切到「放」，与松开右键时 PlaceAtScreenPosition 的中心一致。
	Edit->SetPreviewPlacing(true);

	APlayerController* PC = Cast<APlayerController>(Pawn->GetController());
	double MouseX = 0.0;
	double MouseY = 0.0;
	bEditRightPressValid = PC && PC->GetMousePosition(MouseX, MouseY);
	if (bEditRightPressValid)
	{
		EditRightPressPosition = FVector2D(MouseX, MouseY);
	}
}

void UPlanetCharacterInputComponent::OnLookRightCompleted(const FInputActionValue& Value)
{
	APlanetCharacter* Pawn = GetPawn();
	if (!Pawn)
	{
		return;
	}

	bLookHeld = false;

	UPlanetEditComponent* Edit = GetEditComponentInEditMode();
	// 松开右键后预览回到「挖」。必须在下面的早退**之前**复位：没记下按下位置
	// （bEditRightPressValid 为 false）时也要复位，否则预览会一直停在「放」。
	if (Edit)
	{
		Edit->SetPreviewPlacing(false);
	}

	if (!bEditRightPressValid)
	{
		return;
	}
	bEditRightPressValid = false;

	if (!Edit)
	{
		return;
	}

	APlayerController* PC = Cast<APlayerController>(Pawn->GetController());
	double MouseX = 0.0;
	double MouseY = 0.0;
	if (!PC || !PC->GetMousePosition(MouseX, MouseY))
	{
		return;
	}

	// 按住右键拖拽是转视角，松开时若几乎没动才算「点放」。拖动中相机转了，
	// 光标回到原点附近也不会误解成点击，因为玩家在拖动时不会刻意回到起点。
	const double MovedSq = FVector2D::DistSquared(
		EditRightPressPosition, FVector2D(MouseX, MouseY));
	if (MovedSq > StellarEraInput::ClickMoveThresholdPx * StellarEraInput::ClickMoveThresholdPx)
	{
		return;
	}

	Edit->PlaceAtScreenPosition(FVector2D(MouseX, MouseY));
}

void UPlanetCharacterInputComponent::OnJumpInput(const FInputActionValue& Value)
{
	APlanetCharacter* Pawn = GetPawn();
	if (!Pawn)
	{
		return;
	}

	Pawn->Jump();
}

void UPlanetCharacterInputComponent::OnSprintStarted(const FInputActionValue& Value)
{
	APlanetCharacter* Pawn = GetPawn();
	if (!Pawn)
	{
		return;
	}

	Pawn->SetSprinting(true);
}

void UPlanetCharacterInputComponent::OnSprintCompleted(const FInputActionValue& Value)
{
	APlanetCharacter* Pawn = GetPawn();
	if (!Pawn)
	{
		return;
	}

	Pawn->SetSprinting(false);
}

void UPlanetCharacterInputComponent::OnZoomInput(const FInputActionValue& Value)
{
	APlanetCharacter* Pawn = GetPawn();
	if (!Pawn)
	{
		return;
	}

	const float Scroll = Value.Get<float>();

	if (UPlanetEditComponent* Edit = GetEditComponentInEditMode())
	{
		// 编辑模式下滚轮改笔刷尺寸，相机臂长保持不动 —— 挖洞时要盯着脚下，
		// 顺手把镜头拉远/拉近会打断瞄准。一格滚轮 = 一档，一档多少体素问组件；
		// 按住 Ctrl 走粗调（×10）。
		if (!FMath::IsNearlyZero(Scroll))
		{
			APlayerController* PC = Cast<APlayerController>(Pawn->GetController());
			const bool bCoarse = PC
				&& (PC->IsInputKeyDown(EKeys::LeftControl) || PC->IsInputKeyDown(EKeys::RightControl));
			Edit->AdjustBrushSizeByStep(Scroll > 0.f, bCoarse);
		}
		return;
	}

	Pawn->AddZoom(Scroll);
}

void UPlanetCharacterInputComponent::OnEditModeToggle(const FInputActionValue& Value)
{
	APlanetCharacter* Pawn = GetPawn();
	if (!Pawn)
	{
		return;
	}

	// 不查「矿物透视是否开着」：星图退出与每一次 Possess 都会 ExitMineralViewIfActive，
	// 操控角色时它必然关闭。那条判断只对星图相机有意义（见 UStarMapCameraInputComponent）。
	if (UPlanetEditComponent* Edit = Pawn->GetPlanetEditComponent())
	{
		Edit->ToggleEditMode();
	}
}

void UPlanetCharacterInputComponent::OnBrushSize(const FInputActionValue& Value)
{
	APlanetCharacter* Pawn = GetPawn();
	if (!Pawn)
	{
		return;
	}

	const float Axis = Value.Get<float>();
	if (FMath::IsNearlyZero(Axis))
	{
		return;
	}

	if (UPlanetEditComponent* Edit = GetEditComponentInEditMode())
	{
		// 星图区间跨到 1280，一档 10 体素要按一百多下 —— 按住 Ctrl 走 ×10 粗调。
		APlayerController* PC = Cast<APlayerController>(Pawn->GetController());
		const bool bCoarse = PC
			&& (PC->IsInputKeyDown(EKeys::LeftControl) || PC->IsInputKeyDown(EKeys::RightControl));
		Edit->AdjustBrushSizeByStep(Axis > 0.f, bCoarse);
	}
}
