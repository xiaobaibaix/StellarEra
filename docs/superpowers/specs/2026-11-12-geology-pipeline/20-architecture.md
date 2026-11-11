# 代码架构

> 这份回答「代码怎么组织」，不回答「地质对不对」（那是 `01`–`12`）。
> 前置阅读：`00-pipeline.md` §2（阶段契约）、`10-density-field.md`（密度契约）、`11-material-field.md`（材质契约）。

## 0. 一句话

**烘焙期用图，运行期用核。**

阶段（Stage）只在烘焙时存在，可以慢、可以虚函数、可以用 UObject；体素线程只见到一个扁平结构体和一行 inline 函数。中间隔着一个**不可变快照**。

这条分界线画错，后面所有性能预算都是纸面数字。

---

## 1. 先否掉三个诱饵

### 1.1 不要做「节点图逐体素求值」

Voxel Plugin 自带节点图，看起来正好是「一个环节叠一个特征」的现成实现。**不能用**。

`10-density-field.md` 已经算过一次：1280 体素挖掘 ≈ 2×10⁹ 次 `GetValue`。节点图的单次求值是「遍历 9 个节点 × 虚函数派发 × 每节点缓存查询」，实测量级 3–8 µs，比手写核慢一个数量级：

| 实现 | 单点 | 2×10⁹ 次 |
|---|---|---|
| 手写扁平核（目标） | 0.4 µs | 13 分钟 |
| 节点图逐体素 | 4 µs | 133 分钟 |

而且节点图会**隐藏带宽**：值域是逐节点推导的，任何一层写漏就是静默出洞。手写核里带宽是一个字段，改不了忘。

**结论**：节点图只在烘焙期用来表达阶段依赖（而且用 C++ 类而不是资产图），运行期是一段直白代码。

### 1.2 不要做 FName 键的黑板

`00-pipeline.md` §2 写的是「声明读哪些黑板键」。**那是给拓扑检查用的，不是运行时容器。**

如果真做成 `TMap<FName, FVariant>`：

- 拼错键 → 运行期才炸，而且炸在烘焙里，堆栈隔了七层
- 每个读者要 `Cast<FField2D>` → 一次虚转型
- 无法静态分析「谁写了这个键」

**改成类型化结构体**：

```cpp
struct FPlanetBakeFields
{
    FGeoField2D Elevation;        // 相对 R0，体素
    FGeoField2D DetailAmp;        // 细节噪声幅度，0–255 归一化
    FGeoField2D Carve;            // 河谷/盆地，体素
    FGeoField2D Surface;          // RGBA8：岩性索引 + 线索类型 + 线索强度
    FGeoProfile  Radial;          // 1D：圈层界面 + 元素丰度
};
```

拓扑检查改为**编译期 + 运行时断言**：每个 Stage 声明 `GetReads()` 返回位掩码（枚举，不是 FName），调度器检查「读的位没人置」→ 报错。拼错是编译错误，不是运行期崩溃。

### 1.3 不要把阶段做成 UObject / Actor

七个阶段各挂一个 `UDataAsset`，结果是：改一个参数要跳 7 个资产，资产之间互相引用，谁先谁后靠人记。而阶段参数总量不到 100 个 float。

**一个配方资产 + 七个嵌套 USTRUCT**：

```cpp
USTRUCT() struct FPlanetStageSphereParams { ... };     // 阶段 1
USTRUCT() struct FPlanetStagePlateParams  { ... };     // 阶段 2
...

USTRUCT() struct FPlanetRecipeConfig       // 纯 POD，不含 UObject 指针
{
    float Radius = 500.f;
    float VoxelSize = 100.f;
    int32 Seed = 1337;
    FPlanetStageSphereParams Sphere;
    FPlanetStagePlateParams  Plates;
    // ...
};
```

阶段本身是**无状态 C++ 类**（不是 UCLASS），由 pipeline 按 `Config` 构造。作者入口仍然只有一个。

---

## 2. 三层分离

