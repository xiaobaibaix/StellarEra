# 地质阶段 4c-2：宿主型岩浆矿床 设计

> 上位规格：`2026-11-12-geology-pipeline/04-magmatism.md` §「撒矿」表的后六项。4c-1（硫磺 / 浅成金银 / 斑岩铜）已完成。
> 本步不重新放宿主：六种矿全部读 4b 已放好的岩体原语。唯一新增的岩体是碳酸岩（稀土的宿主）。

## 1. 目标与非目标

**目标**
- 追加岩性「碳酸岩」，并在阶段 4b 的大陆裂谷段放小碳酸岩体。
- 六种宿主型矿：锡钨、伟晶岩锂钽铍、岩浆铜镍、层状铬铂、金刚石、碳酸岩稀土。
- 每种一个撒矿器子类：可开关、可复制、参数带中文注释、进哈希。
- 岩性调试图层与图例覆盖碳酸岩。

**非目标**
- 矽卡岩（缺碳酸盐围岩，跳过）。
- 流体搬运 / 风化富集（阶段 6/7）。
- 不改变 4b 已有的六类岩体与蚀变逻辑。

## 2. 碳酸岩

`GeoLithology` 追加 `Carbonatite = 15`，`Count = 16`，调试色 (200, 180, 210)，显示名「碳酸岩」。
`Stage04b::PlaceIntrusions` 里追加一段（插在金伯利岩之后、蚀变晕之前）：
- 候选：宏观 `ReliefType == 6`（大陆裂谷）且陆上；
- 按 `CarbonatiteSpacingRatio`（默认 0.12）去重，`CarbonatiteChance`（默认 0.35）掷骰；
- `Blob`，半径 `CarbonatiteRadiusRatio`（默认 0.006R），顶深 `CarbonatiteTopDepthRatio`（默认 0.01R），`LithologyId = Carbonatite`，`Hard = 0.02`。
- 参数加进 `FPlanetIntrusionParams` 与配方（`Stage4|Intrusion`），进哈希。
- `IntrusionType` 图层紧凑编号追加「碳酸岩 = 6」，图例 +1 项；`RayHits` 同步。

## 3. 六种矿（规则都 `GetStage() == 4`）

记 `R = Cfg.Radius`。

| 规则 | 宿主 | 位置（宿主本地方向，OrientedBlob 用 `Rotation.Unrotate/Rotate`） | 默认 |
|---|---|---|---|
| 锡钨 | `Granite` | 顶面壳层：局部 z ∈ [0.6, 0.95]·Rz，局部 x/y ∈ [-0.5,0.5]·Rx/Ry；沿岩基长轴（局部 X）拉长的细脉 | 每座 4 条，长半轴 0.02R、半径 0.004R、半厚 0.0015R |
| 伟晶岩 | `Granite` | 边缘壳层：椭球面半径比例 0.88–1.0，随机方向 | 每座 6 个，`Blob` 半径 0.003R |
| 铜镍 | `Gabbro` | 底部：局部 z ∈ [-0.9,-0.5]·Rz，扁平体贴合宿主朝向 | 每个宿主 1 个，横半径 0.7×宿主短半轴、半厚 0.3×宿主 Rz |
| 铬铂 | `LayeredMafic` | 板内薄层：与宿主同中心同朝向，半厚 = 0.4×宿主 Rz | 每片 1 层 |
| 金刚石 | `Kimberlite` | 管内：中心附近小团块 | 每管 1 个，含矿概率 0.15，半径 0.0012R |
| 稀土 | `Carbonatite` | 体内：中心附近 | 每个 1 个，半径 0.6×宿主半径 |

- 每条规则遍历 `Ctx.Fields.Primitives` 中 `LithologyId` 匹配的项；`MineralId == 0` 的原语才是宿主（4c-1 已撒的矿不会再当宿主）。
- 脉体朝向：锡钨用宿主 `Rotation`，局部 X 拉长；其余用宿主 `Rotation`（`Blob` 宿主无 Rotation 时用单位四元数）。
- 全部 `Hard`：脉 0.06、团块 0.1、薄层 0.05。
- 品位走撒矿器公共 `GradeMin/GradeMax`。

## 4. 参数

每个撒矿器自己的 `Rule` 分类，带中文注释，全部进 `HashParams()`：

| 撒矿器 | 参数（默认） |
|---|---|
| 锡钨 | `PerBatholith` 4 / `LengthRatio` 0.02 / `RadiusRatio` 0.004 / `ThicknessRatio` 0.0015 / `TopMinRatio` 0.6 / `TopMaxRatio` 0.95 |
| 伟晶岩 | `PerBatholith` 6 / `RadiusRatio` 0.003 / `ShellMin` 0.88 / `ShellMax` 1.0 |
| 铜镍 | `PerHost` 1 / `WidthScale` 0.7 / `ThicknessScale` 0.3 |
| 铬铂 | `PerSlab` 1 / `ThicknessScale` 0.4 |
| 金刚石 | `PerPipe` 1 / `RadiusRatio` 0.0012 / `FertileChance` 0.15 |
| 稀土 | `PerBody` 1 / `RadiusScale` 0.6 |

碳酸岩放置参数进 `FPlanetIntrusionParams`：`bCarbonatites` / `CarbonatiteRadiusRatio` / `CarbonatiteTopDepthRatio` / `CarbonatiteChance` / `CarbonatiteSpacingRatio`。

## 5. 测试（`Src/Tests/PlanetGeoStage04c2Tests.cpp`）

1. **宿主正确**：每种矿的每个体，到最近对应岩性原语的归一化距离（换算到宿主局部空间）≤ 1.05；不存在没有宿主的矿体。
2. **位置正确**：锡钨在宿主顶面壳层（局部 z ∈ [0.5,1.0]）；伟晶岩在边缘壳层（0.8–1.05）；铜镍在底部（局部 z ∈ [-1,-0.4]）；铬铂与宿主同中心同朝向且半厚 ≤ 0.5 宿主 Rz；金刚石在金伯利岩管内；稀土在碳酸岩内。
3. **金刚石含矿率**：含矿管数 / 总管数 ∈ [0.05, 0.3]（默认 0.15，管数少时放宽下限）。
4. **碳酸岩**：全部在 `ReliefType==6` 且 `Elevation>0`；半径减半后数量不变。
5. **开关**：六个撒矿器逐个去掉后对应矿为 0；两次烘焙哈希一致。
6. **目录**：六种矿名可解析（含碳酸岩）。
7. **岩性表**：`Carbonatite` 的显示名与调试色存在，`Count` 增加一位。

## 6. 风险

- 岩基是 `OrientedBlob`，局部 z 是竖直半径；顶面壳层用局部 z 取比例，宿主倾斜时“顶面”跟着倾斜——这正是想要的（岩基顶近水平）。
- `Blob` 宿主（伟晶岩/铜镍可能遇到 `Blob` 岩床）没有 Rotation，用单位四元数即可。
- 金刚石管半径 0.0012R ≈ 48 体素，深度显示要放大才能看到（查看器已有矿物放大倍数）。
- 碳酸岩只在大陆裂谷，某些种子裂谷段很少：测试若数量为 0 先打印候选统计，不擅自改阈值。
