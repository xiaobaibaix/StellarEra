// Copyright Voxel Plugin SAS. All Rights Reserved.

#include "PlanetEditBrush.h"

int32 PlanetEditBrush::ClampSize(const int32 SizeVoxels)
{
	return FMath::Clamp(SizeVoxels, MinSizeVoxels, MaxSizeVoxels);
}

float PlanetEditBrush::GetHalfExtentCm(const int32 SizeVoxels, const float VoxelSizeCm)
{
	return ClampSize(SizeVoxels) * VoxelSizeCm * 0.5f;
}

FVector PlanetEditBrush::GetEditCenterWorld(
	const FVector& HitLocation,
	const FVector& HitNormal,
	const int32 SizeVoxels,
	const float VoxelSizeCm,
	const bool bPlace)
{
	if (!bPlace)
	{
		return HitLocation;
	}

	// 法线为零（退化命中）时不偏移，总比把方块放到 inf/nan 强。
	const FVector Normal = HitNormal.GetSafeNormal();
	return HitLocation + Normal * GetHalfExtentCm(SizeVoxels, VoxelSizeCm);
}

FPlanetEditBox PlanetEditBrush::MakeBox(const FVector& LocalCenterVoxels, const int32 SizeVoxels)
{
	const int32 Size = ClampSize(SizeVoxels);
	// FVector 的分量是 double，FMath::RoundToInt(double) 返回 int64；
	// FIntVector 收 int32。本项目警告即错误，必须显式窄化。
	const FIntVector Center(
		static_cast<int32>(FMath::RoundToInt(LocalCenterVoxels.X)),
		static_cast<int32>(FMath::RoundToInt(LocalCenterVoxels.Y)),
		static_cast<int32>(FMath::RoundToInt(LocalCenterVoxels.Z)));

	const int32 Low = (Size - 1) / 2;
	const int32 High = Size - 1 - Low;

	FPlanetEditBox Box;
	Box.Min = Center - FIntVector(Low);
	Box.Max = Center + FIntVector(High);
	return Box;
}

FPlanetEditVoxelBox PlanetEditBrush::MakeVoxelBox(const FPlanetEditBox& Box)
{
	FPlanetEditVoxelBox VoxelBox;
	VoxelBox.Min = Box.Min;
	// 两端都含 → 右端开：Max 本身也是有效格，开区间要再往后挪一格。
	VoxelBox.MaxExclusive = Box.Max + FIntVector(1);
	return VoxelBox;
}
