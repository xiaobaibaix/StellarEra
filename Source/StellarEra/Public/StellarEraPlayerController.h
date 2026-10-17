// Copyright Voxel Plugin SAS. All Rights Reserved.
// 玩家控制器：构造输入资产、分派输入、编排登船/离船。

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "StellarEraPlayerController.generated.h"

class AStarMapCameraPawn;
class ASpaceshipPawn;
class UInputAction;
class UInputMappingContext;
struct FInputActionValue;

/**
 * 本项目的玩家控制器。
 *
 * 输入资产（IMC / IA）在运行时用 C++ 构造：零手工步骤，改完代码立刻能跑，
 * 不需要先在编辑器里点出一堆资产。代价是键位暂时不能在编辑器里改；等键位
 * 定稿后再换成 .uasset 是平滑的替换（数据结构一样，只是来源变了）。
 *
 * 所有动作都绑定在本控制器上，再按“当前 Possess 的是哪个 Pawn”分派。
 * 这样切换角色/飞船时不需要重新绑定输入。
 */
UCLASS()
class AStellarEraPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	AStellarEraPlayerController();

	virtual void SetupInputComponent() override;
	virtual void BeginPlay() override;

	/**
	 * 开局直接坐进最近的飞船。由 AStellarEraGameMode 在 BeginPlay 之后延迟一帧调用。
	 *
	 * 复用 BoardShip，所以角色会被登记为已登船：隐藏、附着在船上、MOVE_None。
	 * 飞船停稳时按 E 就能正常离船 —— 这是与 DebugPossessShip 的关键差别，后者
	 * 直接 Possess，不登记登船角色，因此永远不会出现离船提示。
	 *
	 * @return 找到飞船并完成登船为 true；当前 Pawn 不是角色、或关卡里没有
	 *         飞船时为 false。
	 */
	bool EnterNearestShip();

