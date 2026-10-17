# 水系统设计文档

版本：1.0
状态：设计阶段，未实现
依赖：`VoxelPlanet`（星球）· `VoxelCosmos`（轨道）

---

## 一、需求

用户提出的三条要求：

1. **潮汐影响** — 海水随天体引力涨落
2. **水是矿物** — 已实现（地质系统里的 Water）
3. **局部水源可耗尽** — 小水塘抽干就没了

第3 条是关键：**它把水从"无限资源"变成了"可争夺资源"。**

---

## 二、核心约束：体素存不了水量

查证 `VoxelData.h`，体素只有两个可写接口：

```cpp
void SetValue(const FIntVector& P, FVoxelValue Value);    // 密度
void SetMaterial(const FIntVector& P, FVoxelMaterial M); // 材质
```

**没有「液体量」字段。** 一个格子装 1 个水还是装 100 个水，体素本身不区分。

这意味着：

> **水的「形状」存在体素里，水的「数量」必须存在别处。**

这是整个系统设计的出发点。后面所有复杂度都源于这一点。

---

## 三、双账本架构

```
体素层（已有）              外部账本（新增）
─────────────────         ─────────────────
哪些格子是水               这个水体还剩多少
挖开水塘 → 看见坑          抽水 → 减少
回填 → 坑消失              归零 → 挖掉水voxel
```

**为什么不能只靠体素？** 因为体素表达不了"部分填充"。一个装了一半的格子，
从体素看是满的（密度相同），但实际水量减半了。

**为什么体素仍然必要？** 因为玩家需要看到水体消失——挖掉水 voxel，
池底露出来，这是最重要的视觉反馈。如果只是"抽不出水了"，
玩家分不清是水干了还是泵坏了。

---

## 四、两种水源，两种模型

这是本设计最重要的区分。

| | 海洋 | 水塘/湖泊 |
|---|---|---|
| **判定方式** | `距球心 < 水位` | 体素标记 + 区域 ID |
| **数量** | 无限，不记账 | 有限，储量表 |
| **水位** | `BaseSea + TideOffset` | `PitBottom + Volume/Area` |
| **受潮汐** | ✅ 是 | ❌ 否 |
| **耗尽** | 不会 | ✅ 会消失 |

### 为什么水塘不受潮汐影响

潮汐是**天体尺度的引力现象**。一个脸盆里的水不会因为月球引力而晃动。

所以两套模型必须分开，不能共用一套逻辑。

### 但存在一个耦合

如果水塘挖得太深、打通到海洋，它就变成海水口，会随潮汐灌进来。

**接口预留，第一版不实现。** 检测方式：定期检查水塘底部与海洋的连通性。

---

## 五、潮汐系统（FTideSystem）

### 5.1 混沌来自轨道，不是流体模拟

这是必须先澄清的一点。

**潮汐的混沌不需要流体模拟。** 混沌来自天体轨道本身，而 N 体求解器
（辛积分器）跑了几万步后，轨道已经不可预测：

```
轨道混沌（已有）→ 距离混沌 → 潮汐混沌（免费获得）
```

**不需要流体模拟，因为潮汐是纯函数：**

```cpp
float GetTideOffset(double GameTime) const
{
    float Sum = 0.f;
    for (int32 i = 0; i < Bodies.Num(); ++i)
    {
        const double Dist = DistanceToBody(i, GameTime);   // 从 N 体解算器读
        Sum += Mass(i) / (Dist * Dist * Dist);             // 潮汐 ∝ 1/r³
    }
    return Amplitude * Sum;
}
```

**没有历史状态，没有迭代，时间可任意跳转。** 这和流体模拟完全相反
（流体必须逐帧算，状态存在前一帧）。

`1/r³` 就是潮汐效应的物理公式，不是编的。

### 5.2 关键设计：无状态

`GetTideOffset(GameTime)` 每次都从当前轨道现算，所以：

- ✅ 玩家可以快进时间看潮汐变化
- ✅ 不需要保存流体状态
- ✅ 存档只需要存 `GameTime`

**如果用流体模拟，这三条全部做不到。**

### 5.3 潮差与轨道的关系

开普勒第三定律天然给出这个：

```
轨道周期 T ∝ R^1.5
潮汐强度 ∝ M / R³
```

**靠近恒星的小行星潮汐又快又猛，远处的大行星潮汐平缓。**

这不是我加的规则，是轨道力学的结果。玩家自己就能从观察中总结出来。

