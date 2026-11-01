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

位置：`Source/StellarEraPlanet/Src/Atmosphere/StarBodyComponent.h/.cpp`

- 继承 `UActorComponent`，**不 Tick**，只存参数并充当标记（与 `UCosmosOrbitComponent` 同样的取舍）。
- 字段只有一个：`bool bDrivesDirectionalLight = true`（这颗恒星是否接管关卡主光；多颗恒星时只有最近那颗生效）。
  恒星的颜色由各自的材质实例（`MI_Sun_*`）承载，不在这里冗余一份 ——
  多恒星光照本就列为非目标，留一个没人消费的颜色字段只会误导后来者。
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

### 5.4 `AStarBodyActor`（新增 C++ 类，放在 Cosmos）

恒星必须同时挂两个模块的组件（Cosmos 的 `UCosmosOrbitComponent` + Planet 的 `UStarBodyComponent`）。
Planet 不能依赖 Cosmos，所以**只有 Cosmos 能同时引用两者** —— 恒星 Actor 因此放在
`Source/StellarEraCosmos/Src/StarBodyActor.h/.cpp`。

不采用"在 `Content/Code/Planet/BP_Star` 蓝图里拼组件"的做法：跨模块组件的拼装在脚本里脆弱、
无法被自动化测试覆盖，而且骨架组件按 `CLAUDE.md` 的「Actor 组件归属口径」本就该写在 C++ 里。

- 根组件 `UStaticMeshComponent`：`/Engine/BasicShapes/Sphere`，`relative_scale3d = 10000`
  → 半径 50 × 10000 = 5e5 cm（5 公里）；无碰撞、不投影、`Movable`（N 体每帧写位置）
- 默认材质 `/Game/Code/Res/Star/Materials/MI_Sun_G2V`（实例在关卡里各自覆盖）
- `UCosmosOrbitComponent`、`UStarBodyComponent`
- 不 Tick

### 5.5 关卡数据（`UniverseMap`）

世界标定 `CentimetersPerAu = 1.0e6`（`CosmosSimulationSubsystem.h:180`），
`TimeScale = 0.00333`（同文件 `:185`），即 **1 模拟年 ≈ 300 秒真实时间**。
引力常数 `G = 4π²`（AU³ / (M☉·年²)），所以 `v[AU/年] = sqrt(G·M_eff/r)`、`P[年] = 2π·r/v`，
`1 AU/年 = 4.74047 km/s`。

**初速必须按多体合力算，不能套二体公式。** 三颗等质量恒星共线时，外侧一颗除中间那颗外
还受另一颗外侧恒星的拉力；后者的距离是 2d，贡献 `1/4` 的加速度：

- 外侧恒星：`a = G(M_A/d² + M_C/(2d)² + M_P·cosθ/r_PB²)`，d = 200 AU
  → `a = 123.55 AU/年²`，`v = sqrt(a·d) = **157.19 AU/年 = 745.1 km/s**`，`P = 8.00 模拟年 ≈ 40 分钟`
  （套二体公式得 140.5 AU/年 = 666 km/s，偏低 11%，轨道会明显偏心）
- 行星：`a = G(M_A/100² + 2·M_C·(100/223.6)/223.6²) = 465.4 AU/年²`
  → 相对速度 `215.96 AU/年`；再按**零总动量**分配（A 被行星拉动，
  `G·250/100² = 0.987`）→ A `−0.54`、行星 `+215.42 AU/年 = 1021.2 km/s`，
  `P = 2.91 模拟年 ≈ 14.6 分钟`

| 天体 | 位置 (cm) | 半径 (cm) | `MassEarth` (M⊕) | 初速 (km/s) | 圆轨道周期 |
|---|---|---|---|---|---|
| 恒星 A（中） | (0, 0, 0) | 5e5 | 3.329460837e10（= 1e5 M☉） | (−2.55, 0, 0) | — |
| 恒星 B | (−2e8, 0, 0) | 5e5 | 3.329460837e10 | (0, +745.1, 0) | 8.00 模拟年 ≈ 40 分钟 |
| 恒星 C | (+2e8, 0, 0) | 5e5 | 3.329460837e10 | (0, −745.1, 0) | 同上，反向 |
| 行星 | (0, 1e8, 0) | 2e6 | 8.3236520936e7（= 250 M☉） | (+1021.2, 0, 0) | 2.91 模拟年 ≈ 14.6 分钟 |

