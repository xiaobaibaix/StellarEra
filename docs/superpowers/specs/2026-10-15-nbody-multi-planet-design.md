# N 体星系与多星球 — 设计文档

- **日期**：2026-10-15
- **项目**：StellarEra（Unreal Engine 5.8）
- **状态**：设计已批准，待生成实现计划
- **参考**：`F:\workspace\ue\思路\VoxelTestStudy`（VoxelCosmos / VoxelPlanet / VoxelFactory 三个插件 + 若干设计文档）

> **2026-10-17 变更（本文档部分内容已过期）**：`Orbit` / `CentralMassSolar` /
> `bApplyOrbitInit` 三个字段已删除，天体初态改由「关卡摆放位置 + `InitialVelocity`」
> 决定。**§5.2 属性表、§5.3 的 `StepsPerOrbit` 说明、§6.1 关卡摆放**均涉及这三个字段，
> 请以
> [`2026-10-17-nbody-initial-state-from-level.md`](./2026-10-17-nbody-initial-state-from-level.md)
> 为准。

---

## 1. 目标

把参考项目 `VoxelCosmos` 的 **N 体引力求解器**接进 StellarEra，让关卡里摆的多颗体素星球真的互相绕转，并且**每一颗都能登陆**。

---

## 2. 范围

### 本次做

- 搬运 `FNBodySimulator` + `CosmosTypes`（纯数学求解器与单位制）
- 新增 `UCosmosOrbitComponent`：把「轨道」这件事从「星球」里拆出来，任意 Actor 都能当天体
- 新增 `UCosmosSimulationSubsystem`：持有求解器、每帧推进、写回位置、携带骑在星球上的 Pawn
- `AVoxelPlanetActor` 加**局部坐标模式**，让每帧移动不触发体素世界重建
- 角色 / 飞船的**星球切换**（定期重估最近星球）
- 关卡里摆 **1 颗恒星 + 4 颗行星**，用真实天文数据

### 本次明确不做

- **`ACosmosCameraPawn`**（自由飞行观察相机）—— 本项目已有角色与飞船两套相机
- **`UCosmosTrailComponent`**（轨道拖尾）—— 纯视觉，与本任务无关
- **`ACosmosGameMode`** —— 与现有 `AStellarEraGameMode` 互斥（一个关卡只能有一个 GameMode），
  N 体驱动改由 WorldSubsystem 承担，不污染现有 GameMode
- **`VoxelPlanet` 与 `VoxelFactory` 两个插件** —— 本项目已有自己的 `AVoxelPlanetActor`
  （基于 VoxelFree + 地形模块），换成参考项目的会作废刚完成的地形模块工作
- **距离激活**（`VoxelActivationDistanceAu`）—— 远处的星球照样建体素世界
- **真实 AU 尺度** —— 见 3.1
- **星际跃迁 / 超光速** —— 见 3.2
- 网络复制、存档、战斗、建造

---

## 3. 关键前置发现

三条决定了整个架构，先记录依据。

### 3.1 真 AU 尺度与「可登陆」不可兼得

参考项目的单位制是真实天文尺度：`CosmosUnits::AuToCentimeter = 1.495978707e13`，
`FNBodySimulator::ToWorldLocation()` 直接把 AU 乘这个常量塞进 `FVector`（float32）。

而本项目的星球是 `Radius = 1000` 体素 × `VoxelSize = 100` cm = **半径 1 公里**。两者放在一起：

| 问题 | 数字 |
|---|---|
| float32 在 `1.5e13` cm 处的 ULP | **约 18 公里** —— 比整颗星球还大 18 倍，位置会剧烈抖动 |
| 半径 1 km 的星球在 1 AU 外的张角 | 1e-8 度，是个不可见的点 |
| VoxelFree 在 `1.5e13` cm 处渲染 | 直接崩 |

**结论：`CentimetersPerAu` 必须重新标定。** 本次取 **`1e6`（1 AU = 10 公里）**：

| 项 | 值 |
|---|---|
| 1 AU 世界距离 | 10 km |
| float32 ULP @ `1e6` cm | **0.12 cm** —— 远小于体素边长 100 cm |
| 地球轨道处的轨道速度（周期 300 秒） | 209 m/s |
| 每帧位移 @ 60fps | 3.5 m |

单位制内部仍然自洽（G 仍是 4π²，a = 1 AU 的周期仍是 1 模拟年），变的只是
「1 AU = 多少世界厘米」和「1 模拟年 = 多少真实秒」。

