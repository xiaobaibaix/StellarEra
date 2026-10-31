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

void FPlanetProgressiveEdit::ResetState()
{
	Ops.Reset();
	Chunks.Reset();
	NextChunkIndex = 0;
	FullBounds = FPlanetEditVoxelBox();
	AppliedBounds = FPlanetEditVoxelBox();
	LastChunkBounds = FPlanetEditVoxelBox();
	bChangedAnything.store(false, std::memory_order_relaxed);
}

FPlanetEditOp FPlanetProgressiveEdit::MakeOp(const FPlanetEditRequest& Request)
{
	FPlanetEditOp Op;
	Op.Shape = Request.Shape;
	Op.CenterVoxels = Request.CenterVoxels;
	Op.RadiusVoxels = Request.RadiusVoxels;

	if (Request.Shape == EPlanetBrushShape::Sphere)
	{
		PlanetProgressiveKernel::GetSphereThresholds(
			Request.RadiusVoxels, Op.SquaredRadiusPlus2, Op.SquaredRadiusMinus2);
		Op.Bounds = PlanetEditBrush::MakeSphereEditVoxelBox(
			Request.CenterVoxels, Request.RadiusVoxels);
	}
	else
	{
		// 与 PlanetBrushApplicator 同一套：先按四舍五入的体素中心取两端都含的盒，
		// 再换成右端开。方块没有半径的概念，上面留的就是 0。
		Op.SquaredRadiusPlus2 = 0.f;
		Op.SquaredRadiusMinus2 = 0.f;
		const FPlanetEditBox Box = PlanetEditBrush::MakeBox(
			Request.CenterVoxels, Request.SizeVoxels,
			Request.MinSizeVoxels, Request.MaxSizeVoxels);
		Op.Bounds = PlanetEditBrush::MakeVoxelBox(Box);
	}

	return Op;
}

bool FPlanetProgressiveEdit::MakeRequest(
	AVoxelPlanetActor* const InPlanet,
	const FVector& CenterWorld,
	const EPlanetBrushShape InShape,
	const int32 InSizeVoxels,
	const int32 InMinSizeVoxels,
	const int32 InMaxSizeVoxels,
	const bool bInPlace,
	const int32 ChunkSizeVoxels,
	const bool bInMultiThreaded,
	FPlanetEditRequest& OutRequest)
{
	if (!IsValid(InPlanet) || !InPlanet->IsCreated())
	{
		return false;
	}

	// 世界 → 体素：与 UVoxelSphereTools 内部同一个函数（GetRealPosition 就是
	// VoxelWorld->GlobalToLocalFloat），不自己除 VoxelSize —— 那会漏掉缩放。
	const FVoxelVector RealPosition =
		FVoxelToolHelpers::GetRealPosition(InPlanet, CenterWorld, /*bConvertToVoxelSpace=*/true);

	OutRequest = FPlanetEditRequest();
	OutRequest.Shape = InShape;
	OutRequest.CenterVoxels = FVector(RealPosition.X, RealPosition.Y, RealPosition.Z);
	OutRequest.SizeVoxels = InSizeVoxels;
	OutRequest.MinSizeVoxels = InMinSizeVoxels;
	OutRequest.MaxSizeVoxels = InMaxSizeVoxels;
	OutRequest.bPlace = bInPlace;
	OutRequest.bMultiThreaded = bInMultiThreaded;
	OutRequest.ChunkSizeVoxels = ChunkSizeVoxels;

	if (InShape == EPlanetBrushShape::Sphere)
	{
		// 半径和包围盒都按插件那一套算：半径先夹到区间的半边长（厘米），
		// 再除 VoxelSize 换到体素。
		const float VoxelSize = FMath::Max(InPlanet->VoxelSize, 1.f);
		const float RadiusCm = PlanetEditBrush::GetHalfExtentCm(
			InSizeVoxels, VoxelSize, InMinSizeVoxels, InMaxSizeVoxels);
		OutRequest.RadiusVoxels = FVoxelToolHelpers::GetRealDistance(
			InPlanet, RadiusCm, /*bConvertToVoxelSpace=*/true);
	}
	else
	{
		OutRequest.RadiusVoxels = 0.f;
	}

	return true;
}

