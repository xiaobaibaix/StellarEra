# 大尺度渲染映射 Milestone 1 实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 落地映射核心（纯函数 + 当前天体策略）并让体素星球的生成/LOD/缓存与 Actor 缩放脱钩，为 Milestone 2 的写回接线扫清重建与半径契约障碍。

**Architecture:** `FUniverseMapping` 是无 UObject、无状态的相似变换：近场恒等、远场乘 `FarScale`；`PickCurrentBody` 用近场阈值 + 滞回选当前天体。`AVoxelPlanetActor` 的生成器输入、参数哈希、LOD、缓存键、world size 一律改用未缩放 `Radius`，Actor 缩放只影响表现。

**Tech Stack:** UE 5.8 C++、UAutomationTest、UBT（捆绑 dotnet 直调）。

**Spec:** `docs/superpowers/specs/2026-11-22-universe-render-mapping-design.md`

## Global Constraints

- 引擎 5.8 / Win64；不新增模块，代码进 `StellarEraCosmos` 与 `StellarEraPlanet`。
- 含中文的 `.h`/`.cpp` 必须存 UTF-8 with BOM；属性/组件名 CamelCase，面板显示名不带空格/下划线/括号。
- 测试文件只能放 `Src/Tests/`，文件名与用例名都要带模块前缀。
- 构建前必须关闭编辑器；UBT 用捆绑 dotnet，cwd 在项目外：
  ```bash
  "D:/Application/Epic Games/UE_5.8/Engine/Binaries/ThirdParty/DotNet/10.0/win-x64/dotnet.exe" \
    "D:/Application/Epic Games/UE_5.8/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.dll" \
    StellarEraEditor Win64 Development \
    -Project="F:/workspace/ue/StellarEra/StellarEra.uproject" -WaitMutex -FromMsBuild
  ```
- 自动化测试：
  ```bash
  "D:/Application/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" \
    "F:/workspace/ue/StellarEra/StellarEra.uproject" \
    -ExecCmds="Automation RunTests <过滤词>" \
    -TestExit="Automation Test Queue Empty" \
    -unattended -nopause -nullrhi -nosplash -stdout -abslog="<临时日志>"
  ```
- 提交必须带 pathspec：`git commit -m "..." -- <paths>`，不许裸 `git commit`。

---

### Task 1: FUniverseMapping 相似变换与单元测试

**Files:**
- Create: `Source/StellarEraCosmos/Src/UniverseMapping.h`
- Test: `Source/StellarEraCosmos/Src/Tests/UniverseMappingTests.cpp`

**Interfaces:**
- Consumes: 无。
- Produces: `FUniverseMapping`（字段 `RenderCmPerAu`、`MinFarDistanceCm`；方法 `FarScale()`、`NearDistanceCm()`、`IsNear(double)`、`ScaleFor(bool)`、`ToRender(const FVector3d&, bool)`、`ToSim(const FVector&, bool)`；常量 `PhysicalCmPerAu`）。

- [ ] **Step 1: 写失败测试**

创建 `Source/StellarEraCosmos/Src/Tests/UniverseMappingTests.cpp`：

