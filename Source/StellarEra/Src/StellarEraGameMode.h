// Copyright Voxel Plugin SAS. All Rights Reserved.
// 游戏模式：决定 PIE 与打包时生成哪个 Pawn 与 PlayerController。

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "StellarEraGameMode.generated.h"

class APlayerStart;
class ASpaceshipPawn;

/**
 * 本项目的游戏模式。
 *
 * 没有它，PIE 会生成引擎默认的 ADefaultPawn（一个旁观球），角色类永远不会
 * 被实例化。DefaultEngine.ini 里的 GlobalDefaultGameMode 指向本类。
 *
 * 出生点只有一个来源：关卡里的 PlayerStart。飞船摆到那里，代码里不写死任何坐标
 * —— 想换起点就在关卡里拖 PlayerStart。
 *
 * 开局操控的是**飞船本身**：`DefaultPawnClass` 就是飞船类，`SpawnDefaultPawnFor`
 * 再把关卡里手摆的那艘交出去（引擎的默认路径只会新生成一个实例，没有「复用关卡里
 * 的 Actor」这种模式）。角色不在开局生成，而是第一次按 E 下船时才由
 * `AStellarEraPlayerController` 生成。这样就不存在「默认 Pawn 必须找得到一格没被
 * 船挡住的地面」这个约束 —— 它曾让角色落到世界原点。
 */
UCLASS()
class AStellarEraGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AStellarEraGameMode();

protected:
	virtual void BeginPlay() override;

	/**
	 * 选出生点。直接返回关卡里的 PlayerStart，不走超类。
	 *
	 * 超类会用角色胶囊在出生点上做 `EncroachingBlockingGeometry`：本工程的飞船就摆在
	 * PlayerStart 上（`EnsureSpaceshipExists` 会把它摆过去），船体是 `ECC_Pawn` 且
	 * Block 全通道，于是这个出生点被整条判掉、Pawn 落到世界原点。船不是障碍物，是我们
	 * 故意摆在出生点的。
	 */
	virtual AActor* ChoosePlayerStart_Implementation(AController* Player) override;

	/**
	 * 开局交给玩家的 Pawn。
	 *
	 * 先去关卡里找飞船：找到就直接返回它（返回已存在的 Actor 是合法的 ——
	 * `RestartPlayerAtPlayerStart` 只会 SetPawn + FinishRestartPlayer，不会再 spawn）；
	 * 找不到才按 `DefaultPawnClass` 在出生点生成一艘。
	 */
	virtual APawn* SpawnDefaultPawnFor_Implementation(AController* NewPlayer, AActor* StartSpot) override;

	/**
	 * 关卡里的 PlayerStart；没有则返回 nullptr。飞船的出生点由它决定。
	 *
	 * 直接遍历 Actor，不走 `FindPlayerStart` —— 那条路会做遮挡检测，而飞船就压在
	 * 出生点上。PIE 的「Play From Here」优先。
	 */
	APlayerStart* FindSpawnPlayerStart();

	/** 关卡里手摆的那艘飞船；没有则返回 nullptr。 */
	ASpaceshipPawn* FindExistingShip() const;

	/** 把飞船摆到出生点；关卡里没有飞船就在那里生成一艘。 */
	void EnsureSpaceshipExists();
};
