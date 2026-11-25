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

/**
 * 两个跨模块接口的唯一实现点：IStellarEraInputSource（Pawn 取运行时动作表）与
 * IStellarEraSessionCommands（E 的对象查询）。声明在下游模块（Pawns），实现只能
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
	AStellarEraPlayerControllerBase();

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
	 * E（角色）：离 From 最近、且近到可以登船的飞船；没有则 nullptr。
	 * 调用方拿到非空结果后自己 Ship->Board + Possess。
	 */
	virtual ASpaceshipPawn* FindBoardableShipFrom(const APawn* From) const override;

	/**
	 * E（飞船）：舱里已有角色就复用；否则在离船落脚点实时生成一个。
	 * 生成失败返回 nullptr。调用方随后自己 Ship->DisembarkCrew + Possess。
	 */
	virtual APlanetCharacter* GetOrSpawnCrewCharacter(ASpaceshipPawn* Ship) override;
	//~ End IStellarEraSessionCommands Interface

protected:
	// ── 运行时状态 ──

	/** 三张 Pawn 动作表与共享 IMC。定义见 StellarEraInputActions.h。 */
	UPROPERTY(Transient)
	FStellarEraInputActions Input;

	/** 下船时实时生成的角色类（默认 BP_PlanetCharacter）；为空则无法离船。 */
	UPROPERTY(EditDefaultsOnly, Category = "StellarEra")
	TSubclassOf<APlanetCharacter> CrewCharacterClass;
};
