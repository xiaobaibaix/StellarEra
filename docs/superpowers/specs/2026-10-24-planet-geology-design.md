# 行星矿物系统设计

- **日期**：2026-10-24
- **状态**：设计已确认，待写实施计划
- **影响模块**：`StellarEraPlanet` + `Content/Geology/`（JSON 内容）

---

## 1. 目标与边界

### 1.1 目标

给体素行星加一层**矿物分布**：

1. 多种矿物元素，每种元素有**自己的一张密度场**；
2. 多张场合并成地形内部的**物质含量分布**；
3. 该分布**影响地形形状**（矿脉处地表轻微外凸）；
4. 挖掘露出新切面时，按元素含量**混合显示颜色**；
5. 元素表与每颗行星的分布参数都是**可编辑 JSON**，改完不重新编译 C++。

### 1.2 非目标（本期不做）

| 不做 | 原因 / 后续 |
|---|---|
| 开采产出、物品栏、库存 | 本期只留查询接口，见 §10.2 |
| MultiIndex 真实材质（贴图、法线） | 本期用 RGB 顶点色，预留替换点，见 §10.1 |
| 矿藏储量消耗 | 未设计 |
| 文件监听自动重载 | 手动控制台命令即可 |
| 矿脉影响碰撞以外的物理（硬度、密度） | 未来由查询接口支撑 |

### 1.3 已确认的设计决策

| # | 决策 | 理由 |
|---|---|---|
| 1 | 矿物**影响几何** | 用户明确要求；矿脉会在地形轮廓上可见 |
| 2 | 第一版用 **RGB 顶点色混合** | 用户要求，后续可能换真实材质；因此材质输出留单一出口 |
| 3 | 元素场用**程序化噪声参数** | 加元素只改 JSON，不需要新资产，任意元素数量 |
| 4 | JSON 放 `Content/Geology/`，**元素表与行星预设分成两份** | 全局元素表与每行星参数分离；改一个行星不影响其他 |
| 5 | 第一版范围 = **视觉 + 几何 + 查询接口**，不做产出 | 现有挖掘链路（`UPlanetEditComponent`）已经能挖，先打通“看得到” |
| 6 | 实现在**独立 worktree** 中进行 | 用户要求 |

---

## 2. 数据模型

### 2.1 两份 JSON 的职责

| 文件 | 内容 | 变更影响 |
|---|---|---|
| `Content/Geology/Elements.json` | 全局元素表：id、显示名、颜色、噪声参数 | 所有引用该元素的星球 |
| `Content/Geology/Planets/<preset>.json` | 某颗行星：各元素丰度与深度、几何振幅、颜色显现参数 | 只有用这个预设的星球 |

### 2.2 `Elements.json`

```json
{
  "version": 1,
  "elements": [
    {
      "id": "iron",
      "displayName": "铁",
      "color": [0.55, 0.28, 0.16],
      "noise": {
        "frequency": 3.0,
        "octaves": 2,
        "lacunarity": 2.0,
        "gain": 0.5,
        "seed": 101,
        "depthScale": 0.3
      }
    }
  ]
}
```

字段语义：

| 字段 | 类型 | 范围 | 说明 |
|---|---|---|---|
| `id` | string | 唯一、非空 | 行星 JSON 用它引用元素 |
| `displayName` | string | — | 只用于日志/未来 UI |
| `color` | [r,g,b] | 0~1 | 高纯度时的本色 |
| `noise.frequency` | number | > 0 | 单位方向上的噪声频率，越大矿脉越碎 |
| `noise.octaves` | int | 1~6 | 分形层数，性能旋钮 |
| `noise.lacunarity` | number | ≥ 1 | 每层频率倍数 |
| `noise.gain` | number | 0~1 | 每层振幅衰减 |
| `noise.seed` | int | — | 不同元素用不同种子，分布互不相关 |
| `noise.depthScale` | number | (0,1] | 深度混入噪声的强度，见 §3.2；0 会让矿退化成均匀壳 |

### 2.3 `Planets/<preset>.json`

