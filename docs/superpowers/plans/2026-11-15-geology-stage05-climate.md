# 地质阶段 5 气候 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development.

**Goal:** 新增阶段 5，产出温度 / 降水 / 风向 / 生物群系四张场与四个调试图层。

**Architecture:** `FStage05Climate` 读阶段 3 的高程与大陆度，在 `ClimateRes` 上并行计算；雨影是唯一的非局部回溯。参数进 `FPlanetClimateParams`，配方 `Stage5|Climate` 透传。

**Spec:** `docs/superpowers/specs/2026-11-15-geology-stage05-climate-design.md`

## Global Constraints

- 注释 WHY-only ≤3 行中文；中文源文件 UTF-8 with BOM。
- 参数进 `Hash()`；提交 `git commit -- <paths>`；尾行 `Co-Authored-By: Claude Code <noreply@anthropic.com>`。
- 编辑器必须先关；BUILD/TEST 同前（过滤 `StellarEra`，基线 320）。

## 文件

| 文件 | 动作 |
|---|---|
| `Geo/Bake/PlanetRecipeConfig.h` | `FPlanetClimateParams` + `Config.Climate` + 哈希 |
| `Geo/Bake/PlanetBakeTypes.h` | `EPlanetField::Climate`、五张场 |
| `Geo/Bake/Stages/Stage05Climate.{h,cpp}` | 新建 |
| `Geo/Bake/PlanetBakePipeline.cpp` | 注册阶段 5 |
| `Geo/Assets/PlanetRecipe.{h,cpp}` | `Stage5\|Climate` 属性 + ToConfig |
| `Tests/PlanetGeoStage05Tests.cpp` | 新建 |

测试辅助：

```cpp
FPlanetBakeFields Stage05TestPlanet(const FPlanetRecipeConfig& C, FPlanetBakeReport& R)
{
	FPlanetBakeFields F;
	FPlanetBakeContext C2{ C, F, FGeoRng::Derive(C.Seed, 2, 0), R }; FStage02Plates().Bake(C2);
	FPlanetBakeContext C3{ C, F, FGeoRng::Derive(C.Seed, 3, 0), R }; FStage03Terrain().Bake(C3);
	FPlanetBakeContext C5{ C, F, FGeoRng::Derive(C.Seed, 5, 0), R }; FStage05Climate().Bake(C5);
	return F;
}
// 合成星球：只有 Elevation/ContinentalMask/PlateId/CrustType 场，给雨影测试用。
FPlanetBakeFields Stage05RidgePlanet(const FPlanetRecipeConfig& C, float RidgeAmpVoxels, float RidgeHalfWidthDeg);
```

---

### Task 1: 阶段骨架 + 温度 + 风带 + 两个图层

- [ ] **Step 1: 失败测试** `Stage05.Temperature`（赤道 > 0.8、极区 < 0.2、同纬度高地比低地冷）、`Stage05.Wind`（|lat|<30 吹向 ≈ 270°，30–60 ≈ 90°，>60 ≈ 270°）。
- [ ] **Step 2: RED。**
- [ ] **Step 3: 实现**
  - `FPlanetClimateParams`（本轮先放 `ClimateRes=256`、`EquatorTemp=1`、`PoleTemp=0.05`、`LapseRate=0.35`、`Continentality=0.15`、`AxialTiltDeg=23.4`）。
  - `EPlanetField::Climate = 1 << 8`；`FPlanetBakeFields` 加 `Temperature, Precipitation, WindDir, Biome, ClimateLand`。
  - `Stage05Climate::Bake`：校验 `Elevation.Res > 0 && ContinentalMask.Res > 0`；五张场 `Init(ClimateRes)`；并行算温度与风向（公式照 spec §3.1/§3.2）；`ClimateLand = Elevation > 0 ? 1 : 0`。
  - 图层 `ClimateTemperature`（连续 0–1，蓝→红）、`ClimateWind`（分类：0 无风、1 信风、2 西风、3 极地东风；Sample 由 `WindDir` 与纬度重建）。
  - 管线注册阶段 5。
- [ ] **Step 4: 绿；提交。**

---

### Task 2: 降水 + 雨影

- [ ] **Step 1: 失败测试**
  - `Stage05.DryBelt`：真实星球 |lat| ∈ [25°,35°] 的平均降水比 |lat|<10° 与 [40°,50°] 都低 ≥ 0.15。
  - `Stage05.RainShadow`：`Stage05RidgePlanet` 造一条南北向长山（`Elevation = Amp * exp(-(u/w)²)`，u 为到目标经线的角距，`Amp` 取 **0.08R**、`w` 取 6°），在 45° 纬度带取背风侧（东侧 3–5°）与迎风侧（西侧 3–5°），平均降水差 > 0.4。0.04R 时采样点仍在山腰、实测只差约 0.29，够不到验收线，故加高山体。
- [ ] **Step 2: RED。**
- [ ] **Step 3: 实现**：四段纬度基准用 `SmoothStep`；离海衰减 `P *= 1 - OceanFalloff·M`（内陆更干）；雨影按 spec §3.3（沿 `-W` 回溯 stride 2 texel、最多 32 步，撞海提前停）；最后加 `PrecipBias` 并 clamp。
  图层 `ClimatePrecipitation`（连续 0–1，褐→蓝绿）。
- [ ] **Step 4: 绿；提交。**

---

### Task 3: 生物群系 + 一致性与尺度测试

- [ ] **Step 1: 测试** `Stage05.BiomeTable`：每 texel 用记录的（T,P）重查一遍查表函数必须等于 `Biome`；至少四个不同群系出现。
- [ ] **Step 2: 实现**：`Stage05Biome(T, P, Params)` 按 spec §3.4 顺序；写 `Biome`；图层 `Biome`（分类 8 档 + 图例）；`ExportDebugFields` 导出四张场。
- [ ] **Step 3: 测试** `Stage05.ScaleInvariance`（半径减半场逐位相同）、`Stage05.Determinism`（两次烘焙四张场哈希一致）。
- [ ] **Step 4: 绿；提交。**

---

### Task 4: 配方参数 + 探针 + 收尾（controller）

- [ ] `UPlanetRecipe` 增加 `Stage5|Climate` 全部字段（中文注释、默认值同 spec §4）与 `ToConfig`；配方测试加 4 条哈希探针与全字段透传比较。
- [ ] 全量回归；编辑器导出 `Stage5_*` 图层，检查 30° 干旱带、雨影与群系分布，报告实测。
