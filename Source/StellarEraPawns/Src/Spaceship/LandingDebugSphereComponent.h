// Copyright Voxel Plugin SAS. All Rights Reserved.
//
// B 键调试视角里的「下船检测球」（线框）。

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"

#include "LandingDebugSphereComponent.generated.h"

class ASpaceshipPawn;

/**
 * 在船心画落地检测球：半径与按 E 下船用的是同一个 GetLandingQueryRadiusCm()。
 *
 * 为什么单独做成一个组件、而不是让 ASpaceshipPawn 在自己的 Tick 里画：
 * 飞船的 Actor Tick 跑在 TG_PrePhysics，而 UCosmosSimulationSubsystem 要到
 * UWorld::Tick 的 FTickableGameObject::TickObjects 阶段才把求解器位置写回 Actor。
 * 也就是说飞船 Tick 里读到的 GetActorLocation() 是**上一帧**的位置 —— 在那里画球，
 * 球会永远落后一帧（7 km/s @ 25 fps ≈ 280 m），看起来就是「球跑到船后面去了」。
 *
 * 本组件的 TickGroup 设为 TG_PostUpdateWork：它排在 TickObjects 之后，读到的是
 * 本帧写回后的位置，球与船严丝合缝。顺带它也出现在蓝图组件树里。
 *
 * 画的是 DrawDebug 线段（世界坐标、每帧重画）而不是网格组件：这样保持线框观感，
 * 也不需要额外的材质资产。只在调试视角打开、且飞船被本地控制时绘制。
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
