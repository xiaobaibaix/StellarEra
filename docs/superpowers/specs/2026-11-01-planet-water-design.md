# 行星水体设计（海洋 / 湖泊 / 河流）

- **日期**：2026-11-01
- **状态**：设计已确认，未实施
- **影响模块**：`StellarEraPlanet`（Terrain 改动 + 新增 Hydrology / Water 两个子目录）
- **取代**：`docs/VoxelTestStudy/WATER_SYSTEM.md`（那是旧代码库 `VoxelFactory` 时代的设计；
  「潮汐 / 水塘储量 / 抽水」不在本设计范围内，本设计只做水面本身）

---

## 1. 目标与边界

### 1.1 目标

在体素行星上加入**看得见的水**：

1. **海洋**：海平面以下是一片真实的水面，半透明，能看见水下地形；
2. **湖泊**：内陆盆地在溢出高度形成水面，每片湖有自己固定的水位；
3. **河流**：水从高处流向低处，汇入湖泊或海洋；河道在地形上真的被下切出来；
4. **全程序化**：水域由地形高度场自动推演，换 seed / 调参数就长出全新的水系；
5. **平滑水面**：水面是连续曲面（透明度、深度渐变、岸边泡沫、滚动法线），
   不是体素方块。

### 1.2 非目标（本期明确不做）

| 不做 | 原因 / 后续 |
|---|---|
| 游泳、浮力、下潜 | 本期是纯视觉水面；角色站在海底。水位场是现成接口，后续可加 |
| 潮汐、水体随天体引力涨落 | 需要把水位接进 N 体；本期不做 |
| 流动的水、挖开地形后水自动灌入 | 水面是静态几何；见 §12 |
| 水作为资源被抽取 / 储量记账 | 旧设计里有，属于玩法层，不在本期 |
| 手工摆河湖 / 编辑器笔刷 | 用户选择全程序化；后续可在水文场之上加覆盖层 |
| 分块水面 + LOD | 方案 2；本期静态整星，后续升级不冲突（§13） |

### 1.3 已确认的设计决策

| # | 决策 | 理由 |
|---|---|---|
| 1 | 水的层次 = **视觉水面** | 用户选择：先要「看起来有水」，不为游泳/流体付架构代价 |
| 2 | 水域来源 = **全程序化** | 用户选择：与「换 seed 就换地貌」的玩法一致，零手工 |
| 3 | 水面形态 = **平滑曲面** | 用户选择：体素地 + 平滑水，不做方块水 |
| 4 | 实现 = **水面独立几何**（方案 1） | 不动体素世界，性能可控；水位场可直接给后续游泳用 |
| 5 | 水文 **同步烘焙**，按参数哈希缓存 | 512/面约 0.3–1 s，远小于同一轮体素生成；首次进关卡立即有水 |
| 6 | 海底**也叠地质矿色** | 海底是真实岩层，行为与陆地一致；相关测试要改 |
| 7 | 岸线由「水面 ∩ 体素地形」自然裁出 | 水面几何刻意探入地形，绕开精确描边这个最难的问题 |

---

## 2. 现状

当前 `UVoxelPlanetNoiseTerrain` 已经有一个叫「海平面」的东西，但它不是水：

- `ComputePlanetOffset`（`Source/StellarEraPlanet/Src/Terrain/VoxelPlanetNoiseTerrain.cpp`）
  末尾是 `FMath::Max(Noise, Params.SeaLevel)` —— 低于海平面的噪声被**削平成一块实心平面**；
- `ComputePlanetSurfaceColor` 把这块平面涂成水色 —— 于是「海」是角色能走上去的地面；
- 全星只有一个海平面半径，**结构上无法表达不同高度的湖泊与河流**；
- 现在看起来没水，很可能是 `DA_Terrain_*` 里 `SeaLevel` 设得低（fbm 归一化后大多在 ±0.5 内），
  这一点在实施前要用编辑器确认。

本设计保留「海平面」这个参数，但改变它的语义：它只定义海洋水面的高度，
不再参与地形形状。

