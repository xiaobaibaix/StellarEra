# 仿真标定拆分 M3a 实施计划（已废弃，被完整 M3 计划取代）

> **废弃原因（2026-11-22）**：实施到 Task 2 时发现仿真位置也是**渲染 AU**（`ToGameUnitPosition` 按 `CentimetersPerAu=1e6` 除），只把半径/探针切物理会让半径与距离差 1.5e7 倍，接触永不触发。标定拆分不能与写回映射分离——必须位置/速度/半径一起切物理，同时接远场压缩与代理。Task 1 的 `GetPhysicalRadiusCm` 与常量统一仍保留（安全、可复用）；Task 2 的提交已 `git revert`。新的完整计划另出。

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 让求解器里的半径、探针、接触门限用**真实物理厘米**（`CosmosUnits::AuToCentimeter`），而世界坐标输出继续用**渲染标定**（`CentimetersPerAu = 1e6`，1 AU = 10 km）。画面完全不变，但仿真回到真实尺度，为 M3b 的远场压缩铺路。

**Architecture:** 只改 `UCosmosSimulationSubsystem` 的换算口径与 `FUniverseMapping` 常量来源；`FNBodySimulator` 仍用渲染标定做 AU↔世界坐标。行星侧新增 `GetPhysicalRadiusCm()`（未缩放半径，不含 Actor 缩放）。

**Tech Stack:** UE 5.8 C++、UAutomationTest、UBT（捆绑 dotnet 直调）。

**Spec:** `docs/superpowers/specs/2026-11-22-universe-render-mapping-design.md`（Milestone 3 硬性要求）

## Global Constraints

- 代码进 `StellarEraCosmos` / `StellarEraPlanet`；不新增模块；含中文的 `.h`/`.cpp` 保持 UTF-8 BOM。
- 只改计划列出的文件；测试放 `Src/Tests/`，用例名带模块前缀。
- 构建前关闭编辑器；UBT 用捆绑 dotnet，cwd 在项目外（命令同前两份计划）。
- 自动化测试：
  ```bash
  "D:/Application/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" \
    "F:/workspace/ue/StellarEra/StellarEra.uproject" \
    -ExecCmds="Automation RunTests <过滤词>" \
    -TestExit="Automation Test Queue Empty" -unattended -nopause -nullrhi -nosplash \
    -stdout -abslog="F:/workspace/ue/StellarEra/Saved/<名字>.log"
  ```
- 提交带 pathspec。

---

### Task 1: 统一物理常量 + 行星物理半径 getter

**Files:**
- Modify: `Source/StellarEraCosmos/Src/UniverseMapping.h`
- Modify: `Source/StellarEraPlanet/Src/VoxelPlanetActor.h`
- Modify: `Source/StellarEraPlanet/Src/Tests/PlanetScaleDecouplingTests.cpp`
- Modify: `Source/StellarEraCosmos/Src/Tests/UniverseMappingTests.cpp`

**Interfaces:**
- Produces: `float AVoxelPlanetActor::GetPhysicalRadiusCm() const`（= `GetGeneratorRadiusVoxels() * VoxelSize`，不含 Actor 缩放）；`FUniverseMapping::PhysicalCmPerAu` 改为 `CosmosUnits::AuToCentimeter` 的别名（1.495978707e13）。

- [ ] **Step 1: 写失败测试**

在 `PlanetScaleDecouplingTests.cpp` 的用例末尾追加：

```cpp
	// 物理半径 = 未缩放体素半径 × 体素尺寸；缩放到 2 时不变。
	TestEqual(TEXT("物理半径不含缩放"), Planet->GetPhysicalRadiusCm(), 100.f * 100.f, 0.01f);
```

在 `UniverseMappingTests.cpp` 的 `Core` 用例里把两处 `1.496e13` 改成 `CosmosUnits::AuToCentimeter`，并追加：

```cpp
	TestTrue(TEXT("物理常量复用 CosmosUnits"),
		FUniverseMapping::PhysicalCmPerAu == CosmosUnits::AuToCentimeter);
```

（`UniverseMappingTests.cpp` 顶部加 `#include "CosmosTypes.h"`。）

- [ ] **Step 2: 跑测试确认失败**

构建后过滤 `StellarEra.Cosmos.UniverseMapping` 与 `StellarEra.Planet.Geo.ScaleDecoupling`。
Expected: 编译失败（`GetPhysicalRadiusCm` 不存在；常量不相等）。

- [ ] **Step 3: 写最小实现**

`VoxelPlanetActor.h` 在 `GetGeneratorRadiusVoxels` 下方加：

```cpp
	/** 物理半径（厘米）：生成器半径 × 体素尺寸，不含 Actor 缩放。仿真与接触门限用它。 */
	float GetPhysicalRadiusCm() const { return GetGeneratorRadiusVoxels() * VoxelSize; }
```

`UniverseMapping.h` 顶部加 `#include "CosmosTypes.h"`，并把常量改为：

```cpp
	/** 真实 1 AU 的厘米数；复用 CosmosUnits，避免第二份物理常量漂移。 */
	static constexpr double PhysicalCmPerAu = CosmosUnits::AuToCentimeter;
```

- [ ] **Step 4: 构建并跑测试确认通过**

过滤两个用例名，全部通过。

