# 行星大气 — 设计文档

- **日期**：2026-10-18
- **项目**：StellarEra（Unreal Engine 5.8）
- **状态**：设计已批准，待生成实现计划
- **前置文档**：`2026-10-13-planet-character-and-spaceship-design.md`、`2026-10-18-spaceship-nbody-thrust-design.md`

---

## 1. 目标

`UniverseMap` 里的**每一颗**体素行星都要有大气：

- **从太空看**：每颗行星都有一圈可见的大气辉光与边缘渐变，不是只有玩家所在的那颗。
- **站在行星上**：头顶有正确的天空（地平线渐变、亮的朝向与太阳一致），脚下地形有空中透视（近处清晰、远处发白）。

## 2. 范围

### 本次做

- 每颗行星一个**大气壳**（几何球 + 自写单次散射像素着色器），从太空看**同时**可见
- 引擎原生 `SkyAtmosphere` **只保留一个**，逐帧重定位到离相机最近的行星，负责球内的天空、太阳盘与 SkyLight 环境光
- 壳的参数由行星半径推导，全部暴露为可调
- 材质由 C++ 生成（HLSL 作为仓库内的字符串），不手工连节点

### 本次明确不做

- 多次散射、云、臭氧吸收、气溶胶的分层高度分布
- 行星遮挡太阳造成的大气日食阴影
- 大气与体积雾、半透明材质的相互作用
- 修改引擎以支持"一个 world 多套天空大气"
- 每颗恒星一套独立光照（`StellarEraCore/Public/CosmosTypes.h:109` 那条设计意图仍未实现，本次不补）

---

## 3. 关键前置发现

这五条决定了整个架构，先记录依据。

### 3.1 引擎一个 world 只渲染一套天空大气

`FScene` 只有**单个** `FSkyAtmosphereRenderSceneInfo* SkyAtmosphere`
（`Renderer/Private/ScenePrivate.h:2006`），`FAtmosphereUniformShaderParameters` 也只有**单个**
（`Renderer/Private/SkyAtmosphereRendering.h:168-170`）。

`AddSkyAtmosphere` 每次注册都直接覆盖这个指针：

```cpp
// Renderer/Private/SkyAtmosphereRendering.cpp:681-703
Scene->SkyAtmosphereStack.Push(SkyAtmosphereSceneProxy);
SkyAtmosphereSceneProxy->RenderSceneInfo = new FSkyAtmosphereRenderSceneInfo(*SkyAtmosphereSceneProxy);
// Use the most recently enabled SkyAtmosphere
Scene->SkyAtmosphere = SkyAtmosphereSceneProxy->RenderSceneInfo;
```

渲染侧有 14 处调用点全部写成 `Scene->GetSkyAtmosphereSceneInfo()->GetAtmosphereUniformBuffer()`
（`SkyAtmosphereRendering.cpp` 多处、`PathTracing.cpp:2228/4406/4407/4422/4965`），
**没有任何一处遍历数组**。

`FScene::SkyAtmosphereStack` 这个 `TArray` 不是多大气支持：它只有两个消费点——
`ApplyWorldOffset`（`RendererScene.cpp:4661`）和"取最后一个"（`SkyAtmosphereRendering.cpp:705-733`）。

**结论**：这是架构性的（逐视图的全屏效果 + 单个 uniform buffer），不是"引擎忘了循环"。

### 3.2 材质里拿不到大气的 LUT 与 uniform

`SkyAtmosphereCommon.ush` 里唯一"看着能直接调用"的是
`GetAtmosphereTransmittance(PlanetCenterToWorldPos, WorldDir, BottomRadius, TopRadius, Texture2D<float4> TransmittanceLutTexture, SamplerState)`（`:246`）——
它的几何与散射参数全是显式入参，但**多要一个透射率 LUT 纹理**。
那个 LUT 是 `FSkyAtmosphereRenderSceneInfo` 私有的渲染目标，材质绑不上。

