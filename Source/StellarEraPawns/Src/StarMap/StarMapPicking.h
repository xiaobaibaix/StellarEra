// Copyright Voxel Plugin SAS. All Rights Reserved.
// 星图的屏幕拾取：把天体投到屏幕、按像素容差挑出鼠标下的那一颗。纯几何，不碰 UI。

#pragma once

#include "CoreMinimal.h"

class AActor;
class APlayerController;
class UWorld;
struct FStarMapScreenCandidate;

/**
 * 星图拾取。
 *
 * 投影要用 APlayerController（视口与视点），但不需要控制器的任何业务状态；
 * 星图相机的输入回调（Pawns）与 HUD 的悬停查询（经控制器转发）都调这里，
 * 两边判据才一致。
 */
namespace StarMapPicking
{
	/** 没有可用相机参数时的拾取像素容差。相机上的 SelectionPixelRadius 会覆盖它。 */
	constexpr float DefaultPixelTolerance = 20.f;

	/**
	 * 天体的世界半径（厘米）。体素行星用它的地表半径；其它天体退回碰撞半径。
	 * 取不到（非正）返回 0 —— 调用方按「没算出来」处理。
	 *
	 * 拾取与「按 M 进场聚焦」共用这一份：两边对「半径」的口径必须一致。
	 */
	STELLARERAPAWNS_API double GetBodyRadiusCm(const AActor* Body);

	/**
	 * 把一颗天体投到屏幕。Out.ScreenRadiusPx 是视角下的轮廓半径（像素）；
	 * 边缘点投不出来时留 0，调用方退回固定像素容差。
	 */
	STELLARERAPAWNS_API bool MakeScreenCandidate(
		const APlayerController* PC, AActor* Body, FStarMapScreenCandidate& Out);

	/**
	 * 屏幕点下的已注册天体；没有返回空。
	 *
	 * 只有挂了星图相机的天体可被点中（见 AStarMapCameraPawn::HasStarMapCamera）：
	 * 星图的相机是「照某颗天体」的视点，选中即焦点，飞船不该抢走焦点。
	 */
	STELLARERAPAWNS_API AActor* PickBodyUnderScreenPosition(
		const UWorld* World,
		const APlayerController* PC,
		const FVector2D& ScreenPosition,
		float PixelTolerance);
}