- [ ] **Step 5: 提交**

```bash
git add Source/StellarEraCosmos/Src/UniverseMapping.h Source/StellarEraCosmos/Src/Tests/UniverseMappingTests.cpp Source/StellarEraPlanet/Src/VoxelPlanetActor.h Source/StellarEraPlanet/Src/Tests/PlanetScaleDecouplingTests.cpp
git commit -m "refactor(宇宙): 物理常量复用 CosmosUnits，补齐行星物理半径" -- <同一组路径>
```

---

### Task 2: 子系统仿真侧换算切物理

**Files:**
- Modify: `Source/StellarEraCosmos/Src/CosmosSimulationSubsystem.cpp`
- Modify: `Source/StellarEraCosmos/Src/CosmosSimulationSubsystem.h`

**Interfaces:**
- Consumes: `GetPhysicalRadiusCm()`、`FUniverseMapping::PhysicalCmPerAu`。
- Produces: `Mapping.RenderCmPerAu` 在 `FinalizeRegistration` 里由 `CentimetersPerAu` 赋值。

- [ ] **Step 1: 常量联动**

在 `FinalizeRegistration()` 里 `SelectRenderAnchor()` 之前加：

```cpp
	// 映射的渲染标定必须跟配置同源，否则 M3b 的压缩阈值会与写回分叉。
	Mapping.RenderCmPerAu = CentimetersPerAu;
```

- [ ] **Step 2: 按审计表替换仿真侧换算**

| 行（约） | 现在 | 改成 |
|---|---|---|
| 128 | `Planet->GetSurfaceRadiusCm() / CentimetersPerAu` | `Planet->GetPhysicalRadiusCm() / FUniverseMapping::PhysicalCmPerAu` |
| 133 | `Ship->GetContactRadiusCm() / CentimetersPerAu` | `/ FUniverseMapping::PhysicalCmPerAu` |
| 193 | `SurfaceRadiusCm / CentimetersPerAu` | `/ FUniverseMapping::PhysicalCmPerAu`（`SurfaceRadiusCm` 来自 `GetSurfaceRadiusCmAtDirection`，本就是无缩放值） |
| 222 | `Ship->GetContactRadiusCm() / CentimetersPerAu` | `/ FUniverseMapping::PhysicalCmPerAu` |
| 241 | `... / CentimetersPerAu`（探针） | `/ FUniverseMapping::PhysicalCmPerAu` |
| 260 | `... / CentimetersPerAu`（惯量 AABB） | `/ FUniverseMapping::PhysicalCmPerAu` |
| 291-292 | `GetSurfaceRadiusCm() * 1.25 + ProbeRadius * CentimetersPerAu` / `Distance * CentimetersPerAu` | `GetPhysicalRadiusCm() * 1.25 + ProbeRadius * Physical` / `Distance * Physical` |
| 315 | `GetSurfaceRadiusCm() / VoxelSize` | `GetPhysicalRadiusCm() / VoxelSize`（体素量化用未缩放半径） |
| 334 | `GetSurfaceRadiusCm() / CentimetersPerAu` | `GetPhysicalRadiusCm() / Physical` |
| 376 | `SurfaceRadiusCm / CentimetersPerAu` | `/ Physical` |

保持渲染标定不动：`Simulator.SetCentimetersPerAu(CentimetersPerAu)`、`ToWorldVelocityScaled`、`CarryRiders` 的 `RideRadius`、诊断日志。

- [ ] **Step 3: 构建并跑 Cosmos 测试**

```bash
"<UE>/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "F:/workspace/ue/StellarEra/StellarEra.uproject" \
  -ExecCmds="Automation RunTests StellarEra.Cosmos" \
  -TestExit="Automation Test Queue Empty" -unattended -nopause -nullrhi -nosplash -stdout -abslog="F:/workspace/ue/StellarEra/Saved/um-m3a-cosmos.log"
```

Expected: 无新增失败。若有测试断言了旧的 `1e6` 口径下的半径值，按新口径更新断言并在提交信息里说明。

- [ ] **Step 4: 提交**

```bash
git add Source/StellarEraCosmos/Src/CosmosSimulationSubsystem.h Source/StellarEraCosmos/Src/CosmosSimulationSubsystem.cpp
git commit -m "refactor(宇宙): 仿真半径/探针/接触门限切真实物理标定" -- <两个路径>
```

---

### Task 3: 验证（画面不变）

- [ ] **Step 1: 全量测试**

`Automation RunTests StellarEra`，记录通过/失败；唯一允许的失败是预存在的 `StellarEra.Planet.Geo.Dependency`。

- [ ] **Step 2: PIE 冒烟**

启动编辑器 → `UniverseMap` → 播放：行星表面行走、飞船贴地飞行、登陆判定与 HUD 速度应与此前一致；无弹飞、无穿透、无定位漂移。

- [ ] **Step 3: 勾选并提交文档**

```bash
git add docs/superpowers/plans/2026-11-22-universe-calibration-split.md
git commit -m "docs(宇宙): M3a 标定拆分计划勾选" -- docs/superpowers/plans/2026-11-22-universe-calibration-split.md
```

---

## 后续

- **M3b**：写回走 `FUniverseMapping` 压缩远场、远场代理与体素显隐、切换淡入；需要另写计划。
