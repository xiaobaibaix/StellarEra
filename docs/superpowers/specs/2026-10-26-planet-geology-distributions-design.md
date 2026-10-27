# 行星矿物：可叠加的分布生成器

- **日期**：2026-10-26
- **状态**：设计已确认，待写实施计划
- **影响模块**：`StellarEraGeology`、`StellarEraMinerals`、`StellarEraTerrain`（仅测试）、`Config/`、`Content/Geology/`
- **取代**：`2026-10-25-planet-mineral-pods-design.md` 中"所有元素共用同一种矿囊模型"的决策
- **保留**：该 spec 的其余决策（资产唯一数据源、观测视图按矿囊局部建面、几何与颜色共用同一份场）

---

## 1. 目标与边界

### 1.1 问题

现有模型里所有矿物都只有一种分布形态：近球形 3D 矿囊。真实矿床的形态因元素而异——煤成片层、铁铜多沿脉穿切地层、低丰度矿常呈稀疏散布。单一模型让六种元素看起来像同一批东西换了颜色。

### 1.2 目标

1. 每种元素可以选择**多个分布生成器**，按取最大值合成；
2. 第一版提供三种新形态：**片层 / 透镜**、**脉状 / 穿切地层**、**浸染 / 稀疏散布**；
3. 生成器是**多态类**：以后加一种新形态只新增一个类，不改地质场、地形、观测视图；
4. 观测视图继续用**局部栅格**（分辨率跟随特征尺度，而不是行星直径）；
5. 旧资产自动迁移成"囊状"生成器，旧行星的矿囊位置不变。

### 1.3 非目标

| 不做 | 原因 / 后续 |
|---|---|
| 手绘 / 笔刷式矿脉编辑 | 分布仍是参数化程序生成；要手绘需要另一套数据模型 |
| 无限延伸的全局噪声场（整颗行星连续层） | 会让观测视图失去有界区域；本期所有形态都必须是有界基元云 |
| 生成器之间的权重混合（相加后 clamp） | 已确认取最大值；加权和会改变 Norm 的归一化语义 |
| 按经纬度 / 半球限定出现区域 | YAGNI；需要时作为新的生成器子类 |
| 半透明重叠显示 | 仍用图例关掉其他元素来观察穿插 |
| 预设改动自动重刷已烘焙体素 | 体素是烘焙的，仍需显式"重建行星" |

### 1.4 已确认决策

| # | 决策 | 取值 | 理由 |
|---|---|---|---|
| 1 | 扩展方式 | **多态 UObject 子类 + 元素可叠加多个** | 新形态只加一个类；叠加让同元素能同时有底层与富集带 |
| 2 | 运行期架构 | **每个生成器全自定义**（统一契约，各自实现求值、区域枚举、加速结构） | 自由度最高；代价是每个形态自带空间索引 |
| 3 | 合成语义 | **每个元素取各生成器 Norm 的最大值** | 与现有"多囊取 max"一致；重叠不饱和，阈值/纯度/几何公式不变 |
| 4 | 噪声参数归属 | **跟生成器走** | 同一元素的片层与脉状可以分别控制破碎度与稀疏度 |
| 5 | 片层几何 | **有限大的有向透镜（扁椭球）** | 每片有界，观测视图分簇与局部栅格可直接复用 |
| 6 | 脉状几何 | **折线骨架 + 胶囊段** | 每段紧致有界；走向/倾角由骨架自然表达 |
| 7 | 旧数据 | **自动迁成"囊状"生成器**，种子锁定旧公式 | 旧行星行为逐位不变 |
| 8 | 观测视图分簇 | **空间哈希**替代 O(N²) 并查集 | 浸染状默认 512 颗、上限 8192 颗，平方复杂度不可接受 |

---

## 2. 数据模型

### 2.1 编辑器侧类层次

```
UPlanetGeologyDistribution                      (UObject, Abstract, EditInlineNew, DefaultToInstanced)
├── 公共噪声参数：Seed / ShapeDetail / Octaves / Lacunarity / Gain / VeinThreshold
├── UPlanetGeologySpherePodsDistribution        现有矿囊；迁移目标；最朴素的团块形态
├── UPlanetGeologyLensDistribution              片层 / 透镜
├── UPlanetGeologyVeinDistribution              脉状 / 折线骨架
└── UPlanetGeologyDisseminatedDistribution      浸染 / 稀疏散布
```

