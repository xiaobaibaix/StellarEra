// Copyright Voxel Plugin SAS. All Rights Reserved.

#include "StarMapInputComponent.h"

#include "CosmosSimulationSubsystem.h"
#include "EnhancedInputComponent.h"
#include "Engine/World.h"
#include "GameFramework/Controller.h"
#include "GameFramework/HUD.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "InputActionValue.h"
#include "InputCoreTypes.h"
#include "PlanetEditComponent.h"
#include "PlanetEditUI.h"
#include "StarMapCameraInputActions.h"
#include "StarMapPicking.h"
#include "StarMapRigComponent.h"
#include "StellarEraInputActionBuilder.h"
#include "StellarEraInputSource.h"
#include "StellarEraNoticeSink.h"

APawn* UStarMapInputComponent::GetPawn() const
{
	APawn* OwnerPawn = Cast<APawn>(GetOwner());
	const UStarMapRigComponent* Rig = GetRig();
	return (OwnerPawn && Rig && Rig->IsActive()) ? OwnerPawn : nullptr;
}

UStarMapRigComponent* UStarMapInputComponent::GetRig() const
{
	const AActor* Owner = GetOwner();
	return Owner ? Owner->FindComponentByClass<UStarMapRigComponent>() : nullptr;
}

APlayerController* UStarMapInputComponent::GetPlayerController() const
{
	const APawn* OwnerPawn = Cast<APawn>(GetOwner());
	return OwnerPawn ? Cast<APlayerController>(OwnerPawn->GetController()) : nullptr;
}

void UStarMapInputComponent::ShowNotice(const FString& Text) const
{
	// HUD 在 UI 模块（本模块的下游），只能走接口。没有 HUD 时静默跳过。
	const APlayerController* PC = GetPlayerController();
	IStellarEraNoticeSink* Sink = PC ? Cast<IStellarEraNoticeSink>(PC->GetHUD()) : nullptr;
	if (Sink)
	{
		Sink->ShowNotice(Text);
	}
}

UPlanetEditComponent* UStarMapInputComponent::GetEditComponentInEditMode() const
{
	const UStarMapRigComponent* Rig = GetRig();
	UPlanetEditComponent* Edit = Rig ? Rig->GetPlanetEditComponent() : nullptr;
	return Edit && Edit->IsEditMode() ? Edit : nullptr;
}

EStarMapLookMode UStarMapInputComponent::ResolveLookMode(
	const bool bRightHeld,
	const bool bLeftHeld,
	const bool bMiddleHeld,
	const bool bEditMode,
	const bool bContinuousDigging)
{
	// 连续挖期间一个键都不放行：视角一动，屏幕射线跟着变，挖掘点就漂（转头也算）。
	if (bContinuousDigging)
	{
		return EStarMapLookMode::None;
	}

	// 右键优先：它只转头、不挪相机，与同时按住的其它键不冲突。
	if (bRightHeld)
	{
		return EStarMapLookMode::Aim;
	}

	// 中键绕行不看模式：编辑模式下左键被借走时，它就是唯一的绕行键。
	if (bMiddleHeld)
	{
		return EStarMapLookMode::Orbit;
	}

	// 左键在编辑模式里已被借走，此状态下什么都不做。
	if (bLeftHeld && !bEditMode)
	{
		return EStarMapLookMode::Orbit;
	}

	return EStarMapLookMode::None;
}

