# 地质阶段 4b：地下侵入体与蚀变 设计

> 上位规格：`2026-11-12-geology-pipeline/04-magmatism.md` §「地下侵入体」「蚀变分带」。阶段 4 拆三步，本文是 4b；4a（火山地形）已完成，4c（岩浆矿）另写。
> 4b 读 4a 的火山点表与阶段 3 的地形 / 板块场，产出岩体原语与材质线索。

## 1. 目标与非目标

**目标**
- 在弧火山、裂谷、克拉通下方放六类侵入体：岩基、岩株、岩墙、岩床、层状侵入体、金伯利岩管。
- 切面按岩石类型上色（新增 6 种火成岩岩性）。
- 每座岩株 / 岩基顶部挂蚀变晕，四圈线索写进材质 A 通道（只写材质，不建几何）。
- 所有尺寸按半径比例；参数可在配方 `Stage4|Intrusion` 调整、带中文注释。
- 注册三个调试图层，查看器与导出不用改。

**非目标**
- 不撒矿（4c）。
- 不做岩浆房、火山通道、熔岩洞。
- 不做剥蚀出露（阶段 6 结算）：本阶段只按「地表以下的深度」摆，不读 `ErosionDepth`。
- 金伯利岩管不做「胡萝卜形」，用竖长椭球（原规格已把 2 格直径判为做不成管）。

## 2. 与上位规格的差异

| 上位规格 | 本步 | 原因 |
|---|---|---|
| 尺寸按 R=500 的格 | 全部换成占 R 的比例：1 格 = 0.002R（R=40000 时 80 体素） | 半径可变 |
| 层状侵入体内部分规整水平层 | 3–5 片薄板叠置 | 现成原语只有椭球/朝向椭球 |
| 蚀变「由到侵入体距离 × 强度分段」 | 用独立「蚀变晕原语」承载线索 | 原语查询按包围盒，晕比岩体大，单独一个原语最省事 |

## 3. 数据模型

### 3.1 岩性

`GeoLithology` 追加（编号接在 `OceanicCrust` 之后，已有编号不变）：

| Id | 名称 | 调试色 |
|---|---|---|
| 9 | 花岗岩 | (190, 120, 140) |
| 10 | 闪长岩 | (150, 140, 150) |
| 11 | 辉长岩 | (90, 100, 110) |
| 12 | 橄榄岩 | (60, 110, 80) |
| 13 | 金伯利岩 | (110, 60, 130) |
| 14 | 层状镁铁岩 | (70, 80, 90) |

`Count` 改为 15；`DisplayName` / `DebugColor` 补齐。

### 3.2 原语扩展

`FGeoPrimitive` 追加两个字节（放在 `LithologyId` 旁）：

```cpp
uint8 ClueId = 0;        // GeoClue，0 = 无
uint8 ClueStrength = 0;  // 0..15
```

`FGeoPrimitiveIndex::Hash()` 把这两个字节并进已有的打包整数（`<< 40` / `<< 48`）。

新增：

```cpp
namespace GeoClue
{
	constexpr uint8 None = 0, Keratitic = 1, Sericitic = 2, Argillic = 3, Propylitic = 4, Count = 5;
	inline const TCHAR* DisplayName(uint8 Id);
	inline FColor DebugColor(uint8 Id);
}
/** 归一化椭球距离：体内 < 1，中心 0；蚀变按它分圈。 */
FORCEINLINE float GeoPrimitiveDistance(const FGeoPrimitive& P, const FVector3f& X);
```

### 3.3 材质求值（`PlanetMaterialEval::Sample`）

现有那次 BVH 查询里按优先级累加，不增加查询次数：

1. **矿体**：保持现状（强度 > 0.5 取最强；矿优先）。
2. **岩性覆盖**：`LithologyId != 0` 且 `GeoPrimitiveDistance < 1` 的原语中取距离最小者覆盖 `Out.Lithology`。矿体命中的点也照样覆盖岩性，因为岩体内部本来就没有沉积地层。
3. **蚀变**：`ClueId != 0` 且距离 < 1 的原语中取 `ClueStrength` 最大者写 `Out.Clue/ClueStrength`。

