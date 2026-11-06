// Copyright Voxel Plugin SAS. All Rights Reserved.
// APlanetCharacter 专属的 Enhanced Input 动作与按键映射。

#pragma once

#include "CoreMinimal.h"
#include "PlanetCharacterInputActions.generated.h"

class UInputAction;
class UInputMappingContext;

/**
 * 角色（含编辑模式）用到的动作。
 *
 * 由 FStellarEraInputActions 组合持有、由控制器构造，角色在
 * `UPlanetCharacterInputComponent::SetupPlayerInputComponent` 里绑。
 * 动作全部 NewObject 到控制器名下，没有磁盘资产 —— 改键只改这里的映射表。
 *
 * 对象名带 `IA_Character_` 前缀：三张表共用同一个 Outer，同名会被引擎改名成
 * 不可预测的 `_1` / `_0`，前缀保证每张表的动作能按名字认出来。
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

	/**
	 * 视角开关（鼠标右键）。按住才让角色的视角跟鼠标动。
	 *
	 * 单独做成一个布尔动作，而不是在 Look 里直接查 EKeys::RightMouseButton 的
	 * 键状态：键位以后要能在编辑器里改（IMC/IA 就是为此才在运行时构造的），
	 * 写死键名去轮询等于把右键钉死在这一行上。
	 */
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

	/** 构造动作并往 Context 里注册按键映射。已经构造过就直接返回。 */
	void Build(UObject* Outer, UInputMappingContext* Context);
};
