// Copyright Voxel Plugin SAS. All Rights Reserved.
// Pawn 想让控制器做一件「跨 Pawn 的会话动作」，但看不到 Game 模块时用的接口。

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "SessionCommands.generated.h"

class APlanetCharacter;
class APawn;
class ASpaceshipPawn;
class AStarMapCameraPawn;

UINTERFACE(MinimalAPI, meta = (CannotImplementInterfaceInBlueprint))
class UStellarEraSessionCommands : public UInterface
{
	GENERATED_BODY()
};

/**
 * 「该对谁切换」的查询。换 Possess 仍归控制器（Pawn 拿不到控制权），
 * 本接口只回答目标是谁。声明在 Pawns、实现在 Game 模块的控制器上，
 * Pawn 用 `Cast<IStellarEraSessionCommands>(Pawn->GetController())` 取。
 */
class STELLARERAPAWNS_API IStellarEraSessionCommands
{
	GENERATED_BODY()

public:
	/**
	 * M（角色 / 飞船）：从 From 能进入的星图相机；进不去返回 nullptr。
	 * 拿到后调用方自己 `Camera->EnterStarMap(From, PC)`；退图不走本接口。
	 */
	virtual AStarMapCameraPawn* FindStarMapCameraFor(const APawn* From) const = 0;

	/**
	 * E（角色）：离 From 最近且够近可登船的飞船；没有返回 nullptr。
	 * 判定放控制器是因为「关卡里有哪些飞船」是关卡级知识。
	 */
	virtual ASpaceshipPawn* FindBoardableShipFrom(const APawn* From) const = 0;

	/**
	 * E（飞船）：舱里的角色；舱里没记账时取直接挂在飞船下的 APlanetCharacter。
	 * 找不到返回 nullptr，按 E 无法离船。
	 */
	virtual APlanetCharacter* GetOrSpawnCrewCharacter(ASpaceshipPawn* Ship) = 0;
};
