# 矿物视图改用第二套体素世界

- 日期：2026-11-01
- 替代：`docs/superpowers/specs/2026-10-25-mineral-ghost-view-design.md` 的 §4.2
  （矿物几何由「每元素一张 marching cubes 动态网格」改为「每元素一个体素世界」）
- 保留：该 spec 的 §1 目标、§3 进入退出、§4.1 半透明球壳。球壳仍是一颗
  `M_PlanetGhost` 静态球，不改成体素

## 1. 目标

矿物透视视图里，每种矿物由一个**独立的体素世界**渲染，而不是进入时一次性跑
marching cubes 造出全部三角形。

换掉的理由是构建方式与交互需求：

- 现有路径在**进入的那一瞬间**把整颗星的矿物全部抽成网格，耗时随
  「簇数 × 元素数 × `CellsPerFeature`²」增长，改一次密度参数要整个重来；
- 每个元素天生就是独立对象：要单独隐藏、单独染色、单独统计，中间隔一张
  `UDynamicMeshComponent` 数组，只做得到「藏掉那张网格」。

换成体素世界之后，显隐是藏一个 Actor、染色是换它的 MID、LOD 交给插件的
LOD 管理器按相机距离调度 —— 全部是插件已经做好的事。

### 非目标

- **不在矿物视图里做挖掘。** 视图是只读的。
- 不给挖掘系统新增「改矿物数据」的职责（见 §2，不需要）。
- 不把行星本身的体素世界改成半透明材质（沿用 2026-10-25 spec §8 的结论：
  会触发 `RecreateRender` 全量重建）。
- 不为矿物体素世界存盘（它们是纯派生，见 §2）。
- 不做远处高精度 LOD 补偿（见 §8 风险 2）。

## 2. 数据模型：两份真值，一个派生

| 项 | 存在哪 | 谁写 | 挖掘会改吗 | 存盘吗 |
|---|---|---|---|---|
| 地表密度（实心 / 空气） | 行星 `FVoxelData` | 生成器 + `PlanetEditComponent` | **会** | 会 |
| 元素分布 | `FPlanetGeologyField`（解析式纯函数） | 预置资产 `DA_Geology_*` | 不会 | 不存（资产本身就是真值） |
| 矿物视图 | 运行期临时体素世界 | 本设计 | 不适用 | **不存** |

关键结论：**挖掘改变的是密度，不是矿物数据。**

矿物在某个体素上成立的条件是

```
行星在该点实心  ∧  该元素 Norm > 阈值
```

行星被挖空，左边不成立，矿自然消失。所以挖掘系统一行都不用改，也不需要给地质场
加「已采出遮罩」或给每个矿配存档。

> 事实核对（2026-11-01）：`Source/StellarEraPlanet/Src/Edit/` 下 12 个文件
> grep `Geology` 零命中 —— 目前的挖掘只删体素，完全不碰地质场。挖开矿囊之后，
> 新表面上的 `GetMaterialImpl` 照样按解析式着色（它没有「球内早退」，
> 见 `VoxelPlanetNoiseTerrain.cpp` 的 `GetMaterialImpl`）。本设计不改变这一点，
> 只是让矿物视图也遵守同一条判据。

## 3. 架构

### 3.1 类

| 类 | 变化 | 职责 |
|---|---|---|
| `UPlanetMineralVoxelGenerator` | 新增 | `UVoxelGenerator` 子类。一个实例 = 一个元素 |
| `AVoxelMineralFieldActor` | 新增 | `AVoxelWorld` 子类。一个实例 = 一个元素的体素世界 |
| `APlanetMineralViewActor` | 改造 | 宿主：spawn / 销毁 N 个 field actor、球壳、显隐与染色转发、统计入口 |
| `UPlanetMineralViewComponent` | 改造 | 进出模式、元素表、图例 API（对外签名尽量不变） |
| `PlanetMineralSurface.*` | 删除 | marching cubes 路径退役（时机见 §9） |

模块归属全部是 `StellarEraPlanet/Src/Minerals/View/`。`StellarEraPlanet` 已公共
依赖 `Voxel` 插件，不需要动 `Build.cs`。

### 3.2 依赖

