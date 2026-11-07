// Copyright Voxel Plugin SAS. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * 行星阴影的解析解：与 M_PlanetShadowLightFunction 的 HLSL 同一套式子，这边供 C++ 与测试。
 * 改公式两边必须同步。为什么不用阴影图：行星半径 20 km，遮挡球体离着色点 17~20 km，
 * 而定向光阴影图只覆盖相机附近（400 m），永远看不到它。
 */
namespace PlanetShadow
{
	/**
	 * 太阳被行星挡住多少：1 = 完全照到，0 = 完全在影子里。把「指向太阳的射线」与行星球体
	 * 求交：TowardSun <= 0 → 1，Perp < R → 0，Perp ∈ [R, R+Penumbra] 线性过渡（单位世界厘米）。
	 * 点在球面上时退化为「仰角 < 0 即遮挡」，但对高空物体也成立。
	 */
	STELLARERAPLANET_API float ComputeLightMask(
		const FVector& WorldPosition,
		const FVector& DirectionToSun,
		const FVector& PlanetCenter,
		double PlanetRadiusCm,
		double PenumbraCm);
}