更底层的 `SampleAtmosphereMediumRGB`（`:321-353`）读的是全局 `Atmosphere` uniform，
材质着色器里根本没有这个 uniform。

（`GetLightDiskLuminance`（`:269`）与 `GetAlbedo`（`:294`）确实是纯参数化的，
但它们服务的是太阳盘；太阳盘由 §5.4 的引擎大气负责，壳里用不到。）

**结论**：壳的 HLSL 必须**自带散射数学**，不能复用引擎的散射函数。

### 3.3 `UMaterialExpressionCustom::IncludeFilePaths` 只做依赖跟踪

`GetIncludeFilePaths` 把路径收集进 `FMaterialCachedExpressionData::EditorOnlyData::ExpressionIncludeFilePaths`
（`MaterialExpressions.cpp:12929`、`MaterialCachedData.cpp:569`）。全引擎消费点只有两处：

- DDC 哈希：`FMaterial::GetExpressionIncludesHash`（`MaterialShared.cpp:4725-4738`）
- cook 依赖：`MaterialShader.cpp:561-567`

**没有一处把它变成生成的 `#include`。**

**结论**：HLSL 直接作为**自包含字符串**写进 Custom 节点，不依赖任何 shader 目录映射。

### 3.4 壳从球内看天然不可见

球面网格法线朝外，相机在球内时看到的是背面，背面剔除后壳不渲染。
这正是我们要的：**球内交给引擎原生大气**，两者不打架，也不需要手工切可见性。

### 3.5 行星半径有现成 API

`AVoxelPlanetActor::GetSurfaceRadiusCm()` 给基础球半径（cm，含 actor 缩放，不含地形起伏）。

---

## 4. 架构

两个独立的东西，各自解决一半问题：

```
从太空看
  ┌──────────────────────────────────────────────────┐
  │ 壳（每颗行星一个，全部同时可见）                    │
  │   UPlanetAtmosphereComponent                     │
  │     球面网格 + UMaterialInstanceDynamic          │
  │       └─ /Game/Materials/M_PlanetAtmosphere      │
  │            └─ Custom 节点：自包含单次散射 ray march │
  └──────────────────────────────────────────────────┘

站在行星上
  ┌──────────────────────────────────────────────────┐
  │ 引擎原生 SkyAtmosphere（全场景一个）               │
  │   UCosmosSimulationSubsystem 逐帧搬到             │
  │   离相机最近的行星，参数按该行星半径缩放            │
  └──────────────────────────────────────────────────┘
```

### 依赖方向

- 壳组件只依赖 `AVoxelPlanetActor` 的半径与变换，不依赖子系统
- 重定位逻辑放在 `UCosmosSimulationSubsystem`（它已经每帧 Tick、已经知道所有天体）
- 材质生成器是 `#if WITH_EDITOR` 的一次性工具，运行时不存在

---

## 5. 详细设计

### 5.1 UPlanetAtmosphereComponent

放在 `Source/StellarEraPlanet/`（遵守"自定义代码不进第三方插件目录"的既有约定）。

- `UStaticMeshComponent` 子类，构造时设：
  - `StaticMesh = /Engine/BasicShapes/Sphere`（引擎自带，无外部依赖；该网格半径 50 cm）
  - 相对缩放 = `壳半径cm / 50`
  - 碰撞全关、`SetCastShadow(false)`、`bReceivesDecals = false`
  - `TranslucencySortPriority` 设为负值，保证排在其他半透明之前
- 由 `AVoxelPlanetActor` 在构造时创建并挂到根组件上——**每颗行星自动获得一个**，不需要手工摆放
- 材质用 `UMaterialInstanceDynamic` 实例化，每颗行星一个
- 每帧更新两个参数：`PlanetCenter`（取自己的世界位置）与 `SunDirection`
- 组件只**暴露 setter**，不反向查询子系统——§4 说的"不依赖子系统"指的是这个方向

