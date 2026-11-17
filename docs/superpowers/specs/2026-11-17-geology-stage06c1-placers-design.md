# 地质阶段 6c-1：砂矿与滨海重砂 设计

> 上位规格：`2026-11-12-geology-pipeline/06-erosion.md`「撒矿」；矿物玩法规格：`minerals/alluvial-gold.md`。
> 阶段 6 拆三步：6a 水系与剥蚀（已完成）、6b 出露结算（已完成）、6c 沉积矿床。6c 再拆两步，本文是 **6c-1：砂矿与滨海重砂**；6c-2（蒸发岩 / 锂盐 / 煤）另行设计。

## 1. 目标与非目标

**目标**
- 新增撒矿规则 `FGeoPlacerRule` 与撒矿器 `UGeoPlacerScatterer`，按「上游有原生矿 + 沿流追踪」撒砂金、砂锡、砂铂、砂金刚石；`bMouthOnly` 模式撒滨海重砂。
- 阶段 6a 多导出一张 `FlowNext` 场（下游格编号），供规则顺流追踪。
- 矿物目录新增 5 条；配方里的撒矿器可增删、复制、改源矿种与全部参数。

**非目标**
- 不做蒸发岩 / 锂盐 / 煤（6c-2）。
- 不生成水体、不改高程、不改 6a/6b 的算法。
- 不要求源矿已出露（留参数可收紧）。
- 不引入矿物元素映射（砂金与岩金各自一个矿物编号，产出同元素的合并在更后面的玩法层）。

## 2. 关键结构决定：用撒矿器，不用新阶段

沉积矿是「矿物」，矿种必须由 `DA_MineralCatalog` 与配方撒矿器列表驱动；写成阶段就是把矿写死。因此：

- 6c-1 = **三个新类型**（一个规则类、一个撒矿器类、5 条目录数据），管线和阶段表都不动。
- 规则 `GetStage()` 返回 **7**：管线在本阶段 `Bake` 之后立即运行该阶段号的规则，即 **6b 出露结算之后**。这样沉积体落地即最终位置，不会被 6b 抬升或夹到地表。
- 同一阶段有多个撒矿器时按配方列表顺序执行；后面的规则能看到前面刚撒的矿（管线行为，已存在）。

## 3. 数据模型：导出 `FlowNext`

`FPlanetBakeFields` 追加（与 `Hydro` 组同生命周期，只活在烘焙期）：

```cpp
// 阶段 6 的水系拓扑：值 = 下游格编号 + 1，0 = 无下游（海洋 / 内流盆地出口 / 无流向）。
FGeoField2D FlowNext;
```

- 分辨率 = `FlowAccum.Res`（HydroRes）。格编号 `(Face·Res + Y)·Res + X`，与 `FlowAccum` 一致。
- 阶段 6a 在写完 `FlowAccum` 的同一个循环里写：`FlowNext.At(Face,X,Y) = float(Flow.Down[i] + 1)`。
- 格子编号最大 `6·512² = 1.57M < 2²⁴`，float 精确表示，不需要新类型。
- 不进快照、不进内容哈希、不进 `ExportDebugFields`（与 `FlowAccum` 等一致）。
- 规则取格子中心方向用 `FGeoField2D::TexelDir(Face,X,Y)`，不需要重建 `Stage06::FHydroGrid`。

## 4. 规则 `FGeoPlacerRule`

### 4.1 参数

| 字段 | 默认 | 含义 |
|---|---|---|
| `SourceMineralIds` | 空 | 源矿编号列表；空 = 不撒（「无源不砂矿」硬规则） |
| `MinSourceExposure` | 0 | 源矿最低出露比例（0–1）；0 = 只要求存在 |
| `MaxSteps` | 96 | 单条流路最多走多少宏观格 |
| `StepStride` | 2 | 每走几格放一片 |
| `MaxBodiesPerSource` | 24 | 单个源矿最多产出几片 |
| `MinFlowAccumRatio` | 1.0 | 沉积格最低汇流量 = 该倍数 × `Config.Hydro.RiverThreshold` |
| `HalfWidthRatio` | 0.0015 | 横跨河谷的半宽（占 R，随汇流开方放大） |
| `ThicknessVoxels` | 1.0 | 薄层厚度（体素）；规格要求 1 格 |
| `DecayPerVoxel` | 0.00008 | 品位沿流程指数衰减系数（1/体素） |
| `bMouthOnly` | false | 只在入海前最后 `MouthCells` 个陆上格沉积（滨海重砂） |
| `MouthCells` | 3 | `bMouthOnly` 时沉积的格数 |
| `Hard` | 0.3 | 薄层软边宽度（占半径） |

