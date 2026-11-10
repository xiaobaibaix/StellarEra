// Copyright Voxel Plugin SAS. All Rights Reserved.
// ASpaceshipPawn 专属的 Enhanced Input 动作与按键映射。

#pragma once

#include "CoreMinimal.h"
#include "SpaceshipInputActions.generated.h"

class UInputAction;
class UInputMappingContext;

/**
 * 飞船用到的动作，由控制器构造（NewObject 到控制器名下，没有磁盘资产），飞船在
 * `USpaceshipInputComponent::Bind` 里绑。对象名带 `IA_Ship_` 前缀：三张表共用同一个
 * Outer，同名会被引擎改名成不可预测的 `_1` / `_0`。
 */
USTRUCT()
struct STELLARERAPAWNS_API FSpaceshipInputActions
{
	GENERATED_BODY()

	/** 鼠标位移（与角色 / 星图同名同位，保持三张表的形状一致）。 */
	UPROPERTY(Transient)
	TObjectPtr<UInputAction> Look;

	/** W 推力 / S 制动。Axis1D。 */
	UPROPERTY(Transient)
	TObjectPtr<UInputAction> Thrust;

	/** D/A 横滚。Axis1D。 */
	UPROPERTY(Transient)
	TObjectPtr<UInputAction> Roll;

	/** Space / LeftControl 升降。Axis1D。 */
	UPROPERTY(Transient)
	TObjectPtr<UInputAction> Vertical;

	/** 加速键（Shift）。与角色的 Sprint 共用按键，靠 Pawn 类型分派。 */
	UPROPERTY(Transient)
	TObjectPtr<UInputAction> Boost;

	/** 巡航开关（C）。切换飞船的速度保持状态，只绑 Started。 */
	UPROPERTY(Transient)
	TObjectPtr<UInputAction> Cruise;

	/** 驾驶 / 自由视角切换（Tab）。只绑 Started。 */
	UPROPERTY(Transient)
	TObjectPtr<UInputAction> FreeLookToggle;

	/** 自由视角里按住右键才环绕，与角色同一套手感。 */
	UPROPERTY(Transient)
	TObjectPtr<UInputAction> LookActivate;

	/** 飞船调试视角开关（B）。只绑 Started，按一下切一次。 */
	UPROPERTY(Transient)
	TObjectPtr<UInputAction> DebugViewToggle;

	/** 相机缩放（鼠标滚轮）。轴值，每格 ±1。 */
	UPROPERTY(Transient)
	TObjectPtr<UInputAction> Zoom;

	/** 上下船（E）。角色那边是登船，飞船这边是离船 —— 语义不同，各造一条。 */
	UPROPERTY(Transient)
	TObjectPtr<UInputAction> Interact;

	/** 星图模式开关（M）。三个 Pawn 都绑自己表里这条，谁被 Possess 谁响应。 */
	UPROPERTY(Transient)
	TObjectPtr<UInputAction> StarMapToggle;

	/** 构造动作并往 Context 里注册按键映射。已经构造过就直接返回。 */
	void Build(UObject* Outer, UInputMappingContext* Context);
};