```cpp
#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "UniverseMapping.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraUniverseMappingTest,
	"StellarEra.Cosmos.UniverseMapping.Core",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FStellarEraUniverseMappingTest::RunTest(const FString& Parameters)
{
	const FUniverseMapping Mapping;

	TestEqual(TEXT("FarScale 由两套标定相除"),
		Mapping.FarScale(), Mapping.RenderCmPerAu / Mapping.PhysicalCmPerAu, 1e-18);
	TestTrue(TEXT("近场阈值满足 k·R_near = D_min"),
		FMath::IsNearlyEqual(Mapping.NearDistanceCm() * Mapping.FarScale(), Mapping.MinFarDistanceCm, 1.0));

	const FVector3d NearPoint(1.0e6, -2.0e6, 3.0e5);
	TestTrue(TEXT("近场位置恒等"), Mapping.ToRender(NearPoint, true).Equals(FVector(NearPoint)));
	TestEqual(TEXT("近场缩放为 1"), Mapping.ScaleFor(true), 1.0, 0.0);

	const double RealDistance = 1.496e13;
	const double BodyRadius = 6.96e10;
	TestFalse(TEXT("1 AU 走远场"), Mapping.IsNear(RealDistance));

	const FVector Rendered = Mapping.ToRender(FVector3d(RealDistance, 0.0, 0.0), false);
	const double RenderedRadius = BodyRadius * Mapping.ScaleFor(false);
	TestTrue(TEXT("远场角直径守恒"),
		FMath::IsNearlyEqual(RenderedRadius / Rendered.Size(), BodyRadius / RealDistance, 1e-12));

	const FVector3d FarPoint(3.0e12, 4.0e12, -1.0e12);
	TestTrue(TEXT("远场往返一致"),
		Mapping.ToSim(Mapping.ToRender(FarPoint, false), false).Equals(FarPoint, 1.0));

	return true;
}

#endif
```

- [ ] **Step 2: 跑测试确认失败**

先把测试加进构建（UBT 自动收集 `Src/Tests`，无需改 Build.cs），构建后运行：

```bash
"<UE>/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "F:/workspace/ue/StellarEra/StellarEra.uproject" \
  -ExecCmds="Automation RunTests StellarEra.Cosmos.UniverseMapping" \
  -TestExit="Automation Test Queue Empty" -unattended -nopause -nullrhi -nosplash -stdout -abslog="F:/workspace/ue/StellarEra/Saved/um-m1-task1.log"
```

Expected: 编译失败（`UniverseMapping.h` 不存在）。

- [ ] **Step 3: 写最小实现**

创建 `Source/StellarEraCosmos/Src/UniverseMapping.h`（UTF-8 with BOM）：

```cpp
#pragma once

#include "CoreMinimal.h"

/**
 * 大尺度渲染映射：仿真（物理 AU）→ 表现空间（世界厘米）的相似变换。
 * 无状态、不读 dt；近场恒等，远场统一乘 FarScale。
 */
struct STELLARERACOSMOS_API FUniverseMapping
{
	/** 真实 1 AU 的厘米数。仿真/物理换算用。 */
	static constexpr double PhysicalCmPerAu = 1.496e13;

	/** 表现空间标定：1 AU 渲染成多少世界厘米（沿用现有观感 10 km）。 */
	double RenderCmPerAu = 1.0e6;

	/** 远场最小可渲染距离（厘米）。k·d 低于它之前必须切回近场。 */
	double MinFarDistanceCm = 1.0e5;

	double FarScale() const { return RenderCmPerAu / PhysicalCmPerAu; }
	double NearDistanceCm() const { return MinFarDistanceCm / FarScale(); }
	bool IsNear(double DistanceCm) const { return DistanceCm <= NearDistanceCm(); }
	double ScaleFor(bool bNear) const { return bNear ? 1.0 : FarScale(); }

	FVector ToRender(const FVector3d& RelativeCm, bool bNear) const
	{
		return bNear ? FVector(RelativeCm) : FVector(RelativeCm * FarScale());
	}

	FVector3d ToSim(const FVector& RelativeScene, bool bNear) const
	{
		return bNear ? FVector3d(RelativeScene) : FVector3d(RelativeScene) / FarScale();
	}
};
```

- [ ] **Step 4: 构建并跑测试确认通过**

构建（Global Constraints 里的 UBT 命令），然后：

```bash
"<UE>/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "F:/workspace/ue/StellarEra/StellarEra.uproject" \
  -ExecCmds="Automation RunTests StellarEra.Cosmos.UniverseMapping" \
  -TestExit="Automation Test Queue Empty" -unattended -nopause -nullrhi -nosplash -stdout -abslog="F:/workspace/ue/StellarEra/Saved/um-m1-task1.log"
```

Expected: `StellarEra.Cosmos.UniverseMapping.Core` 通过。

