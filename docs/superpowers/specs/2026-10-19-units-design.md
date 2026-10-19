# N 体单位统一 — 设计

- **日期**：2026-10-19
- **分支**：`feat/planet-character-spaceship`
- **状态**：设计已确认，待实施
- **相关**：`2026-10-17-nbody-initial-state-from-level.md`（初态 = 摆放位置 + 初速度）、
  `2026-10-18-spaceship-nbody-thrust-design.md`（推力接入求解器）

---

## 1. 背景

同一套 N 体数据，现在有四种单位写法，而且大多没有标注：

| 位置 | 字段 | 现在填什么 |
|---|---|---|
| `UCosmosOrbitComponent` | `MassSolar` | M☉（行星 0.873237，中心恒星 10000） |
| `UCosmosOrbitComponent` | `InitialVelocity` | AU/年（行星 50，刚好是整数） |
| 关卡 Actor | 位置 | UE 厘米（1 AU = 1,000,000 cm） |
| `ASpaceshipPawn` | `ThrustAcceleration` | 厘米/秒²（1.2e7） |
| HUD | 速度 | 厘米/秒 |

结果是：质量要填小数、速度刚好是整数、位置完全看不出 AU、推力是一串科学计数法。
用户明确要求**固定一套人类单位**，不接受「每个字段加单位枚举」，也不接受整体改 SI。

## 2. 目标与非目标

**目标**

1. N 体输入边界统一为：质量 **地球质量 M⊕**、速度 **km/s**、距离/软化 **AU**、时间 **年**。
2. 换算集中在一个地方，调用点不许手写常数。
3. 已有 4 个天体与 BP 默认值**无损迁移**：物理轨道完全不变。
4. 编辑器里能直接看到「位置是多少 AU」「当前速度是多少 km/s」「质量折合多少 M☉」。
5. 文档里写清「物理速度」与「世界速度」的区别，避免新单位造成新的误解。

**非目标**

- 不改 UE 世界坐标（仍是厘米）。体素、相机、碰撞、星球半径都不动。
- 不改 `FNBodySimulator` 内部单位（仍是 M☉ / AU / AU每年）。
- 不改 `CentimetersPerAu` 与 `TimeScale` 这两个世界标定值。
- 不做单位枚举、不做运行时切换、不做 SI 输入。
- 不改星图相机距离、大气参数、体素参数。

## 3. 单位契约

### 3.1 四层

| 层 | 单位 | 谁用 | 例子 |
|---|---|---|---|
| **设计层**（编辑器手填） | 质量 M⊕、速度 km/s、距离/软化 AU、时间 年 | 策划/用户 | 行星 290740.84 M⊕、初速 237.02 km/s |
| **求解器层**（Core 内部） | M☉、AU、AU每年、AU每年² | `FNBodySimulator` | 质量 0.873237 M☉、初速 50 AU/年 |
| **世界层**（UE） | 厘米、秒、度 | Actor 位置、体素、相机、碰撞 | 1 AU = 1,000,000 cm |
| **换算层** | 只允许出现在 `CosmosTypes.h` 的 `CosmosUnits::` | 两个边界 | 1 AU/年 = 4.7404704635 km/s |

规则：

- 设计层字段**只被两个入口消费**：`UCosmosSimulationSubsystem::AddBodyToSimulator`
  与 `UCosmosTrajectoryComponent::DrawInEditor`。其他代码一律读求解器状态。
- 求解器层永远不出现 M⊕、km/s、厘米。
- 世界层永远不出现 M☉、AU/年、km/s（HUD 的 km/s 是换算后的显示值，不是存储值）。

### 3.2 物理速度与世界速度

这两个量**不相等**，必须分开命名：

