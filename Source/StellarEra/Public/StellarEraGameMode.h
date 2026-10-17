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
 * 出生点只有一个来源：关卡里的 PlayerStart。飞船摆到那里，角色也出生在那里，
 * 代码里不写死任何坐标 —— 想换起点就在关卡里拖 PlayerStart。
 */
UCLASS()
class AStellarEraGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AStellarEraGameMode();

	/**
	 * 是否自动把飞船摆到出生点。关卡里已经有一艘就把它摆过去，没有就在那里生成一艘。
	 *
	 * 关掉它 = 飞船完全由关卡里的手摆位置决定，GameMode 不插手。
	 */
	UPROPERTY(EditDefaultsOnly, Category = "StellarEra")
	bool bAutoPlaceSpaceshipAtStart = true;

	/**
	 * 关卡里没有飞船时生成哪个类。
	 *
	 * 构造函数里默认指向蓝图子类 BP_SpaceshipPawn —— 模型与摆位都在那上面，
	 * 直接生成原生 ASpaceshipPawn 只会得到一个占位立方体。蓝图缺失时退回原生类。
	 */
	UPROPERTY(EditDefaultsOnly, Category = "StellarEra")
	TSubclassOf<ASpaceshipPawn> ShipClass;

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

	/**
	 * 开局登船的重试间隔（秒）。第一次尝试在下一帧就发生（FirstDelay = 0）。
	 */
	UPROPERTY(EditDefaultsOnly, Category = "StellarEra", meta = (ClampMin = "0.01"))
	float StartupShipRetryInterval = 0.1f;

	/**
	 * 开局登船最多重试多少次，超过就放弃并打日志。
	 * 默认 50 次 × 0.1 秒 ≈ 5 秒的窗口。
	 */
	UPROPERTY(EditDefaultsOnly, Category = "StellarEra", meta = (ClampMin = "1"))
	int32 StartupShipMaxAttempts = 50;

protected:
	virtual void BeginPlay() override;

	/** 关卡里的 PlayerStart；没有则返回 nullptr。飞船的出生点由它决定。 */
	APlayerStart* FindSpawnPlayerStart();

	/** 把飞船摆到出生点；关卡里没有飞船就在那里生成一艘。 */
	void EnsureSpaceshipExists();

	/**
	 * 开局登船：由定时器反复调用，直到本地玩家拿到角色 Pawn 并成功登船。
	 *
	 * 不能只等一帧：本地玩家登录并拿到 DefaultPawn 的时机由引擎决定，实测一帧后
	 * GetPawn() 还不是 APlanetCharacter，登船会静默失败。
	 */
	void PossessStartupShip();

private:
	/** 开局登船轮询的定时器。 */
	FTimerHandle StartupShipTimerHandle;

	/** 已经尝试过多少次开局登船。 */
	int32 StartupShipAttempts = 0;
};