---

## 3. 总体架构

```
地形模块参数（Seed / Amplitude / Frequency / Octaves / SeaLevel / ...）
        │
        ├──→【水文烘焙】───────────────────→ FPlanetHydrologyField（只读快照）
        │      Priority-Flood 填洼 + ε 平地     · 水位栅格（体素半径）
        │      D8 汇流 + 河道下切                · 下切深度栅格
        │      湖泊簇 / 流线提取                 · 河流强度栅格
        │                                        · 湖泊列表 / 河流折线
        │
        ├──→【体素生成器】读水文场：地表高度（减去下切）、颜色
        └──→【水面几何构建】读水文场：海洋球壳 / 湖泊面片 / 河流条带
                                           │
                                           └──→ UPlanetWaterComponent（挂在行星 Actor 上）
```

**一条硬约束**：水文场与体素生成器必须使用**同一个高度函数**，否则水会悬空或埋进地里。
因此 `ComputeRawPlanetOffset` 是唯一实现，两边共用 —— 与现有「地形模块与生成器实例
共用 `ComputePlanetOffset`」是同一个理由。

---

## 4. 水文场

### 4.1 数据结构

```cpp
// PlanetHydrologyTypes.h —— 纯数据，无 UObject，可跨线程
struct FPlanetHydrologyParams
{
    float SeaLevel        = -0.35f;  // 归一化；与地形模块的 SeaLevel 同源
    float Amplitude       = 8.f;     // 体素；把归一化位移换成体素
    float MinRiverFlow    = 24.f;    // 汇流量阈值（格数）；决定河网密度
    float MaxCarveDepth   = 4.f;     // 体素；河道最大下切
    float RiverWidthScale = 1.f;     // 河宽系数
    int32 Resolution      = 512;     // 每个 cubemap 面的格数
};

struct FPlanetLakeBasin
{
    FVector CentroidDir;            // 湖泊中心方向
    float   WaterLevelRadius;       // 水面半径（体素，含 Radius）
    float   AngularRadius;          // 角半径（弧度），用于铺面片
};

struct FPlanetRiverPoint
{
    FVector Dir;                    // 单位方向
    float   WaterLevelRadius;       // 水面半径（体素）
    float   WidthVoxels;            // 河宽（体素）
};

struct FPlanetRiverPolyline
{
    TArray<FPlanetRiverPoint> Points;  // 从源头到汇入点，水位单调不增
};
```

```cpp
// PlanetHydrologyField.h
class FPlanetHydrologyField
{
public:
    struct FSample
    {
        float WaterLevelRadius;  // 水面半径（体素）；陆地上等于地形高度（无水）
        float CarveDepth;        // 河道下切深度（体素）
        float RiverStrength;     // 0..1，汇流量归一化
    };

    bool IsValid() const;
    FSample Sample(const FVector& UnitDir) const;   // cubemap 双线性，跨面接缝处理
    float GetSeaLevelRadius() const;                // Radius + Amplitude * SeaLevel
    float GetMaxCarveDepth() const;                 // 给区间查询的包络
    const TArray<FPlanetLakeBasin>& GetLakes() const;
    const TArray<FPlanetRiverPolyline>& GetRivers() const;
};
```

**单位**：场里一律存**体素半径**（= 到球心的距离，体素），不存归一化值。
生成器在体素空间直接比较；水面几何也直接用。`Radius` 在烘焙时已知，不需要运行时再乘。

### 4.2 栅格布局与接缝

复用 `PlanetBiomeCubemap` 的面序与 `FaceUVToDirection` 映射 —— 同一套约定，
避免出现两套 cubemap 布局。

每个面存 `(N + 2)²` 的带边栅格：四周各留一圈**来自相邻面**的副本。
双线性采样因此永远不需要判断「是否跨面」，只有烘焙阶段构造边圈时要做一次跨面映射。
角点取对角面的值（三面交汇处三种取法差别在 1 格内，对水位无可见影响）。

### 4.3 烘焙算法