```
物理速度（设计层 / 求解器层）
    1 AU/年 = 1.495978707e8 km / 3.15576e7 s = 4.7404704635 km/s
    行星 50 AU/年 = 237.02 km/s

世界速度（HUD 显示）
    1 AU/年 = CentimetersPerAu × TimeScale 厘米/秒
            = 1e6 × 0.00333 = 3330 厘米/秒
    行星 50 AU/年 = 166,500 厘米/秒 = 1.665 km/s
```

原因：世界把 1 AU 压成 10 公里（`CentimetersPerAu = 1e6`），又把 1 模拟年压成
约 300 真实秒（`TimeScale = 0.00333`）。因此同一颗行星在求解器里是 237 km/s，
在游戏世界里看起来是 1.665 km/s。两个数都正确，只是两个不同的量。

- 编辑器里的 `InitialVelocityKmPerSecond` 是**物理速度**，决定轨道形状。
- HUD 的「世界速度」是**游戏时间**下的速度，标签必须写明「世界」。
- 飞船推力本来就是游戏手感值（`120 km/s²` 世界加速度），继续按世界层换算进求解器。

## 4. 数据模型

### 4.1 新字段

`UCosmosOrbitComponent`：

| 字段 | 类型 | 默认 | 单位 | 说明 |
|---|---|---|---|---|
| `MassEarth` | `double` | `1.0` | M⊕ | 质量。旧默认恰好是地球质量，语义不变 |
| `InitialVelocityKmPerSecond` | `FVector` | `(0,0,0)` | km/s | 初速度（物理速度） |
| `bExertsGravity` | `bool` | `true` | — | 上一轮的单向引力开关，本轮不动 |

`ASpaceshipPawn`：

| 字段 | 类型 | 默认 | 单位 |
|---|---|---|---|
| `ThrustAccelerationKmPerS2` | `float` | `120.0f` | km/s² |

`FSpaceshipStatus`：

| 字段 | 类型 | 单位 |
|---|---|---|
| `SpeedKmPerSecond` | `float` | 世界 km/s |
| `MaxSpeedKmPerSecond` | `float` | 世界 km/s |

### 4.2 旧字段

`MassSolar` 与 `InitialVelocity` **保留序列化**，但：

- 去掉 `EditAnywhere`，不再出现在细节面板；
- 代码不再消费；
- 只由 `PostLoad` 的迁移逻辑读取；
- 迁移后清零。

保留而不立即删除，是为了让尚未保存的旧资产、以及可能引用过它们的蓝图
仍能正常加载。等所有资产保存过一轮、确认没有蓝图引用后，再单独提交删除。

### 4.3 迁移值（UniverseMap，2026-10-19 实测）

| 天体 | 旧 `MassSolar` (M☉) | 新 `MassEarth` (M⊕) | 旧 `InitialVelocity` (AU/年) | 新 `InitialVelocityKmPerSecond` (km/s) |
|---|---|---|---|---|
| BP_VoxelPlanet（A） | 0.873237 | 290740.8393297756 | (0, 50, 0) | (0, 237.0235231766674, 0) |
| BP_VoxelPlanet2（B） | 0.873237 | 290740.8393297756 | (0, -50, 0) | (0, -237.0235231766674, 0) |
| BP_VoxelPlanet3（中心） | 10000 | 3329460837.433315 | (0, 0, 0) | (0, 0, 0) |
| BP_SpaceshipPawn | 1e-06 | 0.33294608374333146 | (0, 0, 0) | (0, 0, 0) |

飞船的 `MassEarth / 行星 MassEarth` 与旧 `1e-6 / 0.873237` 完全相同，
所以接触解算的让路比例、动量诊断的相对关系都不变。

## 5. 换算层

### 5.1 常量（`Source/StellarEraCore/Public/CosmosTypes.h`）

