// Copyright Voxel Plugin SAS. All Rights Reserved.
// 玩家控制器的接口层：两个跨模块接口的实现全在这里。

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "SessionCommands.h"
#include "StellarEraInputActions.h"
#include "StellarEraInputSource.h"
#include "StellarEraPlayerControllerBase.generated.h"

class APlanetCharacter;
class ASpaceshipPawn;
class AStarMapCameraPawn;

/**
 * 两个跨模块接口的唯一实现点：IStellarEraInputSource（Pawn 取运行时动作表）与
 * IStellarEraSessionCommands（M / E 的对象查询）。声明在下游模块（Pawns），实现只能
 * 落在 Game（最上层）；凑成一层基类是为了让新控制器子类不会漏掉。
 * 本层不生成角色，只做查询；除动作表外无状态。
 */
UCLASS(Abstract)
class STELLARERA_API AStellarEraPlayerControllerBase : public APlayerController,
	public IStellarEraInputSource,
	public IStellarEraSessionCommands
{
	GENERATED_BODY()

public:
	// ── 接口实现 ──
	// 两个都是对别的模块的契约，统一放 public。

	//~ Begin IStellarEraInputSource Interface
	// 三张表各交给它的消费方；聚合结构体只有本层看得见。实现见 .cpp（幂等 Build）。
	virtual const FPlanetCharacterInputActions& GetPlanetCharacterInputActions() override;
	virtual const FSpaceshipInputActions& GetSpaceshipInputActions() override;
	virtual const FStarMapCameraInputActions& GetStarMapCameraInputActions() override;
	//~ End IStellarEraInputSource Interface

	//~ Begin IStellarEraSessionCommands Interface
	/**
	 * M（角色 / 飞船）：从 From 的参考行星上取那台星图相机；参考行星没挂相机时返回
	 * nullptr（星图是行星的观察视角）。进场流程归 EnterStarMap，退图由相机自己记。
	 */
	virtual AStarMapCameraPawn* FindStarMapCameraFor(const APawn* From) const override;

	/**
	 * E（角色）：离 From 最近、且近到可以登船的飞船；没有则 nullptr。
	 * 调用方拿到非空结果后自己 Ship->Board + Possess。
	 */
	virtual ASpaceshipPawn* FindBoardableShipFrom(const APawn* From) const override;

	/**
	 * E（飞船）：舱里的角色；舱里没记账时取直接挂在飞船下的角色。找不到返回 nullptr。
	 * 调用方随后自己 Ship->DisembarkCrew + Possess。
	 */
	virtual APlanetCharacter* GetOrSpawnCrewCharacter(ASpaceshipPawn* Ship) override;
	//~ End IStellarEraSessionCommands Interface

protected:
	// ── 运行时状态 ──

	/** 三张 Pawn 动作表与共享 IMC。定义见 StellarEraInputActions.h。 */
	UPROPERTY(Transient)
	FStellarEraInputActions Input;
};
