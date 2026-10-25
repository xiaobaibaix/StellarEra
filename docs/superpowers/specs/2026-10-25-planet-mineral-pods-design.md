# 行星矿物：3D 矿囊模型 + 资产化参数 + 编辑器预览

- **日期**：2026-10-25
- **状态**：设计已确认，待写实施计划
- **影响模块**：`StellarEraGeology`、`StellarEraTerrain`、`StellarEraMinerals`、`StellarEraUI`（图例只读数据）、`Content/Geology/`
- **取代**：`2026-10-24-planet-geology-design.md` 的「深度高斯带」分布模型与「JSON 唯一编辑入口」两条决策

---

## 1. 目标与边界

### 1.1 问题

现有实现里，矿物观测视图与地形视图**显示的不是同一批矿**：

- 地形按 50 cm 体素在**真实地表深度**采样，切过的是浅层带（硅 5、铁 30、铜 50 体素附近）；
- 观测视图是 40³ 的全局立方栅格，格子边长 2R/40 = 2000 体素，只够看到深层带（1.2–11 km）。

两套深度窗口几乎不相交，于是地形上满屏彩环、观测视图里却是另一批东西。

同时，`Norm_i = 高斯带(Depth) × 矿脉` 把每个元素锁死在一个深度窗口内（相邻窗口只重叠 700–1100 体素，还要两个独立噪声同时过门），所以「一个元素一层、无重叠」是模型层面的必然结果，不是参数没调好。

### 1.2 目标

1. 矿物分布改成**球内任意位置的 3D 矿囊**，元素之间天然可重叠，不存在同心分层；
2. 观测视图与地形视图**消费同一份场、同一个门限**，观测视图里有面 = 地形上会显色；
3. 每个元素的分布与含量都是**资产上的可调参数**，不写死在 C++ 里；
4. 观测视图的**分辨率跟着矿囊尺寸走**，不再被行星直径稀释；
5. 矿物观测**在编辑器内可预览**，不进 PIE。

### 1.3 非目标（本期不做）

| 不做 | 原因 / 后续 |
|---|---|
| 矿体材质半透明 | 半透明有排序伪影；要看重叠用图例关掉其他元素 |
| 改预设后自动重刷已生成的体素地形 | 体素是烘焙的，需重建行星 |
| JSON 与资产的双向导入导出 | 多一个数据源就多一处不同步（YAGNI） |
| 编辑器里拖动行星时预览实时跟随 | 重新点一次按钮即可 |
| 矿囊的非球形基础形状（椭球、细脉） | 形状差异由噪声 `ShapeDetail` 提供 |

### 1.4 已确认决策

| # | 决策 | 理由 |
|---|---|---|
| 1 | 分布模型 = **3D 矿囊，深度只做弱偏置** | 用户明确要求「没有分层的概念」「多少有重叠」 |
| 2 | 数据形态 = **每行星一份 UDataAsset，元素内联** | 与 `DA_Terrain_*` 一致；JSON 与热重载命令退役 |
| 3 | 编辑器预览 = **组件 Details 按钮 + 视口就地进出** | 见 §5.1 的模块依赖约束 |
| 4 | 观测视图 = **按矿囊簇局部建等值面** | 全局 160³ 栅格需 100 MB、6–18 s，且 99% 空间是空的 |
| 5 | 几何与颜色**共用同一份场与同一个门限** | 这是「两个视图一致」的结构性保证 |

---

## 2. 数据模型

### 2.1 资产位置

```
Content/Geology/Planets/DA_Geology_Earth.uasset
```

`UPlanetGeologyPreset : UDataAsset`，声明在 `StellarEraGeology/Public/PlanetGeologyPreset.h`。Geology 没有项目依赖，Terrain 与 Minerals 都依赖它，方向合法。

### 2.2 顶层字段

