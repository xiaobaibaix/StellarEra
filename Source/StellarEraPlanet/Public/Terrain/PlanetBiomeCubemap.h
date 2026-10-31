// Copyright Voxel Plugin SAS. All Rights Reserved.
// 行星 biome 色的方向查表：把配色烘到 cubemap 上，颜色与网格密度解耦。

#pragma once

#include "CoreMinimal.h"
#include "VoxelPlanetNoiseTerrain.h"

class UTextureCube;

/**
 * 行星 biome 色的方向查表。
 *
 * 顶点色只能存在于网格采样点上：LOD 7 的叶块 2 公里、每 64 米一个采样点，
 * 而配色是按海拔硬分带的，采样点一稀，海岸线整片区域就从「水」翻到「沙」。
 * 于是相邻两块 LOD 在屏幕上呈现为一条颜色突变的直线 —— 这不是光照或材质
 * 差异，是同一套配色被两种采样密度重算了一遍。
 *
 * 这里把配色烘到一张 cubemap 上：颜色只跟**方向**有关（ComputePlanetSurfaceColor
 * 的输入本来就是单位方向），与网格密度无关。材质按世界方向查表，粗 LOD 只是
 * 几何糊，颜色不再换一套。
 *
 * 面序与方向映射用 D3D/MSDN 的立方体贴图约定（UE 走 D3D 后端）：
 *   面  | u 轴 | v 轴 | 主轴
 *   +X  | -Z   | -Y   | +X
 *   -X  | +Z   | -Y   | -X
 *   +Y  | +X   | +Z   | +Y
 *   -Y  | +X   | -Z   | -Y
 *   +Z  | +X   | -Y   | +Z
 *   -Z  | -X   | -Y   | -Z
 * 其中 u/v ∈ [-1, 1]，纹理列从左到右对应 u 从 -1 到 +1，行从上到下对应
 * v 从 -1 到 +1（D3D 的 t=0 在图像顶行）。烘焙端与 GPU 采样端因此无需翻转。
 */
namespace PlanetBiomeCubemap
{
	/** 面序号，顺序即 cubemap 的面序。 */
	enum class EFace : int32
	{
		PositiveX = 0,
		NegativeX = 1,
		PositiveY = 2,
		NegativeY = 3,
		PositiveZ = 4,
		NegativeZ = 5,
	};

	constexpr int32 NumFaces = 6;

	/**
	 * 面 + 归一化 UV → 单位方向。
	 *
	 * U/V ∈ [0,1]，V 沿纹理行从上往下。这是烘焙侧用的映射：BakeFaceColors
	 * 对每个像素用它求方向，材质的 TextureSampleParameterCube 用的正是同一套
	 * 约定，两边不需要额外的翻转。
	 */
	STELLARERAPLANET_API FVector FaceUVToDirection(int32 Face, float U, float V);

	/** 单位方向 → 面 + UV。与 FaceUVToDirection 互为逆运算，供测试与调试用。 */
	STELLARERAPLANET_API void DirectionToFaceUV(
		const FVector& UnitDir, int32& OutFace, float& OutU, float& OutV);

	/**
	 * 烘焙一个面到 FColor 数组（行优先）。存的是 sRGB 编码值，配 SRGB=true 的
	 * 贴图采样后解回线性 —— 顶点色那边存的就是线性值，两端必须都落在线性上。
	 *
	 * 纯计算，不碰 RHI —— 自动化测试直接调这个，不需要图形设备。
	 * 深度取该方向的基准地表（Radius + Amplitude * Offset），与生成器在
	 * 地表采样点上算出的深度同阶，地质元素色因此不会偏。
	 */
	STELLARERAPLANET_API void BakeFaceColors(
		const FPlanetNoiseParams& Params,
		const FPlanetGeologyField* Geology,
		double Radius,
		int32 Face,
		int32 Resolution,
		TArray<FColor>& OutColors);

	/**
	 * 六面烘焙并上传成一张瞬态 UTextureCube。
	 *
	 * 需要 RHI（编辑器与游戏世界都有）。返回的贴图挂在瞬态包上，调用方必须
	 * 用 UPROPERTY 持有，否则会被 GC。Resolution ≤ 0 或创建失败时返回 nullptr。
	 */
	STELLARERAPLANET_API UTextureCube* BakeTexture(
		const FPlanetNoiseParams& Params,
		const FPlanetGeologyField* Geology,
		double Radius,
		int32 Resolution);
}
