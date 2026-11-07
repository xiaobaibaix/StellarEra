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
 * 两个跨模块接口的**唯一实现点**。
 *
 * 这些接口都是依赖倒置的钩子：声明在下游模块（Pawns），实现必须落在 Game
 * （最上层），消费方各自 `Cast<接口>(Controller)` 反查。它们凑成一层基类，理由
 * 不是「都属于控制器」，而是「都只能由最上层实现」—— 单独一层，新的控制器
 * 子类就不会漏掉其中任何一个。
 *
 * **本层不生成角色**：乘员由飞船自己提供（舱里的记账，或直接挂在飞船下的
 * APlanetCharacter），本层只做查询。这一层除了动作表没有别的状态，全部实现
 * 只在一个 cpp 里。
 *
 *   - `IStellarEraInputSource`：三个 Pawn 的输入组件来取运行时构造的动作表；
 *   - `IStellarEraSessionCommands`：M / E 的对象查询（可进的星图相机、最近可登船
 *     飞船、舱内或挂接在飞船下的角色）。
 *
 * 星图 UI 那一边不在这里：它要的只是鼠标坐标、屏幕拾取、矿物透视三个查询，
 * 全部是对引擎 API 与 `StarMapPicking` 的转发，而 UI 本来就依赖 Pawns，
 * 手里也有玩家控制器 —— 直接调即可，无需接口反查。
 */
UCLASS(Abstract)
class STELLARERA_API AStellarEraPlayerControllerBase : public APlayerController,
	public IStellarEraInputSource,
	public IStellarEraSessionCommands
{
	GENERATED_BODY()

public:
	// ── 接口实现 ──
	// 两个都是对别的模块的契约，统一放 public：没必要一个 public 一个 protected。

	//~ Begin IStellarEraInputSource Interface
	// 三张表各交给它的消费方；聚合结构体只有本层看得见。实现见 .cpp（幂等 Build）。
	virtual const FPlanetCharacterInputActions& GetPlanetCharacterInputActions() override;
	virtual const FSpaceshipInputActions& GetSpaceshipInputActions() override;
	virtual const FStarMapCameraInputActions& GetStarMapCameraInputActions() override;
	//~ End IStellarEraInputSource Interface

	//~ Begin IStellarEraSessionCommands Interface
	/**
	 * M（角色 / 飞船）：从 From 的参考行星上取那台星图相机；进不去返回 nullptr。
	 *
	 * 进不去的情形：参考行星没挂相机（飞船没有星图相机、深空里没有参考行星）——
	 * 星图是行星的观察视角，没有宿主行星就没有可进入的相机。
	 *
	 * 拿到相机之后的整段进场流程（记返回对象、摆位、求解器状态、聚焦）归
	 * `AStarMapCameraPawn::EnterStarMap`；退图更不问本层 —— 相机自己记着是谁
	 * 把它叫出来的。
	 */
	virtual AStarMapCameraPawn* FindStarMapCameraFor(const APawn* From) const override;

	/**
	 * E（角色）：离 From 最近、且近到可以登船的飞船；没有则 nullptr。
	 *
	 * 角色输入组件拿到非空结果后自己 `Ship->Board(角色)` + `Possess(Ship)`。
	 */
	virtual ASpaceshipPawn* FindBoardableShipFrom(const APawn* From) const override;

	/**
	 * E（飞船）：舱里的角色。舱里没记账时，取直接挂在飞船下的角色。
	 *
	 * 找不到（舱里没人、下面也没挂角色）时返回 nullptr，按 E 无法离船。
	 * 飞船输入组件拿到结果后自己 `Ship->DisembarkCrew(角色)` + `Possess(角色)`。
	 */
	virtual APlanetCharacter* GetOrSpawnCrewCharacter(ASpaceshipPawn* Ship) override;
	//~ End IStellarEraSessionCommands Interface

protected:
	// ── 运行时状态 ──

	/** 三张 Pawn 动作表与共享 IMC。定义见 StellarEraInputActions.h。 */
	UPROPERTY(Transient)
	FStellarEraInputActions Input;
};
