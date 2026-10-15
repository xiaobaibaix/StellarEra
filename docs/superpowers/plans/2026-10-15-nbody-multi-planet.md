# N 体星系与多星球 实现计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 把参考项目 `VoxelCosmos` 的 N 体引力求解器接进 StellarEra，让关卡里的多颗体素星球真的互相绕转，并且每一颗都能登陆。

**Architecture:** 求解器 `FNBodySimulator` 是纯数学、不依赖 UObject；`UCosmosOrbitComponent` 把「轨道」从「星球」里拆出来，任意 Actor 挂上它就是天体；`UCosmosSimulationSubsystem`（`UTickableWorldSubsystem`，引擎保证单例）是唯一编排者 —— 每帧推进求解器、把位置写回 Actor、同步体素世界变换、把位移施加给骑在星球上的 Pawn。`AVoxelPlanetActor` 加一个「局部坐标模式」，让每帧移动不再触发体素世界重建。

**Tech Stack:** Unreal Engine 5.8、Voxel Plugin Free、C++（无新增插件依赖、无新增模块依赖）

**Spec:** `docs/superpowers/specs/2026-10-15-nbody-multi-planet-design.md`

---

## Global Constraints

以下约束适用于本计划的**每一个任务**，不再逐条重复。

- **语言**：所有新增/修改的源文件注释用**中文**，与现有 `VoxelPlanetActor.h/.cpp` 风格一致。
- **UTF-8 BOM**：本项目**没有** `Directory.Build.props`（已用 `find` 确认全树不存在），无 BOM 的 UTF-8 中文源文件会被 VS IntelliSense 按 GBK 误读并报红波浪线。**每个新建的 `.h` / `.cpp` 写完必须补 BOM**，用 Task 1 Step 2 建的 `add_bom.ps1`。现有源文件均以 `ef bb bf` 开头。
- **第三方插件目录只读**：任何自定义代码都不得写进 `Plugins/VoxelFree/`。只允许在 `Source/StellarEra/` 下新增。
- **不需要改 `StellarEra.Build.cs`**：本计划用到的 `UTickableWorldSubsystem`、`TActorIterator`、`UActorComponent` 全部来自已在依赖表里的 `Engine` / `CoreUObject`。**不要**添加新模块依赖。
- **`Config/DefaultInput.ini` 不得改动**。本计划不涉及任何输入。
- **构建方式**：编辑器**必须关闭**（Live Coding 会拒绝 UObject 布局变更），然后：
  ```bash
  powershell -NoProfile -File "C:/Users/23080/AppData/Local/Temp/ue_mcp/ue_build.ps1"
  ```
  该脚本（已确认存在）用引擎自带 dotnet 直调 `UnrealBuildTool.dll`，绕过 `Build.bat` 的锁问题。期望末尾出现 `Build succeeded` 且退出码为 0。
- **验证方式**：本项目没有测试框架，spec §9 已明确「本次不引入测试框架」（参考项目自己也没有 N 体的测试）。因此**每个任务的验证 = 构建通过 + PIE 里逐条肉眼核对 + `LogDiagnostics` 的数字**，每条验证都写成可证伪的观察点（「看到 X」/「日志里不出现 Y」）。**不要**为了凑 TDD 写无法执行的假测试。
- **命名**：新类不加 `StellarEra` 前缀；文件名与类名一致（`UCosmosOrbitComponent` → `CosmosOrbitComponent.h`）。
- **C4458 是硬错误**：UE 5.8 把「局部变量遮蔽继承成员」当错误。写循环/临时变量时不要用 `Owner`、`World`、`Tick` 之类的继承成员名做局部变量 —— 本项目已在 `PlanetCharacter.cpp:35` 踩过。
- **提交信息**：中文，格式 `<type>: <描述>`，末尾附
  ```
  Co-Authored-By: Claude Code <noreply@anthropic.com>
  ```
- **提交必须带明确路径**：工作区常驻未提交的 `Content/` 改动，不能依赖索引，每次 `git add` 都要写全路径。

---

## 关键引擎事实（写代码时不要推翻它们）

这些是读源码确认过的，计划里的每一处设计都建立在它们之上。

1. **`AVoxelWorld` 是 Actor 子对象，变换不跟 `PlanetRoot` 走。** `CreateDefaultSubobject` 只建出一个实例，它的变换会被序列化。本项目 `VoxelPlanetActor.cpp:149-160` 已有注释与处理（每次重建时同步一次）。局部坐标模式下 Actor 每帧移动，必须**每帧**同步，否则渲染网格留在原地、星球跑掉。

2. **`ComputeParamsHash()` 混了 `ActorLocation`。** `VoxelPlanetActor.cpp:294-297` 明确写了「混原始位置而不是换算后的球心：球心会与 VoxelWorld 的变换相消、退化成常量」。所以 `RebuildPlanet()` 顶部那道守卫（`:137-142`）在 Actor 每帧移动时**永远不成立** → 每帧 `DestroyWorld()` + `CreateWorld()`。这是 Task 2 要修的核心问题。

3. **`GetCenterInVoxels()` 现在算出来已经是零。** 因为 `VoxelWorld` 变换 == Actor 变换（`:156-160`），球心 = 逆变换(Actor位置) = 原点。**算法不用改**，Task 2 只改哈希与同步。

4. **`UTickableWorldSubsystem` 必须实现 `GetStatId()`**，用 `RETURN_QUICK_DECLARE_CYCLE_STAT`。

5. **`UWorldSubsystem::DoesSupportWorldType(const EWorldType::Type) const`** 用来把子系统限制在 PIE / 打包世界。编辑器世界里体素星球走的是 `Toggle()` 预览路径（`VoxelPlanetActor.cpp:224-228`），天体不该动。

6. **`APawn::AddActorWorldOffset(const FVector&, bool bSweep, FHitResult*, ETeleportType)`**。传 `bSweep = false`：Pawn 与星球的位移完全相同、相对位置不变，开着扫掠反而会在穿过地形时被卡住。

7. **`AActor::SetActorLocation(const FVector&, bool bSweep, FHitResult*, ETeleportType)`**。同样传 `bSweep = false`。

8. **`FMath::Sin/Cos/Atan2` 只有 float 重载，double 版在 `<Math/Utils.h>`。** 传 double 给 float 重载会被隐式截断，在 N 体里损失 7 位有效数字。`NBodySimulator.cpp` 必须 `#include "Math/Utils.h"`，且角度转弧度用显式 `(PI / 180.0)` 而不是 `FMath::DegreesToRadians`（后者返回 float）。

9. **访问级别**：`APlanetCharacter::FindNearestPlanet()` 是 `protected`（`PlanetCharacter.h:67`），`bHasLanded` 是 `private`（`:160`），`SnapToPlanetSurface()` 是 `public`（`:55`），`PlanetActor` 是 `public`（`:58`）。`ASpaceshipPawn::FindNearestPlanet()` 是 `private`（`SpaceshipPawn.h:199`），`PlanetActor` 是 **`public`**（`:144`，`private:` 段从 `:175` 才开始）。

10. **`StellarEraGameMode.cpp:141-146`** 的 `for (TActorIterator<AVoxelPlanetActor> It(GetWorld()); It; ++It) { Planet = *It; break; }` 取的是遍历到的第一颗，多星球下结果取决于顺序。

---

## 文件结构

**新建：**

| 文件 | 职责 |
|---|---|
| `Source/StellarEra/Public/CosmosTypes.h` | 单位常量、五个枚举、`FCosmosOrbitInit`。纯类型，无 Actor 依赖 |
| `Source/StellarEra/Public/NBodySimulator.h` | `FCosmosBodyState` + `FNBodySimulator` 声明 |
| `Source/StellarEra/Private/NBodySimulator.cpp` | 求解器实现（引力、Verlet、诊断、AU↔世界坐标换算、轨道根数） |
| `Source/StellarEra/Public/CosmosOrbitComponent.h` | 挂在任意 Actor 上把它变成天体的组件 |
| `Source/StellarEra/Private/CosmosOrbitComponent.cpp` | 组件实现（BeginPlay 自注册 / EndPlay 注销） |
| `Source/StellarEra/Public/CosmosSimulationSubsystem.h` | N 体驱动子系统声明 |
| `Source/StellarEra/Private/CosmosSimulationSubsystem.cpp` | 每帧推进、写回、同步体素世界、携带骑乘者、诊断 |

**修改：**

| 文件 | 改什么 |
|---|---|
| `Source/StellarEra/Public/VoxelPlanetActor.h` | 加 `bUseLocalCoordinates` 属性 + `SyncVoxelWorldTransform()` 声明 |
| `Source/StellarEra/Private/VoxelPlanetActor.cpp` | 抽出变换同步逻辑；哈希在局部坐标模式下不混 `ActorLocation` |
| `Source/StellarEra/Public/PlanetCharacter.h` / `Private/PlanetCharacter.cpp` | 定期重估最近行星 |
| `Source/StellarEra/Public/SpaceshipPawn.h` / `Private/SpaceshipPawn.cpp` | 定期重估最近行星 |
| `Source/StellarEra/Private/StellarEraGameMode.cpp` | 母星改成「离世界原点最近」 |

**关卡（编辑器内手工操作，见 Task 6）：** 1 个恒星 `StaticMeshActor` + 4 个 `AVoxelPlanetActor`。

---

### Task 1: 搬运 CosmosTypes 与 NBodySimulator

**Files:**
- Create: `Source/StellarEra/Public/CosmosTypes.h`
- Create: `Source/StellarEra/Public/NBodySimulator.h`
- Create: `Source/StellarEra/Private/NBodySimulator.cpp`
- Create: `C:/Users/23080/AppData/Local/Temp/ue_mcp/add_bom.ps1`（工具脚本，不进仓库）

**Interfaces:**
- Consumes: 无（本任务是地基）
- Produces:
  - `namespace CosmosUnits` — `GravitationalConstant_Game = 39.478417604357434`、`GravitationalConstant_SI = 6.67430e-5`、`AuToCentimeter = 1.495978707e13`、`YearToSecond = 3.15576e7`、`SolarMassToKilogram = 1.98892e30`、`EarthMass = 3.0034893e-6`、`JupiterMass = 9.545942e-4`、`EarthOrbitalSpeed = 6.283185307179586`
  - `enum class ECosmosUnitSystem : uint8 { Game, SI }`
  - `enum class ECosmosIntegrator : uint8 { VelocityVerlet, Euler, None }`
  - `enum class ECosmosBodyType : uint8 { Star, Planet, Moon, Invisible }`
  - `enum class ECosmosLightMode : uint8 { Directional, Point }`
  - `enum class ECosmosOrbitPreset : uint8 { Custom, Circular, Elliptical }`
  - `struct FCosmosOrbitInit` — `Preset` / `SemiMajorAxis` / `Eccentricity` / `InclinationDegrees` / `AscendingNodeDegrees` / `PhaseDegrees` / `OverrideVelocity`，方法 `bool bHasVelocityOverride() const`
  - `struct FCosmosBodyState` — `FVector3d Position/Velocity/Acceleration`、`double Mass`、`double Radius`、`double SofteningOverride`、`TWeakObjectPtr<AActor> OwnerActor`
  - `class FNBodySimulator` — `SetUnitSystem` / `GetUnitSystem` / `SetIntegrator` / `GetIntegrator` / `SetSofteningLength` / `GetSofteningLength` / `SetGravityCutoff` / `GetGravityCutoff` / **`SetCentimetersPerAu(double)`** / **`GetCentimetersPerAu() const`** / `GetNumBodies` / `GetBodies` / `AddBody(const FCosmosBodyState&) -> int32` / `GetBodyMutable(int32)` / `GetBody(int32) const` / `FindBodyByActor(const AActor*) -> int32` / `Clear()` / `Step(float)` / `StepExact(double)` / `GetPendingTime` / `ResetAccumulator` / `GetSimulatedTime` / `GetTotalEnergy` / `GetTotalMomentum` / `GetTotalAngularMomentum` / `GetEnergyDriftRatio` / `MarkEnergyBaseline` / `ToWorldLocation(const FVector3d&) const -> FVector` / `ToGameUnitPosition(const FVector&) const -> FVector3d` / `ToWorldVelocity` / `ToWorldSeconds` / `ToWorldCentimeter` / `ApplyOrbitInit(int32, const FCosmosOrbitInit&, double ReferenceMassSolar)`

---

- [ ] **Step 1: 建补 BOM 的工具脚本**

`C:/Users/23080/AppData/Local/Temp/ue_mcp/add_bom.ps1`：

```powershell
param([Parameter(Mandatory=$true)][string]$Path)
$b = [System.IO.File]::ReadAllBytes($Path)
if ($b.Length -ge 3 -and $b[0] -eq 0xEF -and $b[1] -eq 0xBB -and $b[2] -eq 0xBF) {
    Write-Output "BOM present: $Path"
    exit 0
}
[System.IO.File]::WriteAllBytes($Path, [byte[]](0xEF,0xBB,0xBF) + $b)
Write-Output "BOM added: $Path"
```

验证脚本可用（对一个已存在的源文件跑，应当输出 `BOM present`）：

```bash
powershell -NoProfile -File "C:/Users/23080/AppData/Local/Temp/ue_mcp/add_bom.ps1" -Path "F:/workspace/ue/StellarEra/Source/StellarEra/Public/VoxelPlanetActor.h"
```

预期：`BOM present: F:/workspace/ue/StellarEra/Source/StellarEra/Public/VoxelPlanetActor.h`

- [ ] **Step 2: 建 `CosmosTypes.h`（原样搬运）**

从 `F:\workspace\ue\思路\VoxelTestStudy\VoxelCosmos\Public\CosmosTypes.h` **整份复制**到 `Source/StellarEra/Public/CosmosTypes.h`，**逐字不改**（173 行）。

它已经完全不依赖引擎（只有 `CoreMinimal.h` + `.generated.h`），不需要任何删改。

唯一要补的一句：把 `AuToCentimeter` 的注释改得更明确，避免有人误以为它是本项目的世界尺度。把那三行改成：

