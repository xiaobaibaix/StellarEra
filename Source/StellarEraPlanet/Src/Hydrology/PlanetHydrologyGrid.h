// cubemap 栅格的索引约定。烘焙、采样、水面几何三处共用同一份 —— 抄一份就必然会
// 有一处对不上，而错位一格表现为「水面比地面高/低一格」，很难定位。

#pragma once

#include "CoreMinimal.h"
#include "PlanetBiomeCubemap.h"

namespace PlanetHydrologyGrid
{
	/** 每面四周各留一圈幽灵格。 */
	constexpr int32 Padding = 1;

	constexpr int32 FaceCount = PlanetBiomeCubemap::NumFaces;

	FORCEINLINE int32 PaddedSize(int32 Resolution) { return Resolution + 2 * Padding; }

	FORCEINLINE int32 CellCount(int32 Resolution)
	{
		const int32 Size = PaddedSize(Resolution);
		return FaceCount * Size * Size;
	}

	/**
	 * 格 → 归一化 UV。**只有一个定义**，烘焙与采样都从这里取。
	 *
	 * U = (Index - 0.5) / N：Index = 0 落在面外半格处，Index = N+1 落在另一侧面外
	 * 半格处 —— 幽灵格因此是向外的线性延拓，可以直接喂给 FaceUVToDirection
	 * （它不夹 U/V）。
	 */
	FORCEINLINE float CellToU(int32 Index, int32 Resolution)
	{
		return (float(Index) - 0.5f) / float(Resolution);
	}

	/** UV → 连续格坐标：U ∈ [0,1] ⇒ 返回值 ∈ [0.5, N+0.5]。 */
	FORCEINLINE float UToGrid(float U, int32 Resolution)
	{
		return U * float(Resolution) + 0.5f;
	}

	FORCEINLINE int32 CellIndex(int32 Face, int32 X, int32 Y, int32 Resolution)
	{
		const int32 Size = PaddedSize(Resolution);
		return (Face * Size + Y) * Size + X;
	}

	FORCEINLINE void DecodeCell(int32 Cell, int32 Resolution, int32& OutFace, int32& OutX, int32& OutY)
	{
		const int32 Size = PaddedSize(Resolution);
		OutFace = Cell / (Size * Size);
		const int32 Remainder = Cell % (Size * Size);
		OutY = Remainder / Size;
		OutX = Remainder % Size;
	}

	/** 格 → 单位方向。幽灵格会外插到面外，方向仍然有意义（只是不精确归一化到格心）。 */
	FORCEINLINE FVector CellDirection(int32 Face, int32 X, int32 Y, int32 Resolution)
	{
		return PlanetBiomeCubemap::FaceUVToDirection(
			Face, CellToU(X, Resolution), CellToU(Y, Resolution));
	}

	/**
	 * 方向 → 最近的格。
	 *
	 * 幽灵格的方向外插之后，最靠近的格有可能落在 [0, N+1] 之外（差 1 格），
	 * 所以夹到边界而不是断言。夹出来的最坏结果是「邻接到自己」—— 调用方必须丢弃
	 * 自环（见 PlanetHydrologyBake 的邻接表构造）。
	 */
	FORCEINLINE int32 DirectionToCell(const FVector& UnitDir, int32 Resolution)
	{
		int32 Face = 0;
		float U = 0.f;
		float V = 0.f;
		PlanetBiomeCubemap::DirectionToFaceUV(UnitDir, Face, U, V);
		const int32 X = FMath::Clamp(FMath::RoundToInt(UToGrid(U, Resolution)), 0, Resolution + 1);
		const int32 Y = FMath::Clamp(FMath::RoundToInt(UToGrid(V, Resolution)), 0, Resolution + 1);
		return CellIndex(Face, X, Y, Resolution);
	}
}
