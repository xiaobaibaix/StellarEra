// Copyright Voxel Plugin SAS. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * 行星阴影（行星本体挡住太阳）的解析解。
 *
 * 与 /Game/Code/Planet/Materials/M_PlanetShadowLightFunction 里 Custom 节点的
 * HLSL 是同一套式子 —— 那边每像素算一遍，这边供 C++ 与自动化测试用。
 * 改公式时两边必须同时改；PlanetShadowMaskTests 锁住了 C++ 这边的期望值。
 *
 * 为什么不用阴影图：行星半径 20 km，挡住太阳的是离着色点 17~20 km 外的球体，
 * 而定向光的阴影图只覆盖相机附近（本关卡 DynamicShadowDistance 400 m）。
 * 阴影图永远看不到这块遮挡物，夜面的飞船就会被直射阳照亮 —— 2026-11-05 的现象。
 */
namespace PlanetShadow
{
	/**
	 * 太阳被行星挡住多少：1 = 完全照到，0 = 完全在行星影子里。
	 *
	 * @param WorldPosition   着色点（世界厘米）
	 * @param DirectionToSun  着色点指向太阳的单位向量（= 定向光传播方向的反向）
	 * @param PlanetCenter    行星球心（世界厘米）
	 * @param PlanetRadiusCm  行星表面半径（厘米）
	 * @param PenumbraCm      半影宽度（厘米），<= 1 时按 1 处理
	 *
	 * 做法是把「指向太阳的射线」与行星球体求交：
	 *   - 球心在射线**前方**（TowardSun > 0）且射线到球心的垂直距离 Perp < R
	 *     → 挡住，乘 0；
	 *   - Perp 落在 [R, R + Penumbra] → 线性过渡，给球缘一点软边；
	 *   - 球心在射线背后（TowardSun <= 0）→ 太阳没被挡，乘 1。
	 *
	 * 点在球面上时它退化成「太阳仰角 < 0 即遮挡」，与 PlanetClouds.ush 的
	 * SunVis 同口径；但它对高空物体也成立（10 倍半径高、太阳只在本地地平线
	 * 下 10° 时仍然照得到，不会被误判成夜里）。
	 */
	STELLARERAPLANET_API float ComputeLightMask(
		const FVector& WorldPosition,
		const FVector& DirectionToSun,
		const FVector& PlanetCenter,
		double PlanetRadiusCm,
		double PenumbraCm);
}
