# 地质阶段 6b 出露结算 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development.

**Goal:** 新增阶段 7（`FStage06bExposure`）：按 6a 的剥蚀深度把岩体与矿体沿径向上抬，写 `Exposure` / `RawDepthBefore`，加两个调试图层与配方参数。

**Architecture:** 纯数据后处理：读 `Fields.ErosionDepth` 与 `Fields.Primitives`，原地改原语中心与两个新字段，不改高程、不改形状。矿体跟随宿主岩体的抬升量。

**Spec:** `docs/superpowers/specs/2026-11-16-geology-stage06b-exposure-design.md`

## Global Constraints

- 尺寸占半径比例；注释 WHY-only ≤3 行中文；中文源文件 UTF-8 with BOM。
- 参数进 `Hash()`；提交 `git commit -- <paths>`；尾行 `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`；不碰 `Content/`。
- 若 BUILD 报 Live Coding 或 `LNK1104`，立即 BLOCKED，不改名绕过。
- 迭代时只跑 `StellarEra.Planet.Geo.Stage06b`、`StellarEra.Planet.Geo.Recipe`、`StellarEra.Planet.Geo.Debug`、`StellarEra.Planet.Geo.Stage02Acceptance`；全量 `StellarEra` 只在最终提交前跑一次（基线 333）。
- 性能预算：`Stage02Acceptance` 总烘焙 ≤ 12 s、`Stage06Erosion` ≤ 1.5 s；6b 是纯 CPU 后处理，应 < 0.1 s。

## 文件

| 文件 | 动作 |
|---|---|
| `Geo/Core/GeoPrimitive.h` | `Exposure` / `RawDepthBefore` + `GeoPrimitiveSupport(dir)` |
| `Geo/Core/GeoPrimitiveIndex.cpp` | 哈希并入两字段 |
| `Geo/Bake/PlanetRecipeConfig.h` | `FPlanetExposureParams` + `Config.Exposure` + 哈希 |
| `Geo/Bake/PlanetBakeTypes.h` | `EPlanetField::Exposure = 1 << 10` |
| `Geo/Bake/Stages/Stage06bExposure.{h,cpp}` | 新建 |
| `Geo/Bake/PlanetBakePipeline.cpp` | 注册阶段 7 |
| `Geo/Assets/PlanetRecipe.{h,cpp}` | `Stage6b\|Exposure` 三属性 + ToConfig |
| `Tests/PlanetGeoStage06bTests.cpp` | 新建 |
| `Tests/PlanetGeoRecipeTests.cpp` | 探针 + 全字段透传（非默认值） |
| `Tests/PlanetGeoEvalStage02Tests.cpp` | 默认管线阶段数 6 → 7 |

支撑函数（`GeoPrimitive.h`）：

```cpp
/** 椭球沿单位方向 Dir 的半厚（支撑函数）：| Rot · (Radii ∘ (Rot⁻¹ Dir)) |。 */
FORCEINLINE float GeoPrimitiveSupport(const FGeoPrimitive& P, const FVector3f& Dir)
{
	const FVector3f L = P.Kind == EGeoPrimitiveKind::OrientedBlob ? P.Rotation.UnrotateVector(Dir) : Dir;
	return (L * P.Radii).Size();
}
```

测试辅助（前缀 `Stage06b`）：跑阶段 2→3→4→5→6，**保存原语副本**，再跑阶段 7；返回两份原语。

---

### Task 1: 数据字段、支撑函数、抬升与出露标记

- [ ] **Step 1: 失败测试** `Stage06b.Lift`
  - 全部原语 `Top = |Center| + GeoPrimitiveSupport(P, dir) ≤ S + 1`（`S = R0 + Elevation(dir)`）。
  - `Radii`、`Rotation`、`Hard`、`MineralId`、`Grade` 与阶段 6 结束时逐位相同（逐原语比对）。
  - 至少一个原语 `Exposure > 0.5`；打印数量与最大值。
- [ ] **Step 2: RED。**
- [ ] **Step 3: 实现**
  - 两字段 + 哈希；`FPlanetExposureParams`（`bExposure=true`、`LiftScale=1.0`、`MaxLiftRatio=0.02f`）+ `Hash()`；`EPlanetField::Exposure = 1 << 10`。
  - `FStage06bExposure::Bake`：校验 `ErosionDepth.Res > 0`；`bExposure == false` 直接返回。
    1. 岩体（`MineralId == 0 && LithologyId != 0`）按中心方向采样 `ErosionDepth`，`Lift = clamp(LiftScale·D, 0, MaxLiftRatio·R)`，存 `LiftByIdx`。
    2. 矿体在岩体集合中找「中心落在椭球内（`GeoPrimitiveDistance < 1`）且归一化距离最小」的宿主，`Lift = LiftByIdx[host]`；找不到用自身方向的 `D`。
    3. 平移与夹取按 spec §3 第 3–5 步；`Bounds` 同步平移。
    4. 写 `Exposure = uint8(round(255·ratio))` 与 `RawDepthBefore`。
  - 管线注册阶段 7；阶段数测试 6 → 7。
- [ ] **Step 4: 绿；提交。**

---

### Task 2: 宿主跟随、剥蚀为零、山带出露、开关

- [ ] **Step 1: 失败测试**
  - `Stage06b.HostFollow`：每个矿体的位移与宿主位移相差 ≤ 1 体素；打印无宿主矿体数。
  - `Stage06b.ZeroErosion`：用 `LiftScale=0` 的对照烘焙，所有原语零位移、`Exposure` 全 0（等价于 `bExposure=false` 的零位移断言）。
  - `Stage06b.Mountains`：`MountainMask > 0.5` 附近原语平均 `Exposure` > `MountainMask < 0.1` 的 2 倍。
  - `Stage06b.Switch`：`bExposure=false` 零位移、`Exposure` 全 0；两次烘焙 `Primitives.Hash()` 一致；并行/单线程一致。
- [ ] **Step 2: RED → 实现补强 → 绿；提交。**

---

### Task 3: 图层与配方参数

- [ ] **Step 1: 测试** `Stage06b.Layers`：`Exposure` 图层在某个已知出露原语方向上返回值与 `Exposure/255` 相差 ≤ 0.02；`ExposedOre` 在完全出露矿体中心 = 2、无矿处 = 0。配方探针（`LiftScale`、`MaxLiftRatio`、`bExposure`）+ 全字段非默认值透传。
- [ ] **Step 2: 实现**
  - 两个图层（spec §6）：`Exposure` 连续 0–1 黑→白；`ExposedOre` 分类三档 + 图例（未出露/部分出露/完全出露），只在 `MineralId != 0` 上非零。Sample 用快照原语查询（照 `IntrusionType`，含 `D < 1` 守卫）。
  - 配方三属性 + `ToConfig`。
- [ ] **Step 3: 绿；提交。**

---

### Task 4: 全量回归 + 编辑器目检（controller）

- [ ] 全量 `StellarEra`；编辑器重烘，导出 `Stage7_Exposure_*` 与 `Stage7_ExposedOre_*`，确认山带出露、平原不出露；报告出露原语数、最大出露、阶段 7 耗时。停下等用户验收。
