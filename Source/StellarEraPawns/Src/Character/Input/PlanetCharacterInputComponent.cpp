// Copyright Voxel Plugin SAS. All Rights Reserved.

#include "PlanetCharacterInputComponent.h"

#include "EnhancedInputComponent.h"
#include "GameFramework/HUD.h"
#include "GameFramework/PlayerController.h"
#include "InputActionValue.h"
#include "InputCoreTypes.h"
#include "PlanetCharacter.h"
#include "PlanetEditComponent.h"
#include "PlanetCharacterInputActions.h"
#include "PlanetEditUI.h"
#include "SessionCommands.h"
#include "SpaceshipPawn.h"
#include "StarMapCameraPawn.h"
#include "StellarEraInputActionBuilder.h"
#include "StellarEraInputSource.h"
#include "StellarEraNoticeSink.h"

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
	const FPlanetCharacterInputActions& Input = Source->GetPlanetCharacterInputActions();

	EnhancedInput->BindAction(Input.Move, ETriggerEvent::Triggered, this, &UPlanetCharacterInputComponent::OnMoveInput);
	EnhancedInput->BindAction(Input.Look, ETriggerEvent::Triggered, this, &UPlanetCharacterInputComponent::OnLookInput);
	EnhancedInput->BindAction(Input.LookActivate, ETriggerEvent::Started, this, &UPlanetCharacterInputComponent::OnLookRightStarted);
	EnhancedInput->BindAction(Input.LookActivate, ETriggerEvent::Completed, this, &UPlanetCharacterInputComponent::OnLookRightCompleted);
	EnhancedInput->BindAction(Input.Jump, ETriggerEvent::Started, this, &UPlanetCharacterInputComponent::OnJumpInput);
	EnhancedInput->BindAction(Input.Sprint, ETriggerEvent::Started, this, &UPlanetCharacterInputComponent::OnSprintStarted);
	EnhancedInput->BindAction(Input.Sprint, ETriggerEvent::Completed, this, &UPlanetCharacterInputComponent::OnSprintCompleted);

	// 只绑 Triggered：滚轮每格是一次独立冲量，不滚就不产生值，没有 Completed 可收。
	EnhancedInput->BindAction(Input.Zoom, ETriggerEvent::Triggered, this, &UPlanetCharacterInputComponent::OnZoomInput);

	// 只绑 Started：Axis1D 的 Triggered 按住时每帧触发一次，笔刷会瞬间冲到上限。
	EnhancedInput->BindAction(Input.EditModeToggle, ETriggerEvent::Started, this, &UPlanetCharacterInputComponent::OnEditModeToggle);
	EnhancedInput->BindAction(Input.BrushSize, ETriggerEvent::Started, this, &UPlanetCharacterInputComponent::OnBrushSize);

	// E / M / 左键：角色表里各有一条，E 是登船；左键只绑 Started（编辑挖掘）。
	EnhancedInput->BindAction(Input.StarMapToggle, ETriggerEvent::Started, this, &UPlanetCharacterInputComponent::OnStarMapToggle);
	EnhancedInput->BindAction(Input.Interact, ETriggerEvent::Started, this, &UPlanetCharacterInputComponent::OnInteract);
	EnhancedInput->BindAction(Input.EditPrimary, ETriggerEvent::Started, this, &UPlanetCharacterInputComponent::OnEditPrimary);

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

	// 视角只在按住右键时跟鼠标动；不按的时候走动拐弯不会把相机拖走。
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
	// 松开右键后预览回到「挖」。必须在早退**之前**复位，否则预览会一直停在「放」。
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

	// 按住右键拖拽是转视角，松开时几乎没动才算「点放」。
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
		// 编辑模式下滚轮改笔刷尺寸、相机臂长保持不动 —— 挖洞要盯着脚下。一格 = 一档，
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

	// 不查矿物透视：星图退出与每次 Possess 都会 ExitMineralViewIfActive，操控角色时必然关闭。
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

APlayerController* UPlanetCharacterInputComponent::GetPlayerController() const
{
	const APlanetCharacter* Pawn = GetPawn();
	return Pawn ? Cast<APlayerController>(Pawn->GetController()) : nullptr;
}