```
┌─ A 烘焙期 ───────────────────────────────────────────┐
│  游戏线程拷参数 → 后台任务 → 7 个 Stage 顺序执行        │
│  可慢（秒级）、可用 UObject 读参数、可虚函数            │
└──────────────────┬───────────────────────────────────┘
                   │ 产出（只此一次）
┌─ B 快照 ─────────▼───────────────────────────────────┐
│  FPlanetSnapshot：纯数据、不可变、无 UObject 引用       │
│  TSharedPtr<const FPlanetSnapshot> 跨线程共享          │
└──────────────────┬───────────────────────────────────┘
                   │ 读取（每秒百万次）
┌─ C 求值期 ───────▼───────────────────────────────────┐
│  FPlanetShapeEval / FPlanetMaterialEval：全 inline     │
│  体素线程、无锁、无分配、无虚函数                       │
└───────────────────────────────────────────────────────┘
```

| 层 | 线程 | 频率 | 允许什么 | 禁止什么 |
|---|---|---|---|---|
| A 烘焙 | 后台任务 | 每颗星球 1 次（配置变更时） | UObject、动态分配、虚函数、`exp`/`sqrt` 随便用 | 触碰 World、`GetWorld()`、游戏线程状态 |
| B 快照 | 只读共享 | — | 数组、float/int、BVH | **任何 UObject 指针**（GC 与线程双雷） |
| C 求值 | 体素线程 | 2×10⁹ / 次挖掘 | 内联、查表、少量 `sqrt` | 虚函数、`new`、加锁、日志 |

现状代码已经无意中做对了 B 层（`FPlanetGeologyField` 与 `FPlanetHydrologyField` 都是 `TSharedPtr<const>`、构造后不可变、不含 UObject）。这份架构只是把它从「两个特例」提升成「唯一形态」。

---

## 3. 数据契约

### 3.1 什么进快照（求值期真的要读的）

只留四张球面场 + 一个剖面 + 一个索引 + 一个包络。**每多一张场，单点求值就多一次双线性采样（≈15 ns），快照多几十 MB。**

| 成员 | 类型 | 谁写 | 求值期用途 |
|---|---|---|---|
| `Elevation` | `int16` 定点（1/16 体素），相对 `R0` | 2 → 3（累加） | 主地表 |
| `DetailAmp` | `uint8` | 3 | 细节噪声幅度（山里大、平原小） |
| `Carve` | `uint8`（0–255 映射到 0–`MaxCarve`） | 6 | 河谷/盆地差集 |
| `Surface` | `RGBA8` | 1/3/4/7 合成 | 粗 LOD 与线索（见 §5.2） |
| `Radial` | `FGeoProfile`（几十个 float） | 1 | 深部圈层、背景岩性 |
| `Primitives` | `FGeoPrimitiveIndex`（BVH） | 1/4/6 | 火山锥、侵入体、矿体 |
| `Envelope` | 4 个 float | 各阶段累加 | 早退带 + 值域 |

### 3.2 什么**不**进快照

这一节比上一节重要。烘焙期的产物有十几样，**求值期只需要其中五样**。剩下的留在烘焙期，用完丢弃：

| 不进快照 | 原因 |
|---|---|
| `PlateId` / `BoundaryType` / `BoundaryDistance` | 阶段 2 自己用来撒矿、算陆块，之后只在**烘焙期**被 3 读。求值期地形已经烘焙进 `Elevation` |
| `ContinentalMask` / `ContinentalGrad` | 同上。且 `∇M` 是烘焙期中间量，进快照纯属浪费 |
| `CrustType` / `CrustThickness` / `CrustAge` | 只决定撒矿位置与岩性，结果已体现在 `Surface` 与 `Primitives` |
| `Temperature` / `Precipitation` / `Biome` | 只在烘焙期决定风化与侵蚀。群系 cubemap 是另一套（`PlanetBiomeCubemap`），走自己的烘焙 |
| `FlowAccum` / `ErosionDepth` | 结果已进 `Carve` |
| `StrataColumns`（3A） | 若做，逐格柱表是几十 MB；只在求值期需要岩性时**现算**（1D 剖面 + 噪声），不存 |
| `WeatheringDepth` / `SoilType` | 已进 `Surface` 的 R 通道与线索 |

