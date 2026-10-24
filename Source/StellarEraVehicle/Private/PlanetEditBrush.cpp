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

void PlanetEditBrush::BuildSpherePreviewSegments(const FVector& Center, const float RadiusCm,
	TArray<FPlanetEditSegment>& OutSegments)
{
	OutSegments.Reset();

	if (RadiusCm <= 0.f)
	{
		return;
	}

	// 三个正交大圆：XY、XZ、YZ。每对轴按平面序号取，避免写三份几乎相同的循环。
	constexpr int32 AxisPairs[3][2] = { {0, 1}, {0, 2}, {1, 2} };

	for (int32 Plane = 0; Plane < 3; ++Plane)
	{
		const int32 AxisA = AxisPairs[Plane][0];
		const int32 AxisB = AxisPairs[Plane][1];

		for (int32 Index = 0; Index < PreviewSegmentsPerCircle; ++Index)
		{
			const float Angle0 = 2.f * PI * static_cast<float>(Index) / PreviewSegmentsPerCircle;
			const float Angle1 = 2.f * PI * static_cast<float>(Index + 1) / PreviewSegmentsPerCircle;

			FVector Start = Center;
			FVector End = Center;
			Start[AxisA] += FMath::Cos(Angle0) * RadiusCm;
			Start[AxisB] += FMath::Sin(Angle0) * RadiusCm;
			End[AxisA] += FMath::Cos(Angle1) * RadiusCm;
			End[AxisB] += FMath::Sin(Angle1) * RadiusCm;

			OutSegments.Add({Start, End});
		}
	}
}

void PlanetEditBrush::BuildBoxPreviewSegments(const FIntVector& MinVoxels,
	const FIntVector& MaxExclusiveVoxels, TArray<FPlanetEditSegment>& OutSegments)
{
	OutSegments.Reset();

	const FVector Min(MinVoxels.X, MinVoxels.Y, MinVoxels.Z);
	const FVector Max(MaxExclusiveVoxels.X, MaxExclusiveVoxels.Y, MaxExclusiveVoxels.Z);

	// 8 个角按 (x, y, z) 的 0/1 组合编号：bit0 = X、bit1 = Y、bit2 = Z。
	FVector Corners[8];
	for (int32 Index = 0; Index < 8; ++Index)
	{
		Corners[Index] = FVector(
			(Index & 1) ? Max.X : Min.X,
			(Index & 2) ? Max.Y : Min.Y,
			(Index & 4) ? Max.Z : Min.Z);
	}

	// 沿每个轴 4 条棱，共 12 条。连接的角只差一个 bit。
	static constexpr int32 AxisMask[3] = {1, 2, 4};
	for (const int32 Mask : AxisMask)
	{
		for (int32 Index = 0; Index < 8; ++Index)
		{
			// 只在「该轴对应的 bit 为 0」时连一次，避免每条棱画两遍。
			if ((Index & Mask) != 0)
			{
				continue;
			}
			OutSegments.Add({Corners[Index], Corners[Index | Mask]});
		}
	}
}
