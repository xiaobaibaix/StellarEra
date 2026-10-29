// Copyright Voxel Plugin SAS. All Rights Reserved.

#include "PlanetBrushApplicator.h"

#include "VoxelPlanetActor.h"
#include "VoxelIntBox.h"
#include "VoxelTools/Gen/VoxelBoxTools.h"
#include "VoxelTools/Gen/VoxelSphereTools.h"
#include "VoxelTools/VoxelDataTools.h"
#include "VoxelVector.h"

bool PlanetBrushApplicator::Apply(
	AVoxelPlanetActor& Planet,
	const FVector& CenterWorld,
	const EPlanetBrushShape Shape,
	const int32 SizeVoxels,
	const int32 MinSizeVoxels,
	const int32 MaxSizeVoxels,
	const bool bPlace,
	const bool bMultiThreaded,
	FPlanetEditVoxelBox* const OutEditedBounds,
	const bool bUpdateRender)
{
	const float VoxelSize = FMath::Max(Planet.VoxelSize, 1.f);
	const double StartSeconds = FPlatformTime::Seconds();

	// 记录只在「包围盒不太大」时开。插件的 TVoxelDataImpl::Set 是按**整个包围盒**
	// AddUninitialized(Bounds.Count())，与真正改到几格无关：1280 体素的球要
	// 1286³ × 20 字节 ≈ 43 GB，光标还没落地就 OOM。超过预算就传 nullptr，
	// 改用「中心格前后是否相同」判断有没有真的改到东西。
	//
	// 小笔刷仍走记录：OutModifiedValues 只记**值真的变了**的体素
	// （VoxelDataImpl::Set 里 `OldValue != NewValue` 才追加），是最准的判据。
	const bool bRecordModifiedValues = PlanetEditBrush::ShouldRecordModifiedValues(SizeVoxels);

	// 中心格的体素坐标。读它只为了「改没改到」这一个是非题，不是采样地形。
	const FVoxelVector LocalCenter = Planet.GlobalToLocalFloat(CenterWorld);
	const FIntVector CenterVoxel(
		static_cast<int32>(FMath::RoundToInt(LocalCenter.X)),
		static_cast<int32>(FMath::RoundToInt(LocalCenter.Y)),
		static_cast<int32>(FMath::RoundToInt(LocalCenter.Z)));

	float ValueBefore = 0.f;
	if (!bRecordModifiedValues)
	{
		UVoxelDataTools::GetValue(ValueBefore, &Planet, CenterVoxel);
	}

	TArray<FModifiedVoxelValue> ModifiedValues;
	TArray<FModifiedVoxelValue>* const OutModifiedValues =
		bRecordModifiedValues ? &ModifiedValues : nullptr;

	// 工具会把请求包围盒无条件写进 OutEditedBounds（宏在做事之前就赋值），
	// 所以它只能用来**刷新渲染**，不能用来判断有没有改到 —— 后者用上面的记录。
	FVoxelIntBox EditedBounds;
	FVoxelIntBox* const OutBounds = OutEditedBounds ? &EditedBounds : nullptr;

	if (Shape == EPlanetBrushShape::Sphere)
	{
		const float Radius = PlanetEditBrush::GetHalfExtentCm(
			SizeVoxels, VoxelSize, MinSizeVoxels, MaxSizeVoxels);
		if (bPlace)
		{
			UVoxelSphereTools::AddSphere(&Planet, CenterWorld, Radius,
				OutModifiedValues, OutBounds,
				bMultiThreaded, /*bConvertToVoxelSpace=*/true, bUpdateRender);
		}
		else
		{
			UVoxelSphereTools::RemoveSphere(&Planet, CenterWorld, Radius,
				OutModifiedValues, OutBounds,
				bMultiThreaded, /*bConvertToVoxelSpace=*/true, bUpdateRender);
		}
	}
	else
	{
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
				OutModifiedValues, OutBounds,
				bMultiThreaded, bUpdateRender);
		}
		else
		{
			UVoxelBoxTools::RemoveBox(&Planet, Bounds,
				OutModifiedValues, OutBounds,
				bMultiThreaded, bUpdateRender);
		}
	}

	const double ToolMs = (FPlatformTime::Seconds() - StartSeconds) * 1000.0;

	bool bEdited = false;
	if (bRecordModifiedValues)
	{
		bEdited = ModifiedValues.Num() > 0;
	}
	else
	{
		float ValueAfter = 0.f;
		UVoxelDataTools::GetValue(ValueAfter, &Planet, CenterVoxel);
		bEdited = !FMath::IsNearlyEqual(ValueBefore, ValueAfter);
	}

	// 空操作不调 NotifyVoxelEdited（表面网格刷新 + 未保存标记）。
	if (!bEdited)
	{
		return false;
	}

	if (OutEditedBounds && EditedBounds.IsValid())
	{
		OutEditedBounds->Min = EditedBounds.Min;
		OutEditedBounds->MaxExclusive = EditedBounds.Max;
	}

	Planet.NotifyVoxelEdited();

	// 只报慢的：一次落笔超过 20 ms 才说话，免得连续挖把日志刷满。
	// 体素工具的代价是包围盒 Size³（见 PlanetEditBrush::EstimateToolCellCount），
	// 「工具」一栏就是这块；与总耗时的差值落在渲染刷新上。
	const double TotalMs = (FPlatformTime::Seconds() - StartSeconds) * 1000.0;
	if (TotalMs >= 20.0)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[Dig] %s %d 体素（%lld 格）：体素工具 %.0f ms，总 %.0f ms，记录=%s"),
			bPlace ? TEXT("填") : TEXT("挖"), SizeVoxels,
			PlanetEditBrush::EstimateToolCellCount(SizeVoxels), ToolMs, TotalMs,
			bRecordModifiedValues ? TEXT("开") : TEXT("关"));
	}

	return true;
}