### 4.2 算法

记 `R = Config.Radius`，`Thr = Config.Hydro.RiverThreshold`，`Res = Fields.FlowAccum.Res`。

1. 校验：`Res > 0` 且 `Fields.FlowNext.Res == Res`，否则报错返回；`SourceMineralIds` 为空直接返回。
2. 遍历 `Fields.Primitives`（顺序固定，保证确定性）。源矿判据：`MineralId ∈ SourceMineralIds` 且 `Exposure/255 ≥ MinSourceExposure`。
3. 源矿方向 `Dir = Center.GetSafeNormal()` → 起始格 `Idx = CellIndex(Dir)`；`Cell = int(FlowNext(Idx)) − 1`；`Cell < 0` 跳过（源矿不在河上）。
4. 沿 `FlowNext` 往下游走，最多 `MaxSteps` 步。每格：
   - `DirCell = FlowAccum.TexelDir(Cell)`，`Elev = Elevation.Sample(DirCell)`，`Acc = FlowAccum(cell)`。
   - **有效沉积格** = `Elev > 0`（陆上）且 `Acc ≥ MinFlowAccumRatio·Thr`。
   - `WalkerDistance += R · 相邻格弦距`（用 `DirCell_prev` 与 `DirCell` 的夹角近似弧长）。
5. 普通模式：每累计 `StepStride` 个有效格放一片，直到 `MaxBodiesPerSource`。
   `bMouthOnly`：用长度为 `MouthCells` 的环形缓冲记住最近的有效陆上格；当下一格是海洋（`Elev ≤ 0`）或走到头时，一次性在缓冲里的格子各放一片，然后停止。
6. 单片形态：中心 `Center = DirCell · (R + Elev − 0.5·Thickness)`；局部坐标系 `TangentFrame(DirCell, 0)` 后把 X 轴转到流向（`DirNext − DirCell` 在切平面上的投影；退化时用任意切向）；半径 `(沿流半长, 横跨半宽, 0.5·Thickness)`。沿流半长 = `0.75 × StepStride × 宏观格弧长`，横跨半宽 = `clamp(HalfWidthRatio·R·√(Acc/Thr), 1, 4 × 宏观格弧长)`，厚半轴 = `max(0.5·Thickness, 0.5)`。
7. 品位：`Grade = clamp(round(GradeMax · exp(−DecayPerVoxel·WalkerDistance)), GradeMin, GradeMax)`；`GradeMin/GradeMax` 是撒矿器公共字段，本规则不另设品位参数。
8. 结束条件：`Cell = −1`、`MaxSteps` 用尽、或 `MaxBodiesPerSource` 到顶。

**无源性保证**：源矿列表为空或没有任何匹配原语 → 零输出；规则不按场随机撒，只从已有矿体出发。

### 4.3 确定性

不用随机数：源矿遍历顺序固定、流向由场决定、形态由公式决定。`HashParams()` 把全部参数与 `SourceMineralIds` 排序后并入哈希（列表顺序不影响语义）。

## 5. 撒矿器 `UGeoPlacerScatterer`

- 参数与 §4.1 一一对应，全部 `EditAnywhere` 带中文注释。
- `Sources` 属性类型 `TArray<FName>`，下拉来自配方矿物目录（`GetMineralOptions`）。
- `MakeRule()` 通过 `GetOuter()` 拿配方与矿物目录，把 `FName` 解析成编号填入 `SourceMineralIds`；目录里找不到的名字跳过（不中断烘焙）。
- 默认值：
  | 撒矿器实例 | 矿物 | 源矿 |
  |---|---|---|
  | 砂金 | `AuPlacer` | `EpithermalAuAg`、`PorphyryCu` |
  | 砂锡 | `SnPlacer` | `TinTungsten` |
  | 砂铂 | `PtPlacer` | `ChromiumPGE` |
  | 砂金刚石 | `DiamondPlacer` | `Diamond` |
  | 滨海重砂 | `HeavySand` | 上述四种源矿全部，`bMouthOnly = true` |