```cpp
	/**
	 * 1 AU = 1.495978707e13 厘米。**这是真实物理值，不是本项目的世界尺度。**
	 * 本项目的世界按「1 AU = 10 公里」标定，见 FNBodySimulator::CentimetersPerAu。
	 * 直接用这个常量会把星球甩出 float32 的精度范围（该处 ULP ≈ 18 公里）。
	 */
	inline constexpr double AuToCentimeter = 1.495978707e13;
```

- [ ] **Step 3: 补 BOM**

```bash
powershell -NoProfile -File "C:/Users/23080/AppData/Local/Temp/ue_mcp/add_bom.ps1" -Path "F:/workspace/ue/StellarEra/Source/StellarEra/Public/CosmosTypes.h"
```

- [ ] **Step 4: 建 `NBodySimulator.h`**

从 `F:\workspace\ue\思路\VoxelTestStudy\VoxelCosmos\Public\NBodySimulator.h` 复制到 `Source/StellarEra/Public/NBodySimulator.h`，然后做**三处**改动。

**改动 1**：第 57 行去掉 API 宏。

```cpp
class VOXELCOSMOS_API FNBodySimulator
```
改成
```cpp
class FNBodySimulator
```

**改动 2**：在「单位系统」两个 setter 之后插入新属性。找到这一段：

```cpp
	/** 单位系统。修改会重算引力常数。 */
	void SetUnitSystem(ECosmosUnitSystem Unit);
	ECosmosUnitSystem GetUnitSystem() const { return UnitSystem; }
```

在它下面紧接着插入：

```cpp

	/**
	 * 1 AU 对应多少世界厘米。**本项目最重要的一个标定值。**
	 *
	 * 真实值是 1.496e13，但那样 float32 在该处的 ULP 会到 18 公里 —— 比本项目的
	 * 星球（半径 1 公里）还大 18 倍，位置会剧烈抖动，星球直接看不出在哪。
	 * 所以本项目的世界按「1 AU = 10 公里」标定：1e6 厘米处 ULP 是 0.12 厘米，
	 * 远小于 100 厘米的体素边长，体素渲染不会抖。
	 *
	 * 只影响 AU ↔ 世界厘米的换算，**不影响求解器内部**：位置、速度、质量全程是
	 * AU / AU每年 / M☉ 的双精度，G 仍然是 4π²。改这个值不改变轨道形状，
	 * 只改变「轨道画在世界里多大」。
	 */
	void SetCentimetersPerAu(double InCentimetersPerAu);
	double GetCentimetersPerAu() const { return CentimetersPerAu; }
```

**改动 3**：在私有成员区（`double SofteningLength = 0.0001;` 之前）插入成员：

```cpp
	/** 1 AU 对应多少世界厘米。默认 1e6 = 1 AU 10 公里，理由见 SetCentimetersPerAu。 */
	double CentimetersPerAu = 1.0e6;

```

- [ ] **Step 5: 补 BOM**

```bash
powershell -NoProfile -File "C:/Users/23080/AppData/Local/Temp/ue_mcp/add_bom.ps1" -Path "F:/workspace/ue/StellarEra/Source/StellarEra/Public/NBodySimulator.h"
```

- [ ] **Step 6: 建 `NBodySimulator.cpp`**

从 `F:\workspace\ue\思路\VoxelTestStudy\VoxelCosmos\Private\NBodySimulator.cpp` 复制到 `Source/StellarEra/Private/NBodySimulator.cpp`，然后做**三处**改动。

**改动 1**：在 `SetSofteningLength` 实现后面加上新 setter 的实现：

```cpp
void FNBodySimulator::SetCentimetersPerAu(double InCentimetersPerAu)
{
	// 给一个下限：0 会让所有天体坍缩到原点，负数更无意义。
	CentimetersPerAu = FMath::Max(InCentimetersPerAu, 1.0);
}
```

**改动 2**：`ToWorldLocation` 与 `ToGameUnitPosition` 改用成员，不再用编译期常量。

```cpp
FVector FNBodySimulator::ToWorldLocation(const FVector3d& GameUnitPosition) const
{
	if (UnitSystem == ECosmosUnitSystem::Game)
	{
		return FVector(GameUnitPosition) * CentimetersPerAu;
	}
	return FVector(GameUnitPosition);
}

FVector3d FNBodySimulator::ToGameUnitPosition(const FVector& WorldLocation) const
{
	if (UnitSystem == ECosmosUnitSystem::Game)
	{
		return FVector3d(WorldLocation) / CentimetersPerAu;
	}
	return FVector3d(WorldLocation);
}
```

**改动 3**：`ToWorldVelocity` 与 `ToWorldCentimeter` 同样改用成员，并在函数上方加一段说明它们是**当前无人调用**的：

```cpp
// ── 以下三个换算目前无人调用 ──
// 本项目只用到位置换算（ToWorldLocation / ToGameUnitPosition）。速度、秒、距离
// 三个换算在参考项目里也是死代码。保留是为了让搬运保持完整、将来要加拖尾或
// 速度显示时直接用；改动此处时不要以为有调用方在依赖它。
FVector FNBodySimulator::ToWorldVelocity(const FVector3d& GameUnitVelocity) const
{
	if (UnitSystem == ECosmosUnitSystem::Game)
	{
		// AU/年 → cm/s
		return FVector(GameUnitVelocity) * (CentimetersPerAu / CosmosUnits::YearToSecond);
	}
	return FVector(GameUnitVelocity);
}
```

`ToWorldSeconds` 不动（它用的是 `YearToSecond`，是物理常量，与本项目世界尺度无关）。

`ToWorldCentimeter` 改成：

```cpp
double FNBodySimulator::ToWorldCentimeter(double GameUnitAu) const
{
	return (UnitSystem == ECosmosUnitSystem::Game) ? GameUnitAu * CentimetersPerAu : GameUnitAu;
}
```

其余部分（`ComputeAccelerations` / `StepVelocityVerlet` / `StepEuler` / `StepExact` / `Step` / 四个诊断函数）**一字不改**。

> **更正（最终复审）：`ApplyOrbitInit` 已不在「一字不改」之列 —— 它的初速公式是错的。**
> 原文照搬的写法是「圆轨道速度 `√(GM/r)`，椭圆轨道再乘修正因子 `√((1+e)/(1+e·cosE))`」。
> `PhaseDegrees = 0` 时 `E = 0`，修正因子恰好等于 1，于是发射速度就是半径
> `r = a(1−e)` 处的**圆**轨道速度 —— 四颗行星全被发射到半径 `a(1−e)` 的圆轨道上，
> 配置的偏心率全部丢失；速度方向还被写成纯切向，非近日点相位时与椭圆切线不符。
> 启动日志只打印位置与周期，能量漂移诊断也只衡量守恒性 —— 而圆轨道同样是能量守恒的
> 合法轨道，漂移指标照样合格，所以两个诊断都看不出这个问题。
> 现已按近焦点坐标系的分量改写：半通径 `p = a(1−e²)`，横向 `Vt = √(GM/p)·(1+e·cos ν)`，
> 径向 `Vr = √(GM/p)·e·sin ν`，再按真近点角 `ν` 旋回笛卡尔分量。

- [ ] **Step 7: 补 BOM**

```bash
powershell -NoProfile -File "C:/Users/23080/AppData/Local/Temp/ue_mcp/add_bom.ps1" -Path "F:/workspace/ue/StellarEra/Source/StellarEra/Private/NBodySimulator.cpp"
```

- [ ] **Step 8: 构建验证**

关闭编辑器，然后：

```bash
powershell -NoProfile -File "C:/Users/23080/AppData/Local/Temp/ue_mcp/ue_build.ps1"
```

预期：末尾 `Build succeeded`，退出码 0，**没有** UHT 报错。

这一步只验证「能编译、UHT 认得这三个新文件」。求解器的**数值**正确性（能量漂移）要到 Task 4 有了 `LogDiagnostics` 才可观测 —— 这是 spec §9 明确接受的做法。

- [ ] **Step 9: 确认没有把 `VOXELCOSMOS_API` 带进来**

```bash
grep -rn "VOXELCOSMOS_API" F:/workspace/ue/StellarEra/Source/
```

预期：无输出（退出码 1）。

- [ ] **Step 10: 提交**

```bash
cd F:/workspace/ue/StellarEra
git add Source/StellarEra/Public/CosmosTypes.h Source/StellarEra/Public/NBodySimulator.h Source/StellarEra/Private/NBodySimulator.cpp
git commit -m "feat: 搬运 N 体引力求解器与宇宙单位制

从参考项目 VoxelCosmos 搬入 FNBodySimulator 与 CosmosTypes。
求解器保持与引擎解耦（只认 TWeakObjectPtr<AActor> 做反查），
去掉 VOXELCOSMOS_API。

唯一的功能性改动：把 1 AU 对应的世界厘米数从编译期常量
AuToCentimeter(1.496e13) 改成成员 CentimetersPerAu，默认 1e6
（1 AU = 10 公里）。真 AU 尺度下 float32 的 ULP 有 18 公里，
比本项目半径 1 公里的星球还大 18 倍，星球会抖成一片。

Co-Authored-By: Claude Code <noreply@anthropic.com>"
```

---

### Task 2: AVoxelPlanetActor 局部坐标模式

**Files:**
- Modify: `Source/StellarEra/Public/VoxelPlanetActor.h`
- Modify: `Source/StellarEra/Private/VoxelPlanetActor.cpp`

**Interfaces:**
- Consumes: 无
- Produces:
  - `bool AVoxelPlanetActor::bUseLocalCoordinates` — `EditAnywhere`，默认 `true`
  - `void AVoxelPlanetActor::SyncVoxelWorldTransform()` — `public`，幂等

**为什么这是必须的**：`ComputeParamsHash()` 混了 `ActorLocation`（`VoxelPlanetActor.cpp:294-297`），所以 `RebuildPlanet()` 顶部的守卫（`:137-142`）在 Actor 每帧移动时永远不成立 → 每帧 `DestroyWorld()` + `CreateWorld()`。Task 4 的 N 体子系统会每帧移动星球，不改这里就是每帧重建整个体素世界。

**注意**：`GetCenterInVoxels()` **不用改**。因为 `VoxelWorld` 变换 == Actor 变换，它算出来已经是 `ZeroVector`。

---

- [ ] **Step 1: 在 `VoxelPlanetActor.h` 里加公开方法声明**

找到 `InvalidateAndRebuild()` 的声明（`VoxelPlanetActor.h:45-52`），在它的闭合 `}` 之后、`GetSurfaceRadiusCm()` 之前插入：

```cpp

	/**
	 * 把 Actor 变换同步给代理体素世界。
	 *
	 * AVoxelWorld 是 Actor 子对象，它的变换不跟着 PlanetRoot 走（见 RebuildPlanet
	 * 里的说明）。RebuildPlanet 会调一次；局部坐标模式下 N 体子系统还会**每帧**
	 * 调一次 —— 星球每帧移动，不同步的话渲染网格会留在原地、星球跑掉。
	 *
	 * 幂等：变换已经一致时什么都不做。
	 */
	void SyncVoxelWorldTransform();
```

- [ ] **Step 2: 在 `VoxelPlanetActor.h` 里加 `bUseLocalCoordinates`**

找到地形段落结束、`////// 缩放与性能` 开始的位置（`VoxelPlanetActor.h:98-102`），在 `Terrain` 属性之后、`缩放与性能` 横幅之前插入：

```cpp
	//////////////////////////////////////////////////////////////////////////
	// 坐标模式

	/**
	 * 局部坐标模式。**N 体星系下必须为真。**
	 *
	 * 为真时生成器的球心锚在 Actor 的局部原点，世界位置完全由 Actor 变换承担。
	 * 这正是「移动 Actor 不重建体素世界」的前提：ComputeParamsHash 不再把每帧
	 * 变化的 Actor 位置混进去，重建守卫于是能正常生效。
	 *
	 * 为假时保留旧行为 —— 把 Actor 位置混进哈希，移动 Actor 即触发整颗星重建。
	 * 单颗静止的行星两种模式结果完全相同（球心都在原点），所以默认 true 不影响
	 * 已经摆好的星球。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet|Coordinates")
	bool bUseLocalCoordinates = true;

```

- [ ] **Step 3: 在 `VoxelPlanetActor.cpp` 里抽出同步逻辑**

把 `RebuildPlanet()` 里这一段（`VoxelPlanetActor.cpp:149-160`）：

```cpp
	// --- 0. 让体素世界的原点与 Actor 的原点重合 ---
	// AVoxelWorld 是 Actor 子对象：构造函数里的 CreateDefaultSubobject 只建出一个
	// 实例，它的变换不会跟着 PlanetRoot 走。而它自己的变换是会被序列化下来的 ——
	// 世界原点重定基（AVoxelWorld::ApplyWorldOffset）留下的陈旧位移会一直留在关卡
	// 里，把整颗星顶到离 Actor 几公里外。这里每次重建都把它拨回来。先比较再赋值，
	// 免得每次 OnConstruction 都把组件标脏。必须排在取球心之前：球心按该变换的
	// 逆变换算出来。
	const FTransform ActorTransform = GetActorTransform();
	if (!VoxelWorld->GetActorTransform().Equals(ActorTransform, 0.001f))
	{
		VoxelWorld->SetActorTransform(ActorTransform);
	}
```

**整段替换**为：

```cpp
	// --- 0. 让体素世界的原点与 Actor 的原点重合 ---
	// 必须排在取球心之前：球心按该变换的逆变换算出来。
	SyncVoxelWorldTransform();
```

- [ ] **Step 4: 加 `SyncVoxelWorldTransform()` 的实现**

在 `RebuildPlanet()` 函数**之前**（紧挨着 `#endif` 的 `PostEditChangeProperty` 之后）插入：