`FPlanetHydrologyBake::Bake(HeightFn, Params, Radius) → TSharedPtr<const FPlanetHydrologyField>`

`HeightFn` 是 `TFunctionRef<float(const FVector& UnitDir)>`，由地形模块提供
（见 §7 的 `GetRawOffset`）——水文层因此不需要 include 任何地形头。

1. **采样**：每格 `UnitDir = FaceUVToDirection(face, u, v)`，
   `H = Radius + Amplitude × HeightFn(UnitDir)`（体素）。
2. **Priority-Flood + ε**（Barnes 2014）：以「海洋格」（`H < SeaLevelRadius`）为种子，
   最小堆按高度弹出。邻居低于当前水位则抬到当前水位并标记为「淹」；
   否则保留自己的高度并作为新的种子。ε 项保证平地也有确定的流向。
   - 没有海洋（整颗星都高于海平面）时，以全网格最低的若干格作种子 —— 内流盆地。
3. **结果**：每格得到填洼后的水位 `W ≥ H`。湖泊 = `W > H + ε` 的连通块。
4. **流向**：Priority-Flood+ε 同时产出 D8 流向；湖面平地区域由 ε 项解出流向，
   湖从溢出点排出。
5. **汇流量**：按流向从高到低累加（每格初始 1）。`Accum > MinRiverFlow` 且非湖泊 → 河道。
6. **下切**：`CarveDepth = clamp(1 + 2 × sqrt(Accum / MinRiverFlow), 1, MaxCarveDepth)`。
7. **特征提取**：
   - 湖泊连通块 → `FPlanetLakeBasin`（质心方向、水位、角半径）
   - 河道路径 → 从汇流终点反向追溯源头得到折线，抽稀后得到 `FPlanetRiverPolyline`，
     点上的水位取填洼后的 `W`（天然单调不增），宽度由汇流量折算。

**确定性**：每格的浮点运算顺序固定（并行只用「按格独立」的采样阶段，填洼与汇流单线程），
同一个 seed 必定得到同一片水域。

### 4.4 成本

512/面 = 6 × 512² ≈ 157 万格。

- 采样阶段：每格一次 fbm（Octaves 层 Perlin），可并行 → 约 0.2–0.5 s
- Priority-Flood + 汇流：单线程 `O(N log N)` → 约 0.3–0.6 s
- 总计约 0.5–1 s，一次重建只跑一次。

`HydrologyResolution` 可调（64–2048）。256 用于快速预览，1024 用于最终画面。

---

## 5. 地形侧改动

### 5.1 去掉海平面削平

`ComputePlanetOffset` 末尾的 `FMath::Max(Noise, Params.SeaLevel)` 删除，
重命名为 `ComputeRawPlanetOffset`，输出域从 `[SeaLevel, 1]` 变成 `[-1, 1]`。

所有调用点跟着改：`VoxelPlanetNoiseTerrain.cpp`（实例、`GetSurfaceRadiusVoxels`、
配色）、`PlanetBiomeCubemap.cpp`（烘焙）、相关测试。

### 5.2 河道下切

地表半径从

```
BaseSurface = Radius + Amplitude × RawOffset(dir)
```

变成

```
BaseSurface = Radius + Amplitude × RawOffset(dir) − CarveDepth(dir)
```

`CarveDepth` 从水文场双线性采样。采样发生在现有的「地表不确定带」内，
带外仍然走两条早退分支，挖掘热路径的开销不变。

### 5.3 区间与早退的包络

| 项 | 改前 | 改后 |
|---|---|---|
| 位移下界 | `SeaLevel` | `−1` |
| 地表半径下界 | `Radius + Amplitude × SeaLevel` | `Radius − Amplitude − MaxCarveDepth` |
| `GetValueRangeImpl` 里代入的地表半径下界 | 用 `MinOffset = SeaLevel` | `MinOffset = −1`，并再减 `MaxCarveDepth` |

