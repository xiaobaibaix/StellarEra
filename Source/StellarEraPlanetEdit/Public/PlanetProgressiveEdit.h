// 一笔有进度的编辑：整个包围盒切成块，调用方每帧只推进几块。

#pragma once

#include <atomic>

#include "CoreMinimal.h"
#include "PlanetEditBrush.h"

class AVoxelPlanetActor;

/**
 * 一次落笔的完整参数。
 *
 * **op 只追加、不修改**：一个执行块只认自己创建时的那个前缀（见
 * FPlanetEditChunk::NumOps），靠的就是这条不变式。
 */
struct FPlanetEditOp
{
	EPlanetBrushShape Shape = EPlanetBrushShape::Sphere;
	/** 体素空间（double）。球是球心；方块是体素中心。 */
	FVector CenterVoxels = FVector::ZeroVector;
	float RadiusVoxels = 0.f;
	float SquaredRadiusPlus2 = 0.f;
	float SquaredRadiusMinus2 = 0.f;
	/** 本 op 自己的包围盒（右端开）。方块的贴边判定用的就是它。 */
	FPlanetEditVoxelBox Bounds;
};

/** 一个执行块：只应用 Ops[0 .. NumOps-1]。 */
struct FPlanetEditChunk
{
	FPlanetEditVoxelBox Bounds;
	int32 NumOps = 0;
};

/**
 * 一笔渐进式编辑。
 *
 * 存在理由：体素工具的代价是包围盒 Size³，一次 1280 体素的挖掘要扫 2×10⁹ 格，
 * 全在游戏线程上一口气做完就是几秒的冻结。本类把同一笔编辑切成块，
 * 由调用方每帧只推进几块，帧时间因此有上限。
 *
 * **逐格语义与插件完全相同**：谓词照抄 SphereEdit / BoxEdit（见私有头
 * PlanetProgressiveEditKernel.h），分块只改变「谁在什么时候调 ParallelSet」，
 * 不改变任何一格写进去的值 —— 前提是块不重叠且并集等于整盒
 *（PlanetEditBrush::SplitVoxelBoxIntoChunks 保证，单测钉住）。
 *
 * **不碰渲染**：本类只改体素数据。块写完后由调用方决定怎么刷
 *（GetLastChunkBounds 给出刚写完的那一块；整笔的完整范围是 GetFullBounds）。
 * 这正是「逐帧看见洞从光标处长大」能实现的原因 —— 渲染刷新必须留在调用方。
 *
 * 生命周期：Setup 成功后由调用方独占持有（组件用 TSharedPtr 排队），
 * 非 UObject，用完即丢。
 */
class STELLARERAPLANETEDIT_API FPlanetProgressiveEdit
{
public:
	FPlanetProgressiveEdit() = default;

	/**
	 * 算包围盒、切块、记下球的两个阈值。行星无效 / 未创建、盒子无效时返回 false。
	 *
	 * 世界 → 体素的换算走插件的 FVoxelToolHelpers（与 UVoxelSphereTools 内部
	 * 同一个函数），不自己除 VoxelSize —— 那会漏掉 VoxelWorld 的缩放。
	 */
	bool Setup(
		AVoxelPlanetActor* InPlanet,
		const FVector& CenterWorld,
		EPlanetBrushShape InShape,
		int32 SizeVoxels,
		int32 MinSizeVoxels,
		int32 MaxSizeVoxels,
		bool bInPlace,
		int32 ChunkSizeVoxels,
		bool bMultiThreaded);

	/**
	 * 应用下一块。返回 true = 还有剩余块；false = 已完成（或行星已失效）。
	 * 每块内部开一个只覆盖该块的写锁，锁的持有时间因此与块大小成正比，
	 * 而不是与整笔编辑成正比。
	 */
	bool ApplyNextChunk();

	/** 一次跑完剩余块，返回是否真的改到了体素。存档 / 退出前的安全点用。 */
	bool ApplyAll();

	bool IsFinished() const { return NextChunkIndex >= Chunks.Num(); }
	int32 GetChunkCount() const { return Chunks.Num(); }

	/** 笔刷尺寸（体素数）。日志用。 */
	int32 GetSizeVoxels() const { return SizeVoxels; }

	/**
	 * 有没有任何一格的值真的变了。
	 *
	 * 由写入谓词逐格比对旧值得到（线程安全）。比「中心格前后是否相同」准：
	 * 大笔刷挖到的常常是外圈而中心格本来就已经是空气。
	 */
	bool DidChangeAnything() const { return bChangedAnything.load(std::memory_order_relaxed); }

	/** 已应用的块的并集（体素空间，右端开区间）。无效 = 一块都没做。 */
	const FPlanetEditVoxelBox& GetAppliedBounds() const { return AppliedBounds; }

	/** 最近一次 ApplyNextChunk 真正写的范围。逐块刷渲染用。 */
	const FPlanetEditVoxelBox& GetLastChunkBounds() const { return LastChunkBounds; }

	/** 整笔编辑的完整包围盒。 */
	const FPlanetEditVoxelBox& GetFullBounds() const { return FullBounds; }

	TWeakObjectPtr<AVoxelPlanetActor> GetPlanet() const { return Planet; }

private:
	/** 开写锁 + ParallelSet，写一块。 */
	void ApplyChunk(const FPlanetEditVoxelBox& Chunk);

	TWeakObjectPtr<AVoxelPlanetActor> Planet;

	EPlanetBrushShape Shape = EPlanetBrushShape::Sphere;
	bool bPlace = false;
	bool bMultiThreaded = true;

	/** 体素空间的球心（double）与半径（float，与插件同一个精度）。 */
	FVector CenterVoxels = FVector::ZeroVector;
	float RadiusVoxels = 0.f;
	float SquaredRadiusPlus2 = 0.f;
	float SquaredRadiusMinus2 = 0.f;

	int32 SizeVoxels = 1;

	FPlanetEditVoxelBox FullBounds;
	FPlanetEditVoxelBox AppliedBounds;
	FPlanetEditVoxelBox LastChunkBounds;

	TArray<FPlanetEditVoxelBox> Chunks;
	int32 NextChunkIndex = 0;

	/** 多线程写入，只能用原子量。FThreadSafeBool 在 5.8 已标记弃用。 */
	std::atomic<bool> bChangedAnything{ false };
};
