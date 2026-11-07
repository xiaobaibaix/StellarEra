// Copyright Voxel Plugin SAS. All Rights Reserved.
// 本项目用到的全部 Enhanced Input 动作与按键映射：一个共享 IMC + 三张按 Pawn 拆开的表。

#pragma once

#include "CoreMinimal.h"
#include "PlanetCharacterInputActions.h"
#include "SpaceshipInputActions.h"
#include "StarMapCameraInputActions.h"
#include "StellarEraInputActions.generated.h"

class UInputMappingContext;

/**
 * 运行时构造的输入资产：一个共享 IMC + 三张按 Pawn 拆开的动作表。
 * 本结构体只做聚合与构造：资产由控制器造，绑定由各 Pawn 在自己的
 * `SetupPlayerInputComponent` 里做。Pawn 经 `IStellarEraInputSource` 只取自己那张表，
 * 依赖方向因而是 Pawn ← 控制器。资产 NewObject 到控制器名下，无磁盘资产。
 */
USTRUCT()
struct STELLARERA_API FStellarEraInputActions
{
	GENERATED_BODY()

	/** 三张表共用的归属上下文。 */
	UPROPERTY(Transient)
	TObjectPtr<UInputMappingContext> MappingContext;

	/** APlanetCharacter 的移动 / 视角 / 跳 / 冲刺 / 缩放 / 编辑 / 笔刷 / 登船 / 星图。 */
	UPROPERTY(Transient)
	FPlanetCharacterInputActions Character;

	/** ASpaceshipPawn 的推力 / 横滚 / 升降 / 加速 / 巡航 / 缩放 / 调试视角 / 离船 / 星图。 */
	UPROPERTY(Transient)
	FSpaceshipInputActions Ship;

	/** AStarMapCameraPawn 的视角 / 缩放 / 编辑 / 笔刷 / 矿物透视 / 选择 / 星图开关。 */
	UPROPERTY(Transient)
	FStarMapCameraInputActions StarMap;

	/**
	 * 构造三张 Pawn 动作表与按键映射，已构造则直接返回。
	 * Outer 传控制器：资产随控制器回收。
	 */
	void Build(UObject* Outer);
};