- [ ] **Step 5: 提交**

```bash
git add Source/StellarEraCosmos/Src/UniverseMapping.h Source/StellarEraCosmos/Src/Tests/UniverseMappingTests.cpp
git commit -m "feat(宇宙): 大尺度渲染映射核心（近场恒等/远场相似）" -- Source/StellarEraCosmos/Src/UniverseMapping.h Source/StellarEraCosmos/Src/Tests/UniverseMappingTests.cpp
```

---

### Task 2: 当前天体选择（阈值 + 滞回）

**Files:**
- Modify: `Source/StellarEraCosmos/Src/UniverseMapping.h`
- Modify: `Source/StellarEraCosmos/Src/Tests/UniverseMappingTests.cpp`

**Interfaces:**
- Consumes: `FUniverseMapping::NearDistanceCm()`。
- Produces: `int32 PickCurrentBody(const TArray<double>& DistancesCm, int32 PreviousIndex, const FUniverseMapping&, double ExitScale = 2.0)`；返回天体下标或 `INDEX_NONE`。

- [ ] **Step 1: 写失败测试**

在 `UniverseMappingTests.cpp` 末尾追加：

```cpp
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraUniverseCurrentBodyTest,
	"StellarEra.Cosmos.UniverseMapping.CurrentBody",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FStellarEraUniverseCurrentBodyTest::RunTest(const FString& Parameters)
{
	const FUniverseMapping Mapping;
	const double Enter = Mapping.NearDistanceCm();
	const double Exit = Enter * 2.0;

	TestEqual(TEXT("没有天体"), PickCurrentBody({}, INDEX_NONE, Mapping), INDEX_NONE);
	TestEqual(TEXT("最近且在近场内"), PickCurrentBody({ Enter * 0.5, Enter * 3.0 }, INDEX_NONE, Mapping), 0);
	TestEqual(TEXT("全部在近场外"), PickCurrentBody({ Enter * 3.0, Enter * 4.0 }, INDEX_NONE, Mapping), INDEX_NONE);

	// 滞回：已选中天体在 [Enter, Exit] 之间时不丢，出了 Exit 才丢。
	TestEqual(TEXT("滞回区间保留"), PickCurrentBody({ Enter * 1.5, Enter * 9.0 }, 0, Mapping), 0);
	TestEqual(TEXT("超出滞回区间"), PickCurrentBody({ Enter * 2.5, Enter * 9.0 }, 0, Mapping), INDEX_NONE);

	return true;
}
```

- [ ] **Step 2: 跑测试确认失败**

同 Task 1 的命令，过滤 `StellarEra.Cosmos.UniverseMapping`。
Expected: 编译失败（`PickCurrentBody` 未定义）。

- [ ] **Step 3: 写最小实现**

在 `UniverseMapping.h` 末尾、`FUniverseMapping` 之外追加：

```cpp
/** 当前天体：最近且进入近场；已选中时用 ExitScale 放宽阈值，避免边界抖动。 */
inline int32 PickCurrentBody(const TArray<double>& DistancesCm, int32 PreviousIndex,
	const FUniverseMapping& Mapping, double ExitScale = 2.0)
{
	int32 Nearest = INDEX_NONE;
	double NearestDistance = TNumericLimits<double>::Max();
	for (int32 Index = 0; Index < DistancesCm.Num(); ++Index)
	{
		if (DistancesCm[Index] < NearestDistance)
		{
			NearestDistance = DistancesCm[Index];
			Nearest = Index;
		}
	}
	if (Nearest == INDEX_NONE)
	{
		return INDEX_NONE;
	}

	const bool bKeepPrevious = PreviousIndex != INDEX_NONE && DistancesCm.IsValidIndex(PreviousIndex);
	const double Threshold = bKeepPrevious
		? Mapping.NearDistanceCm() * ExitScale
		: Mapping.NearDistanceCm();

	return NearestDistance <= Threshold ? Nearest : INDEX_NONE;
}
```

