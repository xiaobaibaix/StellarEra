# 地质阶段 4a：火山地形 设计

> 上位规格：`2026-11-12-geology-pipeline/04-magmatism.md`。阶段 4 拆成三步，本文只管 4a。
> 4b 地下侵入体、4c 岩浆矿（竖向矿柱等）各自另写规格，都读本步的火山点表。

## 1. 目标与非目标

**目标**
- 在阶段 3 的最终高程上叠加五种火山：层状火山、盾状火山、溢流玄武岩台地、破火山口、火山渣锥。
- 输出火山点表，供 4b / 4c 定位。
- 所有尺寸按半径比例；所有参数可在配方里调、带中文注释。
- 注册调试图层，查看器与导出无需改动。

**非目标**
- 不建 3D 原语、不做倒悬 / 熔岩洞 / 岩浆房 / 火山通道（通道类放 4b 侵入体）。
- 不撒矿（4c）。
- 不做真实侵蚀；老火山只做「削低 + 放缓」的简化版，真实剥蚀留阶段 6。

## 2. 与上位规格的差异

| 上位规格 | 本步 | 原因 |
|---|---|---|
| 尺寸按 R=500 的「格」 | 全部换成占 R 的比例 | 半径可变（用户硬约束） |
| 热点来自阶段 1 `DeepBlobs` | 阶段 4 自己按种子生成地幔柱热点 | 阶段 1 没有这个字段 |
| 火山是 3D 基元，锥并 / 口差 | 只改 2D 高程场 | R=40000 时一格约 61 体素，锥底约 30 格、火口约 5 格，高程场足够；体素求值与值域剔除不用改 |

## 3. 结构

- 新文件 `Geo/Bake/Stages/Stage04Magmatism.{h,cpp}`，类 `FStage04Magmatism : IPlanetStage`，
  `FPlanetBakePipeline::MakeDefault()` 里排在阶段 3 之后。
- 只读前阶段：`PlateId` `BoundaryType` `BoundaryDistance` `Convergence` `ContinentalMask`
  `ReliefType` `MountainMask` `Elevation`，以及板块框架（欧拉极）。
- 输出（加到 `FPlanetBakeFields`）：
  - `TArray<FGeoVolcano> Volcanoes`
  - `FGeoField2D VolcanoMask`（0 无 … 1 火山体中心，MacroRes）
  - `FGeoField2D VolcanicType`（类型编号，MacroRes）
  - `FGeoField2D VolcanicAge`（0 新 … 1 老，MacroRes）
  - `Elevation` 就地加上火山增量。

```cpp
enum class EGeoVolcanoKind : uint8 { Strato = 1, Shield, FloodBasalt, Caldera, CinderCone };

struct FGeoVolcano
{
	FVector3f Dir;          // 单位方向
	EGeoVolcanoKind Kind;
	float BaseRadius;       // 方向单位（弧度近似）
	float Height;           // 体素
	float CraterRadius;     // 方向单位；0 = 无火口
	float CraterDepth;      // 体素
	float Age;              // 0 新 … 1 老
	bool bActive;
	int32 Plate;            // 所在板块
};
```

`FGeoVolcano` 进快照（4b/4c 及运行期矿物求值要读），也进哈希。

## 4. 火山的位置

### 4.1 层状火山（俯冲弧）
- 候选：汇聚边界（类型 1/2）上盘一侧，离边界 `ArcOffsetRatio`（默认 0.03）。
  上盘 = 大陆遮蔽更大的一侧；洋洋俯冲取板块编号确定的一侧（确定性）。
- 沿弧按 `ArcSpacingRatio`（默认 0.04）做泊松盘采样，间距抖动 ±30%。
- 汇聚速度越大越可能活跃；`ArcActiveFraction`（默认 0.5）控制总体比例。
- 落在现今山带上时，锥体叠在山体之上（取 max 叠加，不是相加），不与山脉错位。

### 4.2 盾状火山（热点链）
- `HotspotCount`（默认 4）个热点，种子均匀撒在球面。
- 对每个热点：所在板块的欧拉极 Ω 给出板块在热点处的运动方向；
  沿板块运动方向（火山被板块带走）每隔 `HotspotSpacingRatio`（默认 0.06）
  放一座，最多 `HotspotChainLength` 座（默认 6），越界到别的板块就截断。
- 链首在热点正上方，年龄 0、活跃；往后线性变老，只有前两座可活跃。
- 洋底的盾状火山可长出海面成为火山岛。

### 4.3 溢流玄武岩
- 大陆裂谷（`ReliefType`=6）处，按 `FloodBasaltChance`（默认 0.3）每段裂谷出一片。
- 形状：平顶台地，边缘陡坎，外轮廓用噪声扰动的圆。

### 4.4 破火山口
- 从层状 + 盾状火山中按 `CalderaFraction`（默认 0.15）抽，且只抽底径大于中位数的。
- 被抽中的火山顶部换成环形洼地 + 外环壁，不再有普通火口。

### 4.5 火山渣锥
- 每座活跃大火山周围 `CinderPerVolcano`（默认 4）个，距中心 1–2 个底半径。

## 5. 形状（高程增量）

