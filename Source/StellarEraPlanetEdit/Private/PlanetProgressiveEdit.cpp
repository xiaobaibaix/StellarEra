#include "PlanetProgressiveEdit.h"

#include "PlanetProgressiveEditKernel.h"
#include "VoxelData/VoxelData.h"
#include "VoxelData/VoxelDataLock.h"
#include "VoxelIntBox.h"
#include "VoxelPlanetActor.h"
#include "VoxelTools/VoxelToolHelpers.h"
#include "VoxelValue.h"

namespace
{
	/** 写锁的名字。与插件的 FUNCTION_FNAME 同一个用途：出问题时能看出是谁锁的。 */
	const FName ProgressiveEditLockName(TEXT("PlanetProgressiveEdit"));
}

bool FPlanetProgressiveEdit::Setup(
	AVoxelPlanetActor* const InPlanet,
	const FVector& CenterWorld,
	const EPlanetBrushShape InShape,
	const int32 InSizeVoxels,
	const int32 InMinSizeVoxels,
	const int32 InMaxSizeVoxels,
	const bool bInPlace,
	const int32 ChunkSizeVoxels,
	const bool bInMultiThreaded)
{
	Planet.Reset();
	Chunks.Reset();
	NextChunkIndex = 0;
	AppliedBounds = FPlanetEditVoxelBox();
	LastChunkBounds = FPlanetEditVoxelBox();
	bChangedAnything.store(false, std::memory_order_relaxed);

	if (!IsValid(InPlanet) || !InPlanet->IsCreated())
	{
		return false;
	}

	Shape = InShape;
	bPlace = bInPlace;
	bMultiThreaded = bInMultiThreaded;
	SizeVoxels = InSizeVoxels;

	// 世界 → 体素：与 UVoxelSphereTools 内部同一个函数（GetRealPosition 就是
	// VoxelWorld->GlobalToLocalFloat），不自己除 VoxelSize —— 那会漏掉缩放。
	const FVoxelVector RealPosition =
		FVoxelToolHelpers::GetRealPosition(InPlanet, CenterWorld, /*bConvertToVoxelSpace=*/true);
	CenterVoxels = FVector(RealPosition.X, RealPosition.Y, RealPosition.Z);

	if (Shape == EPlanetBrushShape::Sphere)
	{
		// 半径和包围盒都按插件那一套算：半径先夹到区间的半边长（厘米），
		// 再除 VoxelSize 换到体素。
		const float VoxelSize = FMath::Max(InPlanet->VoxelSize, 1.f);
		const float RadiusCm = PlanetEditBrush::GetHalfExtentCm(
			InSizeVoxels, VoxelSize, InMinSizeVoxels, InMaxSizeVoxels);
		RadiusVoxels = FVoxelToolHelpers::GetRealDistance(
			InPlanet, RadiusCm, /*bConvertToVoxelSpace=*/true);

		FullBounds = PlanetEditBrush::MakeSphereEditVoxelBox(CenterVoxels, RadiusVoxels);
		PlanetProgressiveKernel::GetSphereThresholds(
			RadiusVoxels, SquaredRadiusPlus2, SquaredRadiusMinus2);
	}
	else
	{
		// 与 PlanetBrushApplicator 同一套：先按四舍五入的体素中心取两端都含的盒，
		// 再换成右端开。方块没有半径的概念，这里只留 0 免得误用。
		RadiusVoxels = 0.f;
		SquaredRadiusPlus2 = 0.f;
		SquaredRadiusMinus2 = 0.f;

		const FPlanetEditBox Box = PlanetEditBrush::MakeBox(
			CenterVoxels, InSizeVoxels, InMinSizeVoxels, InMaxSizeVoxels);
		FullBounds = PlanetEditBrush::MakeVoxelBox(Box);
	}

	if (!PlanetEditBrush::IsValidVoxelBox(FullBounds))
	{
		return false;
	}

	PlanetEditBrush::SplitVoxelBoxIntoChunks(FullBounds, ChunkSizeVoxels, Chunks);
	if (Chunks.Num() == 0)
	{
		return false;
	}

	Planet = InPlanet;
	return true;
}

