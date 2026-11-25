// 一笔有进度的编辑：整个包围盒切成块，调用方每帧只推进几块。

#pragma once

#include <atomic>

#include "CoreMinimal.h"
#include "PlanetEditBrush.h"

class AVoxelPlanetActor;

/**
 * 一次落笔的完整参数。op 只追加、不修改：执行块只认创建时的前缀（FPlanetEditChunk::NumOps）。
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

/** 一次落笔的请求：纯体素数据。生产路径先经行星做世界→体素换算，合并判定只跟体素空间有关；拆开后合并能在无头测试里直接驱动。 */
struct FPlanetEditRequest
{
	EPlanetBrushShape Shape = EPlanetBrushShape::Sphere;
	FVector CenterVoxels = FVector::ZeroVector;
	/** 只有球用；方块填 0。 */
	float RadiusVoxels = 0.f;
	int32 SizeVoxels = 1;
	int32 MinSizeVoxels = 1;
	int32 MaxSizeVoxels = 1;
	bool bPlace = false;
	bool bMultiThreaded = true;
	int32 ChunkSizeVoxels = 128;
};

/** 编辑队列的推进策略。放在这里而非组件，是因为组件是 UObject：纯函数才能无头测试；
 *  规则只有一条：0 号队首由「队首优先」独占，轮转只碰 1..N-1。 */
namespace PlanetEditSchedule
{
	/** 除队首外可轮转的笔数。队列 ≤ 1 时为 0。 */
	STELLARERAPLANET_API int32 GetFairCandidateCount(int32 QueueSize);

	/** 下一个轮转下标：夹到 [1, QueueSize-1] 并回绕；无候选返回 INDEX_NONE。越界只夹不取模 ——
	 *  队列长度逐帧变，「Cursor % N」会让游标指到另一笔身上像跳笔。 */
	STELLARERAPLANET_API int32 GetNextFairIndex(int32 Cursor, int32 QueueSize);
}

/** 一笔渐进式编辑。体素工具代价是包围盒 Size³，1280 体素挖掘要扫 2×10⁹ 格，一口气做完即数秒冻结；
 *  切成块每帧推进几块，帧时间有上限。
 *  逐格语义与插件完全相同（谓词照抄 SphereEdit / BoxEdit），分块只改变「谁在何时调 ParallelSet」，
 *  前提是块不重叠且并集等于整盒（SplitVoxelBoxIntoChunks 保证）。
 *  不碰渲染：只改体素数据，块写完由调用方决定怎么刷。生命周期：调用方独占持有，非 UObject。 */
class STELLARERAPLANET_API FPlanetProgressiveEdit
{
public:
	FPlanetProgressiveEdit() = default;

	/** 世界空间入口：换算成体素后交给 SetupFromRequest。行星无效 / 未创建、包围盒退化时返回 false 且状态清零。 */
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

	/** 纯几何装配：建第一个 op 并切块。公开只为自动化测试能在没有体素世界时构造 job；生产路径一律走 Setup()。 */
	bool SetupFromRequest(const FPlanetEditRequest& Request);

	/** 纯策略合并：把一个请求并进本 job。包围盒不相交、参数不兼容、op 数到 MaxOps → false；
	 *  完全重复的落笔 → true 且不追加（挖取 max、填取 min）。进度不丢靠插入位置：新块插在
	 *  NextChunkIndex 上，已做前缀不动。 */
	bool TryMerge(const FPlanetEditRequest& Request, int32 MaxOps);

	/** 世界空间版：换算成体素后交给 TryMerge。行星不是本 job 的、或本 job 已做完 → false。 */
	bool TryMergeWorld(
		AVoxelPlanetActor* InPlanet,
		const FVector& CenterWorld,
		EPlanetBrushShape InShape,
		int32 InSizeVoxels,
		int32 InMinSizeVoxels,
		int32 InMaxSizeVoxels,
		bool bInPlace,
		int32 ChunkSizeVoxels,
		bool bInMultiThreaded,
		int32 MaxOps);

