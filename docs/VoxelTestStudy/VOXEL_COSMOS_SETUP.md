# 体素星球 × N 体宇宙 —— 接入说明

## 现在两者是连着的

```
ACosmosBodyActor          ← N 体驱动，位置每帧更新，负责自转
  ├── 球体网格            ← 远景用这个，便宜
  └── AVoxelPlanetActor   ← 近景才激活，负责体素地形
```

## 核心约束（决定了这个架构）

### 移动 Actor 不会重建体素

源码实证（`VoxelRendererMeshHandler.cpp:268`）：

```cpp
NewMesh = NewObject<UVoxelProceduralMeshComponent>(RootComponent, ...);
```

渲染网格是**创建为 WorldRoot 的子对象**。网格顶点存在组件局部空间，
组件变换负责把它搬到世界位置。所以：

| 操作 | 影响 LOD 吗 |
|---|---|
| 移动 `ACosmosBodyActor` | ❌ 不影响，网格跟随变换 |
| 改生成器 `Center` | ✅ **会重建**，这是要避免的 |

### 所以分成两套坐标模式

生成器参数只在 `OnConstruction` / `BeginPlay` 求值一次。
运行时 Actor 被 N 体系统移动不会触发重建——
所以 `Center` 必须**锚在局部原点**，由 Actor 变换承担全部位移。

这就是 `AVoxelPlanetActor::bUseLocalCoordinates`（默认开启）的作用。
关掉它则退化为「球心跟随 Actor 位置」，只适合固定不动的场景。

### 分层职责

| 层次 | 谁负责 | 是否运动 |
|---|---|---|
| 宏观轨道 | N 体求解器 | ✅ 每帧 |
| 星球自转 | `UpdateSpin` | ✅ 每帧（体素激活时冻结） |
| 体素地形 | 局部坐标固定 | 只随 Actor 变换 |

远处体素星球被隐藏，不产生任何渲染与内存开销。

---

## 编辑器配置步骤

### 1. 放一个恒星

拖入 `CosmosBodyActor`，设置：

| 字段 | 值 |
|---|---|
| BodyType | `Star` |
| MassSolar | `1.0` |
| VisualRadiusAu | `0.2`（放大到看得见） |
| LightIntensity | `20000` |
| Orbit.Preset | `Custom`（恒星自己不动） |

### 2. 放一颗行星

再拖入一个 `CosmosBodyActor`：

| 字段 | 值 |
|---|---|
| BodyType | `Planet` |
| MassSolar | `3.003e-6`（地球） |
| VisualRadiusAu | `0.02` |
| Orbit.Preset | `Circular` |
| Orbit.SemiMajorAxis | `1.0` |
| Orbit.Eccentricity | `0.0167`（地球真实值） |
| Orbit.InclinationDegrees | `0` |
| CentralMassSolar | `1.0` |
| SpinDegreesPerSecond | `0.5` |
| AxialTiltDegrees | `23.4` |

### 3. 挂载体素星球

**方式 A：建立父子关系（推荐，最省事）**

在关卡里把 `AVoxelPlanetActor` **拖到行星 Actor 下面**，形成父子关系：

```
World Outliner 里：
    BP_Sun
    BP_Planet          ← 把 BP_VoxelPlanet 拖到这里下面
    BP_VoxelPlanet
```

或在视口里：选中 `AVoxelPlanetActor`，按住 **Ctrl** 拖到行星 Actor 上。

运行时会自动识别并打印：
```
[Cosmos] BP_Planet 自动识别到子级体素星球 BP_VoxelPlanet
```

**代码只认父子关系，不需要手动填字段。**
（`bAutoFindChildVoxelPlanet` 默认开启，关掉才需要手动指定 `VoxelPlanetActor`。）

**方式 B：手动指定字段**

行星的 Details 面板 → `Cosmos | Voxel` 分类 → `Voxel Planet Actor` 字段 → 拖入它。
适用于父子关系不可用的情况。

### 4. 设置 GameMode

```
Project Settings → Maps & Modes → GameMode = ACosmosGameMode
```

---

## 运行后应该看到什么

```
启动 → 日志：
  [Cosmos] 自动调优：最短周期 0.99999 年 → 步长 1.000e-05 年
  [Cosmos] 初始化完成：2 个天体，步长 1.000e-05 年，TimeScale 100.0
  [Cosmos] XxxPlanet 体素星球 已停用

靠近行星（日志）：
  [Cosmos] XxxPlanet 体素星球 已激活

远离（再回来）：
  [Cosmos] XxxPlanet 体素星球 已停用
```

**如果看到「已激活」重复刷屏**，说明 `VoxelActivationDistanceAu`
正好卡在相机距离上，把阈值调大或加个迟滞（hysteresis）避免抖动。

---

## 常用真实天体数据

| 天体 | MassSolar | SemiMajorAxis (AU) | Eccentricity |
|---|---|---|---|
| 水星 | 1.66e-7 | 0.387 | 0.2056 |
| 金星 | 2.447e-6 | 0.723 | 0.0068 |
| 地球 | 3.003e-6 | 1.000 | 0.0167 |
| 火星 | 3.227e-7 | 1.524 | 0.0934 |
| 木星 | 9.546e-4 | 5.203 | 0.0484 |
| 土星 | 2.858e-4 | 9.537 | 0.0539 |

`VisualRadiusAu` **不要填真实半径**（地球真实是 4.26e-5 AU，小到看不见）。
经验值：岩石行星 `0.02`，气态巨行星 `0.08`，恒星 `0.2`。

---

## 光照设计：为什么用平行光而不是点光源