**规则**：一个字段进不进快照，判据只有一条——「`GetValueImpl` / `GetMaterialImpl` 里会不会读它」。为调试可视化而留下的字段，走单独的编辑器快照（`FPlanetGeoDebugView`），不进运行期快照。

### 3.3 内存

球面场是 6 面 × `N²` 个 texel。`N = clamp(nextPow2(πR/3), 128, 1024)`：

| `N` | texel 数 | int16 单场 | uint8 单场 | RGBA8 单场 | **四场合计** |
|---|---|---|---|---|---|
| 512 | 1.57 M | 3.1 MB | 1.6 MB | 6.3 MB | **≈ 12 MB** |
| 1024 | 6.29 M | 12.6 MB | 6.3 MB | 25.2 MB | **≈ 50 MB** |

**`00-pipeline.md` §3.2 把 `faceRes` 上限定在 2048，这里改判为 1024。** 理由：

- `R=1000` 时 `N=1024` 的 cell 是 `πR/2N = 1.53` 体素，正好压在「1.5 格可辨」的门槛上，够用
- `N=2048` 四场合计 200 MB/星球，一艘船路过多颗星会直接吃光显存/内存，而收益是 0.77 格 vs 1.53 格——唯一受益者是海岸线，可以靠 shader 的细节噪声补

顺带：`FGeoField2D` 烘焙时顺手建 3 级 mip。粗 LOD 读 mip2，避免远处采样 alias 导致「相机一动线索就闪」（`11-material-field.md` §7 第 4 条验收直接依赖它）。

---

## 4. 阶段接口

```cpp
// 只在烘焙期存在，运行期不会有实例
class IPlanetStage
{
public:
    virtual ~IPlanetStage() = default;

    virtual EPlanetStageId GetId() const = 0;
    virtual EPlanetFieldMask GetReads() const = 0;    // 位掩码，不是 FName
    virtual EPlanetFieldMask GetWrites() const = 0;

    /** 无状态：状态全在 Ctx 里。同一个 Stage 对象可复用于多颗星球。 */
    virtual void Bake(FPlanetBakeContext& Ctx) const = 0;
};
```

`FPlanetBakeContext` 提供四样东西：

```cpp
struct FPlanetBakeContext
{
    const FPlanetRecipeConfig& Config;
    FPlanetBakeFields&         Fields;       // 可变，阶段自己写
    FGeoRng&                   Rng;          // 已按 (Seed, StageId) 派生的独立流
    FPlanetBakeOutput&         Out;          // Primitive 列表 / Envelope / 统计 / 警告
};
```

三条硬规则：

1. **只读前序**。`GetReads()` 里的位必须已被更早的阶段置上，否则 pipeline 直接 `check` 失败。这条替代「靠人记得顺序」。
2. **`Bake` 是 `const`**。阶段不许攒状态，否则第二颗星球会带着第一颗的残留。
3. **写自己那格**。并行烘焙时每个线程写自己负责的 texel / primitive，禁止跨线程归约（浮点顺序不定 → 不确定性，见 §7.3）。

---

## 5. 求值核

### 5.1 形状：六步，每步有预算