### 5.4 潮差幅度

```cpp
TidalRange = BaseAmplitude
           × (M_Star + M_Moons) / (M_Sun + M_MoonEarth)²
           × (R_Earth / R)³
```

用真实单位算，输出用体素表示。潮差大的星球值得建潮汐电站。

---

## 六、水塘系统（FWaterPoolManager）

### 6.1 水塘怎么产生

**不是自动检测，是玩家行为触发的。**

```cpp
// 挖掘后回调
void OnVoxelDug(FIntVector Center, int32 Radius)
{
    // 1. 是不是在海洋附近？
    if (IsNearOcean(Center))  return;          // 通海 → 不记账
    
    // 2. 挖出来的坑够深吗？
    const int32 Depth = MeasurePitDepth(Center, Radius);
    if (Depth < MinPoolDepthVoxels)  return;   // 太浅不成潭
    
    // 3. 建档或累加
    const int32 PoolId = FindOrCreatePool(Center);
    Pools[PoolId].Volume += EstimatePitVolume(Center, Radius, Depth);
}
```

**判据是「玩家挖的坑」，不是「系统扫描的水」。**

理由：玩家挖的坑就是他意图挖的水塘。这符合直觉，且零额外计算——
不需要全世界扫描哪里有水。

### 6.2 水塘的数据结构

```cpp
struct FWaterPool
{
    FIntVector CenterVoxel;        // 中心
    int32     RadiusVoxels;        // 影响半径
    double    Volume;              // 剩余水量（体素³ 当量）
    double    BottomDepthVoxels;   // 池底深度，用于算水位
    int32     PlanetSeed;          // 所属星球
    bool      bConnectedToOcean;   // 是否通海（预留）
};
```

**只存中心+半径+体积，不存每个格子。** 水位下降时按比例减少
`RadiusVoxels`，让水面逐渐缩小——这是最直观的视觉表现。

### 6.3 抽取与耗尽

```cpp
double FWaterPoolManager::Extract(int32 PoolId, double RequestedAmount)
{
    FWaterPool& Pool = Pools[PoolId];
    const double Extracted = FMath::Min(RequestedAmount, Pool.Volume);
    Pool.Volume -= Extracted;

    // 水位下降 → 水面缩小
    const double Area = FMath::Max(PI * FMath::Square(Pool.RadiusVoxels), 1.0);
    const double LevelDrop = Extracted / Area;
    Pool.BottomDepthVoxels -= LevelDrop;

    // 彻底干了 → 挖掉水 voxel，玩家能看见池底
    if (Pool.Volume <= 0.0)
    {
        RemovePoolVoxels(PoolId);
        Pools.Remove(PoolId);
        return Extracted;
    }

    UpdatePoolSurfaceMesh(PoolId);
    return Extracted;
}
```

**关键：`RemovePoolVoxels` 真的去挖水 voxel。** 这给玩家"水干了"的
视觉反馈，比"泵停机了"强得多。

---

## 七、海洋的视觉表现

### 7.1 材质层

`GetMaterialImpl` 里判断是否在水位以下：

```cpp
const double WaterLevel = Radius - Sea.bEnabled ? Sea.Depth : 0;
const bool bUnderwater = (Distance - SurfaceRadius) < WaterLevel;
if (bUnderwater)
{
    Builder.SetColor(Sea.DeepColor);
}
```

**这是唯一的性能风险点。** `GetMaterialImpl` 每体素调一次，
加上海平面判断会让材质阶段开销明显上升。

**解决：按整数块缓存。** 成分与水位在空间上连续，
8×8×8 的块内判定结果几乎相同。

### 7.2 水位线的视觉

不改变体素几何，只改材质——水位上升时，更多体素被判为水下。

**代价：玩家在高潮时不会被水挡住。** 视觉上淹了，实际能走过去。

这是体素静态世界的固有限制。要真正实现，需要把潮位纳入密度场：

```cpp
Value = FMath::Max(TerrainSDF, SeaSDF(TideOffset));
```

但那会让水位变化时整个 chunk 重新网格化——**代价极高，第一版不做。**

---

## 八、潮汐发电

水的第四种利用，也是最优雅的一个。

```cpp
// 功率 ∝ 潮差 × 流量 × 效率
Power = TidalRange × FlowRate × Efficiency;
```

**只有潮差大的星球值得建。** 这让"选一颗好星球"变成一个有意义的决策。

配套的建筑：

