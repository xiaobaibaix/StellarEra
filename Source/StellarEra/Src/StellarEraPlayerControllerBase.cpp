// Copyright Voxel Plugin SAS. All Rights Reserved.
//
// AStellarEraPlayerControllerBase 实现的跨模块接口。
// 都是依赖倒置的钩子：声明在下游模块（Pawns），实现落在本层（Game，最上层），
// 消费方各自 `Cast<接口>(Controller)` 反查。与控制器生命周期代码分开放，
// 因为两者改动的理由不同。

#include "StellarEraPlayerControllerBase.h"

#include "PlanetCharacter.h"
#include "SpaceshipPawn.h"
#include "Engine/World.h"
#include "UObject/ConstructorHelpers.h"

void AStellarEraPlayerControllerBase::ToggleStarMap(APawn* From)
{
	// 只在子类实现；留空是为了让 Abstract 基类仍能构造 CDO。
}

AStellarEraPlayerControllerBase::AStellarEraPlayerControllerBase()
{
	// 下船才生成角色；这里只给默认类，实际生成发生在 GetOrSpawnCrewCharacter。
	static ConstructorHelpers::FClassFinder<APlanetCharacter> CrewBP(
		TEXT("/Game/Code/Pawns/Character/BP_PlanetCharacter"));
	if (CrewBP.Succeeded())
	{
		CrewCharacterClass = CrewBP.Class;
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// IStellarEraInputSource：角色与飞船的输入组件来取动作表；M 在每张表里都映射，
// 但只由被 Possess Pawn 上的 UStarMapInputComponent 绑定。
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
// IStellarEraSessionCommands：E 的对象查询。
//
// 只回答「该换成谁」，机械动作在各自的 Pawn 上，`Possess` 由调用方做。
// ─────────────────────────────────────────────────────────────────────────────

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

	// 开局操控的是飞船，舱里没有角色：下船时才在落脚点实时生成一个。
	if (!CrewCharacterClass)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("AStellarEraPlayerControllerBase: 没有配置 CrewCharacterClass，飞船 %s 无法离船。"),
			*GetNameSafe(Ship));
		return nullptr;
	}

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	APlanetCharacter* Spawned = GetWorld()->SpawnActor<APlanetCharacter>(
		CrewCharacterClass, Ship->GetCrewExitLocation(), FRotator::ZeroRotator, Params);
	if (!Spawned)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("AStellarEraPlayerControllerBase: 飞船 %s 的角色生成失败（%s）。"),
			*GetNameSafe(Ship), *GetNameSafe(CrewCharacterClass));
	}
	return Spawned;
}