```cpp
FORCEINLINE float FPlanetShapeEval::GetValue(const FVector& Rel, int32 LOD) const
{
    const float Dist = Rel.Size();

    // ① 早退（3 ns）：带外直接返回保守常量，连方向都不算
    if (Dist < Envelope.Lo - DensityScale) { return (Dist - Envelope.Hi) / DensityScale; }
    if (Dist > Envelope.Hi + DensityScale) { return (Dist - Envelope.Lo) / DensityScale; }

    const FVector3f Dir = FVector3f(Rel / Dist);

    // ② 宏观（15 ns）：一张 2D 场双线性，已含陆台 + 山带 + 边界剖面
    float Surface = Radius + Snapshot.Elevation.Sample(Dir);

    // ③ 细节（100–150 ns）：N octave fBm，幅度由 2D 场调制，LOD 决定 octave 数
    if (LOD <= DetailLODCutoff)
    {
        Surface += Snapshot.DetailAmp.Sample(Dir) * DetailFbm.Sample(Dir, LOD);
    }

    // ④ 下切（15 ns）
    Surface -= Snapshot.Carve.Sample(Dir);

    // ⑤ 原语（0 或 60–150 ns）：**只在近地表**才查 BVH
    if (bHasPrimitives && FMath::Abs(Dist - Surface) < PrimitiveBand)
    {
        Surface += Snapshot.Primitives.SampleExtrusion(Rel, Dir);
    }

    return (Dist - Surface) / DensityScale;
}
```

| 步 | 典型耗时 | 省不掉的理由 |
|---|---|---|
| ① 早退 | 3 ns | 整块剔除的命脉，`2×10⁹` 次里有 99% 走这条 |
| ② 2D 采样 | 15 ns | 阶段 2 的 7 块板块 × fBm 不可能逐体素重算（`00` §4.2） |
| ③ 细节 fBm | 100–150 ns | 唯一必须逐体素的项。用 `LOD` 断掉远处 |
| ④ 下切 | 15 ns | 河床 |
| ⑤ BVH | 60–150 ns | 用 `|Dist − Surface|` 闸门挡住 95% 的调用 |
| 合计 | **≈ 300 ns** | 预算 1.5 µs，留 5 倍给后续阶段 |

三个**必须现在写进去**的决定：

- **`LOD` 短路**。现状 `GetValueImpl` 收了 `LOD` 却没用。`LOD ≥ 4` 时细节噪声在屏幕上是亚像素，跳过它省 30–40% 的时间，肉眼看不出差别（本来就该由 mip 承载）。
- **原语距离闸门**。不设闸门的话每个体素都查一次 BVH，`2×10⁹ × 100 ns` = 3 分钟纯等。用「距当前地表 < `PrimitiveBand`」挡掉，实际查询率 < 5%。
- **`PrimitiveBand` 必须进 `Envelope`**。它是带宽的一部分，不是常数经验值。阶段 4 上线时报「我把带宽推了 X 格」，否则剔除失效没人发现。

### 5.2 材质：唯一编解码点

`GetMaterialImpl` 只做两件事：拿到 `FGeoSample`，交给 `FGeoMaterialCodec`。

```cpp
FORCEINLINE FVoxelMaterial FPlanetMaterialEval::GetMaterial(const FVector& Rel, int32 LOD) const
{
    FGeoSample S;
    S.Lithology = Snapshot.Surface.Sample(Dir).R;          // 粗 LOD 直接用烘焙值
    if (LOD <= MaterialLODCutoff)
    {
        S = Snapshot.Primitives.SampleMaterial(Rel, Dir, S); // 近处查 BVH，矿物/品位/线索
    }
    return FGeoMaterialCodec::Encode(S);                     // 全项目唯一出口
}
```

`11-material-field.md` §5 说「编解码只允许一处」，架构上就是这一个函数 + 一个 `MF_GeoDecode`，通道表由代码生成 shader 常量。

### 5.3 三个入口必须同源

这是最容易出人命的一条。地形有三个对外入口：

| 入口 | 调用者 | 现状 |
|---|---|---|
| `GetValueImpl` | 体素网格化 | `ComputeRawPlanetOffset` + 水文 + 地质 |
| `GetSurfaceRadiusVoxels` | 角色落地、相机、载具 | 同一份 `ComputeRawPlanetOffset` |
| `GetRawOffset` | 水文烘焙 | 同上 |

现在它们共用 `ComputeRawPlanetOffset`，所以「眼睛看到的地面 = 站上去的地面」。

