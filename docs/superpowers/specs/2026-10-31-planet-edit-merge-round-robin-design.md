# 挖掘狂点：同类型点击合并 + 队列轮转推进 — 设计

- **日期**：2026-10-31
- **项目**：StellarEra（Unreal Engine 5.8）
- **状态**：已实现（见 plans/2026-10-31-planet-edit-merge-round-robin.md）
- **前置**：`2026-10-29-progressive-dig-design.md`（分块跨帧挖掘、编辑队列）

---

## 1. 目标

用户原话：

> 挖掘如果在短时间内点击很多次，似乎不能并行？？如果要并行，可以吗？

明确后的目标（用户从选项里选的）：

1. **同类型、包围盒相交的连续点击合并成一笔** —— 同一片区域连点 N 下，不再把
   `(Size+6)³` 格扫 N 遍。
2. **队列从「只推队首」改成「队首优先 + 余量轮转」** —— 多笔同时活跃时每一笔都
   在推进，而不是一个个干等。

不做真·多线程并发写（理由见第 7 节）。

---

## 2. 现状：为什么看起来不并行

三处叠加，全都是**笔与笔之间**的串行，与笔内无关：

| 位置 | 机制 |
|---|---|
| `UPlanetEditComponent::TickProgressiveEdits` | `while` 循环只取 `EditQueue[0]`，一笔 `RemoveAt(0)` 之后才轮到下一笔 —— 严格 FIFO |
| 同上 | 每帧预算 `ProgressiveEditBudgetMs`（默认 4 ms）跨过就 `break`，队头一笔要吃很多帧 |
| `ApplyBrushAtScreenPosition` | 队列满 `MaxQueuedEdits`（16）时丢 `EditQueue[1]` |

**笔内本来就是并行的**：`FVoxelData::ParallelSet` 内部是 `ParallelFor` 按叶子切
（`VoxelData.inl:178`），`bMultiThreadedEdits` 只控制 `bForceSingleThread`。

**单笔慢的真正大头不是没并行**：球笔刷包围盒 `(Size+6)³`，1280 体素时 ≈ 2.1e9 格，
球体只占 52%。同一个位置连点 5 下 = 把这 2.1e9 格扫 5 遍。

---

## 3. 数据模型：一个 job 持有 op 列表

### 3.1 为什么不能只有一个球

`FPlanetProgressiveEdit` 现在只有单个 `CenterVoxels / RadiusVoxels / SquaredRadius±2`。
合并要求一个 job 能承载多次落笔。

```cpp
/** 一次落笔的完整参数。op 只追加、不修改。 */
struct FPlanetEditOp
{
    EPlanetBrushShape Shape;
    FVector CenterVoxels;        // 体素空间（double）
    float RadiusVoxels = 0.f;
    float SquaredRadiusPlus2 = 0.f;
    float SquaredRadiusMinus2 = 0.f;
    FPlanetEditVoxelBox Bounds;  // 本 op 自己的包围盒；方块形状的贴边判定用它
};

struct FPlanetEditChunk
{
    FPlanetEditVoxelBox Bounds;
    int32 NumOps = 0;            // 应用 Ops[0 .. NumOps-1]
};
```

**核心不变量：op 只追加、不修改，所以「下标前缀」永远稳定。** 一个块不需要知道
以后会追加什么，它只认自己创建时的那个前缀。

`Setup()` 的签名**不变**，内部构造第一个 op。`PlanetBrushApplicator` 一行都不用改。

### 3.2 逐格执行：一个块一次 ParallelSet

`ApplyChunk` 改成对块内每格**按顺序**应用 `Ops[0..NumOps-1]`：

```
一次 FVoxelWriteScopeLock(块范围)
  一次 ParallelSet(块范围, 逐格 {
      for i in [0, NumOps): Value = Op[i].Evaluate(X, Y, Z, Value)
  })
```

逐格按 op 顺序应用，**与依次执行 N 次独立编辑逐格完全相同** —— 因为每条 op 的
谓词只依赖「格子自己 + 当前值 + 自己的几何」，不依赖别的 op。

代价：op 数多时每格要多算几次 `SquaredDistanceTo`。所以有上限（见第 6 节）。

---

## 4. 合并：只给新区域切块，进度不丢

### 4.1 `TryMerge`