**半径**：`R_shell = GetSurfaceRadiusCm() × (1 + AtmosphereHeightRatio)`，
`AtmosphereHeightRatio` 默认 `0.05`，暴露为 `UPROPERTY(EditAnywhere)`。

> **为什么不用物理缩放**：地球大气约是半径的 1.6%，缩到 0.75 km 半径的行星上只有约 12 m，
> 从太空看是一条几乎看不见的线。游戏里要的是"看得见"，所以厚度比是**观感参数**，不是物理量。
> 但它必须与 §5.4 引擎大气用的是同一个值，否则玩家穿过壳边界时会看到跳变。

### 5.2 材质参数

全部做成 `ScalarParameter` / `VectorParameter` 节点，由 C++ 用名字设置：

| 参数名 | 类型 | 含义 | 初值来源 |
|---|---|---|---|
| `PlanetRadius` | Scalar | 行星基础半径（cm） | `GetSurfaceRadiusCm()` |
| `ShellRadius` | Scalar | 壳半径（cm） | §5.1 的公式 |
| `RayleighScaleHeight` | Scalar | 瑞利标高（cm） | `ShellRadius × 0.015` 起调 |
| `MieScaleHeight` | Scalar | 米氏标高（cm） | `ShellRadius × 0.003` 起调 |
| `RayleighBeta` | Vector | 瑞利散射系数（1/cm，RGB） | 见 §6 |
| `MieBeta` | Vector | 米氏散射+吸收系数（1/cm，RGB） | 见 §6 |
| `SunDirection` | Vector | 从行星指向太阳的单位向量 | 每帧更新 |
| `SunIntensity` | Vector | 太阳辐照度的**线性 RGB 倍率** | 初值取到与地形被同一盏 DirectionalLight 照出来的亮度大致相称，PIE 里调 |

Custom 节点的输入分两类：

- **几何输入**：`CameraPos`、`PlanetCenter`（都作为逐帧向量参数传入，见下面的空间说明），
  以及 `WorldPos`（`WorldPosition` 节点）
- **参数输入**：上表八个

输出两个：`Color`（float3）、`Opacity`（float1）。

> **空间一致性（实现第一步就要钉死）**：射线求交要求 `CameraPos`、`PlanetCenter`、`WorldPos`
> 三者**同空间**。做法是把前两个当作逐帧向量参数传（分别来自相机与组件自身变换，绝对世界空间），
> `WorldPos` 用 `WorldPosition` 节点，并把它的 `WorldPositionShaderOffset` 设成与之匹配的模式。
>
> 为什么不直接相信节点：UE5 的材质着色器内部用 translated world space，各节点的空间
> 不完全一致，猜错会让整条射线偏掉。**实现的第一步就是把这三个值各打印一次、确认量级与符号一致**，
> 再往下写求交。
>
> 精度：绝对世界空间下相机与行星中心在 5e6 cm 量级，float32 的分辨率约 0.5 cm，
> 相对 75000 cm 的半径可忽略。

### 5.3 壳的 HLSL：单次散射

**射线**：`ro = CameraPos`，`rd = normalize(WorldPos - CameraPos)`。
只做球面解析求交，不做三角形求交：

- 内球半径 `PlanetRadius`、外球半径 `ShellRadius`，球心 `PlanetCenter`
- 求射线与两个球的交点距离
- `t_enter` = 进入外球的距离（相机在壳外时为正）
- `t_exit` = `min(出外球, 撞内球)`

**积分**（固定步长，`N` 步，`N` 是编译期常量）：

```
T = 1; acc = 0
ds = (t_exit - t_enter) / N
for i in 0 .. N-1:
    t = t_enter + (i + 0.5) * ds
    P = ro + rd * t
    h = max(|P - PlanetCenter| - PlanetRadius, 0)
    dR = exp(-h / RayleighScaleHeight)
    dM = exp(-h / MieScaleHeight)
    T *= exp(-(RayleighBeta * dR + MieBeta * dM) * ds)
    acc += (RayleighBeta * dR * phaseR + MieBeta * dM * phaseM) * T * ds
Color   = acc * SunIntensity
Opacity = 1 - T
```

