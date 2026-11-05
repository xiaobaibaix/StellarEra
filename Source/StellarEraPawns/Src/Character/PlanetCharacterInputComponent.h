// Copyright Voxel Plugin SAS. All Rights Reserved.
// 角色的键鼠输入：绑在角色身上，代码不住在角色类里。

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "PlanetCharacterInputComponent.generated.h"

class APlanetCharacter;
class UInputComponent;
class UPlanetEditComponent;
struct FInputActionValue;

/**
 * APlanetCharacter 的全部输入：绑定、回调、只服务输入的状态。
 *
 * 为什么是组件而不是把代码留在角色里：这些回调既不是玩法也不是物理，却占着
 * 角色头的篇幅；拆出来之后角色类只剩「怎么走、怎么看」，输入改起来不用再翻它。
 *
 * 为什么是 UObject 组件而不是普通 C++ 类：`UEnhancedInputComponent::BindAction`
 * 的成员函数重载要求接收者是 UObject（换 lambda 会丢 GC 安全与反射签名检查）。
 * 由角色在构造函数里 CreateDefaultSubobject，生命周期天然跟着角色。
 */
UCLASS()
class STELLARERAPAWNS_API UPlanetCharacterInputComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	/**
	 * 把控制器给的动作表绑到本组件上。由 APlanetCharacter::SetupPlayerInputComponent
	 * 调用，引擎只在本地 Possess 这个 Pawn 时走到那里。
	 */
	void Bind(UInputComponent* PlayerInputComponent);

	/** 鼠标灵敏度（度/像素）。 */
	UPROPERTY(EditAnywhere, Category = "Input")
	float LookSensitivity = 1.2f;

	/** 上下视角反向。觉得鼠标上下颠倒时把它改成 true。 */
	UPROPERTY(EditAnywhere, Category = "Input")
	bool bInvertLookY = false;

private:
	/** 所属角色；组件只可能挂在角色上。 */
	APlanetCharacter* GetPawn() const;

	void OnMoveInput(const FInputActionValue& Value);
	void OnLookInput(const FInputActionValue& Value);
	void OnLookRightStarted(const FInputActionValue& Value);
	void OnLookRightCompleted(const FInputActionValue& Value);
	void OnJumpInput(const FInputActionValue& Value);
	void OnSprintStarted(const FInputActionValue& Value);
	void OnSprintCompleted(const FInputActionValue& Value);
	void OnZoomInput(const FInputActionValue& Value);

	/** 当前打开的编辑组件；不在编辑模式、或蓝图没挂组件时为空。 */
	UPlanetEditComponent* GetEditComponentInEditMode() const;

	/** 右键是否按住。松开时视角不响应鼠标，走动时相机不会被拖走。 */
	bool bLookHeld = false;

	/** 编辑模式右键按下的屏幕位置。松开时用它区分「点击放置」与「拖拽转视角」。 */
	FVector2D EditRightPressPosition = FVector2D::ZeroVector;

	/** 右键按下时是否记下了编辑位置（不是编辑模式 / 没拿到鼠标时为 false）。 */
	bool bEditRightPressValid = false;
};