### 3.2 轨道速度是步速的上千倍，所以「参考系跟随」是硬需求

地球轨道处星球速度 209 m/s，角色步速 6 m/s、飞船 80 m/s。
星球表面相对角色以**约 35 倍步速**移动。不跟随的话地面会从脚下飞走。

顺带：本尺度下飞船 80 m/s 跨 1 AU 只要 **125 秒**，所以**飞船参数完全不用改**，
输入层也完全不用动。

### 3.3 必须有中心质量，否则行星不会绕转

行星质量只有 `3e-6 M☉`（地球量级），互相之间的引力可以忽略。
若只按 `CentralMassSolar` 算初始速度、却用真实质量跑积分，行星会直接飞出去。

**结论：星系中心必须有一颗 1 M☉ 的恒星。**

### 3.4 移动星球不会重建体素，但有两处拦路

参考文档的核心约束（源码依据 `VoxelRendererMeshHandler.cpp:268`）：
渲染网格是 `WorldRoot` 的子对象，网格顶点存在组件局部空间，组件变换负责把它搬到世界位置。
所以**移动 Actor 不影响 LOD，改生成器 `Center` 才会重建**。

本项目的 `AVoxelPlanetActor` 有两点与此冲突，见 5.1。

---

## 4. 架构

```
UCosmosSimulationSubsystem  (UTickableWorldSubsystem，单例)
    ├─ 持有 ──> FNBodySimulator（纯数学，无 UObject 依赖）
    ├─ 收集 ──> 关卡里所有带 UCosmosOrbitComponent 的 Actor
    └─ 每帧 ──> 推进 → 写回位置 → 携带骑乘者

AVoxelPlanetActor
    └─ 挂 ──> UCosmosOrbitComponent（质量 / 轨道根数 / 中心质量）

ACosmosBodyActor（不搬）
ACosmosCameraPawn / ACosmosTrailComponent / ACosmosGameMode（不搬）
```

### 依赖方向

- `FNBodySimulator` **不依赖任何 UObject**（只认 `TWeakObjectPtr<AActor>` 做反查），
  与引擎彻底解耦 —— 这是它能在无渲染环境下跑大量步数测试的原因，必须保持。
- `UCosmosOrbitComponent` 只依赖自己所属 Actor 的**位置**，不依赖 Actor 类型。
  恒星因此不需要新类：一个 `StaticMeshActor` 挂上它就是天体。
- `UCosmosSimulationSubsystem` 是唯一的编排者：谁进求解器、每帧顺序、谁被携带。
- `AVoxelPlanetActor` **不知道 N 体的存在** —— 它只知道「我可能每帧被移动」。

---

## 5. 详细设计

### 5.1 AVoxelPlanetActor —— 局部坐标模式

**先说一个好消息**：`GetCenterInVoxels()` 现在算出来**已经是零**。
因为 `RebuildPlanet()` 里 `VoxelWorld->SetActorTransform(ActorTransform)`，
而球心 = `VoxelWorld变换.逆变换(Actor位置)` = 原点。球心本来就在局部原点，**算法不用改**。

真正在挡路的是两处：

**5.1.1 参数哈希会每帧失效。**

`ComputeParamsHash()` 里 `Mix(&ActorLocation, ...)` 把每帧变化的 Actor 位置混进哈希，
于是 `RebuildPlanet()` 顶部的守卫（`ParamsHash == LastParamsHash && VoxelWorld->IsCreated()`）
永远不成立 → **每帧 `DestroyWorld()` + `CreateWorld()`**。

新增属性 `bUseLocalCoordinates`（默认 **true**）。默认 true 对现有那颗静止星球**完全等价**
（球心在原点、Actor 在原点，两条路径结果相同），不会影响已经跑通的东西。

```cpp
const float ScaledRadius = GetScaledRadiusVoxels();
Mix(&ScaledRadius, sizeof(ScaledRadius));

// 局部坐标模式下不能混 ActorLocation：它每帧都在变，混进去等于每帧重建
// 整个体素世界。球心锚在原点、位置由 Actor 变换承担，与生成器参数无关。
if (!bUseLocalCoordinates)
{
    const FVector ActorLocation = GetActorLocation();
    Mix(&ActorLocation, sizeof(ActorLocation));
}
```

**5.1.2 VoxelWorld 的变换不会自己跟。**

这是我们代码里已经写明的坑 —— `RebuildPlanet()` 的注释：

> `AVoxelWorld` 是 Actor 子对象：构造函数里的 `CreateDefaultSubobject` 只建出一个实例，
> 它的变换不会跟着 `PlanetRoot` 走。

