// Copyright Voxel Plugin SAS. All Rights Reserved.
// 本项目用到的全部 Enhanced Input 动作与按键映射：一个共享 IMC + 按 Pawn 分表。

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "PlanetCharacterInputActions.h"
#include "SpaceshipInputActions.h"
#include "StarMapCameraInputActions.h"
#include "StellarEraInputActions.generated.h"

class UInputAction;
class UInputMappingContext;

namespace StellarEraInput
{
	/**
	 * 左键「点击」与「拖拽」的分界（像素）。
	 *
	 * 角色右键点放（APlanetCharacter）与星图左键拾取（AStellarEraPlayerController）
	 * 共用同一个阈值，所以放在这里而不是各自的 .cpp 里 —— 两处不一致会出现
	 * 「手抖 4 像素算点击、6 像素算拖拽」这种没人查得出来的偏差。
	 */
	constexpr double ClickMoveThresholdPx = 5.0;
}

/**
 * 控制器自己的三条动作。
 *
 * 它们不属于任何一个 Pawn：上下船（E）、进出星图（M）、星图里选中 / 拖拽天体
 * （鼠标左键）的状态与流程都跨 Pawn，所以绑在控制器上。星图相机只通过
 * `IStarMapLookContext::IsStarMapSelectHeld()` 问「左键现在按着没有」。
 */
USTRUCT()
struct STELLARERAPAWNS_API FControllerInputActions
{
	GENERATED_BODY()

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> Interact;

	/** 星图模式开关（M）。 */
	UPROPERTY(Transient)
	TObjectPtr<UInputAction> StarMapToggle;

	/** 星图里选中天体 / 拖拽绕行（鼠标左键）。按下与松开的位移区分两者。 */
	UPROPERTY(Transient)
	TObjectPtr<UInputAction> StarMapSelect;

	/** 构造动作并往 Context 里注册按键映射。已经构造过就直接返回。 */
	void Build(UObject* Outer, UInputMappingContext* Context);
};

/**
 * 运行时构造的输入资产：一个共享的 IMC，加四张按归属拆开的动作表。
 *
 * 以前二十多个 IA_* 全摊在一个结构体里：角色、飞船、星图相机、控制器四条线
 * 的键挤在一起，改角色键得先翻过半个飞船的字段。现在每张表跟着它的消费方
 * 走 —— `Src/Character/`、`Src/Spaceship/`、`Src/StarMap/` 各一份，剩下的
 * 控制器那三条留在本文件。
 *
 * 四张表共用同一个 `MappingContext`：按键映射必须落在同一个 IMC 里，否则
 * 「按一次 W」要往本地玩家子系统里塞四个上下文。共用按键不打架 —— 同一个
 * IMC 里的动作互不吞输入，而只有被 Possess 的那个 Pawn 绑了它的动作表。
 *
 * 所有资产都 NewObject 到 Outer（控制器）名下，随控制器一起回收；没有磁盘
 * 资产，改键只改各表的 Build，不用碰 .ini 或编辑器资产。
 *
 * 位置：住在本模块（StellarEraPawns）而不是主模块，因为**绑定的一方是 Pawn**。
 * 各 Pawn 在自己的 SetupPlayerInputComponent 里取自己那张表；控制器只负责造
 * 出来、把 IMC 塞进本地玩家子系统。
 */
USTRUCT()
struct STELLARERAPAWNS_API FStellarEraInputActions
{
	GENERATED_BODY()

	/** 四张表共用的归属上下文。 */
	UPROPERTY(Transient)
	TObjectPtr<UInputMappingContext> MappingContext;

	/** APlanetCharacter 的移动 / 视角 / 跳 / 冲刺 / 缩放 / 编辑 / 笔刷。 */
	UPROPERTY(Transient)
	FPlanetCharacterInputActions Character;

	/** ASpaceshipPawn 的推力 / 横滚 / 升降 / 加速 / 巡航 / 缩放 / 调试视角。 */
	UPROPERTY(Transient)
	FSpaceshipInputActions Ship;

	/** AStarMapCameraPawn 的视角 / 缩放 / 编辑 / 笔刷 / 矿物透视。 */
	UPROPERTY(Transient)
	FStarMapCameraInputActions StarMap;

	/** 控制器自己的上下船、星图开关与选择。 */
	UPROPERTY(Transient)
	FControllerInputActions Controller;

	/**
	 * 构造全部动作与按键映射。已经构造过就直接返回。
	 *
	 * Outer 传控制器：资产随控制器回收，不需要单独清理。
	 */
	void Build(UObject* Outer);
};

UINTERFACE(MinimalAPI, meta = (CannotImplementInterfaceInBlueprint))
class UStellarEraInputSource : public UInterface
{
	GENERATED_BODY()
};

/**
 * 「上哪拿输入动作表」。
 *
 * 由 AStellarEraPlayerController 实现（它在 Game 模块，Pawn 看不到），Pawn 在
 * `SetupPlayerInputComponent` 里通过它取到动作表，各绑各的：
 *
 *   - APlanetCharacter 取 `.Character`；
 *   - ASpaceshipPawn 取 `.Ship`；
 *   - AStarMapCameraPawn 取 `.StarMap`；
 *   - 上下船、星图开关与选择仍绑在控制器上（`.Controller`）—— 它们的状态与
 *     流程跨 Pawn。
 *
 * 这样「当前 Possess 的是谁，就由谁响应输入」由引擎的 Pawn 输入栈完成，
 * 控制器里不再有 `Cast<APlanetCharacter>` / `Cast<ASpaceshipPawn>` 的三路分派。
 *
 * 接口住在 Pawns：实现方（Game）在下游，声明方（Pawn）在上游，依赖方向不反。
 */
class STELLARERAPAWNS_API IStellarEraInputSource
{
	GENERATED_BODY()

public:
	/**
	 * 动作表的引用。实现方必须保证调用时已经构造完毕 —— 控制器在这里做的是
	 * 幂等的 Build（已构造则直接返回），所以拿不到空动作。
	 */
	virtual const FStellarEraInputActions& GetStellarEraInputActions() = 0;
};
