// Copyright Voxel Plugin SAS. All Rights Reserved.
// 矿物剖面的纯数学：朝向法线、平面基、旋转、屏幕拾取、遮罩镜像、环上取点。
// 全部无引擎资产依赖，可在无头测试里直接调用。

#pragma once

#include "CoreMinimal.h"

/** 三根旋转环对应的世界轴。 */
enum class ESectionRing : uint8
{
	X = 0,
	Y = 1,
	Z = 2
};

/** 世界空间的圆环折线（组件产出，控制器投影）。 */
struct FSectionRingWorldPolyline
{
	ESectionRing Ring = ESectionRing::X;
	TArray<FVector> Points;
};

/** 屏幕空间的圆环折线（像素坐标）。 */
struct FSectionRingScreenPolyline
{
	ESectionRing Ring = ESectionRing::X;
	TArray<FVector2D> Points;
};

struct FPlanetCutawayMath
{
	/**
	 * 让「相机侧」有正投影的法线：dot(CameraPos − CenterCm, 结果) >= 0。
	 * 退化（法线为零或相机在球心）时原样返回归一化后的 PlaneNormal。
	 */
	static FVector ComputeClipNormal(
		const FVector& PlaneNormal,
		const FVector& CameraPos,
		const FVector& CenterCm);

	/**
	 * 由法线造右手正交基：AxisU、AxisV、N 两两垂直，且 Cross(N, AxisU) == AxisV。
	 * AxisV 即「从相机看过去的上方」，图像第 0 行对应 −AxisV（spec §3.1）。
	 */
	static void ComputePlaneBasis(
		const FVector& PlaneNormal,
		FVector& OutAxisU,
		FVector& OutAxisV);

	/** 绕世界轴旋转已有朝向：Quat(Axis, radians(DeltaDegrees)) * PlaneOrientation。 */
	static FQuat RotatePlane(
		const FQuat& PlaneOrientation,
		const FVector& Axis,
		float DeltaDegrees);

	/**
	 * 屏幕空间拾取最近的圆环折线。距离用点到折线各段的最短距离。
	 * 并列（差 < 1e-3 px）时按 X → Y → Z 顺序取先出现者。
	 * 全部超过 TolerancePx 时返回 false。
	 */
	static bool PickSectionRing(
		TArrayView<const FSectionRingScreenPolyline> Rings,
		const FVector2D& MousePx,
		float TolerancePx,
		ESectionRing& OutRing);

	/**
	 * spec §4.1 遮罩公式的 CPU 镜像，供测试与心智校验：
	 * Half = saturate(dot(P−Origin, N) * 10)
	 * Inside = saturate((InsideRadiusCm − |P−Origin|) * 0.02)
	 * Mask = 1 − Half * Inside * (bActive ? 1 : 0)
	 */
	static float ComputeOpacityMask(
		const FVector& WorldPos,
		const FVector& Origin,
		const FVector& Normal,
		float InsideRadiusCm,
		bool bActive);

	/** 某根环上角度处的世界偏移（未加球心）。X 环在 YZ 平面，Y 环在 XZ，Z 环在 XY。 */
	static FVector RingPoint(ESectionRing Ring, float Radius, float AngleRadians);
};