```cpp
void AVoxelPlanetActor::SyncVoxelWorldTransform()
{
	if (!VoxelWorld)
	{
		return;
	}

	// AVoxelWorld 是 Actor 子对象：构造函数里的 CreateDefaultSubobject 只建出一个
	// 实例，它的变换不会跟着 PlanetRoot 走。而它自己的变换是会被序列化下来的 ——
	// 世界原点重定基（AVoxelWorld::ApplyWorldOffset）留下的陈旧位移会一直留在关卡
	// 里，把整颗星顶到离 Actor 几公里外。
	//
	// 先比较再赋值，免得每次都把组件标脏。
	//
	// 局部坐标模式下本函数还会被 N 体子系统**每帧**调用：星球每帧移动，这个变换
	// 不跟上的话渲染网格会留在原地、星球跑掉。
	const FTransform ActorTransform = GetActorTransform();
	if (!VoxelWorld->GetActorTransform().Equals(ActorTransform, 0.001f))
	{
		VoxelWorld->SetActorTransform(ActorTransform);
	}
}
```

- [ ] **Step 5: 在 `ComputeParamsHash()` 里给 `ActorLocation` 加局部坐标守卫**

把这一段（`VoxelPlanetActor.cpp:294-297`）：

```cpp
	const float ScaledRadius = GetScaledRadiusVoxels();
	const FVector ActorLocation = GetActorLocation();
	Mix(&ScaledRadius, sizeof(ScaledRadius));
	Mix(&ActorLocation, sizeof(ActorLocation));
```

**整段替换**为：

```cpp
	const float ScaledRadius = GetScaledRadiusVoxels();
	Mix(&ScaledRadius, sizeof(ScaledRadius));

	// 局部坐标模式下**不能**混 ActorLocation：N 体子系统每帧都在移动这颗星球，
	// 混进去等于每帧销毁并重建整个体素世界。球心锚在局部原点、世界位置由 Actor
	// 变换承担，与生成器的参数无关，所以移动本来就不需要重建。
	if (!bUseLocalCoordinates)
	{
		const FVector ActorLocation = GetActorLocation();
		Mix(&ActorLocation, sizeof(ActorLocation));
	}
```

**注意**：原来那段上方的注释（「混原始位置而不是换算后的球心……」）要保留，它解释的正是「为什么假模式下必须混位置」。

- [ ] **Step 6: 加一行重建日志**

`RebuildPlanet()` 里守卫通过之后（即 `LastParamsHash = ParamsHash;` 那一行之后）插入：

```cpp
	// 重建一次打一行：这是「移动星球到底有没有触发重建」的唯一可观测证据。
	// 正常情况下拖动星球不应该产生新行（局部坐标模式），
	// 若每帧刷屏就说明哈希守卫失效了。
	UE_LOG(LogTemp, Log, TEXT("AVoxelPlanetActor: 重建 %s（参数哈希 %llu）"), *GetName(), ParamsHash);
```

- [ ] **Step 7: 构建验证**

关闭编辑器，然后：

```bash
powershell -NoProfile -File "C:/Users/23080/AppData/Local/Temp/ue_mcp/ue_build.ps1"
```

预期：`Build succeeded`，退出码 0。

- [ ] **Step 8: 验证「移动星球不重建」（可证伪）**

1. 打开编辑器，加载 `Content/Maps/UniverseMap`，打开 Output Log 并过滤 `AVoxelPlanetActor`。
2. 选中关卡里那颗星球，在视口里**拖动它**（或改 `Radius` 1000 → 1001 → 1000 触发一次真实重建）。
3. **预期**：只出现**一次**重建日志（那一次是改 Radius 造成的），之后纯粹拖动位置**不再产生任何新行**。
4. 反证：把 `bUseLocalCoordinates` 改成 `false`，再拖动 —— **预期每次拖动都刷出新行**。改回 `true`。

第 4 步是关键 —— 它证明这个守卫真的在起作用，而不是因为别的原因此时恰好不重建。

- [ ] **Step 9: 验证回归 —— 星球外观没变**

PIE，确认：
- 星球照常渲染，**没有**消失或变成灰色平面。
- 地形起伏、顶点色与改动前一致（山峰、海洋分层都在）。
- 站在星球表面能正常行走、跳跃。

- [ ] **Step 10: 提交**

```bash
cd F:/workspace/ue/StellarEra
git add Source/StellarEra/Public/VoxelPlanetActor.h Source/StellarEra/Private/VoxelPlanetActor.cpp
git commit -m "feat: 星球支持局部坐标模式，移动不再重建体素世界

ComputeParamsHash 原本混了 ActorLocation，导致 N 体每帧移动星球时
重建守卫永远不成立，会每帧 DestroyWorld + CreateWorld。

新增 bUseLocalCoordinates（默认 true）：为真时不混 ActorLocation，
球心锚在局部原点、世界位置由 Actor 变换承担。单颗静止行星两种模式
结果相同，默认值不影响已摆好的星球。

同时把 RebuildPlanet 里的 VoxelWorld 变换同步抽成公开的
SyncVoxelWorldTransform()，供 N 体子系统每帧调用 —— AVoxelWorld 是
Actor 子对象，变换不会跟着 PlanetRoot 走，不同步渲染网格会留在原地。

Co-Authored-By: Claude Code <noreply@anthropic.com>"
```

---

### Task 3: UCosmosOrbitComponent（天体组件）

**Files:**
- Create: `Source/StellarEra/Public/CosmosOrbitComponent.h`
- Create: `Source/StellarEra/Private/CosmosOrbitComponent.cpp`

**Interfaces:**
- Consumes: `FCosmosOrbitInit`（Task 1）
- Produces:
  - `class UCosmosOrbitComponent : public UActorComponent`，`UCLASS(ClassGroup = (Cosmos), meta = (BlueprintSpawnableComponent))`
  - `double MassSolar = 3.0034893e-6`
  - `FCosmosOrbitInit Orbit`
  - `double CentralMassSolar = 1.0`
  - `bool bApplyOrbitInit = true`
  - `bool bSimulated = true`
  - `int32 SimulatorIndex = INDEX_NONE`

**本任务不做注册。** 组件此刻是纯数据 —— 它要引用的 `UCosmosSimulationSubsystem` 要到 Task 4 才存在，先写注册会编译不过。注册在 Task 4 Step 6 加回来。

---

- [ ] **Step 1: 建 `CosmosOrbitComponent.h`**

```cpp
// Copyright Voxel Plugin SAS. All Rights Reserved.
// 把任意 Actor 变成 N 体系统里的一个天体。

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "CosmosTypes.h"
#include "CosmosOrbitComponent.generated.h"

/**
 * 天体组件：挂在任意 Actor 上，它就参与 N 体引力。
 *
 * 刻意做成「组件」而不是「Actor 基类」：
 *   - 恒星不需要新类 —— 一个 StaticMeshActor 挂上它就是天体，不必为它写一个
 *     只有网格的恒星 Actor；
 *   - 行星也不需要 —— AVoxelPlanetActor 挂上它就是天体，不必让体素星球去继承
 *     一个 N 体的基类。
 *
 * 组件本身不做任何事：它只存参数。注册、推进、写回位置全在
 * UCosmosSimulationSubsystem 里 —— N 体是耦合系统（A 受 B 的力，B 也受 A 的力），
 * 必须有唯一的编排者，各天体分别 tick 会读到彼此的中间状态。
 */
UCLASS(ClassGroup = (Cosmos), meta = (BlueprintSpawnableComponent))
class UCosmosOrbitComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UCosmosOrbitComponent();

	/**
	 * 质量，单位 M☉（太阳质量）。默认值是地球质量。
	 *
	 * 行星之间的引力相对恒星可以忽略，但质量仍是决定「谁绕谁」的唯一依据 ——
	 * 中心天体必须是这里面最大的那个，否则行星会直接飞出去。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cosmos|Body")
	double MassSolar = 3.0034893e-6;

	/**
	 * 轨道初始条件。
	 *
	 * 用「半长轴 + 偏心率」而不是直接填位置速度：这样天体保证落在正确的轨道上。
	 * 直接填位置和速度，一旦两者不匹配就会飞向恒星或者逃逸。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cosmos|Orbit")
	FCosmosOrbitInit Orbit;

	/**
	 * 中心天体的质量，单位 M☉。**只用于推导初始速度**，不参与积分。
	 *
	 * 积分用的是各天体自己的 MassSolar；这个值只在开局算「给定半长轴该飞多快」
	 * 时用一次。填错只会让开局速度不对，不会改变后续的引力。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cosmos|Orbit")
	double CentralMassSolar = 1.0;

	/**
	 * 是否用上面的轨道根数覆盖 Actor 摆放的位置与速度。
	 *
	 * 为真：开局位置由轨道根数算出，Actor 摆在关卡哪里都无所谓。
	 * 为假：位置保持关卡里摆的样子，速度为零 —— 中心恒星用这个。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cosmos|Orbit")
	bool bApplyOrbitInit = true;

	/**
	 * 是否每帧把求解器算出的位置写回所属 Actor。
	 *
	 * **与 bApplyOrbitInit 是两件事，不要合并。** 参考项目只有一个 bSimulated
	 * 同时管这两件事，结果是中心恒星的位置在求解器里漂、在 Actor 上不动，两边
	 * 分叉。这里拆开：
	 *   - 恒星：bApplyOrbitInit = false、bSimulated = true —— 位置保持摆放的样子，
	 *     但仍由求解器写回，两边永远一致；
	 *   - 行星：两个都 true；
	 *   - 纯装饰的远处天体：两个都 false。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cosmos|Body")
	bool bSimulated = true;

	/** 求解器里的索引。运行时由子系统填写，不要手改。 */
	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "Cosmos")
	int32 SimulatorIndex = INDEX_NONE;
};
```

- [ ] **Step 2: 建 `CosmosOrbitComponent.cpp`**

```cpp
// Copyright Voxel Plugin SAS. All Rights Reserved.

#include "CosmosOrbitComponent.h"

UCosmosOrbitComponent::UCosmosOrbitComponent()
{
	// 本组件自己不 tick：推进由 UCosmosSimulationSubsystem 统一做。
	PrimaryComponentTick.bCanEverTick = false;
}
```

- [ ] **Step 3: 补 BOM（两个文件都要）**

```bash
powershell -NoProfile -File "C:/Users/23080/AppData/Local/Temp/ue_mcp/add_bom.ps1" -Path "F:/workspace/ue/StellarEra/Source/StellarEra/Public/CosmosOrbitComponent.h"
powershell -NoProfile -File "C:/Users/23080/AppData/Local/Temp/ue_mcp/add_bom.ps1" -Path "F:/workspace/ue/StellarEra/Source/StellarEra/Private/CosmosOrbitComponent.cpp"
```

- [ ] **Step 4: 构建验证**

关闭编辑器，然后：

```bash
powershell -NoProfile -File "C:/Users/23080/AppData/Local/Temp/ue_mcp/ue_build.ps1"
```

预期：`Build succeeded`，退出码 0。

- [ ] **Step 5: 验证组件可见、默认值正确（可证伪）**

1. 打开编辑器，加载 `Content/Maps/UniverseMap`。
2. 选中关卡里任意一个 Actor，在 Details 面板点 **Add Component**，搜索 `Cosmos Orbit`。
3. **预期**：列表里出现 `Cosmos Orbit`。
4. 添加后展开各分类，**预期**看到：
   - `Cosmos|Body` → `Mass Solar = 3.0034893e-06`、`b Simulated` 勾选
   - `Cosmos|Orbit` → `Orbit` 展开后有 `Preset = Circular`、`Semi Major Axis = 1.0`、`Eccentricity = 0.0`、`Inclination Degrees = 0.0`、`Ascending Node Degrees = 0.0`、`Phase Degrees = 0.0`、`Override Velocity = (0,0,0)`
   - `Cosmos|Orbit` → `Central Mass Solar = 1.0`、`b Apply Orbit Init` 勾选
5. **预期**：`Simulator Index` 是灰的（`VisibleAnywhere`），值为 `-1`。
6. 验证完**把刚加的组件删掉**，不要留在关卡里 —— 摆放是 Task 8 的事。

- [ ] **Step 6: 提交**

```bash
cd F:/workspace/ue/StellarEra
git add Source/StellarEra/Public/CosmosOrbitComponent.h Source/StellarEra/Private/CosmosOrbitComponent.cpp
git commit -m "feat: 新增天体组件 UCosmosOrbitComponent

把「轨道」从「星球」里拆出来：任意 Actor 挂上这个组件就参与 N 体引力，
恒星不必新写一个只有网格的 Actor 类，体素星球也不必继承 N 体基类。

bApplyOrbitInit 与 bSimulated 拆成两个开关（参考项目只有一个，会让中心
恒星的位置在求解器里漂、在 Actor 上不动，两边分叉）。

组件自身不 tick，也不做注册 —— 推进由 UCosmosSimulationSubsystem 统一做。

Co-Authored-By: Claude Code <noreply@anthropic.com>"
```

---

### Task 4: UCosmosSimulationSubsystem —— N 体的唯一驱动

**Files:**
- Create: `Source/StellarEra/Public/CosmosSimulationSubsystem.h`
- Create: `Source/StellarEra/Private/CosmosSimulationSubsystem.cpp`
- Modify: `Source/StellarEra/Public/CosmosOrbitComponent.h`（加 BeginPlay / EndPlay 声明）
- Modify: `Source/StellarEra/Private/CosmosOrbitComponent.cpp`（实现自注册）

**Interfaces:**
- Consumes:
  - `FNBodySimulator`（Task 1）：`SetUnitSystem`、`SetIntegrator`、`SetSofteningLength`、`SetCentimetersPerAu`、`AddBody(const FCosmosBodyState&) -> int32`、`GetBody(int32) -> const FCosmosBodyState&`、`GetNumBodies()`、`ApplyOrbitInit(int32, const FCosmosOrbitInit&, double)`、`StepExact(double)`、`MarkEnergyBaseline()`、`GetEnergyDriftRatio()`、`GetTotalMomentum()`、`GetTotalAngularMomentum()`、`GetSimulatedTime()`、`ToGameUnitPosition(const FVector&) -> FVector3d`、`ToWorldLocation(const FVector3d&) -> FVector`、`Clear()`
  - `FCosmosBodyState`（Task 1）：`Position` / `Mass` / `Radius` / `OwnerActor`
  - `CosmosUnits::GravitationalConstant_Game`（Task 1）= 39.478417604357434
  - `UCosmosOrbitComponent`（Task 3）：`MassSolar`、`Orbit`、`CentralMassSolar`、`bApplyOrbitInit`、`bSimulated`、`SimulatorIndex`
  - `AVoxelPlanetActor::SyncVoxelWorldTransform()`（Task 2）、`GetSurfaceRadiusCm()`