| 字段 | 类型 | 默认 | 说明 |
|---|---|---|---|
| `Elements` | `TArray<FPlanetGeologyElementParams>` | 空 | 元素表；**数组顺序 = 运行期采样顺序 = 图例顺序** |
| `Appearance` | `FPlanetGeologyAppearance` | 见 2.4 | 颜色显现参数 |
| `GeometryAmplitude` | `float` | 1.5 | 矿囊把地表顶起的最大幅度（体素） |

### 2.3 `FPlanetGeologyElementParams`（USTRUCT）

| 字段 | 类型 | 默认 | 含义 |
|---|---|---|---|
| `Id` | `FName` | `None` | 稳定标识，仅日志与存档用 |
| `DisplayName` | `FString` | 空 | 图例与日志显示名 |
| `Color` | `FLinearColor` | 白 | 矿体颜色 |
| `Abundance` | `float` | 1.0 | **只影响颜色混合权重与图例占比，不改几何**；ClampMin 0 |
| `PodCount` | `int32` | 16 | 该元素撒多少矿囊；ClampMin 0 |
| `PodRadius` | `float` | 0.05 | 矿囊名义半径，**占行星基准半径的比例**；Clamp 0.001–0.25。重叠对数量按 `PodCount² × (2r)³` 增长——半径砍半，重叠少 8 倍 |
| `PodRadiusJitter` | `float` | 0.5 | 每囊半径的随机抖动幅度（±比例）；Clamp 0–1 |
| `PodDepthBias` | `float` | 0.0 | 深度偏好强度；**0 = 全深度体积均匀**；Clamp 0–0.9 |
| `PodDepthCenter` | `float` | 0.25 | 偏好深度（占半径比例，1 = 球心）；Clamp 0–1 |
| `PodDepthSpread` | `float` | 0.15 | 偏好深度的高斯宽度（占半径比例）；Clamp 0.01–1 |
| `ShapeDetail` | `float` | 3.0 | **一个矿囊直径内大约几个噪声周期**；ClampMin 0.5 |
| `Octaves` | `int32` | 2 | 形状噪声分形层数；Clamp 1–6 |
| `Lacunarity` | `float` | 2.0 | 每层频率倍数；ClampMin 1 |
| `Gain` | `float` | 0.5 | 每层振幅衰减；Clamp 0–1 |
| `Seed` | `int32` | 0 | 0 时由 `Hash(Id)` 派生，保证不同元素默认不相关 |
| `VeinThreshold` | `float` | 0.5 | 噪声稀疏阈值，越高矿囊越破碎；Clamp 0–0.95 |

单位一律**归一化空间**（行星基准半径 = 1）：换行星半径不会让矿囊相对变大变小。

### 2.4 `FPlanetGeologyAppearance`（改为 USTRUCT）

字段与默认值不变：`ColorThreshold 0.22`、`FadeRange 0.15`、`PurityForFullColor 0.7`。改成 USTRUCT 才能在资产里编辑。

> 默认值只对新建元素/新资产生效。`DA_Geology_Earth` 的实际取值见 §8。

### 2.5 运行期类型

`FPlanetGeologyElement`（地质系统内部的非 USTRUCT）保留，字段变化：

- 删除 `Bands`、`ReachDepth`、`DepthScale`
- 删除元素级的 `Frequency`（频率语义换成 `ShapeDetail`，折叠进噪声坐标）
- 新增 `TArray<FPlanetGeologyPod> Pods` 与上表的播撒参数

```cpp
struct FPlanetGeologyPod
{
    FVector Center;      // 归一化空间
    float Radius = 0.f;  // 归一化空间
};
```

`FPlanetGeologyBand` 整个删除。

### 2.6 接入点

