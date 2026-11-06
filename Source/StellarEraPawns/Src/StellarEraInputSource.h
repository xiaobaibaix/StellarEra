// Copyright Voxel Plugin SAS. All Rights Reserved.
// Pawn 上哪拿 Enhanced Input 动作表：接口住本模块，实现落在控制器。

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "StellarEraInputSource.generated.h"

struct FPlanetCharacterInputActions;
struct FSpaceshipInputActions;
struct FStarMapCameraInputActions;

UINTERFACE(MinimalAPI, meta = (CannotImplementInterfaceInBlueprint))
class UStellarEraInputSource : public UInterface
{
	GENERATED_BODY()
};

/**
 * 「上哪拿输入动作表」。
 *
 * 由 AStellarEraPlayerController 实现（它在 Game 模块，Pawn 看不到），Pawn 在
 * `SetupPlayerInputComponent` 里通过它取到**自己那张表**，各绑各的：
 *
 *   - APlanetCharacter   → `GetPlanetCharacterInputActions()`；
 *   - ASpaceshipPawn     → `GetSpaceshipInputActions()`；
 *   - AStarMapCameraPawn → `GetStarMapCameraInputActions()`。
 *
 * 三张表的类型都住在 Pawns（各自 Pawn 的子目录），聚合与构造在 Game 模块的
 * `FStellarEraInputActions` 里。接口只交单张表、不交出聚合 —— Pawn 也就不需要
 * 看见那个只有控制器才关心的结构体。
 *
 * 这样「当前 Possess 的是谁，就由谁响应输入」由引擎的 Pawn 输入栈完成，
 * 控制器里不再有 `Cast<APlanetCharacter>` / `Cast<ASpaceshipPawn>` 的三路分派。
 */
class STELLARERAPAWNS_API IStellarEraInputSource
{
	GENERATED_BODY()

public:
	/** 角色那张表。实现方保证调用时已经构造完毕（幂等 Build）。 */
	virtual const FPlanetCharacterInputActions& GetPlanetCharacterInputActions() = 0;

	/** 飞船那张表。 */
	virtual const FSpaceshipInputActions& GetSpaceshipInputActions() = 0;

	/** 星图相机那张表。 */
	virtual const FStarMapCameraInputActions& GetStarMapCameraInputActions() = 0;
};
