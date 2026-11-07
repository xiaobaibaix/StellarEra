// Copyright Voxel Plugin SAS. All Rights Reserved.
//
// AStellarEraPlayerControllerBase 实现的跨模块接口，全部集中在本文件。
//
// 它们都是**依赖倒置**的钩子：接口声明在下游模块（Pawns），实现落在本层
// （Game，最上层），消费方各自 `Cast<接口>(Controller)` 反查。和具体控制器的
// 生命周期代码分开放，是因为两者改动的理由不同 —— 这里随下游模块的接口变化
// 而变，那边随会话流程而变。

#include "StellarEraPlayerControllerBase.h"

#include "PlanetCharacter.h"
#include "SpaceshipPawn.h"
#include "StarMapCameraPawn.h"

// ─────────────────────────────────────────────────────────────────────────────
// IStellarEraInputSource：三个 Pawn 的输入组件来取运行时构造的动作表。
// ─────────────────────────────────────────────────────────────────────────────

const FPlanetCharacterInputActions& AStellarEraPlayerControllerBase::GetPlanetCharacterInputActions()
{
	Input.Build(this);
	return Input.Character;
}

const FSpaceshipInputActions& AStellarEraPlayerControllerBase::GetSpaceshipInputActions()
{
	Input.Build(this);
	return Input.Ship;
}

const FStarMapCameraInputActions& AStellarEraPlayerControllerBase::GetStarMapCameraInputActions()
{
	Input.Build(this);
	return Input.StarMap;
}

// ─────────────────────────────────────────────────────────────────────────────
// IStellarEraSessionCommands：M / E 的对象查询。
//
// 只回答「该换成谁」，机械动作在各自的 Pawn 上，`Possess` 由调用方做。
// ─────────────────────────────────────────────────────────────────────────────

AStarMapCameraPawn* AStellarEraPlayerControllerBase::FindStarMapCameraFor(const APawn* From) const
{
	// 「能用哪台相机」完全由 From 的类型与它的参考行星决定（见
	// ResolvePlanetStarMapCamera），本层只做转发 —— 它这里一个星图状态都不存：
	// 谁进了星图、退图回谁，都由那台相机自己记。
	return AStarMapCameraPawn::ResolvePlanetStarMapCamera(From);
}

ASpaceshipPawn* AStellarEraPlayerControllerBase::FindBoardableShipFrom(const APawn* From) const
{
	if (!From)
	{
		return nullptr;
	}

	// 最近的那艘不一定上得去：FindNearestTo 没有距离上限，够不够近由飞船自己判。
	const FVector Location = From->GetActorLocation();
	ASpaceshipPawn* const Ship = ASpaceshipPawn::FindNearestTo(GetWorld(), Location);
	if (!Ship || !Ship->CanBoardFrom(Location))
	{
		return nullptr;
	}
	return Ship;
}

APlanetCharacter* AStellarEraPlayerControllerBase::GetOrSpawnCrewCharacter(ASpaceshipPawn* Ship)
{
	if (!Ship)
	{
		return nullptr;
	}

	// 舱里有人就复用：登船时它只是被隐藏、附着在船上。
	if (APlanetCharacter* Boarded = Ship->GetBoardedCharacter())
	{
		return Boarded;
	}

	// 开局操控的是飞船，舱里还没有记账 —— 角色由飞船自己提供，直接挂在它下面
	// （Child Actor，或关卡里附着到船上）。这里不做生成兜底。
	TArray<AActor*> AttachedActors;
	Ship->GetAttachedActors(AttachedActors);
	for (AActor* Attached : AttachedActors)
	{
		APlanetCharacter* Character = Cast<APlanetCharacter>(Attached);
		if (!Character)
		{
			continue;
		}

		// 先按「在舱里」归一化：隐藏、关碰撞、MOVE_None。这样调用方的
		// DisembarkCrew 会走完整的出舱分支（摘附着、恢复显示与碰撞、清记账），
		// 而不是把一具状态未知的角色直接丢到船外。
		Ship->Board(Character);
		return Character;
	}

	UE_LOG(LogTemp, Warning,
		TEXT("AStellarEraPlayerControllerBase: 飞船 %s 上找不到角色"
			 "（舱里没记账，也没有挂接的 APlanetCharacter），按 E 无法离船。"),
		*GetNameSafe(Ship));
	return nullptr;
}