bool FPlanetProgressiveEdit::SetupFromRequest(const FPlanetEditRequest& Request)
{
	ResetState();
	Shape = Request.Shape;
	bPlace = Request.bPlace;
	bMultiThreaded = Request.bMultiThreaded;
	SizeVoxels = Request.SizeVoxels;

	const FPlanetEditOp Op = MakeOp(Request);
	if (!PlanetEditBrush::IsValidVoxelBox(Op.Bounds))
	{
		return false;
	}

	TArray<FPlanetEditVoxelBox> Boxes;
	PlanetEditBrush::SplitVoxelBoxIntoChunks(Op.Bounds, Request.ChunkSizeVoxels, Boxes);
	if (Boxes.Num() == 0)
	{
		return false;
	}

	Ops.Add(Op);
	FullBounds = Op.Bounds;

	Chunks.Reserve(Boxes.Num());
	for (const FPlanetEditVoxelBox& Box : Boxes)
	{
		FPlanetEditChunk& Chunk = Chunks.AddDefaulted_GetRef();
		Chunk.Bounds = Box;
		Chunk.NumOps = Ops.Num();  // 此刻只有一个 op
	}

	return true;
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

	FPlanetEditRequest Request;
	if (!MakeRequest(InPlanet, CenterWorld, InShape, InSizeVoxels, InMinSizeVoxels,
			InMaxSizeVoxels, bInPlace, ChunkSizeVoxels, bInMultiThreaded, Request))
	{
		ResetState();
		return false;
	}

	if (!SetupFromRequest(Request))
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

void FPlanetProgressiveEdit::ApplyChunk(const FPlanetEditChunk& Chunk)
{
	AVoxelPlanetActor* const Target = Planet.Get();
	if (!IsValid(Target))
	{
		return;
	}

	FVoxelData& WorldData = Target->GetData();
	const FVoxelIntBox ChunkBounds(Chunk.Bounds.Min, Chunk.Bounds.MaxExclusive);

	// 块只认自己创建时的前缀。夹一下只是防御：NumOps 由 TryMerge 在游戏线程写，
	// 不会超过 Ops.Num()。
	const int32 NumOps = FMath::Clamp(Chunk.NumOps, 0, Ops.Num());

	// 写锁只围住数据写入，渲染刷新在锁外 —— 与插件工具完全一致
	//（VoxelGeneratedTools.h 的 GENERATED_TOOL_CALL）。
	{
		FVoxelWriteScopeLock Lock(WorldData, ChunkBounds, ProgressiveEditLockName);

		std::atomic<bool>& Changed = bChangedAnything;
		const bool bAdd = bPlace;
		// 捕获裸指针 + 数量：lambda 跑在任务图线程上，不能碰可能被重分配的容器。
		// op 只在游戏线程的 TryMerge 里增长，与 ApplyChunk 不重叠。
		const FPlanetEditOp* const OpData = Ops.GetData();

		WorldData.ParallelSet<FVoxelValue>(ChunkBounds,
			[OpData, NumOps, bAdd, &Changed](int32 X, int32 Y, int32 Z, FVoxelValue& Value)
			{
				const FVoxelValue NewValue = PlanetProgressiveKernel::EvaluateOps(
					OpData, NumOps, X, Y, Z, Value, bAdd);

				if (NewValue != Value)
				{
					Value = NewValue;
					// relaxed 就够：这里只要「最终可见」，不参与任何同步。
					Changed.store(true, std::memory_order_relaxed);
				}
			},
			/*bForceSingleThread=*/!bMultiThreaded);
	}

	LastChunkBounds = Chunk.Bounds;
	AppliedBounds = PlanetEditBrush::UnionVoxelBox(AppliedBounds, Chunk.Bounds);
}

bool FPlanetProgressiveEdit::TryMerge(
	const FPlanetEditRequest& Request, const int32 MaxOps)
{
	// 没装配过、或已经做完：没有可并的目标。做完的 job 马上会被移出队列，
	// 往它后面追加块永远不会被执行。
	if (Ops.Num() == 0 || Chunks.Num() == 0 || IsFinished())
	{
		return false;
	}

	// 参数必须完全兼容。挖填混排、中途改笔刷尺寸都不合并 —— 挖与填在同一格上
	// 不是同一种操作，混起来「合并结果 == 依次执行」就不再成立。
	if (Request.Shape != Shape
		|| Request.SizeVoxels != SizeVoxels
		|| Request.bPlace != bPlace
		|| Request.bMultiThreaded != bMultiThreaded)
	{
		return false;
	}

	const FPlanetEditOp NewOp = MakeOp(Request);
	if (!PlanetEditBrush::IsValidVoxelBox(NewOp.Bounds))
	{
		return false;
	}

	// 只合并包围盒相交的落笔：否则整笔范围会膨胀成一片没挖过的大区域，
	// 每次刷新都要重建它。
	if (!PlanetEditBrush::VoxelBoxesIntersect(NewOp.Bounds, FullBounds))
	{
		return false;
	}

	// 去重放在 op 上限之前：同一片区域连点，不管点多少下都该被吸收，
	// 而不是在到上限之后突然开始排新任务。
	for (const FPlanetEditOp& Existing : Ops)
	{
		if (Existing.Bounds.Min == NewOp.Bounds.Min
			&& Existing.Bounds.MaxExclusive == NewOp.Bounds.MaxExclusive)
		{
			return true;
		}
	}

	if (Ops.Num() >= FMath::Max(MaxOps, 1))
	{
		return false;
	}

	// 只切新包围盒。老块的边界一个都不动 —— 这就是进度能保留的原因。
	TArray<FPlanetEditVoxelBox> NewBoxes;
	PlanetEditBrush::SplitVoxelBoxIntoChunks(
		NewOp.Bounds, Request.ChunkSizeVoxels, NewBoxes);
	if (NewBoxes.Num() == 0)
	{
		return false;
	}

	// 到这一步才动状态：上面任何一步失败都不能留下半个 op。
	Ops.Add(NewOp);
	FullBounds = PlanetEditBrush::UnionVoxelBox(FullBounds, NewOp.Bounds);

	TArray<FPlanetEditChunk> NewChunks;
	NewChunks.Reserve(NewBoxes.Num());
	for (const FPlanetEditVoxelBox& Box : NewBoxes)
	{
		FPlanetEditChunk& Chunk = NewChunks.AddDefaulted_GetRef();
		Chunk.Bounds = Box;
		Chunk.NumOps = Ops.Num();
	}

	// 插在已做前缀之后：做过的下标一个都不动，新点的那一下排在下一个被执行。
	Chunks.Insert(NewChunks.GetData(), NewChunks.Num(), NextChunkIndex);
	return true;
}

bool FPlanetProgressiveEdit::TryMergeWorld(
	AVoxelPlanetActor* const InPlanet,
	const FVector& CenterWorld,
	const EPlanetBrushShape InShape,
	const int32 InSizeVoxels,
	const int32 InMinSizeVoxels,
	const int32 InMaxSizeVoxels,
	const bool bInPlace,
	const int32 ChunkSizeVoxels,
	const bool bInMultiThreaded,
	const int32 MaxOps)
{
	// 换星了就不能合：体素空间不同，包围盒没有可比性。
	if (!IsValid(InPlanet) || InPlanet != Planet.Get())
	{
		return false;
	}

	FPlanetEditRequest Request;
	if (!MakeRequest(InPlanet, CenterWorld, InShape, InSizeVoxels, InMinSizeVoxels,
			InMaxSizeVoxels, bInPlace, ChunkSizeVoxels, bInMultiThreaded, Request))
	{
		return false;
	}

	return TryMerge(Request, MaxOps);
}