- Produces:
  - 单例子系统，`GetWorld()->GetSubsystem<UCosmosSimulationSubsystem>()` 可取
  - `RegisterBody(UCosmosOrbitComponent*)` / `UnregisterBody(UCosmosOrbitComponent*)`
  - `LogDiagnostics() const`
  - 可调项（`UCLASS(Config = Game)`，可在 `Config/DefaultGame.ini` 的 `[/Script/StellarEra.CosmosSimulationSubsystem]` 段覆盖）：`CentimetersPerAu` / `TimeScale` / `StepsPerOrbit` / `MaxStepsPerFrame` / `SofteningLength` / `RideRadiusScale` / `DiagnosticInterval`
- **本任务不写 `CarryRiders`** —— 那是 Task 5 的事。Task 5 会同时加声明、实现、以及 Tick 里的调用点。

**引擎事实（已核实，别再猜）：**
- `UTickableWorldSubsystem`（`Engine/Public/Subsystems/WorldSubsystem.h:80`）的 `Tick` **有引擎实现**，`Super::Tick(DeltaTime)` 可以调，且基类里带 `checkf(IsInitialized())`。
- 同文件 `:92` 的 `GetStatId` 是 `PURE_VIRTUAL(...)`，不重写会拿到空 StatId，必须重写。
- 同文件 `:96-97` 的 `Initialize` / `Deinitialize` 注释明写「Subclasses must forward calls」，`Super::` 必须调，否则 tick 不会被启用。
- `DoesSupportWorldType` 在 `UWorldSubsystem`（同文件 `:66`）上是 **public virtual**，默认支持 Game / PIE / Editor。

- [ ] **Step 1: 建 `CosmosSimulationSubsystem.h`**

```cpp
// Copyright Voxel Plugin SAS. All Rights Reserved.
// N 体引力系统的唯一驱动。

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "NBodySimulator.h"
#include "CosmosSimulationSubsystem.generated.h"

class UCosmosOrbitComponent;

/**
 * N 体引力系统的驱动。
 *
 * 为什么是 WorldSubsystem 而不是 GameMode：
 *   - 一个关卡只能有一个 GameMode，本项目已有的 AStellarEraGameMode 负责开局登船，
 *     不该被 N 体挤掉；
 *   - N 体是耦合系统（A 受 B 的力，B 也受 A 的力），必须有唯一的编排者。
 *     WorldSubsystem 的单例由引擎保证，不需要在关卡里摆一个驱动 Actor。
 *
 * 每帧顺序（全在一次 Tick 内完成，避免天体之间差一帧）：
 *   1. 推进求解器若干步
 *   2. 对每个天体：把求解器位置写回 Actor（bSimulated 为真时）
 *   3. 体素星球：同步代理体素世界的变换
 *   4. 把位移施加给骑在这颗星球上的 Pawn（Task 5）
 */
UCLASS(Config = Game)
class UCosmosSimulationSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	//~ Begin USubsystem Interface
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	//~ End USubsystem Interface

	//~ Begin FTickableGameObject Interface
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	//~ End FTickableGameObject Interface

	/** 由 UCosmosOrbitComponent::BeginPlay 调用。 */
	void RegisterBody(UCosmosOrbitComponent* Component);

	/** 由 UCosmosOrbitComponent::EndPlay 调用。 */
	void UnregisterBody(UCosmosOrbitComponent* Component);

	/** 把当前诊断打到日志。每 DiagnosticInterval 秒自动调一次。 */
	void LogDiagnostics() const;

	//////////////////////////////////////////////////////////////////////////
	// 标定
	//
	// 这些都是 UPROPERTY(Config)：可以在 Config/DefaultGame.ini 里覆盖，改完重启
	// 编辑器生效，不必重新编译。风险表里那几个「调一下试试」都靠这里。

	/**
	 * 1 AU 对应多少世界厘米。默认 1e6，即 1 AU = 10 公里。
	 *
	 * 真值 1.496e13 下 float32 的 ULP 有 18 公里，比半径 1 公里的星球还大 18 倍，
	 * 体素世界会抖成一团。若实测仍有抖动，把它降到 3e5（1 AU = 3 公里）——
	 * 精度更宽松，代价是整个星系缩到三分之一。
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Cosmos|Scale")
	double CentimetersPerAu = 1.0e6;

	/** 模拟年 / 真实秒。默认 0.00333，即公转一圈约 300 秒。 */
	UPROPERTY(Config, EditAnywhere, Category = "Cosmos|Time")
	double TimeScale = 0.00333;

	/**
	 * 最短轨道每圈积分多少步，用来推自动步长（开普勒第三定律：T = 2π√(a³/GM)）。
	 *
	 * 参考项目写死「最短周期 / 1000」，在本尺度下要 18 帧才算一步，肉眼可见卡顿。
	 * 20000 步/圈给出约每帧一步。
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Cosmos|Time")
	int32 StepsPerOrbit = 20000;

	/** 单帧积分步数上限。超了会自动降 TimeScale，而不是让帧率崩掉。 */
	UPROPERTY(Config, EditAnywhere, Category = "Cosmos|Time")
	int32 MaxStepsPerFrame = 200;

	/**
	 * 引力软化长度，单位 AU。防止两天体擦肩而过时 r→0、加速度趋于无穷导致数值溢出。
	 * 太小失去保护作用，太大会削弱真实引力。
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Cosmos|Physics")
	double SofteningLength = 1.0e-5;

	/**
	 * 携带判定：Pawn 到球心的距离 < 地表半径 × 该值，就把星球本帧的位移加给 Pawn。
	 *
	 * 默认 2.0 —— 半径 1 公里的星球携带 2 公里内的一切。站在地面上、跳跃中、
	 * 飞船悬停在 100 米高度都覆盖得到；在 50 公里外自由飞行时不会被推着走。
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Cosmos|Physics")
	double RideRadiusScale = 2.0;

	/** 诊断日志间隔（秒）。设 0 关闭。 */
	UPROPERTY(Config, EditAnywhere, Category = "Cosmos|Debug")
	float DiagnosticInterval = 5.f;

private:
	/** 首次 Tick 时做一次：配置求解器、注册所有天体、推步长、记能量基准。 */
	void FinalizeRegistration();

	/** 把一颗天体注册进求解器。 */
	void AddBodyToSimulator(UCosmosOrbitComponent* Component);

	TArray<TObjectPtr<UCosmosOrbitComponent>> Bodies;

	FNBodySimulator Simulator;

	/** 自动推导出的积分步长，单位模拟年。 */
	double BaseTimeStep = 5.0e-5;

	/**
	 * 跨帧累积的待推进时间（模拟年）。
	 *
	 * 步长固定是辛积分器的前提（变步长会让能量漂移失去上界），而每帧要推进的
	 * 年数不是步长的整数倍，所以余量必须存下来。见 Tick 里的详细说明。
	 */
	double AdvanceAccumulator = 0.0;

	float DiagnosticTimer = 0.f;
	bool bInitialized = false;
};
```

- [ ] **Step 2: 建 `CosmosSimulationSubsystem.cpp`**

```cpp
// Copyright Voxel Plugin SAS. All Rights Reserved.

#include "CosmosSimulationSubsystem.h"

#include "CosmosOrbitComponent.h"
#include "VoxelPlanetActor.h"
#include "Engine/World.h"
#include "Stats/Stats.h"

void UCosmosSimulationSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	// 引擎在 PostInitProperties 里已经按 UCLASS(Config = Game) 载入过一次。
	// 这里再显式载一次，是为了让「这些值从 ini 来」这件事在代码里看得见。
	LoadConfig();

	Simulator.SetUnitSystem(ECosmosUnitSystem::Game);
	Simulator.SetIntegrator(ECosmosIntegrator::VelocityVerlet);
	Simulator.SetSofteningLength(SofteningLength);
	Simulator.SetCentimetersPerAu(CentimetersPerAu);

	Super::Initialize(Collection);
}

void UCosmosSimulationSubsystem::Deinitialize()
{
	Bodies.Reset();
	Simulator.Clear();
	bInitialized = false;

	Super::Deinitialize();
}

bool UCosmosSimulationSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	// 只在 PIE 与打包世界里跑。
	//
	// 编辑器世界里体素星球走的是预览路径，天体没必要动、也不该动 ——
	// 动了会把关卡里手工摆好的位置改掉，每次打开地图都漂一点。
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId UCosmosSimulationSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UCosmosSimulationSubsystem, STATGROUP_Tickables);
}

void UCosmosSimulationSubsystem::RegisterBody(UCosmosOrbitComponent* Component)
{
	if (!Component)
	{
		return;
	}

	for (const TObjectPtr<UCosmosOrbitComponent>& Existing : Bodies)
	{
		if (Existing == Component)
		{
			return;
		}
	}

	Bodies.Add(Component);
}

void UCosmosSimulationSubsystem::UnregisterBody(UCosmosOrbitComponent* Component)
{
	if (!Component)
	{
		return;
	}

	for (int32 Index = Bodies.Num() - 1; Index >= 0; --Index)
	{
		if (Bodies[Index] == Component)
		{
			Bodies.RemoveAt(Index);
		}
	}

	// 只从列表里摘掉，求解器里的那个天体留着不动 —— 中途把它从数组里删掉会让
	// 后面所有索引整体前移，其余组件已存的 SimulatorIndex 全部失效。
	// 本项目的天体在整局游戏里不增不减，这个取舍是安全的。
}

void UCosmosSimulationSubsystem::AddBodyToSimulator(UCosmosOrbitComponent* Component)
{
	AActor* BodyActor = Component ? Component->GetOwner() : nullptr;
	if (!BodyActor)
	{
		return;
	}

	FCosmosBodyState State;

	// 起始位置用 Actor 当前摆放的位置；bApplyOrbitInit 为真时随后会被轨道根数覆盖。
	State.Position = Simulator.ToGameUnitPosition(BodyActor->GetActorLocation());
	State.Mass = Component->MassSolar;
	State.OwnerActor = BodyActor;

	// 半径只对体素星球有意义（只有它才有「地表」这个概念）。恒星之类的天体
	// 不携带任何人，留 0 即可 —— 携带判定走的是 Task 5 里单独的一条路径。
	if (const AVoxelPlanetActor* Planet = Cast<AVoxelPlanetActor>(BodyActor))
	{
		State.Radius = double(Planet->GetSurfaceRadiusCm()) / CentimetersPerAu;
	}

	Component->SimulatorIndex = Simulator.AddBody(State);

	if (Component->bApplyOrbitInit)
	{
		Simulator.ApplyOrbitInit(Component->SimulatorIndex, Component->Orbit, Component->CentralMassSolar);
	}
}

void UCosmosSimulationSubsystem::FinalizeRegistration()
{
	bInitialized = true;

	if (Bodies.Num() == 0)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[Cosmos] 关卡里没有任何 UCosmosOrbitComponent，N 体系统不启动。"));
		return;
	}

	// 按质量降序注册：中心天体先进求解器。
	// N 体是同时求解的，顺序不影响结果，这样只是让日志更好读。
	Bodies.Sort([](const TObjectPtr<UCosmosOrbitComponent>& A, const TObjectPtr<UCosmosOrbitComponent>& B)
	{
		return A->MassSolar > B->MassSolar;
	});

	for (UCosmosOrbitComponent* Component : Bodies)
	{
		AddBodyToSimulator(Component);

		AActor* BodyActor = Component ? Component->GetOwner() : nullptr;
		if (!BodyActor || !Component->bSimulated || Component->SimulatorIndex == INDEX_NONE)
		{
			continue;
		}

		// 立即同步一次：让开局第一帧就在轨道根数算出的位置上，
		// 而不是关卡里手摆的位置（手摆的位置跟轨道对不上，会先弹一下）。
		const FCosmosBodyState& State = Simulator.GetBody(Component->SimulatorIndex);
		BodyActor->SetActorLocation(Simulator.ToWorldLocation(State.Position), /*bSweep=*/false);

		if (AVoxelPlanetActor* Planet = Cast<AVoxelPlanetActor>(BodyActor))
		{
			Planet->SyncVoxelWorldTransform();
		}
	}

	// ── 自动步长 ──
	// 取最短的一条轨道周期，除以 StepsPerOrbit。开普勒第三定律：T = 2π√(a³/GM)。
	double ShortestPeriod = TNumericLimits<double>::Max();

	for (const UCosmosOrbitComponent* Component : Bodies)
	{
		if (!Component || !Component->bApplyOrbitInit || Component->Orbit.SemiMajorAxis <= 0.0)
		{
			continue;
		}

		const double SemiMajorAxis = Component->Orbit.SemiMajorAxis;
		const double Mu = CosmosUnits::GravitationalConstant_Game * FMath::Max(Component->CentralMassSolar, 1e-6);
		const double Period = 2.0 * PI * FMath::Sqrt(SemiMajorAxis * SemiMajorAxis * SemiMajorAxis / Mu);

		ShortestPeriod = FMath::Min(ShortestPeriod, Period);
	}

	if (FMath::IsFinite(ShortestPeriod) && ShortestPeriod > 0.0 && StepsPerOrbit > 0)
	{
		BaseTimeStep = FMath::Clamp(ShortestPeriod / double(StepsPerOrbit), 1e-12, 1e-2);
	}

	Simulator.MarkEnergyBaseline();

	UE_LOG(LogTemp, Log,
		TEXT("[Cosmos] 初始化完成：%d 个天体，最短周期 %.6f 年，步长 %.6e 年，TimeScale %.5f 年/秒，1 AU = %.0f 厘米"),
		Simulator.GetNumBodies(), ShortestPeriod, BaseTimeStep, TimeScale, CentimetersPerAu);
}

void UCosmosSimulationSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (!bInitialized)
	{
		FinalizeRegistration();
	}

	if (Simulator.GetNumBodies() == 0)
	{
		return;
	}

	// ── 1. 时间推进 ──
	// 真实秒 × TimeScale = 需要模拟的天文年数。
	//
	// 余量必须跨帧累积。步长固定是辛积分器的前提（变步长会破坏辛性质、让能量
	// 漂移失去上界），而每帧要推进的年数不是步长的整数倍 —— 本关卡 60fps 下
	// 每帧需要 1.81 步。直接 floor 丢掉余量的话每帧只跑 1 步，实际速度只有
	// 名义值的 55%，而且随帧率漂移：名义「公转一圈 300 秒」会变成 540 秒，
	// 帧率一掉还要更慢。
	//
	// 单帧的推进量另设上限：加载或卡顿之后不该让模拟去补一大段（那既没有
	// 意义，又会让 TimeScale 因为连续超限而永久掉下去）。
	const double MaxAdvancePerFrame =
		double(FMath::Max(MaxStepsPerFrame - 1, 1)) * BaseTimeStep;
	AdvanceAccumulator += FMath::Min(double(DeltaTime) * TimeScale, MaxAdvancePerFrame);

	const int32 NumSteps = FMath::FloorToInt(AdvanceAccumulator / BaseTimeStep);
	if (NumSteps > 0)
	{
		const int32 StepsToRun = FMath::Min(NumSteps, MaxStepsPerFrame);

		// 只扣掉真正跑了的那些步，剩下的留到下一帧。
		AdvanceAccumulator -= double(StepsToRun) * BaseTimeStep;

		for (int32 StepIndex = 0; StepIndex < StepsToRun; ++StepIndex)
		{
			Simulator.StepExact(BaseTimeStep);
		}

		// 持续超限说明 TimeScale 相对步长太大。降一点，而不是让帧率崩掉。
		if (NumSteps > MaxStepsPerFrame)
		{
			TimeScale *= 0.9;
			UE_LOG(LogTemp, Warning,
				TEXT("[Cosmos] 单帧积分步数超限（%d > %d），TimeScale 自动降至 %.5f"),
				NumSteps, MaxStepsPerFrame, TimeScale);
		}
	}

	// ── 2. 写回各个天体 ──
	for (UCosmosOrbitComponent* Component : Bodies)
	{
		AActor* BodyActor = Component ? Component->GetOwner() : nullptr;
		if (!BodyActor || Component->SimulatorIndex == INDEX_NONE)
		{
			continue;
		}

		const FVector PreviousLocation = BodyActor->GetActorLocation();

		if (Component->bSimulated)
		{
			const FCosmosBodyState& State = Simulator.GetBody(Component->SimulatorIndex);
			BodyActor->SetActorLocation(Simulator.ToWorldLocation(State.Position), /*bSweep=*/false);
		}

		// 位移取 Actor 的**实际**变化量，而不是求解器算出的差值：
		// bSimulated 为假的天体在求解器里会漂、在 Actor 上不动，用实际变化量
		// 两边都自洽（那种天体的位移恒为零）。
		const FVector Delta = BodyActor->GetActorLocation() - PreviousLocation;
		if (Delta.IsNearlyZero())
		{
			continue;
		}

		if (AVoxelPlanetActor* Planet = Cast<AVoxelPlanetActor>(BodyActor))
		{
			// 体素世界是 Actor 子对象，变换不会跟着 PlanetRoot 走。
			// 星球移动后必须同步，否则渲染网格留在原地、星球跑掉。
			Planet->SyncVoxelWorldTransform();
		}
	}

	// ── 3. 诊断 ──
	if (DiagnosticInterval > 0.f)
	{
		DiagnosticTimer += DeltaTime;
		if (DiagnosticTimer >= DiagnosticInterval)
		{
			DiagnosticTimer = 0.f;
			LogDiagnostics();
		}
	}
}

void UCosmosSimulationSubsystem::LogDiagnostics() const
{
	UE_LOG(LogTemp, Log, TEXT("========== Cosmos 诊断 =========="));
	UE_LOG(LogTemp, Log, TEXT("天体数量  : %d"), Simulator.GetNumBodies());
	UE_LOG(LogTemp, Log, TEXT("步长      : %.6e 年"), BaseTimeStep);
	UE_LOG(LogTemp, Log, TEXT("TimeScale : %.5f 年/秒（公转一圈约 %.0f 秒）"),
		TimeScale, TimeScale > 0.0 ? 1.0 / TimeScale : 0.0);
	UE_LOG(LogTemp, Log, TEXT("已模拟    : %.6f 年"), Simulator.GetSimulatedTime());
	UE_LOG(LogTemp, Log, TEXT("能量漂移  : %.6e  （辛积分应 < 1e-5）"), Simulator.GetEnergyDriftRatio());
	UE_LOG(LogTemp, Log, TEXT("总动量    : %.6e"), Simulator.GetTotalMomentum().Size());
	UE_LOG(LogTemp, Log, TEXT("角动量    : %.6e"), Simulator.GetTotalAngularMomentum().Size());
	UE_LOG(LogTemp, Log, TEXT("1 AU      : %.0f 厘米"), CentimetersPerAu);
	UE_LOG(LogTemp, Log, TEXT("=================================="));
}
```