| 位置 | 现在 | 之后 |
|---|---|---|
| `AVoxelPlanetActor::GeologyPreset` | `FString`（JSON 名） | `TObjectPtr<UPlanetGeologyPreset>` |
| `AVoxelPlanetActor::GetGeologyPresetOptions()` | 列 JSON 文件 | 删除（资产选择器取代） |
| `AVoxelPlanetActor::ReloadGeology()` | 重读 JSON | 保留：丢缓存 + 重建行星 |
| `UVoxelPlanetGenerator::GeologyPreset` | `FString`，**`VisibleAnywhere`** | `TObjectPtr<UPlanetGeologyPreset>`，**仍必须是 `VisibleAnywhere`** |
| `UVoxelPlanetGenerator` 缓存键 | `预设名 + 半径` | `预设指针 + 半径 + 预设 Revision` |

> `VisibleAnywhere` 不是风格问题：VoxelFree 的 `UVoxelGenerator::GetParameters` 只认 9 种参数类型，对象指针属性若带 `EditAnywhere`，选中行星 Actor 会弹 ensure。

---

## 3. 场模型

### 3.1 建场

```cpp
static TSharedPtr<const FPlanetGeologyField> BuildFromPreset(
    const UPlanetGeologyPreset& Preset, double PlanetRadiusVoxels);
```

必须在**游戏线程**调用（要读 UObject 资产），构造函数用 `check(IsInGameThread())` 兜住。建完即不可变，`TSharedPtr<const ...>` 可跨线程共享。

字段内部存 `RadiusVoxels`，把外部签名 `(UnitDir, Depth)` 换算成归一化坐标：

```
P = UnitDir × (1 − Depth / RadiusVoxels)
```

外部调用点（地形、地表射线、观测视图）**签名不变**。

### 3.2 撒矿囊（建场时一次）

每个元素用独立随机流 `FRandomStream(Seed != 0 ? Seed : GetTypeHash(Id))`，重复 `PodCount` 次：

```
u1 = Rng.FRand()                                   // 半径抖动
rp = PodRadius × (1 + PodRadiusJitter × (2·u1 − 1))
rp = clamp(rp, 1e-3, 0.25)

D  = Rng.GetUnitVector()                            // 球面均匀方向

rn = ∛Rng.FRand()                                   // 归一化球内深度：0 = 地表，1 = 球心
PodDepthBias > 0：                                  // 带权拒绝采样，最多 64 次
    重复：t = ∛Rng.FRand()
          若 Rng.FRand() < w(t) 则接受，w(t) = (1−b) + b·exp(−((t−Center)/Spread)²)
    64 次仍不接受则采用第 64 次的 t，rn = t

rc = (1 − rp) × rn                                  // 球心距
C  = D × rc                                         // 矿囊球心
```

`rn` 与 `PodDepthCenter` / `PodDepthSpread` 同一坐标系（都按「地表 0 → 球心 1」的比例）。

两条性质：

- `w(t) ≥ 1 − b > 0`，**任何深度都不会被完全排除**，只是被稀释。「没有分层」是结构性的，不靠调参运气。
- `rc ≤ 1 − rp`，矿囊完全在基准球内，不会戳出球面。露头来自矿囊与地表的自然相交。

### 3.3 求值

对采样点 P 与元素 i：

```
PodFactor_i(P) = max over pods of  (1 − (|P − C| / rp)²)_+      // 多囊取 max
N              = P × (2 · ShapeDetail / PodRadius)              // 噪声坐标
Noise01        = 0.5 × (PerlinFractal_3D(N, frequency=1, Octaves) + 1)
Vein           = clamp((Noise01 − VeinThreshold) / (1 − VeinThreshold), 0, 1)
Norm_i         = PodFactor_i × Vein
Raw_i          = Abundance_i × Norm_i
```

- 频率折叠进坐标、噪声 API 的频率参数传 1，所以 `ShapeDetail` 的语义是「矿囊直径内的周期数」，与矿囊绝对大小解耦。
- `Vein` 每元素每点**只求一次**，不是每囊一次。
- 之后的总量、最强元素、纯度、混合色公式**完全沿用现有实现**：
  `Total = Σ Raw_i`、`DominantNorm = max Norm_i`、`Purity = MaxRaw / Total`、
  `MineralColor = Σ(Raw_i · Color_i) / Total`、
  `SampleGeometryDelta = GeometryAmplitude × clamp(Total / TotalAbundance, 0, 1)`。

