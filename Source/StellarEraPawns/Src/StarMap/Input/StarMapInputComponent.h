// Copyright Voxel Plugin SAS. All Rights Reserved.
// 星图的键鼠输入：与 rig 平级挂在飞船 / 角色身上，代码不住在 Pawn 类里。

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "StarMapInputComponent.generated.h"

class APlayerController;
class APawn;
class UInputComponent;
class UPlanetEditComponent;
class UStarMapRigComponent;
struct FInputActionValue;

/** 鼠标位移的去向：`Orbit` 绕焦点转（位置跟着变），`Aim` 只转朝向，`None` 编辑 / 连挖时不动。 */
enum class EStarMapLookMode : uint8
{
	None,
	Orbit,
	Aim,
};

/**
 * 星图的全部输入：绑定、回调、只服务输入的状态。与 USpaceshipInputComponent 同形
 * （一个 Bind() + 一组 On*Input）；星图与飞船、角色各存一份 LookSensitivity，手感本就可以不同。
 */
UCLASS()
class STELLARERAPAWNS_API UStarMapInputComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	/** 把控制器给的动作表绑到本组件上。由飞船 / 角色的 SetupPlayerInputComponent 调用。 */
	void Bind(UInputComponent* PlayerInputComponent);

	/** 鼠标灵敏度（度/像素）。星图与飞船、角色各存一份，见类注释。 */
	UPROPERTY(EditAnywhere, Category = "Input")
	float LookSensitivity = 1.2f;

	/** 上下视角反向。觉得鼠标上下颠倒时把它改成 true。 */
	UPROPERTY(EditAnywhere, Category = "Input")
	bool bInvertLookY = false;

	/**
	 * 星图鼠标位移的归属（纯函数，测试直接调）。按键只能靠标志位区分：连续挖 → None
	 * （视角一动屏幕射线就变）；右键 → Aim；中键 → Orbit；左键 → Orbit，编辑 / 矿物透视下为 None。
	 */
	static EStarMapLookMode ResolveLookMode(
		bool bRightHeld,
		bool bLeftHeld,
		bool bMiddleHeld,
		bool bEditMode,
		bool bContinuousDigging);

private:
	/** 所属 Pawn；只在星图激活时返回，除 M 外的回调据此空转。 */
	APawn* GetPawn() const;

	/** owner 上的星图 rig。 */
	UStarMapRigComponent* GetRig() const;

	/** rig 所属 Pawn 上的编辑组件，且只在编辑模式下才返回。 */
	UPlanetEditComponent* GetEditComponentInEditMode() const;

	void OnLookInput(const FInputActionValue& Value);
	void OnLookActivateStarted(const FInputActionValue& Value);
	void OnLookActivateCompleted(const FInputActionValue& Value);
	void OnLookMiddleStarted(const FInputActionValue& Value);
	void OnLookMiddleCompleted(const FInputActionValue& Value);
	void OnZoomInput(const FInputActionValue& Value);

	/** G 键：切换星图编辑模式。矿物透视开着时忽略（左键归图例）。 */
	void OnEditModeToggle(const FInputActionValue& Value);

	/** `[` / `]`：编辑模式下调笔刷尺寸（±1，按住 Ctrl ×10）。非编辑模式下无反应。 */
	void OnBrushSize(const FInputActionValue& Value);

	/** 左键按下：笔刷操作 → 记录按下位置。与松开的位移一起区分点选与拖拽。 */
	void OnStarMapSelectStarted(const FInputActionValue& Value);

	/** 左键松开：位移没超阈值就拾取鼠标下的天体（再点一次取消选中）。 */
	void OnStarMapSelectCompleted(const FInputActionValue& Value);

	/** M：进 / 出星图；状态与相机都在 rig 里，本组件只负责转发。 */
	void OnStarMapToggle(const FInputActionValue& Value);

	/** owner Pawn 的玩家控制器；拿不到时为空。 */
	APlayerController* GetPlayerController() const;

	/** 弹一条提示（HUD 在下游模块，只能走 IStellarEraNoticeSink）。 */
	void ShowNotice(const FString& Text) const;

	/** 右键是否按住（Aim 转头）。 */
	bool bLookHeld = false;

	/** 中键是否按住（编辑 / 矿物透视下借用左键的绕行）。 */
	bool bMiddleHeld = false;

	/** 左键是否按住。选择逻辑与视角分派都读它（不再绕控制器查询）。 */
	bool bSelectHeld = false;

	/** 左键按下的屏幕位置（像素），松开时算位移判点击 / 拖拽。 */
	FVector2D SelectPressPosition = FVector2D::ZeroVector;
};
