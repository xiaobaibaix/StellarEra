# 飞船接触跟着体素地形走 — 设计

- **日期**：2026-10-31
- **项目**：StellarEra（Unreal Engine 5.8）
- **模块**：`StellarEraCosmos`（求解器 + 子系统）、`StellarEraPlanet`（行星地形查询）
- **状态**：设计已确认，待实现
- **前置**：`2026-10-18-spaceship-nbody-thrust-design.md`（飞船推力交给求解器）、
  `2026-10-29-progressive-dig-design.md`（分块挖掘）

---

## 1. 目标

用户原话：

> 碰撞体为什么没有一起更新？飞船的？？

澄清后的症状：

> 飞船，没有落到被挖掘以后的坑里面，反而是停在空气上

目标：**飞船的着陆面必须是挖过之后的真实体素地表**，不是生成器的理想地表。
范围（用户选定）：**只修飞船接触**。角色（`APlanetCharacter`）走 CMC 物理通道，
不在此次范围内；行星之间的接触也不改。

---

## 2. 现状：为什么停在空气上

证据链，四环：

1. **飞船没有物理、没有移动组件**。`ASpaceshipPawn` 只有一个静态网格 `Hull`，
   位移由 `UCosmosSimulationSubsystem::Tick` 每帧
   `BodyActor->SetActorLocation(..., /*bSweep=*/false)` 直接写
   （`CosmosSimulationSubsystem.cpp:534`），没有任何 sweep/碰撞询问。
2. **着陆由求解器解算**。`FNBodySimulator::ResolveProbeContact`
   （`NBodySimulator.cpp:325`）把飞船的凸包探针逐个当点、与行星当**球**求穿透，
   取最深的一个沿径向外推。`ResolveContacts` 在 `StepExact` 里每个子步调用一次。
3. **那个球的半径是解析值**。`RefreshPlanetContactRadii`
   （`CosmosSimulationSubsystem.cpp:160`）调
   `AVoxelPlanetActor::GetSurfaceRadiusCmAtDirection` → `GetSurfaceRadiusVoxels`
   （`Terrain/VoxelPlanetNoiseTerrain.cpp:112`），后者只有噪声与地质场的不动点迭代，
   **完全不读体素数据**。
4. **只有一个地方读真实体素**：`AVoxelPlanetActor::RaycastSurfaceCm`
   （`VoxelPlanetActor.cpp:1240`）。它从解析命中点出发、按 128 体素分段持读锁
   march，找空/实翻转。挖掘改的是体素数据，所以挖掘对第 3 环完全不可见。

结论：坑挖出来了、网格变了、玩家看得见，但接触解算用的半径一个字节都没变。

另外一条约束决定了方案形状：求解器一帧最多跑 `MaxStepsPerFrame = 200` 个子步
（`CosmosSimulationSubsystem.h:208`），而接触解算在**每个子步**里跑。任何"接触
时现查体素"的做法都会把体素查询放大 200 倍。所以查询必须**每帧一次、查完缓存**。

---

## 3. 设计

### 3.1 行星侧：一个方向 = 一个半径

新增 `AVoxelPlanetActor::GetVoxelSurfaceRadiusCmAtDirection`：

```cpp
/**
 * 真实地表半径：解析半径当种子，沿该方向 march 体素找空/实翻转。
 *
 * QueryMarginCm：探针在该方向上离解析球面超过这个距离时不可能碰到体素地表，
 * 直接返回解析半径，不做体素查询。
 *
 * 找不到翻转（挖穿/整段全实）时退回解析值。没创建（IsCreated() == false）
 * 时同样退回解析值。
 */
float GetVoxelSurfaceRadiusCmAtDirection(
	const FVector& WorldDirection, float QueryMarginCm) const;
```

实现与 `RaycastSurfaceCm` 同构，但**射线是径向线**（起点 = 行星中心）：

