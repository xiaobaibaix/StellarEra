# 地质阶段 6a 水系与剥蚀 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development.

**Goal:** 新增阶段 6（`FStage06Erosion`）：填洼、内流盆地判定、D8 流向、汇流累积、河谷下切刻进高程、剥蚀深度场与四个调试图层。

**Architecture:** 水系在宏观网格（`HydroRes = max(64, FaceRes/4)`）上算，先预计算 8 邻域表（跨面用 `DirToFace` 换算）；priority-flood 单线程，其余逐格并行。河道中心线按宏观格分桶，全分辨率高程按到线段距离刻 U 形谷。剥蚀深度只是一张场，不压高程。

**Spec:** `docs/superpowers/specs/2026-11-16-geology-stage06a-hydrology-design.md`

## Global Constraints

- 尺寸占半径比例；注释 WHY-only ≤3 行中文；中文源文件 UTF-8 with BOM。
- 参数进 `Hash()`；提交 `git commit -- <paths>`；尾行 `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`；不碰 `Content/`。
- 编辑器必须先关（Live Coding 锁 DLL）；BUILD / TEST 命令同前几阶段，全量过滤 `StellarEra`，基线 327。
- 确定性：平手一律按格子编号小者优先；priority-flood 用 (高程, 编号) 作排序键。

## 文件

| 文件 | 动作 |
|---|---|
| `Geo/Bake/PlanetRecipeConfig.h` | `FPlanetHydroParams` + `Config.Hydro` + 哈希 |
| `Geo/Bake/PlanetBakeTypes.h` | `EPlanetField::Hydro = 1 << 9`；`FlowAccum, BasinMask, ErosionDepth, RiverMask` |
| `Geo/Bake/Stages/Stage06Erosion.{h,cpp}` | 新建 |
| `Geo/Bake/PlanetBakePipeline.cpp` | 注册阶段 6 |
| `Geo/Assets/PlanetRecipe.{h,cpp}` | `Stage6\|Hydro` 属性 + ToConfig |
| `Tests/PlanetGeoStage06Tests.cpp` | 新建 |
| `Tests/PlanetGeoRecipeTests.cpp` | 探针 + 全字段透传（非默认值） |
| `Tests/PlanetGeoEvalStage02Tests.cpp` | 默认管线阶段数 5 → 6 |

公共接口（Task 1 产出，后续任务只读）：

```cpp
namespace Stage06
{
	/** 宏观水系网格：格子编号 (Face*Res+Y)*Res+X；Nbr 每格 8 个邻居编号（跨面已换算）。 */
	struct FHydroGrid
	{
		int32 Res = 0;
		TArray<int32> Nbr;        // 8 * N
		TArray<float> Area;       // 立体角权重，和为 1
		TArray<FVector3f> Dir;    // 格心方向
		int32 Num() const { return 6 * Res * Res; }
	};
	STELLARERAPLANET_API void BuildGrid(int32 Res, FHydroGrid& Out);
}
```

测试辅助（`PlanetGeoStage06Tests.cpp` 顶部匿名命名空间，前缀 `Stage06`）：跑阶段 2→3→4→5→6，返回阶段 6 之前的高程副本。

---

### Task 1: 网格、填洼、流向、内流盆地、汇流累积

- [ ] **Step 1: 失败测试**
  - `Stage06.Grid`：`BuildGrid(64)` 每格 8 个邻居都有效且非重复项两两互为邻居（对称）；`Area` 之和 ≈ 1（1e-4）。
  - `Stage06.FlowNetwork`：真实星球（FaceRes 256）——每个陆上格子顺流向走 ≤ N 步必到达海洋或内流盆地；沿流向 `FlowAccum` 不减；每个湖盆有溢出格且溢出格有下游；每个内流盆地集水区平均降水 < `EndorheicPrecip`。
- [ ] **Step 2: RED。**
- [ ] **Step 3: 实现**
  - `FPlanetHydroParams`（字段与默认值照 spec §5，全部先加）+ 哈希。
  - `BuildGrid`：邻居 = 格心沿面内 ±1 texel 偏移后的方向 → `DirToFace` → 目标格；面角格子只有 7 个真实邻居时，第 8 槽重复最近的一个（保持 8 槽便于查表）。
  - `Bake` 前四步：采样 `E`；priority-flood（最小堆，键 `(Filled, Idx)`）；流向（D8 最低邻居）；第一遍累积求洼地集水平均降水 → 判内流 → 切断溢出格 → 第二遍累积；写 `FlowAccum`（归一化）与 `BasinMask`。
  - 管线注册阶段 6；阶段数测试 5 → 6。
- [ ] **Step 4: 绿；提交。**

---

### Task 2: 河道、刻谷、剥蚀深度

- [ ] **Step 1: 失败测试**
  - `Stage06.Carve`：刻谷后每个全分辨率 texel 高程 ≤ 刻谷前；陆地占比变化 < 0.005；刻谷前为陆的 texel 刻谷后仍 > 0；至少 1 条河道刻深 > 0.001R。
  - `Stage06.Erosion`：`MountainMask > 0.5` 格子平均剥蚀 > `MountainMask < 0.1` 的 2 倍；海底剥蚀为 0。
- [ ] **Step 2: RED。**
- [ ] **Step 3: 实现**：spec §4 第 5–8 步；线段按宏观格分桶（每段放入两端格子及其 8 邻格的桶）；`ErosionDepth` 写入宏观场；刻谷后重算 Min/Max/LandAreaFrac。
- [ ] **Step 4: 绿；提交。**

---

### Task 3: 图层、确定性与尺度、配方参数

- [ ] **Step 1: 测试**
  - `Stage06.Determinism`：并行与单线程四张场哈希一致。
  - `Stage06.ScaleInvariance`：按 spec §7.6 的容差。
  - `Recipe`：`Stage6|Hydro` 探针（至少 `RiverThreshold`、`CarveRatio`、`EndorheicPrecip`、`bRivers`）+ 全字段非默认值透传。
- [ ] **Step 2: 实现**：四个图层（spec §6）+ `ExportDebugFields`；配方 UPROPERTY（中文注释）+ `ToConfig`。
- [ ] **Step 3: 绿；提交。**

---

### Task 4: 全量回归 + 编辑器目检（controller）

- [ ] 全量 `StellarEra`；编辑器重烘 GeoPlanetMap，导出 `Stage6_*` 图层，确认河网连到海、内流盆地在干旱区、河谷在高程上可见；报告河道数、内流盆地数、最大下切与阶段 6 耗时。停下等用户验收。
