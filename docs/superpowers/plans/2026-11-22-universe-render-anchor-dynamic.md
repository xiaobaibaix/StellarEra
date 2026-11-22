# 动态渲染原点 M2 实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 让渲染原点（世界坐标的固定钉）从“注册时选一次最重天体”改为“跟随飞船最近的当前天体”，切换时整世界平移一次，画面不跳。

**Architecture:** 复用 M1 的 `PickCurrentBody`（进入阈值 + 滞回）。每帧在写回前计算各天体到飞船的距离，选出当前天体；变化时把 `RenderAnchorWorld` 重新钉到新天体当前的世界位置，下一次 `UpdateRenderOrigin` + 写回会让所有天体（含飞船与挂载物）一起平移，相机在飞船上所以视角不变。星图模式下相机不在天体上，冻结切换。

**Tech Stack:** UE 5.8 C++、UAutomationTest、UBT（捆绑 dotnet 直调）。

**Spec:** `docs/superpowers/specs/2026-11-22-universe-render-mapping-design.md`

## Global Constraints

- 代码进 `StellarEraCosmos`，不新增模块；含中文的 `.h`/`.cpp` 必须 UTF-8 with BOM。
- 测试放 `Src/Tests/`，用例名带 `StellarEra.Cosmos.` 前缀；只改计划列出的文件。
- 构建前关闭编辑器；UBT 用捆绑 dotnet，cwd 在项目外：
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
    -unattended -nopause -nullrhi -nosplash -stdout -abslog="F:/workspace/ue/StellarEra/Saved/<名字>.log"
  ```
- 提交必须带 pathspec：`git commit -m "..." -- <paths>`。

---

### Task 1: 距离表辅助函数（纯函数 + 单测）

**Files:**
- Modify: `Source/StellarEraCosmos/Src/UniverseMapping.h`
- Modify: `Source/StellarEraCosmos/Src/Tests/UniverseMappingTests.cpp`

**Interfaces:**
- Consumes: 无。
- Produces: `TArray<double> SurfaceDistancesCm(const TArray<FVector3d>& Positions, const TArray<double>& RadiiAu, const FVector3d& ShipPositionAu, double CmPerAu, int32 ExcludeIndex = INDEX_NONE)`——返回与输入等长的距离数组（厘米）；无地表（`RadiiAu <= 0`）或被排除的下标为 `TNumericLimits<double>::Max()`。

- [ ] **Step 1: 写失败测试**

在 `UniverseMappingTests.cpp` 末尾追加：

```cpp
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraSurfaceDistancesTest,
	"StellarEra.Cosmos.UniverseMapping.SurfaceDistances",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FStellarEraSurfaceDistancesTest::RunTest(const FString& Parameters)
{
	const TArray<FVector3d> Positions = {
		FVector3d(1.0e-3, 0.0, 0.0),   // 0: 有地表，距飞船 1e-3 AU
		FVector3d(2.0e-3, 0.0, 0.0),   // 1: 无地表
		FVector3d(0.0, 0.0, 0.0)       // 2: 飞船自己，排除
	};
	const TArray<double> Radii = { 1.0e-7, 0.0, 1.0e-7 };
	const TArray<double> Distances = SurfaceDistancesCm(
		Positions, Radii, FVector3d::ZeroVector, 1.0e6, /*ExcludeIndex=*/2);

	TestEqual(TEXT("长度对齐"), Distances.Num(), 3);
	TestEqual(TEXT("有地表按距离换算"), Distances[0], 1.0e3, 1e-6);
	TestTrue(TEXT("无地表记为无穷"), Distances[1] == TNumericLimits<double>::Max());
	TestTrue(TEXT("排除下标记为无穷"), Distances[2] == TNumericLimits<double>::Max());

	return true;
}
```

- [ ] **Step 2: 跑测试确认失败**

构建后运行，过滤 `StellarEra.Cosmos.UniverseMapping.SurfaceDistances`。
Expected: 编译失败（`SurfaceDistancesCm` 未定义）。

- [ ] **Step 3: 写最小实现**

在 `UniverseMapping.h` 末尾追加：

```cpp
/** 各天体到飞船的距离（厘米）；无地表体与 ExcludeIndex 记无穷，永不参与锚点选择。 */
inline TArray<double> SurfaceDistancesCm(const TArray<FVector3d>& Positions, const TArray<double>& RadiiAu,
	const FVector3d& ShipPositionAu, double CmPerAu, int32 ExcludeIndex = INDEX_NONE)
{
	TArray<double> Out;
	Out.Reserve(Positions.Num());
	for (int32 Index = 0; Index < Positions.Num(); ++Index)
	{
		const bool bHasSurface = RadiiAu.IsValidIndex(Index) && RadiiAu[Index] > 0.0;
		Out.Add(bHasSurface && Index != ExcludeIndex
			? FVector3d(Positions[Index] - ShipPositionAu).Size() * CmPerAu
			: TNumericLimits<double>::Max());
	}
	return Out;
}
```

- [ ] **Step 4: 构建并跑测试确认通过**

过滤 `StellarEra.Cosmos.UniverseMapping`，三个用例（Core / CurrentBody / SurfaceDistances）全过。

- [ ] **Step 5: 提交**

```bash
git add Source/StellarEraCosmos/Src/UniverseMapping.h Source/StellarEraCosmos/Src/Tests/UniverseMappingTests.cpp
git commit -m "feat(宇宙): 渲染锚点用的天体距离表" -- Source/StellarEraCosmos/Src/UniverseMapping.h Source/StellarEraCosmos/Src/Tests/UniverseMappingTests.cpp
```

---

### Task 2: 写回前动态选择当前天体

**Files:**
- Modify: `Source/StellarEraCosmos/Src/CosmosSimulationSubsystem.h`
- Modify: `Source/StellarEraCosmos/Src/CosmosSimulationSubsystem.cpp`

**Interfaces:**
- Consumes: `SurfaceDistancesCm`、`PickCurrentBody`（Task 1 / M1）。
- Produces: `void UCosmosSimulationSubsystem::UpdateCurrentBody()`；成员 `int32 CurrentBodyIndex`、`FUniverseMapping Mapping`。

- [ ] **Step 1: 头文件加成员与方法**

在 `CosmosSimulationSubsystem.h` 的 `SelectRenderAnchor` 附近加：

```cpp
	/** 每帧按飞船距离重选当前天体；变化时把渲染原点重新钉到它的当前世界位置。 */
	void UpdateCurrentBody();
