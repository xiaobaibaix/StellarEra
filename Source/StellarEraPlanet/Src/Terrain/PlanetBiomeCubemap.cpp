// Copyright Voxel Plugin SAS. All Rights Reserved.
// biome cubemap：按方向烘配色，供材质查表 —— 颜色因此与网格采样密度无关。

#include "PlanetBiomeCubemap.h"

/**
 * 面 → 主轴 / u 轴 / v 轴，与 D3D 立方体贴图约定逐项对应。写成表因为烘焙与逆映射
 * 共用；写两遍必有一个符号写岔，而符号错只表现为某两面颜色对调，很难查。
 */
namespace
{
	struct FFaceBasis
	{
		FVector Major;
		FVector UAxis;
		FVector VAxis;
	};

	const FFaceBasis FaceBases[PlanetBiomeCubemap::NumFaces] =
	{
		{ FVector( 1.0,  0.0,  0.0), FVector(0.0, 0.0, -1.0), FVector(0.0, -1.0, 0.0) }, // +X
		{ FVector(-1.0,  0.0,  0.0), FVector(0.0, 0.0,  1.0), FVector(0.0, -1.0, 0.0) }, // -X
		{ FVector( 0.0,  1.0,  0.0), FVector(1.0, 0.0,  0.0), FVector(0.0,  0.0, 1.0) }, // +Y
		{ FVector( 0.0, -1.0,  0.0), FVector(1.0, 0.0,  0.0), FVector(0.0,  0.0,-1.0) }, // -Y
		{ FVector( 0.0,  0.0,  1.0), FVector(1.0, 0.0,  0.0), FVector(0.0, -1.0, 0.0) }, // +Z
		{ FVector( 0.0,  0.0, -1.0), FVector(-1.0, 0.0, 0.0), FVector(0.0, -1.0, 0.0) }, // -Z
	};
}

FVector PlanetBiomeCubemap::FaceUVToDirection(int32 Face, float U, float V)
{
	const int32 ClampedFace = FMath::Clamp(Face, 0, NumFaces - 1);
	const FFaceBasis& Basis = FaceBases[ClampedFace];

	// 归一化 UV → 面上的二维坐标：0 映射到 -1，1 映射到 +1。
	const double Sc = 2.0 * double(U) - 1.0;
	const double Tc = 2.0 * double(V) - 1.0;

	const FVector Direction = Basis.Major + Basis.UAxis * Sc + Basis.VAxis * Tc;
	return Direction.GetSafeNormal();
}

void PlanetBiomeCubemap::DirectionToFaceUV(
	const FVector& UnitDir, int32& OutFace, float& OutU, float& OutV)
{
	const FVector Dir = UnitDir.GetSafeNormal();
	const double AbsX = FMath::Abs(Dir.X);
	const double AbsY = FMath::Abs(Dir.Y);
	const double AbsZ = FMath::Abs(Dir.Z);

	// sc/tc 是「投影到本面二维坐标」，除以主轴分量的绝对值归一化到 [-1, 1]。
	double Sc = 0.0;
	double Tc = 0.0;
	if (AbsX >= AbsY && AbsX >= AbsZ)
	{
		OutFace = Dir.X >= 0.0 ? int32(EFace::PositiveX) : int32(EFace::NegativeX);
		const double Ma = FMath::Max(AbsX, UE_DOUBLE_SMALL_NUMBER);
		Sc = (Dir.X >= 0.0 ? -Dir.Z : Dir.Z) / Ma;
		Tc = -Dir.Y / Ma;
	}
	else if (AbsY >= AbsZ)
	{
		OutFace = Dir.Y >= 0.0 ? int32(EFace::PositiveY) : int32(EFace::NegativeY);
		const double Ma = FMath::Max(AbsY, UE_DOUBLE_SMALL_NUMBER);
		Sc = Dir.X / Ma;
		Tc = (Dir.Y >= 0.0 ? Dir.Z : -Dir.Z) / Ma;
	}
	else
	{
		OutFace = Dir.Z >= 0.0 ? int32(EFace::PositiveZ) : int32(EFace::NegativeZ);
		const double Ma = FMath::Max(AbsZ, UE_DOUBLE_SMALL_NUMBER);
		Sc = (Dir.Z >= 0.0 ? Dir.X : -Dir.X) / Ma;
		Tc = -Dir.Y / Ma;
	}

	OutU = float((Sc + 1.0) * 0.5);
	OutV = float((Tc + 1.0) * 0.5);
}