不确定带加宽约 `(1 + SeaLevel) × Amplitude + MaxCarveDepth`（默认参数下 ≈ 7 体素）。
这是本方案唯一的地形性能代价：插件会少跳过一些区块。`Amplitude` 与
`MaxCarveDepth` 都是可调旋钮。

### 5.4 配色

`ComputePlanetSurfaceColor` 删掉「低于海平面 = 蓝色水面」的分支，改为：

| 区域 | 颜色 |
|---|---|
| 海平面以下 | 浅滩沙 → 深水暗岩，按深度插值；水的蓝色与透明度交给水面材质 |
| 河道内 | 湿岩 / 湿沙（比周围地面暗一档） |
| 陆地 | 保持现有的沙 / 草 / 岩 / 雪分带与极地雪 |

同一份函数继续同时服务顶点色与 biome cubemap，LOD 边界的颜色一致性不会回归。

**矿色**：现有逻辑是「`Offset > SeaLevel` 才叠矿色」。本期改为**海底也叠**，
因为海底是真实岩层，透过半透明水看会自然混色。相关断言要改。

### 5.5 表面半径 API

`GetSurfaceRadiusVoxels` 语义不变（仍返回地形面），但现在包含海底与河床。
`FVoxelPlanetTerrainContext` 增加 `TSharedPtr<const FPlanetHydrologyField> Hydrology`
（与 `Geology` 并列）。纯球模块不读它。

---

## 6. 水面几何与材质

### 6.1 三块几何

| 水体 | 几何 | 做法 |
|---|---|---|
| 海洋 | 以星心为球心的球壳 | cube-sphere（与水文场同一套 `FaceUVToDirection`），三角边长 2–4 m。双面渲染，水下抬头能看到水面 |
| 湖泊 | 每片湖一块恒定水位的球面片 | 以湖心方向为中心的局部切平面网格，覆盖 `AngularRadius + 2 格`，多余部分探入地形 |
| 河流 | 沿流线的条带 | 中轴取自 `FPlanetRiverPolyline`，每段在切平面上向两侧偏移 `Width/2 + 0.5 m`，顶点半径取该点的 `WaterLevelRadius` |

**岸线策略（本设计的关键支点）**：三块水面都刻意**探入地形内部**。
水面与体素地形相交，裁剪由深度测试天然完成，于是：

- 水面几何不需要精确贴合海岸线，几何粗一点只是远处少细节；
- 不会出现「水与岸之间漏出一条缝」或「水悬在岸上空」；
- 湖面与河带只需要保证「水存在的地方一定覆盖」，边界多出来的部分自动被地形挡住。

**河流出河口处理**：河带在进入湖泊/海洋前终止（提前一个网格步长），
避免半透明水面重叠显示成一条更深的带子。终止点的水位与湖/海水位一致，接缝不可见。

### 6.2 载体

复用 `UDynamicMeshComponent` —— 矿体视图已经在用同一套
（`Source/StellarEraPlanet/Src/Minerals/View/PlanetMineralViewActor.cpp`），
不需要启用新的插件。

`UPlanetWaterComponent`（`USceneComponent` 子类）挂在 `AVoxelPlanetActor` 上，
内部持有：

- `TArray<TObjectPtr<UDynamicMeshComponent>> SurfaceMeshes`（海洋一块、湖泊每片一块或合并、河流合并）
- `TArray<TObjectPtr<UMaterialInstanceDynamic>> Materials`

组件是行星 Actor 的子组件，变换随行星，不需要额外同步。重建入口
`Rebuild(const FPlanetHydrologyField&, const FPlanetWaterConfig&)`。

### 6.3 材质 `M_PlanetWater`

由 `FPlanetWaterMaterialBuilder` 在 C++ 里构建（照 `PlanetAtmosphereMaterialBuilder`
的写法），参数：

| 参数 | 作用 |
|---|---|
| `ShallowColor` / `DeepColor` | 按水深插值的水色 |
| `Opacity` / `DepthFadeDistance` | 浅处更透明、深处更实 |
| `FoamColor` / `FoamDepth` | 深度差小于阈值处叠泡沫 |
| `NormalStrength` / `NormalSpeed` | 两层法线贴图错速滚动 |
| `SpecularPower` / `FresnelPower` | 高光与菲涅尔 |
| `RefractionStrength` | 屏幕空间折射；v1 可为 0 |