| 建筑 | 作用 |
|---|---|
| 潮汐发电机 | 潮差 → 电 |
| 抽水泵 | 从海洋/水塘取水 |
| 净水厂 | 咸水 → 淡水（循环利用） |
| 汐田 | 高潮上水 → 蒸发 → 采盐 |

---

## 九、实现顺序

每一步都能独立验证，不需要全部完成才能测试。

### 第 1 步：`FTideSystem`（纯计算，可独立测试）

```
VoxelCosmos/Public/TideSystem.h
VoxelCosmos/Private/TideSystem.cpp
Tests/TideTest.cpp
```

**依赖 N 体求解器，不依赖任何新 Actor。** 是最安全的起点。

```cpp
class FTideSystem
{
    void Init(const FTideConfig&, const FNBodySimulator*);
    float GetTideOffset(double GameTime) const;
    double GetWaterLevel(double GameTime) const;
    bool IsOceanAt(const FVector& VoxelPos, double GameTime) const;
};
```

### 第 2 步：海洋材质

接进 `GetMaterialImpl`，需要先做块缓存。

### 第 3 步：`FWaterPoolManager`

挖掘回调 → 建档 → 储量表。

### 第 4 步：抽水泵

最简单的建筑，读水位决定能否工作。

### 第 5 步（可选）：流体蔓延

水往低处流。**这是唯一需要规则流体的地方**，第一版跳过。

---

## 十、待验证的技术点

### 10.1 挖掘回调怎么接

**未解决。** Voxel Plugin 的工具系统（`UVoxelToolBase`）有 `DoEdit()`，
但没有公开的"编辑完成"事件。

三个可能方案：

| 方案 | 做法 | 风险 |
|---|---|---|
| A. 轮询快照 | 每 N 秒对比体素数据 | 慢，且难定位改了哪里 |
| B. 替换工具 | 自己实现一个挖掘工具 | 需要派生 `UVoxelToolBase` |
| C. 只在抽水泵处判定 | 不追踪挖掘，按需检测 | 简单但不准 |

**建议先做 C**，验证玩法后再考虑 A/B。

### 10.2 水体的存档

`FWaterPool` 需要序列化。UE 的 Actor 存档机制可用，但
`FWaterPoolManager` 不是 Actor——可能需要一个管理 Actor 承载它。

### 10.3 潮汐的时间尺度

真实潮汐周期是**半天到一天**。如果游戏里的"一天"是 24 分钟，
潮汐周期就是 12 分钟——玩家可能等不到。

**需要决定：潮汐周期跟随游戏时间，还是缩放到玩家可接受的尺度。**

---

## 十一、与已有系统的关系

```
VoxelCosmos
  └── FNBodySimulator      ← 轨道（已是混沌的）
        │
        ↓ 读轨道
VoxelCosmos/TideSystem     ← 新增：潮位 = f(轨道)
        │
        ↓ 提供水位
VoxelPlanet
  └── GenerateMaterialImpl ← 材质判断水下
  └── GeologySystem        ← 水作为矿物（已有）
        │
        ↓ 提供密度
VoxelFactory
  └── MiningRig            ← 海洋区不产矿
  └── ProcessingBuilding   ← 抽水泵配方
  └── FWaterPoolManager    ← 新增：局部水体储量
```

**注意 `FTideSystem` 放在 `VoxelCosmos`** —— 它依赖 N 体求解器，
和星球生成器是并列关系，不是它的下游。

---

## 十二、明确不做的事

| 功能 | 原因 |
|---|---|
| 真正的 Navier-Stokes 流体 | 成本高两个数量级，且破坏时间跳转 |
| 水淹没玩家并阻挡移动 | 需要改密度场，代价极高 |
| 水体的流体压力/连通性精确计算 | 用中心+半径近似足够 |
| 水面上的动态涟漪（GPU 高度场） | 视觉锦上添花，可后加 |

**第一版目标：水在涨落，水塘会抽干，海水能取用。** 这三条达成，
玩法骨架就立住了。

---

## 十三、验收标准

| 阶段 | 验收 |
|---|---|
| FTideSystem | 潮位随轨道变化；时间可任意跳转；潮差与距离成反比 |
| 海洋材质 | 高潮时更多体素被判为水下；帧率无明显下降 |
| 水塘 | 挖坑建档；抽水减少；归零后 voxel 被挖掉 |
| 抽水泵 | 海洋区和水塘区都能工作；低潮时海洋抽水泵停产 |