void UPlanetCharacterInputComponent::ShowNotice(const FString& Text) const
{
	// HUD 在 UI 模块（本模块的下游），只能走接口。没有 HUD 时静默跳过。
	const APlayerController* PC = GetPlayerController();
	IStellarEraNoticeSink* Sink = PC ? Cast<IStellarEraNoticeSink>(PC->GetHUD()) : nullptr;
	if (Sink)
	{
		Sink->ShowNotice(Text);
	}
}

void UPlanetCharacterInputComponent::OnEditPrimary(const FInputActionValue& Value)
{
	// 角色没有星图选择：左键只服务编辑，非编辑模式下整条回调无动作。
	UPlanetEditComponent* Edit = GetEditComponentInEditMode();
	if (!Edit)
	{
		return;
	}

	APlayerController* PC = GetPlayerController();
	double MouseX = 0.0;
	double MouseY = 0.0;
	if (!PC || !PC->GetMousePosition(MouseX, MouseY))
	{
		return;
	}
	const FVector2D MousePosition(MouseX, MouseY);

	// AHUD::Canvas 只在 PostRender 期间有效，输入回调在 tick 里问它只会拿到 (0,0)，改从控制器查。
	int32 ViewportSizeX = 0;
	int32 ViewportSizeY = 0;
	PC->GetViewportSize(ViewportSizeX, ViewportSizeY);
	const FVector2D ViewportSize(
		static_cast<float>(ViewportSizeX), static_cast<float>(ViewportSizeY));

	int32 ButtonIndex = INDEX_NONE;
	if (ViewportSize.X > 0.0 && ViewportSize.Y > 0.0 &&
		PlanetEditUI::HitTestBrushButton(ViewportSize, MousePosition, ButtonIndex))
	{
		const bool bSphere = ButtonIndex == 0;
		Edit->SetBrushShape(bSphere ? EPlanetBrushShape::Sphere : EPlanetBrushShape::Cube);
		ShowNotice(bSphere ? TEXT("笔刷：球体") : TEXT("笔刷：正方体"));
		return;
	}

	// 归不归编辑由「射线打没打到行星」回答：DigAtScreenPosition 返回 false 也可能是打到了
	// 却没改到体素（往空气里挖）。角色没有连续挖 / Ctrl 填充，Shift 与 Ctrl 不改变语义。
	FPlanetEditTarget Target;
	if (Edit->TryResolveTarget(MousePosition, Target))
	{
		Edit->DigAtScreenPosition(MousePosition);
	}
}

void UPlanetCharacterInputComponent::OnStarMapToggle(const FInputActionValue& Value)
{
	APlanetCharacter* Pawn = GetPawn();
	APlayerController* PC = GetPlayerController();
	if (!Pawn || !PC)
	{
		return;
	}

	// 「从这颗星球上能借用哪台星图相机」由控制器回答（Game 模块，本模块看不到）。
	IStellarEraSessionCommands* Commands = Cast<IStellarEraSessionCommands>(PC);
	AStarMapCameraPawn* Camera = Commands ? Commands->FindStarMapCameraFor(Pawn) : nullptr;
	if (!Camera)
	{
		return;
	}

	// 进星图前退出编辑模式：星图里左键是拾取，不能同时保留笔刷状态与网格。
	if (UPlanetEditComponent* Edit = GetEditComponentInEditMode())
	{
		Edit->SetEditMode(false);
	}

	// 进场（含 Possess）归相机自己。
	Camera->EnterStarMap(Pawn, PC);
}

void UPlanetCharacterInputComponent::OnInteract(const FInputActionValue& Value)
{
	APlanetCharacter* Pawn = GetPawn();
	APlayerController* PC = GetPlayerController();
	if (!Pawn || !PC)
	{
		return;
	}

	// 「最近、且近到可以登船」的飞船由控制器给 —— 关卡里有哪些飞船不在本模块的视野。
	IStellarEraSessionCommands* Commands = Cast<IStellarEraSessionCommands>(PC);
	ASpaceshipPawn* Ship = Commands ? Commands->FindBoardableShipFrom(Pawn) : nullptr;
	if (!Ship)
	{
		return;
	}

	// 上船前退出编辑模式：网格是行星上的组件，不退出会留在外面。必须在 Board 之前。
	if (UPlanetEditComponent* Edit = GetEditComponentInEditMode())
	{
		Edit->SetEditMode(false);
	}

	// 登船的机械动作归飞船（附着 / 隐藏 / 记账），换控制对象归控制器。
	Ship->Board(Pawn);
	PC->Possess(Ship);
}