	/** 应用下一块。true = 还有剩余块；false = 已完成（或行星已失效）。每块内部只锁该块，锁持有时间与块大小成正比。 */
	bool ApplyNextChunk();

	/** 一次跑完剩余块，返回是否真的改到了体素。存档 / 退出前的安全点用。 */
	bool ApplyAll();

	bool IsFinished() const { return NextChunkIndex >= Chunks.Num(); }
	int32 GetChunkCount() const { return Chunks.Num(); }
	int32 GetNextChunkIndex() const { return NextChunkIndex; }
	int32 GetOpCount() const { return Ops.Num(); }

	/** 已合并的落笔与切块。块的 NumOps 是创建时的稳定前缀。测试与诊断用。 */
	const TArray<FPlanetEditOp>& GetOps() const { return Ops; }
	const TArray<FPlanetEditChunk>& GetChunks() const { return Chunks; }

#if WITH_DEV_AUTOMATION_TESTS
	/** 测试专用：把进度游标设到指定块。生产路径推不动进度（ApplyNextChunk 无行星时直接判完成），没法钉「新块插在已做前缀之后」。 */
	void SetNextChunkIndexForTest(const int32 Index)
	{
		NextChunkIndex = FMath::Clamp(Index, 0, Chunks.Num());
	}
#endif

	/** 笔刷尺寸（体素数）。日志用。 */
	int32 GetSizeVoxels() const { return SizeVoxels; }

	/** 有没有任何一格的值真的变了（写入谓词逐格比对旧值，线程安全）。比「中心格前后是否相同」准：大笔刷挖的常是外圈。 */
	bool DidChangeAnything() const { return bChangedAnything.load(std::memory_order_relaxed); }

	/** 已应用的块的并集（体素空间，右端开区间）。无效 = 一块都没做。 */
	const FPlanetEditVoxelBox& GetAppliedBounds() const { return AppliedBounds; }

	/** 最近一次 ApplyNextChunk 真正写的范围。逐块刷渲染用。 */
	const FPlanetEditVoxelBox& GetLastChunkBounds() const { return LastChunkBounds; }

	/** 整笔编辑的完整包围盒（所有 op 的并集）。 */
	const FPlanetEditVoxelBox& GetFullBounds() const { return FullBounds; }

	TWeakObjectPtr<AVoxelPlanetActor> GetPlanet() const { return Planet; }

private:
	/** 世界 → 体素，与插件同一套换算。Setup 与（后续的）TryMergeWorld 共用。 */
	static bool MakeRequest(
		AVoxelPlanetActor* InPlanet,
		const FVector& CenterWorld,
		EPlanetBrushShape InShape,
		int32 SizeVoxels,
		int32 MinSizeVoxels,
		int32 MaxSizeVoxels,
		bool bInPlace,
		int32 ChunkSizeVoxels,
		bool bMultiThreaded,
		FPlanetEditRequest& OutRequest);

	/** 请求 → op（包围盒 + 球阈值）。纯函数。 */
	static FPlanetEditOp MakeOp(const FPlanetEditRequest& Request);

	/** 清空一切状态（不含 Planet）。失败路径与装配开头共用。 */
	void ResetState();

	/** 开写锁 + ParallelSet，写一块。 */
	void ApplyChunk(const FPlanetEditChunk& Chunk);

	TWeakObjectPtr<AVoxelPlanetActor> Planet;

	EPlanetBrushShape Shape = EPlanetBrushShape::Sphere;
	bool bPlace = false;
	bool bMultiThreaded = true;
	int32 SizeVoxels = 1;

	/** op 只追加、不修改 —— 块的前缀因此永远稳定。 */
	TArray<FPlanetEditOp> Ops;
	TArray<FPlanetEditChunk> Chunks;
	int32 NextChunkIndex = 0;

	FPlanetEditVoxelBox FullBounds;
	FPlanetEditVoxelBox AppliedBounds;
	FPlanetEditVoxelBox LastChunkBounds;

	/** 多线程写入，只能用原子量。FThreadSafeBool 在 5.8 已标记弃用。 */
	std::atomic<bool> bChangedAnything{ false };
};
