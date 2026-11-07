// Copyright Voxel Plugin SAS. All Rights Reserved.
// APlanetCharacter 专属的 Enhanced Input 动作与按键映射。

#pragma once

#include "CoreMinimal.h"
#include "PlanetCharacterInputActions.generated.h"

class UInputAction;
class UInputMappingContext;

/**
 * 角色（含编辑模式）用到的动作：控制器构造、角色输入组件绑，全部 NewObject 到控制器名下，
 * 没有磁盘资产 —— 改键只改这里的映射表。对象名带 `IA_Character_` 前缀：三张表共用同一个
 * Outer，同名会被引擎改名成不可预测的 `_1` / `_0`。
 */
USTRUCT()
struct STELLARERAPAWNS_API FPlanetCharacterInputActions
{
	GENERATED_BODY()

	/** WASD 合成的一个二维向量。 */
	UPROPERTY(Transient)
	TObjectPtr<UInputAction> Move;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> Look;

	/** 视角开关（鼠标右键）。做成布尔动作而不是在 Look 里轮询键状态：键位要能在编辑器里改。 */
	UPROPERTY(Transient)
	TObjectPtr<UInputAction> LookActivate;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> Jump;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> Sprint;

	/** 相机缩放（鼠标滚轮）。轴值，每格 ±1。 */
	UPROPERTY(Transient)
	TObjectPtr<UInputAction> Zoom;

	/** 编辑模式开关（G）。 */
	UPROPERTY(Transient)
	TObjectPtr<UInputAction> EditModeToggle;

	/** 笔刷尺寸（`[` / `]`）。Axis1D，只绑 Started，不随按住每帧刷。 */
	UPROPERTY(Transient)
	TObjectPtr<UInputAction> BrushSize;

	/** 主键（鼠标左键）：编辑模式下点笔刷按钮 / 挖掘。只绑 Started。 */
	UPROPERTY(Transient)
	TObjectPtr<UInputAction> EditPrimary;

	/** 上下船（E）。角色这边是登船，飞船那边是离船，所以各表各造一条。 */
	UPROPERTY(Transient)
	TObjectPtr<UInputAction> Interact;

	/** 星图模式开关（M）。三个 Pawn 都绑自己表里这条，谁被 Possess 谁响应。 */
	UPROPERTY(Transient)
	TObjectPtr<UInputAction> StarMapToggle;

	/** 构造动作并往 Context 里注册按键映射。已经构造过就直接返回。 */
	void Build(UObject* Outer, UInputMappingContext* Context);
};