### 3.4 空间索引

建场时把所有矿囊塞进一个 `TMap<FIntVector, TArray<FPlanetGeologyPodRef>>`（`FPlanetGeologyPodRef = { ElementIndex, PodIndex }`）：

- 格子边长 = 全局最大矿囊半径（含抖动）；
- 每囊插入其包围盒（球心 ± rp）覆盖的所有格子，最多 8 格。

查询只查 P 所在的那一格。**正确性**：任何包含 P 的矿囊，其包围盒必覆盖 P 所在格，故必在该格的候选表里。

不在任何矿囊内的点**一次 Perlin 都不求**。这使地形的每体素开销不升反降——现有实现每体素要算 6 次高斯，并在深度窗内调 1–2 次分形 Perlin；新模型下绝大多数体素落在空格子里。

### 3.5 一致性不变量

三处消费必须走同一个 `Accumulate`：

| 消费点 | 判据 |
|---|---|
| 地形顶点色（`GetMaterialImpl`） | `DominantNorm > ColorThreshold` |
| 地形几何（`GetValueImpl`） | `SampleGeometryDelta` |
| 观测视图（`PlanetMineralSurface::Build`） | `Norm_i > ColorThreshold`（同一数值） |

地形在**真实地表深度**采样，所以只有够到地表的矿囊会让地表显色。这与观测视图不矛盾：观测视图里看得到但在地形上看不到的那些，都在球体内部，本来就看不见。

### 3.6 空场语义

`IsEmpty()` 返回真当且仅当：没有元素，**或**所有元素的 `PodCount` 都为 0。观测视图据此干净地拒绝进入，而不是生成一堆空网格。

---

## 4. 观测视图

### 4.1 局部栅格

```
每个元素：
  包围盒相交的矿囊并成一簇（并查集，用未外扩的包围盒）
  每簇：
      CellSize = 2 × min(簇内矿囊半径) / CellsPerPod
      栅格范围 = 簇包围盒 ± 1 格
      角点值 = SampleElementNormAtNormalized(ElementIndex, P)
      FMarchingCubes，IsoValue = Appearance.ColorThreshold
      顶点 × RadiusCm，保留 |v| ≤ RadiusCm 的径向夹回
簇间 ParallelFor；所有簇的顶点/三角形并入该元素的网格
```

三条硬性要求：

1. **栅格外扩 1 格是正确性要求**：`PodFactor` 在 `d = rp` 处恰为 0，外扩一格保证 MC 边界角点全为 0。否则等值面会在包围盒边上开洞。
2. **同元素的重叠矿囊必须聚成一簇**：分别建面会在重叠处留下互相穿插的两张面。
3. **分簇用未外扩的包围盒**：外扩后再判断相交会把「刚好相邻但不相交」的矿囊也强行合并，簇越滚越大。不相交的两张面互不干扰，分开建是正确的。

`CellSize` 用**簇内**最小半径，避免「簇取决于格子、格子取决于簇」的循环；单个矿囊的角点数因此被限制在 `(2·r/r_min + 2)³`，只有真正重叠且尺寸悬殊时才会变高。

### 4.2 参数

`UPlanetMineralViewComponent`：

- `GridResolution`（16..96，默认 40）**删除**；
- 新增 `CellsPerPod`（4..32，默认 12）：一个矿囊直径横向切多少格。

这是跟随矿囊的质量旋钮，与行星直径无关。观测视图的地形分辨率问题由此被结构性解决。

### 4.3 成本

| | 现有 40³ | 新（6 元素共 94 囊，CellsPerPod = 12） |
|---|---|---|
| 角点数 | 69k × 6 元素 = 413k | 94 囊 × 14³ ≈ 258k（每点只求自己的元素） |
| 每囊横向格数 | ≈ 1 | 12 |
| 稠密场数组 | 41³ × 6 × 4 B = 1.6 MB | 无；每簇独立，峰值 < 1 MB |
| 噪声求值次数 | ≈ 10 万 | ≈ 10 万（只算囊内角点，约占 41%） |

