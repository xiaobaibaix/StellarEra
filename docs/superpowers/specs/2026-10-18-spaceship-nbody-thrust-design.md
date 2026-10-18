# 飞船推力接入 N 体 — 设计

**日期**：2026-10-18
**分支**：`feat/planet-character-spaceship`

## 1. 背景：现在 W 键完全不起作用

飞船已经是 N 体系统里的一个天体：关卡实例上挂着 `UCosmosOrbitComponent`，
`bSimulated = true`，位置每帧由 `UCosmosSimulationSubsystem::Tick` 第 2 段写回：

```cpp
BodyActor->SetActorLocation(Simulator.ToWorldLocation(State.Position), /*bSweep=*/false);
```

而推力仍然由 `UFloatingPawnMovement` 算成**位移**。两者是两条互不相干的通路，
写回这一条每帧都把移动组件刚推出去的位置覆盖掉 —— 所以按 W 什么也不会发生，
飞船只能被引力牵着走。这就是「起飞不了」的全部原因。

## 2. 目标

把飞船的移动控制（W/S 前后、Space/LeftControl 升降、Shift 加速、Tab 巡航）
改成**施加在它自己那个 N 体上的加速度**，由求解器积分。

**非目标**（本轮不做）：

- 改引力尺度（`CentimetersPerAu` / `TimeScale` / 行星质量）。本轮只调推力。
- 角动量。鼠标转向（yaw/pitch/横滚）继续走 `AddActorLocalRotation`，是**运动学**的：
  它只决定「推力朝哪」，不参与积分。
- 手感微调。本轮把机制接对、填一组能飞的数，实测之后再调。

## 3. 两条硬约束

### 3.1 推力下限被当地重力锁死

本关卡中心天体 M = 100 M☉、飞船停在 r = 0.07425 AU。G = 4π²（游戏单位）：

```
表面重力 = G·M/r² = 3947.84 / 0.005513 = 716,089 AU/年²  =  7.94e6 cm/s²
```

**推力加速度必须大于这个值，否则飞船一步都升不起来。** 这跟怎么接线无关，
是物理。改动前飞船的最大推力是 4000 + 12000 = 16000 cm/s²，**只有重力的 0.20 %** ——
就算把接线接得完美，那个数也只是让飞船更稳地趴在地上。

换算关系（`1 AU = CentimetersPerAu 厘米`，`1 模拟年 = 1/TimeScale 真实秒`）：

```
1 AU/年²  =  CentimetersPerAu × TimeScale²  厘米/秒²
1 AU/年   =  CentimetersPerAu × TimeScale   厘米/秒
```

`CentimetersPerAu = 1e6`、`TimeScale = 0.00333` 时分别是 **11.089** 和 **3330**。

### 3.2 模块依赖方向：飞船不能调子系统

`StellarEra.Build.cs` 里写死的分层是无环的：

```
StellarEra ─→ StellarEraVehicle ─→ StellarEraPlanet
     └──────→ StellarEraCore
```

`StellarEraVehicle` **不能**依赖 `StellarEra`（会成环）。所以飞船里既不能
include `CosmosOrbitComponent.h`，也不能调 `UCosmosSimulationSubsystem`。

**结论：力的方向只能是「子系统下来读飞船」，不能是「飞船上去写子系统」。**
子系统 `#include "SpaceshipPawn.h"` 是允许的，而且它已经这么做了
（`GetContactRadiusCm` 那条路）。本轮沿用同一模式。

## 4. 数值标定

| 量 | 值 | 推导 |
|---|---|---|
| 行星表面重力 | 716,089 AU/年² = 7.94e6 cm/s² | §3.1 |
| 该处圆轨道速度 | 230.6 AU/年 = 7.678e5 cm/s | √(G·M/r) |
| **`ThrustAcceleration`** | **1.2e7 cm/s²** | = 1.51 × 表面重力，留 51% 净余量 |
| **`BoostMultiplier`** | **3.0** | Shift 时 3.6e7 cm/s² |
| **`DampingTime` τ** | **0.01 秒** | 极速 = 推力 × τ = **1.2e5 cm/s = 1.2 km/s** |
| 极速（Shift） | 3.6e5 cm/s = 3.6 km/s | |
| 求解器里的 `LinearDamping` k | 3.0e4 /年 | = 1 / (τ · TimeScale) |
| 求解器里的推力 | 1.082e6 AU/年² | = 1.2e7 / 11.089 |
| 体素 `LODRange` / `CollisionsRange` | 4.5e5 厘米 = 4.5 公里 | 范围 ≥ 最大速度 × `PredictionTime`，最大速度**含** `BoostMultiplier`：1.2e5 × 3 × 1 s = 3.6e5 厘米 |

