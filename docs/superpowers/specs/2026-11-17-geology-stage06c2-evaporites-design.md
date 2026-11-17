# 地质阶段 6c-2：蒸发岩 / 锂盐 / 煤 设计

> 上位规格：`2026-11-12-geology-pipeline/06-erosion.md`「撒矿」。6c 拆两步：6c-1 砂矿与滨海重砂（已完成），本文是 **6c-2：蒸发岩 / 锂盐 / 煤**。
> 结构沿用 6c-1：不新增阶段，全部做成撒矿器（规则挂阶段号 7 = 6b 之后）。

## 1. 目标与非目标

**目标**
- 6a 多导出一张 `BasinId`（洼地编号），供盆地沉积规则聚合盆地。
- 新增 `FGeoBasinDepositRule`：内流盆地按归一化半径环带撒蒸发岩，可选要求附近有火山（锂盐）。
- 新增 `FGeoCoalRule`：湿热、低平、近海的陆地上撒水平薄煤层。
- 新增 6 条矿物、6 个撒矿器实例；参数全在资产里可调、可删。

**非目标**
- 不做水体、不改高程、不改 6a/6b/6c-1 的算法。
- 不做盐湖卤水（液态），只做固态盐层。
- 不引入元素映射。

## 2. 数据模型：导出 `BasinId`

`FPlanetBakeFields` 追加（`Hydro` 组，只活在烘焙期）：

```cpp
// 洼地编号：-1 = 不在洼地，同一洼地的格子共用。阶段 6c-2 按它聚合盆地。
FGeoField2D BasinId;
```

- 分辨率 = `FlowAccum.Res`；值直接取 6a 的 `Flow.BasinId[i]`（float 精确表示到 2²⁴，洼地数远小于它）。
- 不进快照、不进内容哈希、不进 `ExportDebugFields`。

## 3. 规则 `FGeoBasinDepositRule`

### 3.1 参数

| 字段 | 默认 | 含义 |
|---|---|---|
| `MaxPrecip` | 0.2 | 盆心降水上限；更湿的盆地不产蒸发岩 |
| `MinBasinCells` | 6 | 少于这么多格子的洼地跳过 |
| `RingInner` | 0.0 | 归一化半径下限（0 = 盆心） |
| `RingOuter` | 1.0 | 归一化半径上限（1 = 盆地边缘） |
| `BodiesPerBasin` | 24 | 每个盆地几片 |
| `RadiusRatio` | 0.004 | 单片半径（占 R） |
| `ThicknessVoxels` | 2 | 薄层厚度（体素） |
| `bRequireVolcano` | false | 要求盆心附近有活火山（锂盐用） |
| `VolcanoRadiusRatio` | 0.05 | 「附近」的球面弦距上限（占 R） |
| `Hard` | 0.25 | 软边宽度 |

### 3.2 算法

记 `R = Config.Radius`，`Res = BasinMask.Res`。

1. 校验 `BasinId.Res == BasinMask.Res == FlowAccum.Res`（否则报错返回）；`BodiesPerBasin == 0` 直接返回。
2. 单遍扫描所有格：`BasinMask == 2` 且 `BasinId >= 0` 的格子按编号累加「单位方向之和」与「格子数」。编号升序遍历，保证确定性。
3. 每个盆地：
   - 中心 `Center = SumDir.GetSafeNormal()`；`Center` 近零（对跖抵消）跳过。
   - `Precipitation.Sample(Center) > MaxPrecip` 跳过。
   - 格子数 < `MinBasinCells` 跳过。
   - `bRequireVolcano`：`Fields.Volcanoes` 中 `bActive` 且与 `Center` 的弦距 ≤ `VolcanoRadiusRatio·R` 的火山数 > 0，否则跳过。
   - 盆地角半径 `Rad = max(acos(clamp(Dot(Dir_cell, Center))))`。
4. 撒 `BodiesPerBasin` 片：`u = Rng.NextFloat()`，`r = Rad · lerp(RingInner, RingOuter, sqrt(u))`；方位角 `φ = Rng·2π`，在 `Center` 的切平面里转 `r` 得到方向 `Dir`。
5. 单片：贴地表，中心 `Dir·(R + Elevation.Sample(Dir) − 0.5·Thickness)`；`TangentFrame(Dir, φ)`；半径 `(RadiusRatio·R, RadiusRatio·R, max(0.5·Thickness,0.5))`；`Grade = RandomGrade(Rng)`。
6. 环带边界保证：`RingInner/RingOuter` 可倒置（自动取 min/max）；`RingOuter` 超过 1 由盆地半径封顶。

### 3.3 哈希

参数全部进 `HashParams()`（含 `bRequireVolcano`、`RingInner/RingOuter`）。

## 4. 规则 `FGeoCoalRule`

### 4.1 参数

| 字段 | 默认 | 含义 |
|---|---|---|
| `MinPrecip` | 0.6 | 降水下限：太干不长煤 |
| `MaxElevRatio` | 0.004 | 高程上限（占 R）：只做低地 |
| `MaxSlope` | 0.02 | 坡度上限（高程差 / 弧长） |
| `MaxContinental` | 0.5 | 大陆度上限：越靠海越像三角洲/沼泽 |
| `PatchCount` | 300 | 目标片数 |
| `RadiusRatio` | 0.004 | 单片半径（占 R） |
| `ThicknessVoxels` | 2 | 煤层厚度（体素） |
| `DepthVoxels` | 1 | 埋深（体素）：煤是埋着的，不像砂矿贴地表 |
| `Hard` | 0.2 | 软边宽度 |

