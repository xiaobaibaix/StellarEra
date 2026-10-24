// Copyright Voxel Plugin SAS. All Rights Reserved.
// 剖面圆盘的 CPU 光栅化：逐像素算矿物的颜色与不透明度。
// 只依赖值类型与两个回调，不触引擎、不触资产，可在无头测试里跑。

#pragma once

#include "CoreMinimal.h"
#include "PlanetGeologyTypes.h"

/** 一次剖面渲染的全部输入。 */
struct FPlanetSectionImageParams
{
	int32 Resolution = 512;

	/** 平面基：AxisU 向右、AxisV 向上（图像第 0 行对应 −AxisV）。 */
	FVector AxisU = FVector::ForwardVector;
	FVector AxisV = FVector::RightVector;

	/** 切面法线。光栅化本身不用它，保留给调试与未来的各向异性。 */
	FVector PlaneNormal = FVector::UpVector;

	/** 剖面半径（厘米）= 1.05 × 基准表面半径。 */
	float SectionRadiusCm = 100.f;

	/** 一个体素的边长（厘米）。 */
	float VoxelSize = 100.f;

	/** 基准表面半径（体素），用于深度变暗的归一化。 */
	float BaseRadiusVoxels = 100.f;

	/** 没有矿处的基础底色（行星 SurfaceColor）。 */
	FLinearColor SurfaceColor = FLinearColor::Black;

	/** 颜色显现参数（阈值、渐变、纯色门限）。 */
	FPlanetGeologyAppearance Appearance;
};

struct FPlanetSectionImage
{
	/**
	 * 填充 OutPixels（Res×Res，行主序，BGRA 语义由 FColor 承担）。
	 *
	 * SurfaceRadiusCmFn：给单位方向，返回该方向的地表半径（厘米，含起伏）。
	 * SampleFn：给单位方向与深度（体素，向内为正），返回成分采样。
	 *
	 * 公式见 spec §3.1；每像素两次回调，调用方负责它们的开销。
	 */
	static void Render(
		const FPlanetSectionImageParams& Params,
		TFunctionRef<float(const FVector& UnitDir)> SurfaceRadiusCmFn,
		TFunctionRef<FPlanetGeologySample(const FVector& UnitDir, double DepthVoxels)> SampleFn,
		TArray<FColor>& OutPixels);
};