```cpp
inline constexpr double AuToKilometer = AuToCentimeter / 1.0e5;              // 1.495978707e8
inline constexpr double KilometerPerSecondPerAuPerYear =
    AuToKilometer / YearToSecond;                                            // 4.740470463533348
inline constexpr double AuPerYearPerKilometerPerSecond =
    1.0 / KilometerPerSecondPerAuPerYear;                                    // 0.21094952656969873
inline constexpr double SolarMassInEarthMasses =
    1.0 / EarthMass;                                                         // 332946.0837433315
```

常量与函数**不重名**：常量用 `A per B` 的比率命名，函数用 `XToY(FVector3d)` 命名。

### 5.2 函数

物理单位 ↔ 求解器单位：

```cpp
double EarthMassToSolarMass(double M);
double SolarMassToEarthMass(double M);
FVector3d AuPerYearToKilometerPerSecond(const FVector3d& V);
FVector3d KilometerPerSecondToAuPerYear(const FVector3d& V);
```

UE5 的 `FVector` 就是 `FVector3d`（LWC 双精度），组件上的 `FVector` 字段可直接传入。

求解器单位 ↔ 世界单位（**含游戏时钟**，必须显式传标定值）：

```cpp
double GameVelocityToWorldCmPerSecond(double V, double CmPerAu, double TimeScale);
double WorldCmPerSecondToGameVelocity(double V, double CmPerAu, double TimeScale);
double GameAccelerationToWorldCmPerS2(double A, double CmPerAu, double TimeScale);
double WorldCmPerS2ToGameAcceleration(double A, double CmPerAu, double TimeScale);
```

后四个函数的名字里带 `WorldCm`，与物理换算函数明确区分。任何调用点都不允许
再手写 `* CentimetersPerAu * TimeScale`。

### 5.3 边界调用点

| 调用点 | 换算 |
|---|---|
| `AddBodyToSimulator` | `Mass = EarthMassToSolarMass(MassEarth)`；`Velocity = KilometerPerSecondToAuPerYear(InitialVelocityKmPerSecond)` |
| `FinalizeRegistration` 排序 | 按 `MassEarth` 降序（与旧 `MassSolar` 排序等价） |
| `DrawInEditor` | 与 `AddBodyToSimulator` 同一套换算 |
| `ApplyControlledBodyForces` | `WorldCmPerS2ToGameAcceleration(ThrustWorldCmPerS2, CmPerAu, TimeScale)` |
| `ASpaceshipPawn::GetThrustAccelerationWorldCmPerS2` | `ThrustAccelerationKmPerS2 * 1e5` |
| HUD | `GetVelocity().Size() / 1e5` → 世界 km/s |

注册时增加保护：`MassEarth <= 0`（含 NaN）直接报错并跳过该天体，不静默按 0 质量跑。

## 6. 迁移

### 6.1 `PostLoad`

```cpp
void UCosmosOrbitComponent::PostLoad()
{
    Super::PostLoad();

    // 旧字段非零 = 这份数据存于单位变更之前。
    if (MassSolar > 0.0)
    {
        // 新字段仍是构造默认值 → 没迁移过。
        if (MassEarth == 1.0)
        {
            MassEarth = CosmosUnits::SolarMassToEarthMass(MassSolar);
        }
        MassSolar = 0.0;
    }

    if (!InitialVelocity.IsZero())
    {
        if (InitialVelocityKmPerSecond.IsZero())
        {
            InitialVelocityKmPerSecond =
                CosmosUnits::AuPerYearToKilometerPerSecond(InitialVelocity);
        }
        InitialVelocity = FVector::ZeroVector;
    }
}
```

### 6.2 幂等性

| 情况 | 行为 |
|---|---|
| 旧资产，`MassSolar` 被覆盖 | `MassEarth == 1.0` → 换算一次 |
| 旧资产，`MassSolar` 用默认值 | 新默认 `MassSolar = 0` → 不换算；`MassEarth = 1.0` 正好是旧默认地球质量 |
| 已迁移，尚未保存 | 每次加载 `MassEarth` 从 1.0 开始 → 再算一次，结果相同 |
| 已迁移，已保存 | `MassSolar = 0`、`MassEarth != 1.0`（地球质量除外）→ 不换算 |
| 旧初速非零 | 新初速为零 → 换算一次；旧初速清零 |
| 已迁移初速非零 | 新初速非零 → 不换算 |