- [ ] **Step 4: 构建并跑测试确认通过**

过滤 `StellarEra.Cosmos.UniverseMapping`，两个用例都过。

- [ ] **Step 5: 提交**

```bash
git add Source/StellarEraCosmos/Src/UniverseMapping.h Source/StellarEraCosmos/Src/Tests/UniverseMappingTests.cpp
git commit -m "feat(宇宙): 当前天体选择带滞回阈值" -- Source/StellarEraCosmos/Src/UniverseMapping.h Source/StellarEraCosmos/Src/Tests/UniverseMappingTests.cpp
```

---

### Task 3: 体素星球与 Actor 缩放脱钩

**Files:**
- Modify: `Source/StellarEraPlanet/Src/VoxelPlanetActor.h:325`（新增 getter）
- Modify: `Source/StellarEraPlanet/Src/VoxelPlanetActor.cpp`（9 处半径来源）
- Test: `Source/StellarEraPlanet/Src/Tests/PlanetScaleDecouplingTests.cpp`

**Interfaces:**
- Consumes: 无。
- Produces: `float AVoxelPlanetActor::GetGeneratorRadiusVoxels() const`（= `Radius`，不含 Actor 缩放）。

- [ ] **Step 1: 写失败测试**

创建 `Source/StellarEraPlanet/Src/Tests/PlanetScaleDecouplingTests.cpp`：

```cpp
#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Components/SceneComponent.h"
#include "VoxelPlanetActor.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraPlanetScaleDecouplingTest,
	"StellarEra.Planet.Geo.ScaleDecoupling",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FStellarEraPlanetScaleDecouplingTest::RunTest(const FString& Parameters)
{
	AVoxelPlanetActor* Planet = NewObject<AVoxelPlanetActor>(GetTransientPackage());
	if (!TestNotNull(TEXT("行星 Actor"), Planet))
	{
		return false;
	}
	if (!Planet->GetRootComponent())
	{
		Planet->SetRootComponent(NewObject<USceneComponent>(Planet));
	}

	Planet->Radius = 100.f;
	Planet->VoxelSize = 100.f;
	Planet->SetActorScale3D(FVector(1.0));
	const float BaseRadiusCm = Planet->GetSurfaceRadiusCm();
	const int32 BaseLOD = Planet->ComputeOrbitLOD();

	Planet->SetActorScale3D(FVector(2.0));
	TestEqual(TEXT("地表半径随缩放线性变化（不是 S²）"),
		Planet->GetSurfaceRadiusCm(), BaseRadiusCm * 2.f, 1.f);
	TestEqual(TEXT("LOD 不随缩放变化"), Planet->ComputeOrbitLOD(), BaseLOD);
	TestEqual(TEXT("生成器半径不含缩放"), Planet->GetGeneratorRadiusVoxels(), 100.f, 0.f);

	return true;
}

#endif
```

- [ ] **Step 2: 跑测试确认失败**

过滤 `StellarEra.Planet.Geo.ScaleDecoupling`。Expected: 编译失败（`GetGeneratorRadiusVoxels` 不存在）。

- [ ] **Step 3: 新增 getter**

在 `VoxelPlanetActor.h` 的 `float GetScaledRadiusVoxels() const;`（约 325 行）下方加：

```cpp
	/** 生成器/哈希/LOD/缓存用的半径：不含 Actor 缩放。 */
	float GetGeneratorRadiusVoxels() const { return Radius; }
```

- [ ] **Step 4: 把 9 处半径来源改为未缩放**

在 `VoxelPlanetActor.cpp` 中，把下列位置的 `GetScaledRadiusVoxels()` 改成 `GetGeneratorRadiusVoxels()`：

