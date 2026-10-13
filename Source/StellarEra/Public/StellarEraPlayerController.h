// Copyright Voxel Plugin SAS. All Rights Reserved.
// 玩家控制器：构造输入资产、分派输入、编排登船/离船。

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "StellarEraPlayerController.generated.h"

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

protected:
	/** 构造 IMC 与全部 IA。幂等：重复调用会直接返回。 */
	void BuildInputAssets();

	// --- 角色输入回调 ---
	void OnMove(const FInputActionValue& Value);
	void OnLook(const FInputActionValue& Value);
	void OnJump(const FInputActionValue& Value);
	void OnSprintStarted(const FInputActionValue& Value);
	void OnSprintCompleted(const FInputActionValue& Value);

	// --- 飞船输入回调 ---
	void OnThrust(const FInputActionValue& Value);
	void OnRoll(const FInputActionValue& Value);
	void OnVertical(const FInputActionValue& Value);

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

	UPROPERTY(Transient)
	TObjectPtr<UInputMappingContext> MappingContext;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> IA_Move;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> IA_Look;

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

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> IA_Interact;
};
