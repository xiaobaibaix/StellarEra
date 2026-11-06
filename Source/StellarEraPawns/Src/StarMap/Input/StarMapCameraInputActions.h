// Copyright Voxel Plugin SAS. All Rights Reserved.
// AStarMapCameraPawn 专属的 Enhanced Input 动作与按键映射。

#pragma once

#include "CoreMinimal.h"
#include "StarMapCameraInputActions.generated.h"

class UInputAction;
class UInputMappingContext;

/**
 * 星图相机用到的动作（视角、缩放、编辑、矿物透视、选择、星图开关）。
 *
 * 由 FStellarEraInputActions 组合持有、由控制器构造，相机在
 * `UStarMapCameraInputComponent::SetupPlayerInputComponent` 里绑。
 * 动作全部 NewObject 到控制器名下，没有磁盘资产 —— 改键只改这里的映射表。
 *
 * 对象名带 `IA_StarMap_` 前缀：三张表共用同一个 Outer，同名会被引擎改名成
 * 不可预测的 `_1` / `_0`，前缀保证每张表的动作能按名字认出来。
 *
 * 左键与 M 在角色 / 飞船表里也各有一条：进星图之后 Possess 的是本相机，
 * 退出必须由它自己绑；而角色 / 飞船要能按 M 进来、角色编辑模式要能用左键。
 * 同一个 IMC 里的同名按键互不吞输入，谁被 Possess 谁那条生效。
 */
USTRUCT()
struct STELLARERAPAWNS_API FStarMapCameraInputActions
{
	GENERATED_BODY()

	/** 鼠标位移。能不能用由相机按按住的键分派，见 UStarMapCameraInputComponent::ResolveLookMode。 */
	UPROPERTY(Transient)
	TObjectPtr<UInputAction> Look;

	/** 视角开关（鼠标右键）。按住才转头 / 瞄准。 */
	UPROPERTY(Transient)
	TObjectPtr<UInputAction> LookActivate;

	/**
	 * 星图的中键（绕行）。与 LookActivate 同一个套路：Look 只有鼠标位移，
	 * 「按的是哪个键」得另有布尔动作来分辨。编辑 / 矿物透视下左键被借走，
	 * 绕行就改由中键承担。
	 */
	UPROPERTY(Transient)
	TObjectPtr<UInputAction> LookMiddle;

	/** 相机缩放（鼠标滚轮）。轴值，每格 ±1。 */
	UPROPERTY(Transient)
	TObjectPtr<UInputAction> Zoom;

	/** 编辑模式开关（G）。 */
	UPROPERTY(Transient)
	TObjectPtr<UInputAction> EditModeToggle;

	/** 笔刷尺寸（`[` / `]`）。Axis1D，只绑 Started，不随按住每帧刷。 */
	UPROPERTY(Transient)
	TObjectPtr<UInputAction> BrushSize;

	/** 矿物透视开关（H）。 */
	UPROPERTY(Transient)
	TObjectPtr<UInputAction> MineralViewToggle;

	/** 数字键 1..9：按下标切换元素显示。下标 = 运行期元素顺序。 */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UInputAction>> MineralFilter;

	/** 数字键 0：全部打开。 */
	UPROPERTY(Transient)
	TObjectPtr<UInputAction> MineralShowAll;

	/** 主键（鼠标左键）：选中天体 / 拖拽绕行。按下与松开的位移区分两者。 */
	UPROPERTY(Transient)
	TObjectPtr<UInputAction> StarMapSelect;

	/** 星图模式开关（M）。进星图后 Possess 的是本相机，它必须自己绑这条才退得出去。 */
	UPROPERTY(Transient)
	TObjectPtr<UInputAction> StarMapToggle;

	/** 构造动作并往 Context 里注册按键映射。已经构造过就直接返回。 */
	void Build(UObject* Outer, UInputMappingContext* Context);
};
