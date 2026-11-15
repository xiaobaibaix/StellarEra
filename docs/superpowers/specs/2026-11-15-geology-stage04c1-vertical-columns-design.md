# 地质阶段 4c-1：火山竖向矿柱 设计

> 上位规格：`2026-11-12-geology-pipeline/04-magmatism.md` §「撒矿」竖向矿柱。阶段 4 拆三步，4a 火山地形与 4b 侵入体已完成；本文只做三条竖向矿柱，接触型与岩体伴生矿（锡钨、伟晶岩、铜镍、铬铂、金刚石、稀土）留 4c-2。
> 4c-1 读 4a 的火山点表与 4b 的岩体原语（同一次烘焙的 `FPlanetBakeFields`）。

## 1. 目标与非目标

**目标**
- 硫磺：活跃火山火口内的地表结壳（黄色，肉眼可见）。
- 浅成低温金银：火山锥顶下方 2–6 格的脉体群。
- 斑岩铜：每座岩株顶部下方 6–15 格的浸染体。
- 三者构成固定竖直顺序：硫磺 → 金银 → 斑岩铜。
- 三种矿进入矿物目录；撒矿器可开关、可复制、参数带中文注释。

**非目标**
- 其余六种岩浆矿（4c-2）。
- 矽卡岩（需要碳酸盐围岩，当前岩性表没有沉积岩，按上位规格「需 3A 否则跳过」）。
- 不做地表线索的 UI/图例改动（蚀变线索 4b 已有）。

## 2. 结构

- 三个新撒矿器子类（`GeoMineralScatterer.h/.cpp`）：`UGeoSulfurScatterer`、`UGeoEpithermalScatterer`、`UGeoPorphyryScatterer`；
  对应规则类（`GeoMineralRules.h/.cpp`）实现 `IGeoMineralRule`，`GetStage() == 4`，读 `Ctx.Fields.Volcanoes` 与 `Ctx.Fields.Primitives`。
- 矿物目录 `DA_MineralCatalog` 增加三项：硫磺、浅成低温金银、斑岩铜。
- 竖向顺序由三种规则各自的深度区间保证，不另设总控。

## 3. 规则细节

记 `S(dir) = R0 + Elevation(dir)`（体素），`g = 0.002R`（原规格 1 格）。

| 矿 | 选点 | 形态 | 默认 |
|---|---|---|---|
| 硫磺 | `Volcanoes` 中 `bActive && CraterRadius > 0` 的每座 | `FGeoMineralOverlay`：遮罩在火口半径内为 1（边缘平滑到 1.2 倍），`DepthVoxels = 40` | 遮罩分辨率 = `max(128, Elevation.Res / 2)`；火口半径取 `max(CraterRadius, 0.008R)` |
| 浅成金银 | 同一批活火山，锥顶正下方 | 每座 `VeinCount` 条朝向椭球，沿走向排开，`Hard = 0.06` | 深度 0.004–0.012R，单条半径 (0.004R, 0.004R, 0.0015R)，水平散布 2 倍火口半径，`VeinCount = 3` |
| 斑岩铜 | `Primitives` 中 `LithologyId == Diorite` 的每座岩株 | 一个软边椭球，`Hard = 0.25` | 中心在岩株顶面下方 0.012–0.03R（PerStock 个自上而下铺，默认 1 个取 0.012R）；半径 0.01R |

- 随机流：每条规则用 `Ctx.Rng`（管线按 `(Seed, 4, scatter, 规则哈希, SeedOffset)` 派生，已隔离）。
- 品位：用撒矿器公共的 `GradeMin/GradeMax`。
- 硫磺遮罩分辨率取 `max(128, Elevation.Res / 2)`；火口半径按 texel 量化后至少 1.5 texel，否则遮罩里看不见。

## 4. 参数（每个撒矿器自己的 `Rule` 分类，带中文注释）

| 撒矿器 | 参数 | 默认 |
|---|---|---|
| 硫磺 | `RadiusRatio`（结壳半径下限，占 R）/ `DepthVoxels`（结壳厚度，体素） | 0.008 / 40 |
| 浅成金银 | `DepthMinRatio` / `DepthMaxRatio` / `VeinCount` / `VeinRadiusRatio` / `VeinThicknessRatio` / `SpreadRatio` | 0.004 / 0.012 / 3 / 0.004 / 0.0015 / 2.0 |
| 斑岩铜 | `DepthMinRatio` / `DepthMaxRatio` / `RadiusRatio` / `PerStock` | 0.012 / 0.03 / 0.01 / 1 |

全部进规则哈希（`HashParams`），改参数即改配方哈希。

## 5. 测试（`Src/Tests/PlanetGeoStage04cTests.cpp`）

1. **硫磺只在活火山火口**：遮罩非零的每个 texel 方向，到最近 `bActive` 火山的弦长 ≤ 1.2 × 火口半径；每座带火口的活火山都有遮罩覆盖。
2. **竖直顺序**：每座活跃层状火山从中心向下采样（深度 0 … 0.035R），首次命中的矿种顺序必须是硫磺（地表覆层）→ 金银 → 斑岩铜，三段都非空、无交叉。
3. **斑岩铜只长在岩株上**：每个斑岩铜体中心到最近闪长岩原语中心的距离 ≤ 1.5 × 岩株半径；其顶面在地表以下。
4. **尺度不变**：半径减半后三者数量不变，体素尺寸与深度减半。
5. **开关与确定性**：各自关闭后这类矿为 0；同配方两次烘焙 `Primitives.Hash()` 与覆层哈希一致。
6. **目录**：三个矿种在 `Config.Minerals` 里存在（测试里直接构造目录）。

## 6. 风险

- 火口 0.008R 在 512 分辨率遮罩上只有约 2 texel：边缘会呈块状。可接受（结壳本来就是斑块），测试用 1.2 倍容差。
- 硫磺结壳挡在矿体之前：`PlanetMaterialEval` 里覆层只在没有矿体命中时生效，火口内金银脉从 0.004R 深度起，不与结壳冲突。
- 斑岩铜体半径 0.01R 与岩株 0.008R 同量级：中心下移保证它是「岩株顶下方的浸染体」而不是把岩株包住。