```json
{
  "version": 1,
  "geometryAmplitude": 1.5,
  "appearance": {
    "colorThreshold": 0.22,
    "fadeRange": 0.15,
    "purityForFullColor": 0.7
  },
  "elements": {
    "iron":    { "abundance": 0.75, "peakDepth": 30,  "layerThickness": 20 },
    "copper":  { "abundance": 0.45, "peakDepth": 65,  "layerThickness": 42 },
    "silicon": { "abundance": 0.60, "peakDepth": 48,  "layerThickness": 31 },
    "titanium":{ "abundance": 0.15, "peakDepth": 110, "layerThickness": 72 },
    "uranium": { "abundance": 0.10, "peakDepth": 190, "layerThickness": 124 },
    "water":   { "abundance": 0.30, "peakDepth": 12,  "layerThickness": 12 }
  }
}
```

| 字段 | 默认 | 说明 |
|---|---|---|
| `geometryAmplitude` | 1.5 | 矿脉让地表外凸的最大体素数；`0` = 不影响几何 |
| `appearance.colorThreshold` | 0.22 | 归一化总含量的显现阈值 |
| `appearance.fadeRange` | 0.15 | 从阈值到完全显现的过渡宽度 |
| `appearance.purityForFullColor` | 0.7 | 纯度达到它时颜色完全用矿色 |
| `elements.<id>.abundance` | 0 | 全局丰度，0~1 |
| `elements.<id>.peakDepth` | — | 峰值深度，体素；**正 = 基准球面以下（向行星内部为正）**，缺省 0 |
| `elements.<id>.layerThickness` | — | 高斯层厚，体素，必须 > 0 |

**未在行星 JSON 中出现的元素 = 这颗行星没有该元素**，不需要 `bPresent` 字段。

### 2.4 解析与失败降级

**原则：坏数据退化成“没有矿”，绝不让行星消失或崩溃。**

| 情况 | 行为 |
|---|---|
| 文件不存在 / 读失败 | `Log Error`，返回空场（零元素） |
| JSON 语法错误 | `Log Error`（带文件名），返回空场 |
| 行星 JSON 引用未知元素 id | `Log Warning`，跳过该条 |
| `id` 重复 | 保留第一条，`Log Warning` |
| `layerThickness <= 0` | `Log Warning`，该元素按不存在处理 |
| 数值越界（如 `abundance` 超出 0~1） | 夹到合法范围，`Log Warning` |
| `version` 不是 1 | `Log Warning`，仍按当前 schema 尝试解析 |
| `Elements.json` 为空数组 | 合法：这颗行星没有矿 |

`LoadFromString` 返回一个 `FPlanetGeologyLoadReport`（错误、警告、被跳过/夹取的条目），测试直接断言它。

---

## 3. 数学

### 3.1 深度定义

```
depth = Radius - DistanceToCenter
```

- 单位：体素；
- **以基准球面为 0**，向行星内部为正；
- 不依赖地形起伏（`ComputePlanetOffset`），是纯几何量，因此与地形模块解耦。

### 3.2 单元素原始密度

```
SampleZ = UnitDir.Z + (depth / layerThickness) * depthScale
n_i     = FractalPerlin3D(UnitDir.X, UnitDir.Y, SampleZ, frequency, octaves)   // [-1,1]
noise_i = 0.5 * (n_i + 1)                                                      // [0,1]
raw_i   = abundance_i
        * exp(-((depth - peakDepth_i) / layerThickness_i)²)
        * noise_i
```

`depthScale` 必须大于 0：同一半径上的所有点若噪声值相同，矿会退化成一层均匀的壳而不是矿脉。

`FVoxelFastNoise` 的 `GetPerlinFractal_3D(x, y, z, frequency, octaves)` 内部会把坐标乘频率，因此 `SampleZ` 用无量纲的深度比，不直接加体素深度。

### 3.3 合并与纯度

```
Total       = Σ raw_i                        ∈ [0, Σ abundance_i]
TotalNorm   = Total / Σ abundance_i          ∈ [0,1]（丰度和为 0 时取 0）
w_i         = raw_i / Total                  （Total = 0 时全 0）
Purity      = max(w_i)                       ∈ [0,1]
MineralColor = Σ w_i * color_i
DominantIndex = argmax(raw_i)
```

### 3.4 几何增量

```
GeometryDelta = geometryAmplitude * saturate(TotalNorm)     ∈ [0, geometryAmplitude]
```

只往“更实”的方向推——矿脉让地表外凸，不挖坑。这样区间上界是一个常数，`GetValueRangeImpl` 不需要把各元素上界相加。

### 3.5 颜色