已知边界：迁移后用户**故意**把质量设回 1.0 或把初速设回 0，而旧字段因保存
未落盘仍非零时，下一次加载会按旧值再换算一次。处理方式：构建后立即保存关卡
与 BP，并用 MCP 读回确认旧字段已清零（见 §9）。

### 6.3 落盘

`PostLoad` 只改内存。必须在构建后打开编辑器、让资产加载一次，然后保存：

1. `UniverseMap`（4 个天体所在关卡）；
2. `BP_VoxelPlanet` / `BP_SpaceshipPawn`（若其 CDO 带 Orbit 组件）；
3. 确认 `MassSolar`、`InitialVelocity` 在资产里变成 0。

## 7. 显示层

### 7.1 属性标签

所有宇宙字段补 `DisplayName` / `ToolTip`：

| 字段 | DisplayName |
|---|---|
| `MassEarth` | 质量（地球质量 M⊕） |
| `InitialVelocityKmPerSecond` | 初速度（km/s，物理速度） |
| `SofteningLength` | 引力软化长度（AU） |
| `CentimetersPerAu` | 世界标定（厘米/AU） |
| `TimeScale` | 时间流速（模拟年/真实秒） |
| `PredictionYears` / `TrailYears` | 预测窗口 / 拖尾时长（年） |
| `ThrustAccelerationKmPerS2` | 推力加速度（km/s²，世界） |
| `DampingTime` | 阻尼时间（秒） |

### 7.2 只读回显

`UCosmosOrbitComponent` 增加三个只读、瞬态字段：

| 字段 | 单位 | 来源 |
|---|---|---|
| `PositionAu` | AU | `Owner->GetActorLocation() / CentimetersPerAu` |
| `SolverVelocityKmPerSecond` | km/s（物理换算） | 已注册：求解器当前速度；未注册：`InitialVelocityKmPerSecond` |
| `MassSolarReadout` | M☉ | `MassEarth * CosmosUnits::EarthMass` |

实现方式：组件开一个 `bTickInEditor = true` 的**纯显示 Tick**
（`TickGroup = TG_PostPhysics`）。它不碰求解器、不写物理、不改变「N 体只有一个驱动」
这条约束 —— 该约束针对的是物理推进。取不到 Cosmos 子系统时保持上一次的值，
不猜标定值。

### 7.3 HUD

- `FSpaceshipStatus::SpeedKmPerSecond = GetVelocity().Size() / 1e5`。
- `MaxSpeedKmPerSecond = ThrustAccelerationKmPerS2 × (bBoosting ? BoostMultiplier : 1) × DampingTime`；
  巡航（阻尼为 0）时仍为 0，表示「无上限」。
- 文本改为 `速度 %.2f / %.2f km/s（世界速度）`。
- 新增一行说明或 ToolTip：编辑器里的 `InitialVelocity` 是物理 km/s，HUD 是世界 km/s。

## 8. 影响面

