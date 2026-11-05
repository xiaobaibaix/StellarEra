# 飞船刚体接触 — 设计

- **日期**：2026-11-05
- **项目**：StellarEra（Unreal Engine 5.8）
- **模块**：`StellarEraCosmos`（求解器 + 子系统）、`StellarEraPawns`（飞船输入）
- **状态**：设计已确认，待实现
- **前置**：`2026-10-18-spaceship-nbody-thrust-design.md`（飞船推力交给求解器）、
  `2026-10-31-ship-voxel-contact-design.md`（接触跟着体素地形走）

---

## 1. 目标

用户原话：

> 飞船接触地面就会立马停住，且没有那种翻转的效果，也就是看到飞船机翼尖碰到地表就停住了，
> 不再有任何变形，没有那种正常落下去然后在重力的调整下的物理表现，比如机翼碰到地面，
> 那么他就像是碰到阻碍，可能被这一下导致飞船旋转起来落到地上？

目标：**接触点不在船心时，接触力要产生绕重心的力矩**，飞船因此翻滚、倾倒、停稳，
而不是整体平移后被钉死。

已确认的三个取舍：

| 问题 | 选择 |
|---|---|
| 姿态权威 | **全物理姿态**：朝向、角速度、惯性张量都归求解器；鼠标/横滚变角加速度 |
| 接触模型 | **有摩擦的刚体**：法向冲量 + 库仑摩擦，恢复系数默认 0 |
| 落地恢复 | **纯物理，不辅助**：不自动回正，玩家用 A/D 与推力自救 |

代价（用户已确认）：现有「甩一下鼠标，转一会儿再停」的手感要用同一组参数重建
（见 §3.6，总转角与衰减常数都不变）；落地自由视角与自动回正的语义要改。

---

## 2. 现状：为什么是「钉住」

四条，缺一不可：

1. **没有旋转状态**。`FCosmosBodyState` 只有位置、速度、质量、半径、探针，
   没有朝向、角速度、惯性。`StepExact` 每步只推进平移。
2. **接触只取最深的一个探针**。`ResolveProbeContact` 遍历全部探针，只记住
   `DeepestPenetration` 与那一个法线，然后把**整个天体**沿法线平移
   （`ProbeBody.Position += Normal * ...`）。机翼尖碰到地面时，抵消的是整船的位移，
   没有任何量作用在「离重心多远」上。
3. **飞船开着 `bZeroVelocityOnContact`**。`AddBodyToSimulator` 给飞船置
   `State.bZeroVelocityOnContact = true`，接触分支把飞船**整条速度**设成行星速度：
   相对速度瞬间归零，连切向滑行都没有。这是「碰到就停」的直接来源。
4. **姿态是运动学的**。`ASpaceshipPawn::Tick` 用
   `AddActorLocalRotation` 直接写 Actor 朝向，与求解器无关。即使求解器算出了角速度，
   也会被下一帧的写回覆盖。

另有一条形状约束决定了接触求解的成本上限：求解器一帧最多跑
`MaxStepsPerFrame = 200` 个子步，接触在**每个子步**里解算一次。接触求解必须是
每步几十条指令量级的常数开销，不能有体素查询或堆分配。

---

## 3. 设计

### 3.1 求解器状态：新增字段，全部 opt-in

`FCosmosBodyState` 增加：

```cpp
/** 朝向（本体系 → 世界系）。只有 bRigidBody 时被积分与写回。 */
FQuat4d Orientation = FQuat4d::Identity;

/** 角速度（rad / 模拟年，世界系）。 */
FVector3d AngularVelocity = FVector3d::ZeroVector;

/**
 * 逆惯性张量，**本体系对角**（1 / (M☉·AU²)）。
 * 三个分量对应机体本地 X/Y/Z。零向量 = 不响应力矩。
 */
FVector3d InverseInertiaLocal = FVector3d::ZeroVector;

/** 外力矩产生的角加速度（rad / 模拟年²，世界系）。由外部每帧写入。 */
FVector3d ExternalAngularAcceleration = FVector3d::ZeroVector;

/** 角阻尼（1 / 模拟年）：α_damp = −k·ω。 */
double AngularDamping = 0.0;

/** 是否作为刚体积分姿态、响应接触力矩。默认 false。 */
bool bRigidBody = false;

/** 接触摩擦系数（库仑）。只读**探针方**的值，球方的被忽略。 */
double ContactFriction = 0.6;

/** 接触恢复系数。0 = 完全非弹性。只读探针方的值。 */
double ContactRestitution = 0.0;
```