### 4.2 算法

1. 校验 `Precipitation.Res > 0`、`ContinentalMask.Res > 0`、`Elevation.Res > 0`。
2. 拒绝采样 `PatchCount × 400` 次：
   - `Dir = 随机单位方向`；`Elev = Elevation.Sample(Dir)`；要求 `0 < Elev ≤ MaxElevRatio·R`。
   - `Precipitation.Sample(Dir) ≥ MinPrecip`；`ContinentalMask.Sample(Dir) ≤ MaxContinental`。
   - 坡度：在以 `Dir` 为中心的切平面里取 ±`SlopeProbeRatio`（0.002R）两个方向的 `Elevation`，`Slope = max|ΔE| / (0.002R)`；要求 `Slope ≤ MaxSlope`。
3. 命中则放一片：中心 `Dir·(R + Elev − DepthVoxels − 0.5·Thickness)`；`TangentFrame(Dir, 随机走向)`；半径 `(RadiusRatio·R, RadiusRatio·R, max(0.5·Thickness,0.5))`；`Grade = RandomGrade(Rng)`。

## 5. 撒矿器

两个新类：
- `UGeoBasinDepositScatterer`（属性与 §3.1 一一对应，全部 `EditAnywhere` + 中文注释）。
- `UGeoCoalScatterer`（属性与 §4.1 一一对应）。

实例（配方 `DA_GeoRecipe_Archipelago`）：

| 矿物 | 撒矿器 | 关键参数 |
|---|---|---|
| `EvaporiteCarbonate` | Basin | `RingInner 0.0 / RingOuter 0.35` |
| `EvaporiteGypsum` | Basin | `0.35 / 0.6` |
| `EvaporiteHalite` | Basin | `0.6 / 0.85` |
| `EvaporiteSylvite` | Basin | `0.85 / 1.0` |
| `LithiumSalt` | Basin | `0.0 / 0.3`、`bRequireVolcano = true` |
| `Coal` | Coal | 默认 |

## 6. 矿物目录新增

| Id | 显示名 | 调试色 |
|---|---|---|
| `EvaporiteCarbonate` | 蒸发岩·碳酸盐 | 淡青 |
| `EvaporiteGypsum` | 蒸发岩·石膏 | 米白 |
| `EvaporiteHalite` | 蒸发岩·岩盐 | 粉白 |
| `EvaporiteSylvite` | 蒸发岩·钾盐 | 橙红 |
| `LithiumSalt` | 锂盐 | 紫 |
| `Coal` | 煤 | 近黑 |

追加在末尾，不动已有 18 条的相对顺序（编号 = 下标 + 1）。

## 7. 调试图层

不新增。沉积体是普通原语，「矿物 / 地表矿化」图层可见。

## 8. 测试（`Src/Tests/PlanetGeoStage06c2Tests.cpp`）

**合成场**（Res=8，手工铺 `BasinMask/BasinId/Precipitation/Elevation`）：

1. `BasinId` 导出：分辨率与 `BasinMask` 一致；`BasinMask == 2` 的格子 `BasinId >= 0`；非内流格 `BasinId == -1`。
2. `RingBand`：环带 `[0.3, 0.6]` 产出的片中心归一化半径全部落在带内（±0.05）。
3. `RingPrecip`：盆心降水 0.5 > `MaxPrecip 0.2` → 0 片；降水 0.1 → 有片。
4. `RingVolcano`：`bRequireVolcano` 且场上无火山 → 0 片；加一座同向活火山 → 有片。
5. `CoalConditions`：高降水 + 低高程 + 缓坡 + 近海 → 有片；任一条件不满足 → 0 片。
6. `Shape`：蒸发岩厚度 = `ThicknessVoxels`，煤中心 = 地表下 `DepthVoxels + Thickness/2`（±0.6 体素）。
7. 哈希探针：两个规则的每个参数改一项哈希必变。

**整管线**：

8. `Stage06c2.Pipeline`：默认配方 + 6 个新撒矿器烘焙；打印每种片数；断言煤 > 0；若存在内流盆地则蒸发岩 > 0（无盆地时打印提示，不判失败）。
9. 确定性：两次烘焙 `Primitives.Hash()` 一致；单线程/并行一致。

## 9. 风险

- **内流盆地数量随种子变化**：默认星球可能没有合格盆地，蒸发岩为 0 是合法结果；集成测试因此按条件断言。
- **环带边界相接不相交**：四个环实例的边界首尾相接（0–0.35 / 0.35–0.6 / 0.6–0.85 / 0.85–1.0）。材质求值取「强度最大」的矿体，重叠区归谁不确定，所以故意不留重叠；环内片数是离散的，接缝处可能露出一点基底，属可接受。
- **煤的坡度探测**固定 0.002R，极小星球上会跨多格；属可接受近似。
- **`BasinId` 属坏数据防护**：`>= 6·Res²` 不做断言，按浮点读入后聚类时忽略负值与越界值。
