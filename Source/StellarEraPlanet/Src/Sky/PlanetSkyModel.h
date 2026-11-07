#pragma once

#include "CoreMinimal.h"
#include "PlanetAtmosphereParams.h"

/**
 * 自建大气壳的天空积分器（CPU 侧）。是 Shaders/PlanetClouds.ush 单次散射部分的第二份实现，
 * 改那边就要回来对一遍：光学厚度、相位函数、SunVis 淡出窗口、alpha 合成方式四项。
 * 纯 C++，无 UObject，可直接单测。
 */
namespace PlanetSky
{
	/** 一颗太阳：单位方向（行星中心 → 光源，来自 CollectSunsFor）与颜色×相对亮度。 */
	struct FSun
	{
		FVector Direction = FVector::UpVector;
		FLinearColor Radiance = FLinearColor::White;
	};

	/** 一次立方图渲染的全部输入。长度单位 cm，与壳材质同一套。 */
	struct FModelInput
	{
		/** 观察点（玩家相机）世界坐标。 */
		FVector ObserverLocation = FVector::ZeroVector;
		FVector PlanetCenter = FVector::ZeroVector;
		float SurfaceRadiusCm = 0.0f;
		float ShellRadiusCm = 0.0f;
		PlanetAtmosphere::FShellScatteringParams Scattering;
		/** 最多 4 颗，与 PlanetClouds::MaxSuns 同源；由 CollectSunsFor 按 MaxSuns 截断。 */
		TArray<FSun> Suns;
		/** 壳材质里的补偿系数（默认 14），不是太阳真亮了 14 倍。 */
		FLinearColor SunIntensity = FLinearColor(14.0f, 14.0f, 14.0f, 1.0f);
		/** 0 = 晴空。只做一阶衰减，不参与积分。 */
		float CloudCoverage = 0.0f;
		/** 星空/银河的平均辐亮：逃逸到太空的方向按透射率混进来。 */
		FLinearColor SpaceSkyRadiance = FLinearColor(0.008f, 0.010f, 0.020f, 1.0f);
		/** 与壳着色器的亮度校准倍率，只作用于散射项。 */
		float BrightnessScale = 1.0f;
		/** 单条射线的积分步数，与材质 ATMO_STEPS 默认一致。 */
		int32 Steps = 24;
	};

	/**
	 * 立方图第 FaceIndex 面、第 (PixelX, PixelY) 个像素对应的世界方向（单位向量）。
	 * 面序与 UV 映射抄自 ReflectionEnvironmentShaders.usf 的 GetCubemapVector：
	 * ScaledUVs = ((FaceCoord + 0.5) / Res) * 2 - 1，FaceCoord = (col, row) = (PixelX, PixelY)。
	 * 这张表错了环境光就会从错误的方向来，改之前先回引擎源码对一遍。
	 */
	FVector CubeFaceDirection(int32 FaceIndex, int32 PixelX, int32 PixelY, int32 Resolution);

	/** 单方向辐亮度，已按壳材质的 alpha 合成折算成「肉眼看到的颜色」。供测试与调试。 */
	FLinearColor SampleDirection(const FModelInput& Input, const FVector& Direction);

	/** 按引擎立方图约定渲染 6 面到 OutPixels（面主序，同一面内先行后列）。 */
	void RenderCube(const FModelInput& Input, int32 Resolution, TArray<FLinearColor>& OutPixels);
}