`bRigidBody == false`（行星、恒星、全部现有点质量测试）时，上面这些字段一个都不读、
不改：`ExternalAngularAcceleration` 恒为零、`InverseInertiaLocal` 恒为零、角速度不积分，
现有行为逐位不变。全项目只有飞船打开它。

### 3.2 姿态积分

放在 `FNBodySimulator::StepExact` 里、三种积分器的 `switch` **之后**、
`ResolveContacts` **之前**：

```
对每个 bRigidBody 天体：
    α = ExternalAngularAcceleration − AngularDamping · ω
    ω += α · dt
    q += 0.5 · quat(0, ω) ⊗ q · dt
    q  = normalize(q)
```

放在 `StepExact` 而不是某个积分器内部，理由与 `ResolveContacts` 相同：三种积分器、
`Step` 与 `PredictTrajectory` 走的是同一个入口，姿态不会漏掉任何一条路径。
放在 `ResolveContacts` 之前，是为了让接触解算看到的角速度已经是本子步积分后的值 ——
接触修正的 ω 会在下一个子步继续参与积分。

**探针在帧内是冻结的**：`ContactProbes` 每帧只按 Actor 变换旋转一次（见 §3.7 的
`RefreshShipContactProbes`），整帧最多 200 个子步共用这一批世界系偏移。所以「机翼触地
→ 角速度产生 → 下一个子步姿态改变」成立，但**第三步「探针位置改变」不发生** ——
接触力臂最多滞后一帧的转角。这是 16 点 + 每帧快照这一设计固有的近似（高速翻滚时
误差可达数米），不是实现缺陷；抖动/侧推的幅度只能在 PIE 里判（§6.3）。

### 3.3 单位

求解器的时间单位是模拟年，输入与显示是真实秒。换算只有一句：
`dt_sim = TimeScale · dt_real`。

| 量 | 真实秒 | 求解器 | 换算 |
|---|---|---|---|
| 角速度 ω | rad/s | rad/年 | `ω_sim = ω_real / TimeScale` |
| 角加速度 α | rad/s² | rad/年² | `α_sim = α_real / TimeScale²` |
| 角阻尼 k | 1/s | 1/年 | `k_sim = k_real / TimeScale` |

`TimeScale` 是 `UCosmosSimulationSubsystem` 上的属性（默认 0.00333 模拟年/真实秒）。
换算集中在 `ApplyControlledBodyForces`，与线速度/线性阻尼同处，调用点不写常数。

### 3.4 惯性张量：从接触探针的 AABB 算

飞船没有物理体（`SimulatePhysics = false`），
`FBodyInstance::GetBodyInertiaTensor()` 不可用。用**接触探针的 AABB** 当长方体：
探针就是 Hull 凸包的顶点，AABB 略小于真实包围盒，误差在船体尺寸的几个百分点内。

```
a, b, c = AABB 三条边的**全长**（AU）
m       = 求解器质量（M☉）
Ix = m/12·(b² + c²)      Iy = m/12·(a² + c²)      Iz = m/12·(a² + b²)
InverseInertiaLocal = (1/Ix, 1/Iy, 1/Iz)
```

没有探针（Hull 没导 Simple Collision）时退回球体惯性 `I = 2/5·m·r²`，三个分量相同。

Hull 的轴心在船体几何中心（见 `spaceship-hull-bounds-pivot-at-center` 的实测），
与「Actor 原点 = 重心」这个假设一致。惯性按**本体系对角**存，不存完整矩阵 ——
长方体主轴就是机体轴，非对角项理论为零。

逆惯性作用到世界系向量时做两次旋转：

```
I⁻¹_world(v) = q ⊗ (InverseInertiaLocal ⊙ (q⁻¹ ⊗ v))
```

### 3.5 接触求解：从「最深一点平移」改成「多接触点顺序冲量」

`ResolveProbeContact` 重写。**球-球分支完全不动**：行星之间不会互撞，限制作用面。

#### 收集接触点

遍历 `ProbeBody.ContactProbes`，逐个算穿透（沿用逐探针地表半径的判定），
得到：