几条读法：

- **阻尼极速（1.2 km/s）只有圆轨道速度的 0.156 倍** —— 光按 W 飞不出行星，
  只能悬停、爬升、绕圈。要进入轨道必须开巡航（关阻尼）持续加速，
  从零加到圆轨道速度需要 7.678e5 / 1.2e7 ≈ **0.064 秒**。巡航因此是个有意义的机制，
  不是可有可无的开关。
- **响应时间是硬的**。推力下限被重力顶到 8e6，所以「加速到极速的耗时」= 约 3τ = 30 ms
  （60 fps 下两帧）。旧的「2 秒加到 80 m/s」是旧尺度（飞船不参与引力）的产物，回不去了。
  这一档是**在「有惯性感」和「世界跟得上」之间取的折中**：τ 再大极速就上千 km/s，
  体素调用器的范围要跟着涨到几十公里。

## 5. 设计

### 5.1 求解器：两个新状态量

`FCosmosBodyState` 加两项，默认都是零 —— 现有行为一字不变：

```cpp
/**
 * 外力加速度（AU/年²）。**不是引力**，由外部每帧写入，求解器只加不解释。
 * 默认零 = 纯引力天体。
 */
FVector3d ExternalAcceleration = FVector3d::ZeroVector;

/**
 * 线性阻尼系数（1/年）：a_damp = −k·v。默认 0 = 无阻尼。
 * 单位是 1/年 而不是 1/秒，因为求解器的时间单位是模拟年。
 */
double LinearDamping = 0.0;
```

`ComputeAccelerations()` 末尾那一行：

```cpp
// 改前
Accelerations[i] = G * Sum;
// 改后
Accelerations[i] = G * Sum
    + Bodies[i].ExternalAcceleration
    - Bodies[i].LinearDamping * Bodies[i].Velocity;
```

**为什么阻尼必须在求解器里逐子步算，而不是在外面算好一个常量加速度：**
`StepVelocityVerlet` 一帧内要跑最多 `MaxStepsPerFrame = 200` 个子步，覆盖约 7.5 毫秒的
世界时间，而 τ = 10 毫秒 —— 一帧之内速度要变 2 倍以上。在外面按帧首速度算一个常量阻力，
等于把阻尼削掉一大截。

**稳定性**：`StepVelocityVerlet` 在步骤②重算加速度时用的还是**踢之前**的速度
（`x += v·dt/2` → `ComputeAccelerations` → `v += a·dt`），所以阻尼项是显式欧拉，
每步的放大因子是 `1 − k·dt`。要求 `k·dt < 2`，而
`k·dt = 3.0e4 × 1.25e-7 = 3.75e-3`，余量三个数量级。**积分器不用改。**

### 5.2 预测副本要清零

`FNBodySimulator::PredictTrajectory` 是 `FNBodySimulator Copy = *this;` 再往前跑。
副本会连 `ExternalAcceleration` 和 `LinearDamping` 一起继承，于是星图上的预测线
会画成一条「一直加速」的曲线。

**决定：副本里把这两项清零** —— 预测的是**松手之后会去哪**的纯引力轨迹。
理由：预测窗口默认 0.5 × 系统最短周期（本关卡 ≈ 0.9 秒真实时间），
在这个窗口里推力主导的曲线没有参考价值；而纯引力轨迹正是玩家要的信息。
想要「一直按着 W 会去哪」是另一个特性，应该显式做，不该靠静默继承。

### 5.3 子系统：每帧下来读飞船的旋钮