量级相当，分辨率 12 倍。成本随 `Σ 矿囊体积 / 最小矿囊体积` 线性增长；把 `PodCount` 拉到几百会明显变慢，但那是显式调参的结果。

### 4.4 接口改动

- 新增 `float FPlanetGeologyField::SampleElementNormAtNormalized(int32 ElementIndex, const FVector& P) const`；`SampleElementNorm(UnitDir, Depth)` 转发到它。
- `SampleElementNorms`（唯一生产调用点是 `Build`）退役。
- `PlanetMineralSurface::FBuildParams`：`GridResolution` → `CellsPerPod`；`FCacheKey` 同步。
- `FBuildResult::GridPoints` 语义改为「实际求过场的角点数」，继续进日志。
- `APlanetMineralViewActor` 不改（仍是每元素一个 `UDynamicMeshComponent`，网格数 == 元素数）。

### 4.5 看见重叠

不同元素的矿囊独立播撒，模型上不会互相避让。但矿体材质不透明，同屏只能看到靠前的那层。确认重叠的方法：**用图例关掉其他元素**，露出来的矿囊里会嵌着别的元素。

重叠**不是保证**而是统计结果：两个矿囊相交的概率约 `(r_a+r_b)³`，所以默认数据（§8）只有约 3 对。想看得更明显就调大 `PodRadius` 或 `PodCount`——这是显式的调参，不是魔法。

---

## 5. 编辑器预览

### 5.1 按钮位置

`StellarEraMinerals → StellarEraTerrain`（`Activate(AVoxelPlanetActor*)` 已经是这个方向），所以 Terrain 反向引用 Minerals 会成环，`AVoxelPlanetActor` 的 Details 页面无法直接调矿物组件。

采用：按钮放在 `UPlanetMineralViewComponent` 自己身上，分类 `Planet|MineralView`。零新增依赖、零接口样板。若将来需要「选中行星即可点」，在 Terrain 声明 `IMineralPreviewable` 接口再改。

```cpp
UFUNCTION(CallInEditor, Category="Planet|MineralView")
void EnterMineralPreviewInEditor();

UFUNCTION(CallInEditor, Category="Planet|MineralView")
void ExitMineralPreviewInEditor();

UFUNCTION(BlueprintPure) bool IsPreviewing() const;
```

复用 `Activate(Planet)` / `Deactivate()`，内部按 `World->WorldType == EWorldType::Editor` 分支。

### 5.2 两套分支

| | PIE / 运行时（不变） | 编辑器预览（新） |
|---|---|---|
| 隐藏行星 | `SetActorHiddenInGame(true)` + 关碰撞 | `SetIsTemporarilyHiddenInEditor(true)` |
| 大气 | `bAtmosphereEnabled=false` + `SetVisibility(false)` | 不动（行星被临时隐藏，壳跟着不渲染；退出不触发大气重建） |
| 观察体 | `SpawnActor`，Tick 跟随位置 | `SpawnActor` + `RF_Transient`，定位一次，**不 Tick** |
| 碰撞 | 关掉 | 不动 |
| 退出 | 逐项恢复 | 只撤销临时隐藏 |

**编辑器分支不改任何 UPROPERTY**：翻 `bAtmosphereEnabled` / `bActorEnableCollision` 会把关卡标脏，退出后留下待提交的脏包。临时隐藏是非序列化的编辑器状态，正是为此设计。

### 5.3 生命周期

- `SpawnParams.ObjectFlags |= RF_Transient`，`Owner = 行星`，永远不会写进 umap。
- 退出预览、组件销毁、关卡关闭都走 `Deactivate()` → `Destroy()`。
- `UDynamicMeshComponent` + `M_MineralBlock` 在编辑器视口正常渲染，不需要 PIE。

