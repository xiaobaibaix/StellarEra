// Copyright Voxel Plugin SAS. All Rights Reserved.
// B 键调试视角里的「下船检测球」（线框）。

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"

#include "LandingDebugSphereComponent.generated.h"

class ASpaceshipPawn;

/**
 * 在船心画落地检测球：半径与按 E 下船用的是同一个 GetLandingQueryRadiusCm()。
 * 单独做成组件是为了 tick 顺序：飞船 Tick 跑在 TG_PrePhysics，求解器要到
 * FTickableGameObject::TickObjects 才写回位置，在那里画会永远落后一帧。
 * 本组件在 TG_PostUpdateWork，读到的是本帧位置。只在调试视角打开且本地控制时绘制。
 */
UCLASS(ClassGroup = (Spaceship))
class STELLARERAPAWNS_API ULandingDebugSphereComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	ULandingDebugSphereComponent();

	//~ Begin UActorComponent Interface.
	virtual void TickComponent(float DeltaTime, ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;
	//~ End UActorComponent Interface.
};