- 声明在 `StellarEraGeology`（该模块无项目依赖）。
- 每个子类在 Details 里展开自己的参数组；噪声参数在基类，归每个生成器实例所有。
- 基类持有 `virtual BuildRuntime(上下文)`，上下文含：元素 Id、生成器下标、是否禁用加速（测试用）。
- **嵌套编辑必须能通知外层预设**：编辑 Instanced 子对象时，引擎只会调用子对象自己的
  `PostEditChangeProperty`，不会调用 `UPlanetGeologyPreset::PostEditChangeProperty`。
  因此基类覆写 `PostEditChangeProperty`，`GetTypedOuter<UPlanetGeologyPreset>()` 找到外层后调用
  `NotifyContentChanged()`（`++Revision` + 广播 `OnPresetChanged`）。漏了这条会同时坏两件事：
  观测预览不自动重建、生成器缓存键的 `Revision` 不变导致地形不重刷。

### 2.2 元素参数

```cpp
USTRUCT()
struct FPlanetGeologyElementParams
{
    FName Id;
    FString DisplayName;
    FLinearColor Color;
    float Abundance;      // 只影响颜色权重与图例占比，不改几何

    UPROPERTY(EditAnywhere, Instanced, Category = "Distribution")
    TArray<TObjectPtr<UPlanetGeologyDistribution>> Distributions;
};
```

元素级不再有 `PodCount` / `PodRadius` / 噪声参数；这些字段作为**遗留字段**保留序列化（见 §7）。

### 2.3 种子派生

```
ResolvedSeed = Seed != 0
    ? Seed
    : int32(GetTypeHash(元素 Id.ToString())
             ^ (生成器下标 * 0x9E3779B1)
             ^ GetTypeHash(子类名.ToString()))
```

- 新生成器使用上式；不同元素、不同生成器、不同形态默认互不相关。
- **必须哈希字符串，不能哈希 `FName` 本身**：`GetTypeHash(FName)` 取的是比较索引（名字池下标），
  跨进程/跨机器不稳定，编辑器与打包版会长出不同的矿。`GetTypeHash(FString)` 走 `FCrc::Strihash`
  （对字符内容做 CRC），是确定性的。
- **生成器重排会改变形状**（种子含下标）。写进 README。
- 迁移出的囊状生成器**不使用上式**，直接写入旧规则算出的 `ResolvedSeed`（见 §7.2）。

### 2.4 运行期契约

```cpp
struct FPlanetGeologyRegion
{
    FBox  Bounds;                // 归一化空间（行星基准半径 = 1），保守包围盒
    float CharacteristicRadius;  // 区域内最小特征尺度；观测视图用它定格边
};

class FPlanetGeologyDistributionRuntime
{
public:
    virtual ~FPlanetGeologyDistributionRuntime();

    /** 该元素在某归一化点的强度 ∈ [0,1]。 */
    virtual float EvaluateNormalized(const FVector& P) const = 0;

    /** 绕过加速结构遍历全部基元；测试 oracle。生产路径不调用。 */
    virtual float EvaluateBruteForce(const FVector& P) const = 0;

    /** 追加本生成器的全部有界区域。 */
    virtual void EnumerateRegions(TArray<FPlanetGeologyRegion>& Out) const = 0;

    virtual bool IsEmpty() const = 0;

    /** 诊断：基元数。 */
    virtual int32 NumPrimitives() const = 0;
};
```

- 运行期对象不可变、不持有 UObject 引用，可跨线程共享。
- `BuildFromPreset` 仍在游戏线程调用（`check(IsInGameThread())` 不变）。

### 2.5 元素运行期形态

```cpp
struct FPlanetGeologyElement
{
    FString Id;
    FString DisplayName;
    FLinearColor Color;
    float Abundance;
    TArray<TSharedPtr<const FPlanetGeologyDistributionRuntime>> Distributions;
};
```

`FPlanetGeologyElement` 中的 `Pods`、`PodRadius`、`ShapeDetail`、`Octaves`、`Lacunarity`、`Gain`、`Seed`、`VeinThreshold` 全部删除。`FPlanetGeologyPod` 保留，仅供囊状生成器的运行期使用。

---

## 3. 求值与合成

对归一化点 `P`：

```
ElementNorm(P) = max over 生成器 g  of  g->EvaluateNormalized(P)
Raw_i          = Abundance_i × ElementNorm_i
```

之后的总量、最强元素、纯度、混合色、几何外凸公式**完全沿用现有实现**：

```
Total        = Σ Raw_i
DominantNorm = max ElementNorm_i
Purity       = MaxRaw / Total
Color        = Σ(Raw_i · Color_i) / Total
GeometryDelta= GeometryAmplitude × clamp(max_i(ElementNorm_i), 0, 1)
```

