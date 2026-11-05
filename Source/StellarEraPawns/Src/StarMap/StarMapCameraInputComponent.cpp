// Copyright Voxel Plugin SAS. All Rights Reserved.

#include "StarMapCameraInputComponent.h"

#include "EnhancedInputComponent.h"
#include "GameFramework/Controller.h"
#include "InputActionValue.h"
#include "PlanetEditComponent.h"
#include "StarMapCameraPawn.h"
#include "StellarEraInputActions.h"

AStarMapCameraPawn* UStarMapCameraInputComponent::GetPawn() const
{
	return Cast<AStarMapCameraPawn>(GetOwner());
}

const IStarMapLookContext* UStarMapCameraInputComponent::GetLookContext() const
{
	// 用非 const 的 AController* 做 Cast，避开「const UObject* 能否 Cast 到接口」这个坑；
	// 接口方法全是 const，返回值再收窄成 const 指针。
	AController* Controller = GetPawn() ? GetPawn()->GetController() : nullptr;
	return Cast<IStarMapLookContext>(Controller);
}

const UPlanetEditComponent* UStarMapCameraInputComponent::GetEditComponentInEditMode() const
{
	const AStarMapCameraPawn* Pawn = GetPawn();
	const UPlanetEditComponent* Edit = Pawn ? Pawn->GetPlanetEditComponent() : nullptr;
	return Edit && Edit->IsEditMode() ? Edit : nullptr;
}

EStarMapLookMode UStarMapCameraInputComponent::ResolveLookMode(
	const bool bRightHeld,
	const bool bLeftHeld,
	const bool bMiddleHeld,
	const bool bEditMode,
	const bool bContinuousDigging,
	const bool bActiveMineralView)
{
	// 连续挖期间一个键都不放行：挖掘点跟着光标走，视角一动路径就画歪了。
	// 转头也算 —— 视线一变，屏幕射线跟着变，挖掘点一样会漂。
	if (bContinuousDigging)
	{
		return EStarMapLookMode::None;
	}

	// 右键优先：它只转头、不挪相机，与同时按住的其它键不冲突。
	if (bRightHeld)
	{
		return EStarMapLookMode::Aim;
	}

	// 中键绕行不看模式：编辑 / 矿物透视下左键被借走时，它就是唯一的绕行键。
	if (bMiddleHeld)
	{
		return EStarMapLookMode::Orbit;
	}

	// 左键在编辑模式（挖掘 / 填充）与矿物透视（图例点击）里已经被借走，
	// 这两种状态下按下左键必须什么都不做。
	if (bLeftHeld && !bEditMode && !bActiveMineralView)
	{
		return EStarMapLookMode::Orbit;
	}

	return EStarMapLookMode::None;
}