```cpp
struct FContactPoint
{
    FVector3d Offset;   // 探针相对船心的位置（AU）= ContactProbes[i]
    FVector3d Normal;   // 球心 → 探针，归一化
    double    Depth;    // > 0
    double    LambdaN;  // 本子步累计法向冲量，摩擦锥用
};
```

按 `Depth` 降序，**最多取 16 个**。上限的理由：机翼 + 机身的接触用不了更多，
而每个点每轮迭代都有成本，且大量同时接触是「静置抖动」的主要来源。

#### 顺序冲量

`ContactIterations = 4` 轮。每轮、每个接触点：

```
接触点速度：   v_c = v + ω × r                  （球方若 bRigidBody 再加 ω_s × r_s）
法向速度：     v_n = v_c · n
法向有效质量： m_n = 1 / (1/m + n · [I⁻¹(r × n) × r])
法向增量：     Δλ = −(v_n + e · v_n⁰) / m_n      （v_n⁰ 是迭代开始前的法向速度）
               Δλ > 0 才施加（只推不拉）；λ_n += Δλ
切向：         v_t = v_c − v_n·n
切向增量：     Δλ_t = −|v_t| / m_t，夹到 [−μ·λ_n, +μ·λ_n]
冲量：         J = Δλ·n + Δλ_t·(−v_t/|v_t|)
施加：         v += J/m
               ω += I⁻¹(r × J)                 （仅 bRigidBody）
```

`e = 0` 时 `Δλ = −v_n / m_n`，即「把接近速度归零」。`e` 取 `ProbeBody.ContactRestitution`。

球方（行星）默认 `bRigidBody == false`，所以它的 `ω` 不变、角项为零；代码里仍写成
通用形式，只要 `bRigidBody` 为真就参与，这样以后行星之间要接触也能用。

切向的速度衰减由摩擦自然给出 —— **「落地后横向速度停住」不再需要
`bZeroVelocityOnContact`**。飞船在 `AddBodyToSimulator` 里不再置那个标志；
字段本身保留（求解器里还有兜底分支），注释改为「已被摩擦接触取代」。

#### 位置修正

冲量解完后做一次，语义与现在相同：取**所有接触点里的最大穿透** `MaxDepth`，
按 `ComputeContactShares` 的原比例推出地表：

```
ProbeBody.Position  += n_of_max · (MaxDepth · ShareProbe)
SphereBody.Position -= n_of_max · (MaxDepth · ShareSphere)
```

仍然**不留穿透余量**（余量会让船每步往下陷一点，长期看就是地表在漏）。
逐点分别推会过度推开：多个探针同时接触时，每个都推一次等于把船推出数倍穿透量。

### 3.6 飞船侧：输入从「直接转 Actor」变成「角加速度」

`Tick` 里的 `AddActorLocalRotation` 整段删除。`AutoLevelSpeed` 的回正代码路径删除
（属性保留、注释标为失效；删属性会让蓝图与关卡实例里存的值丢失）。

`GetAutoLevelUp()` **保留**：落地检测半径（`GetLandingQueryRadiusCm`）用它的反向当
重力方向，与自动回正是两个用途。

新增两个方法：

```cpp
/** 本帧要施加的世界系角加速度（rad/s²）。自由视角时返回零，并丢弃待发冲量。 */
FVector GetAngularAccelerationWorldRadPerS2(float DeltaSeconds);

/** 子系统每帧写回的实际角速度（世界系 rad/s），与 SetCosmosVelocity 对称。 */
void SetCosmosAngularVelocity(const FVector& InWorldRadPerSecond);
```

#### 鼠标：角加速度冲量

`Look(DeltaYaw, DeltaPitch)` 不再改 `YawRate/PitchRate` 后由 Tick 转 Actor，而是把增量
累进 `PendingAngularImpulseLocal`（机体本地，rad/s），同样的
`MouseImpulseScale` 与 `MaxYawRate/MaxPitchRate` 上限。子系统每帧读一次并**清零**：

```
α_world = R(q) · PendingAngularImpulseLocal / DeltaSeconds
```

#### A/D 横滚：持续角加速度，到顶衰减

```
ω_roll      = 实际角速度在机体 X 轴上的分量（来自 SetCosmosAngularVelocity）
f           = clamp(1 − |ω_roll| / MaxRollRate, 0, 1)
α_roll      = RollInput · RollAngularAcceleration · f
```

`RollAngularAcceleration` 新属性，默认 `MaxRollRate / 0.08`（约 1500°/s²），
即 80 毫秒到顶。