`LithologyId == 0 && ClueId == 0` 的原语（现阶段的矿）行为与今天完全一致。

### 3.4 阶段 1 的岩性图层

`Stage01Sphere` 的 `Lithology` 图层把 `Id >= GeoLithology::ContinentalCrust` 一律映射到 `Crust`：新增火成岩编号不会让地壳切面出现杂色，岩体另看 4b 的图层。

## 4. 放置

统一记法：`S = R0 + Elevation(dir)` 为该方向地表半径（体素）；`Top` 为顶深（占 R），则岩体中心在 `S − (Top + 竖半径) × R` 处（竖半径占 R）。沿 `dir` 摆放，跟着球面走。

| 类型 | 位置与数量 | 形状与原语 | 岩性 |
|---|---|---|---|
| 岩基 | 同一俯冲对（`Plate` + `OtherPlate` 相同）的弧火山按最近邻聚成段；段内 ≥ `BatholithMinVolcanoes` 座放 1 个，中心 = 段质心，长轴沿段主轴（首尾连线） | `OrientedBlob`，Radii (0.08, 0.04, 0.03)R，顶深 0.05R | 花岗岩 |
| 岩株 | 每座弧火山正下方 1 个；顶 = 岩基顶 | `Blob`，半径 0.008R（0.7–1.3 抖动），顶深 0.03R | 闪长岩 |
| 岩墙 | 每条裂谷段（`ReliefType==6` 聚段）2 条；从顶深 0.02R 竖直打到 0.06R | `OrientedBlob`，Radii (0.002, 0.002, 0.02)R，竖直 | 辉长岩 |
| 岩床 | 每片溢流玄武岩台地下 1 片；另在古老克拉通（Age>0.85、M>0.6）按密度撒 | `OrientedBlob`，Radii (0.05, 0.05, 0.002)R，水平 | 辉长岩 |
| 层状侵入体 | 古老克拉通，每块候选 1 组 | 3–5 片叠置，每片 Radii (0.03, 0.03, 0.0025)R，层间距 0.004R，整体顶深 0.06R | 层状镁铁岩 |
| 金伯利岩管 | 古老克拉通、远离山带（`MountainMask < 0.2`）、陆上；全局最多 8 根，泊松盘间距 0.05R | `OrientedBlob`，Radii (0.002, 0.002, 0.01)R，竖直，顶深 0.004R | 金伯利岩 |

- 随机流：`FGeoRng::Derive(Cfg.Seed, 4, GeoHash::Str("intrusion"))` 一条，每类再 `Derive(…, GeoHash::Str("<kind>"))`，关掉一类不影响其它类。
- 侵入体一律追加到 `Fields.Primitives`；阶段 4 的 `GetWrites` 加 `EPlanetField::Primitives`。
- 都要求 `LithologyId` 非 0、`Hard = 0.02`（硬接触），`MineralId = 0`。

## 5. 蚀变晕

- 每个岩株挂 1 个晕：中心同岩株，Radii = 岩株半径 × 2.2。
- 每个岩基挂 1 个晕：中心同岩基，Radii × 1.25。
- 分圈（归一化距离 D）：`D<0.3` 钾化、`<0.55` 绢英岩化、`<0.8` 泥化、`<1` 青磐岩化。
- 强度按圈递减：15 / 11 / 7 / 3。
- 晕原语 `LithologyId=0 && MineralId=0`，只带 `ClueId/ClueStrength`；材质求值直接读它自己的 D，不需要知道围岩距离。

## 6. 参数（配方 `Stage4|Intrusion`）

