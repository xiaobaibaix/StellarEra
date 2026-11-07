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
 * 「上哪拿输入动作表」：各 Pawn 在 `SetupPlayerInputComponent` 里取自己那张，
 * 实现在 Game 模块的控制器上（Pawn 看不到）。只交单张表，不交出聚合结构 ——
 * 输入按 Possess 的 Pawn 分派由引擎的输入栈完成。
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