```

在 `RenderAnchorWorld` 下方加：

```cpp
	/** 当前天体（求解器下标）。与 RenderAnchorIndex 同步；星图模式下冻结。 */
	int32 CurrentBodyIndex = INDEX_NONE;

	/** 映射参数（M2 只用它的近场阈值；压缩在 M3 接）。 */
	FUniverseMapping Mapping;
```

并在文件顶部加 `#include "UniverseMapping.h"`。

- [ ] **Step 2: 实现 UpdateCurrentBody**

在 `CosmosSimulationSubsystem.cpp` 的 `UpdateRenderOrigin` 之后加：

```cpp
void UCosmosSimulationSubsystem::UpdateCurrentBody()
{
	// 星图相机不在天体上，重定位会让整片星空跳一次，冻结。
	if (bStarMapActive)
	{
		return;
	}

	const int32 NumBodies = Simulator.GetNumBodies();
	int32 ShipIndex = INDEX_NONE;
	for (int32 Index = 0; Index < NumBodies; ++Index)
	{
		AActor* Owner = Simulator.GetBody(Index).OwnerActor.Get();
		if (Owner && Cast<ICosmosBody>(Owner))
		{
			ShipIndex = Index;
			break;
		}
	}
	if (ShipIndex == INDEX_NONE)
	{
		return;
	}

	TArray<FVector3d> Positions;
	TArray<double> Radii;
	Positions.SetNum(NumBodies);
	Radii.SetNum(NumBodies);
	for (int32 Index = 0; Index < NumBodies; ++Index)
	{
		Positions[Index] = Simulator.GetBody(Index).Position;
		Radii[Index] = Simulator.GetBody(Index).Radius;
	}

	const TArray<double> Distances = SurfaceDistancesCm(
		Positions, Radii, Positions[ShipIndex], CentimetersPerAu, ShipIndex);
	const int32 Picked = PickCurrentBody(Distances, CurrentBodyIndex, Mapping);
	if (Picked == INDEX_NONE || Picked == CurrentBodyIndex)
	{
		return;
	}

	// 先按旧原点算出新天体的当前世界位置，钉住它；下一次 UpdateRenderOrigin + 写回会让
	// 其余天体（含飞船）一起平移同样的量，相机在飞船上所以视角不变。
	RenderAnchorWorld = Simulator.ToWorldLocation(Positions[Picked]);
	CurrentBodyIndex = Picked;
	RenderAnchorIndex = Picked;
	UE_LOG(LogTemp, Log, TEXT("[Cosmos] 渲染原点切换到天体 %d（%s）"),
		Picked, *GetNameSafe(Simulator.GetBody(Picked).OwnerActor.Get()));
}
```