| 行（改前） | 上下文 |
|---|---|
| 212 | `ComputeOrbitLOD` 的 `ScaledRadius` |
| 224 | `RefreshLODInvoker` 的 `RangeSource` |
| 742 | `AutoLODRadius` |
| 776 | `Recipe->ToConfig(...)` |
| 784 | `CacheRadius` |
| 852 | `PendingCacheHash` 的半径参数 |
| 867 | `GeoSnapshot->R0` 比较 |
| 895 | `SetWorldSize(...)` |
| `ComputeParamsHash` 内 | `const float ScaledRadius = ...; Mix(&ScaledRadius, ...)` |

`ComputeParamsHash` 里的注释同步改成「混未缩放半径：Actor 缩放只影响表现，不该触发重建」。

`GetSurfaceRadiusCm()`（1240 行）**保持** `GetScaledRadiusVoxels() * VoxelSize`：它是世界半径，必须含缩放。

- [ ] **Step 5: 构建并跑测试确认通过**

先构建，再过滤 `StellarEra.Planet.Geo.ScaleDecoupling`；然后跑一遍现有地质测试确认没退化：

```bash
"<UE>/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "F:/workspace/ue/StellarEra/StellarEra.uproject" \
  -ExecCmds="Automation RunTests StellarEra.Planet.Geo" \
  -TestExit="Automation Test Queue Empty" -unattended -nopause -nullrhi -nosplash -stdout -abslog="F:/workspace/ue/StellarEra/Saved/um-m1-task3-geo.log"
```

Expected: 新用例通过，`StellarEra.Planet.Geo.*` 无新增失败。

- [ ] **Step 6: 提交**

```bash
git add Source/StellarEraPlanet/Src/VoxelPlanetActor.h Source/StellarEraPlanet/Src/VoxelPlanetActor.cpp Source/StellarEraPlanet/Src/Tests/PlanetScaleDecouplingTests.cpp
git commit -m "fix(体素): 生成/LOD/缓存与 Actor 缩放脱钩，缩放不再触发重建" -- Source/StellarEraPlanet/Src/VoxelPlanetActor.h Source/StellarEraPlanet/Src/VoxelPlanetActor.cpp Source/StellarEraPlanet/Src/Tests/PlanetScaleDecouplingTests.cpp
```

---

### Task 4: 全量验证与对照

**Files:**
- 无代码改动。

- [ ] **Step 1: 跑全量自动化测试**

```bash
"<UE>/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "F:/workspace/ue/StellarEra/StellarEra.uproject" \
  -ExecCmds="Automation RunTests StellarEra" \
  -TestExit="Automation Test Queue Empty" -unattended -nopause -nullrhi -nosplash -stdout -abslog="F:/workspace/ue/StellarEra/Saved/um-m1-all.log"
```

Expected: 与改动前相比无新增失败（记录基线数量）。

- [ ] **Step 2: 编辑器内人工对照（缩放不重建）**

1. 关闭无头测试进程，用 schtasks `/it` 在 console 会话启动编辑器（不在 SSH 会话直接开）。
2. 打开 `UniverseMap`，选中 `BP_VoxelPlanet3`，把 Scale 从 1 改成 2。
3. 观察输出日志：不应出现体素重建/烘焙行；`GetSurfaceRadiusCm` 应翻倍。
4. 改回 1。

Expected: 无重建日志，星球只是视觉上变大。

- [ ] **Step 3: 勾选计划并提交文档**

把本计划文件的复选框按实际结果勾选，然后：

```bash
git add docs/superpowers/plans/2026-11-22-universe-render-mapping-m1.md
git commit -m "docs(宇宙): M1 映射核心实施计划勾选" -- docs/superpowers/plans/2026-11-22-universe-render-mapping-m1.md
```

---

## Milestone 2（另出计划，不在本计划内）

- 仿真长度标定切到 `PhysicalCmPerAu`（半径、接触探针、`ToWorldGameUnits` 全部同步）。
- `UCosmosSimulationSubsystem` 写回改走 `FUniverseMapping`，`RenderAnchorIndex` 改为随当前天体动态切换。
- 远场代理（廉价球/广告牌）与体素网格的显隐切换。
- 视觉验证：1 AU 外的恒星角直径与真实一致、接近时无跳变。
