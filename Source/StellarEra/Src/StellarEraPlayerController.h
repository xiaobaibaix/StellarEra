// Copyright Voxel Plugin SAS. All Rights Reserved.
// 玩家控制器：构造输入资产、分派输入、编排登船/离船。

#pragma once

#include "CoreMinimal.h"
#include "StellarEraPlayerControllerBase.h"
#include "StellarEraPlayerController.generated.h"

/**
 * 玩家控制器：运行时构造输入资产，输入绑定按「谁被操控谁绑」—— 角色与飞船各自绑自己的表。
 * M 在每张表里都有映射，但只由被 Possess Pawn 上的 UStarMapInputComponent 绑定。
 * 本类只绑 Possess 与鼠标模式；跨模块接口在基类 AStellarEraPlayerControllerBase 里。
 */
UCLASS()
class AStellarEraPlayerController : public AStellarEraPlayerControllerBase
{
	GENERATED_BODY()

public:
	// ── 构造与生命周期 ──

	AStellarEraPlayerController();

	virtual void BeginPlay() override;

	// ── 静态查询（无状态，能无头测）──

	/**
	 * 换视点的唯一驱动：把实现 IVoxelViewpointInvokers 的 Pawn 按 InPawn 设成视点，
	 * 只有 InPawn 拿到 true；InPawn 为空（OnUnPossess）时全部关掉。静态便于无实例测试。
	 */
	static void ApplyViewpointInvokerGate(UWorld* World, const APawn* InPawn);

protected:
	// ── Possess / 逐帧 ──

	virtual void OnPossess(APawn* InPawn) override;

	virtual void PlayerTick(float DeltaTime) override;

	/**
	 * 按当前 Possess 的 Pawn 切换鼠标模式：角色与飞船自由视角光标常驻、仅右键时捕获；
	 * 飞船驾驶光标隐藏、永久捕获。OnPossess、飞船 Tab 切换与星图进出都走它。
	 */
	void ApplyMouseModeForPawn(APawn* InPawn);

	/** Pawn 上开着的编辑模式一律退出。上船 / 进出星图 / 调试换 Pawn 共用。 */
	void ForceExitEditMode(APawn* InPawn);

	// ── 调试命令 ──

	/**
	 * 调试用：在角色与飞船之间切换 Possess，跳过登船流程。
	 * PIE 控制台输入 `DebugPossessShip` 调用。
	 */
	UFUNCTION(Exec)
	void DebugPossessShip();

	/**
	 * 调试用：把当前飞船当成已落地走一遍下船流程（等价于按 E），能把「坐在船上但
	 * 没有角色」的状态补回来；也用于无头验证（`-ExecCmds="DebugDisembark"`）。
	 */
	UFUNCTION(Exec)
	void DebugDisembark();

private:
	/** 太空背景球（带 SpaceBackdrop 标签的 Actor）：每帧跟到相机位置，等效无限远。 */
	UPROPERTY(Transient)
	TObjectPtr<AActor> SpaceBackdropActor;

	/** 上次按哪种飞船视角设的鼠标模式；与飞船当前状态不同时在 PlayerTick 里重设。 */
	bool bAppliedShipFreeLook = false;

	/** 上次按哪种星图状态设的鼠标模式；进出图不再换 Pawn，只能在 PlayerTick 里重设。 */
	bool bAppliedStarMapActive = false;
};
