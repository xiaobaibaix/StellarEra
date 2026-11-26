# 飞船刚体驱动：求解器只喂力

日期：2026-11-26
分支：`feature/ship-force-driven`

## 目标

1× 时飞船是 UE 刚体：Chaos 负责积分与接触解算，N 体求解器只算受力（引力 / 推力 / 阻力）并把力加到刚体上，物理步之后把刚体的位置 / 速度读回求解器。

**只改飞船。** 行星与恒星保持现状（求解器积分 + 写回位置）——它们没有物理体，把 6378 km 的体素行星改成模拟刚体会直接毁掉体素流送与渲染。

## 已确认的取舍

- 倍率 ≠ 1×：切回求解器驱动（瞬移写回），船的碰撞关闭。后续做「临近碰撞自动回到 1×」。
- 本轮**不处理**，已知会导致落地不稳：
  - 行星仍被求解器每帧瞬移，静态块的物理速度是 0，接触摩擦 / 惯性不对（要等浮动原点）；
  - 30 m 碰撞块窗口：高速下船前方没有碰撞体；
  - 1.5e12 cm 世界坐标下的 Chaos 精度；
  - 船的力矩 / 翻滚：第一版不做，姿态仍由求解器写。

## 设计

### 状态

- `FCosmosBodyState` 新增 `bool bPhysicsDriven`，由求解器在积分与接触里跳过该体。
- 子系统新增 `bool bShipPhysics`（当前倍率是否 1×），由控制器在 `AdjustTimeScale` 里调用 `SetTimeScaleMultiplier` 通知。

### 刚体

- `ASpaceshipPawn`：进入物理模式时对 `Hull`：
  - `SetSimulatePhysics(true)`；
  - `SetEnableGravity(false)`（重力由求解器喂）；
  - `SetMassOverrideInKg(NAME_None, MassKg, true)`，`MassKg = State.Mass × CosmosUnits::SolarMassToKilogram`；
  - `SetUseCCD(true)`；
  - `SetLinearDamping(1 / DampingTime)`（`DampingTime <= 0` 时为 0）。
- 退出物理模式：`SetSimulatePhysics(false)`，由求解器瞬移接管。

### 每帧施力

- 物理模式下，子系统对船每帧：
  - `A_game = Body.GravitationalAcceleration + Body.ExternalAcceleration`（AU/年²）；
  - `A_world = CosmosUnits::GameAccelerationToWorldCmPerS2(A_game, CentimetersPerAu, TimeScale)`（厘米/秒²）；
  - `F = MassKg × A_world`，加到刚体质心（`AddForce`）。
- 力用上一子步算出的加速度（一帧滞后），第一版接受。

### 求解器跳过

- `StepVelocityVerlet` / `StepEuler`：`bPhysicsDriven` 的体跳过位置 / 速度积分，但仍参与 `ComputeAccelerations`（别人要它、它要引力）。
- `ResolveContacts`：跳过所有涉及 `bPhysicsDriven` 体的配对。
- `RefreshShipContactProbes`：物理模式下清空探针（`SetContactProbes({})`，`Radius = 0`）。
- 写回（`CosmosSimulationSubsystem::Tick` 第 2 段）：物理模式跳过船的位置写回与 `SetCosmosVelocity`；**姿态仍按现有方式写**。

### 读回

- 物理步之后（下一次子系统 Tick 开头）：把 Actor 的位置 `Simulator.ToGameUnitPosition`、线速度 `CosmosUnits::WorldCmPerSecondToGameVelocity` 写回 `FCosmosBodyState`。
- 读回误差（读回值 vs 上一帧写进去的值）打 Verbose 日志，验证用。

### 模式切换

- 进入物理（1×）：同步刚体到求解器位置一次、设初始速度、清探针、开模拟。
- 退出物理（≠1×）：关模拟、求解器写回恢复、接触关闭（按用户要求）。

### 接口

`ICosmosBody` 新增（世界单位，换算在调用方做）：

- `virtual void SetPhysicsDriven(bool bDriven)`；
- `virtual bool IsPhysicsDriven() const`；
- `virtual void AddCosmosForceWorldCmPerS2(const FVector& AccelerationWorldCmPerS2)`——Pawn 内部乘质量并 `AddForce`；
- `virtual void SetCosmosLinearDamping(float DampingPerSecond)`。

### 失败处理

刚体不可用（无 `BodyInstance` / 质量为 0 / `SetSimulatePhysics` 失败）时退回求解器驱动，打一次 Error 日志。

## 验证

1. 编译通过（`StellarEraEditor`）。
2. PIE：1× 下有推力时悬停不掉、松推力自由落体；倍率切到 ≠1× 后回到瞬移、切回 1× 不跳变。
3. 日志：模式切换行、读回误差行有数。
4. 落地行为只记录现象，不承诺稳定（已知边界见上）。

## 涉及文件

- `Source/StellarEraCosmos/Src/CosmosBody.h`（接口）
- `Source/StellarEraCosmos/Src/NBody/NBodySimulator.h/.cpp`（`bPhysicsDriven`、跳过积分与接触）
- `Source/StellarEraCosmos/Src/CosmosSimulationSubsystem.h/.cpp`（模式、施力、读回、跳过写回）
- `Source/StellarEraPawns/Src/Spaceship/SpaceshipPawn.h/.cpp`（刚体开关、施力、读回接口）
- `Source/StellarEra/Src/StellarEraPlayerController.cpp`（倍率通知）