`UCosmosSimulationSubsystem` 新增 `ApplyControlledBodyForces()`，在 `Tick` 里
紧挨着 `RefreshPlanetContactRadii()` 调用 —— 两者都是「推进之前把引擎侧状态喂给求解器」：

```cpp
// ── 0. 刷新行星接触半径 + 施加受控天体的外力 ──
RefreshPlanetContactRadii();
ApplyControlledBodyForces();
```

函数体（走一遍 `Bodies`，只认 `ASpaceshipPawn`）：

```cpp
void UCosmosSimulationSubsystem::ApplyControlledBodyForces()
{
    // 1 AU/年² 等于多少厘米/秒² —— 与速度那条是同一个换算族，见 §3.1。
    const double CmPerS2PerAuPerY2 = CentimetersPerAu * TimeScale * TimeScale;

    for (UCosmosOrbitComponent* Component : Bodies)
    {
        if (!Component || Component->SimulatorIndex == INDEX_NONE) { continue; }

        const ASpaceshipPawn* Ship = Cast<ASpaceshipPawn>(Component->GetOwner());
        if (!Ship) { continue; }

        // 求解器的 GetBodyMutable 是裸的 Bodies[Index]，越界不报错、直接是 UB ——
        // 校验只能在这一层做，与 GetBodyVelocityGameUnits 同一个理由。
        if (Component->SimulatorIndex >= Simulator.GetNumBodies()) { continue; }

        FCosmosBodyState& Body = Simulator.GetBodyMutable(Component->SimulatorIndex);

        Body.ExternalAcceleration =
            FVector3d(Ship->GetThrustAccelerationWorld()) / CmPerS2PerAuPerY2;

        // τ 是真实秒，求解器的时间单位是模拟年，所以除一个 TimeScale。
        const float DampingTime = Ship->GetDampingTime();
        Body.LinearDamping = DampingTime > 0.f
            ? 1.0 / (double(DampingTime) * TimeScale)
            : 0.0;
    }
}
```

**帧序**：飞船的 `Tick` 与子系统的 `Tick` 谁先谁后不确定，所以输入会差一帧。
差一帧对持续按住的推力没有影响，接受。

### 5.4 飞船：暴露旋钮 + 收回速度

`ASpaceshipPawn` 新增（全部只用 `Engine` 里的类型，不碰 `StellarEra`）：

```cpp
/**
 * 本帧要施加在自己 N 体上的推力加速度（厘米/秒²，世界坐标）。零 = 无推力。
 *
 * 由 UCosmosSimulationSubsystem 每帧下来读走。方向只能是「子系统读飞船」——
 * 飞船不能反过来调子系统，模块依赖会成环（见 spec §3.2）。
 *
 * 方向 = (机头 × ThrustInput + 机身上 × VerticalInput)，按 UFloatingPawnMovement
 * 的 GetClampedToMaxSize(1) 同样的规则夹一下：斜着同时按 W 和 Space 不该比
 * 单按一个多出 41% 的推力。
 */
FVector GetThrustAccelerationWorld() const;

/** 阻尼时间常数（秒）。0 = 不阻尼（巡航）。 */
float GetDampingTime() const;

/**
 * 由子系统在写回位置时一并写入的**求解器速度**（厘米/秒，世界坐标）。
 * 本类不自己算速度 —— 位移归求解器，速度也就该由它给。
 */
void SetCosmosVelocity(const FVector& InVelocity) { CosmosVelocity = InVelocity; }

/** 覆写：让引擎侧所有问「这艘船多快」的地方（HUD、体素调用器的预测）拿到真值。 */
virtual FVector GetVelocity() const override { return CosmosVelocity; }
```

`GetVelocity()` 这一处覆写是**单点收口**，替掉了三个散落的改点：

- `IsLanded()` 的第一道判断（`GetVelocity().Size() > LandingSpeedThreshold`）**一字不用改**；
- `GetStatus().Speed` 改成读 `GetVelocity()`；
- `UVoxelInvokerWithPredictionComponent::GetInvokerGlobalPosition_Implementation`
  用的是 `GetOwner()->GetVelocity()`（插件源码 `VoxelInvokerComponent.cpp:168`），
  覆写之后预测点前移量才是真实速度 × `PredictionTime`。删掉移动组件后
  `RootComponent->GetComponentVelocity()` 不再有人写，不覆写的话预测会静默失效。

