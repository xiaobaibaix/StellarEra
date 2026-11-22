# 真实尺度核心 M3-core 实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 在**功能开关默认关闭**的前提下，让求解器可以运行在真实 AU 尺度（位置、速度、半径、探针同一口径），并让写回经过 `FUniverseMapping` 做近场 1:1、远场压缩。开关关闭时行为与今天逐帧一致。

**Architecture:** 物理初始位置不再只能来自关卡摆放：`UCosmosOrbitComponent` 新增 `InitialPositionAu`（开启时覆盖摆放）。子系统新增 `bPhysicalScale`：开启时模拟器与全部仿真换算使用 `PhysicalCmPerAu`，世界速度仍用渲染标定；写回对当前天体用物理厘米、其余天体用 `RenderCmPerAu` 压缩。远场代理与启用属下一份计划。

**Tech Stack:** UE 5.8 C++、UAutomationTest、UBT（捆绑 dotnet 直调）。

**Spec:** `docs/superpowers/specs/2026-11-22-universe-render-mapping-design.md`

## Global Constraints

- 不新增模块；含中文的 `.h`/`.cpp` 保持 UTF-8 BOM；测试在 `Src/Tests/`，用例名带模块前缀。
- `bPhysicalScale` 默认 `false`；关闭时不得改变任何现有数值行为。
- 提交带 pathspec。
- 构建/测试命令同前几份计划（捆绑 dotnet 直调 UBT；`UnrealEditor-Cmd` 跑 `Automation RunTests`）。

---

### Task 1: 物理初始位置覆盖

**Files:**
- Modify: `Source/StellarEraCosmos/Src/CosmosOrbitComponent.h`
- Modify: `Source/StellarEraCosmos/Src/CosmosSimulationSubsystem.cpp`
- Modify: `Source/StellarEraCosmos/Src/UniverseMapping.h`
- Modify: `Source/StellarEraCosmos/Src/Tests/UniverseMappingTests.cpp`

**Interfaces:**
- Produces: `InitialPositionAu`（FVector）与 `bUseInitialPositionAu`（bool）；`FVector3d ResolveInitialPosition(const FVector3d& PlacementAu, const FVector& InitialAu, bool bUseInitialAu)`。

- [ ] **Step 1: 写失败测试**

在 `UniverseMappingTests.cpp` 追加：

```cpp
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraResolveInitialPositionTest,
	"StellarEra.Cosmos.UniverseMapping.InitialPosition",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FStellarEraResolveInitialPositionTest::RunTest(const FString& Parameters)
{
	const FVector3d Placement(1.0, 2.0, 3.0);
	const FVector Override(4.0, 5.0, 6.0);

	TestTrue(TEXT("未开启时用摆放"),
		ResolveInitialPosition(Placement, Override, false).Equals(Placement));
	TestTrue(TEXT("开启时用覆盖值"),
		ResolveInitialPosition(Placement, Override, true).Equals(FVector3d(Override)));

	return true;
}
```

- [ ] **Step 2: 跑测试确认失败**（`ResolveInitialPosition` 未定义）

- [ ] **Step 3: 写实现**

`UniverseMapping.h` 末尾：

```cpp
/** 物理初态：开启覆盖时用显式 AU 位置，否则沿用关卡摆放换算出的求解器位置。 */
inline FVector3d ResolveInitialPosition(const FVector3d& PlacementAu, const FVector& InitialAu, bool bUseInitialAu)
{
	return bUseInitialAu ? FVector3d(InitialAu) : PlacementAu;
}
```

`CosmosOrbitComponent.h` 在 `InitialVelocityKmPerSecond` 下方：

```cpp
	/**
	 * 物理初始位置（AU）。bUseInitialPositionAu 为真时覆盖关卡摆放；两者都只注册时读一次。
	 * 关卡摆放按渲染标定换算，只有这里填的值才是真实 AU。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cosmos|Body")
	FVector InitialPositionAu = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cosmos|Body")
	bool bUseInitialPositionAu = false;
```

`CosmosSimulationSubsystem.cpp` 注册处：

```cpp
	State.Position = ResolveInitialPosition(
		Simulator.ToGameUnitPosition(BodyActor->GetActorLocation()),
		Component->InitialPositionAu, Component->bUseInitialPositionAu);
```

- [ ] **Step 4: 构建并跑测试**（`StellarEra.Cosmos.UniverseMapping` 全过；`StellarEra.Cosmos` 无回归）

- [ ] **Step 5: 提交**

```bash
git add <4 个路径>
git commit -m "feat(宇宙): 天体可显式指定物理初始位置（AU）" -- <4 个路径>
```

---

### Task 2: bPhysicalScale 开关与仿真标定

**Files:**
- Modify: `Source/StellarEraCosmos/Src/CosmosSimulationSubsystem.h`
- Modify: `Source/StellarEraCosmos/Src/CosmosSimulationSubsystem.cpp`

**Interfaces:**
- Produces: `UPROPERTY(Config) bool bPhysicalScale = false;`；成员 `double ActiveCmPerAu`（初始化时 = `bPhysicalScale ? PhysicalCmPerAu : CentimetersPerAu`）。

- [ ] **Step 1: 加开关**

`CosmosSimulationSubsystem.h` 在 `CentimetersPerAu` 下方：

```cpp
	/** 开 = 求解器与全部仿真换算用真实物理 AU；关 = 沿用渲染标定（默认，行为不变）。 */
	UPROPERTY(Config, EditAnywhere, Category = "Cosmos|Scale")
	bool bPhysicalScale = false;

	/** 本帧仿真空间使用的 1 AU 厘米数：bPhysicalScale 决定是物理值还是渲染值。 */
	double ActiveCmPerAu = 1.0e6;
```

