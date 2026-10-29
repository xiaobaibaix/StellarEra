// Copyright Voxel Plugin SAS. All Rights Reserved.

#include "PlanetBrushApplicator.h"

#include "PlanetProgressiveEdit.h"
#include "VoxelIntBox.h"
#include "VoxelPlanetActor.h"
#include "VoxelTools/VoxelToolHelpers.h"

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
	const double StartSeconds = FPlatformTime::Seconds();

	// 建一个「一块到底」的 job 再一次跑完。这样同步落笔与渐进挖掘
	// **共用同一份逐格谓词**，不存在两套实现各自漂移的可能；
	// ChunkSizeVoxels = MAX_int32 保证 SplitVoxelBoxIntoChunks 只切出 1 块。
	FPlanetProgressiveEdit Edit;
	if (!Edit.Setup(&Planet, CenterWorld, Shape, SizeVoxels, MinSizeVoxels, MaxSizeVoxels,
			bPlace, /*ChunkSizeVoxels=*/MAX_int32, bMultiThreaded))
	{
		return false;
	}

	// 逐格比对旧值得到的「真的改到没有」。比过去那两种判据都准：
	// 记录数组在 1280 体素时要 GB 级内存（见 ShouldRecordModifiedValues），
	// 而「中心格前后是否相同」会漏掉只改到外圈的大笔刷。
	const bool bEdited = Edit.ApplyAll();
	const double DataMs = (FPlatformTime::Seconds() - StartSeconds) * 1000.0;

	// 空操作不调 NotifyVoxelEdited（表面网格刷新 + 未保存标记）。
	if (!bEdited)
	{
		return false;
	}

	const FPlanetEditVoxelBox& FullBounds = Edit.GetFullBounds();
	if (OutEditedBounds)
	{
		OutEditedBounds->Min = FullBounds.Min;
		OutEditedBounds->MaxExclusive = FullBounds.MaxExclusive;
	}

	// 执行单元只改体素数据、不碰渲染。刷新放在锁外的这一点，与插件的
	// GENERATED_TOOL_SUFFIX 完全同一位置（`if (bUpdateRender) UpdateWorld(...)`）。
	// 范围取整笔编辑的包围盒：一块到底时它就是个单块，与过去传给工具的一致。
	if (bUpdateRender)
	{
		FVoxelToolHelpers::UpdateWorld(
			&Planet, FVoxelIntBox(FullBounds.Min, FullBounds.MaxExclusive));
	}

	Planet.NotifyVoxelEdited();

	// 只报慢的：一次落笔超过 20 ms 才说话，免得连续挖把日志刷满。
	// 体素工具的代价是包围盒 Size³（见 PlanetEditBrush::EstimateToolCellCount），
	// 「数据」一栏就是这块；与总耗时的差值落在渲染刷新上。
	const double TotalMs = (FPlatformTime::Seconds() - StartSeconds) * 1000.0;
	if (TotalMs >= 20.0)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[Dig] %s %d 体素（%lld 格）：体素数据 %.0f ms，总 %.0f ms"),
			bPlace ? TEXT("填") : TEXT("挖"), SizeVoxels,
			PlanetEditBrush::EstimateToolCellCount(SizeVoxels), DataMs, TotalMs);
	}

	return true;
}