```
UPlanetMineralViewComponent
  └─ APlanetMineralViewActor
       ├─ N × AVoxelMineralFieldActor  (AVoxelWorld)
       │    └─ UPlanetMineralVoxelGenerator  (UVoxelGenerator)
       │         ├─ FPlanetGeologyField      (共享指针，只读)
       │         └─ 行星实心判定回调          (只读)
       └─ 球壳 UStaticMeshComponent + M_PlanetGhost MID
```

`AVoxelMineralFieldActor` 与行星**无父子关系**（`AActor::IsHidden` 会让所有子
`USceneComponent::ShouldRender` 返回 false，这也是 2026-10-25 spec §5.1 里
`APlanetMineralViewActor` 必须独立 spawn 的同一个原因）。

## 4. 生成器规格

### 4.1 求值流程

```
GetValue(Context, LocalVoxelPos):
  ① WorldPos = FieldActor 变换 × (LocalVoxelPos · VoxelSize)

  ② 区域早退
     若 WorldPos 不在本元素的任何区域 AABB 内 → 空气
     区域来源：FPlanetGeologyField::EnumerateRegions(ElementIndex, Out)，在
     CreateWorld 之前一次性取好、缓存在生成器实例里

  ③ 密度门
     若行星在该点不是实心 → 空气
     行星索引：因为 §5 让两者的 Transform / VoxelSize / 世界边界完全一致，
     世界空间位置映射到的行星体素索引就等于 LocalVoxelPos，不需要换算
     读法：PlanetWorld->GetData().GetValue(LocalVoxelPos, 0)

  ④ 元素强度
     N = Geology->SampleElementNormAtNormalized(
             ElementIndex, (WorldPos - PlanetCenterCm) / SurfaceRadiusCm)

  ⑤ 屏阈
     Value = (Threshold - N) / FadeRange      // N > Threshold 时为负 = 实心
     Threshold = Field.GetAppearance().ColorThreshold
     FadeRange = Field.GetAppearance().FadeRange
     （与现有 MC 路径完全一致：IsoValue 也取 ColorThreshold）
```

**早退顺序按代价排**：② 纯算术 → ③ 一次八叉树走位 → ④ 地质场求值（最贵）。
绝大多数块在 ② 就被判空，这是这套方案能比 MC 便宜的主要来源。

`Threshold` / `FadeRange` 的复用保证：同一个矿囊在「地表着色」「矿物视图」
两处的位置判据完全相同。

### 4.2 体素值符号

`FVoxelValue` 约定 **负 = 实心**（`TVoxelValueImpl::Full()` 返回
`-MAX_VOXELVALUE`；地形模块 `value = |P-C| - R` 因此球内为负）。

⑤ 的映射因此是 `Threshold - N`：N 越大越负 = 越是矿。

结果需夹到 `[-MAX_VOXELVALUE, MAX_VOXELVALUE]`（int16 量程）。`FadeRange`
是 0.15 量级，`1 / 0.15 ≈ 6.7`，乘上量程后不会溢出；但仍要夹，因为
`FadeRange` 是资产上可编辑的值。

### 4.3 并发

生成器在插件的工作线程上被调用。约束：

- `FPlanetGeologyField` 是不可变对象，`SampleElementNormAtNormalized` 是纯函数，
  可跨线程读。
- 行星 `FVoxelData` 的只读访问：`FVoxelData::Get` 走
  `FVoxelOctreeUtilities::GetBottomNode`，只沿 `HasChildren()` 往下走、**不创建
  节点**，因此并发读安全。头文件注明 `GetValue` "Requires read lock"，
  用 `FVoxelDataLock`（读）包住一批读取。
- 视图期间不做任何挖掘（§1 非目标），所以不存在写锁竞争。

### 4.4 注入与可测性

生成器不直接持有 `AVoxelPlanetActor*`，而是持有：

```cpp
TFunction<bool(const FIntVector& PlanetVoxelIndex)> IsSolidAtPlanetVoxel;
```

运行期由组件绑定到行星的 `FVoxelData` 读取；测试里注入一个假的回调，就能在
不建体素世界的前提下验证 ② ③ ④ ⑤ 四段逻辑。