- [ ] **Step 3: 给 `CosmosOrbitComponent.h` 加 BeginPlay / EndPlay 声明**

在 `public:` 段里，`SimulatorIndex` 的下面、类的收尾 `};` 之前插入：

```cpp
	//~ Begin UActorComponent Interface
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	//~ End UActorComponent Interface
```

- [ ] **Step 4: 给 `CosmosOrbitComponent.cpp` 实现自注册**

把整个文件替换成：

```cpp
// Copyright Voxel Plugin SAS. All Rights Reserved.

#include "CosmosOrbitComponent.h"

#include "CosmosSimulationSubsystem.h"
#include "Engine/World.h"

UCosmosOrbitComponent::UCosmosOrbitComponent()
{
	// 本组件自己不 tick：推进由 UCosmosSimulationSubsystem 统一做。
	PrimaryComponentTick.bCanEverTick = false;
}

void UCosmosOrbitComponent::BeginPlay()
{
	Super::BeginPlay();

	// 组件自己找上门注册，而不是让子系统去遍历全关卡的 Actor：
	// 遍历全世界既慢，又会漏掉运行期生成的 Actor。
	if (const UWorld* OwningWorld = GetWorld())
	{
		if (UCosmosSimulationSubsystem* CosmosSubsystem = OwningWorld->GetSubsystem<UCosmosSimulationSubsystem>())
		{
			CosmosSubsystem->RegisterBody(this);
		}
	}
}

void UCosmosOrbitComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (const UWorld* OwningWorld = GetWorld())
	{
		if (UCosmosSimulationSubsystem* CosmosSubsystem = OwningWorld->GetSubsystem<UCosmosSimulationSubsystem>())
		{
			CosmosSubsystem->UnregisterBody(this);
		}
	}

	Super::EndPlay(EndPlayReason);
}
```

> 局部变量刻意叫 `OwningWorld` / `CosmosSubsystem` 而不是 `World` / `Subsystem` ——
> C4458（局部变量遮蔽继承成员）在本项目是硬错误，别用短名字去赌。

- [ ] **Step 5: 补 BOM（4 个文件）**

```bash
powershell -NoProfile -File "C:/Users/23080/AppData/Local/Temp/ue_mcp/add_bom.ps1" -Path "F:/workspace/ue/StellarEra/Source/StellarEra/Public/CosmosSimulationSubsystem.h"
powershell -NoProfile -File "C:/Users/23080/AppData/Local/Temp/ue_mcp/add_bom.ps1" -Path "F:/workspace/ue/StellarEra/Source/StellarEra/Private/CosmosSimulationSubsystem.cpp"
powershell -NoProfile -File "C:/Users/23080/AppData/Local/Temp/ue_mcp/add_bom.ps1" -Path "F:/workspace/ue/StellarEra/Source/StellarEra/Public/CosmosOrbitComponent.h"
powershell -NoProfile -File "C:/Users/23080/AppData/Local/Temp/ue_mcp/add_bom.ps1" -Path "F:/workspace/ue/StellarEra/Source/StellarEra/Private/CosmosOrbitComponent.cpp"
```

- [ ] **Step 6: 构建验证**

关闭编辑器，然后：

```bash
powershell -NoProfile -File "C:/Users/23080/AppData/Local/Temp/ue_mcp/ue_build.ps1"
```

预期：`Build succeeded`，退出码 0。

- [ ] **Step 7: 最小关卡验证（可证伪）**

这一步刻意用**跟 Task 8 一样的配置**（一颗网格恒星 + 一颗体素行星），出问题现在就知道，不要另起一套简化版。

**为什么恒星必须是 `StaticMeshActor` 而不是 `AVoxelPlanetActor`：** `APlanetCharacter::FindNearestPlanet` 与 `AStellarEraGameMode::EnsureSpaceshipExists` 都只认 `AVoxelPlanetActor`。恒星若也是体素星球，玩家永远落在**不动的恒星**上，Task 5 的参考系跟随就没有验证对象。恒星摆在原点不动，正好也满足 `ApplyOrbitInit` 以世界原点为轨道中心这个前提。

1. 打开编辑器，加载 `Content/Maps/UniverseMap`。
2. 摆**恒星甲**：Place Actors → Basic → **Sphere**（`StaticMeshActor`），
   - 位置 `(0, 0, 0)`，缩放使其半径约 **1500 米**（引擎 Sphere 静态网格半径 50 cm，即 Scale ≈ 3000）。
   - Add Component → `Cosmos Orbit`：`Mass Solar = 1.0`、`b Apply Orbit Init` **取消勾选**、`b Simulated` 勾选。
3. 摆**行星乙**：`AVoxelPlanetActor`，位置 `(1000000, 0, 0)`（1 AU = 10 公里 = 1e6 厘米）。
   - `Radius = 1000`、`VoxelSize = 100`、`VoxelThreads = 4`，`Terrain` 留空（光滑球，省时间）。
   - Add Component → `Cosmos Orbit`：`Mass Solar = 3.0034893e-06`、`Orbit` 展开后
     `Preset = Circular`、`Semi Major Axis = 1.0`、`Eccentricity = 0.0`，
     `Central Mass Solar = 1.0`、`b Apply Orbit Init` 勾选、`b Simulated` 勾选。
4. 按 Play（PIE）。等约 30 秒。

**预期（逐条核对，任一条不成立就是没做对）：**

| # | 观察点 | 预期 | 不成立说明什么 |
|---|---|---|---|
| 1 | 输出日志开头 | 出现 `[Cosmos] 初始化完成：2 个天体，最短周期 1.000000 年，步长 5.000000e-05 年，TimeScale 0.00333 年/秒，1 AU = 1000000 厘米` | 天体没注册上（组件没挂 / `BeginPlay` 没跑 / 子系统 `DoesSupportWorldType` 把 PIE 挡了） |
| 2 | 每 5 秒的诊断 | `能量漂移` 的绝对值 **< 1e-5** | 积分器接错了。1e-2 以上说明退化成欧拉法，去查 `SetIntegrator` 有没有调 |
| 3 | 行星乙的位置 | 肉眼可见地绕原点转圈，约 **300 秒**转一整圈；30 秒内转过约 36°，移动约 6 公里 | 步长或 TimeScale 不对 |
| 4 | 恒星甲的位置 | **一动不动**停在原点 | `b Apply Orbit Init` 没取消勾选，或者 `MassSolar` 不是 1.0（两体互绕，重心会偏） |
| 5 | **`AVoxelPlanetActor: 重建` 日志** | 乙在移动的**整个过程里一条都不出现** | **这是 Task 2 的验收点。** 若每帧刷屏，说明 `ComputeParamsHash` 还在混 `ActorLocation`，或者 `bUseLocalCoordinates` 没勾 |
| 6 | 星球外观 | 乙移动时是一个完整的球在走，**不是**「网格留在原地、碰撞体跑掉」 | `SyncVoxelWorldTransform()` 没接上 |
| 7 | 飞船 / 角色 | **会被乙甩下**，留在原地不动 | **这一条是预期的、不是 bug** —— 参考系跟随是 Task 5 的事。先记下「甩下」这个现象，Task 5 做完后它会消失 |

5. 停下 PIE。**保留**这两颗天体（Task 8 会在它们基础上补齐另外三颗行星），但**不要提交关卡文件** —— 关卡改动统一在 Task 8 一次性提交。

- [ ] **Step 8: 提交**

```bash
cd F:/workspace/ue/StellarEra
git add Source/StellarEra/Public/CosmosSimulationSubsystem.h Source/StellarEra/Private/CosmosSimulationSubsystem.cpp Source/StellarEra/Public/CosmosOrbitComponent.h Source/StellarEra/Private/CosmosOrbitComponent.cpp
git commit -m "feat: N 体驱动 UCosmosSimulationSubsystem

用 WorldSubsystem 而不是 GameMode：一个关卡只能有一个 GameMode，已有的
AStellarEraGameMode 负责开局登船，不该被 N 体挤掉；而 N 体是耦合系统，
必须有唯一编排者，WorldSubsystem 的单例由引擎保证。

只在 Game/PIE 世界创建：编辑器世界里动天体只会把关卡里手工摆好的位置改掉。

自动步长按开普勒第三定律从最短轨道周期推（参考项目写死 /1000，在本尺度下
18 帧才算一步）。单帧步数超限时降 TimeScale 而不是让帧率崩掉。

组件自己找上门注册，子系统不遍历关卡 —— 遍历全世界既慢又会漏掉运行期
生成的 Actor。

Co-Authored-By: Claude Code <noreply@anthropic.com>"
```

---

### Task 5: 参考系跟随 —— 星球移动时带上站在上面的人

