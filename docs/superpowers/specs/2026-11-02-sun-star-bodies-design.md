# 恒星天体：天空可见的太阳 + 参与 N 体 + 大气跟随 — 设计文档

- **日期**：2026-11-02
- **项目**：StellarEra（Unreal Engine 5.8）
- **状态**：设计已批准，待生成实现计划
- **前置文档**：`2026-10-19-units-design.md`、`2026-11-01-atmosphere-inside-sky-design.md`

---

## 1. 背景

用户往 `UniverseMap` 里加了一个"太阳"（`StaticMeshActor` + `/Engine/BasicShapes/Sphere` + `MI_Sun_*`），反馈"感觉没有效果"。

无头打开关卡读到的实际状态（2026-11-02）：

- **关卡里没有太阳**。`UniverseMap` 共 12 个 Actor：`WorldDataLayers`、`WorldPartitionMiniMap`、
  `DirectionalLight`、`PostProcessVolume`、3 颗 `BP_VoxelPlanet`、`BP_SpaceshipPawn`、`PlayerStart`、
  3 个 `BP_StarMapCameraPawn`。磁盘上也没有它的自动保存。用户加的太阳没有落盘。
- 唯一的太阳相关资产在 `TestMap`：`StaticMeshActor`「Sphere」+ `MI_Sun_M2V`，位置 (3261, −7644, 9990)，`scale = 1`。

即使它落盘了，现有代码里它也产生不了任何效果，四条机制事实：

1. **不发光照**。`M_Sun` 是 `MSM_UNLIT` + Emissive（`BuildSunMaterial` 直接接 Emissive），
   UE 里自发光不产生照明，场景亮度只来自那盏 `DirectionalLight`。
2. **大气不认它**。`UPlanetAtmosphereComponent::ResolveSunDirection()`
   (`Source/StellarEraPlanet/Src/Atmosphere/PlanetAtmosphereComponent.cpp:232`) 只取关卡里
   **第一盏 `ADirectionalLight`** 的朝向取反；找不到就返回零并只警告一次。
3. **不是天体**。N 体子系统与星图都靠 `FindComponentByClass<UCosmosOrbitComponent>()` 认天体，
   裸 `StaticMeshActor` 没有该组件，不参与引力、不会动、星图里也不出现。
4. **体量不对**。`BasicShapes/Sphere` 半径 50 cm，`scale=1` 就是半径 50 cm 的球，
   `MI_Sun_M2V` 又是三档预设里最暗的红矮星。

## 2. 目标

在 `UniverseMap` 里做出一个**三恒星 + 一行星**的层级系统：

- 三颗恒星初始共线，中间一颗带行星；外侧两颗以大小相等、方向相反的初速绕中间恒星做圆轨道。
- 行星绕中间恒星做圆轨道（半径 100 AU）。
- 站在行星表面抬头**看得见太阳盘**，另外两颗在天空中各是一个亮球。
- 大气壳的明暗分界、地形被照亮的一面**都跟随离本行星最近的那颗恒星**。

## 3. 非目标

- **多恒星光照**不做：不会出现三盏同时投影的方向光。地形照明仍由单盏 `DirectionalLight` 承担，
  只是它的朝向被同步到"最近恒星"方向。
- 恒星自转、耀斑、日冕粒子、恒星风不做。
- 恒星与行星的碰撞不做（`StaticMeshActor` 恒星没有接触半径）。
- 星图里区分恒星 / 行星的界面改动不做（恒星会作为普通天体出现）。
- `TestMap` 里那颗试验用的太阳球不动，留着当对照。

## 4. 关键约束：模块依赖不能成环

`StellarEraCosmos` 依赖 `StellarEraPlanet`（`Source/StellarEraCosmos/StellarEraCosmos.Build.cs:48`），
反向不允许。因此 **`StellarEraPlanet` 里的大气组件不能引用 `UCosmosOrbitComponent`**。

恒星的身份因此拆成两个互不认识的组件：

| 组件 | 所在模块 | 回答的问题 |
|---|---|---|
| `UCosmosOrbitComponent` | Cosmos | 质量多少、初速多少（物理身份） |
| `UStarBodyComponent`（新增） | Planet | 这是一颗恒星、光是什么颜色（视觉 / 大气身份） |