> VoxelFree 的生成器参数只认 9 种类型，`FString` 的 `EditAnywhere` 会弹 ensure。
> 生成器上的可编辑字段一律用 `VisibleAnywhere` / `EditConst`，不上
> `EditAnywhere`；可调参数（阈值、元素索引）由组件在 `CreateWorld` 之前直接赋值，
> 不通过生成器的参数系统。

## 5. 世界参数对齐

矿物世界的体素索引与行星体素索引必须一一对应，这样 §4.1 ③ 不需要任何坐标换算。

`AVoxelMineralFieldActor` 在 spawn 时从行星复制：

| 属性 | 来源 |
|---|---|
| `VoxelSize` | 行星 `VoxelSize` |
| `RenderOctreeDepth` / `WorldSizeInVoxel` | 行星同名属性 |
| `bUseCustomWorldBounds` / `CustomWorldBounds` | 行星同名属性 |
| `MaxLOD` / `MinLOD` / `LODMaterials` | 行星同名属性 |
| Actor `Transform` | 行星 `GetActorTransform()` |

`MaterialConfig = EVoxelMaterialConfig::RGB`；`VoxelMaterial` 由组件在建世前赋成
该元素的 MID。

不复制：`SaveObject`（保持 `nullptr`）、`Generator`（由生成器覆盖）、
`NumberOfThreads`（沿用默认）。

**线程池**：行星在 `CreateWorld` 之前已用
`UVoxelBlueprintLibrary::CreateWorldVoxelThreadPool` 建好全局池
（`VoxelPlanetActor.cpp` 的 `RebuildPlanet`），矿物世界直接复用，不重复创建。

## 6. 材质、显隐、染色

- 每个 field actor 一个 `UMaterialInstanceDynamic`，由 `M_MineralBlock` 创建，
  设 `BlockColor = Element.Color`。
- **显隐**：`SetActorHiddenInGame(!b) + SetActorEnableCollision(false)`。
  因为矿物世界本来就无碰撞，第二句是保险。
- **染色**：改该 MID 的 `BlockColor`，即时生效，不需要重建世界。
- **已知缺陷（第一版接受）**：隐藏不停止生成。藏起来的那个世界仍然吃线程池。
  处置见 §8 风险 3。

## 7. 生命周期

### 7.1 PIE / 运行时

```
Activate(Planet):
  1. 解析地质场与元素表（沿用现有逻辑与缓存）
  2. spawn APlanetMineralViewActor（不挂在行星下）
  3. 对每个元素 spawn 一个 AVoxelMineralFieldActor
  4. 逐个 CreateWorld(FVoxelWorldCreateInfo())
  5. 隐藏行星、关碰撞、关大气（沿用现有保存-恢复）

Deactivate():
  1. 逐个 DestroyWorld() → Destroy()
  2. 销毁宿主 Actor
  3. 恢复行星网格、碰撞、大气
  重复调用安全
```

### 7.2 编辑器预览

`EnterMineralPreviewInEditor` 走同一套 Actor，但**不能直接调 `CreateWorld()`**：
编辑器世界里插件要求用 `Toggle()`（`AVoxelWorld::OnPreBeginPIE` 在
`PlayType == Preview` 时的既有约束，见 `VoxelPlanetActor.cpp:709-714` 的记录）。

退出预览用 `DestroyWorld()` + `Destroy()`，并且不写任何 UPROPERTY，保证关卡不脏。

### 7.3 行星移动

行星在 N 体里每帧移动。矿物世界必须跟着走。

**field actor 不 attach 到宿主**：`AVoxelWorld` 在构造时把 `WorldRoot` 立为根
组件（与行星那边记录的是同一个原因），再挂一个父级会让体素世界的局部/全局
变换语义打架。改成由 `APlanetMineralViewActor` 在激活期间每帧把行星球心位置
写进每个 field actor 的 `SetActorLocation`。

代价是每帧 N 次 `SetActorLocation`（N = 元素数，当前 5）。这与「用 attach 省掉
这几次赋值」的取舍明显偏向正确性，因此不优化。

## 8. 风险与待测项