速度回写放在 `Tick` 第 2 段的写回循环里，与位置写回同一个 `bSimulated` 门：

```cpp
if (Component->bSimulated)
{
    const FCosmosBodyState& State = Simulator.GetBody(Component->SimulatorIndex);
    BodyActor->SetActorLocation(Simulator.ToWorldLocation(State.Position), /*bSweep=*/false);

    if (ASpaceshipPawn* Ship = Cast<ASpaceshipPawn>(BodyActor))
    {
        Ship->SetCosmosVelocity(ToWorldVelocityScaled(State.Velocity));
    }
}
```

`ToWorldVelocityScaled` 是 `UCosmosSimulationSubsystem` 上新增的一个小换算
（**不是** `FNBodySimulator::ToWorldVelocity`）：

```cpp
/**
 * 游戏单位速度（AU/年）→ **玩家感知**的世界速度（厘米/秒）。
 *
 * 即「飞船在真实一秒里走过多少厘米」。1 模拟年只花 1/TimeScale 真实秒，
 * 所以 v_world = v_game × CentimetersPerAu × TimeScale（= 3330）。
 *
 * ⚠ 与 FNBodySimulator::ToWorldVelocity 差 YearToSecond · TimeScale ≈ 1.05e5 倍 ——
 * 那个是「1 模拟年 = 1 真实年」的实时值，屏幕上的数不是它。NBodySimulator.h
 * 里已经把这件事写明了。
 */
FVector ToWorldVelocityScaled(const FVector3d& GameUnitVelocity) const;
```

### 5.5 删掉 `UFloatingPawnMovement`

**不能只「不喂它输入」就留着。** `UFloatingPawnMovement::TickComponent` 每帧都会
`MoveUpdatedComponent(Velocity * dt)`，留着它就会和求解器抢着写位置。

删掉之后 `Tick` 第 4 段塌缩成两件事：

```cpp
// --- 4. 推力 ---
// 推力不再由这里施加 —— 本类只把「想施加多少」暴露出去（GetThrustAccelerationWorld），
// 由 UCosmosSimulationSubsystem 每帧下来读走、换算、写进求解器。
// 本段只剩相机后缩需要的 ThrustInput，见第 5 段。
```

具体删掉的：`FloatingMovement` 组件本身、`BaseAcceleration` / `BaseDeceleration`
两个成员与 `BeginPlay` 里的抄写、`ForwardSpeed`、保底速度那段、
`bCruiseCoasting` 那段 slerp、两条 `AddInputVector`。

**巡航的语义变了**：旧代码在巡航时把速度矢量 slerp 到机头方向（为了「锁住速率」）。
新模型下不需要了 —— 加速度本来就沿机头，速度会自然弯过去；松手时无推力无阻尼，
走纯引力测地线。这比旧行为更对。巡航 = **只关阻尼**，推力照给。

### 5.6 体素调用器范围

`SpaceshipPawn.cpp` 构造函数里 `LODRange` / `CollisionsRange` 从 `20000.f`（200 米）
改成 `450000.f`（4.5 公里）。

原注释（`SpaceshipPawn.cpp:45-48`）自己写着：巡航 8000 cm/s、范围 200 米，
不预测只剩 2.5 秒提前量。调用器的位置是搬到**预测点**去的，所以范围必须罩得住
`速度 × PredictionTime` 这个偏移量 —— 而最大速度要**含** `BoostMultiplier`：
1.2e5 × 3 = 3.6e5 cm/s，`PredictionTime = 1.f` → 偏移 3.6e5 厘米，取 4.5 公里留
25% 余量。

巡航（关阻尼）下速度无上界，任何有限范围都盖不住 —— 这是已知上限。

**代价未知**：本关卡行星半径只有 750 米，4.5 公里的范围等于整颗行星全程满精度 + 带碰撞。
这一项要实测；嫌贵就往回收，注释里写清推导。

### 5.7 诊断措辞