**Files:**
- Modify: `Source/StellarEra/Public/CosmosSimulationSubsystem.h`（加 `CarryRiders` 声明）
- Modify: `Source/StellarEra/Private/CosmosSimulationSubsystem.cpp`（加 include、实现、Tick 里的调用点）

**Interfaces:**
- Consumes: `AVoxelPlanetActor::GetSurfaceRadiusCm()`（`VoxelPlanetActor.h:61`，public `UFUNCTION(BlueprintPure)`，返回 float 厘米）、`RideRadiusScale`（Task 4）、Tick 里的天体循环（Task 4）
- Produces: `void CarryRiders(AActor* BodyActor, const FVector& Delta)` —— 私有，只在本子系统内用

**为什么这一步是硬需求：** 1 AU 的轨道速度是 209 米/秒。没有它，站在地面上的角色每帧都被地面「抽走」—— 星球走了、人留在原地，下一步重力又把人往下拽，表现为剧烈抖动，随后被甩进太空。这不是手感问题，是能不能站着的问题。

- [ ] **Step 1: 在 `CosmosSimulationSubsystem.h` 里加声明**

找到 `private:` 段里 `AddBodyToSimulator` 的声明，在它**之后**、`TArray<TObjectPtr<UCosmosOrbitComponent>> Bodies;` **之前**插入：

```cpp
	/**
	 * 把 BodyActor 本帧的位移施加给骑在它上面的 Pawn。
	 *
	 * 只在星球附近生效（判定半径 = 地表半径 × RideRadiusScale），不是全局平移：
	 * 在 50 公里外自由飞行的飞船不该被行星拖着走。
	 */
	void CarryRiders(AActor* BodyActor, const FVector& Delta);
```

- [ ] **Step 2: 给 `CosmosSimulationSubsystem.cpp` 补 include**

在 `#include "Engine/World.h"` 那一行**之后**插入两行：

```cpp
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
```

- [ ] **Step 3: 在 `CosmosSimulationSubsystem.cpp` 末尾追加实现**

```cpp
void UCosmosSimulationSubsystem::CarryRiders(AActor* BodyActor, const FVector& Delta)
{
	// 只有体素星球携带人。恒星是网格、没有「地表」这个概念，
	// 站在上面的东西靠自身重力待着就行。
	const AVoxelPlanetActor* Planet = Cast<AVoxelPlanetActor>(BodyActor);
	if (!Planet)
	{
		return;
	}

	UWorld* OwningWorld = GetWorld();
	if (!OwningWorld)
	{
		return;
	}

	const double RideRadius = double(Planet->GetSurfaceRadiusCm()) * RideRadiusScale;
	const double RideRadiusSq = RideRadius * RideRadius;
	const FVector PlanetLocation = Planet->GetActorLocation();

	for (TActorIterator<APawn> It(OwningWorld); It; ++It)
	{
		APawn* Rider = *It;
		if (!Rider)
		{
			continue;
		}

		// 已经挂在别的 Actor 上的 Pawn 一律跳过，两个理由：
		//   1. 挂在**这颗星球**上的：星球移动时附着关系本来就把它带走了，
		//      再加一次位移就是走两步；
		//   2. 挂在**别的 Pawn** 上的（登船后的角色挂在飞船下）：那个 Pawn
		//      如果也被本函数推着走，附着关系已经带走了它，同样会走两步。
		// TActorIterator 的遍历顺序不保证，靠顺序去重是不可靠的，所以按附着
		// 关系去重。（本项目当前只有一处 AttachToActor：登船时角色挂到飞船下。）
		if (Rider->GetAttachParentActor())
		{
			continue;
		}

		if (FVector::DistSquared(Rider->GetActorLocation(), PlanetLocation) > RideRadiusSq)
		{
			continue;
		}

		// 不用 Sweep：Pawn 本来就在地面上，硬扫会把它从地形里挤出去。
		// 这一帧星球平移多少，Pawn 就跟着平移多少，相对关系原样保留。
		Rider->AddActorWorldOffset(Delta, /*bSweep=*/false);
	}
}
```

- [ ] **Step 4: 在 `Tick` 里接上调用点**

找到 Task 4 写下的这一段（在「2. 写回各个天体」循环里）：

```cpp
		if (AVoxelPlanetActor* Planet = Cast<AVoxelPlanetActor>(BodyActor))
		{
			// 体素世界是 Actor 子对象，变换不会跟着 PlanetRoot 走。
			// 星球移动后必须同步，否则渲染网格留在原地、星球跑掉。
			Planet->SyncVoxelWorldTransform();
		}
	}
```

改成：

```cpp
		if (AVoxelPlanetActor* Planet = Cast<AVoxelPlanetActor>(BodyActor))
		{
			// 体素世界是 Actor 子对象，变换不会跟着 PlanetRoot 走。
			// 星球移动后必须同步，否则渲染网格留在原地、星球跑掉。
			Planet->SyncVoxelWorldTransform();
		}

		// 顺序要紧：先把星球自己搬到新位置，再把人按同样的位移带过去。
		CarryRiders(BodyActor, Delta);
	}
```

- [ ] **Step 5: 构建验证**

关闭编辑器，然后：

```bash
powershell -NoProfile -File "C:/Users/23080/AppData/Local/Temp/ue_mcp/ue_build.ps1"
```

预期：`Build succeeded`，退出码 0。

- [ ] **Step 6: 关卡验证（可证伪）**

沿用 Task 4 Step 7 摆好的关卡（恒星甲在原点、行星乙在 1 AU 处绕转），直接按 Play。

**预期（逐条核对）：**

| # | 观察点 | 预期 | 不成立说明什么 |
|---|---|---|---|
| 1 | 开局 | 飞船贴在乙的地表上方约 4 米（`ShipSpawnHeight = 400`），不是悬在太空、也不是埋在球里 | `EnsureSpaceshipExists` 挑错星球了 |
| 2 | 飞船不动，等 30 秒 | 飞船**跟着乙一起走**，始终贴在乙地表上方。**Task 4 里那个「被甩下」的现象必须消失** | 携带没生效：`CarryRiders` 没被调、或 `Delta.IsNearlyZero()` 提前 `continue` 了 |
| 3 | 按 `E` 下船（`IA_Interact` 绑的是 E，见 `StellarEraPlayerController.cpp:145`） | 角色站在乙的地面上 | —— |
| 4 | 角色不动，再等 30 秒 | 角色跟着乙一起走，**始终站在地面同一点上**，不抖动、不被甩进太空 | 携带没生效：`CarryRiders` 没被调、或判定半径接错。若只抖不甩，可能是 `RideRadiusScale` 太小，角色在边界上反复进出 |
| 5 | **反证**：驾飞船按住空格垂直飞离乙超过 2 公里（约 25 秒），然后停住 | 飞船**不再被带走**，乙从下方跑掉 | 若仍被带走，说明判定半径没生效，`RideRadiusScale` 的乘法接错了 |
| 6 | 恒星甲 | 始终停在原点 | —— |

第 5 条是这次验证的重点：它把「携带」和「全局平移」区分开。只做 1–4 的话，一个把全世界都平移过去的错误实现也能通过。

- [ ] **Step 7: 提交**

```bash
cd F:/workspace/ue/StellarEra
git add Source/StellarEra/Public/CosmosSimulationSubsystem.h Source/StellarEra/Private/CosmosSimulationSubsystem.cpp
git commit -m "feat: 参考系跟随，星球移动时带上站在上面的人

1 AU 轨道速度 209 米/秒，没有这一步角色每帧都会被地面抽走，表现为剧烈
抖动并随后被甩进太空。

判定半径 = 地表半径 × RideRadiusScale（默认 2.0），不是全局平移：在
50 公里外自由飞行的飞船不该被行星拖着走。

跳过已附着的 Pawn。TActorIterator 顺序不保证，靠顺序去重不可靠，所以按
附着关系去重：挂在星球上的、和挂在别的 Pawn 上的（登船后的角色）都由
附着关系带走，再加一次位移就是走两步。

Co-Authored-By: Claude Code <noreply@anthropic.com>"
```

---

### Task 6: 关卡摆放 —— 1 恒星 + 4 行星

> **与 spec §10 的顺序差异（有意为之，已裁定）：** spec §10 把「参考系跟随 + 星球切换 + 母星选择」排在「关卡摆放」之前。本计划把**关卡摆放提到星球切换之前**。
>
> **理由：** 星球切换的验收需要关卡里有多颗星球 —— 单星球关卡里 `FindNearestPlanet` 永远返回同一颗，切换逻辑无论写没写对都观察不到差别，任务就成了不可证伪的。母星选择的验收同样需要 4 颗星球才能看出「第一颗命中的」和「离原点最近的」不是同一颗。spec 的 §10 是建议顺序，而「每个任务都要能独立验证」是硬要求，两者冲突时后者优先。代码内容一字未改，只是排序。

**Files:**
- Modify: `Content/Maps/UniverseMap.umap` 及其 OFPA 外部 Actor：`Content/__ExternalActors__/Maps/UniverseMap/`
- Create: `Content/Code/DA_Terrain_Venus.uasset`、`DA_Terrain_Earth.uasset`、`DA_Terrain_Mars.uasset`、`DA_Terrain_Jupiter.uasset`

**Interfaces:**
- Consumes: `UCosmosOrbitComponent`（Task 3）、`AVoxelPlanetActor::bUseLocalCoordinates`（Task 2）、`UCosmosSimulationSubsystem` 的 `CentimetersPerAu = 1e6`（Task 4）、Task 4/5 摆好的恒星甲与行星乙
- Produces: Tasks 7、8 验收所依赖的完整关卡

**这一步没有源码改动，全是编辑器操作。** 全部数据取自 spec §6.1，数值照抄，不要「差不多」。

**天体阵容（spec §6.1 原表）：**

| 天体 | MassSolar | 半长轴 (AU) | 世界轨道 (km) | 偏心率 | 地形种子 | VoxelThreads |
|---|---|---|---|---|---|---|
| 恒星（中心，`StaticMeshActor`） | 1.0 | — | 原点 | — | — | — |
| 行星一（金星位，**母星**） | 2.447e-6 | 0.723 | 7.2 | 0.0068 | 1337 | 4 |
| 行星二（地球位） | 3.003e-6 | 1.000 | 10.0 | 0.0167 | 2026 | 2 |
| 行星三（火星位） | 3.227e-7 | 1.524 | 15.2 | 0.0934 | 3041 | 2 |
| 行星四（木星位） | 9.546e-4 | 5.203 | 52.0 | 0.0484 | 4093 | 2 |

**为什么水星位（0.387 AU = 3.87 km）刻意不放：** 恒星视觉半径 1.5 km、行星半径 1 km，3.87 km 处两者表面只隔 1.37 km，视觉上会贴着。

**母星为什么必然是行星一：** 角色与飞船都按「离世界原点最近」选星球（见 Task 7）。圆轨道上 |r| = a，所以「最近」等价于「轨道最小」。即使算上偏心率，近日点距离 行星一 = 0.723 × (1 − 0.0068) = 0.718 AU = 7.18 km，行星二 = 1.000 × (1 − 0.0167) = 0.983 AU = 9.83 km —— 行星一在任何相位上都更近，这个选择是稳的，不会因为时间推移翻车。

**相位为什么全填 0：** `FNBodySimulator::ApplyOrbitInit`（`NBodySimulator.cpp:316`）把 `PhaseDegrees` 当偏近点角用，`PhaseDegrees = 0` 对应**近日点**，位置落在世界 **+X** 方向。四颗都填 0 时开局沿 +X 排成一条线（7.18 / 9.83 / 13.82 / 49.51 km，均为近日点距离 `a(1−e)`），一眼就能看出摆对没有；之后周期不同（0.61 / 1.00 / 1.88 / 11.87 年）会自然散开。
**注意 `Preset` 字段是被 `ApplyOrbitInit` 完全忽略的** —— 真正参与计算的是 `SemiMajorAxis` / `Eccentricity` / `PhaseDegrees` / `InclinationDegrees` / `AscendingNodeDegrees`。填 `Circular` 只是给编辑器看，别指望它把 `Eccentricity` 清零。

**Amplitude 为什么填 80 而不是默认的 8：** 模块头部注释（`VoxelPlanetNoiseTerrain.h:91-98`）写明坡度 ≈ `Amplitude × VoxelSize / 半波长`，而半波长 ≈ `π × 半径(体素) / Frequency`。默认值 8 是按**半径 100 体素**调的；本关卡半径 1000 体素，半波长放大 10 倍，同样的 Amplitude 8 只剩 0.36° 坡度 —— 一颗几乎看不出来有地形的光滑球。按半径等比放大到 **80** 才与模块默认配置的起伏比例一致（起伏 80 体素 × 100 cm = 80 m，占半径 1 km 的 8%，与默认配置的 8/100 相同）。

> **这是本任务最主要的性能旋钮。** `Amplitude` 同时是不确定带宽度（两倍 Amplitude），插件只能跳过区间完全在实体内或实体外的区块。若 4 颗行星同屏时网格化明显卡顿，第一个要降的就是它（80 → 40 → 20），第二个是 `VoxelThreads`。

- [ ] **Step 1: 建 4 份地形 Data Asset**

`UVoxelPlanetTerrainModule` 的基类是 `UDataAsset` 且标了 `UCLASS(Abstract)`（`VoxelPlanetTerrainModule.h:53-54`），所以走标准 Data Asset 创建流程。

对下面四行各做一遍：

1. 内容浏览器定位到 `Content/Code/`。
2. 右键 → **Miscellaneous → Data Asset**。
3. 类选择器里搜 `VoxelPlanetNoiseTerrain`，选中它，命名如下。

| 资产名 | Seed | Amplitude | 其余属性 |
|---|---|---|---|
| `DA_Terrain_Venus` | 1337 | 80 | 全部保持默认 |
| `DA_Terrain_Earth` | 2026 | 80 | 全部保持默认 |
| `DA_Terrain_Mars` | 3041 | 80 | 全部保持默认 |
| `DA_Terrain_Jupiter` | 4093 | 80 | 全部保持默认 |

