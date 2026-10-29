# 分块跨帧挖掘 — 设计

- **日期**：2026-10-29
- **项目**：StellarEra（Unreal Engine 5.8）
- **状态**：设计已确认，待实现
- **前置**：`2026-10-28-starmap-continuous-dig-design.md`（星图连续挖、笔刷范围可调）

---

## 1. 目标

用户原话：

> 我想要做到即使的挖掘效果，没有卡的感觉，有什么办法可以做到吗？

明确后的目标：**落笔不产生可感知的帧停顿**，且屏幕上连续看到洞在长。选定方案
（用户从四个选项里选的）：**分块跨帧**。

---

## 2. 现状：代价从哪来

体素工具的包围盒是 `(2R + 6)³` 格（`FVoxelSphereToolsImpl::GetBounds`：
`FVoxelIntBox(Position - Radius - 3, Position + Radius + 3)`）。整块包围盒内每个
32³ 叶子都要被物化，物化时逐体素调生成器（`InitForEdit → GetValues`，这条路径
没有区间预判）。

已实测的单格代价（`StellarEra.Temp.EarlyOutBench`，临时 benchmark，已删除）：

| 路径 | 耗时 |
|---|---|
| 地表带（走满 Perlin + 地质场） | 1075.6 ns/次 |
| 深处 / 高空（密度场早退） | 21.8 / 22.6 ns/次 |

由此**推算**（未端到端实测）的落笔耗时：小笔刷 ≈ 37 ms，中 ≈ 0.34 s，最大档
（1280 体素）≈ 4 s。最大档的**硬地板**：`2.13e9 格 × 22 ns ÷ 16 线程 ≈ 2.9 s`，
继续优化生成器也拿不掉。

结论：单帧内做完一笔编辑这件事本身没有解。要"不卡"只能**把工作摊到多帧**。

---

## 3. 设计

### 3.1 执行单元 `FPlanetProgressiveEdit`

新文件 `Source/StellarEraPlanetEdit/{Public,Private}/PlanetProgressiveEdit.*`。
它保存一笔编辑的**全部参数 + 进度**：

- 目标行星（`TWeakObjectPtr<AVoxelPlanetActor>`）
- **体素空间**的中心与半径（由 `FVoxelToolHelpers::GetRealPosition/GetRealDistance`
  换算，与插件工具同一个函数）
- 形状（球 / 方）、挖 / 放
- 完整包围盒（体素空间，右端开区间）
- 预切好的块列表，**按离中心由近到远排序**
- 下一块下标、已应用块数

三个入口：

| 函数 | 作用 |
|---|---|
| `Setup(...)` | 算包围盒、切块、排序；返回是否有效 |
| `ApplyChunk()` | 应用下一块，返回是否还有剩余 |
| `ApplyAll()` | 一次跑完全部剩余块（存档前的安全点用） |

### 3.2 切块与每帧预算

块 = 叶子对齐的 **128³ 体素**（4×4×4 个 32³ 叶子），三轴都与 32 对齐。

- 为什么不切到 32³：`FVoxelData::ParallelSet` 每调用一次都要重新遍历八叉树、
  对空父节点 `CreateChildren`，任务开销会吃掉收益。
- 128³ ≈ 2.1e6 格 ≈ **2.9 ms**（16 线程、早退路径 22 ns/格）。
- 块不重叠、并集恰好等于包围盒（单测钉住）。
- 小块笔刷（半径 < 64 体素）切出来就是 1 块，行为与现在**完全相同**。

每帧预算：组件属性 `ProgressiveEditBudgetMs`，默认 4。Tick 里连做块直到累计
超过预算。

### 3.3 逐块刷渲染：坑从中心长大

每应用完一块，把该块范围并进 `UPlanetEditComponent::PendingRenderBounds`
（该机制已存在），并在 Tick 末尾刷一次 —— `LODManager.UpdateBounds`
是异步的，游戏线程代价很小。

块按离中心距离排序，因此视觉上洞从光标处向外扩。

### 3.4 队列与安全点

`TArray<TSharedPtr<FPlanetProgressiveEdit>> EditQueue`，上限
`MaxQueuedEdits`（默认 16），超限丢**最旧的**。只有队首推进。

三个必须"先干完再说"的地方：

1. `SetEditMode(false)` —— 退出编辑模式
2. `EndPlay` —— 角色 / Pawn 销毁
3. 换星（`SetGridPlanet` 收到不同的行星）

顺序固定为：`ApplyAll` 清空队列 → 刷渲染 → 再 `RequestSaveEdits()`。
不这么做会存下半成品，而且写锁会在半持有状态下撞进存档路径。

### 3.5 一套谓词，两个驱动

`PlanetBrushApplicator::Apply` 的**签名不变**，内部改成「建一个单块 job 并
`ApplyAll`」。组件走队列 + 分块。这样球 / 方的逐格语义只有**一份**实现。

`Apply` 今天返回的那些值（`OutEditedBounds`、是否真的改到体素）语义保持不变：
- `OutEditedBounds`：job 的完整包围盒（工具宏本来也是无条件写请求包围盒）
- 是否真改到：job 完成后比较中心格的体素值（现有的大笔刷路径就是这么判的）

