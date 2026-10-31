// Copyright Voxel Plugin SAS. All Rights Reserved.
// 星图 UI 每帧要问玩家控制器的四件事。

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "StarMapUIInputs.generated.h"

class AActor;
class UPlanetMineralViewComponent;

UINTERFACE(MinimalAPI, meta = (CannotImplementInterfaceInBlueprint))
class UStarMapUIInputs : public UInterface
{
	GENERATED_BODY()
};

/**
 * 星图 UI 的输入来源。
 *
 * 声明在 UI 侧、由 Game 模块的 AStellarEraPlayerController 实现：依赖方向是
 * UI ← Game。UI 因此不需要 include 控制器（那会让 UI → Game → UI 成环）。
 *
 * 只放 UI 真正用到的查询。控制器自己的输入处理、上下船、模式切换都不在这里。
 */
class STELLARERAUI_API IStarMapUIInputs
{
	GENERATED_BODY()

public:
	/** 鼠标的视口像素坐标；光标不可用（未捕获 / 不可见）时返回 false。 */
	virtual bool GetStarMapMousePositionPx(FVector2D& OutPosition) const = 0;

	/** 任意屏幕点下的已注册天体。悬停与点击共用。 */
	virtual AActor* PickBodyUnderScreenPosition(const FVector2D& ScreenPosition) const = 0;

	/** 把一颗天体投到屏幕；成功时 OutCenter 为中心、OutRadiusPx 为轮廓半径（像素）。 */
	virtual bool ProjectBodyToScreen(AActor* Body, FVector2D& OutCenter, float& OutRadiusPx) const = 0;

	/** 当前激活的矿物透视组件；未激活时为空。 */
	virtual UPlanetMineralViewComponent* GetActiveMineralView() const = 0;
};
