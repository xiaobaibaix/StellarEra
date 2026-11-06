// Copyright Voxel Plugin SAS. All Rights Reserved.
// 星图相机的键鼠输入：绑在相机身上，代码不住在相机类里。

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "UObject/Interface.h"
#include "StarMapCameraInputComponent.generated.h"

class APlayerController;
class AStarMapCameraPawn;
class UInputComponent;
class UPlanetEditComponent;
struct FInputActionValue;

/**
 * 星图里鼠标位移该拿去干什么。
 *
 * - `Orbit`：绕焦点转（相机位置跟着变），见 AStarMapCameraPawn::OrbitLook。
 * - `Aim`：只转视线朝向（相机位置不变），见 AStarMapCameraPawn::AimLook。
 * - `None`：正在编辑 / 连续挖，视角一律不动。
 */
enum class EStarMapLookMode : uint8
{
	None,
	Orbit,
	Aim,
};

UINTERFACE(MinimalAPI, meta = (CannotImplementInterfaceInBlueprint))
class UStarMapLookContext : public UInterface
{
	GENERATED_BODY()
};

/**
 * 星图相机需要控制器配合的一件事：把提示弹到屏幕上。
 *
 * 相机没有 HUD，矿物透视进不去时的提示只能经控制器转出去。组件住
 * StellarEraPawns、控制器住 StellarEra，直接 include 会反向依赖 Game 模块。
 * 做法照 ICosmosBody：游戏模块单向依赖 Pawns，实现放控制器上。
 *
 * 左键是否按住、编辑模式、连续挖、矿物透视是否激活都直接从本组件 / 相机 Pawn
 * 自己读，不绕控制器。
 */
class STELLARERAPAWNS_API IStarMapLookContext
{
	GENERATED_BODY()

public:
	/** 在屏幕上弹一条带时长的提示。相机没有 HUD，用它报「进不了矿物透视」的原因。 */
	virtual void ShowStarMapPrompt(const FString& Text, float Seconds) = 0;
};

/**
 * AStarMapCameraPawn 的全部输入：绑定、回调、只服务输入的状态。
 *
 * 与 USpaceshipInputComponent 同形：一个 Bind()、一组 On*Input，回调里直接
 * 判着走，不另抽分派层。三个 Pawn 各存一份 LookSensitivity —— 星图、飞船、
 * 角色的手感本来就可以不同。
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
	 * 星图的鼠标位移该拿去哪。纯函数，OnLookInput 调它，测试直接调它。
	 *
	 * 星图的鼠标位移只在按住鼠标（CaptureDuringMouseDown）时才有值，所以
	 * 「按的是哪个键」只能靠按住的标志位区分。分派规则（按优先级）：
	 *   - 连续挖期间一律 None —— 挖掘点跟着光标走，视角一动就画歪（转头也算，
	 *     视线一变，屏幕射线就变了）。
	 *   - 右键 → Aim：只转头、不挪相机，任何模式都可用。
	 *   - 中键 → Orbit：编辑 / 矿物透视下左键被借走时，绕行改由它承担。
	 *   - 左键 → Orbit，但编辑模式（挖掘 / 填充）与矿物透视（图例）下左键已被
	 *     借走，只能是 None。
	 *   - 都没按 → None。
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

	/** 控制器的星图上下文；拿不到时为空。 */
	IStarMapLookContext* GetLookContext() const;

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

	/**
	 * 左键按下：按钮 → 笔刷操作 → 记录按下位置预备拾取。
	 *
	 * 按下与松开的位移区分「点选天体」与「拖拽转视角」，见 OnStarMapSelectCompleted。
	 */
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