现在每次重建时同步一次。局部坐标模式下 Actor 每帧移动，必须**每帧同步**，
否则渲染网格会留在原地、星球跑掉。

把 `RebuildPlanet()` 里那段内联代码抽成公开方法，由子系统在移动星球后调用：

```cpp
/**
 * 把 Actor 变换同步给代理体素世界。
 *
 * AVoxelWorld 是 Actor 子对象，它的变换不跟着 PlanetRoot 走（见 RebuildPlanet
 * 里的说明），所以局部坐标模式下 Actor 每帧移动后必须显式同步一次，
 * 否则渲染网格会留在原地。
 */
void SyncVoxelWorldTransform();
```

`RebuildPlanet()` 里改为调用它，行为不变。

**不受影响的部分**：`GetSurfaceRadiusCmAtDirection()` 把世界方向直接传给地形模块，
而地形是「方向的径向函数」，与世界位置无关 —— 局部坐标模式不改变它。

### 5.2 UCosmosOrbitComponent —— 轨道参数

`UActorComponent`（不是 `USceneComponent`：它只读自己所属 Actor 的变换，不需要自己的变换）。

| 属性 | 默认 | 说明 |
|---|---|---|
| `MassSolar` | `3.0034893e-6` | 质量，单位 M☉（默认地球） |
| `Orbit` | `FCosmosOrbitInit` | 半长轴 / 偏心率 / 倾角 / 升交点 / 相位 / 速度覆盖 |
| `CentralMassSolar` | `1.0` | 中心天体质量，只用于推导初始速度 |
| `bApplyOrbitInit` | `true` | 是否用轨道根数覆盖摆放的位置与速度 |
| `bSimulated` | `true` | 是否每帧把求解器算出的位置写回 Actor |
| `SimulatorIndex` | `INDEX_NONE` | 运行时，求解器索引 |

**`bApplyOrbitInit` 与 `bSimulated` 必须拆成两个开关**（参考项目只有一个 `bSimulated`，
两个语义混在一起，会让恒星的位置在求解器里漂移、在 Actor 上不动，两边分叉）：

- **恒星**：`bApplyOrbitInit = false`（位置保持关卡里摆的样子）、`bSimulated = true`
  （位置由求解器写回，与求解器完全一致）
- **行星**：两个都 `true`
- **纯装饰的远处天体**：两个都 `false`

### 5.3 UCosmosSimulationSubsystem —— 驱动

`UTickableWorldSubsystem`。N 体是耦合系统（A 受 B 的力、B 也受 A 的力），必须单例 ——
WorldSubsystem 的单例由引擎保证，不需要在关卡里摆驱动 Actor，也不污染现有 GameMode
（它已经负责开局登船）。

| 属性 | 默认 | 说明 |
|---|---|---|
| `CentimetersPerAu` | `1e6` | 1 AU = 10 公里 |
| `TimeScale` | `0.00333` | 模拟年 / 真实秒 → 公转一圈 300 秒 |
| `StepsPerOrbit` | `20000` | 每圈积分步数，决定自动步长 |
| `MaxStepsPerFrame` | `200` | 防止 TimeScale 调大后一帧算几千步 |
| `Integrator` | `VelocityVerlet` | 辛积分，误差有界不累积 |
| `SofteningLength` | `0.02` | 引力软化，防止近距离数值爆炸（原 `1e-5`，见下） |
| `RideRadiusScale` | `2.0` | 携带判定：距球心 < 地表半径 × 该值 |

**`SofteningLength` 默认值从 `1e-5` 改成了 `0.02`（2026-10-18）。** `1e-5` 在近距离
交会时等于没有：UniverseMap 三体开局能量漂移 `3.9e-07` 一路健康，到 t ≈ 0.84 模拟年
（约 4.2 分钟 PIE）突然变成 `+547`，天体被甩到 4000 AU 外；那次运行里两体的**最小间距
是 0.013 AU**，`1e-5` 在这个距离上什么也挡不住。

离线复刻引擎的积分器（同款 Velocity Verlet、同款势能、同款步长 `P/20000`）扫软化长度，
跑到 t = 10 模拟年：