- 三颗恒星共线于 X 轴。B / C 分居 ±X，速度分别沿 ∓Y（两个速度矢量方向相反），
  但两者绕 A 的**旋向相同**，所以相位恒相差 180°，**永不交汇**（与现有关卡两颗行星的做法一致）。
  行星 +X 方向的速度与 B / C 同旋向（三者的 `r × v` 都是 −Z）。
- **行星质量必须保持"游戏大质量"（250 M☉），不能改成真实的 1 M⊕。**
  1 M⊕ 在 100 AU 处的 Hill 半径只有 `100·(1/3e5)^(1/3) ≈ 0.0215 AU = 2.15e4 cm`，
  比行星半径 2e6 cm 还小 —— 飞船在**地表**就会被恒星剥走。
  250 M☉ 时 `r_H ≈ 9.4 AU = 9.4e6 cm`，飞船离行星中心 2.39e6 cm（比值 0.25），
  低于 0.36 这条"能活"的经验阈值。现有关卡本来就用的 2500 M☉，本次降到 250 是为了
  把行星对恒星的扰动压小 10 倍。
- 行星放在 +Y 方向，避免与 X 轴上的恒星重叠。
- 天空张角（`2·atan(R/d)`）：
  - 主太阳 A：`2·atan(5e5 / 1e8)` ≈ **0.57°**（真太阳 0.53°，基本一致）
  - B / C：距行星 `sqrt((2e8)² + (1e8)²)` ≈ 2.236e8 cm ≈ 223.6 AU，各约 **0.26°**
- 质量量级是游戏尺度（现有关卡的"行星"已有 2500 M☉），决定动力学的是质量**比**，
  不是绝对值；视觉与周期对上即可。

### 5.6 关卡内容改动

- 保留 `BP_VoxelPlanet3`（唯一在原点、表面半径 2e6 cm、飞船与 `PlayerStart` 就摆在它上方的那颗），
  移到 (0, 1e8, 0) 并给圆轨道初速；质量从 1e4 M☉ 改成 250 M☉。
- 删除 `BP_VoxelPlanet` 与 `BP_VoxelPlanet2`（含它们的大气壳与对应的星图相机）。
- 放置 3 个 `AStarBodyActor` 实例（材质分别用 `MI_Sun_G2V` / `MI_Sun_B3V` / `MI_Sun_M2V`）。
- 飞船与 `PlayerStart` 随行星平移同样的位移，飞船初速与行星相同（否则一出生就被甩下）。
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
AStarBodyActor(A/B/C)          UCosmosSimulationSubsystem.Tick
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
   - 命名：`StellarEra.Planet.Atmosphere.StarBody.FindNearest`、
     `StellarEra.Planet.Atmosphere.SunDirection.Compute`、
     `StellarEra.Cosmos.StarBodyActor.Skeleton`
2. **无头长时间跑**：`StellarEra.Cosmos.Simulator.FourBodyStability` 在求解器里跑 10 模拟年，
   断言能量漂移 < 1e-4 且没有天体超出 500 AU。**这是三恒星的真正风险点**，
   参数一改就要重跑。
3. **画面验证**：PIE 里站在行星表面抬头，确认太阳盘大小、另两颗恒星、明暗分界跟随。

## 8. 风险与退路

- **四体稳定性**：三颗 1e5 M☉ 恒星 + 一颗行星，能否长期稳定必须实测。
  `CLAUDE.md` 已记录现构型在 t≈0.84 模拟年就爆掉。若漂移超标，退路是
  降低质量或拉大 B / C 间距 —— **只改数据，不改代码**。
- **主光同步的观感**：`DirectionalLight` 朝向每帧跟随太阳，阴影生成会持续变化。
  若出现可见的闪烁，退路是改成按角度阈值（例如 0.5°）阶梯式更新。
- **恒星尺寸与体素行星的比例**：恒星半径 1.5e6、行星 1e6，只是 1.5 倍。
  这是游戏尺度取舍，不是物理比例；若观感不佳只调 `scale` 与轨道半径。