有受控天体时总能量与总动量都不再守恒，`GetEnergyDriftRatio()` 会报出一个巨大的数
（那是推力做功，不是积分误差）。`LogDiagnostics` 里加个后缀：

```cpp
UE_LOG(LogTemp, Log, TEXT("能量漂移  : %.6e  （辛积分应 < 1e-5）%s"),
    Simulator.GetEnergyDriftRatio(),
    bHasForcedBodies ? TEXT("  ⚠ 含受控天体，此值不守恒") : TEXT(""));
```

`bHasForcedBodies` 由 `ApplyControlledBodyForces` 顺带置位（有任何天体
`ExternalAcceleration` 或 `LinearDamping` 非零即为真），每帧重置。

## 6. 参数清单：删 / 改 / 加

### `ASpaceshipPawn`

| 现在 | 处置 |
|---|---|
| `UFloatingPawnMovement` 组件 | **删**（§5.5） |
| `FloatingMovement->MaxSpeed = 8000` | **删**。极速由 `ThrustAcceleration × DampingTime` 给出 |
| `FloatingMovement->Acceleration = 4000` | **删**，由 `ThrustAcceleration` 取代 |
| `FloatingMovement->Deceleration = 2000` | **删**，由 `DampingTime` 取代 |
| `FloatingMovement->TurningBoost = 8` | **删**（只被巡航那段 slerp 用） |
| `BaseAcceleration` / `BaseDeceleration` | **删**（没有组件要抄了） |
| `MinThrustSpeed = 2000` | **删**。阻尼模型下按 W 自然收敛到极速，不需要「保底 + 锁速」 |
| `BoostAcceleration = 12000` | **改成 `BoostMultiplier = 3.0`** |
| `bPreserveVelocity = false` | **保留**，语义改为「关掉阻尼」 |
| `ForwardSpeed` | **删**（只服务保底速度判断） |
| `ThrustInput` / `VerticalInput` / `RollInput` | 保留，语义不变 |
| — | **加** `ThrustAcceleration = 1.2e7f` |
| — | **加** `BoostMultiplier = 3.0f` |
| — | **加** `DampingTime = 0.01f` |
| — | **加** `CosmosVelocity` 成员 + `SetCosmosVelocity` + `GetVelocity()` 覆写 |
| — | **加** `GetThrustAccelerationWorld()` / `GetDampingTime()` |

### `FSpaceshipStatus`

| 现在 | 处置 |
|---|---|
| `float Speed` | 保留，改成求解器速度 |
| `float MaxSpeed` | 保留，重定义为「阻尼极速 = 推力 × τ」；巡航（无阻尼）时为 0 |
| `bool bThrustLocked` | **删** |
| 其余字段 | 不变 |

`AStellarEraHUD::DrawStatusPanel` 跟着改：删掉 `bThrustLocked` 那个分支
（推力标签只剩 `W 加速` / `S 反推` / `无`），速度那一行按 `MaxSpeed > 0` 分两种印法。

### 被这两处改动带出来的过时注释（一并修）

- `NBodySimulator.h:36-38` —— `FCosmosBodyState::Radius` 的注释说「飞船是 0，所以
  飞船落到地表 = 飞船的**中心点**被钳在地表半径上」。**已不成立**：A+B 那次改动
  让飞船的 Radius = 碰撞球半径（300 厘米）。
- `NBodySimulator.h:264-267` —— `ComputeContactShares` 的注释说「本项目的飞船就是
  `MassSolar = 0`」。实际是 `1e-6`。
- `SpaceshipPawn.h:55-57` —— 类注释说「位移交给 `UFloatingPawnMovement`」。
- `SpaceshipPawn.h:143` —— `Collision` 的注释说「也是 `UFloatingPawnMovement`
  扫掠用的形状」。
- `SpaceshipPawn.cpp:45-48` —— `VoxelInvoker` 注释里的「巡航速度 8000 cm/s」
  与「凭空多出 80 m 预警」，两个数都变了。

## 7. 已知后果