记 d = 到火山中心的角距离 / 底半径，c = 火口半径 / 底半径。

| 类型 | 剖面 |
|---|---|
| 层状 | `H·(1−d)^1.6`（陡、凹形坡）；火口内 `−CraterDepth·(1−(d/c)²)` |
| 盾状 | `H·(1−d²)^1.5`（宽缓穹顶），顶部小火口 |
| 溢流 | 平顶 `H`，边缘 `smoothstep` 陡坎，宽为 0.1 个底半径 |
| 破火山口 | 原锥剖面，顶部 c 内压到环壁高 − Depth，外沿一圈环壁 |
| 渣锥 | `H·(1−d)`，小火口 |

- 每座再乘一个低频噪声（±15%）让坡面不规整，加 2–3 条放射状沟（角向噪声）。
- 老火山（Age>0.5，陆上）：高度 × (1 − 0.5·Age)，剖面指数变小（坡更缓），火口变浅。
- 叠加规则：火山之间及火山与阶段 3 地形之间取 max（相邻锥不会叠出双倍高），火口差集最后统一减。
- 陆地占比约束：只有洋底火山允许露出海面；陆上火山不会把陆地压到海平面下。

## 6. 参数（配方 `Stage4|Volcano`）

每项 UPROPERTY 带中文注释，`ToConfig` 透传，进 `FPlanetRecipeConfig::Hash()`。

| 参数 | 默认 | 含义 |
|---|---|---|
| `bArcVolcanoes` / `bHotspots` / `bFloodBasalt` / `bCalderas` / `bCinderCones` | true | 各类开关 |
| `StratoBaseRatio` / `StratoHeightRatio` / `StratoCraterRatio` | 0.024 / 0.012 / 0.004 | 层状火山底半径 / 高 / 火口半径 |
| `CraterDepthRatio` | 0.002 | 火口深 |
| `ArcSpacingRatio` / `ArcOffsetRatio` / `ArcActiveFraction` | 0.04 / 0.03 / 0.5 | 弧火山间距 / 离边界距离 / 活跃比例 |
| `ShieldBaseRatio` / `ShieldHeightRatio` | 0.04 / 0.008 | 盾状火山底半径 / 高 |
| `HotspotCount` / `HotspotSpacingRatio` / `HotspotChainLength` | 4 / 0.06 / 6 | 热点数 / 链间距 / 链长 |
| `FloodBasaltChance` / `FloodBasaltSizeRatio` / `FloodBasaltHeightRatio` | 0.3 / 0.06 / 0.003 | 溢流玄武岩出现概率 / 半宽 / 高 |
| `CalderaFraction` / `CalderaRadiusRatio` / `CalderaDepthRatio` | 0.15 / 0.016 / 0.008 | 破火山口比例 / 半径 / 深 |
| `CinderPerVolcano` / `CinderBaseRatio` / `CinderHeightRatio` | 4 / 0.006 / 0.002 | 渣锥个数 / 底半径 / 高 |

尺寸统一按半径给，所以数值是讨论时直径数值的一半。

## 7. 调试图层

`DescribeDebugLayers` 注册，`ExportDebugFields` 导出：
- `VolcanicType`：类别色 + 图例（层状 / 盾状 / 溢流 / 破火山口 / 渣锥）。
- `VolcanoActive`：活跃 / 休眠。
- `VolcanicAge`：0–1 渐变。
- `VolcanoDelta`：阶段 4 的高程增量。

`StellarEra.Planet.Geo.Debug.Registry` 自动覆盖「阶段 4 已登记图层」。

## 8. 测试（`Src/Tests/PlanetGeoStage04Tests.cpp`）

1. **弧火山**：每段长度 ≥ 3 个间距的汇聚边界都有层状火山；最近邻间距落在 `[0.5, 1.6] × ArcSpacingRatio`；全部在上盘一侧。
2. **热点链**：链上年龄单调递增；相邻火山连线方向与所在板块运动方向夹角 < 60°。
3. **火口**：每座带火口的火山，火口底高程 < 火口边缘高程。
4. **活跃**：所有活跃火山都是层状 / 链上前两座盾状 / 其周围渣锥。
5. **陆地占比**：开 / 关阶段 4 的陆地占比差 < 0.01；任何 texel 的高程不低于阶段 3 的高程减火口深。
6. **确定性**：同配方烘两次，火山点表逐项相等、哈希相等。
7. **尺度不变**：半径减半，火山数量相同，`BaseRadius`（方向单位）相同，`Height` 减半。
8. **开关**：每个 `bXxx=false` 时对应类型数量为 0。
9. 配方哈希探针：新参数改动都改变哈希（加进 `PlanetGeoRecipeTests.cpp`）。

## 9. 风险

- 弧火山叠在高山上取 max，可能被山体吞掉看不见 → 测试 1 打印「露出山体」的比例，必要时改成「山上相加、平地 max」。
- 热点处板块速度近 0 时链方向不定 → 速度 < SpeedMin 一半的热点只放一座。
- 阶段 4 改 `Elevation` 后，`ReliefType` 语义定为「阶段 3 宏观地形」，火山另看 `VolcanicType`。