**快照化之后，如果模块层还留着老解析式，而实例层改读快照，角色会站到山里或空气里**——而且只在特定 LOD / 特定板块上出现，极难查。

规则：**`FPlanetShapeEval::GetSurfaceRadius(Dir)` 是唯一的地表函数**，三个入口全部转发它，`ComputeRawPlanetOffset` 随老实现一起删掉。验收：随机 10⁴ 个方向，三个入口的返回值两两之差 < 0.01 体素。

---

## 6. 目录与模块

### 6.1 目录（`Source/StellarEraPlanet/Src/Geo/`）

```
Geo/
  Config/
    PlanetRecipe.h              // FPlanetRecipeConfig + 七个阶段的 Params
  Noise/
    GeoNoise.h/.cpp             // Perlin / fBm / ridged / domain warp，确定性
    GeoRng.h                    // 按 (Seed, Stage, Purpose) 派生，禁全局状态
  Fields/
    GeoField2D.h/.cpp           // 6 面 + padding + mip + 双线性
    GeoProfile.h                // 1D 径向剖面（插值）
  Primitives/
    GeoPrimitive.h              // 形态原语（对应 12-primitives.md 的 7 种）
    GeoPrimitiveIndex.h/.cpp    // BVH（构建在烘焙期，查询在求值期）
  Bake/
    PlanetBakeContext.h
    PlanetBakePipeline.h/.cpp   // 拓扑排序 + 执行 + 统计
    PlanetStage.h               // IPlanetStage
    Stages/
      Stage01Sphere.cpp
      Stage02Plates.cpp
      Stage03Terrain.cpp
      Stage04Magmatism.cpp
      Stage05Climate.cpp
      Stage06Erosion.cpp
      Stage07Weathering.cpp
  Snapshot/
    PlanetSnapshot.h            // 冻结产物（§3.1）
    PlanetShapeEval.h           // header-only inline
    PlanetMaterialEval.h
    GeoMaterialCodec.h/.cpp     // 唯一编解码
  Rules/
    MineralRule.h               // 撒矿规则（烘焙期）
    MineralCatalog.h/.cpp       // 29 种矿的读表
```

### 6.2 依赖规则（这才是架构）

| 目录 | 允许 include | 禁止 |
|---|---|---|
| `Geo/Noise` `Geo/Fields` `Geo/Primitives` | Core、Math | **Engine、UObject、Voxel** |
| `Geo/Bake` | 上面 + `Geo/Snapshot` | World、Actor、Component |
| `Geo/Snapshot` | 上面 | **任何 UObject** |
| `Src/Terrain/*` | 全部 | 自己实现地形数学（必须转发 `PlanetShapeEval`） |

`Src/Terrain/VoxelPlanetNoiseTerrain.cpp` 会从 412 行掉到 80 行：它只剩「取快照 → 转发三次求值」。

规则靠一条测试守住，不靠自觉：

```cpp
// 扫 Geo/{Noise,Fields,Primitives,Snapshot} 下所有文件，出现 "CoreUObject.h"
// 或 "Engine/" 或 "VoxelWorld.h" 就 FAIL
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGeoCoreDependencyTest, ...)
```

### 6.3 现在**不**拆新模块

技术上 `Geo/` 完全可以独立成 `StellarEraPlanetGeo` 模块。**现在不拆**，理由：

- 单模块改动成本低：新文件不用想「属于哪个模块」、跨模块 UCLASS 不用写 `STELLARERAPLANET_API`
- 边界靠目录 + 一条测试已经能守住，模块边界是更贵的手段，不是更有效的手段
- 单人项目，多一个模块就多一份 `Build.cs` 维护和一次全量重编译

**拆分触发条件**（任一成立就拆）：

1. 要在 commandlet / 独立工具里跑烘焙（不加载 World）
2. `Geo/` 的编译时间占到 `StellarEraPlanet` 的 40% 以上
3. 要把同一套地质用在地球外的项目里

---

## 7. 线程、生命周期、确定性

### 7.1 烘焙不上游戏线程