`SampleElementNorms` 是测试 oracle，不做"球外早退"；`SampleElementNormAtNormalized` 球外仍直接返回 0。这条不对称保持不变。

### 3.1 公共噪声

每个生成器独立一套噪声：

```
FeatureRadius = 形态自己的特征尺度（片层=厚度，脉=管半径，浸染=颗粒半径，囊=囊半径）
NoiseScale    = ShapeDetail / (2 × FeatureRadius)
RawNoise      = PerlinFractal_3D(P × NoiseScale, frequency = 1, Octaves)
Noise01       = 0.5 × (RawNoise + 1)
Vein          = VeinThreshold > 0
              ? clamp((Noise01 − VeinThreshold) / (1 − VeinThreshold), 0, 1)
              : Noise01
```

`Octaves / Lacunarity / Gain` 的设置方式与现有实现一致（`SetFractalLacunarity` + `SetFractalOctavesAndGain`，采样时 octaves 必须与设置时一致）。

**可解析性约束**：一个噪声周期约 `2 × FeatureRadius / ShapeDetail`，观测视图一格约 `2 × FeatureRadius / CellsPerFeature`。要让等值面能解析噪声，需 `ShapeDetail ≤ CellsPerFeature / 2`。默认 3 ≤ 6，成立；README 写明。

### 3.2 基元尺度一致性

**同一个生成器内**，所有基元的包围半径必须处于同一量级（最大 ≤ 约 4 × 最小）：

- 片层：一片一个透镜，三轴抖动 ≤ 1.35 倍；
- 脉状：按 `SegmentLengthRatio × 半径` 分段，段长与半径成固定比例；
- 浸染：一颗一粒，半径抖动 ≤ 1.5 倍；
- 囊状：一囊一球，沿用现有半径抖动。

原因：每个生成器的稀疏网格以"该生成器最大基元包围半径"为格边。如果同一生成器里混有
一个远大于其它的基元，格边会被它拉大，小基元所在格子里的候选数随之暴涨，地形每体素
开销失控。**跨生成器不要求同量级**——它们是各自独立的网格。

片层宽而薄是刻意的（A / C 可达 20 倍以上），这不违反本条：约束的是"片与片之间"，
不是"一片自己的长宽比"。脉状之所以要拆成短段，就是为了让胶囊的包围半径保持在
`约 2.5 × 半径`，而不是"一条 0.35 长的对角线包围盒"。

---

## 4. 三种形态

### 4.1 片层 / 透镜 `UPlanetGeologyLensDistribution`

#### 参数

| 参数 | 范围 / 默认 | 含义 |
|---|---|---|
| `SeamCount` | 0–256 / 24 | 撒多少片；0 = 该生成器不产出（与 `PodCount = 0` 同义） |
| `SeamLength` (A) | 0.01–0.5 / 0.12 | 长轴半径（占行星半径） |
| `SeamWidth` (B) | 0.01–0.5 / 0.06 | 宽轴半径 |
| `SeamThickness` (C) | 0.0005–0.05 / 0.008 | 径向半厚 |
| `SizeJitter` | 0–1 / 0.35 | 三轴各自抖动幅度（±比例） |
| `DepthBias` | 0–0.9 / 0.6 | 深度偏好强度；0 = 全深度**体积**均匀 |
| `DepthCenter` | 0–1 / 0.12 | 偏好深度（0 = 地表，1 = 球心） |
| `DepthSpread` | 0.01–1 / 0.06 | 偏好深度的高斯宽度 |
| `TiltJitterDeg` | 0–45 / 5 | 相对球面切平面的随机倾斜 |
| `WarpAmplitude` | 0–2 / 0 | 径向域扭曲强度（单位 = 厚度）；0 = 关闭 |
| `WarpScale` | 0.5–8 / 2 | 扭曲噪声频率（单位 = 厚度的倒数） |

#### 放置

对每一片：

1. 方向 `D = Rng.GetUnitVector()`；
2. 半径比例 `rr = ∛U`（体积均匀），深度 `t = 1 − rr`；再按权重
   `(1−b) + b·exp(−((t−Center)/Spread)²)` 做带权拒绝采样（最多 64 次）；
3. 球心 `C = D × rr`（`rr = 1 − t`）；
4. 局部基：`LocalZ = D`，`LocalX` = 切平面内随机单位向量，`LocalY = LocalZ × LocalX`；
5. 倾斜：绕一条随机切向轴旋转 `TiltJitterDeg × (2u−1)` 度；
6. 三个半轴各乘 `1 + SizeJitter × (2u−1)`，再 clamp 到参数范围。