### 3.6 逐格语义（照抄插件，不许自己发明）

球（`VoxelSphereToolsImpl.inl` 的 `SphereEdit<false>`）：

```
Plus2  = (Radius + 2)²
Minus2 = max(Radius - 2, 0)²
对包围盒内每一格 (X,Y,Z)：
  SqDist = |(X,Y,Z) - Center|²
  SqDist >  Plus2  → 跳过（不改）
  SqDist <= Minus2 → 挖：Empty()；填：Full()
  否则            → Distance = sqrt(SqDist)
                     NewValue = FVoxelValue{ Clamp(Radius - Distance, -2, 2) / 2
                                             × (填 ? -1 : 1) }
                     挖：Value = Max(旧值, NewValue)   （MergeAsset(…, true)）
                     填：Value = Min(旧值, NewValue)   （MergeAsset(…, false)）
```

包围盒：`FloorToInt(Center - R - 3) … CeilToInt(Center + R + 3) + 1`（`FVoxelIntBox`
从 `FVoxelVector` 构造时的取整就是这个）。

方块（`VoxelBoxToolsImpl::SetValueBox`）：包围盒内每格直接赋 `Empty()` / `Full()`，
**没有** ±3 余量 —— 包围盒就是 `PlanetEditBrush::MakeVoxelBox` 的结果。

写锁按块持有：`FVoxelWriteScopeLock Lock(WorldData, ChunkBounds, FName)`
（`VoxelData/VoxelDataLock.h`），然后 `WorldData.ParallelSet<FVoxelValue>(ChunkBounds, Predicate)`。

---

## 4. 对外接口

`UPlanetEditComponent` 新增三个 `UPROPERTY(EditAnywhere, Category = "PlanetEdit")`：

| 属性 | 默认 | 含义 |
|---|---|---|
| `ProgressiveEditChunkSizeVoxels` | 128 | 块边长（体素），内部按 32 对齐 |
| `ProgressiveEditBudgetMs` | 4.0 | 每帧最多花多少毫秒推进编辑 |
| `MaxQueuedEdits` | 16 | 待处理编辑队列上限，超了丢最旧的 |

`PlanetEditBrush` 新增两个**纯函数**（可无头测试）：

| 函数 | 作用 |
|---|---|
| `MakeSphereEditVoxelBox(CenterVoxels, RadiusVoxels)` | 与 `FVoxelSphereToolsImpl::GetBounds` 同结果的包围盒 |
| `SplitVoxelBoxIntoChunks(Box, ChunkSizeVoxels)` | 切成对齐块，按盒子中心由近到远排序 |

---

## 5. 验证

| # | 检查 | 怎么测 |
|---|---|---|
| 1 | 切块无重叠、并集 == 包围盒、全部 32 对齐 | 纯函数单测（`StellarEra.Planet.Edit.ProgressiveChunks`） |
| 2 | 包围盒与插件 `GetBounds` 逐分量相同 | 单测里直接调插件的 `FVoxelSphereToolsImpl::GetBounds` 比对，**不写死期望数字** |
| 3 | 球内核公式与 `SphereEdit<false>` 一致 | 取代表点逐格比对（含 ±2 边界内外） |
| 4 | 小块笔刷退化为单块 | 单测断言块数 == 1 |
| 5 | 编译通过 | UBT 直调 dotnet，`Result: Succeeded` |
| 6 | 全量自动化测试 | `Automation RunTests StellarEra`，除既有失败外全绿 |
| 7 | 手感 | PIE 里按档位落笔，日志输出「笔刷尺寸 / 块数 / 总耗时 / 最长单帧」 |

---

## 6. 已知代价（明确接受，不藏）

1. **总机器时间不变**。大笔刷仍是 2.9 s 地板，只是从「一次冻 4 秒」变成
   「4 ms/帧摊开十几秒」。最大档会从"卡一下"变成"看着它长"。
2. **游戏线程仍在做计算**，只是每帧有上限。16 线程并行时其他游戏线程工作
   仍会被抢走一部分 —— 预算调大就是拿帧率换等待时间。
3. **写锁按块持有**。每块 ~3 ms 期间，其他读该区域的线程（渲染 chunk 重建、
   笔刷预览射线）会等这几毫秒。相比现在的一次 4 秒，这是净改善。
4. **队列被丢的编辑不会生效**。连续挖 + 超大笔刷时会出现"点了但没挖"。

---

## 7. 不做的事

- 不动 `Plugins/VoxelFree/`：只用它的公开 API（`FVoxelData::ParallelSet`、
  `FVoxelWriteScopeLock`、`FVoxelToolHelpers::GetRealPosition/GetRealDistance`、
  `FVoxelSphereToolsImpl::GetBounds`）。
- 不做后台线程版本。分块跨帧先在游戏线程上摊开，若手感不够再评估。
- 不做粗 LOD 编辑、不改笔刷上限、不改存档时机。
- 不做撤销 / 重做。
