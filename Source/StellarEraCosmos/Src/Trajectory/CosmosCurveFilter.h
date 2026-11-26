// Copyright Voxel Plugin SAS. All Rights Reserved.
// 轨迹线 / 预测线的几何滤波：闭合误差分摊、空间平滑、按角度重采样、帧间指数平滑。

#pragma once

#include "CoreMinimal.h"

/** 全是纯函数，测试直接调。FVector 就是 FVector3d，世界坐标与求解器坐标共用一套。 */
namespace CosmosCurveFilter
{
	/** 把末点相对首点的误差 Δ 按序号线性摊开（第 i 点减 i/(N-1)·Δ）；结束后末点与首点逐位相等。 */
	STELLARERACOSMOS_API void DistributeClosureError(TArray<FVector3d>& Points);

	/** 闭环平滑（要求末点 == 首点）：循环 [1,2,1]/4 迭代 Iterations 次，结束后仍首尾逐位相等。 */
	STELLARERACOSMOS_API void SmoothClosed(TArray<FVector3d>& Points, int32 Iterations);

	/** 开放曲线平滑：同一核，首末点固定 —— 线始终从天体当前位置出发。 */
	STELLARERACOSMOS_API void SmoothOpen(TArray<FVector3d>& Points, int32 Iterations);

	/**
	 * 闭环按轨道面内的**绝对**角度重采样成 NumOut 点再补尾点（= 首点），让前后两帧逐点对应。
	 * 法向由运动方向定，角度零点 = 世界 X 投到轨道面（退化时用 Y）。
	 * 角度沿环不单调（自交 / 严重进动）或不是恰好绕一圈时返回 false。
	 */
	STELLARERACOSMOS_API bool ResampleClosedByAngle(const TArray<FVector3d>& Loop, int32 NumOut,
		TArray<FVector3d>& Out, FVector3d& OutNormal);

	/** Current 逐点朝 Target 走 Alpha（0..1）。点数不同返回 false，不改 Current。 */
	STELLARERACOSMOS_API bool BlendToward(TArray<FVector3d>& Current, const TArray<FVector3d>& Target,
		double Alpha);

	/** 帧长 → 指数平滑系数 1 − exp(−Dt/Tau)，与帧率无关。Tau ≤ 0 返回 1（不平滑）。 */
	STELLARERACOSMOS_API double SmoothingAlpha(double DeltaSeconds, double TauSeconds);
}