void UStarMapCameraInputComponent::Bind(UInputComponent* PlayerInputComponent)
{
	// 每次 Possess 都会重绑。按着鼠标切进 / 切出星图时，松开事件会送到别的 Pawn
	// 身上，标志就永远停在 true —— 一进星图没按键也在转视角。在这里清一次。
	// 放在早退之前：绑失败时输入本来就全哑，但标志不该留着上一轮的旧值。
	bLookHeld = false;
	bMiddleHeld = false;

	AStarMapCameraPawn* Pawn = GetPawn();
	UEnhancedInputComponent* EnhancedInput = Cast<UEnhancedInputComponent>(PlayerInputComponent);
	IStellarEraInputSource* Source = Pawn ? Cast<IStellarEraInputSource>(Pawn->GetController()) : nullptr;
	if (!Pawn || !EnhancedInput || !Source)
	{
		// 静默解绑等于「星图对键鼠完全没反应」——留一条 Error，别让它变成哑故障。
		UE_LOG(LogTemp, Error, TEXT("[Input] AStarMapCameraPawn 绑定失败：Pawn=%s EnhancedInput=%s InputSource=%s Controller=%s"),
			*GetNameSafe(Pawn),
			EnhancedInput ? TEXT("是") : TEXT("否"),
			Source ? TEXT("是") : TEXT("否"),
			*GetNameSafe(Pawn ? Pawn->GetController() : nullptr));
		return;
	}

	const FStellarEraInputActions& Input = Source->GetStellarEraInputActions();

	EnhancedInput->BindAction(Input.Look, ETriggerEvent::Triggered, this, &UStarMapCameraInputComponent::OnLookInput);
	EnhancedInput->BindAction(Input.LookActivate, ETriggerEvent::Started, this, &UStarMapCameraInputComponent::OnLookActivateStarted);
	EnhancedInput->BindAction(Input.LookActivate, ETriggerEvent::Completed, this, &UStarMapCameraInputComponent::OnLookActivateCompleted);
	EnhancedInput->BindAction(Input.LookMiddle, ETriggerEvent::Started, this, &UStarMapCameraInputComponent::OnLookMiddleStarted);
	EnhancedInput->BindAction(Input.LookMiddle, ETriggerEvent::Completed, this, &UStarMapCameraInputComponent::OnLookMiddleCompleted);

	// 只绑 Triggered：滚轮每格是一次独立冲量，不滚就不产生值，没有 Completed 可收。
	EnhancedInput->BindAction(Input.Zoom, ETriggerEvent::Triggered, this, &UStarMapCameraInputComponent::OnZoomInput);

	// 绑定成功也给一条 Verbose：「没报错」不等于「绑上了」，无头跑时用它当正证据。
	UE_LOG(LogTemp, Verbose, TEXT("[Input] AStarMapCameraPawn(%s) 已绑定星图相机输入"), *Pawn->GetName());
}

void UStarMapCameraInputComponent::OnLookInput(const FInputActionValue& Value)
{
	AStarMapCameraPawn* Pawn = GetPawn();
	if (!Pawn)
	{
		return;
	}

	const FVector2D Axis = Value.Get<FVector2D>();
	const float DeltaYaw = Axis.X * LookSensitivity;
	const float DeltaPitch = (bInvertLookY ? -Axis.Y : Axis.Y) * LookSensitivity;

	const UPlanetEditComponent* Edit = GetEditComponentInEditMode();
	const IStarMapLookContext* Context = GetLookContext();

	switch (ResolveLookMode(
		bLookHeld,
		Context && Context->IsStarMapSelectHeld(),
		bMiddleHeld,
		Edit != nullptr,
		Edit && Edit->IsContinuousDigging(),
		Context && Context->IsMineralViewActive()))
	{
	case EStarMapLookMode::Orbit:
		Pawn->OrbitLook(DeltaYaw, DeltaPitch);
		break;
	case EStarMapLookMode::Aim:
		Pawn->AimLook(DeltaYaw, DeltaPitch);
		break;
	case EStarMapLookMode::None:
	default:
		break;
	}
}

void UStarMapCameraInputComponent::OnLookActivateStarted(const FInputActionValue& Value)
{
	// 「按住右键 = 转头」。角色的右键（视角开关 + 编辑点放）由
	// UPlanetCharacterInputComponent 自己绑同一个动作、自己处理。
	bLookHeld = true;
}

void UStarMapCameraInputComponent::OnLookActivateCompleted(const FInputActionValue& Value)
{
	bLookHeld = false;
}

void UStarMapCameraInputComponent::OnLookMiddleStarted(const FInputActionValue& Value)
{
	// 编辑模式下左键被借走时的绕行键，见 ResolveLookMode。
	bMiddleHeld = true;
}

void UStarMapCameraInputComponent::OnLookMiddleCompleted(const FInputActionValue& Value)
{
	bMiddleHeld = false;
}

void UStarMapCameraInputComponent::OnZoomInput(const FInputActionValue& Value)
{
	if (AStarMapCameraPawn* Pawn = GetPawn())
	{
		Pawn->AddZoom(Value.Get<float>());
	}
}