1. **飞船的位移从此完全归 N 体。** 在没有 `UCosmosOrbitComponent` 的关卡里
   （例如 `NewMap` 那样的平地测试图），飞船将无法移动 —— 这是「飞船的移动就是它的
   N 体」的直接推论，不是 bug。需要飞行的关卡都得挂上那个组件。
2. **不按 W 时会掉下来。** 阻尼作用在自由落体上 → 落地有终端速度（自由落体的
   终端速度是 `g/k`；满推力下的极速才是 `推力 × τ`），不是硬砸。阻尼也作用在轨道
   运动上 → 松手后飞船会减速掉下来。
   这是「不操作就会掉」的物理，符合直觉，但意味着**停不住在轨道上**，除非开巡航。
3. **飞船的引力对行星仍然可忽略**（`MassSolar = 1e-6`，对行星的加速度量级
   7e-3 AU/年²，比行星自己的重力小 8 个数量级）。
4. **能量/动量诊断失去意义**（§5.7 只加措辞，不改算法）。
5. **阻尼作用在惯性系速度上，不是相对携带天体的速度。** 求解器不知道谁携带谁
   （那是 `CarryRiders` 的事，而且是单向的），阻尼项就是 `−k·v`，`v` 是求解器里的
   绝对速度。所以飞船随行星公转、或者停靠在运动中的行星上时，阻尼抽走的是
   `k·v_行星` 那个量级（本关卡 8.44e5 AU/年²，是行星 A/B 表面重力的 137 倍）——
   后果是**在运动的行星上停不住**；而且 `IsLanded()`（`SpaceshipPawn.cpp`，
   阈值 `LandingSpeedThreshold`）比的是**世界**速度、恒为假，HUD 的「落地」显示与
   上下船交互都会失效。当前 probe 看不见它，因为飞船骑的中心天体速度恰为 0。
6. **响应很硬**（约 30 ms 到极速），见 §4。

## 8. 测试策略

**纯数学层（`FNBodySimulator`，可脱离引擎跑）**：

1. 单天体 + 恒定 `ExternalAcceleration`：解析解 `x = x₀ + v₀t + ½at²`，
   相对误差应 < 1e-9（Velocity Verlet 对常力是精确的，误差只来自浮点）。
2. 单天体 + `LinearDamping`，无引力：解析解 `v = v₀·e^(−kt)`，
   与数值解的相对误差应随步长线性下降（显式欧拉是一阶）。
3. 终端速度：常力 + 阻尼，长跑之后 `v → A/k`，相对误差 < 1e-6。
4. **回归**：现有能量漂移测试造的全是点质量、两项都为零，结果必须一字不变。
5. `PredictTrajectory` 的副本不继承外力：给主求解器设上外力，调 `PredictTrajectory`，
   主求解器状态不变，且预测轨迹与「外力为零」的对照跑逐点一致。

**引擎层（无头 `-game` probe，沿用 `%TEMP%\ue_mcp\run_game_probe.ps1`）**：

6. 开局日志里飞船的 `Radius` 仍是 `0.0003 AU`（A+B 那条没被这次改动打回去）。
7. 按 W（用 `-game` 下没法按键 —— 改为临时给 `ThrustAcceleration` 一个非零的
   默认方向）后，飞船的高度在若干帧内单调上升，且**加速度量级与 1.2e7 cm/s² 同量级**
   （对照 `G·M/r²`，应当看到净上升加速度 ≈ 0.5 g）。
8. 松手后速度按 τ ≈ 10 ms 衰减（不是 2 秒）。

**视觉（用户在 PIE 里确认）**：

9. 飞船能起飞、能悬停、能落地；HUD 的「速度」显示的是真实速度。
10. 高速飞行时地形跟得上（不穿模）—— 这是 §5.6 那 4.5 公里范围是否够的判据。

## 9. 显式不做

- 不改引力尺度、不改行星质量、不改 `CentimetersPerAu` / `TimeScale`。
- 不做角动量 / 姿态的物理化。
- 不做「按着 W 会去哪」的推力感知预测。
- 不把飞船的推力抽成通用接口。现在只有一艘船，`Cast<ASpaceshipPawn>` 就是全部；
  等出现第二个受控天体（第二艘船、导弹）再抽 —— 那时抽出来的形状也才看得清。