#### 求值

```
Local = Orientation⁻¹ × (P − C)
若 WarpAmplitude > 0：
    Local.Z += WarpAmplitude × C_thickness × (2 × Perlin(Local / C_thickness × WarpScale) − 1)
q      = (Local.X/A)² + (Local.Y/B)² + (Local.Z/C)²
Factor = max(0, 1 − q)
Norm   = max over 片 (Factor × Vein)
```

域扭曲只多一次 Perlin，且只在候选基元内采样。默认关闭。

#### 区域与加速

- 区域：每片一个；包围盒 = 有向椭球的 AABB（沿世界轴 i 的半长 = `Σ_j |R_ij| × h_j`）；特征半径 = `C`。
- 稀疏网格格边 = 最大片包围半径（默认约 0.12）。

#### 观测视图成本

片层"宽而薄"的代价是观测视图角点多：一片 `0.14 × 0.07 × 0.008` 的透镜、`CellsPerFeature = 12`
时，簇盒约 `0.28 × 0.14 × 0.016`、格边 `0.00133`，单片面约 26 万角点；16 片约 420 万个。
若 `CellsPerFeature = 32`，侧面格数会超过 512 的轴上上限，簇内通过放大格边降分辨率——
这是预期行为，不是 bug。片层建议 `CellsPerFeature` 取 12–16、厚度 ≥ 0.008。

### 4.2 脉状 `UPlanetGeologyVeinDistribution`

#### 参数

| 参数 | 范围 / 默认 | 含义 |
|---|---|---|
| `VeinCount` | 0–64 / 4 | 几条主脉；0 = 该生成器不产出（与 `PodCount = 0` 同义） |
| `VeinLength` | 0.05–1.0 / 0.35 | 单条总长度（占行星半径） |
| `VeinRadius` | 0.002–0.05 / 0.008 | 管半径 |
| `RadiusJitter` | 0–1 / 0.4 | 逐段半径抖动 |
| `SegmentLengthRatio` | 1–4 / 2.5 | 段长 / 半径 |
| `DirectionJitterDeg` | 0–45 / 8 | 每段方向偏折 |
| `BranchCount` | 0–8 / 1 | 每条主脉的分叉数 |
| `DepthBias` | 0–0.9 / 0.5 | 起点深度偏好强度 |
| `DepthCenter` | 0–1 / 0.4 | 起点偏好深度 |
| `DepthSpread` | 0.01–1 / 0.12 | 起点偏好宽度 |

#### 放置

对每条主脉：

1. 起点与初始方向：起点用与片层相同的带权深度采样；初始方向取随机单位向量；
2. 段长 `S = SegmentLengthRatio × VeinRadius`；
3. 循环直到累计长度 ≥ `VeinLength` 或下一步会越出球面：
   - 方向加 `DirectionJitterDeg × (2u−1)` 的随机扰动并归一化；
   - 新点 = 旧点 + 方向 × `S`；
   - 记录一条胶囊（段半径 = `VeinRadius × (1 + RadiusJitter × (2u−1))`）。
4. 分叉：在已有点中随机取 `BranchCount` 个起点，各生成一条长度为主脉一半的子链。

球面判定：新点满足 `|NewPoint| > 1 − 0.5 × VeinRadius` 时停止（露头靠胶囊自身探出，不额外外推）。

`VeinLength` 是**目标**长度：起点越浅、方向越朝外，实际长度越短。起点深度均值 0.4、
方向各向同性时，多数主脉能走完 0.3 左右；要更长的脉就把 `DepthCenter` 调深或
`VeinLength` 调小一点，别指望参数等于实际长度。

#### 求值

```
t      = clamp(dot(P − A, B − A) / |B − A|², 0, 1)
d      = |P − (A + t × (B − A))|
Factor = max(0, 1 − (d / r)²)
Norm   = max over 段 (Factor × Vein)
```

#### 区域与加速

- 区域：每段一个；包围盒 = 线段 AABB 各轴外扩 `r`；特征半径 = `r`。
- 稀疏网格格边 = 最大段包围半径 = `0.5 × S + r`（`S = SegmentLengthRatio × r`），默认约 `2.25 × r`，半径抖动上界时约 `2.65 × r`。

#### 已知取舍