#### 手感守恒

旧版：鼠标把 `YawRate` 一次推到 90°/s，Tick 每帧 `AddActorLocalRotation`，同时
`FInterpTo(YawRate, 0, dt, AngularDamping)` 指数衰减。总转角 = `90 / 3.5 ≈ 25.7°`。

新版：同一个 90°/s 一次性注入求解器的 ω，求解器的角阻尼取
`AngularDamping / TimeScale`（同一个 3.5/s），指数衰减的积分结果相同 ——
**总转角与衰减时间常数都不变，不需要重调手感参数**。

#### 自由视角

语义从「落地后飞船整体冻结」改成「**落地后不接管飞船**」：

- 不施加角加速度，鼠标全部给相机；
- 飞船自己的残余角速度按 `AngularDamping` 衰减，接触与摩擦继续生效；
- 相机环绕逻辑不变。

表现上仍是「落地后鼠标环绕看船」，但船不再是绝对静止 —— 这是纯物理姿态的必然结果。

### 3.7 子系统接线与写回

**`AddBodyToSimulator`**（飞船分支）：

```cpp
State.bRigidBody  = true;
State.Orientation = FQuat4d(BodyActor->GetActorQuat());  // 初态 = 关卡摆放
// State.bZeroVelocityOnContact = true;  ← 删除
```

朝向与位置同源：关卡里怎么摆，运行后就从什么姿态开始，与
「位置取自摆放」完全一致。

**`RefreshShipContactProbes`**：

- 探针旋转**仍读 `Ship->GetActorTransform()`**（含上一帧写回的朝向），不直接读
  `State.Orientation`。稳态下两者相等（写回就是写 Orientation），差别只有 float 精度
  与一帧延迟，远小于一个探针间距。缩放同样从 Actor 取。
- 顺手写惯性：探针 AABB → 长方体公式 → `State.InverseInertiaLocal`；
  无探针时球体惯性。每帧重算，成本是探针数的 min/max（与已有的旋转遍历同阶）。

**`ApplyControlledBodyForces`**：

```cpp
Body.ExternalAngularAcceleration = FVector3d(
    Ship->GetAngularAccelerationWorldRadPerS2(DeltaSeconds)) / (TimeScale * TimeScale);

Body.AngularDamping = double(ShipAngularDampingPerSecond) / TimeScale;
```

`ShipAngularDampingPerSecond` 取飞船现有的 `AngularDamping`（3.5/s）。

**写回**：位置与朝向必须同一次写，否则组件变换更新两次：

```cpp
if (State.bRigidBody)
{
    BodyActor->SetActorTransform(
        FTransform(FQuat(State.Orientation),
                   Simulator.ToWorldLocation(State.Position),
                   BodyActor->GetActorScale3D()),
        /*bSweep=*/false, nullptr, ETeleportType::None);
}
else
{
    // 非刚体天体仍只写位置。否则 Identity 的 Orientation 会把行星的摆放朝向
    // 瞬间抹成零 —— 它们的朝向归关卡摆位，不归求解器。
    BodyActor->SetActorLocation(Simulator.ToWorldLocation(State.Position), /*bSweep=*/false);
}
```

`CarryRiders` 用的是位置差量，不受朝向影响。
`SetCosmosAngularVelocity` 实际在**本函数里、写 `ExternalAngularAcceleration` 之前**调用，
拿到的是本帧步进**之前**的角速度 —— 它的唯一读者就是紧跟着的
`GetAngularAccelerationWorldRadPerS2`，功能等价，且省掉一次跨帧时序依赖。

### 3.8 数据流一帧

```
Pawn::Tick        Look()/SetRoll() → PendingAngularImpulseLocal、RollInput
                  （不再写 Actor 朝向）
        ↓
Subsystem::Tick   ① RefreshPlanetContactRadii
                  ② RefreshShipContactProbes   ← 用 State.Orientation 转探针 + 写惯性
                  ③ ApplyControlledBodyForces  ← 读待发冲量（清零）、写 α 与角阻尼
                  ④ 200 × StepExact           ← 姿态积分 → 接触冲量修正 ω
                  ⑤ 写回：SetActorTransform(位置+朝向) + SetCosmosVelocity
                            + SetCosmosAngularVelocity
```

---

## 4. 边界与兜底