| 步骤 | 做法 |
|---|---|
| 解析种子 | `AnalyticRadiusCm = GetSurfaceRadiusCmAtDirection(Dir)`（已有函数，含噪声 + 地质不动点） |
| 体素空间 | 中心在原点、单位 = 体素；`AnalyticRadiusVoxels = AnalyticRadiusCm / VoxelSize` |
| 起点 | `Dir * AnalyticRadiusVoxels` |
| 两个方向的预算 | 向外 `1.25 · BaseRadiusVoxels − AnalyticRadiusVoxels`；向里 `AnalyticRadiusVoxels − 1`。**不复用** `PlanetSurfaceRay::ComputeMarchStepRange`：它的两个上界是相对"调用方给的射线原点在球外"定义的，径向线以球心为原点时代入只是把 Inward/Backward 换个名字，读代码的人会被绕进去 |
| 判决 | 起点空 → 向里找第一格实；起点实 → 向外找第一格空（与 `RaycastSurfaceCm` 同一条规则） |
| 边界 | 翻转处两格的中点，误差 ≤ 1 体素 |
| 读锁 | `FVoxelReadScopeLock(GetData(), 段盒, TEXT("PlanetVoxelSurfaceRadius"))`，每段 128 体素；起点单独取一次小锁。理由与 `RaycastSurfaceCm` 相同：整段取锁会锁住大半棵八叉树 |

步进判决抽成纯函数放进 `PlanetSurfaceRay`（该文件已经是"纯数学、不碰 UObject、
可无头测试"的约定）：

```cpp
/**
 * 沿一条线找空/实翻转。SampleEmptyAt(Step) 抽样，Step 是相对起点的体素步数
 * （正 = 沿查询方向）。本函数先抽 Step 0：起点为空就往负方向走，起点为实
 * 就往正方向走。找到时 OutBoundaryStep = ±(边界步 − 0.5)。
 *
 * 返回 false = 走完预算没翻转（挖穿 / 整段全实），调用方退回解析值。
 */
STELLARERAPLANET_API bool FindSurfaceBoundaryStep(
	int32 MaxForwardSteps, int32 MaxBackwardSteps,
	TFunctionRef<bool(int32 Step)> SampleEmptyAt,
	double& OutBoundaryStep);
```

抽样回调的**锁由调用方持有**：回调内部自己维护"当前 128 体素段"的锁，步数越出
该段时释放重取。步进是单调的，所以一个"当前段"就够了。这样做的好处是：判决
逻辑可无头测试，锁粒度又没有被拉回"整条线一把锁"。

### 3.2 求解器侧：逐探针半径

`FCosmosBodyState` 加两个字段：

```cpp
/**
 * 逐探针的真实地表半径（求解器单位 AU），与 ContactProbes 一一对应。
 *
 * 空数组 / 数量对不上 = 全部按对方的 Radius 球面接触（加这个字段之前的行为）。
 * 由子系统每帧写一次，求解器只读。
 */
TArray<double> ContactProbeSurfaceRadii;

/** 上面这份半径是对着哪个天体（求解器下标）算的。INDEX_NONE = 没有。 */
int32 ContactSurfaceSphereIndex = INDEX_NONE;
```

`SetContactProbes` 顺带清空这两项 —— 探针重建过而半径没重建时，
`ContactProbeSurfaceRadii.Num() != ContactProbes.Num()` 会自动退回球面，
但更早地把索引置 `INDEX_NONE` 能保证语义上不会"用旧世界的半径"。

`ResolveProbeContact` 只加三处：

```cpp
const bool bUsePerProbe =
	ProbeBody.ContactSurfaceSphereIndex == SphereIndex
	&& ProbeBody.ContactProbeSurfaceRadii.Num() == ProbeBody.ContactProbes.Num();

// ① 快速排除用的半径取「球半径」与「逐探针半径的最大值」的较大者。
//    抬高地形时逐探针半径会**大于**球半径，只按球半径排除会把接触整个漏掉。
double RejectRadius = SphereRadius;
if (bUsePerProbe) { for (double R : ...Radii) RejectRadius = FMath::Max(RejectRadius, R); }
if (CenterDistance >= RejectRadius + ProbeBody.ContactProbeRadius) return;

// ② 逐个探针的穿透改用逐探针半径
for (int32 ProbeIndex = 0; ProbeIndex < ProbeBody.ContactProbes.Num(); ++ProbeIndex)
{
	const FVector3d Probe = ProbeBody.ContactProbes[ProbeIndex];
	const double ProbeSurfaceRadius =
		bUsePerProbe ? ProbeBody.ContactProbeSurfaceRadii[ProbeIndex] : SphereRadius;
	const double Penetration = ProbeSurfaceRadius - Distance;
	...
}
```