一整条脉的胶囊互相重叠，观测视图会把它们并成一簇，簇长 = 脉长。默认 `VeinLength = 0.35`、`r = 0.008`、`CellsPerFeature = 12` 时约 263 格/轴，安全；`VeinLength = 1.0` 会撞 512 格/轴硬上限，只降分辨率、不丢几何。

### 4.3 浸染状 `UPlanetGeologyDisseminatedDistribution`

#### 参数

| 参数 | 范围 / 默认 | 含义 |
|---|---|---|
| `GrainCount` | 0–8192 / 512 | 颗粒数 |
| `GrainRadius` | 0.0005–0.02 / 0.002 | 颗粒半径 |
| `RadiusJitter` | 0–1 / 0.5 | 半径抖动 |
| `DepthBias` | 0–0.9 / 0.5 | 深度偏好强度 |
| `DepthCenter` | 0–1 / 0.15 | 偏好深度 |
| `DepthSpread` | 0.01–1 / 0.08 | 偏好宽度 |

#### 放置与求值

- 放置：方向均匀 + `∛U` 体积均匀 + 带权深度拒绝采样；半径按抖动缩放。
- 求值：`Factor = max(0, 1 − (d/r)²)`；`Norm = max over 粒 (Factor × Vein)`。
- 区域：每粒一个；特征半径 = 粒半径。
- 稀疏网格格边 = 最大粒包围半径。

#### 成本提示

512 粒约等于 512 个小簇 × 约 2200 角点 ≈ 110 万角点，可接受。8192 是上限而非推荐值；README 写明"想更密先加粒径，再考虑加数量"。

### 4.4 遗留囊状 `UPlanetGeologySpherePodsDistribution`

参数与语义完全沿用现有 `PodCount / PodRadius / PodRadiusJitter / PodDepthBias / PodDepthCenter / PodDepthSpread`。放置、球面二次衰减、空间索引、区域枚举都是现有 `PlacePods / PodInfluence / PodIndex` 的搬迁。

用途：迁移旧资产；作为最朴素的团块形态；作为迁移保真测试的基准。

### 4.5 生成器日志

`BuildFromPreset` 对每个"元素 + 生成器"打一条 `Log`：

```
元素=iron 生成器=Vein 基元=108 区域=108 格边=0.0212 平均每格候选=2.03
```

调参时看"平均每格候选"的相对变化：格边取包围半径，而基元 AABB 每轴约 2× 包围半径，所以这个数**天生就在 2–4**，不是"越接近 1 越好"。明显偏大（例如 > 10）才说明某个基元相对格边过大（违反 §3.2），或格边取小了。

---

## 5. 观测视图接入（`StellarEraMinerals`）

### 5.1 数据源

`PlanetMineralSurface::Build` 从 `Field.GetPods(ElementIndex)` 改为 `Field.EnumerateRegions(ElementIndex)`：

```cpp
struct FRegionInstance
{
    int32 ElementIndex;
    FBox  BoundsCm;   // 归一化 Bounds × SurfaceRadiusCm
    float RadiusCm;   // CharacteristicRadius × SurfaceRadiusCm
};
```

### 5.2 空间哈希分簇

现有实现是所有基元两两比较 AABB 的 O(N²) 并查集。新实现：

1. `CellSize = max over 区域的轴长最大值`（若为 0 则提前返回）；
2. 每个区域按 `BoundsCm` 覆盖的格范围插入 `TMap<FIntVector, TArray<int32>>`（格边取最大轴长，所以每个区域最多覆盖 2³ = 8 格）；
3. 每格内部做 **sweep-and-prune**：按 `Bounds.Min.X` 排序，对每个 i 只继续检查
   `Min.X ≤ Max.X_i` 的后续 j，再做完整 AABB 相交判断，相交才 `Union`；
4. 分簇结果与 O(N²) 参照实现必须一致（忽略簇顺序）。

**不能**把"同格"直接当成"相交"：两个小区域可以落在同一格却相距很远。直接 union 会把
不相交的矿体并成一个大簇，簇盒变大、格边不变 → 角点数暴涨（默认数据下可能就是几百倍）。

正确性：两个 AABB 相交 ⟹ 共享至少一个点 ⟹ 该点落在唯一一格 ⟹ 两个 AABB 都覆盖并插入
该格 ⟹ 在该格的 sweep 里被检查到 ⟹ 必被 `Union`。

复杂度：插入 O(8N)；每格 sweep O(k log k + 实际相交对数)，k 是该格区域数。默认数据里
k 很小；最坏情况（所有区域挤在一格）退化为该格的 O(k²)，但仍只影响那一格。

簇排序仍按"簇内最小区域下标"升序，保证两次构建的顶点顺序一致。