在 `FinalizeRegistration` 末尾 `SelectRenderAnchor()` 之后加一行 `CurrentBodyIndex = RenderAnchorIndex;`。

在 `Tick` 里、`const FVector3d* RenderAnchorOverride = nullptr;` 之前调用 `UpdateCurrentBody();`。

- [ ] **Step 3: 构建并跑现有 Cosmos 测试**

```bash
"<UE>/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "F:/workspace/ue/StellarEra/StellarEra.uproject" \
  -ExecCmds="Automation RunTests StellarEra.Cosmos" \
  -TestExit="Automation Test Queue Empty" -unattended -nopause -nullrhi -nosplash -stdout -abslog="F:/workspace/ue/StellarEra/Saved/um-m2-cosmos.log"
```

Expected: 无新增失败（记录基线）。

- [ ] **Step 4: 提交**

```bash
git add Source/StellarEraCosmos/Src/CosmosSimulationSubsystem.h Source/StellarEraCosmos/Src/CosmosSimulationSubsystem.cpp
git commit -m "feat(宇宙): 渲染原点跟随当前天体，切换时整世界平移" -- Source/StellarEraCosmos/Src/CosmosSimulationSubsystem.h Source/StellarEraCosmos/Src/CosmosSimulationSubsystem.cpp
```

---

### Task 3: 实机验证（画面不跳、坐标有界）

**Files:**
- 无代码改动。

- [ ] **Step 1: PIE 飞行验证**

1. 关闭无头测试进程；用 schtasks `/it` 在 console 会话启动编辑器，打开 `UniverseMap`，播放。
2. 从初始行星飞向另一颗行星，跨过 `R_near ≈ 0.1 AU`。
3. 观察：日志出现「渲染原点切换到天体 N」；切换瞬间画面不跳；HUD 位置与速度连续。

Expected: 有切换日志、无视觉跳变、无 `World Memory Leaks` 或崩溃。

- [ ] **Step 2: 星图冻结验证**

进入星图模式，确认不出现「渲染原点切换」日志、星空不跳。

- [ ] **Step 3: 勾选计划并提交文档**

```bash
git add docs/superpowers/plans/2026-11-22-universe-render-anchor-dynamic.md
git commit -m "docs(宇宙): M2 动态渲染原点计划勾选" -- docs/superpowers/plans/2026-11-22-universe-render-anchor-dynamic.md
```

---

## 后续里程碑（另出计划）

- **M3**：仿真标定切 `PhysicalCmPerAu`（半径/探针/UI 同步），写回改走 `FUniverseMapping` 压缩远场；代理渲染。
- 远场代理与体素网格显隐、切换淡入。