- 半透明、双面
- 深度渐变与泡沫使用 `SceneDepth` 与像素深度之差（材质里的标准做法）
- 法线按**世界位置 triplanar** 采样，避免球面 UV 的极点挤压与经线接缝
- 光照走场景，昼夜与天体光照自动生效

### 6.4 性能

- 全部静态几何，draw call 为个位数
- 海洋球壳面数约 5–10 万（1 km 半径行星，2–4 m 边长）
- 湖泊与河流面数远小于海洋
- 半透明的高填充率只在「水面占满屏幕」时才有感知

---

## 7. 代码结构与依赖

```
Source/StellarEraPlanet/Src/
├── Hydrology/
│   ├── PlanetHydrologyTypes.h          纯数据（参数、湖泊、流线）
│   ├── PlanetHydrologyField.h/.cpp     只读快照 + 采样
│   └── PlanetHydrologyBake.h/.cpp      采样 → 填洼 → 汇流 → 下切 → 特征提取
├── Water/
│   ├── PlanetWaterSurfaceBuilder.h/.cpp  水文场 → FDynamicMesh3
│   ├── PlanetWaterComponent.h/.cpp       挂行星的水面组件
│   └── PlanetWaterMaterialBuilder.h/.cpp 建 M_PlanetWater
└── Terrain/                            （改动，不新增目录）
```

**依赖方向**：

- `Hydrology` 不 include `Terrain`：烘焙只接受高度回调与参数
- `Terrain` 持有 `TSharedPtr<const FPlanetHydrologyField>`（进 `FVoxelPlanetTerrainContext`）
- `Water` 依赖 `Hydrology` 与 `Terrain`（要采样地形高度做湖泊余量），不被它们依赖

`UVoxelPlanetTerrainModule` 增加两个虚函数：

```cpp
/** 未削平的归一化位移（0 = 基准球面）。纯球模块返回 0。 */
virtual float GetRawOffset(const FVector& UnitDir) const;

/** 水体配置。返回 false = 这颗星不要水（纯球模块就是 false）。 */
virtual bool GetWaterConfig(FPlanetWaterConfig& OutConfig) const;
```

`FPlanetWaterConfig` 同时承载水文参数与外观参数（海平面、分辨率、汇流阈值、
下切深度、水的浅/深色、泡沫色……）。这样「火星预设」「金星预设」可以各自
是一套完全不同的水貌，而水文层不需要认识具体的地形子类。

---

## 8. 时序与缓存

`AVoxelPlanetActor::RebuildPlanet`：

```
1. 地形参数快照（含 SeaLevel / HydrologyResolution / 汇流阈值 / 下切深度）
2. 计算参数哈希；与缓存一致则复用水文场，否则同步烘焙
3. 构造 Context（Geology + Hydrology）→ 生成器实例 → CreateWorld
4. 水面几何构建 → UPlanetWaterComponent::Rebuild
5. 水材质参数
```

- 缓存键 = 形状参数 + 海平面 + 分辨率 + 汇流阈值 + 下切深度
- 只改外观参数（水色、泡沫）不重烘水文，只更新材质
- 编辑器与 PIE 都走同一条路径，与现有 `InvalidateAndRebuild` 一致

**参数（新增）**，都在地形模块上：

| 参数 | 默认 | 说明 |
|---|---|---|
| `SeaLevel` | −0.35 | 语义改为「海洋水面高度」，不再削平地形 |
| `HydrologyResolution` | 512 | 每面格数；256 快速预览，1024 最终画面 |
| `RiverFlowThreshold` | 24 | 汇流量阈值；越大河网越稀 |
| `MaxCarveDepth` | 4 | 河道最大下切（体素） |
| `RiverWidthScale` | 1 | 河宽系数 |
| `WaterShallowColor` / `WaterDeepColor` | 蓝 | 原 `ShallowWaterColor` / `DeepWaterColor` 改名复用，资产里的值保留 |
| `bEnableWater`（Actor 上） | true | 整颗星关水 |