| 情形 | 处理 | 理由 |
|---|---|---|
| `bRigidBody == false` | 新增字段全部忽略 | 行星、恒星、现有测试的行为逐位不变 |
| `InverseInertiaLocal` 为零（没探针且半径也为 0） | 只做线速度冲量，`ω` 不变 | 除零保护；与「不响应力矩」语义一致 |
| 接触点超过 16 个 | 只取最深的 16 个 | 防爆；机翼+机身用不了更多 |
| 探针正好落在球心（方向无定义） | 跳过该点 | 法线无意义，位置修正也无方向 |
| 飞船没有探针（Hull 没导 Simple Collision） | 走球-球分支，只平移不旋转 | 兜底球的接触点永远在球心连线上，`r × n == 0`，本来就没有力矩 |
| 逐探针地表半径数量对不上 | 退回球面（现有逻辑） | 与 `2026-10-31` 的设计一致 |
| 飞船质量或惯性非有限 | **注册期就挡掉**：`AddBodyToSimulator` 用 `!IsFinite(MassEarth)` 拒绝注册；惯量助手对非法边长/质量返回零；NaN 探针在接触收集期跳过。因此不在每帧热路径再加一层守卫 | 可达路径为零；每帧每天体多一次 `IsFinite` 不买任何东西 |
| 自由视角激活 | 丢弃待发冲量，`α = 0` | 鼠标归相机 |
| 高帧率（`DeltaSeconds` 很小） | `α = impulse / Δt` 自动变大 | 乘积 `α·Δt` 与帧率无关，注入的 ω 恒定 |

---

## 5. 文件清单

| 文件 | 改动 |
|---|---|
| `Source/StellarEraCosmos/Src/NBody/NBodySimulator.h` | `FCosmosBodyState` 加字段；姿态积分与冲量辅助函数声明 |
| `Source/StellarEraCosmos/Src/NBody/NBodySimulator.cpp` | `StepExact` 加姿态积分；`ResolveProbeContact` 重写为多接触点顺序冲量 |
| `Source/StellarEraCosmos/Src/CosmosSimulationSubsystem.cpp` | `AddBodyToSimulator` 初始化刚体字段；`RefreshShipContactProbes` 用求解器朝向 + 写惯性；`ApplyControlledBodyForces` 写角加速度与角阻尼；写回改用 `SetActorTransform` |
| `Source/StellarEraPawns/Src/Spaceship/SpaceshipPawn.h/.cpp` | 删 `AddActorLocalRotation` 与自动回正；新增 `PendingAngularImpulseLocal`、`SetCosmosAngularVelocity`、`GetAngularAccelerationWorldRadPerS2`、`RollAngularAcceleration` |
| `Source/StellarEraCosmos/Src/Tests/RigidBodyContactTests.cpp` | 新增：求解器刚体与接触用例 |
| `Source/StellarEraPawns/Src/Tests/SpaceshipAngularInputTests.cpp` | 新增：角加速度输入的纯函数用例 |

模块依赖不变：`StellarEraCosmos → StellarEraPawns` 已存在（子系统本来就在 Cast 飞船）。

---

## 6. 测试

### 6.1 求解器层（`StellarEra.Cosmos.RigidBody.*`）

直接构造 `FCosmosBodyState`，不碰世界，无头可跑：

| 测试 | 断言 |
|---|---|
| `AngularAccelerationIntegrates` | 恒定 α 跑一步，`ω == α·dt`，误差 < 1e-12 |
| `OrientationStaysUnit` | 跑 1000 步后 `\|q\| − 1 < 1e-12`；角速度很大时也不漂 |
| `ProbeContactProducesTorque` | 单个偏心探针穿透 → ω 非零，方向与 `r × n` 同向 |
| `CenteredProbeProducesNoTorque` | 探针在球心连线上 → ω 恒零（回归：不能凭空转） |
| `FrictionDampsTangential` | 切向初速 + 法向接触 → `\|v_t\|` 单调下降，`v_n → 0` |
| `RestingShipSettles` | 4 个探针、小初速跑 N 步 → `\|v\|`、`\|ω\|` 低于阈值，且无残余穿透 |
| `NonRigidBodyIgnoresAngularState` | `bRigidBody = false` + 非零 ω → 朝向与 ω 均不变 |
| `InertiaFromBoxMatchesFormula` | AABB 已知的探针集 → 逆惯性等于 `12/(m·(b²+c²))` |
| 现有能量漂移测试 | 不受影响（造的都是点质量、`bRigidBody = false`） |

