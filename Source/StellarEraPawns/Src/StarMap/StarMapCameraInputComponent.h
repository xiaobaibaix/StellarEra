// Copyright Voxel Plugin SAS. All Rights Reserved.
// 星图相机的键鼠输入：绑在相机身上，代码不住在相机类里。

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "UObject/Interface.h"
#include "StarMapCameraInputComponent.generated.h"

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
 * 星图相机判「鼠标位移该拿去干什么」时要问控制器的那两句话。
 *
 * 左键按住与矿物透视都是控制器的状态（选择逻辑、图例面板都在那边），
 * 组件住 StellarEraPawns、控制器住 StellarEra，直接 include 会反向依赖
 * Game 模块。做法照 ICosmosBody：游戏模块单向依赖 Pawns，实现放控制器上。
 *
 * 只有这两个：编辑模式与连续挖直接从相机 Pawn 自己的
 * GetPlanetEditComponent() 读，不绕控制器。
 */
class STELLARERAPAWNS_API IStarMapLookContext
{
	GENERATED_BODY()

public:
	/** 星图里左键是否按住（选择用的按下 / 拖拽判定）。 */
	virtual bool IsStarMapSelectHeld() const = 0;

	/** 矿物透视（图例）是否激活；激活时左键归图例，不归绕行。 */
	virtual bool IsMineralViewActive() const = 0;
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

	/** 控制器的星图视角状态；拿不到时为空。 */
	const IStarMapLookContext* GetLookContext() const;

	/** 相机 Pawn 自己的编辑组件，且只在编辑模式下才返回。 */
	const UPlanetEditComponent* GetEditComponentInEditMode() const;

	void OnLookInput(const FInputActionValue& Value);
	void OnLookActivateStarted(const FInputActionValue& Value);
	void OnLookActivateCompleted(const FInputActionValue& Value);
	void OnLookMiddleStarted(const FInputActionValue& Value);
	void OnLookMiddleCompleted(const FInputActionValue& Value);
	void OnZoomInput(const FInputActionValue& Value);

	/** 右键是否按住（Aim 转头）。 */
	bool bLookHeld = false;

	/** 中键是否按住（编辑 / 矿物透视下借用左键的绕行）。 */
	bool bMiddleHeld = false;
};