现状水文烘焙（6 面 × 512 的 flow accumulation）在游戏线程跑，是秒级卡顿。架构上要求：**所有 Stage 的 `Bake` 在后台任务执行**，游戏线程只负责「拷参数」和「收快照」。

```cpp
// 游戏线程
FPlanetRecipeConfig Cfg = Recipe->ToConfig();       // UObject → POD，唯一一次
Async(EAsyncExecution::ThreadPool, [Cfg, WeakThis]
{
    TSharedPtr<const FPlanetSnapshot> Snap = FPlanetBakePipeline::Bake(Cfg);
    AsyncTask(ENamedThreads::GameThread, [WeakThis, Snap] { /* 换快照 + 重建世界 */ });
});
```

进度上报：`FPlanetBakeOutput::Progress`（原子 float），UI 每帧读。别在 worker 上碰 UMG。

### 7.2 顺序与重入

- **同一颗星球同时只允许一个烘焙任务**；再次请求 → 取消旧的（`FThreadSafeBool bCancelled`，Stage 在每个大循环里查一次）
- 换快照必须是**整体替换**：`Snapshot = NewSnap`，旧的靠 `TSharedPtr` 引用计数自然销毁。不要原地改字段（体素线程正在读）

### 7.3 确定性：存档里只有 recipe + seed

这是程序化世界最容易欠下的债。**要求：给定 `(Config, Seed)`，任何机器、任何线程数、任何次数，烘焙结果必须字节一致。** 否则存档会随版本膨胀，玩家回档发现地形变了。

六条禁令：

| 禁止 | 替代 |
|---|---|
| `FMath::Rand` / `FMath::FRand`（全局状态） | `FGeoRng`，按 `(Seed, StageId, Purpose)` 派生 |
| 遍历 `TMap` / `TSet` 决定顺序 | 用 `TArray` 并在写入前 `Sort` |
| 并行归约（`ParallelFor` 累加同一个 float） | 每格独立写自己的 texel；必须归约时按固定顺序单线程 |
| 用 `FPlatformTime` / 帧号影响输出 | 种子只来自 `Config` |
| 用 `FVector`（double）做中间累加 | 定点或固定顺序的 float 累加序列 |
| 依赖 `AActor` 的 transform / 世界状态 | `Config` 里什么都有 |

验收（写进测试）：同 seed 连烘两次 → 快照 SHA1 相同；强制单线程 vs 8 线程 → 相同；`Radius` 改一位 → 不同。

### 7.4 编辑层与快照的关系

编辑器里挖洞走 `PlanetEditComponent` → `SaveObject`（Voxel 插件的存档层），**不进快照**。

```
最终体素 = 快照求值 ⊕ SaveObject 的编辑差分
```

规则：`SaveObject` 里记一个版本戳 = `hash(Radius, Seed, RecipeVersion)`。改半径/改种子后戳不匹配 → **弹警告并要求确认**（确认后清空编辑，或保留但告知地形会错位）。现在没这个戳，改半径会静默留下飘在空中的旧编辑块。

---

## 8. 性能护栏

架构解决「能不能扩展」，护栏解决「半年后还成不成立」。

| 指标 | 采集点 | 红线 |
|---|---|---|
| 单点 `GetValue` | micro benchmark（10⁶ 次取均值） | 1.5 µs |
| 单点 `GetMaterial` | 同上 | 2 µs |
| 早退命中率 | 运行期两个原子计数器（带内 / 带外） | ≥ 90%，低于就报「谁推了带宽」 |
| 烘焙耗时 | 每个 Stage 分段计时，进 `FPlanetBakeOutput::Timings` | 单阶段 ≤ 2 s，全流程 ≤ 10 s |
| 快照内存 | `GetAllocatedSize()` | ≤ 60 MB/星球 |
| 早退带宽 | `Envelope.Hi - Envelope.Lo` | ≤ 15% R |