### 5.4 参数改动后重建

- `UPlanetGeologyPreset::PostEditChangeProperty`：`++Revision`，并在 `WITH_EDITOR` 下广播静态多播委托 `OnPresetChanged`。
- 正在预览的 `UPlanetMineralViewComponent` 订阅它，收到后重新 `Activate()`（重新取场 → 场指针变了 → 重建网格）。
- 地形侧不需要回调：生成器缓存键含 `Revision`，下次取场自动重建。
- 未开预览时无人订阅，零成本。

---

## 6. 迁移与退役

### 6.1 删除

| 位置 | 内容 |
|---|---|
| `Content/Geology/Elements.json`、`Planets/*.json`、`README.md` | 整套 JSON 与说明（README 重写为资产说明） |
| `PlanetGeologyField` | `LoadFromFiles` / `LoadFromString` / `ListPresetNames` / `ParseBand` / `ParseElementTable` / `ParsePlanet` |
| `PlanetGeologyField` | `GetElementDefinitions()`、`ElementOrder`、`MaxReachDepth` |
| `PlanetGeologyTypes.h` | `FPlanetGeologyBand` |
| `VoxelPlanetActor` | `StellarEra.ReloadGeology` 控制台命令、`GetGeologyPresetOptions()` |
| `PlanetMineralSurface` | 全局 `[点][元素]` 稠密场、`GridResolution` |

`AVoxelPlanetActor::ReloadGeology()` 保留，语义改为「丢掉缓存的场 + 重建行星」。

### 6.2 已知限制

改预设**不会重刷已经生成的体素地形**——体素数据是烘焙的，需要重建行星。写进 `Content/Geology/README.md`。

### 6.3 顺序

1. 预设类型 + `BuildFromPreset`（保持场的外部签名）
2. 矿囊播撒 + 空间索引 + 求值
3. 地形调用点（签名不变）与生成器缓存键
4. 观测视图局部栅格 + `CellsPerPod`
5. 编辑器预览 + 预设广播
6. 删 JSON / 命令 / 旧测试，建 `DA_Geology_Earth`
7. 图例与文档：`Content/Geology/README.md`、`CLAUDE.md` 的 `StellarEraGeology` 模块描述（「JSON 预设」→「UDataAsset 预设」）

---

## 7. 测试

### 7.1 保留（改构造输入）

`Geology.Determinism`、`Geology.Bounds`、`Geology.Color`、`Geology.Pipeline`、
`MineralSurface.Build / IsoAccuracy / ElementOrder / Threshold / Empty / Deterministic / EmptySlots / CacheKey`、
`MineralView.ActorBuild / EmptySlot / Empty`、`StarMap.MineralLegend.*`。

### 7.2 删除 / 改名

| 现在 | 处理 |
|---|---|
| `Geology.Parse` | 删除（JSON 退役） |
| `Geology.Bands` | 删除（模型消失） |
| `MineralSurface.SurfaceBand` | 改写为 `MineralSurface.SurfaceOutcrop`（露头矿囊确实出面） |
| `Geology.SampledDefinitions` | 删除（两种顺序合一），并入 `MineralSurface.ElementOrder` |
| `Geology.Veins` | 改写为 `Geology.PodShape` |
| `MineralSurface.GridResolution` | 改名 `MineralSurface.CellsPerPod` |
| `Geology.RangeCoverage` | 改写为 `Geology.PodCoverage`（见下） |

### 7.3 新增