```cpp
/**
 * 把同一次「连续点击」的又一次落笔并进本 job。
 * 参数不兼容、范围不相交、op 数到上限时返回 false（调用方另起一笔）。
 */
bool TryMerge(
    AVoxelPlanetActor* InPlanet, const FVector& CenterWorld,
    EPlanetBrushShape InShape, int32 SizeVoxels, int32 MinSizeVoxels, int32 MaxSizeVoxels,
    bool bInPlace, int32 ChunkSizeVoxels, bool bMultiThreaded);
```

算法：

1. **参数校验**：同行星 / 同形状 / 同 `SizeVoxels` / 同 `bPlace` / 同 `bMultiThreaded` /
   `Ops.Num() < MaxMergedOps`。任一不符 → `false`。
2. 按 `Setup` 同一套公式算新 op 的包围盒；与 `FullBounds` **不相交 → `false`**
   （用户选定的合并判定：并集不会比两者之和膨胀）。
3. **去重**：新旧球心距离 < 0.5 体素且半径相同 → 直接返回 `true` 且不追加。
   挖取 `max`、填取 `min`，重复一次是同值，追加纯属浪费。
   这一条同时修掉「按住不动连点，坑不长但一直排任务」。
4. `Ops.Add(新 op)`；`FullBounds = UnionVoxelBox(FullBounds, 新包围盒)`。
5. 只把**新包围盒**交给 `SplitVoxelBoxIntoChunks` 切块，每块 `NumOps = Ops.Num()`。
6. `Chunks.Insert(新块, NextChunkIndex)` —— **插在已做前缀之后**。

### 4.2 第 6 步是支点

插在 `NextChunkIndex` 位置上：

- 已做前缀的下标一个都不动 → **进度完全保留**；
- 新点的那一下排在下一个被执行 → 视觉上「新的坑立刻开始长」；
- `AppliedBounds` 与 `GetLastChunkBounds` 的语义都不用改。

对比另外两种做法（已否决，记在这里备查）：

- **重切并集、进度归零**：代码最少，但 1280 体素一笔是 1331 块、队首每帧推一块要
  22 秒；每 200 ms 点一下就归零 → 这笔永远做不完。
- **只合未开工的笔**：大笔刷下一帧就开工，「短时间狂点」这个场景基本合不上。

### 4.3 重叠格会被写两次，但同值

新块覆盖整个新球区域；落在「老块 ∩ 新球」里的格子会被两个块各写一次。

安全，因为同一 job 内所有 op 的 `bPlace` 相同（合并的硬性前提），而挖是 `max`、
填是 `min`，两者都**幂等且可交换**：

```
先 A 后 B：max(max(v, a), b) = max(v, a, b)
先 B 后 A：max(max(v, b), a) = max(v, a, b)
```

这条不靠论证兜底，靠测试钉住（第 8 节 #3：合并结果 == 依次独立执行，逐格全等，
挖 / 填各跑一遍）。

**方块笔刷同样成立**：`EvaluateBoxValue` 的贴边判定按**各自那一笔的盒子**
（`Op.Bounds`），逐格按顺序应用，与依次执行两笔完全一致。

---

## 5. 调度：队首优先 + 余量轮转

`UPlanetEditComponent::TickProgressiveEdits` 拆成两段：

```
预算 = max(ProgressiveEditBudgetMs, 0.1)
队首份额 = 预算 × ProgressiveEditHeadBudgetShare

阶段一（队首）：
  while 队列非空 且 已用时 < 队首份额:
      推进队首一块；累积渲染范围
      做完 → OnEditFinished + RemoveAt(0)，新队首继续
      队首份额用完或预算用完 → 退出

阶段二（余量轮转）：
  while 队列 > 1 且 已用时 < 预算:
      按游标取 1..N-1 中的一笔推进一块；游标 +1 回绕
```

游标**跨帧保留**（`NextFairEditIndex` 成员）。两笔交替推进，不会有人饿死。
队列变短导致游标越界时夹到 `[1, N-1]`，不做取模 —— 取模在队列动态变化时会跳笔。

两条必须写死、否则会退化成「永远不动」或「某笔永远饿着」：

- **阶段一每帧至少推一块**：预算判定放在**推进之后**，与现有实现一致。
  队首份额再小，这一帧也要往前走一块。
- **阶段二每帧最多各推一块**：进循环前判总预算，不做「再补一块」的补偿。

`ProgressiveEditHeadBudgetShare` 默认 **0.7**，`ClampMin 0 / ClampMax 1`。

---