### 6.2 Pawn 层（`StellarEra.Spaceship.AngularInput.*`）

命名前缀跟着现有 Pawn 测试走（`StellarEra.Spaceship.*`），不用 `Pawns`。

纯函数，不建世界：

- 零 ω + 已知待发冲量 → `GetAngularAccelerationWorldRadPerS2` 返回 `impulse / Δt`
- 第二次调用返回零（冲量已清零）
- 自由视角激活 → 返回零
- 横滚到顶（`|ω_roll| == MaxRollRate`）→ 横滚分量衰减到 0，其它分量不受影响

### 6.3 人工 PIE 验收（`UniverseMap`）

1. 低空俯冲让**一侧机翼尖**先触地 → 飞船绕接触点翻滚、倒向地面，然后停稳
2. 平地正落 → 不弹跳、不抖动、不穿地
3. 撞击后按 A/D + W 能自救扶正（无自动回正）
4. 落地后鼠标环绕看船，船不再被鼠标转动
5. 起飞后鼠标手感与改动前一致（总转角约 25°，衰减约 0.3 秒）

无头测试建不起真实体素世界（`AVoxelPlanetActor` 需要带 Voxel LOD 管理器的
`UWorld`），所以第 6.3 节不假装被自动化覆盖。

### 6.4 验证命令

```bash
# 构建（编辑器必须关着）
"D:/Application/Epic Games/UE_5.8/Engine/Binaries/ThirdParty/DotNet/10.0/win-x64/dotnet.exe" \
  "D:/Application/Epic Games/UE_5.8/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.dll" \
  StellarEraEditor Win64 Development \
  -Project="F:/workspace/ue/StellarEra/StellarEra.uproject" -WaitMutex -FromMsBuild

# 自动化测试
"D:/Application/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" \
  "F:/workspace/ue/StellarEra/StellarEra.uproject" \
  -ExecCmds="Automation RunTests StellarEra" \
  -TestExit="Automation Test Queue Empty" \
  -unattended -nopause -nullrhi -nosplash -stdout -abslog="%TEMP%/stellar_tests.log"
```

---

## 7. 风险与旋钮

1. **静置抖动**（最大风险）。非累计增量冲量 + e = 0 在静置接触上的经典问题。
   旋钮按优先级：`ContactIterations` 4 → 8；接触点上限 16 → 24；角阻尼。
   先按 4 轮实现，PIE 实测再调。若仍抖，升级为累计冲量（每轮保存 λ、允许回滚到非负）。
2. **去掉 `bZeroVelocityOnContact` 后落地瞬间的切向速度**。飞船质量远小于行星
   （`ComputeContactShares` 的注释里给过这个量级），法向冲量在一个子步内就能把
   相对法向速度抹到零；切向由摩擦逐子步衰减。这是**有意**的行为变化，
   也是「机翼触地会滑一下」的来源。
3. **`FQuat4d` 与 `FQuat` 的互转**。UE 的 `TQuat<float>` 有从 `TQuat<double>` 的
   模板构造；若某个版本没有，就手写四个分量，成本一行。
4. **200 子步的成本**。最坏 200 × 16 点 × 4 轮 × 约 60 flops ≈ 77 万次浮点运算/帧，
   相对 200 次 O(N²) 引力可忽略；且只有穿透时才进入接触分支。
5. **外部对 `GetActorQuat()` 的读取**。相机横滚滞后用 `PreviousRotation` 差分，
   写回改成 `SetActorTransform` 后语义不变（每帧仍只写一次朝向）。
6. **`AutoLevelSpeed` 失效**。它默认就是 0；若有人调回正数，不会再回正，
   需要在属性注释里写明。

---

## 8. 不做的事

- **不做凸包 vs 体素真实几何的精确接触**。成本与 200 子步冲突，
  探针 AABB 惯性 + 球面接触的精度足够表现「机翼触地翻滚」。
- **不接管角色**。`APlanetCharacter` 走引擎 CMC，本来就有完整旋转物理。
- **不做自动回正 / 落地辅助**。用户明确选择纯物理。
- **不做多船碰撞**（凸包 vs 凸包）。`ResolveContacts` 里双方都带探针时仍退回
  球-球分支，与现在一致。
- **不改 `UCosmosTrajectoryComponent`**。它只读位置与速度。
- **不引入新资产或存档格式**。新字段只存在于运行期内存。