protected:
	/** 构造 IMC 与全部 IA。幂等：重复调用会直接返回。 */
	void BuildInputAssets();

	/**
	 * 按当前 Possess 的 Pawn 切换鼠标模式。
	 *
	 * 角色：光标常驻可见，鼠标只在按住右键时被视口捕获（＝只在按住右键时
	 * 有位移值，相机也只在按住右键时转）。飞船：光标隐藏、永久捕获。
	 *
	 * 挂在 OnPossess 上而不是登船/离船两处：本工程换 Pawn 的每一条路径
	 * （开局 Possess、登船、离船、DebugPossessShip）最后都走 Possess。
	 */
	void ApplyMouseModeForPawn(APawn* InPawn);

	virtual void OnPossess(APawn* InPawn) override;

	// --- 角色输入回调 ---
	void OnMove(const FInputActionValue& Value);
	void OnLook(const FInputActionValue& Value);

	/** 右键按下 / 松开。只当角色视角的开关用，飞船的转向不看它。 */
	void OnLookActivateStarted(const FInputActionValue& Value);
	void OnLookActivateCompleted(const FInputActionValue& Value);
	void OnJump(const FInputActionValue& Value);
	void OnSprintStarted(const FInputActionValue& Value);
	void OnSprintCompleted(const FInputActionValue& Value);

	// --- 飞船输入回调 ---
	void OnThrust(const FInputActionValue& Value);
	void OnRoll(const FInputActionValue& Value);
	void OnVertical(const FInputActionValue& Value);
	void OnBoostStarted(const FInputActionValue& Value);
	void OnBoostCompleted(const FInputActionValue& Value);
	void OnCruise(const FInputActionValue& Value);
	void OnZoom(const FInputActionValue& Value);

	/**
	 * 进入 / 退出星图模式。
	 *
	 * 方向由「当前 Possess 的是不是星图 Pawn」判定，**不用单独的状态标志** ——
	 * 幂等，也不会和实际 Possess 的 Pawn 不同步。
	 *
	 * 做成无参是为了直接绑给 Enhanced Input：FEnhancedInputActionHandlerSignature
	 * 就是一个无参委托（EnhancedInputComponent.h:490 的 DEFINE_BIND_ACTION）。
	 */
	void ToggleStarMap();

	/** 左键按下：记下位置，用来区分「点击」和「拖拽」。 */
	void OnStarMapSelectStarted(const FInputActionValue& Value);

	/** 左键松开：位移小于阈值算点击（拾取），否则算拖拽结束（不改变选中）。 */
	void OnStarMapSelectCompleted(const FInputActionValue& Value);

	/** 把已注册天体投到屏幕，交给纯函数挑出光标下的那颗。没有就是 nullptr。 */
	AActor* PickBodyUnderCursor() const;

	/**
	 * 调试用：直接在角色与飞船之间切换 Possess，跳过登船流程。
	 * 在 PIE 控制台输入 `DebugPossessShip` 调用。Task 4 做好登船后，
	 * 这个命令仍保留作为开发工具。
	 */
	UFUNCTION(Exec)
	void DebugPossessShip();

	virtual void PlayerTick(float DeltaTime) override;

	/** 按 E 时的分派：没在船上就尝试登船，在船上就尝试离船。 */
	void OnInteract(const FInputActionValue& Value);

	/** 尝试让角色登上指定飞船。 */
	void BoardShip(ASpaceshipPawn* Ship);

	/** 尝试从当前飞船下船。 */
	void LeaveShip(ASpaceshipPawn* Ship);

	/** 在屏幕上显示一行提示（每帧刷新，duration 0）。 */
	void ShowPrompt(const FString& Text);

	/** 鼠标灵敏度（度/像素）。 */
	UPROPERTY(EditAnywhere, Category = "Input")
	float LookSensitivity = 1.2f;

	/** 上下视角反向。觉得鼠标上下颠倒时把它改成 true。 */
	UPROPERTY(EditAnywhere, Category = "Input")
	bool bInvertLookY = false;

	/**
	 * 右键是否按住。松开时角色的视角不响应鼠标 —— 鼠标划过屏幕不带画面，
	 * 角色自己拐弯（A/D 横移时 bOrientRotationToMovement 转的身）也不会被拖走。
	 */
	bool bLookHeld = false;

	/**
	 * 星图里左键是否按住。与 bLookHeld 同理：换 Pawn 时必须清零，否则按着左键
	 * 进出星图之后，松开事件送到新 Pawn 身上，这个标志就永远停在 true。
	 */
	bool bStarMapSelectHeld = false;

	/** 左键按下的屏幕位置（像素）。用 double 存 —— 与 GetMousePosition 的 double 重载对齐。 */
	FVector2D StarMapPressPosition = FVector2D::ZeroVector;

	/** 拾取的像素容差。20 像素保证远处的星球也点得中。 */
	UPROPERTY(EditAnywhere, Category = "StarMap")
	float SelectionPixelRadius = 20.f;

	/** 星图相机 Pawn。懒创建并复用，不每次进出都 Spawn/Destroy。 */
	UPROPERTY(Transient)
	TObjectPtr<AStarMapCameraPawn> StarMapPawn;

	/** 进入星图前 Possess 的 Pawn。退出时换回去。弱引用 —— 它可能已经没了。 */
	TWeakObjectPtr<APawn> PreviousPawn;

	UPROPERTY(Transient)
	TObjectPtr<UInputMappingContext> MappingContext;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> IA_Move;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> IA_Look;

	/**
	 * 视角开关（鼠标右键）。按住才让角色的视角跟鼠标动。
	 *
	 * 单独做成一个布尔动作，而不是在 OnLook 里直接查 EKeys::RightMouseButton 的
	 * 键状态：键位以后要能在编辑器里改（IMC/IA 就是为此才在运行时构造的），
	 * 写死键名去轮询等于把右键钉死在这一行上。
	 */
	UPROPERTY(Transient)
	TObjectPtr<UInputAction> IA_LookActivate;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> IA_Jump;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> IA_Sprint;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> IA_Thrust;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> IA_Roll;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> IA_Vertical;

	/** 飞船加速键（Shift）。与角色的 IA_Sprint 共用按键，靠 Pawn 类型分派。 */
	UPROPERTY(Transient)
	TObjectPtr<UInputAction> IA_Boost;

	/** 巡航开关（Tab）。切换飞船的速度保持状态，只绑 Started。 */
	UPROPERTY(Transient)
	TObjectPtr<UInputAction> IA_Cruise;

	/** 相机缩放（鼠标滚轮）。轴值，每格 ±1。 */
	UPROPERTY(Transient)
	TObjectPtr<UInputAction> IA_Zoom;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> IA_Interact;

	/** 星图模式开关（M）。 */
	UPROPERTY(Transient)
	TObjectPtr<UInputAction> IA_StarMapToggle;

	/** 星图里选中天体 / 拖拽转视角（鼠标左键）。按下与松开的位移区分两者。 */
	UPROPERTY(Transient)
	TObjectPtr<UInputAction> IA_StarMapSelect;
};
