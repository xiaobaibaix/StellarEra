// Copyright Voxel Plugin SAS. All Rights Reserved.
// 游戏模式：决定 PIE 与打包时生成哪个 Pawn 与 PlayerController。

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "StellarEraGameMode.generated.h"

/**
 * 本项目的游戏模式。
 *
 * 没有它，PIE 会生成引擎默认的 ADefaultPawn（一个旁观球），角色类永远不会
 * 被实例化。DefaultEngine.ini 里的 GlobalDefaultGameMode 指向本类。
 */
UCLASS()
class AStellarEraGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AStellarEraGameMode();

	/** 飞船相对行星球心的生成方向（会归一化）。默认放在赤道一侧。 */
	UPROPERTY(EditDefaultsOnly, Category = "StellarEra")
	FVector ShipSpawnDirection = FVector(0.f, 1.f, 0.f);

	/** 飞船生成点离行星表面的高度（厘米）。 */
	UPROPERTY(EditDefaultsOnly, Category = "StellarEra")
	float ShipSpawnHeight = 400.f;

	/** 关卡里没有飞船时是否自动生成一艘，方便开箱即测。 */
	UPROPERTY(EditDefaultsOnly, Category = "StellarEra")
	bool bSpawnSpaceshipIfMissing = true;

	/**
	 * 开局直接把玩家放进最近的飞船，而不是先走角色。
	 *
	 * 走的是正常登船流程，因此角色会被登记为已登船（隐藏、附着在船上）：
	 * 飞船停稳时屏幕上会出现「按 E 离船」，按 E 就能下车继续行走。
	 *
	 * GlobalDefaultGameMode 指向的是本原生类，没有蓝图实例可以勾选，
	 * 所以这里的默认值就是最终生效值 —— 想改回「先走路」改这一行即可。
	 */
	UPROPERTY(EditDefaultsOnly, Category = "StellarEra")
	bool bStartInSpaceship = true;

protected:
	virtual void BeginPlay() override;

	/** 在球面上找一处空地生成飞船；关卡里已存在则跳过。 */
	void EnsureSpaceshipExists();

	/** 开局登船：延迟一帧执行，等本地玩家登录并拿到 Pawn 之后。 */
	void PossessStartupShip();
};