| 软化 ε | 漂移@2.0 | 漂移@5.0 | 漂移@10.0 | 最小间距 | 结论 |
|---|---|---|---|---|---|
| 0.0125 | −1.3e-08 | **+1.5e+02** | +1.5e+02 | 0.0052 | t=2 看着好，t=5 炸了 |
| 0.015 | −1.3e-07 | +5.8e-07 | +1.4e-05 | 0.0514 | 撑住了，但漂移在往上爬 |
| **0.02** | +3.2e-08 | +1.9e-07 | **+5.5e-08** | 0.1252 | 撑住，漂移压在 1e-7 |
| 0.03 | +2.9e-03 | +2.4e-03 | +6.0e-03 | 0.0261 | 撑住但交会没解开 |
| 0.05 | +2.9e-08 | +1.7e-08 | +4.4e-06 | 0.0598 | 撑住，漂移在往上爬 |
| 0.075 | +1.3e-08 | +1.5e-07 | +1.1e-07 | 0.0666 | 撑住，漂移压在 1e-7 |

只有 0.02 与 0.075 把漂移按在 1e-7 撑到 t = 10 年。取 0.02 是因为软化把 r 处的力按
`(1 + (ε/r)²)^(−3/2)` 削掉，ε 小 3.75 倍则削弱程度差 `3.75³ ≈ 53` 倍 —— 1 AU 处 0.02
削掉 0.06%，0.075 削掉 0.84%。0.02 仍在天体半径（本关卡 0.075 AU）以下，这是有意的：
软化长度是「点质量模型从这里往下就不成立」的尺度，而两颗 0.075 AU 的球擦到 0.15 AU
以内本来就已经互相穿透了。

⚠ 0.03 那行是反例，别读成「更大的值更稳」。它的 1e-3 漂移不是更稳，是**没解**：那次
交会最近到 0.026 AU，加速度量级 `G·m/(r²+ε²) ≈ 2.5e6` AU/年²，乘一个步长
`dt = 3.0e-5` 年，`Δv` 量级 75 AU/年 —— 而系统的速度尺度才 20 AU/年。一步之内速度翻
几倍，辛积分器不再守恒，只是没炸而已。

⚠ 这一项治的是**数值爆炸**，不是**混沌**。三个 100 M☉ 天体共线对撞本来就是散射问题：
跑到 t = 10 年的过程中仍有一颗被甩到 ~200 AU 外，那是能量守恒（漂移 1e-7）之下的真实
动力学，不是 bug。想让关卡里是个安分的天体系统，得改关卡的初态。

⚠ `MaxAutoPredictionPeriods` 默认 0.5 那张「窗口/P 拐点」表是在软化长度还是 1e-5 时
测的，**没有在新默认值下重测**（见 `2026-10-17-trajectory-system-design.md` §12 的注）。

**`StepsPerOrbit = 20000` 是对参考项目的修正。** 参考项目写死「最短周期 / 1000」，
在本尺度下步长 1e-3 年，而每帧推进 5.55e-5 年 —— **每 18 帧才算一步**，肉眼可见卡顿。
20000 步/圈给出步长 5e-5 年，约每帧一步：

```
每帧推进 = (1/60) × 0.00333 = 5.55e-5 年
步长     = 1 / 20000        = 5.00e-5 年
每帧步数 = 1.11 步
```

能量漂移仍能压在 1e-6 量级（辛积分的误差有界），诊断接口保持有意义。

**每帧顺序**（全在子系统一个 tick 内完成，避免差一帧）：

```
1. 记录每个天体上一帧的世界位置
2. 推进求解器 N 步
3. 对每个天体：
     NewWorld = ToWorldLocation(State.Position)
     Delta    = NewWorld - 上一帧世界位置
4. 对每个天体：
     a. 若 bSimulated：Owner->SetActorLocation(NewWorld)
     b. 若 Owner 是 AVoxelPlanetActor：SyncVoxelWorldTransform()
     c. 把 Delta 施加给骑在这颗天体上的 Pawn
5. 更新光照（本次不做 —— 场景已有光照，见第 7 节第 2 条）
```

### 5.4 参考系跟随

```cpp
// bSweep=false：Pawn 与星球的位移完全相同，相对位置不变；
// 开着扫掠反而会在瞬移穿过地形时被卡住。
Pawn->AddActorWorldOffset(Delta, /*bSweep=*/false);
```

判据：**Pawn 到球心距离 < 地表半径 × `RideRadiusScale`（默认 2.0）**。
半径 1 km 的星球 → 携带范围 2 km。

这个判据同时覆盖三种情况，且飞船在 50 km 外自由飞行时不会被莫名其妙推着走：

- 站在地面上（距球心 ≈ 半径）
- 跳跃中（半径 + 1 m）
- 飞船悬停在 100 m 高度（半径 + 100 m）