```
Presence    = saturate((TotalNorm - colorThreshold) / fadeRange)
PurityMix   = saturate((Purity - (1 - purityForFullColor)) / purityForFullColor)
MineralWeight = Presence * PurityMix

Color = lerp(BaseColor, MineralColor, MineralWeight)
```

**水面（`Offset <= SeaLevel`）不叠矿色。** 海面不是岩石，否则整片海会被染红。

**露天矿不需要额外规则**：`peakDepth` 接近 0 的元素在 `depth ≈ 0` 处高斯尾部不为零，平地自然透出矿色；山顶 `depth < 0`，自然没有。挖开后的新网格顶点落在更深处，同一个函数直接给出矿色——不存在第二条代码路径。

### 3.6 区间契约（防出洞）

现有密度：

```cpp
Value = (Distance - SurfaceRadius) / DensityScale;
SurfaceRadius = Radius + Amplitude * ComputePlanetOffset(...) + GeometryDelta;
```

- `ComputePlanetOffset ∈ [SeaLevel, 1]`
- `GeometryDelta ∈ [0, geometryAmplitude]`

因此：

```
MinValue = (MinDistance - Radius - Amplitude * 1.0 - geometryAmplitude) / DensityScale
MaxValue = (MaxDistance - Radius - Amplitude * SeaLevel) / DensityScale
```

`geometryAmplitude` 只加在 `MinValue` 一侧。这一项漏掉就会在矿脉处出洞，且是静默的——由 `RangeCoverage` 测试兜底。

---

## 4. 接入点

### 4.1 地形上下文

`FVoxelPlanetTerrainModule.h` 的 `FVoxelPlanetTerrainContext` 增加：

```cpp
TSharedPtr<const FPlanetGeologyField> Geology;   // 可为空 = 没有矿
```

### 4.2 生成器：单一上下文入口

现状有两处手工拼上下文：

- `VoxelPlanetGenerator.cpp` 的 `GetInstance()`；
- `VoxelPlanetActor.cpp` 的 `GetSurfaceRadiusCmAtDirection()`。

后者注释已写明“必须与生成器里那份一致”。再加一个字段就是第三个拷贝点，因此抽方法：

```cpp
// UVoxelPlanetGenerator（均非 const：懒加载会改缓存）
FVoxelPlanetTerrainContext MakeTerrainContext();      // 唯一构造点
TSharedPtr<const FPlanetGeologyField> GetGeology();   // 按预设懒加载并缓存
void InvalidateGeology();                              // 清缓存
```

`GetInstance()` 与 Actor 地表查询都改用它。

缓存键是预设名字符串；`GetSurfaceRadiusCmAtDirection` 可能每帧被调用，不能每次读盘。`GetInstance()` 本身就是非 const，因此不需要 `mutable`。

### 4.3 密度场

`FVoxelPlanetNoiseTerrainInstance::GetValueImpl`：

```cpp
const double BaseSurface = Radius + Amplitude * ComputePlanetOffset(Params, UnitDir);
const double Delta = Geology ? Geology->SampleGeometryDelta(UnitDir, Radius - Distance) : 0.0;
const double Value = (Distance - BaseSurface - Delta) / DensityScale;
```

### 4.4 区间

`GetValueRangeImpl` 按 §3.6 修改；`MaxGeologyDelta` 由 `Geology->GetMaxGeometryDelta()` 提供。

### 4.5 地表查询

`GetSurfaceRadiusVoxels` 与密度场用同一个 `SampleGeometryDelta`——否则角色出生/落地会按“没有矿脉的地面”计算，陷进矿石凸起里。

### 4.6 材质

`GetMaterialImpl`：

1. 先算现有地形色（水/沙/草/岩/雪）；
2. `depth = Radius - Distance`，`Sample = Geology->Sample(UnitDir, depth)`；
3. 若 `Offset > SeaLevel` 且 `TotalNorm > colorThreshold`，按 §3.5 叠矿色；
4. 通过唯一出口打包：

```cpp
return MakeVoxelMaterial(Sample, Color);   // 文件内静态函数；本期 = RGB 顶点色
```

`MakeVoxelMaterial` 是 `VoxelPlanetNoiseTerrain.cpp` 里的文件内静态函数，不新增公开头。未来换 MultiIndex 只改这个函数体。

### 4.7 Actor

`AVoxelPlanetActor` 增加：

