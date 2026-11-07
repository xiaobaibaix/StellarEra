// Copyright Voxel Plugin SAS. All Rights Reserved.
// 游戏模式：决定 PIE 与打包时生成哪个 Pawn 与 PlayerController。

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "StellarEraGameMode.generated.h"

class APlayerStart;
class ASpaceshipPawn;

/**
 * 本项目的游戏模式（DefaultEngine.ini 的 GlobalDefaultGameMode）。开局操控飞船，
 * 角色第一次按 E 下船时才生成。出生点只有一个来源：关卡里的 PlayerStart。
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
	 * 选出生点：直接返回关卡里的 PlayerStart，不走超类 —— 超类用角色胶囊做
	 * EncroachingBlockingGeometry，而飞船就摆在 PlayerStart 上，会把出生点判掉。
	 */
	virtual AActor* ChoosePlayerStart_Implementation(AController* Player) override;

	/**
	 * 开局交给玩家的 Pawn：优先返回关卡里已有的飞船（返回已存在 Actor 合法）；
	 * 找不到才按 DefaultPawnClass 在出生点生成一艘。
	 */
	virtual APawn* SpawnDefaultPawnFor_Implementation(AController* NewPlayer, AActor* StartSpot) override;

	/**
	 * 关卡里的 PlayerStart；没有则返回 nullptr。直接遍历 Actor，不走 FindPlayerStart
	 * （会做遮挡检测）。PIE 的「Play From Here」优先。
	 */
	APlayerStart* FindSpawnPlayerStart();

	/** 关卡里手摆的那艘飞船；没有则返回 nullptr。 */
	ASpaceshipPawn* FindExistingShip() const;

	/** 把飞船摆到出生点；关卡里没有飞船就在那里生成一艘。 */
	void EnsureSpaceshipExists();
};