## 6. 矿物目录新增

`DA_MineralCatalog` 追加 5 条（编号 = 下标 + 1，追加在末尾，不动已有 13 条的相对顺序）：

| Id | 显示名 | 调试色 |
|---|---|---|
| `AuPlacer` | 砂金 | 金黄 |
| `SnPlacer` | 砂锡 | 灰白 |
| `PtPlacer` | 砂铂 | 银灰 |
| `DiamondPlacer` | 砂金刚石 | 冰蓝 |
| `HeavySand` | 滨海重砂 | 深褐 |

配方 `DA_GeoRecipe_Archipelago` 追加 5 个撒矿器（全部 `bEnabled = true`，`GradeMin/GradeMax` 取默认）。

## 7. 调试图层

不新增。沉积体是普通原语，现有的「矿物」「地表矿化」图层按 `MineralId` 着色即可看到；查看器与导出无需改动。

## 8. 测试（`Src/Tests/PlanetGeoStage06cTests.cpp`）

**合成场单元测试**（手工构造 `FPlanetBakeFields`：Res=8，一条已知链的 `FlowNext`、`FlowAccum`、`Elevation`，一个源矿原语）：

1. `Stage06c.NoSource`：源矿列表为空 → 0 片；列表里有但场上没有该矿 → 0 片。
2. `Stage06c.FollowFlow`：非嘴模式 → 产出片数 > 0，且每片中心所在格在链上、陆上、`FlowAccum ≥ 阈值`。
3. `Stage06c.GradeDecay`：沿链相邻两片的 `Grade` 单调不增；首片接近 `GradeMax`，末片更低（但不低于 `GradeMin`）。
4. `Stage06c.Mouth`：链末端是海洋 → `bMouthOnly` 只在最后 `MouthCells` 个陆上格产出，且片数 = `MouthCells`（测试链全部为有效沉积格）；非嘴模式片数明显更多。
5. `Stage06c.Shape`：每片 `Radii.Z ≤ ThicknessVoxels/2 + 0.01`（1 格砂层）；`Kind == OrientedBlob`。
6. `Stage06c.Exposure`：`MinSourceExposure = 0.5` 时，`Exposure = 0` 的源不产出。

**整管线集成**：

7. `Stage06c.Pipeline`：默认配方 + 四个砂矿撒矿器 + 滨海重砂烘焙；打印每种矿的片数；断言砂金片数 > 0、每片中心所在格在陆上且 `FlowAccum ≥ RiverThreshold`；断言规则阶段号为 7、默认管线里有阶段 7（沉积体不会被 6b 抬升）。
8. `Stage06c.Determinism`：同配方两次烘焙 `Primitives.Hash()` 一致；单线程/并行一致。
9. `Stage06c.Recipe`：源矿名解析（目录有名 → 编号；目录无名 → 不崩且该名不产矿）；每个标量参数各做一次哈希探针（改一个参数哈希必变）。

## 9. 风险

- **`FlowNext` 未导出时**：规则报「需要阶段 6 的 FlowNext」并返回，不静默产 0。
- **源矿在海底或不在河上**：起始格 `Down = −1` 或海洋，跳过；统计打印跳过数。
- **多源汇流处品位跳升**：不同流路的 `WalkerDistance` 起点不同，汇流后仍按各自距离衰减，局部可能上跳；规格允许沉积点局部跳升，验收按单条流路判。
- **薄层视觉**：1 体素薄层在远处只体现在材质上（与上位规格一致），几何只保证在地表贴一层。
- **同一矿种叠两份**：本规则不用随机，重复叠加会产生重合的片，靠 `bEnabled` 控制用量。
- **资产未提交**：`DA_MineralCatalog` / `DA_GeoRecipe_Archipelago` 当前仍在工作区，本次改动追加在同一文件上；是否提交由用户决定。