代价：每颗星球每帧遍历一次 Pawn 列表（4 颗 × 2 个 Pawn = 8 次距离平方），可忽略。

### 5.5 星球切换

角色和飞船现在都只在 `BeginPlay` 里调一次 `FindNearestPlanet()`。星球在动，最近的那颗会变。

改成**每 0.5 秒重估一次**；变了就 `bHasLanded = false` + `SnapToPlanetSurface()` 重新贴地。

日常行走时不会触发 —— 站在一颗行星上时它在脚下，别的行星至少在几公里外，
最近关系不会翻转。只有坐飞船挪到另一颗附近才会切。

`PlanetSearchRadius` 两边都保持 `1e8` cm（1000 km），在本尺度下覆盖整个星系，不用改。

### 5.6 母星选择

`AStellarEraGameMode::EnsureSpaceshipExists()`（`StellarEraGameMode.cpp:141-146`）
现在是「遍历到第一颗就 `break`」，多星球下结果取决于遍历顺序。

改成**取离世界原点最近的 `AVoxelPlanetActor`**。

**这条规则不是随便挑的，它是「和角色保持一致」推出来的。** 关卡里没有 PlayerStart，
角色生成在**世界原点**；`APlanetCharacter::BeginPlay` 立刻用 `FindNearestPlanet()`
找最近的行星并贴到它的地表。飞船必须落在**同一颗**行星上，否则开局登船会失败 ——
两者相距几公里，而 `BoardingRadius` 只有 600 cm。

所以「离原点最近」同时是角色和飞船的选法，两边天然一致。**改动其中任何一边都必须
同时改另一边**（见风险表第 8 行）。

**由此推出的两个后果：**

1. **母星是轨道最小的那颗行星（金星位，7.2 km），不是地球位。** 圆轨道上
   「离原点的距离 = 半长轴」，所以「最近」永远等于「轨道最小」。这是刻意的安排 ——
   母星就该摆在这个位置，让开局角色就近落在它上面。
2. **世界原点在恒星内部**（恒星球心在原点、半径 1.5 km）。角色生成的一瞬间确实
   在恒星里，但 `BeginPlay` 在第一个 Tick 之前就把它贴到行星地表了。

---

## 6. 关卡摆放

### 6.1 天体阵容

**1 颗恒星 + 4 颗行星**，用真实天文数据。

| 天体 | MassSolar | 半长轴 (AU) | 世界轨道 (km) | 偏心率 |
|---|---|---|---|---|
| 恒星（中心） | 1.0 | — | — | — |
| 行星一（金星位，**母星**） | 2.447e-6 | 0.723 | 7.2 | 0.0068 |
| 行星二（地球位） | 3.003e-6 | 1.000 | 10.0 | 0.0167 |
| 行星三（火星位） | 3.227e-7 | 1.524 | 15.2 | 0.0934 |
| 行星四（木星位） | 9.546e-4 | 5.203 | 52.0 | 0.0484 |

**水星位（0.387 AU = 3.87 km）刻意不放**：恒星视觉半径 1.5 km，行星半径 1 km，
3.87 km 处两者表面只隔 1.37 km，视觉上会贴着。

**恒星不新增类**：关卡里摆一个 `StaticMeshActor`（引擎球体，缩放到半径 1.5 km），
挂上 `UCosmosOrbitComponent`，`MassSolar = 1.0`、`bApplyOrbitInit = false`、`bSimulated = true`。
一个机制覆盖所有天体，恒星也不占体素世界。

**行星**：全部 `AVoxelPlanetActor`，`Radius = 1000` 体素、`VoxelSize = 100` cm（半径 1 km），
`bUseLocalCoordinates = true`，各挂一个 `UCosmosOrbitComponent`，
地形模块用现成的 `UVoxelPlanetNoiseTerrain`，**各给不同种子**。

**母星是行星一（金星位，轨道最小）**：角色与飞船都按「离世界原点最近」选星球
（见 5.6），圆轨道上这等价于「轨道最小」，所以母星必须是轨道最小的那颗。
它的相位决定开局时它在轨道上的哪一点：`PhaseDegrees = 0` 且倾角、升交点都为 0 时，
`ApplyOrbitInit` 算出的位置落在世界 +X 方向（近心点在 +X），便于对照与调试。

### 6.2 性能

4 颗行星 = **4 个体素世界**。参考文档自己警告过这一点（「9 颗行星 = 9 个 Voxel World」）。

