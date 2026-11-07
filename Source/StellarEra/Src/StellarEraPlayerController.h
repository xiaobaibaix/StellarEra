// Copyright Voxel Plugin SAS. All Rights Reserved.
// 玩家控制器：构造输入资产、分派输入、编排登船/离船。

#pragma once

#include "CoreMinimal.h"
#include "StellarEraPlayerControllerBase.h"
#include "StellarEraPlayerController.generated.h"

/**
 * 本项目的玩家控制器。
 *
 * 输入资产（IMC / IA）在运行时用 C++ 构造：零手工步骤，改完代码立刻能跑，
 * 不需要先在编辑器里点出一堆资产。代价是键位暂时不能在编辑器里改；等键位
 * 定稿后再换成 .uasset 是平滑的替换（数据结构一样，只是来源变了）。
 *
 * 输入绑定按「谁被操控谁绑」分：
 *   - **角色 / 飞船 / 星图相机**在自己的 `SetupPlayerInputComponent` 里绑全部输入，
 *     动作表由基类经 `IStellarEraInputSource` 提供 —— 所以这里没有按 Pawn 类型
 *     分派的三路 `Cast`，新增一种可操控 Pawn 也不用改本类；
 *   - **本类不再绑任何输入**（`SetupInputComponent` 已删除）。**M 与 E 都由 Pawn
 *     自己执行** —— 只向控制器要「该对谁切换」：E 的目标是飞船 / 角色
 *     （机械动作在 `ASpaceshipPawn::Board` / `DisembarkCrew`），M 的目标是星图
 *     相机（机械动作在 `AStarMapCameraPawn::EnterStarMap` / `ExitStarMap`，
 *     退图连查询都不需要）。`Possess` 仍在这里，是唯一留给控制器的会话动作。
 *     星图左键选择归相机（角色只在编辑模式下用同一个动作挖矿）；
 *     星图的视角、缩放、编辑、矿物透视（H 与数字键）也都归相机。
 *
 * 两个跨模块接口都实现在基类 `AStellarEraPlayerControllerBase` 里（那边是它们
 * 的唯一实现点）：接口声明在下游模块、实现落在最上层，所以单独一层，本类只继承。
 * 本类只写**玩法策略**：生命周期（构造、Possess、Tick）、鼠标模式、编辑模式收尾、
 * 提示、调试命令。
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
	 * 换视点的唯一驱动：把 World 里所有实现 IVoxelViewpointInvokers 的 Pawn
	 * 按 InPawn 设成视点，只有 InPawn 拿到 true。InPawn 为空（OnUnPossess
	 * 路径）时全部关掉。
	 *
	 * 静态是为了让测试能在没有控制器实例的情况下验它 —— 与 HasStarMapCamera
	 * 同理。不实现本接口的 Pawn 不受影响。
	 */
	static void ApplyViewpointInvokerGate(UWorld* World, const APawn* InPawn);

protected:
	// ── Possess / 逐帧 ──

	virtual void OnPossess(APawn* InPawn) override;

	virtual void PlayerTick(float DeltaTime) override;

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

	/** Pawn 上开着的编辑模式一律退出。上船 / 进出星图 / 调试换 Pawn 共用。 */
	void ForceExitEditMode(APawn* InPawn);

	// ── 调试命令 ──

	/**
	 * 调试用：直接在角色与飞船之间切换 Possess，跳过登船流程。
	 * 在 PIE 控制台输入 `DebugPossessShip` 调用。登船流程做好之后，
	 * 这个命令仍保留作为开发工具。
	 */
	UFUNCTION(Exec)
	void DebugPossessShip();

	/**
	 * 调试用：把当前飞船当成「已落地」直接走一遍下船流程（等价于按 E）。
	 *
	 * 与 DebugPossessShip 配对：它能让人进入「坐在船上但没有角色」的状态，
	 * 这条能把角色补回来。也用于无头验证（`-ExecCmds="DebugDisembark"`）。
	 */
	UFUNCTION(Exec)
	void DebugDisembark();
};