---

## 9. 测试

### 9.1 自动化

前缀 `StellarEra.Planet.Hydrology.*`：

- 合成高度回调（解析的环形山 + 盆地），断言填洼后湖水位 = 溢出高度
- 沿流线水位单调不增；河网最终汇入海
- `ComputeRawPlanetOffset ∈ [−1, 1]`
- cubemap 接缝两侧采样差 < ε（含角点）

前缀 `StellarEra.Planet.Water.*`：

- 海洋球壳半径 = `Radius + Amplitude × SeaLevel`
- 湖面片的水位与场里一致
- 河带顶点高度单调、无 NaN
- 网格法线朝外

**更新现有测试**：

- 区间覆盖（`RangeCoverage`）加入 `CarveDepth` 项
- `PlanetGeologyPresetTests` / 配色测试里「水面不叠矿色」的断言改成「海底叠矿色」

### 9.2 PIE 验收（`UniverseMap`）

1. 从轨道看：海面、海岸线、内陆湖泊清晰可见
2. 落地：沿海岸走能看到水陆交界，水是半透明的，能看见水下地形
3. 沿河走：河道是真的凹下去，向上游走高度单调上升
4. 湖：内陆盆地有水面，湖面平坦
5. 帧率：水面在画面里占据大半屏时无明显掉帧

---

## 10. 已知风险

| 风险 | 说明 | 应对 |
|---|---|---|
| cubemap 接缝与角点 | 邻接拓扑写错会让填洼在水域边缘出怪（水位泄漏） | 接缝连续性测试专门钉这一条 |
| 河网密度 | 阈值决定「毛细血管」还是「主干河」 | 两个旋钮；先给默认值，PIE 里调 |
| 半透明排序 | 水面与大气壳、矿物视图、星图后期的透明排序 | 实机看；必要时给水面单独排序优先级 |
| 烘焙卡顿 | 改形状参数时同步烘焙 | 已接受；远小于同一轮的体素生成 |
| 边界重叠 | 河流与湖泊/海洋接缝处半透明叠色 | 河带提前终止 |

---

## 11. 与现有系统的关系

- **大气**：水面在球外看不到（行星不透明），球内天空由大气子系统负责，互不干扰
- **星图**：水面不影响 `RaycastSurfaceCm`（它交体素地形）；星图看到的是地形轮廓，
  海岸线左右两边的颜色差异来自地形配色
- **编辑**：挖掘改的是体素数据，水面几何不跟着变（§12）
- **无水下出生逻辑**：本项目当前没有出生/放置逻辑，本期不新增

---

## 12. 明确限制（v1）

1. **水面不随挖掘更新**：挖开陆地靠近水面的地方，可能露出水面片探入地形的那部分，
   表现为一段悬空的薄水面。重新构建水面（重建行星）即可恢复。
2. **角色下水站在海底**：没有游泳与浮力，这是本期的定义，不是缺陷。
3. **水位不随时间变化**：没有潮汐、没有蒸发。

---

## 13. 分期实施

| 期 | 内容 | 验收 |
|---|---|---|
| 1 | 去削平 + 水文烘焙 + 河道下切 + 调试配色 | 能看出河网从山上汇进海；地形有真实海底 |
| 2 | 海洋球壳 + `M_PlanetWater` | 能看见海、岸线、透明度 |
| 3 | 湖泊面片 | 内陆盆地有湖，湖面平坦 |
| 4 | 河流条带 | 河真正连到湖/海，沿河高度单调 |
| 5 | 打磨 | 泡沫、深度色、法线滚动、折射 |

后续升级方向（不属于本设计）：分块水面 + LOD（方案 2）、游泳/浮力、潮汐。
三者都只消费水位场与水面组件，不需要推翻本期结构。