现在每颗 `VoxelThreads = 4`，4 颗就是 16 条网格化线程。**按星球数下调**：
母星 4 条、其余每颗 2 条 → 共 10 条。

---

## 7. 风险

| # | 风险 | 影响 | 应对 |
|---|---|---|---|
| 1 | **体素世界在 1e6 cm 尺度下的浮点精度** | 星球可能有轻微抖动 | 理论 ULP 0.12 cm ≪ 体素 100 cm；若实测有抖动，把 `CentimetersPerAu` 降到 `3e5`（1 AU = 3 km） |
| 2 | **光照** —— 星球在 52 km 外，恒星只是个球 | 远处行星可能太暗 | 场景已有平行光（无限远、无衰减），覆盖整个星系；本次不引入参考项目的多恒星平行光系统 |
| 3 | **携带判据的边界** | 飞船在 2 km 高度时被带走，玩家可能觉得「没推杆自己动了」 | `RideRadiusScale` 可调；物理上正确（飞船相对星球静止，星球动它就该动） |
| 4 | **4 个体素世界同时驻留** | 内存与网格化线程争抢 | `VoxelThreads` 下调到每颗 2 条；实测卡顿再考虑距离激活 |
| 5 | **星球切换瞬间的贴地瞬移** | 换星球时角色可能被弹一下 | 只在最近关系真的翻转时触发（日常行走不会）；必要时给 `SnapToPlanetSurface` 加插值 |
| 6 | **辛积分步长与 TimeScale 不匹配** | 调大 TimeScale 后轨道失真 | `MaxStepsPerFrame` 兜底 + 超限自动降 TimeScale 并打日志（沿用参考项目做法） |
| 7 | **`bApplyOrbitInit` 语义与参考项目不同** | 从参考项目搬过来的代码若直接用 `bSimulated` 会踩坑 | 两个开关在 5.2 已明确定义；搬运时同步改注释 |
| 8 | **角色与飞船的选星规则必须同步** | 只改一边 → 开局角色和飞船落在不同行星上，登船失败 | 两边都用「离世界原点最近」（5.6）；改动任一边时检查另一边。若将来加了 PlayerStart，两边都要改成基于出生点选 |

---

## 8. 验收标准

1. PIE 后 4 颗行星**真的绕恒星公转**，一圈约 300 秒，轨道形状与偏心率相符。
2. 站在这颗星球表面**不会被甩掉**，能正常行走、跳跃；抬头能看到别的星球在动。
3. 星球移动时**体素世界不重建** —— 日志里没有反复的 `CreateWorld` / `DestroyWorld`，
   帧率稳定。
4. 开着飞船从母星飞到另一颗行星，**能降落在它的表面上**，按 E 能离船行走。
5. 离船后角色的重力方向指向**脚下这颗**星球（不是原来那颗）。
6. 从另一颗星球起飞飞回母星，同样能降落、离船。
7. `LogDiagnostics` 的能量漂移长期维持在 **1e-5 以下**。
8. 全程无崩溃、无 ensure、无 `LogVoxel` 错误。
9. **回归**：母星的地形起伏、顶点色、材质与本次改动前一致。

---

## 9. 测试方式

- **构建**：用既有的 UBT 直调脚本（绕过 `Build.bat` 的锁问题），构建前需关闭编辑器。
- **纯数学部分**：`FNBodySimulator` 不依赖引擎，理论上可以脱离 UE 跑
  （设初始条件 → 跑若干步 → 看能量漂移）。但**参考项目自己没有 N 体的测试** ——
  它的 `Tests/` 里只有 `FactoryRecipeTest.cpp` 和 `GeologyTest.cpp`，N 体从来只靠
  运行期的 `LogDiagnostics` 验证。本次沿用同样做法：不引入测试框架，
  靠漂移数字做验收（第 8 节第 7 条）。
- **运行验证**：打开编辑器 → 加载关卡 → PIE。
- **回归**：改动不得破坏既有的 `AVoxelPlanetActor` 行为（纯球体生成、材质、LOD Invoker、
  地形模块）。

---

## 10. 落地顺序

1. 搬 `CosmosTypes` + `FNBodySimulator`，把 `CentimetersPerAu` 变成成员
2. `AVoxelPlanetActor` 加 `bUseLocalCoordinates` + `SyncVoxelWorldTransform`
3. 新增 `UCosmosOrbitComponent`
4. 新增 `UCosmosSimulationSubsystem`
5. 参考系跟随 + 星球切换 + 母星选择
6. 关卡摆放 1 恒星 + 4 行星
7. 验收
