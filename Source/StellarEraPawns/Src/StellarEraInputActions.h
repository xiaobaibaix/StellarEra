// Copyright Voxel Plugin SAS. All Rights Reserved.
// 本项目用到的全部 Enhanced Input 动作与按键映射，集中在一个结构体里。

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
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
 * 运行时构造的输入资产。
 *
 * 以前这些 IA_* 直接摊在 AStellarEraPlayerController 上：二十个 UPROPERTY 加一个
 * 一百多行的构建函数，把控制器本身的逻辑淹了。集中到这里之后只是一份数据，
 * 由控制器持有、构造，再交给 Pawn 去绑定。
 *
 * 所有资产都 NewObject 到 Outer（控制器）名下，随控制器一起回收；没有磁盘资产，
 * 改键只改 Build 里的映射表，不用碰 .ini 或编辑器资产。
 *
 * 位置：住在本模块（StellarEraPawns）而不是主模块，因为**绑定的一方是 Pawn**。
 * 角色 / 飞船在自己的 SetupPlayerInputComponent 里取这张表；控制器只负责造出来、
 * 把 IMC 塞进本地玩家子系统。
 */
USTRUCT()
struct STELLARERAPAWNS_API FStellarEraInputActions
{
	GENERATED_BODY()

	/** 这些动作的归属上下文。 */
	UPROPERTY(Transient)
	TObjectPtr<UInputMappingContext> MappingContext;

	// ── 角色 ──

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> Move;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> Look;

	/**
	 * 视角开关（鼠标右键）。按住才让角色 / 飞船 / 星图的视角跟鼠标动。
	 *
	 * 单独做成一个布尔动作，而不是在 Look 里直接查 EKeys::RightMouseButton 的
	 * 键状态：键位以后要能在编辑器里改（IMC/IA 就是为此才在运行时构造的），
	 * 写死键名去轮询等于把右键钉死在这一行上。
	 */
	UPROPERTY(Transient)
	TObjectPtr<UInputAction> LookActivate;

	/**
	 * 星图的中键（绕行）。与 LookActivate 同一个套路：Look 只有鼠标位移，
	 * 「按的是哪个键」得另有布尔动作来分辨。
	 */
	UPROPERTY(Transient)
	TObjectPtr<UInputAction> LookMiddle;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> Jump;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> Sprint;

	// ── 飞船 ──

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> Thrust;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> Roll;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> Vertical;

	/** 飞船加速键（Shift）。与角色的 Sprint 共用按键，靠 Pawn 类型分派。 */
	UPROPERTY(Transient)
	TObjectPtr<UInputAction> Boost;

	/** 巡航开关（Tab）。切换飞船的速度保持状态，只绑 Started。 */
	UPROPERTY(Transient)
	TObjectPtr<UInputAction> Cruise;

	/** 飞船调试视角开关（B）。只绑 Started，按一下切一次。 */
	UPROPERTY(Transient)
	TObjectPtr<UInputAction> DebugViewToggle;

	/** 相机缩放（鼠标滚轮）。轴值，每格 ±1。角色 / 飞船 / 星图各绑各的。 */
	UPROPERTY(Transient)
	TObjectPtr<UInputAction> Zoom;

	// ── 交互 / 星图 / 编辑 ──

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> Interact;

	/** 星图模式开关（M）。 */
	UPROPERTY(Transient)
	TObjectPtr<UInputAction> StarMapToggle;

	/**
	 * 星图里选中天体 / 拖拽绕行（鼠标左键）。按下与松开的位移区分两者。
	 * 编辑模式与矿物透视下左键已被借走，那时拖拽是编辑 / 图例，绕行改由中键承担、
	 * 转头仍是右键 —— 见 UStarMapCameraInputComponent::ResolveLookMode。
	 */
	UPROPERTY(Transient)
	TObjectPtr<UInputAction> StarMapSelect;

	/** 编辑模式开关（G）。 */
	UPROPERTY(Transient)
	TObjectPtr<UInputAction> EditModeToggle;

	/** 笔刷尺寸（`[` / `]`）。Axis1D，只绑 Started，不随按住每帧刷。 */
	UPROPERTY(Transient)
	TObjectPtr<UInputAction> BrushSize;

	// ── 矿物透视 ──

	/** 矿物透视开关（H）。 */
	UPROPERTY(Transient)
	TObjectPtr<UInputAction> MineralViewToggle;

	/** 数字键 1..9：按下标切换元素显示。下标 = 运行期元素顺序。 */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UInputAction>> MineralFilter;

	/** 数字键 0：全部打开。 */
	UPROPERTY(Transient)
	TObjectPtr<UInputAction> MineralShowAll;

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
 *   - APlanetCharacter 绑 Move / Look / Jump / Sprint / Zoom / 右键点放；
 *   - ASpaceshipPawn 绑 Look / 推力 / 滚转 / 升降 / 加速 / 巡航 / 缩放 / B；
 *   - AStarMapCameraPawn 绑星图视角（绕行 / 转头）与缩放，见 UStarMapCameraInputComponent；
 *     星图的开关、选择与全部模式键仍绑在控制器上（进图与选择逻辑归控制器）。
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
