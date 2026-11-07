// Copyright Voxel Plugin SAS. All Rights Reserved.
// 星图相机的键鼠输入：绑在相机身上，代码不住在相机类里。

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "StarMapCameraInputComponent.generated.h"

class APlayerController;
class AStarMapCameraPawn;
class UInputComponent;
class UPlanetEditComponent;
struct FInputActionValue;

/** 鼠标位移的去向：`Orbit` 绕焦点转（位置跟着变），`Aim` 只转朝向，`None` 编辑 / 连挖时不动。 */
enum class EStarMapLookMode : uint8
{
	None,
	Orbit,
	Aim,
};

/**
 * AStarMapCameraPawn 的全部输入：绑定、回调、只服务输入的状态。与 USpaceshipInputComponent
 * 同形（一个 Bind() + 一组 On*Input）；三个 Pawn 各存一份 LookSensitivity，手感本就可以不同。
 */
UCLASS()
class STELLARERAPAWNS_API UStarMapCameraInputComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	/** 把控制器给的动作表绑到本组件上。由 AStarMapCameraPawn::SetupPlayerInputComponent 调用。 */
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
		bool bContinuousDigging,
		bool bActiveMineralView);

private:
	/** 所属相机；组件只可能挂在星图相机上。 */
	AStarMapCameraPawn* GetPawn() const;

	/** 相机 Pawn 自己的编辑组件，且只在编辑模式下才返回。 */
	UPlanetEditComponent* GetEditComponentInEditMode() const;

	void OnLookInput(const FInputActionValue& Value);
	void OnLookActivateStarted(const FInputActionValue& Value);
	void OnLookActivateCompleted(const FInputActionValue& Value);
	void OnLookMiddleStarted(const FInputActionValue& Value);
	void OnLookMiddleCompleted(const FInputActionValue& Value);
	void OnZoomInput(const FInputActionValue& Value);

	/** G 键：切换星图相机的编辑模式。矿物透视开着时忽略（左键归图例）。 */
	void OnEditModeToggle(const FInputActionValue& Value);

	/** `[` / `]`：编辑模式下调笔刷尺寸（±1，按住 Ctrl ×10）。非编辑模式下无反应。 */
	void OnBrushSize(const FInputActionValue& Value);

	/** H 键：进入 / 退出矿物透视。状态与逻辑都在相机 Pawn 上。 */
	void OnMineralViewToggle(const FInputActionValue& Value);

	/** 数字键 1..9：切换第 ElementIndex 个元素（下标从 0 起）。 */
	void OnMineralFilter(const FInputActionValue& Value, int32 ElementIndex);

	/** 数字键 0：全部打开。 */
	void OnMineralShowAll(const FInputActionValue& Value);

	/** 左键按下：笔刷操作 → 记录按下位置。与松开的位移一起区分点选与拖拽。 */
	void OnStarMapSelectStarted(const FInputActionValue& Value);

	/** 左键松开：位移没超阈值就拾取鼠标下的天体（再点一次取消选中）。 */
	void OnStarMapSelectCompleted(const FInputActionValue& Value);

	/** M：进 / 出星图。真正换 Possess 的实现住在控制器，经 IStellarEraSessionCommands 转发。 */
	void OnStarMapToggle(const FInputActionValue& Value);

	/** 缓存的玩家控制器；拿不到时为空。 */
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
