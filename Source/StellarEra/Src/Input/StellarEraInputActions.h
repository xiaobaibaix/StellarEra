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
 * 运行时构造的输入资产：一个共享的 IMC，加三张按 Pawn 拆开的动作表。
 *
 * 每张表的类型与 `Build` 都跟着它的消费方走 —— `StellarEraPawns/Src/Character/`、
 * `Src/Spaceship/`、`Src/StarMap/` 各一份。本结构体只做聚合与构造，住在 Game
 * 模块（控制器所在的模块），因为「动作资产归谁」与「谁 Possess 谁响应」是两件事：
 * 资产由控制器造，绑定由各 Pawn 在自己的 `SetupPlayerInputComponent` 里做。
 *
 * Pawn 看不到本结构体：它们经 `IStellarEraInputSource`
 * （`StellarEraPawns/Src/StellarEraInputSource.h`）只取自己那张表。接口只交
 * 单张表，依赖方向因此是 Pawn ← 控制器，不会反过来。
 *
 * 同一个按键可以在多张表里各有一条动作（E：角色登船 / 飞船离船；M：三个 Pawn
 * 都能开星图；左键：角色编辑 / 星图选择）。这不冲突：同一个 IMC 里的动作互不
 * 吞输入，真正响应的是被 Possess 那个 Pawn 绑定的那条。
 *
 * 所有资产都 NewObject 到 Outer（控制器）名下，随控制器一起回收；没有磁盘
 * 资产，改键只改各表的 Build，不用碰 .ini 或编辑器资产。
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
	 * 构造三张 Pawn 动作表与按键映射。已经构造过就直接返回。
	 *
	 * Outer 传控制器：资产随控制器回收，不需要单独清理。
	 */
	void Build(UObject* Outer);
};