```cpp
UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet|Geology",
          meta = (GetOptions = "GetGeologyPresetOptions"))
FString GeologyPreset = TEXT("earth");
```

- `RebuildPlanet` 里推进生成器：`PlanetGenerator->GeologyPreset = GeologyPreset;`
- `ComputeParamsHash` 混入预设名字符串（与 `Terrain` 的资产路径同样处理）——换预设即重建。
- `GetGeologyPresetOptions()` 扫描 `Content/Geology/Planets/*.json` 给下拉框，避免手打出错。

**已知后果**：改 JSON 内容不会改变哈希；重载由 §4.8 的命令显式触发。另外，改地质参数后重建会让旧的手工挖掘编辑与新地形不再对齐——这与现在改地形参数的行为一致，不是新问题。

### 4.8 重载命令

```
StellarEra.ReloadGeology
```

遍历当前世界所有 `AVoxelPlanetActor` → `InvalidateGeology()` → `InvalidateAndRebuild()`。改完 JSON 跑一次，不需要重启编辑器。

### 4.9 打包

`Config/DefaultGame.ini`：

```ini
[/Script/UnrealEd.ProjectPackagingSettings]
+DirectoriesToAlwaysStageAsUFS=(Path="Geology")
```

路径解析统一用 `FPaths::ProjectContentDir() / TEXT("Geology/...")`，编辑器和打包后同一套代码。

---

## 5. 性能

### 5.1 两级早退

```cpp
if (!Geology || Geology->IsEmpty()) return 0;                 // 空场
if (depth < -MaxReachDepth || depth > MaxReachDepth) return 0; // 高斯尾部之外
```

`MaxReachDepth = max(|peakDepth| + 3 * layerThickness)`，解析时算好。行星内部绝大部分体积与整个高空零成本。

### 5.2 深度剪枝

```cpp
for (每个元素) {
    const double DepthFactor = exp(-((depth - peakDepth) / thickness)²);
    if (DepthFactor < 0.01) continue;      // 该元素在这个深度没有贡献
    ...1 次分形 Perlin + 1 次 exp...
}
```

在任一深度上只有 1~3 个元素的高斯还有意义，其余直接跳过。

### 5.3 成本预估

现有地形每体素 4 次 Perlin。地质在地表薄带里额外增加约 1~3 个元素 × 2 八度 ≈ 2~6 次 Perlin，即该带内约 1.5~2.5 倍。**这是估计值，实施后用 `stat unit` 对比验证**（编辑器必须在前台，否则 3 FPS 是进程节流假象）。

`GetMaterialImpl` 只在网格顶点调用（`VoxelMarchingCubeMesher.cpp:121`），不随体积增长。

---

## 6. 线程安全与确定性

- `FPlanetGeologyField` 构造后**完全不可变**，由 `TSharedPtr<const ...>` 持有；
- 所有采样方法 `const`；噪声种子与分形参数在构造期设置，运行期不再修改；
- 同一份 JSON + 同一坐标 → 完全相同的结果（测试覆盖）；
- 生成器实例在创建时快照该指针，后台线程共享同一份只读数据。

---

## 7. 文件清单

### 7.1 新增

| 文件 | 职责 |
|---|---|
| `Source/StellarEraPlanet/Public/PlanetGeologyTypes.h` | `FPlanetGeologyElement`、`FPlanetGeologySample`、`FPlanetGeologyAppearance`、`FPlanetGeologyLoadReport` |
| `Source/StellarEraPlanet/Public/PlanetGeologyField.h` | `FPlanetGeologyField`：`LoadFromString` / `LoadFromFiles` / `Sample` / `SampleGeometryDelta` / `GetMaxReachDepth` / `GetMaxGeometryDelta` / `IsEmpty` |
| `Source/StellarEraPlanet/Private/PlanetGeologyField.cpp` | JSON 解析 + 数学实现 |
| `Source/StellarEraPlanet/Private/PlanetGeologyFieldTests.cpp` | 自动化测试（§8） |
| `Content/Geology/Elements.json` | 默认 6 种元素 |
| `Content/Geology/Planets/earth.json` | 默认行星预设 |
| `Content/Geology/README.md` | schema 说明 |

### 7.2 修改