**黄金星球回归测试**：固定 `Config`（R=500, Seed=1337），采样 10⁵ 个固定方向的 `GetValue`，把结果哈希存进测试常量。任何阶段上线后哈希变了 → 说明你改了地形，需要显式更新基线（而不是「测试挂了随手改期望值」）。同一次跑顺带断言耗时不上涨 20%。

这条测试是整份架构唯一能挡住「七个人各加一点，最后慢十倍」的东西。

---

## 9. 迁移路线

**别推倒重写。** 现状 5000 行里有 6 个地形相关测试（`PlanetTerrainEarlyOutTests`、`RawOffsetTests`、`MinSurfaceTests`、`HydrologyCarveTests`、`GradientNormalTests`、`VoxelPlanetBiomeMaterialTests`），它们就是重构的安全网。

| 步骤 | 做什么 | 验收（必须全绿才进下一步） |
|---|---|---|
| **M0** | 引入 `FPlanetSnapshot` 外壳 + 一个 `FLegacyStage`，把现状三处内联逻辑原样搬进去。**行为零变化** | 6 个地形测试全绿；10⁵ 点采样哈希与改动前逐字节一致 |
| **M1** | 阶段 1 + `FGeoProfile`：圈层、背景岩性 | 深部剖面可视；值域仍由 Legacy 覆盖 |
| **M2** | 阶段 2 + 阶段 3，`Elevation` 改为读 2D 场 | 大圆穿越次数 ≤ 10；陆地面积达标；带宽 ≤ 15% R |
| **M3** | 阶段 4 + `GeoPrimitiveIndex` + `GeoMaterialCodec`（**S1 竖切**） | 「飞过去见黄色结壳 → 挖到硫磺」端到端 |
| **M4** | 阶段 5/6/7 增量叠加 | 每层单独回归带宽 + 耗时 |

M0 的收益容易被低估：它把「重构」和「改行为」彻底分开。M0 之后每一步只动一个阶段，任何回归都能二分定位。

---

## 10. 与现有代码的三个冲突点

| # | 冲突 | 处理 |
|---|---|---|
| 1 | 地形有三个入口，目前共用 `ComputeRawPlanetOffset` | 全部改转发 `FPlanetShapeEval::GetSurfaceRadius`，删掉老函数。**不同源 = 角色站错地面** |
| 2 | `UVoxelPlanetTerrainModule::GetSurfaceRadiusVoxels` 是 `const` 虚函数，拿不到快照 | 把快照作为模块的 `TSharedPtr` 成员（非 UPROPERTY，`Transient` 语义），`GetInstance` 时写入；`const` 方法读它。注意读写时序：**先建快照，再交给实例，再让模块回答查询** |
| 3 | 编辑层（SaveObject）与快照各自演进 | 加版本戳 + 不匹配告警（§7.4） |

顺带一条：`PlanetMineralViewActor` / `PlanetMineralVolume` 已经是矿体可视化前端，**别重写**——把它的数据源从 `FPlanetGeologyField::GetPods` 换成 `FGeoPrimitiveIndex` 的枚举接口，它就是新架构的调试器。

---

## 11. 架构级验收

七条，全部可自动化：

1. **依赖**：`Geo/{Noise,Fields,Primitives,Snapshot}` 不出现 Engine/UObject/Voxel 的 include。
2. **同源**：三个地表入口在 10⁴ 个随机方向上两两相差 < 0.01 体素。
3. **确定性**：同 `(Config, Seed)` 两次烘焙哈希相同；1 线程 vs 8 线程相同。
4. **无泄漏**：快照里没有任何 `UObject*`（静态断言 + 一条 `check` 遍历）。
5. **预算**：单点 `GetValue` ≤ 1.5 µs、`GetMaterial` ≤ 2 µs。
6. **带宽**：`Envelope` 宽度 ≤ 15% R，且等于各阶段上报增量之和（允许 10% 误差）。
7. **黄金星球**：固定 seed 的 10⁵ 点哈希与基线一致，耗时不上涨 20%。

前六条是「架构没走形」，第七条是「行为没漂移」。