**相位函数**：`phaseR = 3/(16π) · (1 + cos²θ)`；米氏用 Henyey-Greenstein，`g = 0.76`。
`θ` 是 `rd` 与 `SunDirection` 的夹角。

**为什么可以这样简化**：单次散射 + 固定步长在视觉上已经给出正确的地平线渐变、边缘辉光与
空中透视；多次散射只是把结果整体提亮，用 `SunIntensity` 或 `RayleighBeta` 的缩放就能补。
引擎自己在低画质档也是这么做的。

**边界**：

- 行星背面的壳像素被不透明地形挡掉（深度测试开），不会"透过行星发光"
- 太阳盘不在这里画（由 §5.4 的引擎大气负责）；米氏前向峰给的是太阳周围的辉光，不是圆盘

### 5.4 引擎大气的重定位

`UCosmosSimulationSubsystem::Tick` 里加一步：

1. 取相机位置
2. 在所有 `AVoxelPlanetActor` 里找**表面距离最近**的一颗
3. 把场景里唯一的 `ASkyAtmosphere` 设成：
   - `SetActorLocation(行星中心)`，`TransformMode = PlanetCenterAtComponentTransform`
   - `BottomRadius = 行星半径km`
   - `AtmosphereHeight = 行星半径km × AtmosphereHeightRatio`（与壳同一个厚度比）
   - 散射系数按 §6 的 `1/s` 关系缩放
4. 找不到任何行星时（理论上不会发生，`UniverseMap` 里始终有行星）保持上一次的设置，不写 0
   ——`AtmosphereHeight` 写 0 会让着色器里的 `exp(-h / H)` 除零

**不切可见性**：反正只有一套会渲染，与其"关一个开一个"（会触发渲染状态重建），
不如始终只有这一个、只改它的位置和参数。只在"最近行星"真的改变时才写，避免每帧抖动。

**深空不需要特判**：相机远离所有行星时，它已经在所有大气之外，`BottomRadius` 与
`AtmosphereHeight` 相对相机距离都是小量，贡献趋近于零，不需要额外的"关掉"分支。

### 5.5 材质生成器

`Source/StellarEraPlanet/Private/AtmosphereMaterialBuilder.cpp`，整文件包在 `#if WITH_EDITOR` 里。

```cpp
static const TCHAR* AtmosphereHLSL = TEXT(R"(
    // 自包含单次散射。本文档不复制整段 HLSL，
    // 算法以上面 §5.3 的伪代码为准
)");

void BuildPlanetAtmosphereMaterial();
```

做的事：

1. `NewObject<UMaterial>` 建空材质
2. `NewObject<UMaterialExpressionCustom>`，填 `Code`、`Inputs`、`AdditionalOutputs`、`OutputType`
3. 为 §5.2 每个参数建 `UMaterialExpressionScalarParameter` / `VectorParameter`，接到对应输入
4. 建 `CameraPositionWS` / `ObjectPositionWS` / `WorldPosition` 节点接到几何输入
5. `Custom.Color → EmissiveColor`、`Custom.Opacity → Opacity`
6. 材质本体设：`BlendMode = BLEND_Translucent`、`ShadingModel = MSM_Unlit`、
   `TwoSided = false`、`bCastDynamicShadow = false`
7. 存到 `/Game/Materials/M_PlanetAtmosphere`

**触发方式**：`FAutoConsoleCommandWithWorldAndArgs` 注册控制台命令
`StellarEra.BuildAtmosphereMaterial`，在编辑器里跑一次即可。