## 6. 边界与失败面

| 面 | 处理 |
|---|---|
| **跨行星轮转** | 现有 `AccumulateRenderBounds` 换星会**丢掉**旧星待刷范围。改成换星前先 `FlushPendingRenderUpdate()`，再清空、记新星 |
| **op 数膨胀** | `MaxMergedOps`（默认 8）：超了另起一笔，避免逐格要过几十个球 |
| **挖 / 填混排** | 不合并（`bPlace` 不同直接拒绝） |
| **笔刷尺寸中途改变** | 不合并（`SizeVoxels` 不同直接拒绝） |
| **队列已满** | 保留现有 `RemoveAt(1)`；有了合并，实际很难堆满 |
| **行星失效** | `ApplyNextChunk` 已有处理（判完成、退出），保持不变 |
| **块粒度 > 预算** | 128³ ≈ 2.9 ms > 队首份额 2.8 ms，所以队首实际每帧只推一块；阶段二再推一块 → 双笔时单帧最多约 5.8 ms。这是块粒度决定的，写进注释 |

---

## 7. 为什么不做真·多线程并发写

数据层其实**允许**：写锁是叶子级的（`FVoxelData::Lock` 对 `MainLock` 拿**读**锁，
`FVoxelDataOctreeLocker::LockImpl` 只在 `IsLeafOrHasNoChildren()` 的节点上加锁，
`VoxelData.cpp:183-207`），两支不相交的编辑锁不同叶子。

但收益不成比例：

- `ParallelSet` 内部已经在抢任务图线程，两个 `ParallelSet` 并发只是互相抢核；
- `UpdateWorld`（LOD 管理器）与 `NotifyVoxelEdited`（UObject、包脏标记）必须回游戏线程；
- 合并已经把「同一片区域连点」收成一笔，真正的重复扫描没了。

---

## 8. 验证

| # | 检查 | 怎么测 |
|---|---|---|
| 1 | 参数不兼容 / 不相交 / 超 op 上限 → 拒绝合并 | 单测 `StellarEra.Planet.Editor.Merge.Rejects` |
| 2 | 合并后 `NextChunkIndex`、`AppliedBounds` 不变，块数增加 | 单测 `Merge.PreservesProgress` |
| 3 | **合并结果 == 依次独立执行**，逐格全等（挖、填各一遍） | 单测 `Merge.ResultEqualsSequential`，真实体素数据 |
| 4 | 同位置重复点击不追加 op | 单测 `Merge.DedupesIdenticalOps` |
| 5 | 两笔同时活跃时都能推进 | 若能复用 `PlanetEditTests.cpp` 的世界夹具驱动组件 Tick 就自动化；否则手工 PIE 验证并在计划里注明 |
| 6 | 编译 | UBT 直调 dotnet，`Result: Succeeded` |
| 7 | 全量回归 | `Automation RunTests StellarEra`，除既有失败（`EarthPresetValues`）外全绿 |

---

## 9. 已知代价（明确接受）

1. **单帧上限变了**：多笔同时活跃时单帧最多「队首一块 + 轮转一块」≈ 5.8 ms，而不是
   原来的 4 ms。这是「多笔一起推进」的直接代价。
2. **轮转的笔各自变慢**：两笔活跃时每一笔的速度约为独占时的一半。用户选的就是
   「队首优先」，所以队首仍拿大头。
3. **合并的 job 每格要过多个 op**：合并后的每个块对覆盖到的每格依次评估 `NumOps`
   个 op，重叠区域的格子会被多评估 —— 8 笔重叠但不同的方块并成一笔时，合并后总评估量是 Σ k·B（8 笔同尺寸时为 36B），约为「8 笔分别执行」的 4.5 倍，并由 `MaxMergedOps` 封顶。只有包围盒
   **完全相同**时才真正免费（去重直接返回、不追加 op）。重叠但不同的盒子合并换到
   的是：新增落笔不占队列条目（满队列时不再挤掉别的笔），且新点的洞立刻开始长；
   代价就是这些额外的逐格评估。
4. **合并只在队列里发生**：小笔刷走同步路径，不参与合并（本来就快）。

---

## 10. 不做的事

- 不动 `Plugins/VoxelFree/`。
- 不做真·多线程并发写（见第 7 节）。
- 不改笔刷尺寸区间、不改存档时机、不做撤销 / 重做。
- 不合并**不相交**的点击（已否决「无条件合并」）。
