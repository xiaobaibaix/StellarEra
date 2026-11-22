# 启用前置 M4a 实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 在打开 `bPhysicalScale` 之前闭合三个已裁定的前置：飞船游戏量跟仿真标定同口径、`CurrentBodyIndex` 能清空（否则近/远分类永久粘住）、编辑器轨迹预览能拿到物理标定。全部在开关关闭时行为不变。

**Architecture:** 只改 `UCosmosSimulationSubsystem` 与 `UCosmosTrajectoryComponent`。游戏量（推力、速度写回）改用 `ActiveCmPerAu`；`UpdateCurrentBody` 在 `PickCurrentBody` 返回 `INDEX_NONE` 时清空当前天体但保留渲染原点；编辑器的标定来源回落到子系统 CDO。

**Tech Stack:** UE 5.8 C++、UAutomationTest、UBT（捆绑 dotnet 直调）。

**Spec:** `docs/superpowers/specs/2026-11-22-universe-render-mapping-design.md`

## Global Constraints

- 不新增模块；含中文的 `.h`/`.cpp` 保持 UTF-8 BOM；测试在 `Src/Tests/`。
- `bPhysicalScale` 默认 `false`；关闭时数值行为不变（`ActiveCmPerAu == CentimetersPerAu`）。
- 提交带 pathspec；构建/测试命令同前几份计划。

---

### Task 1: 飞船游戏量跟 ActiveCmPerAu

**Files:**
- Modify: `Source/StellarEraCosmos/Src/CosmosSimulationSubsystem.cpp`

**Interfaces:**
- Consumes: `ActiveCmPerAu`（M3-core）。
- Produces: `ToWorldVelocityScaled` 与 `ApplyControlledBodyForces` 的推力换算使用 `ActiveCmPerAu`。

- [x] **Step 1: 替换两个换算点**

`ToWorldVelocityScaled`：

```cpp
	return FVector(CosmosUnits::GameVelocityToWorldCmPerSecond(
		GameUnitVelocity, ActiveCmPerAu, TimeScale));
```

`ApplyControlledBodyForces` 的推力：

```cpp
		Body.ExternalAcceleration = FVector3d(CosmosUnits::WorldCmPerS2ToGameAcceleration(
			FVector3d(Ship->GetThrustAccelerationWorldCmS2(DeltaSeconds)),
			ActiveCmPerAu, TimeScale));
```

在 `ToWorldVelocityScaled` 上方补一行注释：游戏量（速度/推力）跟随仿真标定，开关关闭时与渲染标定逐位相同。

- [x] **Step 2: 构建并跑 Cosmos 测试**

`Automation RunTests StellarEra.Cosmos`，30 例无回归。

- [x] **Step 3: 提交**

```bash
git add Source/StellarEraCosmos/Src/CosmosSimulationSubsystem.cpp
git commit -m "fix(宇宙): 飞船速度/推力跟 ActiveCmPerAu，开启物理尺度后不差 1.5e7" -- Source/StellarEraCosmos/Src/CosmosSimulationSubsystem.cpp
```

---

### Task 2: CurrentBodyIndex 可清空

**Files:**
- Modify: `Source/StellarEraCosmos/Src/CosmosSimulationSubsystem.cpp`

**Interfaces:**
- 行为变化：`PickCurrentBody` 返回 `INDEX_NONE` 时 `CurrentBodyIndex = INDEX_NONE`（所有天体转远场）；`RenderAnchorIndex`/`RenderAnchorWorld` 保留，世界原点仍有界。

- [x] **Step 1: 改 UpdateCurrentBody 的分支**

把：

```cpp
	const int32 Picked = PickCurrentBody(Distances, CurrentBodyIndex, Mapping);
	if (Picked == INDEX_NONE || Picked == CurrentBodyIndex)
	{
		return;
	}
```

改成：

```cpp
	const int32 Picked = PickCurrentBody(Distances, CurrentBodyIndex, Mapping);
	if (Picked == CurrentBodyIndex)
	{
		return;
	}
	if (Picked == INDEX_NONE)
	{
		// 离开所有天体的退出带：全部转远场；原点仍钉在上一颗上，只为保持世界坐标有界。
		CurrentBodyIndex = INDEX_NONE;
		return;
	}
```

- [x] **Step 2: 构建并跑 Cosmos 测试**（30 例无回归；`PickCurrentBody` 已有单测）

- [x] **Step 3: 提交**

```bash
git add Source/StellarEraCosmos/Src/CosmosSimulationSubsystem.cpp
git commit -m "fix(宇宙): 离开近场后清空当前天体，近远分类不再永久粘住" -- Source/StellarEraCosmos/Src/CosmosSimulationSubsystem.cpp
```

---

### Task 3: 编辑器预览标定从 CDO 读

**Files:**
- Modify: `Source/StellarEraCosmos/Src/CosmosTrajectoryComponent.cpp`

**Interfaces:**
- `CmPerAu` 与 `Softening` 在编辑器世界（无子系统实例）回落到 `GetDefault<UCosmosSimulationSubsystem>()`，并考虑 `bPhysicalScale`。

- [x] **Step 1: 替换标定读取**

把：

```cpp
	double CmPerAu = 1.0e6;
	double Softening = 0.0001;
	if (const UCosmosSimulationSubsystem* Subsystem =
		World->GetSubsystem<UCosmosSimulationSubsystem>())
	{
		CmPerAu = Subsystem->CentimetersPerAu;
		Softening = Subsystem->SofteningLength;
	}
```

改成：

```cpp
	const UCosmosSimulationSubsystem* Simulation =
		World->GetSubsystem<UCosmosSimulationSubsystem>();
	if (!Simulation)
	{
		Simulation = GetDefault<UCosmosSimulationSubsystem>();
	}
	double CmPerAu = Simulation->bPhysicalScale
		? FUniverseMapping::PhysicalCmPerAu
		: Simulation->CentimetersPerAu;
	double Softening = Simulation->SofteningLength;
```

（`GetDefault` 在类已注册时非空；若担心可加判空。）

- [x] **Step 2: 构建并跑 Cosmos 测试**（30 例无回归）

- [x] **Step 3: 提交**

```bash
git add Source/StellarEraCosmos/Src/CosmosTrajectoryComponent.cpp
git commit -m "fix(宇宙): 编辑器轨迹预览回落到 CDO 标定，支持物理尺度" -- Source/StellarEraCosmos/Src/CosmosTrajectoryComponent.cpp
```

---

### Task 4: 验证

- [x] **Step 1: 全量测试**：`Automation RunTests StellarEra`；唯一允许失败是预存在的 `StellarEra.Planet.Geo.Dependency`。

- [x] **Step 2: 勾选并提交文档**

```bash
git add docs/superpowers/plans/2026-11-22-universe-enable-preconditions.md
git commit -m "docs(宇宙): M4a 启用前置计划勾选" -- docs/superpowers/plans/2026-11-22-universe-enable-preconditions.md
```

---

## 下一份计划（M4b，不在本计划内）

- 远场代理（体素隐藏 + 廉价球/广告牌 + 切换淡入）。
- 打开 `bPhysicalScale` + `InitialPositionAu` 的测试关卡与 PIE 验证（角直径、切换无跳变、HUD/拖曳/预测）。
- 开启路径的自动化测试。