一个恒星 Actor 同时挂两个。Cosmos 不需要知道"恒星"这个概念，Planet 也不需要知道 N 体。

## 5. 设计

### 5.1 `UStarBodyComponent`（新增）

位置：`Source/StellarEraPlanet/Src/Atmosphere/UStarBodyComponent.h/.cpp`

- 继承 `UActorComponent`，**不 Tick**，只存参数并充当标记（与 `UCosmosOrbitComponent` 同样的取舍）。
- 字段：
  - `FLinearColor LightColor = White`（预留：将来若做单光源染色，用它驱动 `DirectionalLight`）
  - `bool bDrivesDirectionalLight = true`（这颗恒星是否接管关卡主光；多颗恒星时只有最近那颗生效）
- 恒星的位置直接用 `GetOwner()->GetActorLocation()`，组件自己不存位置。

### 5.2 太阳方向解析（核心改动）

`UPlanetAtmosphereComponent::ResolveSunDirection()` 改成两级：

1. **优先：最近的 `UStarBodyComponent`。** 遍历世界取离本行星最近的一颗，
   方向 = `normalize(StarLocation − PlanetLocation)`。
   用"最近"而不是"唯一"，是因为三颗恒星下每颗行星应当被自己那颗恒星照亮。
2. **回退：第一盏 `ADirectionalLight`**（现有行为原样保留）。关卡里没有恒星时行为完全不变，
   老的关卡与现有测试不受影响。

缓存策略沿用现在的写法：缓存指针 + `IsValid` 判断，失效就重新遍历。
恒星会动，所以方向本来就每帧在变；刷新判据由"方向变化超过阈值才写材质参数"控制，
避免每帧无谓的 `SetVectorParameterValue`。

### 5.3 主光同步

同一组件里，解析出太阳方向后把缓存的那盏 `ADirectionalLight` 的朝向设为 `−SunDirection`
（`DirectionalLight` 的 forward 是光的传播方向，与现有注释一致），
仅当半径最近的恒星 `bDrivesDirectionalLight == true` 时执行。

这条是多恒星场景下"地形明暗面与天空明暗分界一致"的必要条件：天空按最近恒星算，
地形若还按另一盏灯的朝向算，两者会打架。

### 5.4 `BP_Star` 预制体

`Content/Code/Planet/BP_Star.uasset` 已存在但当前没有可用组件。补成恒星预制体：

- `StaticMeshComponent`：`/Engine/BasicShapes/Sphere`，`relative_scale3d = 30000`
  → 半径 50 × 30000 = 1.5e6 cm
- 材质：每个实例用不同预设（`MI_Sun_G2V` / `MI_Sun_B3V` / `MI_Sun_M2V`）
- `UCosmosOrbitComponent`：质量按第 5.5 节填
- `UStarBodyComponent`

### 5.5 关卡数据（`UniverseMap`）

世界标定 `CentimetersPerAu = 1.0e6`（`CosmosSimulationSubsystem.h:180`），
`TimeScale = 0.00333`（同文件 `:185`），即 **1 模拟年 ≈ 300 秒真实时间**。
圆轨道周期 `P[年] = sqrt(r[AU]³ / M[M☉])`，速度 `v[AU/年] = 2π·sqrt(M/r)`，
`1 AU/年 = 4.74047 km/s`。

| 天体 | 位置 (cm) | 半径 (cm) | `MassEarth` (M⊕) | 初速 (km/s) | 圆轨道周期 |
|---|---|---|---|---|---|
| 恒星 A（中） | (0, 0, 0) | 1.5e6 | 3.329460837e10（= 1e5 M☉） | (0, 0, 0) | — |
| 恒星 B | (−2e8, 0, 0) | 1.5e6 | 3.329460837e10 | (0, +666, 0) | 8.94 模拟年 ≈ 44.7 分钟 |
| 恒星 C | (+2e8, 0, 0) | 1.5e6 | 3.329460837e10 | (0, −666, 0) | 同上，反向 |
| 行星 | (0, 1e8, 0) | 1e6 | 1 | (−942, 0, 0) | 3.16 模拟年 ≈ 15.8 分钟 |

