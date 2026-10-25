# 行星地质数据

纯文本 JSON，由 `FPlanetGeologyField` 在运行时读取（编辑器与打包后同一套路径：
`FPaths::ProjectContentDir()/Geology/...`）。不是资产，不走内容浏览器。

## 文件

- `Elements.json`：全局元素表。所有行星共享。
- `Planets/<preset>.json`：每颗行星的丰度、层位与外观。行星 Actor 的
  `Planet|Geology|GeologyPreset` 填文件名（不含 `.json`），空 = 没有矿。

## 元素（Elements.json）

| 字段 | 说明 |
|---|---|
| `id` | 唯一非空；行星 JSON 用它引用元素 |
| `displayName` | 保留给未来 UI/日志；当前代码不读取 |
| `color` | `[r,g,b]`，0~1，高纯度时的本色 |
| `noise.frequency` | 单位方向上的噪声频率，越大矿脉越碎 |
| `noise.octaves` | 1~6，分形层数，性能旋钮 |
| `noise.lacunarity` / `noise.gain` | 每层频率倍数 / 振幅衰减 |
| `noise.seed` | 不同元素用不同种子，分布互不相关 |
| `noise.depthScale` | (0,1]，深度混入噪声的强度；太小矿会退化成均匀壳 |
| `noise.veinThreshold` | [0,0.95]，矿脉稀疏门。噪声 0.5 是均值，阈值越高只有峰值附近留矿；0 = 旧行为（满层） |

## 行星（Planets/*.json）

| 字段 | 默认 | 说明 |
|---|---|---|
| `geometryAmplitude` | 1.5 | 矿脉让地表外凸的最大体素数；0 = 不影响几何 |
| `appearance.colorThreshold` | 0.15 | 最强元素自身强度（`DominantNorm`）的显现阈值 |
| `appearance.fadeRange` | 0.45 | 从阈值到完全显现的过渡宽度 |
| `appearance.purityForFullColor` | 0.7 | 纯度混合系数 `PurityMix = saturate((Purity - (1 - p)) / p)`：`Purity ≤ 1 - p` 时不用矿色，线性升高到 `Purity = 1` 才完全用矿色 |
| `elements.<id>.abundance` | 0 | 全局丰度，0~1 |
| `elements.<id>.peakDepth` | — | 峰值深度，体素；**正 = 基准球面以下（向行星内部为正）**，缺省 0 |
| `elements.<id>.layerThickness` | — | 高斯层厚，体素，必须 > 0 |

未在行星 JSON 中出现的元素 = 这颗行星没有该元素。

### 稀疏矿脉与显色

每个元素先在自己的噪声上过一道矿脉门：

```
Vein    = saturate((noise01 - veinThreshold) / (1 - veinThreshold))
raw_i   = abundance_i * 高斯(depth) * Vein
```

阈值以上才留矿。各元素种子、频率、门限都不同，同一个地点通常只有一种元素在门内，
`Purity = max(raw_i) / Σ raw_i` 于是能接近 1，颜色是元素本色而不是六种矿各占一份的灰。

显色判定看 `DominantNorm = max_i(高斯_i × Vein_i)`，**与丰度无关**：
铀的丰度只有 0.10，在它自己的矿脉里也能到 1 并满色显示。若改用总含量
（`Total = Σ raw_i`，会被丰度和稀释），低丰度元素无论埋得多准都过不了阈值。

### 地表可见性

`peakDepth` 是**绝对体素深度**，而某个地表点的地质深度约为
`-terrainAmplitude × Offset`（低地为正）。因此近地表矿层能不能露头取决于地形振幅。

`earth` 预设的六个层位是按 shipped 的 `DA_Terrain_Earth`（Amplitude=200）实测的
地表深度分布排的：中位表面约 +1.5 体素，四分位低地约 +25，最深的低地约 +70。
所以 silicon(+5) 到处都是、water(+10) 只在低地、iron(+30) 约四分之一地表可见、
copper(+50) 只在深洼、titanium(+78) 几乎只见于最深处、uranium(+130) 只能挖到。
若把 `earth` 挂到小振幅地形（例如模块默认的 8），自然地表会几乎看不到矿色，
通常要向下挖 20+ 体素才出现——这不是地质加载失败。

### 数值与收敛约束

- `geometryAmplitude` 会被夹到 `[0, 64]`；`layerThickness` 必须是有限正数并被夹到
  `(0, 10000]`；`peakDepth` 夹到 `[-10000, 10000]`。越界会在日志里留下 Warning。
- 地表查询用两次不动点迭代 `d ← D0 - δ(d)`，收缩条件是
  `|∂δ/∂depth| < 1`。`geometryAmplitude / min(layerThickness)` 应**远小于 1**：
  shipped 内容最薄的层是 water 的 8 体素，`1.5 / 8 = 0.19`；该比值接近或超过约 1 时
  迭代可能不收敛，角色会站在与地形表面不一致的半径上。

## 改完怎么生效

编辑器控制台执行：

```
StellarEra.ReloadGeology
```

会重新读盘并重建场景中所有体素行星。注意：重建会让已有的手工挖掘编辑与新地形
不再对齐（与改地形参数的行为一致）。