| # | 风险 | 处置 |
|---|---|---|
| 1 | §4.1 ③ 的跨世界读取可能慢：每个矿物体素一次八叉树走位 | 在块生成回调里打点测量；若不可接受，退回「只读行星已挖掘区域列表」（从 `FVoxelUncompressedWorldSave` 的编辑盒子里取） |
| 2 | **远处矿脉消失或变成团块**：体素 LOD 按相机距离，行星 `MaxLOD = 6` 时远处块边长 64 米，一条半径 20 米的矿脉在远处低于体素尺度 | **已确认接受**（2026-11-01）。这取代了 MC 路径「整颗星的矿同时可见」的体验 |
| 3 | 隐藏的世界仍在生成，白吃线程池 | 第一版只做 `SetActorHiddenInGame`。若实测有影响，改成「隐藏 = `DestroyWorld`，显示 = 重新 `CreateWorld`」 |
| 4 | 5 个世界与行星抢同一个线程池 | 视图里行星已隐藏，可接受；实测有影响时给矿物世界单开一个较小的池 |
| 5 | 编辑器预览路径与 PIE 路径的建世方式不同（`Toggle` vs `CreateWorld`） | 两条分支各自有日志与失败处理，不共用 `CreateWorld` 调用点 |

## 9. 与 marching cubes 路径的关系

先并存一轮做 A/B 对照（同一个地质场，两条路径各进一次视图比对矿体位置），
确认体素路径的矿体位置与地表着色一致之后，**在同一份实施计划的最后一步删除**
`PlanetMineralSurface.*` 与 `APlanetMineralViewActor` 里的动态网格构建代码。

并存期间由组件上的一个开关选择路径，默认走体素。开关是**过渡用的**，删除 MC
路径时一并删除，不留在主干。

## 10. 统计与导出

**走解析场，不走体素世界。**

原因：体素世界里的体素数量随 LOD 变 —— 远处块边长 64 米、近处 1 米，同一个矿
在相机远近不同时数出来的体素数不同，不是稳定量。

做法：对每个元素

```
1. EnumerateRegions(ElementIndex, Regions)
2. 对每个 Region 的 AABB 按固定步长 h 采样
3. 计数：SampleElementNormAtNormalized(i, P) > Threshold ∧ 行星在 P 处实心
4. 体积 ≈ 计数 × h³
```

得到的值与相机位置无关，两次进入视图结果一致。`h` 由调用方传入（默认取行星
`VoxelSize`）。

导出的格式由调用方决定，本设计只提供

```cpp
float GetElementVolumeCm3(int32 ElementIndex, float StepCm) const;
TArray<float> GetAllElementVolumesCm3(float StepCm) const;
```

两个入口。

## 11. 测试

| 层 | 内容 |
|---|---|
| 生成器单元测试（`Src/Tests/`，不建世界） | ② 区域早退：区域外的点返回空气；③ 密度门：注入 `IsSolidAtPlanetVoxel` 恒 false 时整块空气；④ ⑤ 阈值：`N == Threshold` 时返回 0（等值面），`N` 略大返回负 |
| 组件 API 测试 | `NumElements() == Elements.Num()`；`SetElementVisible` 幂等；`SetAllElementsVisible` 后逐个状态正确 |
| 统计测试 | 同一地质场、同一 `StepCm`，两次调用结果完全相等（与相机无关） |
| 手工（`UniverseMap`） | 进视图 → 世界数 == 该预置的元素数（`DA_Geology_Earth` 为 5）；关掉一种矿 → 它消失；挖一个洞 → 重进视图 → 洞里无矿 |

自动化测试跑 editor world，**不执行 BeginPlay**；任何依赖 PIE 建世的断言都不要
写进 automation test。

## 12. 文件清单

新增：

```
Source/StellarEraPlanet/Src/Minerals/View/
├── PlanetMineralVoxelGenerator.h / .cpp
└── VoxelMineralFieldActor.h / .cpp
```

改造：

```
Source/StellarEraPlanet/Src/Minerals/View/
├── PlanetMineralViewActor.h / .cpp      宿主改为管理 N 个体素世界
└── PlanetMineralViewComponent.h / .cpp  对外 API 尽量不变；加统计入口与路径开关
```

删除（实施计划最后一步）：

```
Source/StellarEraPlanet/Src/Minerals/Surface/PlanetMineralSurface.h / .cpp
```

含中文的 `.h` / `.cpp` 存 UTF-8 with BOM。
