# 地质阶段 6b：出露结算 设计

> 上位规格：`2026-11-12-geology-pipeline/06-erosion.md`「出露结算」。阶段 6 拆三步：6a 水系与剥蚀（已完成）、6b 出露结算（本文）、6c 沉积矿床。
> 本步把 6a 算出的剥蚀深度作用到阶段 4 放下的岩体与矿体上，让「剥得深的地方露出深部单元」。

## 1. 目标与非目标

**目标**
- 新增 `FStage06bExposure`，读 6a 的 `ErosionDepth`，把岩体与矿体沿径向往上抬。
- 原语新增两个字段：`Exposure`（0–255 出露比例）与 `RawDepthBefore`（抬升前离地表多深，体素）。
- 抬升受三条硬线约束（不高于地表、随宿主、只抬不改形）。
- 注册两个调试图层；参数进配方 `Stage6b|Exposure`、带中文注释、进哈希。

**非目标**
- 不改高程（剥蚀在 6a 已经体现为现今地表）。
- 不做出露后的矿物学变化（氧化铁帽、次生富集）——属阶段 7。
- 不搬动火山点表（火山是地表地貌，不是埋深物体）。

## 2. 阶段号与管线位置

- 阶段 7 尚未实现，因此 6b 用**阶段号 7**，`FStage06bExposure` 注册在阶段 6 之后。
- 将来实现风化壳阶段时用阶段号 8；「阶段 7 风化壳」的文档编号顺延。
- 读写：读 `E PlanetField::Hydro | Primitives | Elevation`，写 `EPlanetField::Exposure`（新位）。

## 3. 抬升规则

记 `D = ErosionDepth(dir)`（体素，6a 的宏观场，按方向双线性采样），`R = Cfg.Radius`。

1. **抬升量**：`Lift = clamp(LiftScale · D, 0, MaxLiftRatio · R)`。
2. **宿主先算**：岩体（`LithologyId != 0 && MineralId == 0`）按自身中心方向的 `D` 算 `Lift`；**矿体（`MineralId != 0`）与蚀变晕（`MineralId == 0 && LithologyId == 0 && ClueId != 0`）使用宿主岩体的 `Lift`**——宿主按「原语中心落在其椭球内、取归一化距离最小者（平手取编号小者）」匹配；找不到宿主退回自身方向的 `D`。晕描述的就是宿主本身，分开抬会让晕留在原地描述几百体素外的空气。
3. **平移**：岩体沿自身抬升前方向平移 `Center += dir · Lift`（`dir` 归一化）；矿体与蚀变晕**复制宿主的整条位移向量**（含宿主已被第 4 条收回的部分）——只抄模长会因两者中心方向不同而相对错位。
4. **不高于地表**：抬升后沿 `dir` 的最高点 `Top = |Center| + Support(dir)`（椭球支撑函数），地表半径 `S = R0 + Elevation(dir)`；若 `Top > S`，把 `Center` 沿 `dir` 收回 `Top − S`，并把 `Exposure` 记满。
5. **出露比例**：`Exposure = clamp(|Δ| / max(2 · Support(dir), 1), 0, 1)`，`Δ` 是该原语的实际位移（岩体 = `dir · Lift`，跟随者 = 宿主位移向量）；被第 4 条收回的记 1。`RawDepthBefore = S − (|Center_before| + Support(dir))`。

形状、朝向、品位、`Hard` 不变；`Bounds` 只平移中心。

## 4. 数据模型

`FGeoPrimitive` 追加（放在 `ClueStrength` 之后）：

```cpp
uint8 Exposure = 0;         // 0 未出露 … 255 完全出露
float RawDepthBefore = 0.f; // 抬升前原语最高点离地表多深（体素），可为负（本来就露头）
```

两者进 `FGeoPrimitiveIndex::Hash()`。

## 5. 参数（配方 `Stage6b|Exposure`）

| 参数 | 默认 | 含义 |
|---|---|---|
| `bExposure` | true | 是否做出露结算 |
| `LiftScale` | 1.0 | 抬升量占剥蚀深度的比例（1 = 完全抬升） |
| `MaxLiftRatio` | 0.02 | 单个原语最大抬升（占 R），防止极端剥蚀把矿整体顶到地表 |

## 6. 调试图层

- `Exposure`：连续 0–1（黑→白），Sample 取该方向最近的已标记原语的 `Exposure`；无原语返回 0。
- `ExposedOre`：分类三档（0 未出露 / 1 部分出露 / 2 完全出露），只在 `MineralId != 0` 的原语上非零。

两者由原语派生，图层直接读快照，`ExportDebugFields` 不新增场。

## 7. 测试（`Src/Tests/PlanetGeoStage06bTests.cpp`）

1. **不高于地表**：全部原语抬升后 `Top ≤ S + 1` 体素。
2. **随宿主**：每个矿体、每个蚀变晕与其宿主的位移相差 ≤ 1 体素（匹配失败的数量分别打印，不算违规）。
3. **剥蚀为零则不动**：`D ≈ 0` 处原语中心位移 ≤ 0.5 体素且 `Exposure == 0`。
4. **山带出露更强**：`MountainMask > 0.5` 区域原语平均 `Exposure` > `MountainMask < 0.1` 的 2 倍。
5. **形状不变**：所有原语 `Radii`、`Rotation`、`Hard`、`MineralId`、`Grade` 与阶段 6a 结束时逐位相同。
6. **开关与确定性**：`bExposure=false` 时零位移；同配方两次烘焙 `Primitives.Hash()` 一致；并行/单线程一致。
7. **有可见出露**：`Exposure > 0.5` 的矿体数 > 0，打印总数与最大值。
8. 配方哈希探针（三个参数）+ 全字段透传（非默认值）。

## 8. 风险

- **宿主匹配**：矿体对岩体按椭球归一化距离最小匹配，O(矿 × 岩体)；默认约 700 × 300，可接受。
- **出露比例的分母**用 `2·Support(dir)` 近似体厚度；对极扁矿脉偏乐观，属可接受近似。
- **原语位置变化会改快照哈希**，既有体素存档需重烘（与之前新增阶段相同）。
- **抬升上限**：`MaxLiftRatio` 默认 0.02R（800 体素）。6a 的剥蚀深度理论上限约 0.028R（`ErosionRatio·2 + CarveMaxRatio`），所以上限只在最陡最湿的山带才会真正生效；它是安全网，不是常态。若调大剥蚀参数，需同步检查这个上限。