### 5.3 格边与上限

- 簇内格边 `CellSize = 2 × 簇内最小特征半径 / CellsPerFeature`；
- 保持现有两条硬上限：每轴 ≤ 512 格、单簇单元素 ≤ 8'000'000 角点；
- 触发上限时放大格边（降分辨率），**绝不跳过簇**。

### 5.4 `CellsPerPod` 改名

`CellsPerPod` → `CellsPerFeature`（`FBuildParams`、`FCacheKey`、`UPlanetMineralViewComponent`）。

这是 UPROPERTY 改名，必须加 Core Redirect，否则 `BP_VoxelPlanet` 上现有值 `32` 会被重置为默认 12：

```ini
[CoreRedirects]
+PropertyRedirects=(OldName="/Script/StellarEraMinerals.PlanetMineralViewComponent.CellsPerPod",NewName="/Script/StellarEraMinerals.PlanetMineralViewComponent.CellsPerFeature")
```

---

## 6. 地形接入（`StellarEraTerrain`）

**零代码改动**：`UVoxelPlanetGenerator` 只消费 `FPlanetGeologyField` 的采样入口；缓存键仍是"预设指针 + 半径 + Revision"。

代价是每体素多一层"元素 × 生成器"的网格查询（默认 6 × 1）。命中格子之外仍然一次 Perlin 都不求。验收时对比重建耗时基线，允许不劣化。

---

## 7. 迁移

### 7.1 运行期兜底（行为逐位不变的保证）

`BuildFromPreset` 里，若某元素 `Distributions.Num() == 0 && PodCount > 0`，就在运行期用旧字段造一个囊状快照。旧种子规则必须原样搬过去：

```
LegacySeed = Params.Seed != 0
    ? Params.Seed
    : int32(GetTypeHash(Params.Id) & 0x7fffffffu)
```

**不能**使用 §2.3 的新派生规则——那会让旧行星的矿囊位置全变。

### 7.2 资产侧自动迁移

`UPlanetGeologyPreset::PostLoad`：

1. 对每个元素，若 `Distributions.Num() == 0 && PodCount > 0`：
   - `NewObject<UPlanetGeologySpherePodsDistribution>(this)`；
   - 复制全部旧参数与 `LegacySeed`；
   - 加入 `Distributions`；
   - 把 `PodCount` 置 0 作为"已迁移"标记。
2. 已迁移并保存的资产不会重复迁移；未保存则每次加载重建同一个生成器，结果一致。

旧字段保留序列化，标 `meta = (DeprecatedProperty)`（与 `UCosmosOrbitComponent` 的旧字段做法一致）。

迁移逻辑抽成可测函数：

```cpp
static void MigrateLegacyElement(FPlanetGeologyElementParams& Element, UObject* Outer);
```

`PostLoad` 只负责遍历调用。

### 7.3 迁移保真判据

同一份旧参数下：

- 运行期兜底（§7.1）与显式囊状生成器（§7.2）逐点 `SampleElementNormAtNormalized` 相等；
- 种子等于 `LegacySeed`；
- `IsEmpty()` 语义一致。

---

## 8. 初始数据 `DA_Geology_Earth`

`StellarEra.CreateEarthGeologyPreset` 本来就是覆盖写盘，扩 `FillEarthPreset` 即可。

| 元素 | 生成器 | 关键参数 | VeinThreshold |
|---|---|---|---|
| 硅 | 片层 | 16 片；A 0.14 / B 0.07 / 厚 0.008；深度中心 0.10，偏置 0.5 | 0.45 |
| 铁 | 脉状 | 5 条；长 0.40；半径 0.009；每条 1 分叉 | 0.50 |
| 铜 | 脉状 | 3 条；长 0.28；半径 0.007；无分叉 | 0.55 |
| 钛 | 浸染 | 320 粒；半径 0.0025 | 0.60 |
| 铀 | 浸染 | 140 粒；半径 0.0015；深度中心 0.35、偏置 0.6 | 0.65 |

公共噪声：`ShapeDetail 3 / Octaves 2 / Lacunarity 2 / Gain 0.5`。`Appearance` 与 `GeometryAmplitude` 不变（`0.05 / 0.12 / 0.45`、`1.5`）。

---

## 9. 错误处理

