// Copyright Voxel Plugin SAS. All Rights Reserved.
// 星图的屏幕拾取：把天体投到屏幕、按像素容差挑出鼠标下的那一颗。纯几何，不碰 UI。

#pragma once

#include "CoreMinimal.h"

class AActor;
class APlayerController;
class UWorld;
struct FStarMapScreenCandidate;

/**
 * 星图拾取。投影要用 APlayerController（视口与视点），但不要控制器的业务状态；
 * 相机输入回调与 HUD 悬停查询都调这里，两边判据才一致。
 */
namespace StarMapPicking
{
	/** 没有可用相机参数时的拾取像素容差。rig 上的 SelectionPixelRadius 会覆盖它。 */
	constexpr float DefaultPixelTolerance = 20.f;

	/**
	 * 天体的世界半径（厘米）：体素行星用地表半径，其它退回碰撞半径，取不到返回 0。
	 * 拾取与「按 M 进场聚焦」共用这一份口径。
	 */
	STELLARERAPAWNS_API double GetBodyRadiusCm(const AActor* Body);

	/** 把天体投到屏幕；边缘点投不出来时 ScreenRadiusPx 留 0，调用方退回固定容差。 */
	STELLARERAPAWNS_API bool MakeScreenCandidate(
		const APlayerController* PC, AActor* Body, FStarMapScreenCandidate& Out);

	/**
	 * 屏幕点下的已注册天体；没有返回空。所有注册天体都可被点中 ——
	 * 星图视点不挂在天体上，焦点与「有没有视点物」无关。
	 */
	STELLARERAPAWNS_API AActor* PickBodyUnderScreenPosition(
		const UWorld* World,
		const APlayerController* PC,
		const FVector2D& ScreenPosition,
		float PixelTolerance);
}
