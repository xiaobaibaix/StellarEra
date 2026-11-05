// Copyright Voxel Plugin SAS. All Rights Reserved.
// 飞船的键鼠输入：绑在飞船身上，代码不住在飞船类里。

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "SpaceshipInputComponent.generated.h"

class ASpaceshipPawn;
class UInputComponent;
struct FInputActionValue;

/**
 * ASpaceshipPawn 的全部输入：绑定、回调、只服务输入的状态。
 *
 * 与 UPlanetCharacterInputComponent 同构，但飞船**不设「按住右键」的门** ——
 * 鼠标就是它的操纵杆，要按住右键才能转向等于飞不动。
 */
UCLASS()
class STELLARERAPAWNS_API USpaceshipInputComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	/** 把控制器给的动作表绑到本组件上。由 ASpaceshipPawn::SetupPlayerInputComponent 调用。 */
	void Bind(UInputComponent* PlayerInputComponent);

	/** 鼠标灵敏度（度/像素）。飞船与角色的手感可以不一样，所以各存一份。 */
	UPROPERTY(EditAnywhere, Category = "Input")
	float LookSensitivity = 1.2f;

	/** 上下视角反向。觉得鼠标上下颠倒时把它改成 true。 */
	UPROPERTY(EditAnywhere, Category = "Input")
	bool bInvertLookY = false;

private:
	/** 所属飞船；组件只可能挂在飞船上。 */
	ASpaceshipPawn* GetPawn() const;

	void OnLookInput(const FInputActionValue& Value);
	void OnThrustInput(const FInputActionValue& Value);
	void OnRollInput(const FInputActionValue& Value);
	void OnVerticalInput(const FInputActionValue& Value);
	void OnBoostStarted(const FInputActionValue& Value);
	void OnBoostCompleted(const FInputActionValue& Value);
	void OnCruiseToggle(const FInputActionValue& Value);
	void OnDebugViewToggle(const FInputActionValue& Value);
	void OnZoomInput(const FInputActionValue& Value);

	/** 通过 HUD 的 IStellarEraNoticeSink 弹一条提示；拿不到 HUD 时静默跳过。 */
	void ShowNotice(const FString& Text) const;
};