UE 的点光源是平方反比衰减：

$$I = \frac{I_s}{d^2} \times \text{saturate}\left(1 - \left(\frac{d}{r}\right)^4\right)^2$$

代入太阳系尺度（1 AU = 1.5e13 cm，默认衰减半径 1000 cm）——**差 10 个数量级**。
即使强行放大半径，行星处强度也是恒星处的 1e-24，需要天文数字级强度，
而且靠近恒星的行星全被烧成纯白。

**平行光（DirectionalLight）是无限远光源，没有距离衰减**，
正好对应「恒星照亮整个星系」这个物理事实。

### 多恒星系统

每颗恒星给每个受照天体各挂一个平行光：

```
BP_PlanetA
  └── DirectionalLight  ← 来自 BP_Sun_A
BP_PlanetB
  ├── DirectionalLight  ← 来自 BP_Sun_A
  └── DirectionalLight  ← 来自 BP_Sun_B   双星系统
```

**UE 的平行光之间会自动叠加**，所以双星系统天然支持——
一颗行星被两个方向的恒星同时照射，贡献自动相加。

### 关键参数

| 字段 | 说明 |
|---|---|
| `LuminosityLux` | 恒星的辐射强度，**单位是 lux**，不是 cd。太阳在地球处约 1.0 |
| `StarLightRangeAu` | 照明半径。超出范围的恒星不提供光，省掉无意义的灯光 |
| `IlluminatingStars` | 留空 = 自动使用场景中所有恒星。填了就只用指定的 |
| `LightMode` | `Directional`（默认，推荐）或 `Point`（仅紧凑小系统） |

### 为什么强度会随距离变化

```cpp
Light->SetIntensity(Star->LuminosityLux / FMath::Max(DistanceAu * DistanceAu, 1e-6));
```

虽然平行光本身无衰减，但**远处恒星看起来应该更暗**——这是真实的视觉规律。
用平方反比归一化到 1 AU 处的强度。

这样木星（5.2 AU）上的一束光会比地球上暗 27 倍，符合直觉。

### 不投影

平行光设了 `CastShadows = false`。AU 尺度下阴影贴图毫无意义且极贵。

---

## 轨道拖尾（A 方案：纯 C++ 位置历史条带）

挂在 `ACosmosBodyActor` 上，记录最近 N 帧位置，渲染成渐隐的三角带。

### 为什么不用 Niagara

| 原因 | 说明 |
|---|---|
| 资产依赖 | Niagara System 必须手工在编辑器里做，代码无法生成 `.uasset` |
| 时间不同步 | Niagara 寿命按「时间」计，`TimeScale` 调大后尾迹长度不可预测 |
| 可测试性 | 纯 C++ 版本可在无渲染环境下测试 |

### 工作方式

```
每帧（或每 SampleInterval 秒）：
  1. 记录当前世界坐标到环形缓冲头部
  2. 计算每个点的「展开方向」= 视线方向 × 轨迹方向
  3. 每个点生成两个顶点（左右各偏移 TrailWidth/2）
  4. 相邻四点构成两个三角形
  5. UpdateMeshSection 写入动态网格
```

**关键技巧**：展开方向用「视线 × 轨迹」算，而不是固定轴。
这样条带永远正对摄像机，不会侧过去看不见。

### 可调参数（在 TrailComponent 上）

| 参数 | 默认 | 说明 |
|---|---|---|
| `TrailLength` | 64 | 历史点数。64 点 @60fps ≈ 1 秒轨迹 |
| `TrailWidth` | 2000 cm | 条带宽度 |
| `HeadColor` / `TailColor` | 蓝→深蓝 | 头尾插值渐变 |
| `SampleInterval` | 0.05 s | 采样间隔。防止 TimeScale 变化时长度剧变 |
| `MinSampleDistanceAu` | 0 | 移动太小时跳过采样，避免慢速公转时轨迹挤成一团 |

**Actor 上只有一个开关**：`bShowTrail`（默认 true）

### 局限

拖尾不会让行星「动得更快」。太阳系里行星速度 30 km/s，
1 AU 外看每帧位移几乎为零——拖尾只是视觉辅助，让运动可感知。

---

## 已知限制

### 1. 远处的星球是球体网格，不是体素

`VoxelInvoker` 挂在相机上——相机不动，远处星球的 chunk 就不在内存里。
你飞过去时才开始生成。

**所以「体素星球在轨道上」是视觉上的。** 玩家永远看不到远处星球上的体素细节。

### 2. 大坐标精度

1 AU = 1.5e13 cm，float 只剩 7 位有效数字，轨道位置误差约几百公里。
N 体内部用 `FVector3d` 是准确的，但**写回 Actor 时会转成 float**。

外太阳系（> 1000 AU）会出现肉眼可见的抖动。近距离（< 100 AU）没问题。

### 3. 每颗星球一个体素世界

9 颗行星 = 9 个 Voxel World。LOD 切换缓解了，但**全部近距离停靠时依然是 9 个**。

### 4. 体素星球激活时自转冻结

自转会让整个世界跟着转。玩家站在星球表面时，地球几秒转一圈的效果会让人眩晕。

所以激活体素时自动冻结自转（远景球体仍正常转，提供视觉暗示）。
想完全不自转（潮汐锁定）就把 `SpinDegreesPerSecond` 设成 0。

---

## 诊断

在 GameMode 上调 `LogDiagnostics()`，输出会包含：

```
[Cosmos] 能量漂移  : 2.34e-09  (辛积分应<1e-5)
```

这是判断 N 体实现是否正确的硬指标。数值稳步增大就说明积分器有问题。
