# 形态原语库

七个原语覆盖全部矿体与地质体的形状。**层与撒矿规则复用同一套**，原语自带值域实现，
剔除逻辑不用每处重写。

## 统一接口

```cpp
struct IGeoPrimitive
{
    virtual float      Evaluate(const FVector& UnitDir, double Depth) const = 0;  // 强度 ∈ [0,1]
    virtual FBox3d     GetBounds() const = 0;                                     // 包围盒（球面度+深度）
    virtual FGeoRange  GetRange(const FBox3d& Box) const = 0;                     // 保守值域
    virtual EPrimitiveKind GetKind() const = 0;
};
```

## 七个原语

| # | 原语 | 参数 | 出现在 | 现成对应 |
|---|---|---|---|---|
| 1 | **起伏界面** | 基准深度、起伏振幅、频率、过渡宽度 | 莫霍面、核幔边界、相变面、风化前锋 | 无，需新增 |
| 2 | **渐变带** | 中心深度、半宽、渐变曲线、扰动量 | 地壳内部分层、气候带、冲积扇分选 | 浸染（可改造） |
| 3 | **团块** | 中心、三轴半径、旋转、噪声扰动、硬/软边 | 侵入体、深部团块、透镜体、矿囊 | 囊状 |
| 4 | **线状带** | 起点、方向、长度、宽度、衰减、走向来源 | 造山带金、洋中脊硫化物、绿岩带、矿脉 | 脉状 |
| 5 | **层状** | 基准深度、层厚、倾角、横向范围 | 条带状铁矿、煤、层状侵入体、蒸发岩层 | 片层 |
| 6 | **同心分带** | 中心、外半径、环数、每环厚度与属性 | 斑岩蚀变晕、蒸发岩环 | 无，需新增 |
| 7 | **沿流程衰减** | 流路引用、起始品位、衰减率、沉积点列表 | 砂矿 | 无，需新增 |

## 各原语要点

### 1 起伏界面

```
r = Base + Amp * LowFreq(dir)
strength = smoothstep(r - W, r + W, |p|)
```
- 过渡宽度 `W` 小 → 清晰面（核幔）；大 + domain warp → 交错扩散（地壳内部）。
- **只用于深部，不用于矿体**。

### 2 渐变带

深度方向的一维剖面，横向可加噪声。用于「没有硬边界」的东西。
蒸发岩的环、冲积扇的分选可以用「半径分段 + smoothstep」表达，不必用 6。

### 3 团块

```
q = (p - Center) / Radii;            // 椭球
strength = 1 - smoothstep(1 - Hard, 1, |q| + Noise * NoiseAmp)
```
- 硬接触：`Hard` 取小（0.02–0.05）；软边（风化壳）：`Hard` 取大（0.2）。
- 三轴不等 + 旋转 → 岩墙、岩床、透镜体都靠它。
- **所有矿囊、侵入体都是它**，这是用得最多的原语。

### 4 线状带

```
t = clamp(dot(p - Start, Dir) / Length, 0, 1)
strength = (1 - |perp| / Width(t)) * (1 - t * Decay)
```
- `Dir` 可来自场（平行缝合线、沿断层走向）——**这是「矿跟着构造走」的实现点**。
- 石英脉、矿脉、剪切带矿全用它。

### 5 层状

```
local = dot(p - Origin, Up)            // Up 可倾斜
strength = 1 - smoothstep(Thick, Thick + Soft, |local mod Spacing|)
```
- 唯一的「规整水平面」，BIF、煤、层状侵入体、蒸发岩层。
- 会被 3B 的褶皱弯曲（变形场作用在 `p` 上，不是在原语上）。

### 6 同心分带

```
d = |p - Center| / OuterRadius
ring = floor(d * RingCount)
strength = RingProfile[ring](d)
```
- 只用于**材质**（蚀变晕）与**蒸发岩环**（少数需要几何的场合）。
- 蚀变晕不建议建几何（4 格岩株画不出 4 圈），只用它算 `ClueType` 与 `ClueStrength`。

### 7 沿流程衰减

```
strength = StartGrade * exp(-k * TravelDistance)
         * DepositWeights[FlowType]      // 凸岸、急流下方、扇顶…
```
- 需要阶段 6 的流路（`FlowAccum` + `Valley`）。
- **必须**先有上游原生矿（阶段 4），否则强度恒 0 ——「无源不砂矿」是硬规则。

## 组合规则

- 一个矿体 = 1 个原语实例，不允许「多原语布尔组合」做单个矿体（值域会爆炸）。
- 需要复杂形状时，用**多个实例**表达（成串透镜体），而不是一个复杂原语。
- 每个实例都进 BVH，`GetBounds` 必须是紧包围（含噪声余量）。

## 值域义务

每个原语必须实现 `GetRange`，且满足：抽样 10⁵ 点，`GetRange` 的区间 ⊇ 实际 `Evaluate` 值。
这条有单独测试（参照 `PlanetTerrainEarlyOutTests` 的写法），新增原语必须一起交测试。