**不变的部分**：法线仍是"球心 → 探针"的径向；让路比例 `ComputeContactShares`；
`bZeroVelocityOnContact` 的"撞上即停"；默认的法向抹除与切向保留。所有现有
测试应当原样通过 —— 不带逐探针半径时 `ProbeSurfaceRadius == SphereRadius`，
逐字等价于改动前。

### 3.3 数据流与刷新时机

全部落在 `UCosmosSimulationSubsystem::RefreshShipContactProbes`
（`CosmosSimulationSubsystem.cpp:212`），`Tick` 里在 `Advance` **之前**
（`CosmosSimulationSubsystem.cpp:471-472`）调用，**每帧一次**：

1. 现有代码：读 `Ship->GetContactProbesLocalCm()`，用船的变换转到 AU，
   `State.SetContactProbes(...)`（顺带清空逐探针半径与索引）。
2. **粗筛选星**：遍历求解器里 `Radius > 0` 且 `OwnerActor` 是 `AVoxelPlanetActor`
   的天体，取离船心最近的一个；距离 ≥ `1.25 · BaseRadius + ContactProbeRadius`
   时整支跳过。
3. **逐探针**：
   - `Dir = normalize(ProbeAU + 船心AU − 星心AU)`
   - **方向缓存键** = `Round(Dir · BaseRadiusVoxels)`（`BaseRadiusVoxels =
     GetSurfaceRadiusCm() / VoxelSize`）。一格 ≈ 地表 1 体素，船体投影到地表通常
     只有个位数格，所以一帧真正 march 的次数是**个位数**，不是探针数。
   - 未命中缓存 → `Planet->GetVoxelSurfaceRadiusCmAtDirection(Dir, ContactProbeRadiusCm)`，
     写进缓存。
   - 一帧查询次数上限 `MaxSurfaceQueriesPerFrame = 32`：超了剩下的探针用解析半径，
     首次触发打一次 `Warning`，之后 `Verbose`。
4. 写入 `State.ContactProbeSurfaceRadii`（除以 `CentimetersPerAu`）与
   `State.ContactSurfaceSphereIndex = 星球下标`。

缓存是 `RefreshShipContactProbes` 的局部 `TMap`，每帧新建，不跨帧存活 ——
每帧重算正是为了跟上挖掘。

### 3.4 边界与兜底

| 情形 | 处理 | 理由 |
|---|---|---|
| 探针离解析球面超过 `QueryMarginCm` | 不查体素，直接用解析半径 | 没接触就不需要精度；抬高地形见 §7 |
| march 走完全空 / 全实 | 退回解析半径 | 挖穿星心。宁可悬空也不让飞船掉进无底洞；`Verbose` 一行日志 |
| 没有行星在范围内 | 索引 `INDEX_NONE`、半径数组空 | 不清的话会拿上一帧的半径去碰 |
| 探针数量变了 | 数量对不上 → 退回球面 | 现有行为，不需要额外兜底 |
| 两颗星同时在范围内 | 只算最近的那颗；另一颗退回球面 | 逐探针半径一次只对一颗星有效 |
| `IsCreated()` 为假（LOD 管理器没起来） | 退回解析半径 | 体素数据此时不存在 |
| 每帧查询超 32 次 | 剩下的用解析半径 | 防病态地形拖死主线程 |

---

## 4. 单位与坐标

- 行星 API 进出都是**厘米 / 世界方向**，与 `GetSurfaceRadiusCmAtDirection`、
  `RaycastSurfaceCm` 一致。
- 求解器里的半径一律 **AU**；子系统用 `CentimetersPerAu` 换算。
- 方向在两个空间之间**可以原样传递**：AU 世界空间与世界空间只差等比缩放与平移，
  没有旋转（`RefreshPlanetContactRadii` 里已有同一条说明）。
- 探针的"最远距离"用 `State.ContactProbeRadius`（AU）乘回厘米传给行星 API 当查询余量。

---

## 5. 文件清单