void UStarMapInputComponent::Bind(UInputComponent* PlayerInputComponent)
{
	// 每次 Possess 都会重绑：按着鼠标切 Pawn 时松开事件送到别处，标志会永远停在 true，
	// 一进星图没按键也在转视角；放在早退之前清一次。
	bLookHeld = false;
	bMiddleHeld = false;
	bSelectHeld = false;

	APawn* OwnerPawn = Cast<APawn>(GetOwner());
	UStarMapRigComponent* Rig = GetRig();
	UEnhancedInputComponent* EnhancedInput = Cast<UEnhancedInputComponent>(PlayerInputComponent);
	IStellarEraInputSource* Source =
		OwnerPawn ? Cast<IStellarEraInputSource>(OwnerPawn->GetController()) : nullptr;
	if (!OwnerPawn || !Rig || !EnhancedInput || !Source)
	{
		// 静默解绑等于「星图对键鼠完全没反应」——留一条 Error，别让它变成哑故障。
		UE_LOG(LogTemp, Error, TEXT("[Input] 星图输入绑定失败：Pawn=%s Rig=%s EnhancedInput=%s InputSource=%s"),
			*GetNameSafe(OwnerPawn), Rig ? TEXT("是") : TEXT("否"),
			EnhancedInput ? TEXT("是") : TEXT("否"), Source ? TEXT("是") : TEXT("否"));
		return;
	}

	// 只取星图自己那张表：别的 Pawn 的动作不归这里管。左键选择 / M 也在这张表里。
	const FStarMapCameraInputActions& Input = Source->GetStarMapCameraInputActions();

	EnhancedInput->BindAction(Input.Look, ETriggerEvent::Triggered, this, &UStarMapInputComponent::OnLookInput);
	EnhancedInput->BindAction(Input.LookActivate, ETriggerEvent::Started, this, &UStarMapInputComponent::OnLookActivateStarted);
	EnhancedInput->BindAction(Input.LookActivate, ETriggerEvent::Completed, this, &UStarMapInputComponent::OnLookActivateCompleted);
	EnhancedInput->BindAction(Input.LookMiddle, ETriggerEvent::Started, this, &UStarMapInputComponent::OnLookMiddleStarted);
	EnhancedInput->BindAction(Input.LookMiddle, ETriggerEvent::Completed, this, &UStarMapInputComponent::OnLookMiddleCompleted);

	// 只绑 Triggered：滚轮每格是一次独立冲量，不滚就不产生值，没有 Completed 可收。
	EnhancedInput->BindAction(Input.Zoom, ETriggerEvent::Triggered, this, &UStarMapInputComponent::OnZoomInput);

	// 只绑 Started：Axis1D 的 Triggered 按住时每帧触发，笔刷会瞬间冲到上限。
	EnhancedInput->BindAction(Input.EditModeToggle, ETriggerEvent::Started, this, &UStarMapInputComponent::OnEditModeToggle);
	EnhancedInput->BindAction(Input.BrushSize, ETriggerEvent::Started, this, &UStarMapInputComponent::OnBrushSize);

	// M 只在这里绑：飞船 / 角色自己的那份已删，进退都走同一个 Toggle。
	EnhancedInput->BindAction(Input.StarMapSelect, ETriggerEvent::Started,
		this, &UStarMapInputComponent::OnStarMapSelectStarted);
	EnhancedInput->BindAction(Input.StarMapSelect, ETriggerEvent::Completed,
		this, &UStarMapInputComponent::OnStarMapSelectCompleted);
	EnhancedInput->BindAction(Input.StarMapToggle, ETriggerEvent::Started,
		this, &UStarMapInputComponent::OnStarMapToggle);

	// 绑定成功也给一条 Verbose：「没报错」不等于「绑上了」，无头跑时用它当正证据。
	UE_LOG(LogTemp, Verbose, TEXT("[Input] %s 已绑定星图输入"), *OwnerPawn->GetName());
}

void UStarMapInputComponent::OnLookInput(const FInputActionValue& Value)
{
	UStarMapRigComponent* Rig = GetRig();
	if (!GetPawn() || !Rig)
	{
		return;
	}

	const FVector2D Axis = Value.Get<FVector2D>();
	const float DeltaYaw = Axis.X * LookSensitivity;
	const float DeltaPitch = (bInvertLookY ? -Axis.Y : Axis.Y) * LookSensitivity;

	const UPlanetEditComponent* Edit = GetEditComponentInEditMode();

	switch (ResolveLookMode(
		bLookHeld,
		bSelectHeld,
		bMiddleHeld,
		Edit != nullptr,
		Edit && Edit->IsContinuousDigging()))
	{
	case EStarMapLookMode::Orbit:
		Rig->OrbitLook(DeltaYaw, DeltaPitch);
		break;
	case EStarMapLookMode::Aim:
		Rig->AimLook(DeltaYaw, DeltaPitch);
		break;
	case EStarMapLookMode::None:
	default:
		break;
	}
}

void UStarMapInputComponent::OnLookActivateStarted(const FInputActionValue& Value)
{
	// 「按住右键 = 转头」。角色的右键由 UPlanetCharacterInputComponent 自己绑同一个动作。
	bLookHeld = true;
}

void UStarMapInputComponent::OnLookActivateCompleted(const FInputActionValue& Value)
{
	bLookHeld = false;
}

void UStarMapInputComponent::OnLookMiddleStarted(const FInputActionValue& Value)
{
	// 编辑模式下左键被借走时的绕行键，见 ResolveLookMode。
	bMiddleHeld = true;
}

void UStarMapInputComponent::OnLookMiddleCompleted(const FInputActionValue& Value)
{
	bMiddleHeld = false;
}

void UStarMapInputComponent::OnZoomInput(const FInputActionValue& Value)
{
	UStarMapRigComponent* Rig = GetRig();
	if (!GetPawn() || !Rig)
	{
		return;
	}

	Rig->AddZoom(Value.Get<float>());
}

void UStarMapInputComponent::OnEditModeToggle(const FInputActionValue& Value)
{
	UStarMapRigComponent* Rig = GetRig();
	if (!GetPawn() || !Rig)
	{
		return;
	}

	if (UPlanetEditComponent* Edit = Rig->GetPlanetEditComponent())
	{
		Edit->ToggleEditMode();
	}
}

void UStarMapInputComponent::OnBrushSize(const FInputActionValue& Value)
{
	UStarMapRigComponent* Rig = GetRig();
	if (!GetPawn() || !Rig)
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
		APlayerController* PC = GetPlayerController();
		const bool bCoarse = PC
			&& (PC->IsInputKeyDown(EKeys::LeftControl) || PC->IsInputKeyDown(EKeys::RightControl));
		Edit->AdjustBrushSizeByStep(Axis > 0.f, bCoarse);
	}
}

