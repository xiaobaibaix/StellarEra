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
 * 「该对谁切换」的查询：E 的目标飞船 / 角色，M 的目标星图相机。
 *
 * **换 Possess 本身仍归控制器**：`Possess` 是 `AController` 的方法，Pawn 拿不到
 * 控制权。所以每个会话键都拆成两半 —— 本接口回答「目标是谁」（最近可登船的飞船、
 * 舱里或新生成的角色、从这台 Pawn 能进的星图相机），Pawn 拿到目标后自己做机械
 * 动作（`Ship->Board` / `Ship->DisembarkCrew` / `Camera->EnterStarMap`），
 * 最后一步才调 `Controller->Possess(...)` 换人。
 *
 * 为什么需要它：M / E 由各 Pawn 的输入组件绑 —— M 装在角色、飞船与星图相机上，
 * E 装在角色与飞船上。而关卡视野（关卡里有哪些飞船）住在
 * AStellarEraPlayerController（Game 模块），Pawn 所在模块是它的上游，
 * 不能 include 控制器。
 *
 * 所以接口声明在本模块、实现落在控制器上，Pawn 通过
 * `Cast<IStellarEraSessionCommands>(Pawn->GetController())` 取到。
 */
class STELLARERAPAWNS_API IStellarEraSessionCommands
{
	GENERATED_BODY()

public:
	/**
	 * M（角色 / 飞船）：从 From 这个 Pawn 能进入的星图相机；进不去返回 nullptr。
	 *
	 * 进不去的情形：From 的参考行星没挂星图相机（飞船没有星图相机、深空里没有
	 * 参考行星）。调用方拿到非空结果后自己 `Camera->EnterStarMap(From, PC)`。
	 *
	 * **退图不走本接口**：相机自己知道是哪个 Pawn 把它叫出来的，直接
	 * `ExitStarMap()` 即可。
	 */
	virtual AStarMapCameraPawn* FindStarMapCameraFor(const APawn* From) const = 0;

	/**
	 * E（角色）：离 From 最近、且近到可以登船的飞船；没有则 nullptr。
	 *
	 * 判定放在控制器：关卡里有哪些飞船是关卡级知识，`CanBoardFrom` 的半径由飞船
	 * 自己提供。调用方拿到非空结果即可 `Ship->Board(Character)` + `Possess(Ship)`。
	 */
	virtual ASpaceshipPawn* FindBoardableShipFrom(const APawn* From) const = 0;

	/**
	 * E（飞船）：舱里的角色。舱里没记账时，取直接挂在飞船下的 APlanetCharacter
	 * （子 Actor / 关卡里附着到船上）。本工程不在运行时生成角色。
	 *
	 * 找不到返回 nullptr，按 E 无法离船。调用方拿到后做
	 * `Ship->DisembarkCrew(Character)` + `Possess(Character)`。
	 */
	virtual APlanetCharacter* GetOrSpawnCrewCharacter(ASpaceshipPawn* Ship) = 0;
};