**预期**：四份资产创建成功，双击打开后 Details 面板顶部分类是 `Terrain|Shape` / `Terrain|Ocean` / `Terrain|Color`，`Amplitude` 显示 80。

> 若类选择器里搜不到 `VoxelPlanetNoiseTerrain`，说明编辑器没重新编译 —— 先做 Step 0：关闭编辑器跑一次 `ue_build.ps1`，再打开。

- [ ] **Step 2: 摆恒星**

沿用 Task 4 摆的 `Sphere`（`StaticMeshActor`），只改两处：

1. 选中它，`Location = (0, 0, 0)`，`Scale = (3000, 3000, 3000)`。
   （引擎 `/Engine/BasicShapes/Sphere` 半径 50 cm，×3000 = 150000 cm = **1.5 km**。）
2. 它身上的 `Cosmos Orbit` 组件确认：`Mass Solar = 1.0`、`b Apply Orbit Init` **未勾选**、`b Simulated` **勾选**。

**预期**：视口里一个半径 1.5 km 的球悬在世界原点。

- [ ] **Step 3: 把 Task 4 的行星乙改成行星一（母星）**

选中 Task 4 摆的 `AVoxelPlanetActor`（在 `(1000000, 0, 0)`），重命名为 `Planet_Venus`。

**先把 `Location` 改成 `(718083.6, 0, 0)`** —— 这是 0.723 AU × (1 − 0.0068) = 0.7180836 AU 的世界厘米数，也就是 `ApplyOrbitInit` 在 `PhaseDegrees = 0` 时算出的近日点位置。**必须摆在轨道位置上，不能随便摆**，理由见 Step 4 后面的说明。

然后按下表改：

| 属性 | 值 |
|---|---|
| `Planet` → `Radius` | `1000` |
| `Planet\|Scale` → `Voxel Size` | `100` |
| `Planet\|Terrain` → `Terrain` | `DA_Terrain_Venus` |
| `Planet\|Performance` → `Voxel Threads` | `4` |
| `Planet\|Coordinates` → `b Use Local Coordinates` | **勾选** |
| `Cosmos Orbit` → `Mass Solar` | `2.447e-6` |
| `Cosmos Orbit` → `Orbit` → `Semi Major Axis` | `0.723` |
| `Cosmos Orbit` → `Orbit` → `Eccentricity` | `0.0068` |
| `Cosmos Orbit` → `Orbit` → `Preset` | `Circular`（不参与计算，只为可读） |
| `Cosmos Orbit` → `Orbit` → `Inclination Degrees` | `0` |
| `Cosmos Orbit` → `Orbit` → `Ascending Node Degrees` | `0` |
| `Cosmos Orbit` → `Orbit` → `Phase Degrees` | `0` |
| `Cosmos Orbit` → `Central Mass Solar` | `1.0` |
| `Cosmos Orbit` → `b Apply Orbit Init` | **勾选** |
| `Cosmos Orbit` → `b Simulated` | **勾选** |

`PlanetMaterial` 保持原样不动 —— 它是从 Task 4 那颗星球继承来的，本来就是对的。

- [ ] **Step 4: 复制出行星二 / 三 / 四**

选中 `Planet_Venus`，按 `Ctrl+D` 复制三次，分别命名并改下表四列（未列出的属性沿用复制来源）：

| 名称 | `Location`（厘米） | `Terrain` | `Mass Solar` | `Semi Major Axis` | `Eccentricity` | `Voxel Threads` |
|---|---|---|---|---|---|---|
| `Planet_Earth` | `(983300, 0, 0)` | `DA_Terrain_Earth` | `3.003e-6` | `1.000` | `0.0167` | `2` |
| `Planet_Mars` | `(1381658.4, 0, 0)` | `DA_Terrain_Mars` | `3.227e-7` | `1.524` | `0.0934` | `2` |
| `Planet_Jupiter` | `(4951174.8, 0, 0)` | `DA_Terrain_Jupiter` | `9.546e-4` | `5.203` | `0.0484` | `2` |

`Phase Degrees` / `Inclination Degrees` / `Ascending Node Degrees` 三颗都保持 `0`。

**为什么 Location 要摆成近日点距离（a×(1−e)×1e6 厘米），而不是随便摆在 `(1000000, 0, 0)`：**

`EnsureSpaceshipExists`（GameMode 的 `BeginPlay`）与 `APlanetCharacter::BeginPlay` 都在 **`UCosmosSimulationSubsystem` 的首次 Tick 之前**跑，而 `FinalizeRegistration` 正是挂在首次 Tick 上的。也就是说**开局选星球的那一刻，行星还在手工摆放的位置上，还没被轨道根数搬走**。

四颗若都摆在 `(1000000, 0, 0)`，「离世界原点最近」就四颗并列，规则退化成「取 `TActorIterator` 第一个」—— 正好是 Task 7 要消灭的那个不确定性，而且是在代码之外以数据的形式埋回来。

摆在近日点位置上，两条规则就**在任何时刻都给出同一个答案**：摆位阶段（行星一 7.18 km 最近）和 `FinalizeRegistration` 之后（同样 7.18 km 最近）完全一致，开局没有任何跳变。

- [ ] **Step 5: 保存并 Play 验证（可证伪）**

先 **保存所有**（`Ctrl+Shift+S`），再按 Play。

**预期（逐条核对）：**

| # | 观察点 | 预期 | 不成立说明什么 |
|---|---|---|---|
| 1 | 日志开头 | `[Cosmos] 初始化完成：5 个天体，最短周期 0.614758 年，步长 3.073792e-05 年，TimeScale 0.00333 年/秒，1 AU = 1000000 厘米` | 天体数不对 = 有星球漏挂 `Cosmos Orbit`；周期不对 = 半长轴填错 |
| 2 | 开局行星位置 | 四颗行星沿 **+X** 排成一条线，距原点约 **7.2 / 10.0 / 15.2 / 52.0 km** | 相位或半长轴不对 |
| 3 | 每 5 秒诊断 | `能量漂移` 绝对值 **< 1e-5** | 积分器接错 |
| 4 | 等 60 秒 | 四颗行星**明显散开**（周期 0.61 / 1.00 / 1.88 / 11.87 年，60 秒 ≈ 0.2 年，行星一已转过约 117°） | 求解器没在推进 |
| 5 | `AVoxelPlanetActor: 重建` 日志 | 全程**一条都不出现** | 有星球没勾 `b Use Local Coordinates` |
| 6 | 四颗行星外观 | 各自有地形起伏与配色，且**四颗的地貌各不相同**（种子不同） | 四颗共用同一份 `Terrain` 资产了 |
| 7 | 帧率 | 能跑，不卡死 | 降 `Amplitude`（80 → 40 → 20），再降非母星的 `VoxelThreads` |

> **已知取舍，不是 bug：** 恒星只是一个 1.5 km 的球，行星四在 52 km 外，看起来会是一个偏暗的小球。场景里已有的平行光是无限远、无衰减的，覆盖整个星系，所以行星本身是亮的；本次**不引入**参考项目那套「每颗恒星为每个受照天体生成一个 DirectionalLight」的多恒星光照系统（见 spec §7 第 2 行）。

> **已知现象，不是 bug：** 角色会可靠地落在**行星一**上（`FindNearestPlanet` 本来就是取最近的），但**飞船未必** —— `EnsureSpaceshipExists` 现在还是「取 `TActorIterator` 第一个」（`StellarEraGameMode.cpp:141-146`），四颗行星里命中的是哪一颗纯看遍历顺序。两者不一致时开局登船必然失败（相距几公里，`BoardingRadius` 只有 600 cm），日志会打出 `StellarEraGameMode: 开局登船失败：N 次重试后仍未完成`。这正是 Task 7 要修的，本任务先记下这个现象。

- [ ] **Step 6: 提交**

关卡用的是 OFPA（One File Per Actor），摆进去的 Actor 存在 `Content/__ExternalActors__/` 下，**必须一起提交**，只提交 `.umap` 会得到一个空关卡。

```bash
cd F:/workspace/ue/StellarEra
git add Content/Maps/UniverseMap.umap Content/__ExternalActors__/Maps/UniverseMap Content/Code/DA_Terrain_Venus.uasset Content/Code/DA_Terrain_Earth.uasset Content/Code/DA_Terrain_Mars.uasset Content/Code/DA_Terrain_Jupiter.uasset
git commit -m "feat: 关卡摆放 1 恒星 + 4 行星

真实天文数据：金星位（母星，轨道最小）/ 地球位 / 火星位 / 木星位。
水星位刻意不放 —— 3.87 km 处恒星与行星表面只隔 1.37 km，视觉上会贴着。

恒星用 StaticMeshActor + UCosmosOrbitComponent，不新增类：一个机制覆盖
所有天体，恒星也不占一个体素世界。

地形 Amplitude 从默认 8 调到 80。默认值是按半径 100 体素调的，本关卡半径
1000 体素，半波长放大 10 倍，8 只剩 0.36 度坡度 —— 一颗看不出来有地形的
光滑球。按半径等比放大才与模块默认配置的起伏比例一致。

4 颗行星 = 4 个体素世界，VoxelThreads 按星球数下调：母星 4 条、其余 2 条。

Co-Authored-By: Claude Code <noreply@anthropic.com>"
```

---

### Task 7: 星球切换 + 母星选择

**Files:**
- Modify: `Source/StellarEra/Public/PlanetCharacter.h`
- Modify: `Source/StellarEra/Private/PlanetCharacter.cpp`（`Tick`、`SetBoarded`、新增 `ReevaluatePlanet`）
- Modify: `Source/StellarEra/Public/SpaceshipPawn.h`
- Modify: `Source/StellarEra/Private/SpaceshipPawn.cpp`（`Tick`）
- Modify: `Source/StellarEra/Private/StellarEraGameMode.cpp:141-152`

**Interfaces:**
- Consumes: `APlanetCharacter::FindNearestPlanet()`（protected，`PlanetCharacter.h:67`）、`APlanetCharacter::SnapToPlanetSurface()`（public，`:55`）、`APlanetCharacter::PlanetActor`（public，`:58-59`）、私有成员 `bHasLanded`（`:160`）、`ASpaceshipPawn::FindNearestPlanet()`（private，`SpaceshipPawn.h:199`）、`ASpaceshipPawn::PlanetActor`（public，`:144-145`）
- Produces: 无新对外接口；两个 Pawn 各自多一个 `PlanetReevaluateInterval` 可调项（默认 0.5 秒）

**为什么两件事合成一个任务：** 它们都是「多颗星球才成立的行为」，共用同一次验收（飞过去、落地、下船），拆开各自都不可证伪。

**两个必须处理的坑：**

1. **登船期间不能重评。** 角色登船后挂在飞船下（`StellarEraPlayerController.cpp:346` 的 `AttachToActor`），会跟着飞船跨星球移动。此时若重评，`SnapToPlanetSurface()` 会把角色从**船里瞬移到新星的地表上**。用 `GetAttachParentActor()` 判据挡掉 —— 这跟 Task 5 用的是同一个信号，本项目只有这一处 `AttachToActor`。
2. **离船那一刻必须立刻重评。** 否则角色会拿着**旧星**的重力方向最多 0.5 秒，表现为刚下船就朝错误方向掉。`SetBoarded(false)` 里把计时器清零即可。

- [ ] **Step 1: `PlanetCharacter.h` 加三个成员**

在 `PlanetSearchRadius`（`PlanetCharacter.h:61-63`）**之后**插入：

```cpp
	/**
	 * 重新评估「最近的行星」的间隔（秒）。默认 0.5。设 0 关闭。
	 *
	 * 行星在动，最近的一颗会换人。最典型的场景：开着飞船去另一颗星球、落地、
	 * 下船 —— 角色的 BeginPlay 早在母星上就跑完了，不重评的话它会一直拿旧星
	 * 当重力源，一脚踩空直接被推向十几公里外的旧星（表现为「向上掉进太空」）。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet")
	float PlanetReevaluateInterval = 0.5f;
```

在 private 段的 `bool bHasLanded = false;`（`:160`）**之后**插入：

```cpp
	/** 距离下次重新评估最近行星还剩多少秒。 */
	float PlanetReevaluateTimer = 0.f;

	/** 重找最近的行星；换了就切过去并重新落到新星的地表上。 */
	void ReevaluatePlanet();
```

- [ ] **Step 2: `PlanetCharacter.cpp` 的 `Tick` 里接上重评**

在 `Tick` 的 `Super::Tick(DeltaSeconds);`（`:160`）**之后**、`if (!PlanetActor)`（`:162`）**之前**插入：

```cpp
	// 行星在动，最近的一颗会换人。放在 PlanetActor 判空之前：开局若一颗都没
	// 找到（PlanetActor 为空），这里才有机会在后续帧找回来。
	//
	// 登船期间不重评：角色此刻挂在飞船下跟着飞船跨星球移动，重评会把角色从
	// 船里瞬移到新星的地表上。本项目只有一处 AttachToActor（登船，见
	// StellarEraPlayerController.cpp），所以附着关系就是「在船上」。
	const bool bBoarded = GetAttachParentActor() != nullptr;

	if (!bBoarded && PlanetReevaluateInterval > 0.f)
	{
		PlanetReevaluateTimer -= DeltaSeconds;
		if (PlanetReevaluateTimer <= 0.f)
		{
			PlanetReevaluateTimer = PlanetReevaluateInterval;
			ReevaluatePlanet();
		}
	}
```

- [ ] **Step 3: `PlanetCharacter.cpp` 里新增 `ReevaluatePlanet`**

在 `SnapToPlanetSurface` 函数（`:238-269`）**之后**插入：

```cpp
void APlanetCharacter::ReevaluatePlanet()
{
	AVoxelPlanetActor* Nearest = FindNearestPlanet();
	if (Nearest == PlanetActor)
	{
		return;
	}

	// 换星：旧星已经不在脚下了（或者本来就没找到）。把「已落地」清掉 ——
	// 新星的地表还没流送出来，得让 Tick 末尾的兜底逻辑重新把角色按到球面上。
	PlanetActor = Nearest;
	bHasLanded = false;

	if (PlanetActor)
	{
		// 内部会把 LastSurfaceUp 清零，相机基座下一帧重建。
		SnapToPlanetSurface();
	}
}
```

