// Copyright Voxel Plugin SAS. All Rights Reserved.

#include "PlanetBrushApplicator.h"

#include "VoxelPlanetActor.h"
#include "VoxelIntBox.h"
#include "VoxelTools/Gen/VoxelBoxTools.h"
#include "VoxelTools/Gen/VoxelSphereTools.h"
#include "VoxelVector.h"

bool PlanetBrushApplicator::Apply(
	AVoxelPlanetActor& Planet,
	const FVector& CenterWorld,
	const EPlanetBrushShape Shape,
	const int32 SizeVoxels,
	const int32 MinSizeVoxels,
	const int32 MaxSizeVoxels,
	const bool bPlace,
	const bool bMultiThreaded)
{
	const float VoxelSize = FMath::Max(Planet.VoxelSize, 1.f);

	// 工具的 OutModifiedValues 只记录**值真的变了**的体素（VoxelDataImpl::Set 里
	// `OldValue != NewValue` 才追加），这正是「是否真的改到了体素」的判据。
	TArray<FModifiedVoxelValue> ModifiedValues;

	if (Shape == EPlanetBrushShape::Sphere)
	{
		const float Radius = PlanetEditBrush::GetHalfExtentCm(
			SizeVoxels, VoxelSize, MinSizeVoxels, MaxSizeVoxels);
		if (bPlace)
		{
			UVoxelSphereTools::AddSphere(&Planet, CenterWorld, Radius,
				/*OutModifiedValues=*/&ModifiedValues, /*OutEditedBounds=*/nullptr,
				bMultiThreaded, /*bConvertToVoxelSpace=*/true, /*bUpdateRender=*/true);
		}
		else
		{
			UVoxelSphereTools::RemoveSphere(&Planet, CenterWorld, Radius,
				/*OutModifiedValues=*/&ModifiedValues, /*OutEditedBounds=*/nullptr,
				bMultiThreaded, /*bConvertToVoxelSpace=*/true, /*bUpdateRender=*/true);
		}
	}
	else
	{
		const FVoxelVector LocalCenter = Planet.GlobalToLocalFloat(CenterWorld);
		const FPlanetEditBox Box = PlanetEditBrush::MakeBox(
			FVector(LocalCenter.X, LocalCenter.Y, LocalCenter.Z),
			SizeVoxels, MinSizeVoxels, MaxSizeVoxels);
		// FPlanetEditBox 的 Min / Max 两端都含（MakeBox 的尺寸就是 Max-Min+1），
		// 而 FVoxelIntBox 的 Max 是开区间。直接透传会让方块笔刷少一格 ——
		// 换算收在 MakeVoxelBox 里（有单测），这里拿到的 MaxExclusive 已加过 1。
		const FPlanetEditVoxelBox VoxelBox = PlanetEditBrush::MakeVoxelBox(Box);
		const FVoxelIntBox Bounds(VoxelBox.Min, VoxelBox.MaxExclusive);

		if (bPlace)
		{
			UVoxelBoxTools::AddBox(&Planet, Bounds,
				/*OutModifiedValues=*/&ModifiedValues, /*OutEditedBounds=*/nullptr,
				bMultiThreaded, /*bUpdateRender=*/true);
		}
		else
		{
			UVoxelBoxTools::RemoveBox(&Planet, Bounds,
				/*OutModifiedValues=*/&ModifiedValues, /*OutEditedBounds=*/nullptr,
				bMultiThreaded, /*bUpdateRender=*/true);
		}
	}

	// 空操作不调 NotifyVoxelEdited（表面网格刷新 + 存档防抖）；渲染仍会在上面按请求包围盒刷新一次。
	if (ModifiedValues.Num() == 0)
	{
		return false;
	}

	Planet.NotifyVoxelEdited();
	return true;
}