bool FPlanetProgressiveEdit::ApplyNextChunk()
{
	if (IsFinished())
	{
		return false;
	}

	AVoxelPlanetActor* const Target = Planet.Get();
	if (!IsValid(Target) || !Target->IsCreated())
	{
		// 行星没了（被销毁 / 换关）：剩下的块不再有意义，直接判完成，
		// 否则调用方的队列会拿着一个永远推不动的 job。
		NextChunkIndex = Chunks.Num();
		return false;
	}

	ApplyChunk(Chunks[NextChunkIndex]);
	++NextChunkIndex;

	return !IsFinished();
}

bool FPlanetProgressiveEdit::ApplyAll()
{
	while (ApplyNextChunk())
	{
	}

	return DidChangeAnything();
}

void FPlanetProgressiveEdit::ApplyChunk(const FPlanetEditVoxelBox& Chunk)
{
	AVoxelPlanetActor* const Target = Planet.Get();
	if (!IsValid(Target))
	{
		return;
	}

	FVoxelData& WorldData = Target->GetData();
	const FVoxelIntBox ChunkBounds(Chunk.Min, Chunk.MaxExclusive);

	// 写锁只围住数据写入，渲染刷新在锁外 —— 与插件工具完全一致
	//（VoxelGeneratedTools.h 的 GENERATED_TOOL_CALL）。
	{
		FVoxelWriteScopeLock Lock(WorldData, ChunkBounds, ProgressiveEditLockName);

		// 谓词只读格子自己的坐标与当前值，不依赖邻格，所以「分块写」与
		//「一次写完」逐格同结果（块不重叠且并集等于整盒，单测钉住）。
		std::atomic<bool>& Changed = bChangedAnything;
		const bool bAdd = bPlace;

		if (Shape == EPlanetBrushShape::Sphere)
		{
			const FVector Center = CenterVoxels;
			const float Radius = RadiusVoxels;
			const float Plus2 = SquaredRadiusPlus2;
			const float Minus2 = SquaredRadiusMinus2;

			WorldData.ParallelSet<FVoxelValue>(ChunkBounds,
				[Center, Radius, Plus2, Minus2, bAdd, &Changed](
					int32 X, int32 Y, int32 Z, FVoxelValue& Value)
				{
					const float SquaredDistance =
						PlanetProgressiveKernel::SquaredDistanceTo(Center, X, Y, Z);
					const FVoxelValue NewValue = PlanetProgressiveKernel::EvaluateSphereValue(
						Radius, Plus2, Minus2, SquaredDistance, Value, bAdd);

					if (NewValue != Value)
					{
						Value = NewValue;
						// relaxed 就够：这里只要「最终可见」，不参与任何同步。
						Changed.store(true, std::memory_order_relaxed);
					}
				},
				/*bForceSingleThread=*/!bMultiThreaded);
		}
		else
		{
			// 贴边判定用整笔编辑的盒子，不是本块的盒子（见内核头文件）。
			const FIntVector BoxMin = FullBounds.Min;
			const FIntVector BoxMaxExclusive = FullBounds.MaxExclusive;

			WorldData.ParallelSet<FVoxelValue>(ChunkBounds,
				[BoxMin, BoxMaxExclusive, bAdd, &Changed](
					int32 X, int32 Y, int32 Z, FVoxelValue& Value)
				{
					const FVoxelValue NewValue = PlanetProgressiveKernel::EvaluateBoxValue(
						FIntVector(X, Y, Z), BoxMin, BoxMaxExclusive, Value, bAdd);

					if (NewValue != Value)
					{
						Value = NewValue;
						Changed.store(true, std::memory_order_relaxed);
					}
				},
				/*bForceSingleThread=*/!bMultiThreaded);
		}
	}

	LastChunkBounds = Chunk;
	AppliedBounds = PlanetEditBrush::UnionVoxelBox(AppliedBounds, Chunk);
}