- [ ] **Step 4: `PlanetCharacter.cpp` 的 `SetBoarded` 里清零计时器**

找到 `SetBoarded`（`:306-320`）里的这一段：

```cpp
	if (!bBoarded)
	{
		// 离船后重新踩上球面：碰撞体可能已经因为距离太远被回收了。
		bHasLanded = false;
	}
```

改成：

```cpp
	if (!bBoarded)
	{
		// 离船后重新踩上球面：碰撞体可能已经因为距离太远被回收了。
		bHasLanded = false;

		// 离船时可能已经在另一颗星球旁边了（开船过来、下船）。把计时器清零，
		// 让下一帧立刻重找最近的行星，而不是拿着旧星的重力方向等半秒。
		PlanetReevaluateTimer = 0.f;
	}
```

- [ ] **Step 5: `SpaceshipPawn.h` 加两个成员**

在 `PlanetSearchRadius`（`SpaceshipPawn.h:147-149`）**之后**插入：

```cpp
	/**
	 * 重新评估「最近的行星」的间隔（秒）。默认 0.5。设 0 关闭。
	 *
	 * 行星在动，最近的一颗会换人。不重评的话，飞离母星之后自动回正会一直
	 * 对齐到已经远去的旧星，横滚再也拉不回来。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spaceship")
	float PlanetReevaluateInterval = 0.5f;
```

在 private 段的 `float PitchRate = 0.f;`（`:196`）**之后**插入：

```cpp
	/** 距离下次重新评估最近行星还剩多少秒。 */
	float PlanetReevaluateTimer = 0.f;
```

- [ ] **Step 6: `SpaceshipPawn.cpp` 的 `Tick` 里接上重评**

在 `Tick` 的 `Super::Tick(DeltaSeconds);`（`:210`）**之后**、`// --- 1. 角速度衰减 ---`**之前**插入：

```cpp
	// 行星在动，最近的一颗会换人。飞船不需要像角色那样重新摆位 ——
	// PlanetActor 只喂给 GetAutoLevelUp()，换个参考方向就行。
	if (PlanetReevaluateInterval > 0.f)
	{
		PlanetReevaluateTimer -= DeltaSeconds;
		if (PlanetReevaluateTimer <= 0.f)
		{
			PlanetReevaluateTimer = PlanetReevaluateInterval;
			PlanetActor = FindNearestPlanet();
		}
	}
```

- [ ] **Step 7: `StellarEraGameMode.cpp` 换成「离原点最近」**

把 `EnsureSpaceshipExists` 里的这一段（`:141-152`）：

```cpp
	AVoxelPlanetActor* Planet = nullptr;
	for (TActorIterator<AVoxelPlanetActor> It(GetWorld()); It; ++It)
	{
		Planet = *It;
		break;
	}
	if (!Planet)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("StellarEraGameMode: 关卡里没有 AVoxelPlanetActor，跳过飞船生成。"));
		return;
	}
```

替换成：

```cpp
	// 取离世界原点最近的那颗 —— 必须与 APlanetCharacter::FindNearestPlanet
	// 用同一条规则。角色没有 PlayerStart，生成在世界原点后按同一条规则落到
	// 最近的行星上；两处规则若不一致，飞船会生成在另一颗星球上，开局登船的
	// 距离判据（BoardingRadius = 600 cm）差着好几公里，必然失败。
	//
	// 不能再用「取第一颗」：TActorIterator 的顺序不保证，关卡里有 4 颗行星时
	// 它命中的是哪一颗纯看运气。
	AVoxelPlanetActor* Planet = nullptr;
	float NearestDistSq = TNumericLimits<float>::Max();
	for (TActorIterator<AVoxelPlanetActor> It(GetWorld()); It; ++It)
	{
		const float DistSq = FVector::DistSquared(It->GetActorLocation(), FVector::ZeroVector);
		if (DistSq < NearestDistSq)
		{
			NearestDistSq = DistSq;
			Planet = *It;
		}
	}
	if (!Planet)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("StellarEraGameMode: 关卡里没有 AVoxelPlanetActor，跳过飞船生成。"));
		return;
	}
```

- [ ] **Step 8: 构建验证**

关闭编辑器，然后：

```bash
powershell -NoProfile -File "C:/Users/23080/AppData/Local/Temp/ue_mcp/ue_build.ps1"
```

预期：`Build succeeded`，退出码 0。

- [ ] **Step 9: 验收（可证伪，这是本任务的核心）**

按 Play，按顺序做完：

| # | 操作 | 预期 | 不成立说明什么 |
|---|---|---|---|
| 1 | 看开局日志 | **没有** `StellarEraGameMode: 开局登船失败` | 母星规则没生效，两处规则仍不一致 |
| 2 | 看开局位置 | 角色与飞船都在**行星一**（离原点 7.18 km 的那颗）上 | 母星规则没生效。角色那侧本来就取最近，所以要查的是 `EnsureSpaceshipExists` —— 它可能还在用「取第一颗」 |
| 3 | 驾飞船飞向行星二（地球位，开局在 9.83 km 处，相距约 2.65 km）。`MaxSpeed = 8000 cm/s`，约 **33 秒** | 能飞到 | —— |
| 4 | 接近行星二到 6 米内，按 **`E`** 登船 / 离船键 | 角色出现在**行星二**的地表上 | —— |
| 5 | **下船后站住不动，看 30 秒** | 角色**稳稳站在行星二地面上**，跟着行星二一起走 | **这是本任务最关键的验收点。** 若角色一离船就朝行星一的方向「掉」进太空，说明 `ReevaluatePlanet` 没生效或 `SetBoarded` 没清零计时器 |
| 6 | 再按 `E` 登船，飞回行星一 | 来回都正常 | 登船期间重评没被挡住的话，登船瞬间角色会被瞬移到地表 |
| 7 | 反证：把 `PlanetReevaluateInterval` 临时设为 `0`（在关卡实例的 Details 里改角色身上的值），重跑第 5 步 | 角色**立刻朝旧星方向掉走** | 若设成 0 也不掉，说明第 5 步本来就没测到重评逻辑，是别的东西在兜底 |

第 7 条是这条验收里唯一能把「重评生效」和「碰巧没出问题」分开的一步。只做 1–6 的话，一个 `ReevaluatePlanet` 永远返回同一个指针的错误实现也能通过。

- [ ] **Step 10: 提交**

```bash
cd F:/workspace/ue/StellarEra
git add Source/StellarEra/Public/PlanetCharacter.h Source/StellarEra/Private/PlanetCharacter.cpp Source/StellarEra/Public/SpaceshipPawn.h Source/StellarEra/Private/SpaceshipPawn.cpp Source/StellarEra/Private/StellarEraGameMode.cpp
git commit -m "feat: 星球切换与母星选择

星球在动，最近的一颗会换人。角色不重评的话，开船去另一颗星球、落地、
下船之后它会一直拿旧星当重力源，一脚踩空被推向十几公里外的旧星。

两个坑：
- 登船期间不能重评：角色挂在飞船下跟着跨星球移动，重评会把角色从船里
  瞬移到新星地表。用 GetAttachParentActor() 挡掉（本项目只有一处
  AttachToActor，就是登船）。
- 离船那一刻必须立刻重评：SetBoarded(false) 里把计时器清零，否则角色
  会拿着旧星的重力方向最多半秒。

母星选择：EnsureSpaceshipExists 从「取第一颗」改成「离世界原点最近」，
与 APlanetCharacter::FindNearestPlanet 用同一条规则。两处不一致时飞船会
生成在另一颗星球上，开局登船的距离判据差着好几公里，必然失败。
TActorIterator 的顺序不保证，4 颗行星时「取第一颗」纯看运气。

Co-Authored-By: Claude Code <noreply@anthropic.com>"
```

---

### Task 8: 整体验收

**Files:** 无源码改动。本任务产出的是验收结论；只有发现缺陷时才改代码。

**Interfaces:**
- Consumes: Tasks 1–7 的全部产物
- Produces: spec §8 九条验收标准的逐条结论

**本任务不写新测试框架。** 参考项目自己也没有 N 体的单元测试（它的 `Tests/` 里只有 `FactoryRecipeTest.cpp` 和 `GeologyTest.cpp`），N 体从来靠运行期的 `LogDiagnostics` 验证。本次沿用同样做法 —— 引入测试框架不在本次范围内。

**四颗行星的周期表（先算清楚，验收时照着对）：**

| 行星 | a (AU) | T (年) = a^1.5 | 名义墙钟 (秒) | 相对行星一 |
|---|---|---|---|---|
| 行星一（母星） | 0.723 | 0.61476 | 184.6 | 1.000 |
| 行星二 | 1.000 | 1.00000 | 300.3 | **1.627** |
| 行星三 | 1.524 | 1.88138 | 565.0 | **3.060** |
| 行星四 | 5.203 | 11.86811 | 3564 | **19.305** |

名义墙钟 = T ÷ TimeScale（1 年 = 1/0.00333 = 300.3 秒）。

> **对「一圈约 300 秒」这句话的订正：** spec §8 第 1 条写的 300 秒是**行星二（地球位，a = 1.0 AU）**的一圈，不是四颗都一样。行星一的周期只有 184.6 秒，行星四将近一小时。验收时按上表逐颗对，别拿 300 秒去套行星一。
>
> **相对周期比（1 : 1.627 : 3.060 : 19.305）比绝对秒数可靠得多** —— 它直接验证开普勒第三定律，而且与帧率无关。绝对秒数只在帧率稳得住时才对得上。

- [ ] **Step 1: 逐条走 spec §8 的九条验收标准**

按 Play，按下表逐条做。**每一条都要真跑一遍，不要看着像对就跳过** —— 这张表的每一行都对应一个具体的失败模式。

| spec §8 | 怎么测 | 预期 | 不成立时归谁 |
|---|---|---|---|
| **1** 真的在公转 | 开局记下四颗行星的位置，等约 185 秒再看 | 行星一回到起点附近（误差几度以内）；四颗的相对周期比符合 1 : 1.627 : 3.060 : 19.305 | Task 4（步长/累积器）；若四颗都不动则是 Task 4 的注册 |
| **2** 站在地表不被甩掉 | 角色站在母星上，用 WASD 走一段、跳一下；抬头看别的星球 | 走动、跳跃、相机都正常；能看见别的星球在动 | Task 5（参考系跟随） |
| **3** 体素世界不重建 | 全程盯 Output Log，过滤 `重建` / `CreateWorld` / `DestroyWorld` | 一条都不出现；帧率稳定 | Task 2（参数哈希还在混 `ActorLocation`，或某颗没勾 `b Use Local Coordinates`） |
| **4** 能飞到并降落在另一颗 | 驾飞船从母星飞向行星二（开局相距约 2.65 km，`MaxSpeed = 8000 cm/s`，约 33 秒），降落，按 `E` | 能落在行星二表面，能离船行走 | 降落失败查 `ASpaceshipPawn::IsLanded`；离船失败查 `BoardingRadius` |
| **5** 离船后重力指向脚下这颗 | 离船后站着不动看 30 秒 | 角色稳稳站在**行星二**上 | **Task 7**（这是它最核心的验收点） |
| **6** 能飞回母星 | 从行星二起飞飞回母星，降落，按 `E` | 同样正常 | Task 7 |
| **7** 能量漂移 < 1e-5 | 看每 5 秒的诊断日志，跑够几分钟 | `能量漂移` 的绝对值长期 **< 1e-5** | Task 1（积分器）或 Task 4（`SetIntegrator` 没调） |
| **8** 无崩溃 / 无 ensure / 无 `LogVoxel` 错误 | 全程盯 Output Log 的 Warning / Error | 没有 | 按日志内容定位 |
| **9** 母星地形无回归 | 对比母星现在的样子与本次改动前（编辑器预览或改动前的截图） | 起伏、顶点色、材质**完全一致**，没有整体偏移、没有空洞 | **Task 2** —— 本次唯一动过 `AVoxelPlanetActor` 生成路径的改动就是 `bUseLocalCoordinates`，回归风险全在它身上 |

- [ ] **Step 2: 性能测量**

四颗行星 = 4 个体素世界，这是本次最大的性能风险（参考文档自己警告过「9 颗行星 = 9 个 Voxel World」）。

1. 在 PIE 里按 `~` 打开控制台，输入 `stat unit`。
2. 分别在三个位置各看 10 秒，记下 `Frame` 毫秒数：
   - 站在母星地表；
   - 悬停在母星上方 5 km（能看见全部四颗）；
   - 飞到行星四（52 km）附近。
3. **预期**：三处都稳定在 16.7 ms（60fps）以内，没有持续掉到 33 ms（30fps）以下。

若超标，**按这个顺序**降（每次只改一项，改完重测）：
1. 所有行星的 `Terrain` → `Amplitude`：80 → 40 → 20；
2. 非母星的 `VoxelThreads`：2 → 1；
3. `UCosmosSimulationSubsystem` 的 `StepsPerOrbit`：20000 → 10000（代价是轨道精度下降）。

- [ ] **Step 3: 若全部通过，不产生提交**

本任务没有源码改动。验收通过就在本计划文件里把上面两张表的复选框勾上，任务完成。

- [ ] **Step 4: 若有条目失败**

**不要开新任务。** 失败的条目归属哪一步，就回到那一步改，改完从 Step 1 重跑**整张表**（不是只重跑失败的那一行 —— 改动可能有连带影响）。提交用：

```bash
cd F:/workspace/ue/StellarEra
git add <明确路径>
git commit -m "fix: <具体问题>

<根因，不要只写现象>

Co-Authored-By: Claude Code <noreply@anthropic.com>"
```

---

## 完成标准

本计划完成的定义是：**spec §8 的九条全部通过**，且 Task 8 Step 2 的性能测量不超标。Tasks 1–7 各自的构建通过与关卡验证只是中间门槛。