**模块依赖**：`StellarEraPlanet.Build.cs` 需要加 `UnrealEd`，包在 `if (Target.bBuildEditor)` 里。
这是本仓库第一次引入 `UnrealEd` 依赖，只影响编辑器构建。

**失败处理**：壳组件在 `BeginPlay` 里用软路径加载材质；加载不到就
`UE_LOG(LogTemp, Warning, ...)` 并把自己设为不可见，不崩。

---

## 6. 参数从哪来

**几何缩放关系**：引擎大气组件的默认值是地球的，长度单位 km、系数单位 1/km。
把长度乘以 `s`、系数乘以 `1/s`，辐射传输方程**完全不变**（几何缩放模型的不变性）。

```
s = 行星半径km / 6360
所有长度 × s，所有 1/长度 的系数 × 1/s
```

代入 `s ≈ 1.18e-4` 时 `1/s ≈ 8480`：`RayleighScatteringScale` 从 `0.0331` 变成约 `281`，
`MieScatteringScale` 从 `0.003996` 变成约 `34`。这两个量的量级（`10²` 与 `10¹`）
是**推导出来的**，不是实测的。

**壳的参数**不用这套：壳是我们自己写的，直接按 §5.2 用 `ShellRadius` 的比例给初值，
在 PIE 里看着调。两边的**厚度比必须一致**（见 §5.1 的提示）。

---

## 7. 风险

| # | 风险 | 影响 | 应对 |
|---|---|---|---|
| 1 | **引擎大气在 0.75 km 半径下不一定正常** | 球内没有天空，方案 A 落空 | 未实测。LUT 的参量化是尺度不变的，理论上应成立；若实测不行，退路是把壳改成双面（球内也画），代价是失去 SkyLight 环境光采集与太阳盘 |
| 2 | 壳的像素成本 | 太空视角下多颗行星同时占屏 | 步数 `N` 是编译期常量，先取小值；用 `stat unit` 对比开关壳前后的帧时间 |
| 3 | 引擎大气重定位时抖动 | 换行星的一瞬间画面跳变 | 只在"最近行星"真的改变时才写参数；必要时做平滑过渡 |
| 4 | 壳与地形的深度/排序 | 壳盖住地形或地形盖住壳 | 深度测试开、深度写入关、`TranslucencySortPriority` 负值；在 PIE 里对着地平线确认 |
| 5 | 材质生成器只在编辑器可用 | 打包后材质资产缺失 | 材质是资产，随包走；生成器只是"怎么造出这个资产"的可复现记录，不是运行时依赖 |

---

## 8. 验收标准

1. 在太空视角（远到能同时看见三颗行星）能看见**每一颗**行星都有大气辉光，且辉光不出现在行星被自己挡住的背面。
2. 飞到某颗行星表面：头顶天空有地平线渐变，亮的一侧朝向太阳；脚下地形近处清晰、远处发白（空中透视）。
3. 从行星 A 飞到行星 B：A 的壳在远处仍可见，引擎大气跟着搬到 B，B 球内的天空正确。
4. 时间推进导致太阳方向变化时，壳上的明暗分界跟着转。
5. 无崩溃、无 ensure、无 shader 编译错误、无 `LogMaterial` 的 include 路径告警。
6. `stat unit` 对比开关壳组件前后的帧时间，记录实测差值（不预设阈值）。

---

## 9. 测试方式

- **构建**：既有的 UBT 直调脚本 `%TEMP%\ue_mcp\ue_build.ps1`（绕过 `Build.bat` 的锁），构建前需关闭编辑器。
- **材质生成**：编辑器里跑一次控制台命令 `StellarEra.BuildAtmosphereMaterial`。
- **运行验证**：打开编辑器 → 加载 `UniverseMap` → PIE，人工过一遍第 8 节。
- **回归**：不得破坏既有的 `AVoxelPlanetActor` 行为（球体生成、材质、LOD Invoker）与 n-body 模拟。
- **自动化**：本项目尚无渲染相关测试框架，本次不引入。