- 三颗恒星共线于 X 轴。B / C 分居 ±X，速度分别沿 ∓Y（两个速度矢量方向相反），
  但两者绕 A 的**旋向相同**，所以相位恒相差 180°，**永不交汇**（与现有关卡两颗行星的做法一致）。
- 行星放在 +Y 方向，避免与 X 轴上的恒星重叠。
- 天空张角（`2·atan(R/d)`）：
  - 主太阳 A：`2·atan(1.5e6 / 1e8)` ≈ **1.72°**（真太阳 0.53°）
  - B / C：距行星 `sqrt((2e8)² + (1e8)²)` ≈ 2.236e8 cm ≈ 223.6 AU，各约 **0.77°**
- 质量量级是游戏尺度（现有关卡的"行星"已有 2500 M☉），决定动力学的是质量**比**，
  不是绝对值；视觉与周期对上即可。

### 5.6 关卡内容改动

- 保留一颗半径 1e6 的行星（现 `BP_VoxelPlanet` 或 `BP_VoxelPlanet2` 之一），
  移到 (0, 1e8, 0) 并给圆轨道初速。
- 删除另外两颗 `BP_VoxelPlanet`（含它们的大气组件与外挂星图相机）。
- 放置 3 个 `BP_Star` 实例。
- 对应体素存档目录 `Content/Saves/PlanetVoxel/**` 的清理单独确认，不在本次代码改动里做。

### 5.7 顺带修复：材质构建器的路径漂移

`Source/StellarEraPlanet/Src/Star/SunMaterialBuilder.cpp:52-54` 仍然指向
`/Game/Code/Star/Materials`，而资产已在提交 `eefaaac` 迁到 `/Game/Code/Res/Star/Materials`。
现在跑 `StellarEra.BuildSunMaterial` 会在旧路径**另建一套**材质，与关卡里引用的不是同一份。
本次一并把常量改成 `Res/Star` 路径。

## 6. 数据流

```
关卡摆放                       运行时每帧
─────────                     ───────────────────────────────────────
BP_Star(A/B/C)                UCosmosSimulationSubsystem.Tick
 ├ UCosmosOrbitComponent  ──→   推进 N 体、把位置写回 Actor
 └ UStarBodyComponent           │
行星 BP_VoxelPlanet               ▼
 └ UPlanetAtmosphereComponent   ResolveSunDirection()
     ├ 找最近的 UStarBodyComponent  → 方向（相对行星）
     ├ 写材质参数 SunDirection
     └ 转 DirectionalLight 朝向 = −方向
```

## 7. 测试与验证

1. **自动化测试（Planet 模块，新增）**
   - 关卡式测试世界放两颗 `UStarBodyComponent`，行星靠近其中一颗 →
     `ResolveSunDirection` 指向最近那颗。
   - 世界里只有 `ADirectionalLight` → 回退路径与改动前一致。
   - 命名：`StellarEra.Planet.Atmosphere.SunDirection.*`
2. **无头长时间跑**：模拟 ≥ 10 模拟年，检查日志"能量漂移"行与天体是否逃逸。
   这是三恒星的真正风险点。
3. **画面验证**：PIE 里站在行星表面抬头，确认太阳盘大小、另两颗恒星、明暗分界跟随。

## 8. 风险与退路

- **四体稳定性**：三颗 1e5 M☉ 恒星 + 一颗行星，能否长期稳定必须实测。
  `CLAUDE.md` 已记录现构型在 t≈0.84 模拟年就爆掉。若漂移超标，退路是
  降低质量或拉大 B / C 间距 —— **只改数据，不改代码**。
- **主光同步的观感**：`DirectionalLight` 朝向每帧跟随太阳，阴影生成会持续变化。
  若出现可见的闪烁，退路是改成按角度阈值（例如 0.5°）阶梯式更新。
- **恒星尺寸与体素行星的比例**：恒星半径 1.5e6、行星 1e6，只是 1.5 倍。
  这是游戏尺度取舍，不是物理比例；若观感不佳只调 `scale` 与轨道半径。
