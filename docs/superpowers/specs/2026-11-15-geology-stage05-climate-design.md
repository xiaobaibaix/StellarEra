# 地质阶段 5：气候 设计

> 上位规格：`2026-11-12-geology-pipeline/05-climate.md`。本阶段只产场，不改地形、不撒矿；为阶段 6（侵蚀）与 7（风化）提供条件。

## 1. 目标与非目标

**目标**
- 新增 `FStage05Climate`（阶段号 5），产出四张 256 分辨率场：`Temperature`、`Precipitation`、`WindDir`、`Biome`。
- 雨影：本阶段唯一的非局部算法（逆风回溯扣水汽）。
- 注册四个调试图层；参数进配方 `Stage5|Climate`、带中文注释、进哈希。

**非目标**
- 不接生物群系材质（`PlanetBiomeCubemap` 本轮不动）。
- 不模拟季节：配方里的自转轴倾角只记录、不参与计算。
- 不产任何矿种。

## 2. 结构

- 新文件 `Geo/Bake/Stages/Stage05Climate.{h,cpp}`，`IPlanetStage`：`GetStageIndex() == 5`，读 `EPlanetField::Elevation | Continental`，写新增的 `EPlanetField::Climate`。
- 场全部 `ClimateRes`（默认 256）；`Biome` 存 float 编号（0–7）。
- 结果放 `FPlanetBakeFields`：`Temperature`、`Precipitation`、`WindDir`、`Biome`、`ClimateLand`（陆地/海洋，调试用）。
- 快照不新增字段（阶段 6/7 在烘焙期读 `Fields`；本轮无运行期消费者）。

## 3. 算法

### 3.1 温度

```
lat = asin(clamp(dir.Z, -1, 1))
Insolation = 1 - sin²(lat)
T = PoleTemp + (EquatorTemp - PoleTemp) * Insolation
  - LapseRate * clamp(E / (0.08 * R), 0, 1)     // 高程越高越冷
  + Continentality * (ContinentalMask - 0.5)    // 内陆更极端
T = clamp(T, 0, 1)
```

### 3.2 风带

按纬度绝对值分三段，存「风吹向」的方位角（0 = 北，90 = 东）：

| |lat| | 风带 | 吹向 |
|---|---|---|
| < 30° | 信风 | 270°（向西） |
| 30–60° | 西风 | 90°（向东） |
| > 60° | 极地东风 | 270° |

方位角转切向：`N = normalize(Z - dir·(dir·Z))`，`W = N·cos(az) + (dir×N)·sin(az)`。

### 3.3 降水

1. **纬度基准**：赤道 `BaseEquator`，30° 附近 `BaseDry`，中纬 `BaseMid`，两极 `BasePole`；用 `SmoothStep` 在四段之间过渡。
2. **离海衰减**：`P *= 1 - OceanFalloff * M`（M = 大陆度；内陆 1、海洋 0）。
3. **雨影**：从当前点沿 `-W` 每步 `ShadowStride` texel、最多 `ShadowSteps` 步；第 k 步的高程差 `Rise = Elev(q_k) - Elev(q_{k-1})`，`Rise > 0` 时 `P *= 1 - RainShadow * clamp(Rise / (0.02R), 0, 1)`；遇到海面（E ≤ 0）提前结束（海上水汽充足）。
4. `P = clamp(P, 0, 1)`。

### 3.4 生物群系

按固定顺序查表（首个命中即返回，保证「同一对温/降水 → 同一群系」）：

| 顺序 | 群系 | 条件 |
|---|---|---|
| 1 | 冰原 | `T < BiomeIceTemp` |
| 2 | 沙漠 | `P < BiomeDesertPrecip` |
| 3 | 苔原 | `T < BiomeTundraTemp` |
| 4 | 针叶林 | `T < BiomeForestTemp && P > BiomeForestPrecip` |
| 5 | 雨林 | `T > BiomeRainTemp && P > BiomeRainPrecip` |
| 6 | 稀树草原 | `T > BiomeSavannaTemp` |
| 7 | 温带林 | `P > BiomeTemperatePrecip` |
| 8 | 草原 | 其余 |

## 4. 参数（配方 `Stage5|Climate`）

| 参数 | 默认 | 含义 |
|---|---|---|
| `AxialTiltDeg` | 23.4 | 自转轴倾角（仅记录，本轮不模拟季节） |
| `EquatorTemp` / `PoleTemp` | 1.0 / 0.05 | 赤道 / 极点归一化温度 |
| `LapseRate` | 0.35 | 高程每达到 0.08R 扣掉的温度份额 |
| `Continentality` | 0.15 | 大陆度对温度的影响幅度 |
| `BaseEquator` / `BaseDry` / `BaseMid` / `BasePole` | 0.9 / 0.15 / 0.7 / 0.15 | 四段纬度降水基准 |
| `OceanFalloff` | 0.3 | 内陆降水衰减 |
| `RainShadow` | 0.8 | 雨影强度 |
| `ShadowSteps` / `ShadowStride` | 32 / 2 | 回溯步数与步长（texel） |
| `PrecipBias` | 0 | 全局降水偏移（调参用） |
| `BiomeIceTemp` / `BiomeDesertPrecip` / `BiomeTundraTemp` / `BiomeForestTemp` / `BiomeForestPrecip` / `BiomeRainTemp` / `BiomeRainPrecip` / `BiomeSavannaTemp` / `BiomeTemperatePrecip` | 0.2 / 0.2 / 0.35 / 0.55 / 0.35 / 0.7 / 0.6 / 0.75 / 0.5 | 生物群系阈值 |

全部进 `Hash()`，`ToConfig` 透传。

## 5. 调试图层

- `ClimateTemperature`：连续 0–1，蓝→红。
- `ClimatePrecipitation`：连续 0–1，褐→蓝绿。
- `ClimateWind`：分类四档（信风 / 西风 / 极地东风 / 无风）。
- `Biome`：分类 8 档 + 图例。

`ExportDebugFields` 导出四张场。

## 6. 测试（`Src/Tests/PlanetGeoStage05Tests.cpp`）

1. **30° 干旱带**：真实星球上 |lat ∈ [25°,35°]| 的平均降水比赤道带与中纬带低 ≥ 0.15。
2. **雨影**：合成一颗「南北向长山、风自西向东」的星球（`Amp = 0.08R`、`w = 6°`），在 45° 纬度带取背风侧（东侧 3–5°）与迎风侧（西侧 3–5°），平均降水差 > 0.4。山高必须用 0.08R：3–5° 采样点已落在山腰，0.04R 时只能测出约 0.29。
3. **温度**：极区 < 0.2、赤道 > 0.8。
4. **生物群系一致**：场中每 texel 的（T,P）重新查表必须等于记录的 `Biome`。
5. **尺度不变**：半径减半，四张场逐位相同（气候只依赖角度与比例）。
6. **确定性**：同配方两次烘焙四张场哈希一致。

## 7. 风险

- 雨影回溯是 O(texels × steps) = 393k × 32 ≈ 12.6M 次采样，并行下可接受；把 `ShadowSteps` 调大要留意。
- 归一化温度与上位规格的「摄氏度」口径不同：规格验收写「极区 < 0、赤道 > 0.8」，本设计用归一化 0–1，测试改为「极区 < 0.2、赤道 > 0.8」并记录偏差。
- 风带只按纬度分三段，没有地形绕流；雨影的「逆风」是球面切向直线，靠近极点时步进会偏向经度，属可接受简化。