| 情况 | 行为 |
|---|---|
| 元素的 `Distributions` 为空且无遗留矿囊 | 该元素无矿；`IsEmpty()` 视其他元素而定 |
| 生成器参数非法（负数、越界） | 建场时 clamp，与现有 `AssetClamps` 的立场一致 |
| 生成器基元数为 0 | 该生成器运行期 `IsEmpty()` 为真，元素继续看其他生成器 |
| `Seed = 0` | 按 §2.3 派生；迁移数据按 §7.1 锁定旧值 |
| 观测视图触发格数/角点上限 | 放大格边降分辨率，不跳过簇、不丢几何；日志计数 |
| 观测视图没有任何区域 | 返回空结果，不报错（现有语义） |
| 非游戏线程调用 `BuildFromPreset` | `check(IsInGameThread())` 触发（不变） |

---

## 10. 测试

### 10.1 保留但换夹具

`Geology.Determinism / Bounds / Color / Pipeline / ElementNorms.MatchesDominant / RangeCoverage / AssetClamps / PresetDefaults / PresetRevision / LegacyAssetPath / LegacyMigration / EarthPresetValues / GeneratorParametersSupported`，
`MineralSurface.Build / ClosedMesh / IsoAccuracy / Threshold / Empty / Deterministic / ElementOrder / EmptySlots / CacheKey / SurfaceOutcrop / RadiusInvariance / ClusterMerge / ClampSmoke`，
`MineralView.ActorBuild / EmptySlot / Empty`。

### 10.2 改名

| 现在 | 改为 |
|---|---|
| `Geology.PodCoverage` / `PodOverlap` / `PodShape` | `Geology.ShapePods.Coverage` / `.Overlap` / `.Shape` |
| `Geology.SpatialIndex` | 每种形态各一条：`Geology.ShapePods.BruteForceAgrees` / `Geology.Lens.BruteForceAgrees` / `Geology.Vein.BruteForceAgrees` / `Geology.Disseminated.BruteForceAgrees` |
| `MineralSurface.PodLocal` | `MineralSurface.RegionLocal` |
| `MineralSurface.CellsPerPod` | `MineralSurface.CellsPerFeature` |

### 10.3 新增

| 测试 | 断言 | 防的是 |
|---|---|---|
| `Geology.Distribution.MaxComposition` | 两生成器的元素逐点 `Norm == max(两者)`；两者皆 0 处为 0 | 合成被写成相加/覆盖 |
| `Geology.Distribution.EmptySemantics` | 无元素、或所有生成器为空 → `IsEmpty()` 为真 | 空场判定漏生成器 |
| `Geology.Distribution.SeedDerivation` | 非 0 Seed 被尊重；0 时不同元素/生成器互不相同；迁移种子 == 旧公式 | 旧行星形状被改 |
| `Geology.Lens.Bounds` | 区域深度落在偏好带；两次建场逐位一致；不越出球体+基元半径 | 放置越界 / 不确定 |
| `Geology.Vein.Continuity` | `VeinThreshold = 0` 时相邻骨架段中点场值 > 0；段数不超上限 | 脉断成点 / 基元不紧致 |
| `Geology.Disseminated.Counts` | 区域数 == GrainCount；半径在抖动范围内 | 数量/尺寸参数失效 |
| `Geology.ShapePods/Lens/Vein/Disseminated.BruteForceAgrees` | 每种形态：加速路径与暴力路径随机点相等（1e-6） | 索引漏基元 |
| `Geology.PresetRevision.NestedEdit` | 改生成器子对象的属性后 `Revision` 自增且广播一次 | 嵌套编辑不通知外层（预览不刷新、地形不重刷） |
| `Geology.LegacyMigrationFidelity` | §7.3 的三条判据 | 迁移改形状 |
| `MineralSurface.RegionClustering` | 随机区域集：空间哈希分簇 == O(N²) 参照划分 | 哈希漏 union |
| `MineralSurface.ClosedMesh.AllForms` | 片层/脉状/浸染夹具的网格每条边恰好两个三角形 | 面开洞 |
| `MineralSurface.GridCap` | 超长脉触发上限时网格非空、几何不丢 | 触发上限时静默跳簇 |
| `MineralView.CellsPerFeature` | 字段默认值 = 12；缓存键包含它；改值使缓存失效 | 改名后缓存键漏字段 |

测试仍是 `Automation RunTests StellarEra`（headless，editor world，不跑 BeginPlay）。

---

## 11. 验收