| 文件 | 改动 |
|---|---|
| `Source/StellarEraPlanet/Src/Surface/PlanetSurfaceRay.h/.cpp` | 新增纯函数 `FindSurfaceBoundaryStep` |
| `Source/StellarEraPlanet/Src/VoxelPlanetActor.h/.cpp` | 新增 `GetVoxelSurfaceRadiusCmAtDirection` |
| `Source/StellarEraCosmos/Src/NBody/NBodySimulator.h` | `FCosmosBodyState` 加两个字段；`SetContactProbes` 清空它们 |
| `Source/StellarEraCosmos/Src/NBody/NBodySimulator.cpp` | `ResolveProbeContact` 的排除半径与逐探针穿透 |
| `Source/StellarEraCosmos/Src/CosmosSimulationSubsystem.h/.cpp` | `RefreshShipContactProbes` 里算逐探针半径；新增常量 |
| `Source/StellarEraPlanet/Src/Tests/PlanetSurfaceRayTests.cpp` | 步进判决的用例 |
| `Source/StellarEraCosmos/Src/Tests/NBodySimulatorTests.cpp` | 求解器分支与回归用例 |

模块依赖不变：`StellarEraCosmos → StellarEraPlanet` 已存在
（`RefreshPlanetContactRadii` 就在 cast `AVoxelPlanetActor`）。

---

## 6. 测试

### 6.1 纯函数层（`StellarEra.Planet.SurfaceRay.*`）

`FindSurfaceBoundaryStep` 喂假采样器，不碰体素：

- 起点空、往里第 3 格第一次实 → `OutBoundaryStep == -2.5`
- 起点实、往外第 4 格第一次空 → `+3.5`
- 预算走完没翻转 → 返回 `false`
- 单向预算为 1 的边界情形

### 6.2 求解器层（`StellarEra.Cosmos.Contact.*`）

直接构造 `FCosmosBodyState`，不碰世界：

- **最小值复现（挖坑）**：行星半径 100、飞船探针半径全部 60 → 飞船停在 60 而不是 100
- 球半径 100、逐探针 {100, 60, 60} → 最深穿透按 100 算（与改动前同值）
- `ContactSurfaceSphereIndex` 填错 → 退回 100
- 逐探针半径数量与探针数不等 → 退回 100
- 逐探针半径**大于**球半径（抬高地形）+ 船心距 > 球半径 + 探针半径 → 快速排除
  不误杀
- `bZeroVelocityOnContact` / 法向抹除的既有行为回归

### 6.3 人工 PIE 验收（无法自动化）

真实体素世界在无头测试里建不起来 —— `PlanetProgressiveEditTests.cpp:1023` 已经
记过这条（`AVoxelPlanetActor` 需要带 Voxel LOD 管理器的 `UWorld` 才有 `IsCreated()`）。
所以这一条**不假装被测试覆盖**：

1. `UniverseMap` 挖一个坑，飞进坑里 → 落到坑底，HUD「地表高度」与坑底一致
2. 平地着陆 / 起飞 → 行为与改动前一致
3. 贴着坑壁横飞 → 不抖、不被弹到解析球面上

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

## 7. 已知限制

1. **抬高地形有上限**。查询余量是 `ContactProbeRadius`：地形被抬高超过船体尺寸
   时，飞船要先陷到解析球面附近才被顶出来，表现为一次下沉再弹起。挖（降低地表）
   没有这个限制 —— 而挖正是本次要修的场景。后续若需要，放宽这个余量只是一处常量。
2. **帧内快照**。逐探针半径每帧算一次，帧内别的玩家继续挖不会立即反映；下一帧跟上。
   半径按**方向**缓存，所以船在帧内平飞不会让缓存失效。
3. **预测轨迹**复制同一份 `FCosmosBodyState`，所以预测里的接触也用当前快照半径。
   预测本来就是外推，不需要更准。
4. **行星之间的接触不变**，仍用 `RefreshPlanetContactRadii` 的解析球半径。
5. **挖穿（全空）退回解析半径**，飞船会悬在坑口而不是穿过星心。

---

## 8. 不做的事

- 不做"凸包 vs 体素真实几何"的精确接触（成本与 `MaxStepsPerFrame = 200` 冲突）。
- 不动角色：`UPlanetCharacterMovementComponent` 走引擎物理，本来读的就是体素碰撞。
- 不动 `RefreshPlanetContactRadii`：行星对行星不需要体素精度。
- 不引入新的资产或存档格式；`FCosmosBodyState` 只在运行期内存里。