- [ ] **Step 2: 初始化与替换**

`FinalizeRegistration()` 开头：

```cpp
	ActiveCmPerAu = bPhysicalScale ? FUniverseMapping::PhysicalCmPerAu : CentimetersPerAu;
	Simulator.SetCentimetersPerAu(ActiveCmPerAu);
	Mapping.RenderCmPerAu = CentimetersPerAu;
```

把 `AddBodyToSimulator` 与 `RefreshPlanetContactRadii` / `RefreshShipContactProbes` 里用于**仿真空间**的 `CentimetersPerAu`（注册半径、接触半径、探针、惯量、门限）改为 `ActiveCmPerAu`；世界侧（`ToWorldVelocityScaled`、`CarryRiders`、诊断、配置校验）保持 `CentimetersPerAu`。用 grep 复核并在报告里列剩余归属。

- [ ] **Step 3: 构建并跑 Cosmos 测试**（开关默认关，28 例应无回归）

- [ ] **Step 4: 提交**

```bash
git add <2 个路径>
git commit -m "feat(宇宙): 物理尺度开关，仿真换算走 ActiveCmPerAu" -- <2 个路径>
```

---

### Task 3: 写回经映射（近场 1:1、远场压缩）

**Files:**
- Modify: `Source/StellarEraCosmos/Src/UniverseMapping.h`
- Modify: `Source/StellarEraCosmos/Src/Tests/UniverseMappingTests.cpp`
- Modify: `Source/StellarEraCosmos/Src/CosmosSimulationSubsystem.cpp`

**Interfaces:**
- Produces: `FVector ToRenderWorld(const FVector3d& BodyAu, const FVector3d& AnchorAu, const FVector3d& ShipAu, const FVector& AnchorWorld, const FVector& ShipWorld, bool bNear, double PhysicalCmPerAu, double RenderCmPerAu)`。

- [ ] **Step 1: 写失败测试**

```cpp
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraToRenderWorldTest,
	"StellarEra.Cosmos.UniverseMapping.RenderWorld",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FStellarEraToRenderWorldTest::RunTest(const FString& Parameters)
{
	const FVector AnchorWorld(100.f, 0.f, 0.f);
	const FVector ShipWorld(1.0e6f, 0.f, 0.f);
	const FVector3d AnchorAu = FVector3d::ZeroVector;
	const FVector3d ShipAu(6.684e-8, 0.0, 0.0); // 1e6 cm / Physical

	// 近场：相对锚点 × 物理标定。
	const FVector3d NearBody(1.0e-7, 0.0, 0.0);
	const FVector Near = ToRenderWorld(NearBody, AnchorAu, ShipAu, AnchorWorld, ShipWorld,
		true, 1.496e13, 1.0e6);
	TestTrue(TEXT("近场用物理标定"), Near.Equals(FVector(AnchorWorld + FVector3d(1.0e-7) * 1.496e13), 1.0));

	// 远场：相对飞船 × 渲染标定。
	const FVector3d FarBody(1.0, 0.0, 0.0);
	const FVector Far = ToRenderWorld(FarBody, AnchorAu, ShipAu, AnchorWorld, ShipWorld,
		false, 1.496e13, 1.0e6);
	TestTrue(TEXT("远场用渲染标定"), Far.Equals(FVector(ShipWorld + FVector3d(1.0 - 6.684e-8) * 1.0e6), 1.0));

	return true;
}
```

- [ ] **Step 2: 跑测试确认失败**

- [ ] **Step 3: 写实现**

`UniverseMapping.h` 末尾：

```cpp
/** 表现空间写回：近场（当前天体）按物理厘米 1:1，远场按渲染标定压缩，均以飞船为远场中心。 */
inline FVector ToRenderWorld(const FVector3d& BodyAu, const FVector3d& AnchorAu, const FVector3d& ShipAu,
	const FVector& AnchorWorld, const FVector& ShipWorld, bool bNear,
	double PhysicalCmPerAu, double RenderCmPerAu)
{
	return bNear
		? AnchorWorld + FVector((BodyAu - AnchorAu) * PhysicalCmPerAu)
		: ShipWorld + FVector((BodyAu - ShipAu) * RenderCmPerAu);
}
```

`CosmosSimulationSubsystem.cpp` 写回：把 `Simulator.ToWorldLocation(RenderPosition)` 换成经 `ToRenderWorld` 的结果；`bNear = (Component->SimulatorIndex == CurrentBodyIndex || BodyActor 实现 ICosmosBody)`。远场天体 `SetActorScale3D(FVector(RenderCmPerAu / PhysicalCmPerAu))`，近场 1。**仅当 `bPhysicalScale` 打开时走这条路径**；关闭时保持现有代码。

- [ ] **Step 4: 构建并跑 Cosmos 测试**（开关关：无回归；纯函数测试通过）

- [ ] **Step 5: 提交**

```bash
git add <3 个路径>
git commit -m "feat(宇宙): 写回经映射，近场 1:1 远场压缩（默认关）" -- <3 个路径>
```

---

## 下一份计划（不在本计划内）

- 远场代理（体素隐藏 + 廉价球/广告牌 + 切换淡入）。
- 在测试关卡打开 `bPhysicalScale` 与 `InitialPositionAu`，PIE 验证角直径与切换无跳变。