| 测试 | 断言 | 防的是 |
|---|---|---|
| `Geology.PodCoverage` | 每个元素正样本的深度跨度 > 0.5R，且不存在「全元素皆空」的深度区间 | 退回分层 |
| `Geology.PodOverlap` | 固定种子下至少一对元素存在共同覆盖点 | 「看不到重叠」复发 |
| `Geology.AbundanceGeometryInvariance` | 只改 `Abundance`，`SampleElementNorm` 逐点不变 | 含量参数意外改了几何 |
| `Geology.SpatialIndex` | 1000 个随机点上索引查询 == 暴力遍历全部矿囊，逐点相等 | 索引漏囊（静默少矿） |
| `MineralSurface.ClosedMesh` | 每个非空网格的每条边恰好被两个三角形共用 | 外扩一格漏了 → 面上开洞 |
| `MineralSurface.PodLocal` | 每元素网格包围盒落在该元素矿囊包围盒并集外扩 1 格内 | 局部栅格范围算错 |
| `Geology.GeneratorParametersSupported`（扩展） | 预设指针属性是 `VisibleAnywhere`，不是 `EditAnywhere` | VoxelFree 参数类型 ensure |

`SpatialIndex` 与 `ClosedMesh` 是必须的：这两个 bug 都不报错，只表现为「矿看起来少了」或「有破面」。

---

## 8. 初始数据（`DA_Geology_Earth`）

| 元素 | Abundance | PodCount | PodRadius | Seed |
|---|---|---|---|---|
| silicon 硅 | 0.60 | 20 | 0.050 | 303 |
| iron 铁 | 0.75 | 20 | 0.050 | 101 |
| water 水 | 0.20 | 16 | 0.048 | 606 |
| copper 铜 | 0.45 | 16 | 0.045 | 202 |
| titanium 钛 | 0.15 | 12 | 0.042 | 404 |
| uranium 铀 | 0.10 | 10 | 0.038 | 505 |

公共值：`ShapeDetail 3`、`Octaves 2`、`Lacunarity 2`、`Gain 0.5`、`VeinThreshold 0.5`、`PodDepthBias 0`、`PodRadiusJitter 0.5`。
`Appearance`：`ColorThreshold 0.05`、`FadeRange 0.12`、`PurityForFullColor 0.45`。

丰度总和 2.25，图例百分比与今天一致（27/33/9/20/7/4）。

共 94 个矿囊，按上表推算：**跨元素重叠对 ≈ 3**（`Σ N_a·N_b × (r_a+r_b)³`），**地表露头 ≈ 13 处**（`Σ N × 3r`），比现在少一个数量级。

> 早先的草稿用 24 囊 / 半径 0.025：那时重叠对期望只有 **0.04 对**——默认数据下永远看不到重叠，验收项 2 会直接失败。重叠数与体积分数的平方成正比，所以半径不能太小。

这是起点，之后全靠编辑器调参。

---

## 9. 验收

1. `Automation RunTests StellarEra` 全绿（headless，约 25 s）。
2. PIE 按 H：矿囊横向约 12 格；能用图例关掉某元素后看到里面嵌着的别种矿囊。
3. 地形：只有露头矿囊染色，颜色与观测视图同元素同色；海面不再有蓝环。
4. 编辑器：选中组件 → 点「进入矿物预览」→ 视口出现矿囊；退出后 `git status` **没有新增脏包**。
5. `UniverseMap` 重建行星的耗时与当前基线对比不劣化（两个数字都记进实施记录）。

---

## 10. 风险

| 风险 | 缓解 / 退路 |
|---|---|
| 地形每体素多一次哈希查询 | 理论更快（跳过 Perlin），需实测；退路是给生成上下文加一格局部缓存（相邻体素同格命中率高） |
| `SetIsTemporarilyHiddenInEditor` 在 OFPA 关卡下的行为 | 实测；退路是显式记录/恢复 `bHiddenEd` |
| 生成器上的预设指针属性触发 VoxelFree 参数 ensure | 强制 `VisibleAnywhere`，并扩展 `GeneratorParametersSupported` 测试 |
| `PodDepthBias > 0` 会改变整套矿囊位置 | 参数是作者在编辑器里调的，不是运行时改的；接受 |
| 建场读 UObject 必须在游戏线程 | 构造函数 `check(IsInGameThread())` |