| 参数 | 默认 | 含义 |
|---|---|---|
| `bBatholiths` / `bStocks` / `bDikes` / `bSills` / `bLayeredIntrusions` / `bKimberlites` / `bAlteration` | true | 各类开关 |
| `BatholithLongRatio` / `BatholithShortRatio` / `BatholithThickRatio` / `BatholithTopDepthRatio` | 0.08 / 0.04 / 0.03 / 0.05 | 岩基长 / 短 / 竖半径、顶深 |
| `BatholithMinVolcanoes` | 3 | 段内至少几座弧火山才长岩基 |
| `StockRadiusRatio` / `StockTopDepthRatio` | 0.008 / 0.03 | 岩株半径、顶深 |
| `DikePerRift` / `DikeThicknessRatio` / `DikeLengthRatio` / `DikeTopDepthRatio` | 2 / 0.002 / 0.02 / 0.02 | 每条裂谷岩墙数、厚、半高、顶深 |
| `SillRadiusRatio` / `SillThicknessRatio` / `SillTopDepthRatio` / `CratonSillDensity` | 0.05 / 0.002 / 0.02 / 0.4 | 岩床半径、厚、顶深、克拉通撒布密度 |
| `LayeredCount` / `LayeredRadiusRatio` / `LayeredThicknessRatio` / `LayeredGapRatio` / `LayeredTopDepthRatio` | 4 / 0.03 / 0.0025 / 0.004 / 0.06 | 层数、半径、单层厚、层间距、顶深 |
| `KimberliteRadiusRatio` / `KimberliteHalfHeightRatio` / `KimberliteTopDepthRatio` / `KimberliteMax` | 0.002 / 0.01 / 0.004 / 8 | 管半径、半高、顶深、最多几根 |
| `AlterHaloScale` / `AlterKeratiticBand` / `AlterSericiticBand` / `AlterArgillicBand` | 2.2 / 0.3 / 0.55 / 0.8 | 晕半径倍数、前三圈内边界（第四圈到晕边界） |

全部进 `FPlanetRecipeConfig::Hash()`；`ToConfig` 透传。

## 7. 调试图层

- `IntrusionType`：按岩性类别色 + 图例（六种），Sample = 用快照原语查询该点岩性，非火成岩返回 0。
- `AlterationClue`：分类 + 图例（四圈）。
- `AlterationStrength`：0–15 连续。

查看器的 3D 切面、剖切、导出自动覆盖。

## 8. 测试（`Src/Tests/PlanetGeoStage04bTests.cpp`）

1. **岩性覆盖**：合成花岗岩 `Blob`，中心 `Sample().Lithology == Granite`，体外 1.5 倍半径处恢复地壳。
2. **蚀变分圈**：合成晕原语，沿半径取 4 个点落在四圈，`ClueId` 顺序正确、`ClueStrength` 单调递减。
3. **矿优先**：矿体与岩体同时命中时 `Mineral` 仍被写出，岩性也被覆盖。
4. **弧必有岩基**：每段 ≥ `BatholithMinVolcanoes` 座的弧火山下方都有岩基；岩株数 = 弧火山数。
5. **深度**：所有侵入体顶深 ≥ 0.01R；金伯利岩只出现在 `CrustAge>0.85`、`M>0.6`、`MountainMask<0.2` 的陆上点。
6. **尺度不变**：半径减半，各类数量不变，方向单位半径不变，体素尺寸与深度减半。
7. **确定性**：同配方两次烘焙 `Primitives.Hash()` 相等；并行 / 单线程 `ContentHash` 相等。
8. **开关**：七类逐个关掉后对应岩性 / 线索数量为 0。
9. **哈希探针**：新参数改动都改变配方哈希。
10. `Debug.Registry` 自动覆盖 4b 三个图层。

## 9. 风险

- 岩基半径 0.08R 椭球在曲面上很长；两段弧靠太近时岩基会相交。相交不算错（同成分岩体合并），不另做去重。
- 蚀变晕 ×2.2 在浅部会伸到地表以上：材质只在体素内求值，伸出的部分无害；测试只查地下点的分圈。
- 原语数量：弧数十 + 岩墙十余 + 岩床 + 岩株 + 层状 4×N + 晕，默认在 300–600 量级，BVH 与快照大小可接受。