void UStarMapInputComponent::OnStarMapSelectStarted(const FInputActionValue& Value)
{
	UStarMapRigComponent* Rig = GetRig();
	if (!GetPawn() || !Rig)
	{
		return;
	}

	// 编辑模式：按钮优先，其次一次笔刷操作（按钮命中不能顺带挖到脚下）。
	UPlanetEditComponent* Edit = GetEditComponentInEditMode();
	if (Edit)
	{
		APlayerController* PC = GetPlayerController();
		double MouseX = 0.0;
		double MouseY = 0.0;
		if (!PC || !PC->GetMousePosition(MouseX, MouseY))
		{
			return;
		}
		const FVector2D MousePosition(MouseX, MouseY);

		// AHUD::Canvas 只在 PostRender 期间有效，输入回调里问 HUD 要视口尺寸永远是 (0,0)；从控制器查。
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

		// 这次点击归不归编辑，看「射线打没打到行星」；不能看 DigAtScreenPosition 返回值
		// （打到行星但没改到体素，如往空气里挖，也返回 false）。
		FPlanetEditTarget Target;
		if (Edit->TryResolveTarget(MousePosition, Target))
		{
			// Shift+左键按住 = 连续挖（沿路径挖）；Ctrl+左键 = 填一次；左键 = 挖一次。
			const bool bShift =
				PC->IsInputKeyDown(EKeys::LeftShift) || PC->IsInputKeyDown(EKeys::RightShift);
			const bool bCtrl =
				PC->IsInputKeyDown(EKeys::LeftControl) || PC->IsInputKeyDown(EKeys::RightControl);

			if (bShift)
			{
				// 按下先挖一次，之后交给组件的 Tick 节流。
				Edit->SetContinuousDigging(true);
				Edit->DigAtScreenPosition(MousePosition);
			}
			else if (bCtrl)
			{
				Edit->PlaceAtScreenPosition(MousePosition);
			}
			else
			{
				Edit->DigAtScreenPosition(MousePosition);
			}
			return;
		}

		// 编辑模式下**连按下位置都不记**：否则点在行星轮廓边缘（射线没打到地表但
		// 在拾取半径内），松开那一下会取消选中，挖着挖着焦点就没了。换选中请按 G 退出。
		return;
	}

	// 用 double 重载只为少一次转换；引擎内部是 `float X, Y` 再展宽，精度上并不占便宜。
	double MouseX = 0.0;
	double MouseY = 0.0;
	if (APlayerController* PC = GetPlayerController(); PC && PC->GetMousePosition(MouseX, MouseY))
	{
		SelectPressPosition = FVector2D(MouseX, MouseY);
		bSelectHeld = true;
	}
}

void UStarMapInputComponent::OnStarMapSelectCompleted(const FInputActionValue& Value)
{
	// 连续挖必须在所有早退之前关掉：按下时命中行星走「消费点击」分支，bSelectHeld 没置位，
	// 下面早退会跳过它。按当前 rig 查组件不会漏 —— 每条退出路径都经 SetEditMode(false) 顺手清掉。
	if (UPlanetEditComponent* Edit = GetEditComponentInEditMode())
	{
		Edit->SetContinuousDigging(false);

		// 编辑模式下按下时没记位置，这里也不拾取（挖矿期间左键只挖）；顺手清掉残留标志。
		bSelectHeld = false;
		return;
	}

	UStarMapRigComponent* Rig = GetRig();
	if (!GetPawn() || !Rig)
	{
		return;
	}

	if (!bSelectHeld)
	{
		return;
	}
	bSelectHeld = false;

	APlayerController* PC = GetPlayerController();
	double MouseX = 0.0;
	double MouseY = 0.0;
	if (!PC || !PC->GetMousePosition(MouseX, MouseY))
	{
		return;
	}

	// 按下与松开的位移大于阈值 → 这是「拖拽转视角」的收尾，不改选中。
	const double MovedSq = FVector2D::DistSquared(
		SelectPressPosition, FVector2D(MouseX, MouseY));
	if (MovedSq > StellarEraInput::ClickMoveThresholdPx * StellarEraInput::ClickMoveThresholdPx)
	{
		return;
	}

	// 点空白什么都不做：取消选中会让相机停止跟随、丢失目标；要取消就明确地点回它自己。
	AActor* Picked = StarMapPicking::PickBodyUnderScreenPosition(
		GetWorld(), PC, FVector2D(MouseX, MouseY), Rig->GetSelectionPixelRadius());
	if (!Picked)
	{
		return;
	}

	UCosmosSimulationSubsystem* Subsystem =
		GetWorld() ? GetWorld()->GetSubsystem<UCosmosSimulationSubsystem>() : nullptr;
	if (!Subsystem)
	{
		return;
	}

	Subsystem->SetSelectedBody(
		Subsystem->GetSelectedBody() == Picked ? nullptr : Picked);
}

void UStarMapInputComponent::OnStarMapToggle(const FInputActionValue& Value)
{
	// 进退都走这一个入口：M 只在本组件绑了一次（飞船 / 角色自己的那份已删）。
	if (UStarMapRigComponent* Rig = GetRig())
	{
		Rig->Toggle(GetPlayerController());
	}
}
