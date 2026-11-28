// Copyright Voxel Plugin SAS. All Rights Reserved.
// 行星 biome 色的方向查表：把配色烘到 cubemap 上，颜色与网格密度解耦。

#pragma once

#include "CoreMinimal.h"

/**
 * 行星 biome 色的方向查表。顶点色只在网格采样点上，粗 LOD 下按海拔分带会让海岸线
 * 整片翻面、LOD 边界出现颜色突变直线。配色只跟方向有关，烘到 cubemap 后材质按
 * 世界方向查表，颜色不再随网格密度改变。面序与 UV 用 D3D 立方体贴图约定。
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

	/** 面 + 归一化 UV → 单位方向。V 沿纹理行从上往下，与
	 * TextureSampleParameterCube 同一约定，两边无需翻转。 */
	STELLARERACELESTIAL_API FVector FaceUVToDirection(int32 Face, float U, float V);

	/** 单位方向 → 面 + UV。与 FaceUVToDirection 互为逆运算，供测试与调试用。 */
	STELLARERACELESTIAL_API void DirectionToFaceUV(
		const FVector& UnitDir, int32& OutFace, float& OutU, float& OutV);
}