1. 编辑器关闭 → 捆绑 dotnet 直调 UBT：`StellarEraEditor Win64 Development` 通过。
2. `Automation RunTests StellarEra` 全绿。
3. 编辑器打开 `DA_Geology_Earth`，五个元素各带生成器；改片层的 `SeamCount`，正在预览的观测体自动重建。
4. 编辑器预览：同屏可见片层、穿切地层的脉、散点浸染；图例关掉其他元素后能看到互相穿插。
5. `UniverseMap` 点"重建行星"，PIE 地表只见露头矿；海面无蓝色环。
6. 重建耗时与基线对比不劣化（两个数字记进实施记录）。
7. 预览退出后 `git status` 无新增脏包。
8. `BP_VoxelPlanet3` 的 `CellsPerPod = 32` 改名后仍是 32。
9. 旧关卡迁移兜底：视觉与原实现一致，自动化测试逐点证明。

---

## 12. 文件清单

**新增（`StellarEraGeology`）**

| 文件 | 职责 |
|---|---|
| `Public/PlanetGeologyDistribution.h` / `Private/PlanetGeologyDistribution.cpp` | 抽象 UObject 基类、`FPlanetGeologyRegion`、运行期接口、种子派生、噪声初始化 |
| `Public/PlanetGeologySpherePodsDistribution.h` / `.cpp` | 囊状（迁移目标） |
| `Public/PlanetGeologyLensDistribution.h` / `.cpp` | 片层 / 透镜 |
| `Public/PlanetGeologyVeinDistribution.h` / `.cpp` | 脉状 / 折线骨架 |
| `Public/PlanetGeologyDisseminatedDistribution.h` / `.cpp` | 浸染 / 稀疏散布 |
| `Private/PlanetGeologyDistributionTests.cpp` | §10 的新增测试 |

**修改**

| 文件 | 改动 |
|---|---|
| `PlanetGeologyTypes.h` | 元素运行期字段替换；保留 `FPlanetGeologyPod` |
| `PlanetGeologyPreset.h` / `.cpp` | `Distributions` 数组、`PostLoad` 迁移、`FillEarthPreset` |
| `PlanetGeologyField.h` / `.cpp` | max 合成、`EnumerateRegions`、删全局 `PodIndex` |
| `PlanetGeologyPodTests.cpp` / `PlanetGeologyPresetTests.cpp` | 按 §10 改 |
| `PlanetMineralSurface.h` / `.cpp` | 区域化、空间哈希分簇、`CellsPerFeature` |
| `PlanetMineralViewComponent.h` / `.cpp` | 字段改名 |
| `PlanetMineralSurfaceTests.cpp` / `PlanetMineralViewTests.cpp` | 按 §10 改 |
| `Config/DefaultEngine.ini` | CoreRedirect |
| `Content/Geology/README.md`、`CLAUDE.md` | 文档 |

`Build.cs` 无需改动：没有新模块依赖；VoxelFree 的参数系统碰不到这些内联子对象（预设指针仍是 `VisibleAnywhere`）。

---

## 13. 风险与已知取舍

| 风险 | 缓解 / 退路 |
|---|---|
| 地形每体素多一层"元素 × 生成器"查询 | 默认 6 × 1；验收量重建耗时，不劣化才收 |
| `PostLoad` 创建子对象在异步加载 / cook 下异常 | 自动化测试"加载 → 保存 → 再加载"；异常则退成控制台命令 + 运行期兜底 |
| 浸染粒数或长脉把观测视图顶到上限 | 硬上限已在，触发只降分辨率；日志计数 |
| CoreRedirect 写错，BP 的 32 被重置 | 验收第 8 条直接读 Details |
| 生成器重排改变种子 → 形状全变 | README 写明；迁移数据锁定旧种子 |
| 复制/重命名预设资产时内联子对象归属 | 实施时人工验一次"复制 → 打开 → 保存" |
| `Instanced` 数组嵌在 USTRUCT 里时 Details 的"+"不弹子类选择器 | 实施第一步就在编辑器里验；失败则退成每个元素一个 `UObject` 容器子对象，元素结构不变 |
| 嵌套编辑不触发外层 `PostEditChangeProperty` | 基类主动通知外层（§2.1）；`Geology.PresetRevision.NestedEdit` 测试覆盖 |
| 片层"宽而薄"导致观测视图角点远多于矿囊 | §4.1 给了成本估算与推荐值；默认 16 片约 420 万角点；`CellsPerFeature` 32 会先撞上限、只降分辨率 |
| 同一元素多个生成器重叠处颜色/几何饱和 | 取 max 不累加，`DominantNorm ≤ 1`、`GeometryDelta ≤ GeometryAmplitude` 不变 |
| `ShapeDetail` 超过 `CellsPerFeature / 2` 时等值面混叠 | README 写明；默认值满足约束 |