| 模块 / 文件 | 改动 |
|---|---|
| `StellarEraCore/Public/CosmosTypes.h` | 新增常量与换算函数 |
| `StellarEraCore/Private/NBodySimulatorTests.cpp` | 新增 `StellarEra.Core.Units` |
| `StellarEra/Public/CosmosOrbitComponent.h` | 新字段、旧字段、只读回显、Tick 覆盖 |
| `StellarEra/Private/CosmosOrbitComponent.cpp` | 构造默认值、`PostLoad`、`TickComponent` |
| `StellarEra/Private/CosmosSimulationSubsystem.cpp` | 边界换算、排序、推力换算、注释 |
| `StellarEra/Public/CosmosSimulationSubsystem.h` | 注释与单位说明 |
| `StellarEra/Private/CosmosTrajectoryComponent.cpp` | `DrawInEditor` 边界换算 |
| `StellarEra/Public/CosmosTrajectoryComponent.h` | 注释 |
| `StellarEra/Private/CosmosTrajectoryComponentTests.cpp` | 构造参数改新单位 |
| `StellarEraVehicle/Public/SpaceshipPawn.h` | `ThrustAccelerationKmPerS2`、状态字段、注释 |
| `StellarEraVehicle/Private/SpaceshipPawn.cpp` | `GetStatus`、推力 getter、注释 |
| `StellarEra/Private/StellarEraHUD.cpp` | km/s 文本 |
| `CLAUDE.md` | 增加「单位约定」一节 |
| `Content/Maps/UniverseMap` + BP | 迁移后保存 |

**模块依赖不新增**：`StellarEraVehicle` 不引入 `StellarEraCore`，推力用本地
`1e5` 厘米/千米换算；其余换算都在已依赖 Core 的 `StellarEra` 里。

## 9. 验收

1. **构建**：关闭编辑器后用捆绑 dotnet 直调 UBT，`StellarEraEditor Win64 Development` 成功。
2. **自动化测试**：
   - `StellarEra.Core.Units` 通过；
   - `StellarEra.Core.*` 全部回归；
   - `StellarEra.Trajectory.LineBatchNoAccumulation` 回归；
   - `StellarEra.*` 全部回归。
3. **迁移核对（MCP）**：
   - 4 个天体的 `MassEarth` / `InitialVelocityKmPerSecond` 与 §4.3 表一致；
   - `MassSolar` / `InitialVelocity` 读回为 0；
   - 换算回去的求解器初速为 A/B `±50 AU/年`。
4. **物理等价**：PIE 短跑，行星轨迹与迁移前一致；不做长时间运行（三体
   t ≈ 0.84 年爆炸是已知问题，与本轮无关）。
5. **上一轮收尾**：飞船关卡实例的 `bExertsGravity = false` 已设置并保存。
6. **文档**：`CLAUDE.md` 的单位约定与实现一致。

## 10. 风险

| 风险 | 处理 |
|---|---|
| `PostLoad` 迁移未落盘，下次加载重复换算 | 新字段非默认值 + 旧字段清零，双重保护；构建后立即保存并用 MCP 读回 |
| 蓝图仍引用旧字段 | 旧字段保留序列化与 `BlueprintReadWrite`，不删除；只去掉 `EditAnywhere` |
| 恒星质量 3.329e9 M⊕ 难以阅读 | 回显 `MassSolarReadout`（M☉），工具提示给出换算 |
| 「物理速度」与「世界速度」混淆 | 字段名、HUD 标签、`CLAUDE.md` 三处都写明；HUD 标「世界速度」 |
| `MassEarth == 1.0` 精度比较 | 构造默认值与换算常量同源；地球质量重复换算结果仍是 1.0 |
| 保存资产会改动 LFS 二进制 | 只保存确实被迁移的关卡/BP；提交时按 pathspec，避免吞入其他暂存文件 |

## 11. 决策记录

| 决策 | 结论 |
|---|---|
| 输入方案 | 固定一套人类单位，不做单位枚举 |
| 质量单位 | 地球质量 M⊕（接受恒星为 3.329e9 M⊕） |
| 速度单位 | km/s（物理速度） |
| 距离/软化/时间 | AU / AU / 年，值不变，只补标注 |
| 飞船推力 | 改为 km/s²（120），HUD 显示世界 km/s |
| 位置回显 | 轨道组件加纯显示 Tick，只读显示 AU / km/s / M☉ |
| 迁移方式 | `PostLoad` 幂等迁移 + 构建后保存并核对 |
| 旧字段 | 保留序列化，代码不再消费；后续单独删除 |