| 文件 | 改动 |
|---|---|
| `Source/StellarEraPlanet/Public/VoxelPlanetTerrainModule.h` | 上下文加 `Geology` 字段 |
| `Source/StellarEraPlanet/Public/VoxelPlanetGenerator.h` | `GeologyPreset`、`MakeTerrainContext()`、`GetGeology()`、`InvalidateGeology()` |
| `Source/StellarEraPlanet/Private/VoxelPlanetGenerator.cpp` | 上下文单一入口；按预设加载缓存 |
| `Source/StellarEraPlanet/Private/VoxelPlanetNoiseTerrain.cpp` | 密度 / 区间 / 地表查询 / 材质四处接入 |
| `Source/StellarEraPlanet/Public/VoxelPlanetActor.h` | `GeologyPreset`、`GetGeologyPresetOptions()`、`ReloadGeology()` |
| `Source/StellarEraPlanet/Private/VoxelPlanetActor.cpp` | 推参数、混哈希、地表查询走 `MakeTerrainContext()`、控制台命令 |
| `Source/StellarEraPlanet/StellarEraPlanet.Build.cs` | 加 `Json` 依赖 |
| `Config/DefaultGame.ini` | UFS staging 目录 |

### 7.3 分层

`FPlanetGeologyField` 只依赖 `Core` + `Json` + 插件的 `FastNoise`；不依赖 Actor、World。`StellarEraVehicle` 不感知本系统。

---

## 8. 测试计划

全部无头执行：

```
UnrealEditor-Cmd StellarEra.uproject -ExecCmds="Automation RunTests StellarEra" ...
```

| 测试 | 验证 |
|---|---|
| `StellarEra.Planet.Geology.Parse` | 合法 JSON 解析；未知 id 跳过；`layerThickness <= 0` 被拒；缺文件降级为空场；`LoadReport` 内容正确 |
| `StellarEra.Planet.Geology.Determinism` | 同坐标两次采样完全一致；不同 `noise.seed` 得到不同布局 |
| `StellarEra.Planet.Geology.Bounds` | `Total <= Σ abundance`；`GeometryDelta ∈ [0, geometryAmplitude]`；`Purity ∈ [0,1]` |
| `StellarEra.Planet.Geology.RangeCoverage` | **关键**：包围盒内随机采样，`GetValueRangeImpl` 必须覆盖所有 `GetValueImpl` 值 |
| `StellarEra.Planet.Geology.Color` | 无矿 → 地形底色；纯矿 → 元素色；水面不叠矿色 |

`LoadFromString` 让解析测试不依赖磁盘。

---

## 9. 验证步骤

1. UBT 构建：`Result: Succeeded`；
2. 自动化测试全绿（记录总数与失败数）；
3. `UniverseMap` 中地表可见零星矿色、矿脉处有轻微凸起；
4. 进入行星编辑模式挖开一处，新切面出现矿色；
5. 修改 `earth.json` 的 `abundance` → `StellarEra.ReloadGeology` → 地形变化；
6. 编辑器在前台，对比改前/改后的 `stat unit`。

---

## 10. 未来扩展（接口预留）

### 10.1 MultiIndex 真实材质

`GetMaterialImpl` 里的 `MakeVoxelMaterial(Sample, BaseColor)` 是唯一出口。未来版本：

- 在 `FPlanetGeologySample` 上增加 `DominantIndex` 与混合权重（已有）；
- 元素定义增加材质槽索引；
- `MakeVoxelMaterial` 改为构造 `EVoxelMaterialConfig::MultiIndex`，并建立材质集合资产。

地形与地质场的代码不需要改。

### 10.2 开采查询接口

```cpp
// 给定行星与体素坐标，返回该点的成分
FPlanetGeologySample SampleGeologyAtVoxel(const AVoxelPlanetActor* Planet, const FIntVector& VoxelPosition);
```

本期只保证 `FPlanetGeologyField::Sample` 可被外部调用；包装成 Actor 方法留到接开采时再做。

---

## 11. 与旧原型的关系

`docs/VoxelTestStudy/VoxelPlanet/Private/GeologySystem.cpp` 是前身原型，验证过“层厚 = |峰值深度| × 0.65”这一关键参数。本设计保留它的核心教训：

- **深度必须混进噪声**，否则矿是均匀的壳（`depthScale`）；
- **层厚必须显著小于峰值间距**，否则高斯尾部互相淹没；
- **未归一化的权重 + 纯度** 能区分贫矿与富矿。

但不沿用它的固定 6 种矿枚举、`LayerThickness` 写死、以及“每体素取材质”的成本假设。
