# 行星编辑：落笔合并 + 队列轮转 实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 让「短时间狂点挖掘」不再把同一片区域扫 N 遍（同类型且包围盒相交的落笔合并成一笔），并让队列里多笔编辑同时推进（队首优先 + 余量轮转）。

**Architecture:** `FPlanetProgressiveEdit` 从「一个球」升级成「一个 op 列表 + 一块接着一块执行」；合并只给新区域切块并插在已做前缀之后，进度不丢。组件侧把「只推队首」的 `TickProgressiveEdits` 拆成「队首吃 70% 预算、余量按跨帧游标轮转」两段。全部改动落在 `StellarEraPlanetEdit` 模块，不碰 `Plugins/VoxelFree`。

**Tech Stack:** Unreal Engine 5.8.3 / Win64 / C++，VoxelFree 插件（只读），UE Automation Framework。

**Spec:** [docs/superpowers/specs/2026-10-31-planet-edit-merge-round-robin-design.md](../specs/2026-10-31-planet-edit-merge-round-robin-design.md)

## Global Constraints

- **不动 `Plugins/VoxelFree/`、`Plugins/VibeUE/`。** 只改 `Source/StellarEraPlanetEdit/` 与 `Source/StellarEra/`。
- **含中文的 `.h`/`.cpp` 必须是 UTF-8 with BOM**（`head -c 3 <file> | xxd` 应打印 `ef bb bf`）。改动既有文件不要动首三个字节；编辑工具保留原字节即可。
- **不要统一换行符。** 仓库是 CRLF/LF 混合，`autocrlf=true` 下 CRLF 是稳定态。
- **构建前必须关闭编辑器**（Live Coding 会锁 DLL）。不用 `Build.bat`（它的锁机制会让 UBT 报「找不到 .uproject」）。
- **提交时 pathspec 挂在 commit 上**：`git commit -m "..." -- <paths>`。本仓库索引里有常驻暂存文件，绝不用裸 `git commit`，也不要 `git add` 之后裸提交。
- **数值约定**：`MaxMergedOps` 默认 **8**；`ProgressiveEditHeadBudgetShare` 默认 **0.7**（ClampMin 0 / ClampMax 1）；`ProgressiveEditChunkSizeVoxels` 保持 **128**；`ProgressiveEditBudgetMs` 保持 **4.0**。
- **命名**：Epic 命名法（`A`/`U`/`F`/`E`/`I`/`T` 前缀，布尔 `bXxx`，UObject 指针 `TObjectPtr<>`）。
- **不改**笔刷尺寸区间、存档时机、撤销/重做。

### 两条命令（后面每一步都用它们）

构建（**先关编辑器**，cwd 必须在项目外）：

```bash
cd /f/workspace/ue && \
"D:/Application/Epic Games/UE_5.8/Engine/Binaries/ThirdParty/DotNet/10.0/win-x64/dotnet.exe" \
  "D:/Application/Epic Games/UE_5.8/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.dll" \
  StellarEraEditor Win64 Development \
  -Project="F:/workspace/ue/StellarEra/StellarEra.uproject" -WaitMutex -FromMsBuild
```

子集跑测试（编辑器开着也能跑；最后一次跑全量时去掉 `.Planet.Editor` 后缀）：

```bash
MSYS_NO_PATHCONV=1 MSYS2_ARG_CONV_EXCL='*' \
"D:/Application/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" \
  "F:/workspace/ue/StellarEra/StellarEra.uproject" \
  -ExecCmds="Automation RunTests StellarEra.Planet.Editor" \
  -TestExit="Automation Test Queue Empty" \
  -unattended -nopause -nullrhi -nosplash -stdout \
  -abslog="F:/workspace/ue/StellarEra/Saved/Logs/automation-plan-edit.log"
```

日志尾部应有 `... tests, ... Success`，且新增的测试名逐条出现。**失败时先看日志里 `LogAutomationTest: Error:` 行**，别去看引擎自带 `UE::UnifiedErrorTest` 的那几条（它们是引擎自检输出，与本项目无关）。

## File Structure

| 文件 | 职责 | 本计划中的改动 |
|---|---|---|
| `Source/StellarEraPlanetEdit/Public/PlanetProgressiveEdit.h` | 分块编辑的类型与 job | 新增 `FPlanetEditOp` / `FPlanetEditChunk` / `FPlanetEditRequest` / `PlanetEditSchedule`；job 改为 op 列表 + 块带 `NumOps` |
| `Source/StellarEraPlanetEdit/Private/PlanetProgressiveEdit.cpp` | job 实现 | `MakeRequest` / `MakeOp` / `SetupFromRequest` / `ApplyChunk` / `TryMerge` / `TryMergeWorld` |
| `Source/StellarEraPlanetEdit/Private/PlanetProgressiveEditKernel.h` + `.cpp` | 逐格谓词（与插件同式） | 新增 `EvaluateOps` |
| `Source/StellarEraPlanetEdit/Public/PlanetEditBrush.h` + `Private/PlanetEditBrush.cpp` | 笔刷几何纯函数 | 新增 `VoxelBoxesIntersect` |
| `Source/StellarEraPlanetEdit/Public/PlanetEditComponent.h` | 编辑组件 | 新增 `ProgressiveEditHeadBudgetShare` / `MaxMergedOps` / `NextFairEditIndex` |
| `Source/StellarEraPlanetEdit/Private/PlanetEditComponent.cpp` | 组件调度 | 两段式 tick、换星前 flush、队列路径先尝试合并 |
| `Source/StellarEraPlanetEdit/Private/PlanetProgressiveEditTests.cpp` | 纯几何 / 策略单测 | 新增 7 条测试 |
| `Source/StellarEra/Private/PlanetEditTests.cpp` | 笔刷纯函数 + CDO 默认值 | 新增 `VoxelBoxesIntersect` 测试、两条默认值断言 |

**测试策略说明（必须先读，别自己发明夹具）：**

本模块现有的自动化测试**全部是纯几何 / 纯策略**，没有任何一条会创建真实体素世界：
`PlanetProgressiveEditTests.cpp` 比对的是我们的内核函数与插件 inline 实现的逐格同值，
`PlanetEditTests.cpp` 覆盖笔刷数学与 CDO 默认值。`AVoxelPlanetActor` 需要一个带
Voxel LOD 管理器的 UWorld 才能 `IsCreated()`，无头测试里建不起来。

因此本计划把「合并的正确性」拆成两层来钉：

1. **策略层**（可自动化）：`FPlanetEditRequest` 是纯数据，`SetupFromRequest` / `TryMerge`
   都不碰行星。合并决策、切块、插入位置全部能被无头测试直接驱动。
2. **执行层**（自动化模拟）：`Merge.ResultEqualsSequential` 按运行期真实顺序
   （从 `GetNextChunkIndex()` 起逐块、块内逐格按 `NumOps` 过 op）在一张合成网格上
   模拟执行，与「依次执行两次独立编辑」逐格比对。唯一没被覆盖的是
   `FVoxelData::ParallelSet` 那一行管道调用 —— 它是插件代码，且 `EvaluateOps`
   已被单独钉住。

**不要**为了让某个测试跑起来去 `SpawnActor<AVoxelPlanetActor>`：`IsCreated()` 过不了，
测试会静默退化成「返回 false 就通过」的假绿。

---

## Task 1: 逐格多 op 求值内核 `EvaluateOps`

**Files:**
- Modify: `Source/StellarEraPlanetEdit/Public/PlanetProgressiveEdit.h`（新增两个 struct）
- Modify: `Source/StellarEraPlanetEdit/Private/PlanetProgressiveEditKernel.h`（声明）
- Modify: `Source/StellarEraPlanetEdit/Private/PlanetProgressiveEditKernel.cpp`（实现）
- Test: `Source/StellarEraPlanetEdit/Private/PlanetProgressiveEditTests.cpp`

**Interfaces:**
- Consumes: `PlanetProgressiveKernel::EvaluateSphereValue` / `EvaluateBoxValue` / `SquaredDistanceTo` / `GetSphereThresholds`（均已存在）；`PlanetEditBrush::MakeSphereEditVoxelBox` / `MakeBox` / `MakeVoxelBox`（已存在）
- Produces:
  - `struct FPlanetEditOp { EPlanetBrushShape Shape; FVector CenterVoxels; float RadiusVoxels; float SquaredRadiusPlus2; float SquaredRadiusMinus2; FPlanetEditVoxelBox Bounds; };`
  - `struct FPlanetEditChunk { FPlanetEditVoxelBox Bounds; int32 NumOps; };`
  - `FVoxelValue PlanetProgressiveKernel::EvaluateOps(const FPlanetEditOp* Ops, int32 NumOps, int32 X, int32 Y, int32 Z, FVoxelValue CurrentValue, bool bPlace);`

- [ ] **Step 1: 在 `PlanetProgressiveEdit.h` 里加两个 struct**

插在 `class AVoxelPlanetActor;` 那行之后、`FPlanetProgressiveEdit` 的文档注释之前：

```cpp
/**
 * 一次落笔的完整参数。
 *
 * **op 只追加、不修改**：一个执行块只认自己创建时的那个前缀（见
 * FPlanetEditChunk::NumOps），靠的就是这条不变式。
 */
struct FPlanetEditOp
{
	EPlanetBrushShape Shape = EPlanetBrushShape::Sphere;
	/** 体素空间（double）。球是球心；方块是体素中心。 */
	FVector CenterVoxels = FVector::ZeroVector;
	float RadiusVoxels = 0.f;
	float SquaredRadiusPlus2 = 0.f;
	float SquaredRadiusMinus2 = 0.f;
	/** 本 op 自己的包围盒（右端开）。方块的贴边判定用的就是它。 */
	FPlanetEditVoxelBox Bounds;
};

/** 一个执行块：只应用 Ops[0 .. NumOps-1]。 */
struct FPlanetEditChunk
{
	FPlanetEditVoxelBox Bounds;
	int32 NumOps = 0;
};
```

- [ ] **Step 2: 写失败的测试**

在 `PlanetProgressiveEditTests.cpp` 的 `#endif` 之前追加。该文件顶部已有
`VoxelValue.h` / `PlanetEditBrush.h` / `PlanetProgressiveEditKernel.h` 的 include；
**还需补一行** `#include "PlanetProgressiveEdit.h"`（放在 `#include "PlanetEditBrush.h"` 之后）。

```cpp
// 单 op 的 EvaluateOps 必须与直接调 EvaluateSphereValue / EvaluateBoxValue 逐格同值。
//
// 这条钉的是重构的第一步：把「逐格求值」从 ApplyChunk 里抽出来时不改任何一格的结果。
// 位置用一个非整数的球心，好让 ±2 边界落在格子中间，边界两侧都被走到。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraProgressiveOpsKernelTest,
	"StellarEra.Planet.Editor.Kernel.OpsMatchSingle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraProgressiveOpsKernelTest::RunTest(const FString& Parameters)
{
	const FVoxelValue Seeds[] = {
		FVoxelValue::Empty(), FVoxelValue::Full(),
		FVoxelValue{ 0.3f }, FVoxelValue{ -0.7f } };

	// --- 球 ---
	const float Radius = 6.f;
	float Plus2 = 0.f;
	float Minus2 = 0.f;
	PlanetProgressiveKernel::GetSphereThresholds(Radius, Plus2, Minus2);

	FPlanetEditOp SphereOp;
	SphereOp.Shape = EPlanetBrushShape::Sphere;
	SphereOp.CenterVoxels = FVector(1.5, -2.25, 3.75);
	SphereOp.RadiusVoxels = Radius;
	SphereOp.SquaredRadiusPlus2 = Plus2;
	SphereOp.SquaredRadiusMinus2 = Minus2;
	SphereOp.Bounds = PlanetEditBrush::MakeSphereEditVoxelBox(SphereOp.CenterVoxels, Radius);

	int32 SphereMismatch = 0;
	for (int32 X = SphereOp.Bounds.Min.X; X < SphereOp.Bounds.MaxExclusive.X; ++X)
	{
		for (int32 Y = SphereOp.Bounds.Min.Y; Y < SphereOp.Bounds.MaxExclusive.Y; ++Y)
		{
			for (int32 Z = SphereOp.Bounds.Min.Z; Z < SphereOp.Bounds.MaxExclusive.Z; ++Z)
			{
				const float SquaredDistance = PlanetProgressiveKernel::SquaredDistanceTo(
					SphereOp.CenterVoxels, X, Y, Z);

				for (int32 SeedIndex = 0; SeedIndex < UE_ARRAY_COUNT(Seeds); ++SeedIndex)
				{
					for (int32 PlaceIndex = 0; PlaceIndex < 2; ++PlaceIndex)
					{
						const bool bPlace = PlaceIndex != 0;
						const FVoxelValue Direct = PlanetProgressiveKernel::EvaluateSphereValue(
							Radius, Plus2, Minus2, SquaredDistance, Seeds[SeedIndex], bPlace);
						const FVoxelValue ThroughOp = PlanetProgressiveKernel::EvaluateOps(
							&SphereOp, 1, X, Y, Z, Seeds[SeedIndex], bPlace);

						if (Direct != ThroughOp)
						{
							++SphereMismatch;
						}
					}
				}
			}
		}
	}
	TestEqual(TEXT("单 op 的球求值与 EvaluateSphereValue 逐格同值"), SphereMismatch, 0);

	// --- 方块：贴边判定读的是 Op.Bounds，单 op 时就是整笔编辑的盒子 ---
	FPlanetEditOp BoxOp;
	BoxOp.Shape = EPlanetBrushShape::Cube;
	BoxOp.CenterVoxels = FVector(2.0, 2.0, 2.0);
	BoxOp.Bounds = PlanetEditBrush::MakeVoxelBox(
		PlanetEditBrush::MakeBox(BoxOp.CenterVoxels, 6, 1, 64));

	int32 BoxMismatch = 0;
	for (int32 X = BoxOp.Bounds.Min.X; X < BoxOp.Bounds.MaxExclusive.X; ++X)
	{
		for (int32 Y = BoxOp.Bounds.Min.Y; Y < BoxOp.Bounds.MaxExclusive.Y; ++Y)
		{
			for (int32 Z = BoxOp.Bounds.Min.Z; Z < BoxOp.Bounds.MaxExclusive.Z; ++Z)
			{
				for (int32 SeedIndex = 0; SeedIndex < UE_ARRAY_COUNT(Seeds); ++SeedIndex)
				{
					for (int32 PlaceIndex = 0; PlaceIndex < 2; ++PlaceIndex)
					{
						const bool bPlace = PlaceIndex != 0;
						const FVoxelValue Direct = PlanetProgressiveKernel::EvaluateBoxValue(
							FIntVector(X, Y, Z), BoxOp.Bounds.Min, BoxOp.Bounds.MaxExclusive,
							Seeds[SeedIndex], bPlace);
						const FVoxelValue ThroughOp = PlanetProgressiveKernel::EvaluateOps(
							&BoxOp, 1, X, Y, Z, Seeds[SeedIndex], bPlace);

						if (Direct != ThroughOp)
						{
							++BoxMismatch;
						}
					}
				}
			}
		}
	}
	TestEqual(TEXT("单 op 的方块求值与 EvaluateBoxValue 逐格同值"), BoxMismatch, 0);

	// --- 两个 op：必须等于「先应用 op0、再应用 op1」 ---
	FPlanetEditOp TwoOps[2] = { SphereOp, SphereOp };
	TwoOps[1].CenterVoxels = FVector(4.0, 0.0, 0.0);
	TwoOps[1].Bounds = PlanetEditBrush::MakeSphereEditVoxelBox(TwoOps[1].CenterVoxels, Radius);

	int32 OrderMismatch = 0;
	for (int32 X = TwoOps[0].Bounds.Min.X; X < TwoOps[0].Bounds.MaxExclusive.X; ++X)
	{
		for (int32 Y = TwoOps[0].Bounds.Min.Y; Y < TwoOps[0].Bounds.MaxExclusive.Y; ++Y)
		{
			for (int32 Z = TwoOps[0].Bounds.Min.Z; Z < TwoOps[0].Bounds.MaxExclusive.Z; ++Z)
			{
				for (int32 PlaceIndex = 0; PlaceIndex < 2; ++PlaceIndex)
				{
					const bool bPlace = PlaceIndex != 0;
					FVoxelValue Expected = PlanetProgressiveKernel::EvaluateSphereValue(
						Radius, Plus2, Minus2,
						PlanetProgressiveKernel::SquaredDistanceTo(TwoOps[0].CenterVoxels, X, Y, Z),
						FVoxelValue::Full(), bPlace);
					Expected = PlanetProgressiveKernel::EvaluateSphereValue(
						Radius, Plus2, Minus2,
						PlanetProgressiveKernel::SquaredDistanceTo(TwoOps[1].CenterVoxels, X, Y, Z),
						Expected, bPlace);

					const FVoxelValue ThroughOps = PlanetProgressiveKernel::EvaluateOps(
						TwoOps, 2, X, Y, Z, FVoxelValue::Full(), bPlace);
					if (Expected != ThroughOps)
					{
						++OrderMismatch;
					}
				}
			}
		}
	}
	TestEqual(TEXT("两个 op 必须按顺序依次生效"), OrderMismatch, 0);

	return true;
}
```

- [ ] **Step 3: 跑测试确认它失败**

跑子集命令。期望：编译失败，最先出现的是 `error: use of undeclared identifier 'EvaluateOps'`
（`FPlanetEditOp` 在 Step 1 已加进去）。

- [ ] **Step 4: 在 `PlanetProgressiveEditKernel.h` 加声明**

在 `SquaredDistanceTo` 声明之前插入。**该头文件目前不 include 任何项目头**，
需要补 `#include "PlanetProgressiveEdit.h"`（放在 `#include "VoxelValue.h"` 之后）：

```cpp
	/**
	 * 对一格按顺序应用 Ops[0 .. NumOps-1]，返回最终值。
	 *
	 * 顺序就是语义：方块笔刷的贴边判定读「当前值」，所以必须与依次执行
	 * NumOps 次独立编辑逐格完全一致。
	 *
	 * 传裸指针 + 数量而不是 TArray：调用方在 ParallelSet 的 lambda 里用，
	 * 那里不能碰可能被重分配的容器。
	 */
	FVoxelValue EvaluateOps(
		const FPlanetEditOp* Ops,
		int32 NumOps,
		int32 X, int32 Y, int32 Z,
		FVoxelValue CurrentValue,
		bool bPlace);
```

- [ ] **Step 5: 在 `PlanetProgressiveEditKernel.cpp` 加实现**

追加到 `namespace PlanetProgressiveKernel { ... }` 内部（闭合大括号之前）：

```cpp
	FVoxelValue EvaluateOps(
		const FPlanetEditOp* const Ops, const int32 NumOps,
		const int32 X, const int32 Y, const int32 Z,
		FVoxelValue CurrentValue, const bool bPlace)
	{
		if (Ops == nullptr)
		{
			return CurrentValue;
		}

		for (int32 Index = 0; Index < NumOps; ++Index)
		{
			const FPlanetEditOp& Op = Ops[Index];
			if (Op.Shape == EPlanetBrushShape::Sphere)
			{
				CurrentValue = EvaluateSphereValue(
					Op.RadiusVoxels, Op.SquaredRadiusPlus2, Op.SquaredRadiusMinus2,
					SquaredDistanceTo(Op.CenterVoxels, X, Y, Z), CurrentValue, bPlace);
			}
			else
			{
				CurrentValue = EvaluateBoxValue(
					FIntVector(X, Y, Z), Op.Bounds.Min, Op.Bounds.MaxExclusive,
					CurrentValue, bPlace);
			}
		}

		return CurrentValue;
	}
```

- [ ] **Step 6: 跑测试确认通过**

跑子集命令。期望日志里出现 `StellarEra.Planet.Editor.Kernel.OpsMatchSingle ... Success`，
末尾 `0 Fail`（引擎自检的 `UnifiedErrorTest` Error 行不算）。

- [ ] **Step 7: 检查 BOM**

```bash
cd /f/workspace/ue/StellarEra && \
head -c 3 Source/StellarEraPlanetEdit/Public/PlanetProgressiveEdit.h | xxd && \
head -c 3 Source/StellarEraPlanetEdit/Private/PlanetProgressiveEditKernel.h | xxd && \
head -c 3 Source/StellarEraPlanetEdit/Private/PlanetProgressiveEditKernel.cpp | xxd
```
三条都必须是 `ef bb bf`。

- [ ] **Step 8: 提交**

```bash
cd /f/workspace/ue/StellarEra && git commit -m "feat(行星编辑): 多 op 逐格求值内核

把「逐格应用一次落笔」抽成 EvaluateOps，为落笔合并铺路。
单 op 路径与既有 EvaluateSphereValue / EvaluateBoxValue 逐格同值。

Co-Authored-By: Claude Code <noreply@anthropic.com>" -- \
  Source/StellarEraPlanetEdit/Public/PlanetProgressiveEdit.h \
  Source/StellarEraPlanetEdit/Private/PlanetProgressiveEditKernel.h \
  Source/StellarEraPlanetEdit/Private/PlanetProgressiveEditKernel.cpp \
  Source/StellarEraPlanetEdit/Private/PlanetProgressiveEditTests.cpp
```

---

## Task 2: job 改成 op 列表，`Setup` 拆出纯几何入口

这个任务**不改变任何运行期行为**：仍然是一个 op、一套包围盒、一样的块数。
它只是把「世界 → 体素」的换算与「建 op / 切块」分开，好让 Task 3 的合并
能在没有行星的情况下被测试。

**Files:**
- Modify: `Source/StellarEraPlanetEdit/Public/PlanetProgressiveEdit.h`
- Modify: `Source/StellarEraPlanetEdit/Private/PlanetProgressiveEdit.cpp`
- Test: `Source/StellarEraPlanetEdit/Private/PlanetProgressiveEditTests.cpp`

**Interfaces:**
- Consumes: `FPlanetEditOp` / `FPlanetEditChunk` / `EvaluateOps`（Task 1）
- Produces:
  - `struct FPlanetEditRequest`（字段见下）
  - `bool FPlanetProgressiveEdit::SetupFromRequest(const FPlanetEditRequest& Request)`
  - `int32 GetOpCount() const`、`int32 GetNextChunkIndex() const`
  - `const TArray<FPlanetEditOp>& GetOps() const`、`const TArray<FPlanetEditChunk>& GetChunks() const`
  - `Setup(...)` 签名**不变**

- [ ] **Step 1: 写失败的测试**

追加到 `PlanetProgressiveEditTests.cpp` 的 `#endif` 之前：

```cpp
// 装配的纯几何入口：没有行星也要能用同一套公式算出包围盒与块。
//
// 钉两件事：
//  1) 包围盒 / 块数必须与笔刷的那两个函数逐分量一致（不能有第二套公式）；
//  2) 每个块的 NumOps 都等于当时已有的 op 数 —— 这是「块只认自己创建时的前缀」
//     这条不变式的第一次落地。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraProgressiveJobSetupTest,
	"StellarEra.Planet.Editor.Job.SetupFromRequest",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraProgressiveJobSetupTest::RunTest(const FString& Parameters)
{
	FPlanetEditRequest Sphere;
	Sphere.Shape = EPlanetBrushShape::Sphere;
	Sphere.CenterVoxels = FVector(10.0, -4.0, 7.0);
	Sphere.RadiusVoxels = 6.f;
	Sphere.SizeVoxels = 10;
	Sphere.MinSizeVoxels = 1;
	Sphere.MaxSizeVoxels = 64;
	Sphere.bPlace = false;
	Sphere.bMultiThreaded = false;
	Sphere.ChunkSizeVoxels = 4;

	TSharedPtr<FPlanetProgressiveEdit> Job = MakeShared<FPlanetProgressiveEdit>();
	if (!TestTrue(TEXT("球装配成功"), Job->SetupFromRequest(Sphere)))
	{
		return false;
	}

	const FPlanetEditVoxelBox ExpectedBounds =
		PlanetEditBrush::MakeSphereEditVoxelBox(Sphere.CenterVoxels, Sphere.RadiusVoxels);

	TestEqual(TEXT("一个 op"), Job->GetOpCount(), 1);
	TestTrue(TEXT("整笔包围盒与笔刷同一个函数同值"),
		Job->GetFullBounds().Min == ExpectedBounds.Min
		&& Job->GetFullBounds().MaxExclusive == ExpectedBounds.MaxExclusive);
	TestTrue(TEXT("op 的包围盒就是整笔的包围盒"),
		Job->GetOps()[0].Bounds.Min == ExpectedBounds.Min
		&& Job->GetOps()[0].Bounds.MaxExclusive == ExpectedBounds.MaxExclusive);

	TArray<FPlanetEditVoxelBox> ExpectedChunks;
	PlanetEditBrush::SplitVoxelBoxIntoChunks(
		ExpectedBounds, Sphere.ChunkSizeVoxels, ExpectedChunks);
	TestEqual(TEXT("块数与切块函数一致"), Job->GetChunkCount(), ExpectedChunks.Num());
	TestEqual(TEXT("还没开始推"), Job->GetNextChunkIndex(), 0);
	TestFalse(TEXT("还没做完"), Job->IsFinished());

	int32 WrongPrefix = 0;
	for (const FPlanetEditChunk& Chunk : Job->GetChunks())
	{
		if (Chunk.NumOps != 1)
		{
			++WrongPrefix;
		}
	}
	TestEqual(TEXT("只有一个 op 时每个块的前缀都是 1"), WrongPrefix, 0);

	// --- 方块：包围盒走 MakeBox + MakeVoxelBox，半径留 0 ---
	FPlanetEditRequest Cube = Sphere;
	Cube.Shape = EPlanetBrushShape::Cube;
	Cube.RadiusVoxels = 0.f;
	Cube.CenterVoxels = FVector(2.0, 2.0, 2.0);
	Cube.SizeVoxels = 6;

	TSharedPtr<FPlanetProgressiveEdit> CubeJob = MakeShared<FPlanetProgressiveEdit>();
	if (!TestTrue(TEXT("方块装配成功"), CubeJob->SetupFromRequest(Cube)))
	{
		return false;
	}

	const FPlanetEditVoxelBox ExpectedCubeBounds = PlanetEditBrush::MakeVoxelBox(
		PlanetEditBrush::MakeBox(Cube.CenterVoxels, Cube.SizeVoxels,
			Cube.MinSizeVoxels, Cube.MaxSizeVoxels));
	TestTrue(TEXT("方块包围盒与笔刷同一个函数同值"),
		CubeJob->GetFullBounds().Min == ExpectedCubeBounds.Min
		&& CubeJob->GetFullBounds().MaxExclusive == ExpectedCubeBounds.MaxExclusive);
	TestEqual(TEXT("方块没有半径"), CubeJob->GetOps()[0].RadiusVoxels, 0.f, 0.001f);

	// --- 再装配一次必须整体重置：旧 op / 旧块不能残留 ---
	FPlanetEditRequest Again = Sphere;
	Again.CenterVoxels = FVector(1000.0, 0.0, 0.0);
	if (TestTrue(TEXT("重新装配成功"), Job->SetupFromRequest(Again)))
	{
		TestEqual(TEXT("重新装配后 op 数回到 1"), Job->GetOpCount(), 1);
		TestTrue(TEXT("重新装配后包围盒换到了新位置"),
			Job->GetFullBounds().Min.X > ExpectedBounds.MaxExclusive.X);
		TestEqual(TEXT("重新装配后进度归零"), Job->GetNextChunkIndex(), 0);
	}

	return true;
}
```

- [ ] **Step 2: 跑测试确认它失败**

跑子集命令。期望编译失败：`FPlanetEditRequest` 未定义、`SetupFromRequest` 不是成员。

- [ ] **Step 3: 替换 `PlanetProgressiveEdit.h` 里的类型与类**

在 Task 1 加的两个 struct **之后**、`FPlanetProgressiveEdit` 的文档注释**之前**插入：

```cpp
/**
 * 一次落笔的请求：全是体素空间的纯数据。
 *
 * 存在的理由：生产路径要先经过行星做「世界 → 体素」换算，而合并判定本身
 * 只跟体素空间有关。把两者拆开，合并就能在无头测试里被直接驱动。
 */
struct FPlanetEditRequest
{
	EPlanetBrushShape Shape = EPlanetBrushShape::Sphere;
	FVector CenterVoxels = FVector::ZeroVector;
	/** 只有球用；方块填 0。 */
	float RadiusVoxels = 0.f;
	int32 SizeVoxels = 1;
	int32 MinSizeVoxels = 1;
	int32 MaxSizeVoxels = 1;
	bool bPlace = false;
	bool bMultiThreaded = true;
	int32 ChunkSizeVoxels = 128;
};
```

然后把 `class STELLARERAPLANETEDIT_API FPlanetProgressiveEdit { ... };` 的**整个类体**
替换成下面这份（老的 `CenterVoxels` / `RadiusVoxels` / `SquaredRadius±2` 成员移到 op 里了）：

```cpp
class STELLARERAPLANETEDIT_API FPlanetProgressiveEdit
{
public:
	FPlanetProgressiveEdit() = default;

	/**
	 * 世界空间入口：换算成体素后交给 SetupFromRequest。
	 * 行星无效 / 未创建、包围盒退化时返回 false 且状态清零。
	 */
	bool Setup(
		AVoxelPlanetActor* InPlanet,
		const FVector& CenterWorld,
		EPlanetBrushShape InShape,
		int32 SizeVoxels,
		int32 MinSizeVoxels,
		int32 MaxSizeVoxels,
		bool bInPlace,
		int32 ChunkSizeVoxels,
		bool bMultiThreaded);

	/**
	 * 纯几何装配：建第一个 op 并切块。
	 *
	 * 公开只为一件事：自动化测试要在没有体素世界的情况下构造 job
	 *（AVoxelPlanetActor 需要一个带 Voxel LOD 管理器的 UWorld 才 IsCreated()）。
	 * 生产路径一律走 Setup()。
	 */
	bool SetupFromRequest(const FPlanetEditRequest& Request);

	/**
	 * 应用下一块。返回 true = 还有剩余块；false = 已完成（或行星已失效）。
	 * 每块内部开一个只覆盖该块的写锁，锁的持有时间因此与块大小成正比，
	 * 而不是与整笔编辑成正比。
	 */
	bool ApplyNextChunk();

	/** 一次跑完剩余块，返回是否真的改到了体素。存档 / 退出前的安全点用。 */
	bool ApplyAll();

	bool IsFinished() const { return NextChunkIndex >= Chunks.Num(); }
	int32 GetChunkCount() const { return Chunks.Num(); }
	int32 GetNextChunkIndex() const { return NextChunkIndex; }
	int32 GetOpCount() const { return Ops.Num(); }

	/** 已合并的落笔与切块。块的 NumOps 是创建时的稳定前缀。测试与诊断用。 */
	const TArray<FPlanetEditOp>& GetOps() const { return Ops; }
	const TArray<FPlanetEditChunk>& GetChunks() const { return Chunks; }

	/** 笔刷尺寸（体素数）。日志用。 */
	int32 GetSizeVoxels() const { return SizeVoxels; }

	/**
	 * 有没有任何一格的值真的变了。
	 *
	 * 由写入谓词逐格比对旧值得到（线程安全）。比「中心格前后是否相同」准：
	 * 大笔刷挖到的常常是外圈而中心格本来就已经是空气。
	 */
	bool DidChangeAnything() const { return bChangedAnything.load(std::memory_order_relaxed); }

	/** 已应用的块的并集（体素空间，右端开区间）。无效 = 一块都没做。 */
	const FPlanetEditVoxelBox& GetAppliedBounds() const { return AppliedBounds; }

	/** 最近一次 ApplyNextChunk 真正写的范围。逐块刷渲染用。 */
	const FPlanetEditVoxelBox& GetLastChunkBounds() const { return LastChunkBounds; }

	/** 整笔编辑的完整包围盒（所有 op 的并集）。 */
	const FPlanetEditVoxelBox& GetFullBounds() const { return FullBounds; }

	TWeakObjectPtr<AVoxelPlanetActor> GetPlanet() const { return Planet; }

private:
	/** 世界 → 体素，与插件同一套换算。Setup 与（后续的）TryMergeWorld 共用。 */
	static bool MakeRequest(
		AVoxelPlanetActor* InPlanet,
		const FVector& CenterWorld,
		EPlanetBrushShape InShape,
		int32 SizeVoxels,
		int32 MinSizeVoxels,
		int32 MaxSizeVoxels,
		bool bInPlace,
		int32 ChunkSizeVoxels,
		bool bMultiThreaded,
		FPlanetEditRequest& OutRequest);

	/** 请求 → op（包围盒 + 球阈值）。纯函数。 */
	static FPlanetEditOp MakeOp(const FPlanetEditRequest& Request);

	/** 清空一切状态（不含 Planet）。失败路径与装配开头共用。 */
	void ResetState();

	/** 开写锁 + ParallelSet，写一块。 */
	void ApplyChunk(const FPlanetEditChunk& Chunk);

	TWeakObjectPtr<AVoxelPlanetActor> Planet;

	EPlanetBrushShape Shape = EPlanetBrushShape::Sphere;
	bool bPlace = false;
	bool bMultiThreaded = true;
	int32 SizeVoxels = 1;

	/** op 只追加、不修改 —— 块的前缀因此永远稳定。 */
	TArray<FPlanetEditOp> Ops;
	TArray<FPlanetEditChunk> Chunks;
	int32 NextChunkIndex = 0;

	FPlanetEditVoxelBox FullBounds;
	FPlanetEditVoxelBox AppliedBounds;
	FPlanetEditVoxelBox LastChunkBounds;

	/** 多线程写入，只能用原子量。FThreadSafeBool 在 5.8 已标记弃用。 */
	std::atomic<bool> bChangedAnything{ false };
};
```

- [ ] **Step 4: 替换 `PlanetProgressiveEdit.cpp` 的 `Setup`**

把现有 `bool FPlanetProgressiveEdit::Setup(...) { ... }`（整个函数，从 `Planet.Reset();`
到最后的 `return true;`）替换成下面四个函数，**位置就在原来的 Setup 处**：

```cpp
void FPlanetProgressiveEdit::ResetState()
{
	Ops.Reset();
	Chunks.Reset();
	NextChunkIndex = 0;
	FullBounds = FPlanetEditVoxelBox();
	AppliedBounds = FPlanetEditVoxelBox();
	LastChunkBounds = FPlanetEditVoxelBox();
	bChangedAnything.store(false, std::memory_order_relaxed);
}

FPlanetEditOp FPlanetProgressiveEdit::MakeOp(const FPlanetEditRequest& Request)
{
	FPlanetEditOp Op;
	Op.Shape = Request.Shape;
	Op.CenterVoxels = Request.CenterVoxels;
	Op.RadiusVoxels = Request.RadiusVoxels;

	if (Request.Shape == EPlanetBrushShape::Sphere)
	{
		PlanetProgressiveKernel::GetSphereThresholds(
			Request.RadiusVoxels, Op.SquaredRadiusPlus2, Op.SquaredRadiusMinus2);
		Op.Bounds = PlanetEditBrush::MakeSphereEditVoxelBox(
			Request.CenterVoxels, Request.RadiusVoxels);
	}
	else
	{
		// 与 PlanetBrushApplicator 同一套：先按四舍五入的体素中心取两端都含的盒，
		// 再换成右端开。方块没有半径的概念，上面留的就是 0。
		Op.SquaredRadiusPlus2 = 0.f;
		Op.SquaredRadiusMinus2 = 0.f;
		const FPlanetEditBox Box = PlanetEditBrush::MakeBox(
			Request.CenterVoxels, Request.SizeVoxels,
			Request.MinSizeVoxels, Request.MaxSizeVoxels);
		Op.Bounds = PlanetEditBrush::MakeVoxelBox(Box);
	}

	return Op;
}

bool FPlanetProgressiveEdit::MakeRequest(
	AVoxelPlanetActor* const InPlanet,
	const FVector& CenterWorld,
	const EPlanetBrushShape InShape,
	const int32 InSizeVoxels,
	const int32 InMinSizeVoxels,
	const int32 InMaxSizeVoxels,
	const bool bInPlace,
	const int32 ChunkSizeVoxels,
	const bool bInMultiThreaded,
	FPlanetEditRequest& OutRequest)
{
	if (!IsValid(InPlanet) || !InPlanet->IsCreated())
	{
		return false;
	}

	// 世界 → 体素：与 UVoxelSphereTools 内部同一个函数（GetRealPosition 就是
	// VoxelWorld->GlobalToLocalFloat），不自己除 VoxelSize —— 那会漏掉缩放。
	const FVoxelVector RealPosition =
		FVoxelToolHelpers::GetRealPosition(InPlanet, CenterWorld, /*bConvertToVoxelSpace=*/true);

	OutRequest = FPlanetEditRequest();
	OutRequest.Shape = InShape;
	OutRequest.CenterVoxels = FVector(RealPosition.X, RealPosition.Y, RealPosition.Z);
	OutRequest.SizeVoxels = InSizeVoxels;
	OutRequest.MinSizeVoxels = InMinSizeVoxels;
	OutRequest.MaxSizeVoxels = InMaxSizeVoxels;
	OutRequest.bPlace = bInPlace;
	OutRequest.bMultiThreaded = bInMultiThreaded;
	OutRequest.ChunkSizeVoxels = ChunkSizeVoxels;

	if (InShape == EPlanetBrushShape::Sphere)
	{
		// 半径和包围盒都按插件那一套算：半径先夹到区间的半边长（厘米），
		// 再除 VoxelSize 换到体素。
		const float VoxelSize = FMath::Max(InPlanet->VoxelSize, 1.f);
		const float RadiusCm = PlanetEditBrush::GetHalfExtentCm(
			InSizeVoxels, VoxelSize, InMinSizeVoxels, InMaxSizeVoxels);
		OutRequest.RadiusVoxels = FVoxelToolHelpers::GetRealDistance(
			InPlanet, RadiusCm, /*bConvertToVoxelSpace=*/true);
	}
	else
	{
		OutRequest.RadiusVoxels = 0.f;
	}

	return true;
}

bool FPlanetProgressiveEdit::SetupFromRequest(const FPlanetEditRequest& Request)
{
	ResetState();
	Shape = Request.Shape;
	bPlace = Request.bPlace;
	bMultiThreaded = Request.bMultiThreaded;
	SizeVoxels = Request.SizeVoxels;

	const FPlanetEditOp Op = MakeOp(Request);
	if (!PlanetEditBrush::IsValidVoxelBox(Op.Bounds))
	{
		return false;
	}

	TArray<FPlanetEditVoxelBox> Boxes;
	PlanetEditBrush::SplitVoxelBoxIntoChunks(Op.Bounds, Request.ChunkSizeVoxels, Boxes);
	if (Boxes.Num() == 0)
	{
		return false;
	}

	Ops.Add(Op);
	FullBounds = Op.Bounds;

	Chunks.Reserve(Boxes.Num());
	for (const FPlanetEditVoxelBox& Box : Boxes)
	{
		FPlanetEditChunk& Chunk = Chunks.AddDefaulted_GetRef();
		Chunk.Bounds = Box;
		Chunk.NumOps = Ops.Num();  // 此刻只有一个 op
	}

	return true;
}

bool FPlanetProgressiveEdit::Setup(
	AVoxelPlanetActor* const InPlanet,
	const FVector& CenterWorld,
	const EPlanetBrushShape InShape,
	const int32 InSizeVoxels,
	const int32 InMinSizeVoxels,
	const int32 InMaxSizeVoxels,
	const bool bInPlace,
	const int32 ChunkSizeVoxels,
	const bool bInMultiThreaded)
{
	Planet.Reset();

	FPlanetEditRequest Request;
	if (!MakeRequest(InPlanet, CenterWorld, InShape, InSizeVoxels, InMinSizeVoxels,
			InMaxSizeVoxels, bInPlace, ChunkSizeVoxels, bInMultiThreaded, Request))
	{
		ResetState();
		return false;
	}

	if (!SetupFromRequest(Request))
	{
		return false;
	}

	Planet = InPlanet;
	return true;
}
```

- [ ] **Step 5: 替换 `ApplyChunk`，`ApplyNextChunk` 保持在原位**

把现有 `void FPlanetProgressiveEdit::ApplyChunk(const FPlanetEditVoxelBox& Chunk) { ... }`
的**整个函数体**替换成：

```cpp
void FPlanetProgressiveEdit::ApplyChunk(const FPlanetEditChunk& Chunk)
{
	AVoxelPlanetActor* const Target = Planet.Get();
	if (!IsValid(Target))
	{
		return;
	}

	FVoxelData& WorldData = Target->GetData();
	const FVoxelIntBox ChunkBounds(Chunk.Bounds.Min, Chunk.Bounds.MaxExclusive);

	// 块只认自己创建时的前缀。夹一下只是防御：NumOps 由 TryMerge 在游戏线程写，
	// 不会超过 Ops.Num()。
	const int32 NumOps = FMath::Clamp(Chunk.NumOps, 0, Ops.Num());

	// 写锁只围住数据写入，渲染刷新在锁外 —— 与插件工具完全一致
	//（VoxelGeneratedTools.h 的 GENERATED_TOOL_CALL）。
	{
		FVoxelWriteScopeLock Lock(WorldData, ChunkBounds, ProgressiveEditLockName);

		std::atomic<bool>& Changed = bChangedAnything;
		const bool bAdd = bPlace;
		// 捕获裸指针 + 数量：lambda 跑在任务图线程上，不能碰可能被重分配的容器。
		// op 只在游戏线程的 TryMerge 里增长，与 ApplyChunk 不重叠。
		const FPlanetEditOp* const OpData = Ops.GetData();

		WorldData.ParallelSet<FVoxelValue>(ChunkBounds,
			[OpData, NumOps, bAdd, &Changed](int32 X, int32 Y, int32 Z, FVoxelValue& Value)
			{
				const FVoxelValue NewValue = PlanetProgressiveKernel::EvaluateOps(
					OpData, NumOps, X, Y, Z, Value, bAdd);

				if (NewValue != Value)
				{
					Value = NewValue;
					// relaxed 就够：这里只要「最终可见」，不参与任何同步。
					Changed.store(true, std::memory_order_relaxed);
				}
			},
			/*bForceSingleThread=*/!bMultiThreaded);
	}

	LastChunkBounds = Chunk.Bounds;
	AppliedBounds = PlanetEditBrush::UnionVoxelBox(AppliedBounds, Chunk.Bounds);
}
```

`ApplyNextChunk` 里那一行 `ApplyChunk(Chunks[NextChunkIndex]);` 不用改（新签名接受
`FPlanetEditChunk`，`Chunks` 已是该类型的 `TArray`）。

- [ ] **Step 6: 构建**

关编辑器，跑构建命令。`Result: Succeeded`。常见失败：
`FPlanetEditOp` 未定义 → Task 1 的 struct 没加或 `PlanetProgressiveEditKernel.h`
漏了 `#include "PlanetProgressiveEdit.h"`。

- [ ] **Step 7: 跑测试**

跑子集命令。期望 `StellarEra.Planet.Editor.Job.SetupFromRequest ... Success`，
且**旧的三条**（`ProgressiveChunks` / `ProgressiveKernel` / `Kernel.OpsMatchSingle`）仍然通过。

- [ ] **Step 8: 检查 BOM 并提交**

```bash
cd /f/workspace/ue/StellarEra && \
head -c 3 Source/StellarEraPlanetEdit/Private/PlanetProgressiveEdit.cpp | xxd && \
git commit -m "refactor(行星编辑): job 改为 op 列表，装配拆出纯几何入口

Setup 只负责世界→体素换算，建 op 与切块交给 SetupFromRequest。
行为不变：仍是一个 op，包围盒与块数与重构前逐分量相同。

Co-Authored-By: Claude Code <noreply@anthropic.com>" -- \
  Source/StellarEraPlanetEdit/Public/PlanetProgressiveEdit.h \
  Source/StellarEraPlanetEdit/Private/PlanetProgressiveEdit.cpp \
  Source/StellarEraPlanetEdit/Private/PlanetProgressiveEditTests.cpp
```

---

## Task 3: 合并 `TryMerge` —— 只给新区域切块，进度不丢

**Files:**
- Modify: `Source/StellarEraPlanetEdit/Public/PlanetEditBrush.h`
- Modify: `Source/StellarEraPlanetEdit/Private/PlanetEditBrush.cpp`
- Modify: `Source/StellarEraPlanetEdit/Public/PlanetProgressiveEdit.h`
- Modify: `Source/StellarEraPlanetEdit/Private/PlanetProgressiveEdit.cpp`
- Test: `Source/StellarEraPlanetEdit/Private/PlanetProgressiveEditTests.cpp`（3 条）
- Test: `Source/StellarEra/Private/PlanetEditTests.cpp`（1 条）

**Interfaces:**
- Consumes: `FPlanetEditRequest` / `MakeOp` / `ResetState` / `Ops` / `Chunks` / `NextChunkIndex`（Task 2）
- Produces:
  - `bool PlanetEditBrush::VoxelBoxesIntersect(const FPlanetEditVoxelBox& A, const FPlanetEditVoxelBox& B);`
  - `bool FPlanetProgressiveEdit::TryMerge(const FPlanetEditRequest& Request, int32 MaxOps);`
  - `bool FPlanetProgressiveEdit::TryMergeWorld(AVoxelPlanetActor*, const FVector& CenterWorld, EPlanetBrushShape, int32 SizeVoxels, int32 MinSizeVoxels, int32 MaxSizeVoxels, bool bInPlace, int32 ChunkSizeVoxels, bool bMultiThreaded, int32 MaxOps);`

**与 spec §4.1 第 3 步的一处收紧（有意，不是走样）：** spec 写的是「球心距离 < 0.5 体素
且半径相同 → 不追加」。实施用**包围盒完全相同**判同一片区域。理由：包围盒是从
中心与半径取整出来的，同一片区域的两次点击必然得到同一个整数盒子；而距离判据
在中心正好跨过整数边界时会漏判。两者都只影响「要不要白加一个幂等 op」，不影响正确性。

- [ ] **Step 1: 在 `PlanetEditBrush.h` 声明 `VoxelBoxesIntersect`**

紧跟在 `UnionVoxelBox` 的声明之后：

```cpp
	/**
	 * 两个右端开的体素盒是否有公共格。
	 *
	 * A.MaxExclusive == B.Min 不算相交（没有公共格）—— 合并只对真正重叠的落笔
	 * 有意义，贴边相邻的两笔并起来会凭空多出一片没挖过的范围。
	 */
	STELLARERAPLANETEDIT_API bool VoxelBoxesIntersect(
		const FPlanetEditVoxelBox& A, const FPlanetEditVoxelBox& B);
```

- [ ] **Step 2: 在 `PlanetEditBrush.cpp` 实现**

紧跟在 `UnionVoxelBox` 的实现之后：

```cpp
bool PlanetEditBrush::VoxelBoxesIntersect(
	const FPlanetEditVoxelBox& A, const FPlanetEditVoxelBox& B)
{
	if (!IsValidVoxelBox(A) || !IsValidVoxelBox(B))
	{
		return false;
	}

	return A.Min.X < B.MaxExclusive.X && B.Min.X < A.MaxExclusive.X
		&& A.Min.Y < B.MaxExclusive.Y && B.Min.Y < A.MaxExclusive.Y
		&& A.Min.Z < B.MaxExclusive.Z && B.Min.Z < A.MaxExclusive.Z;
}
```

- [ ] **Step 3: 写四条失败的测试**

**(a)** 在 `PlanetProgressiveEditTests.cpp` 追加到 `#endif` 之前（先放共用的请求构造器）：

```cpp
namespace
{
	/** 合并测试共用的一套请求：球、半径 6 体素、4 体素的块（好切出多块）。 */
	FPlanetEditRequest MakeMergeTestRequest(const FVector& Center, const bool bPlace = false)
	{
		FPlanetEditRequest Request;
		Request.Shape = EPlanetBrushShape::Sphere;
		Request.CenterVoxels = Center;
		Request.RadiusVoxels = 6.f;
		Request.SizeVoxels = 10;
		Request.MinSizeVoxels = 1;
		Request.MaxSizeVoxels = 64;
		Request.bPlace = bPlace;
		Request.bMultiThreaded = false;
		Request.ChunkSizeVoxels = 4;
		return Request;
	}

	void TestBoxesEqual(
		FAutomationTestBase& Test, const TCHAR* What,
		const FPlanetEditVoxelBox& Actual, const FPlanetEditVoxelBox& Expected)
	{
		Test.TestTrue(What,
			Actual.Min == Expected.Min && Actual.MaxExclusive == Expected.MaxExclusive);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraProgressiveMergeRejectTest,
	"StellarEra.Planet.Editor.Merge.Rejects",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraProgressiveMergeRejectTest::RunTest(const FString& Parameters)
{
	TSharedPtr<FPlanetProgressiveEdit> Job = MakeShared<FPlanetProgressiveEdit>();
	if (!TestTrue(TEXT("装配成功"), Job->SetupFromRequest(MakeMergeTestRequest(FVector::ZeroVector))))
	{
		return false;
	}

	const int32 ChunksBefore = Job->GetChunkCount();
	const FPlanetEditVoxelBox FullBefore = Job->GetFullBounds();

	// 1. 离得远 → 包围盒不相交。
	TestFalse(TEXT("不相交必须拒绝"),
		Job->TryMerge(MakeMergeTestRequest(FVector(500.0, 0.0, 0.0)), 8));

	// 2. 形状不同。
	{
		FPlanetEditRequest Box = MakeMergeTestRequest(FVector(1.0, 0.0, 0.0));
		Box.Shape = EPlanetBrushShape::Cube;
		Box.RadiusVoxels = 0.f;
		TestFalse(TEXT("形状不同必须拒绝"), Job->TryMerge(Box, 8));
	}

	// 3. 笔刷尺寸不同。
	{
		FPlanetEditRequest Bigger = MakeMergeTestRequest(FVector(1.0, 0.0, 0.0));
		Bigger.SizeVoxels = 12;
		TestFalse(TEXT("尺寸不同必须拒绝"), Job->TryMerge(Bigger, 8));
	}

	// 4. 挖 / 填不同。
	TestFalse(TEXT("挖填不同必须拒绝"),
		Job->TryMerge(MakeMergeTestRequest(FVector(1.0, 0.0, 0.0), /*bPlace=*/true), 8));

	// 5. op 数到上限：相交、不重复，但 MaxOps = 1 时已满。
	TestFalse(TEXT("op 数到上限必须拒绝"),
		Job->TryMerge(MakeMergeTestRequest(FVector(3.0, 0.0, 0.0)), /*MaxOps=*/1));

	// 一次拒绝都不能改状态。
	TestEqual(TEXT("拒绝后 op 数不变"), Job->GetOpCount(), 1);
	TestEqual(TEXT("拒绝后块数不变"), Job->GetChunkCount(), ChunksBefore);
	TestBoxesEqual(*this, TEXT("拒绝后包围盒不变"), Job->GetFullBounds(), FullBefore);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraProgressiveMergeDedupeTest,
	"StellarEra.Planet.Editor.Merge.DedupesIdenticalOps",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraProgressiveMergeDedupeTest::RunTest(const FString& Parameters)
{
	const FPlanetEditRequest Request = MakeMergeTestRequest(FVector(2.0, 2.0, 2.0));

	TSharedPtr<FPlanetProgressiveEdit> Job = MakeShared<FPlanetProgressiveEdit>();
	if (!TestTrue(TEXT("装配成功"), Job->SetupFromRequest(Request)))
	{
		return false;
	}

	const int32 ChunksBefore = Job->GetChunkCount();
	const int32 OpsBefore = Job->GetOpCount();

	// 同一个落笔再来一遍：已经在这一笔里，返回 true 但不追加任何东西。
	// 挖取 max、填取 min，重复一次是同值 —— 追加纯属浪费。
	TestTrue(TEXT("重复的落笔返回 true"), Job->TryMerge(Request, 8));
	TestEqual(TEXT("重复的落笔不追加 op"), Job->GetOpCount(), OpsBefore);
	TestEqual(TEXT("重复的落笔不追加块"), Job->GetChunkCount(), ChunksBefore);

	// 连点 5 下：仍然只有一笔。
	for (int32 Index = 0; Index < 5; ++Index)
	{
		Job->TryMerge(Request, 8);
	}
	TestEqual(TEXT("连点 5 下仍然只有一个 op"), Job->GetOpCount(), 1);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraProgressiveMergeProgressTest,
	"StellarEra.Planet.Editor.Merge.PreservesProgress",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraProgressiveMergeProgressTest::RunTest(const FString& Parameters)
{
	const FPlanetEditRequest First = MakeMergeTestRequest(FVector::ZeroVector);

	TSharedPtr<FPlanetProgressiveEdit> Job = MakeShared<FPlanetProgressiveEdit>();
	if (!TestTrue(TEXT("装配成功"), Job->SetupFromRequest(First)))
	{
		return false;
	}

	const int32 ChunksBefore = Job->GetChunkCount();
	const int32 IndexBefore = Job->GetNextChunkIndex();
	const FPlanetEditVoxelBox AppliedBefore = Job->GetAppliedBounds();
	const FPlanetEditVoxelBox FullBefore = Job->GetFullBounds();

	// 相交的新落笔必须被接受。
	const FPlanetEditRequest Second = MakeMergeTestRequest(FVector(4.0, 0.0, 0.0));
	TestTrue(TEXT("相交必须接受"), Job->TryMerge(Second, 8));

	TestEqual(TEXT("op 数 +1"), Job->GetOpCount(), 2);
	TestTrue(TEXT("块数增加"), Job->GetChunkCount() > ChunksBefore);

	// 进度一字未动：已做前缀的下标不变，AppliedBounds 不变。
	TestEqual(TEXT("已做前缀不变"), Job->GetNextChunkIndex(), IndexBefore);
	TestBoxesEqual(*this, TEXT("AppliedBounds 不变"), Job->GetAppliedBounds(), AppliedBefore);

	// 整笔范围是两支的并集。
	TestBoxesEqual(*this, TEXT("FullBounds == 两支包围盒的并集"), Job->GetFullBounds(),
		PlanetEditBrush::UnionVoxelBox(FullBefore, Job->GetOps()[1].Bounds));

	// 老块的前缀仍是 1、新块的前缀是 2 —— 这就是「进度不丢」的机制。
	// 按 NumOps 数而不是按下标数：插入点在 NextChunkIndex（这里是 0），
	// 新块排在前面，下标区间跟「新旧」不对应。
	int32 PrefixOneCount = 0;
	int32 PrefixTwoCount = 0;
	for (const FPlanetEditChunk& Chunk : Job->GetChunks())
	{
		if (Chunk.NumOps == 1)
		{
			++PrefixOneCount;
		}
		else if (Chunk.NumOps == 2)
		{
			++PrefixTwoCount;
		}
	}
	TestEqual(TEXT("老块（前缀 1）数量不变"), PrefixOneCount, ChunksBefore);
	TestEqual(TEXT("新块（前缀 2）数量等于新增的块数"),
		PrefixTwoCount, Job->GetChunkCount() - ChunksBefore);

	return true;
}
```

**(b)** 在 `Source/StellarEra/Private/PlanetEditTests.cpp` 中，按该文件既有格式
（`IMPLEMENT_SIMPLE_AUTOMATION_TEST` + 同一套 flags）追加一条，位置紧跟在
`FStellarEraEditBrushUnionVoxelBoxTest::RunTest` 之后：

```cpp
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraEditBrushVoxelBoxesIntersectTest,
	"StellarEra.Planet.Editor.Brush.VoxelBoxesIntersect",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraEditBrushVoxelBoxesIntersectTest::RunTest(const FString& Parameters)
{
	// FPlanetEditVoxelBox 没有构造函数，用局部小工具拼，别为测试去改公开结构体。
	auto Box = [](const FIntVector& Mn, const FIntVector& MaxEx)
	{
		FPlanetEditVoxelBox Out;
		Out.Min = Mn;
		Out.MaxExclusive = MaxEx;
		return Out;
	};

	const FPlanetEditVoxelBox A = Box(FIntVector(0, 0, 0), FIntVector(10, 10, 10));

	TestTrue(TEXT("自己与自己相交"), PlanetEditBrush::VoxelBoxesIntersect(A, A));
	TestTrue(TEXT("部分重叠"), PlanetEditBrush::VoxelBoxesIntersect(
		A, Box(FIntVector(5, 5, 5), FIntVector(15, 15, 15))));
	TestTrue(TEXT("完全包含"), PlanetEditBrush::VoxelBoxesIntersect(
		A, Box(FIntVector(2, 2, 2), FIntVector(4, 4, 4))));

	// 右端开：贴边相邻没有公共格。
	TestFalse(TEXT("X 轴贴边不相交"), PlanetEditBrush::VoxelBoxesIntersect(
		A, Box(FIntVector(10, 0, 0), FIntVector(20, 10, 10))));
	TestFalse(TEXT("Y 轴贴边不相交"), PlanetEditBrush::VoxelBoxesIntersect(
		A, Box(FIntVector(0, 10, 0), FIntVector(10, 20, 10))));
	TestFalse(TEXT("Z 轴贴边不相交"), PlanetEditBrush::VoxelBoxesIntersect(
		A, Box(FIntVector(0, 0, 10), FIntVector(10, 10, 20))));
	TestFalse(TEXT("离得远不相交"), PlanetEditBrush::VoxelBoxesIntersect(
		A, Box(FIntVector(100, 0, 0), FIntVector(110, 10, 10))));

	// 无效盒子一律 false。
	TestFalse(TEXT("无效盒子不相交"),
		PlanetEditBrush::VoxelBoxesIntersect(A, FPlanetEditVoxelBox()));

	return true;
}
```

- [ ] **Step 4: 跑测试确认它们失败**

跑子集命令。期望编译失败：`VoxelBoxesIntersect` / `TryMerge` 未定义。

- [ ] **Step 5: 在 `PlanetProgressiveEdit.h` 声明两个合并入口**

插在 `SetupFromRequest` 的声明之后：

```cpp
	/**
	 * 纯策略合并：把一个请求并进本 job。
	 *
	 * 包围盒不相交、参数不兼容（形状 / 尺寸 / 挖填 / 多线程）、op 数到 MaxOps
	 * → false（调用方另起一笔）。
	 * 完全重复的落笔（包围盒一致）→ true 且不追加任何东西：挖取 max、填取 min，
	 * 重复一次是同值。
	 *
	 * **进度不丢**靠插入位置：新块插在 NextChunkIndex 上，已做前缀一个都不动。
	 */
	bool TryMerge(const FPlanetEditRequest& Request, int32 MaxOps);

	/**
	 * 世界空间版的 TryMerge：换算成体素后交给 TryMerge。
	 * 行星不是本 job 的行星、或本 job 已做完 → false。
	 */
	bool TryMergeWorld(
		AVoxelPlanetActor* InPlanet,
		const FVector& CenterWorld,
		EPlanetBrushShape InShape,
		int32 SizeVoxels,
		int32 MinSizeVoxels,
		int32 MaxSizeVoxels,
		bool bInPlace,
		int32 ChunkSizeVoxels,
		bool bMultiThreaded,
		int32 MaxOps);
```

- [ ] **Step 6: 在 `PlanetProgressiveEdit.cpp` 实现**

追加到文件末尾（`ApplyChunk` 之后）：

```cpp
bool FPlanetProgressiveEdit::TryMerge(
	const FPlanetEditRequest& Request, const int32 MaxOps)
{
	// 没装配过、或已经做完：没有可并的目标。做完的 job 马上会被移出队列，
	// 往它后面追加块永远不会被执行。
	if (Ops.Num() == 0 || Chunks.Num() == 0 || IsFinished())
	{
		return false;
	}

	// 参数必须完全兼容。挖填混排、中途改笔刷尺寸都不合并 —— 挖与填在同一格上
	// 不是同一种操作，混起来「合并结果 == 依次执行」就不再成立。
	if (Request.Shape != Shape
		|| Request.SizeVoxels != SizeVoxels
		|| Request.bPlace != bPlace
		|| Request.bMultiThreaded != bMultiThreaded)
	{
		return false;
	}

	const FPlanetEditOp NewOp = MakeOp(Request);
	if (!PlanetEditBrush::IsValidVoxelBox(NewOp.Bounds))
	{
		return false;
	}

	// 只合并包围盒相交的落笔：否则整笔范围会膨胀成一片没挖过的大区域，
	// 每次刷新都要重建它。
	if (!PlanetEditBrush::VoxelBoxesIntersect(NewOp.Bounds, FullBounds))
	{
		return false;
	}

	// 去重放在 op 上限之前：同一片区域连点，不管点多少下都该被吸收，
	// 而不是在到上限之后突然开始排新任务。
	for (const FPlanetEditOp& Existing : Ops)
	{
		if (Existing.Bounds.Min == NewOp.Bounds.Min
			&& Existing.Bounds.MaxExclusive == NewOp.Bounds.MaxExclusive)
		{
			return true;
		}
	}

	if (Ops.Num() >= FMath::Max(MaxOps, 1))
	{
		return false;
	}

	// 只切新包围盒。老块的边界一个都不动 —— 这就是进度能保留的原因。
	TArray<FPlanetEditVoxelBox> NewBoxes;
	PlanetEditBrush::SplitVoxelBoxIntoChunks(
		NewOp.Bounds, Request.ChunkSizeVoxels, NewBoxes);
	if (NewBoxes.Num() == 0)
	{
		return false;
	}

	// 到这一步才动状态：上面任何一步失败都不能留下半个 op。
	Ops.Add(NewOp);
	FullBounds = PlanetEditBrush::UnionVoxelBox(FullBounds, NewOp.Bounds);

	TArray<FPlanetEditChunk> NewChunks;
	NewChunks.Reserve(NewBoxes.Num());
	for (const FPlanetEditVoxelBox& Box : NewBoxes)
	{
		FPlanetEditChunk& Chunk = NewChunks.AddDefaulted_GetRef();
		Chunk.Bounds = Box;
		Chunk.NumOps = Ops.Num();
	}

	// 插在已做前缀之后：做过的下标一个都不动，新点的那一下排在下一个被执行。
	Chunks.Insert(NewChunks.GetData(), NewChunks.Num(), NextChunkIndex);
	return true;
}

bool FPlanetProgressiveEdit::TryMergeWorld(
	AVoxelPlanetActor* const InPlanet,
	const FVector& CenterWorld,
	const EPlanetBrushShape InShape,
	const int32 InSizeVoxels,
	const int32 InMinSizeVoxels,
	const int32 InMaxSizeVoxels,
	const bool bInPlace,
	const int32 ChunkSizeVoxels,
	const bool bInMultiThreaded,
	const int32 MaxOps)
{
	// 换星了就不能合：体素空间不同，包围盒没有可比性。
	if (!IsValid(InPlanet) || InPlanet != Planet.Get())
	{
		return false;
	}

	FPlanetEditRequest Request;
	if (!MakeRequest(InPlanet, CenterWorld, InShape, InSizeVoxels, InMinSizeVoxels,
			InMaxSizeVoxels, bInPlace, ChunkSizeVoxels, bInMultiThreaded, Request))
	{
		return false;
	}

	return TryMerge(Request, MaxOps);
}
```

- [ ] **Step 7: 构建并跑测试**

关编辑器 → 构建 `Result: Succeeded` → 跑子集命令。
期望四条新测试全部 `Success`，且 `ProgressiveChunks` / `ProgressiveKernel` /
`Kernel.OpsMatchSingle` / `Job.SetupFromRequest` 仍绿。

- [ ] **Step 8: 检查 BOM 并提交**

```bash
cd /f/workspace/ue/StellarEra && \
head -c 3 Source/StellarEraPlanetEdit/Public/PlanetEditBrush.h | xxd && \
head -c 3 Source/StellarEraPlanetEdit/Private/PlanetEditBrush.cpp | xxd && \
git commit -m "feat(行星编辑): 同类型相交落笔合并成一笔

新落笔只给新包围盒切块，插在已做前缀之后，进度不丢；
包围盒一致视为重复，直接吸收不追加 op。

Co-Authored-By: Claude Code <noreply@anthropic.com>" -- \
  Source/StellarEraPlanetEdit/Public/PlanetEditBrush.h \
  Source/StellarEraPlanetEdit/Private/PlanetEditBrush.cpp \
  Source/StellarEraPlanetEdit/Public/PlanetProgressiveEdit.h \
  Source/StellarEraPlanetEdit/Private/PlanetProgressiveEdit.cpp \
  Source/StellarEraPlanetEdit/Private/PlanetProgressiveEditTests.cpp \
  Source/StellarEra/Private/PlanetEditTests.cpp
```

---

## Task 4: 等价性 —— 合并结果必须与依次独立执行逐格全等

**只有测试，没有生产代码改动。** 这是整个合并特性的安全网：spec §8 #3 的核心断言。

**Files:**
- Test: `Source/StellarEraPlanetEdit/Private/PlanetProgressiveEditTests.cpp`

**Interfaces:**
- Consumes: `SetupFromRequest` / `TryMerge` / `GetOps` / `GetChunks` / `GetNextChunkIndex`（Task 2、3）；`PlanetProgressiveKernel::EvaluateOps`（Task 1）
- Produces: 无

- [ ] **Step 1: 写测试**

追加到 `PlanetProgressiveEditTests.cpp` 的 `#endif` 之前（可以复用 Task 3 的
`MakeMergeTestRequest`）：

```cpp
// 合并的正确性判据：**逐格全等**，不是「看起来差不多」。
//
// 路径 1：两次独立编辑依次执行（现在的行为）。
// 路径 2：合并成一笔后按运行期真实顺序执行 —— 从 GetNextChunkIndex 起逐块，
//        块内逐格按该块的 NumOps 过 op。
//
// 合并发生在一次都没推过的时候，所以新块被插到 0 号位置，先于老块执行。
// 顺序反过来也必须同值，这正是「挖取 max / 填取 min 幂等且可交换」要钉的点。
//
// 合成网格而不是真实体素世界：AVoxelPlanetActor 需要一个带 Voxel LOD 管理器的
// UWorld 才 IsCreated()，无头测试里建不起来。网格承担的是「同一格的保存/读回」，
// 逐格谓词用的仍是运行期那一份 EvaluateOps。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraProgressiveMergeEquivalenceTest,
	"StellarEra.Planet.Editor.Merge.ResultEqualsSequential",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraProgressiveMergeEquivalenceTest::RunTest(const FString& Parameters)
{
	// 三种初值铺满：向空与向实两个方向的边界都会被走到。
	auto MakeSeed = [](const int32 X, const int32 Y, const int32 Z) -> FVoxelValue
	{
		const uint32 Hash = static_cast<uint32>((X * 73856093) ^ (Y * 19349663) ^ (Z * 83492791));
		switch (Hash % 3)
		{
		case 0:  return FVoxelValue::Empty();
		case 1:  return FVoxelValue::Full();
		default: return FVoxelValue{ 0.25f };
		}
	};

	for (int32 PlaceIndex = 0; PlaceIndex < 2; ++PlaceIndex)
	{
		const bool bPlace = PlaceIndex != 0;

		const FPlanetEditRequest First = MakeMergeTestRequest(FVector::ZeroVector, bPlace);
		const FPlanetEditRequest Second = MakeMergeTestRequest(FVector(6.0, 0.0, 0.0), bPlace);

		TSharedPtr<FPlanetProgressiveEdit> Job = MakeShared<FPlanetProgressiveEdit>();
		if (!TestTrue(TEXT("装配成功"), Job->SetupFromRequest(First)))
		{
			return false;
		}
		if (!TestTrue(TEXT("合并成功"), Job->TryMerge(Second, 8)))
		{
			return false;
		}
		TestEqual(TEXT("合并后有两个 op"), Job->GetOpCount(), 2);

		const TArray<FPlanetEditOp>& Ops = Job->GetOps();
		const FPlanetEditVoxelBox Union =
			PlanetEditBrush::UnionVoxelBox(Ops[0].Bounds, Ops[1].Bounds);

		const FIntVector Min = Union.Min;
		const FIntVector Size(
			Union.MaxExclusive.X - Min.X,
			Union.MaxExclusive.Y - Min.Y,
			Union.MaxExclusive.Z - Min.Z);

		auto IndexOf = [&Min, &Size](const int32 X, const int32 Y, const int32 Z)
		{
			return (X - Min.X) + Size.X * ((Y - Min.Y) + Size.Y * (Z - Min.Z));
		};

		TArray<FVoxelValue> Initial;
		Initial.SetNum(Size.X * Size.Y * Size.Z);
		for (int32 X = Min.X; X < Union.MaxExclusive.X; ++X)
		{
			for (int32 Y = Min.Y; Y < Union.MaxExclusive.Y; ++Y)
			{
				for (int32 Z = Min.Z; Z < Union.MaxExclusive.Z; ++Z)
				{
					Initial[IndexOf(X, Y, Z)] = MakeSeed(X, Y, Z);
				}
			}
		}

		// 路径 1：依次执行两次独立编辑（每笔只有自己那一个 op）。
		TArray<FVoxelValue> Sequential = Initial;
		for (const FPlanetEditOp& Op : Ops)
		{
			for (int32 X = Op.Bounds.Min.X; X < Op.Bounds.MaxExclusive.X; ++X)
			{
				for (int32 Y = Op.Bounds.Min.Y; Y < Op.Bounds.MaxExclusive.Y; ++Y)
				{
					for (int32 Z = Op.Bounds.Min.Z; Z < Op.Bounds.MaxExclusive.Z; ++Z)
					{
						const int32 Index = IndexOf(X, Y, Z);
						Sequential[Index] = PlanetProgressiveKernel::EvaluateOps(
							&Op, 1, X, Y, Z, Sequential[Index], bPlace);
					}
				}
			}
		}

		// 路径 2：合并后的块，按运行期顺序（从 NextChunkIndex 起）逐块应用。
		TArray<FVoxelValue> MergedValues = Initial;
		for (int32 ChunkIndex = Job->GetNextChunkIndex();
			ChunkIndex < Job->GetChunks().Num(); ++ChunkIndex)
		{
			const FPlanetEditChunk& Chunk = Job->GetChunks()[ChunkIndex];
			for (int32 X = Chunk.Bounds.Min.X; X < Chunk.Bounds.MaxExclusive.X; ++X)
			{
				for (int32 Y = Chunk.Bounds.Min.Y; Y < Chunk.Bounds.MaxExclusive.Y; ++Y)
				{
					for (int32 Z = Chunk.Bounds.Min.Z; Z < Chunk.Bounds.MaxExclusive.Z; ++Z)
					{
						const int32 Index = IndexOf(X, Y, Z);
						MergedValues[Index] = PlanetProgressiveKernel::EvaluateOps(
							Ops.GetData(), Chunk.NumOps, X, Y, Z, MergedValues[Index], bPlace);
					}
				}
			}
		}

		int32 Mismatch = 0;
		for (int32 Index = 0; Index < Sequential.Num(); ++Index)
		{
			if (Sequential[Index] != MergedValues[Index])
			{
				++Mismatch;
			}
		}
		TestEqual(
			bPlace ? TEXT("填：合并结果与依次独立执行逐格全等")
			       : TEXT("挖：合并结果与依次独立执行逐格全等"),
			Mismatch, 0);
	}

	return true;
}
```

- [ ] **Step 2: 跑测试**

不需要重新构建（只改了测试文件？**不对** —— 测试文件在模块内，必须重新构建）。
关编辑器 → 构建 → 跑子集命令。
期望 `StellarEra.Planet.Editor.Merge.ResultEqualsSequential ... Success`。

- [ ] **Step 3: 如果失败，别改测试**

按 `superpowers:systematic-debugging`：先打印第一处不一致的坐标、两条路径各自的值、
以及那一格属于哪个块（`NumOps` 是 1 还是 2），再判断是实现错还是测试的模拟顺序错。
最可能的两个原因：`MakeOp` 里方块/球的包围盒与 `Setup` 不一致；`TryMerge` 在
`SplitVoxelBoxIntoChunks` 之前就改了 `Ops`。**不要**为了让测试变绿去放宽断言。

- [ ] **Step 4: 提交**

```bash
cd /f/workspace/ue/StellarEra && \
git commit -m "test(行星编辑): 合并结果与依次独立执行的逐格等价性

挖、填各跑一遍：合成网格上按运行期块顺序模拟，与两笔独立编辑逐格比对。

Co-Authored-By: Claude Code <noreply@anthropic.com>" -- \
  Source/StellarEraPlanetEdit/Private/PlanetProgressiveEditTests.cpp
```

---

## Task 5: 队列两段式推进 —— 队首优先 + 余量轮转

**Files:**
- Modify: `Source/StellarEraPlanetEdit/Public/PlanetProgressiveEdit.h`（新增 `PlanetEditSchedule`）
- Modify: `Source/StellarEraPlanetEdit/Private/PlanetProgressiveEdit.cpp`（实现两个纯函数）
- Modify: `Source/StellarEraPlanetEdit/Public/PlanetEditComponent.h`
- Modify: `Source/StellarEraPlanetEdit/Private/PlanetEditComponent.cpp`
- Test: `Source/StellarEraPlanetEdit/Private/PlanetProgressiveEditTests.cpp`

**Interfaces:**
- Consumes: 现有 `EditQueue`（`TArray<TSharedPtr<FPlanetProgressiveEdit>>`）、`Edit->ApplyNextChunk()`、`Edit->GetLastChunkBounds()`、`OnEditFinished`、`AccumulateRenderBounds`、`FlushPendingRenderUpdate`
- Produces:
  - `int32 PlanetEditSchedule::GetFairCandidateCount(int32 QueueSize)`
  - `int32 PlanetEditSchedule::GetNextFairIndex(int32 Cursor, int32 QueueSize)`
  - `UPlanetEditComponent::ProgressiveEditHeadBudgetShare`（`float`，默认 0.7）
  - `UPlanetEditComponent::NextFairEditIndex`（`int32` 私有成员，初值 `INDEX_NONE`）

- [ ] **Step 1: 写失败的测试**

追加到 `PlanetProgressiveEditTests.cpp` 的 `#endif` 之前：

```cpp
// 轮转游标必须满足三条，任一条错都会退化成「某笔永远饿着」或「某笔被跳过」：
//  1) 0 号是队首，永远不进轮转；
//  2) 走到末尾回绕到 1，不回到 0；
//  3) 队列变短导致越界时夹回 1，不取模（取模在队列动态变化时会跳笔）。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraProgressiveScheduleTest,
	"StellarEra.Planet.Editor.Schedule.FairCursor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraProgressiveScheduleTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("空队列没有候选"), PlanetEditSchedule::GetFairCandidateCount(0), 0);
	TestEqual(TEXT("单笔没有候选"), PlanetEditSchedule::GetFairCandidateCount(1), 0);
	TestEqual(TEXT("三笔有两个候选"), PlanetEditSchedule::GetFairCandidateCount(3), 2);

	TestEqual(TEXT("空队列取不到下标"),
		PlanetEditSchedule::GetNextFairIndex(INDEX_NONE, 0), INDEX_NONE);
	TestEqual(TEXT("单笔取不到下标"),
		PlanetEditSchedule::GetNextFairIndex(INDEX_NONE, 1), INDEX_NONE);

	// 首次从 1 开始：0 是队首，由阶段一负责。
	TestEqual(TEXT("游标未初始化时从 1 开始"),
		PlanetEditSchedule::GetNextFairIndex(INDEX_NONE, 3), 1);
	TestEqual(TEXT("1 之后是 2"), PlanetEditSchedule::GetNextFairIndex(1, 3), 2);
	TestEqual(TEXT("2 之后回绕到 1"), PlanetEditSchedule::GetNextFairIndex(2, 3), 1);
	TestEqual(TEXT("越界夹回 1"), PlanetEditSchedule::GetNextFairIndex(9, 3), 1);
	TestEqual(TEXT("两笔时 2 越界夹回 1"), PlanetEditSchedule::GetNextFairIndex(2, 2), 1);
	TestEqual(TEXT("游标 0 不进轮转"), PlanetEditSchedule::GetNextFairIndex(0, 3), 1);

	return true;
}
```

- [ ] **Step 2: 在 `PlanetProgressiveEdit.h` 加命名空间**

放在 `FPlanetEditRequest` 之后、`FPlanetProgressiveEdit` 的文档注释之前：

```cpp
/**
 * 编辑队列的推进策略。
 *
 * 放在这里而不是组件里，是因为组件是 UObject：纯函数独立出来才能无头测试。
 * 规则只有一条 —— 0 号是队首，由「队首优先」阶段独占，轮转只碰 1..N-1。
 */
namespace PlanetEditSchedule
{
	/** 除队首外可轮转的笔数。队列 ≤ 1 时为 0。 */
	STELLARERAPLANETEDIT_API int32 GetFairCandidateCount(int32 QueueSize);

	/**
	 * 下一个轮转下标：夹到 [1, QueueSize-1] 并回绕；没有候选时返回 INDEX_NONE。
	 *
	 * 越界只夹不取模：队列长度在帧与帧之间会变，「Cursor % N」会让游标指到
	 * 另一笔身上，看起来像跳笔。
	 */
	STELLARERAPLANETEDIT_API int32 GetNextFairIndex(int32 Cursor, int32 QueueSize);
}
```

- [ ] **Step 3: 在 `PlanetProgressiveEdit.cpp` 实现**

追加到文件末尾：

```cpp
int32 PlanetEditSchedule::GetFairCandidateCount(const int32 QueueSize)
{
	return QueueSize > 1 ? QueueSize - 1 : 0;
}

int32 PlanetEditSchedule::GetNextFairIndex(const int32 Cursor, const int32 QueueSize)
{
	if (QueueSize <= 1)
	{
		return INDEX_NONE;
	}

	const int32 MinIndex = 1;
	const int32 MaxIndex = QueueSize - 1;

	// 游标未初始化、或队列变短后越界：从 1 重新开始。
	if (Cursor < MinIndex || Cursor > MaxIndex)
	{
		return MinIndex;
	}

	const int32 NextIndex = Cursor + 1;
	return NextIndex > MaxIndex ? MinIndex : NextIndex;
}
```

- [ ] **Step 4: 在 `PlanetEditComponent.h` 加两个属性与一个游标**

两个 `UPROPERTY` 紧跟在 `ProgressiveEditBudgetMs` 之后，**Category / 访问级别与
`ProgressiveEditBudgetMs` 完全一致**：

```cpp
	/**
	 * 队首独占的预算份额（0~1）。剩下的留给其他活跃的笔轮转。
	 *
	 * 默认 0.7：队首仍拿大头，别的笔每帧也能往前走一块，不会饿死。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0", ClampMax = "1"))
	float ProgressiveEditHeadBudgetShare = 0.7f;

	/**
	 * 一笔编辑最多合并多少次落笔。
	 *
	 * 合并的 job 每格要按顺序过 op，上限是防「一格里过几十个球」。
	 * 到上限后新的点击另起一笔。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "1"))
	int32 MaxMergedOps = 8;
```

私有成员 `NextFairEditIndex` 跟在 `EditQueue` 的声明下面：

```cpp
	/** 余量轮转的游标（跨帧保留）。INDEX_NONE = 还没开始轮转。 */
	int32 NextFairEditIndex = INDEX_NONE;
```

- [ ] **Step 5: 替换 `TickProgressiveEdits`**

把现有实现（从 `if (EditQueue.Num() == 0)` 到最后的 `FlushPendingRenderUpdate();`）
整体替换成：

```cpp
void UPlanetEditComponent::TickProgressiveEdits()
{
	if (EditQueue.Num() == 0)
	{
		NextFairEditIndex = INDEX_NONE;
		return;
	}

	const double StartSeconds = FPlatformTime::Seconds();
	const double BudgetMs = FMath::Max(static_cast<double>(ProgressiveEditBudgetMs), 0.1);
	const double Share = FMath::Clamp(
		static_cast<double>(ProgressiveEditHeadBudgetShare), 0.0, 1.0);
	const double HeadBudgetMs = BudgetMs * Share;

	// 取「已用毫秒」：两个阶段用同一把尺子，预算才好分配。
	auto ElapsedMs = [StartSeconds]()
	{
		return (FPlatformTime::Seconds() - StartSeconds) * 1000.0;
	};

	bool bAppliedChunk = false;

	// --- 阶段一：队首优先 ---
	// 预算检查放在**推进过一块之后**：份额再小，这一帧也要往前走一块，
	// 否则队列永远推不动。
	while (EditQueue.Num() > 0)
	{
		const TSharedPtr<FPlanetProgressiveEdit>& Edit = EditQueue[0];
		if (!Edit.IsValid())
		{
			// Setup 失败时不会入队，这里只是防御：坏掉的项不能堵住队头。
			EditQueue.RemoveAt(0);
			continue;
		}

		const bool bMoreWork = Edit->ApplyNextChunk();
		bAppliedChunk = true;

		// 只并刚写完的那一块：整笔的范围在没做完时刷，会让渲染把还没改的
		// chunk 也重建一遍。
		AccumulateRenderBounds(Edit->GetPlanet().Get(), Edit->GetLastChunkBounds());

		if (!bMoreWork)
		{
			OnEditFinished(*Edit);
			EditQueue.RemoveAt(0);
			continue;
		}

		if (ElapsedMs() >= HeadBudgetMs)
		{
			break;
		}
	}

	// --- 阶段二：余量轮转 ---
	// 每帧每笔最多推一块，不做「再补一块」的补偿：块粒度 128³ 约 2.9 ms，
	// 补一块就会把单帧顶到 9 ms 以上。
	while (EditQueue.Num() > 1)
	{
		if (ElapsedMs() >= BudgetMs)
		{
			break;
		}

		NextFairEditIndex = PlanetEditSchedule::GetNextFairIndex(
			NextFairEditIndex, EditQueue.Num());
		if (NextFairEditIndex == INDEX_NONE)
		{
			break;
		}

		const TSharedPtr<FPlanetProgressiveEdit>& Edit = EditQueue[NextFairEditIndex];
		if (!Edit.IsValid())
		{
			EditQueue.RemoveAt(NextFairEditIndex);
			continue;
		}

		const bool bMoreWork = Edit->ApplyNextChunk();
		bAppliedChunk = true;

		AccumulateRenderBounds(Edit->GetPlanet().Get(), Edit->GetLastChunkBounds());

		if (!bMoreWork)
		{
			OnEditFinished(*Edit);
			// 删掉之后游标指向的位置已经换了笔，下一轮由 GetNextFairIndex 夹回来。
			EditQueue.RemoveAt(NextFairEditIndex);
		}
	}

	if (bAppliedChunk)
	{
		// 当帧就刷：洞因此看得见地从光标处向外长，而不是等整笔做完才出现。
		FlushPendingRenderUpdate();
	}
}
```

- [ ] **Step 6: 换星时先刷再记新星**

替换 `AccumulateRenderBounds` 里换星那一段：

```cpp
	// 换了一颗行星：旧范围的坐标属于旧星的体素空间，不能混进来。
	// 轮转下同一帧里可能真的换星，所以必须先把旧的刷掉再换 —— 直接清空会
	// 把旧星还等着刷的范围丢掉，那一片的网格就永远停在旧数据上。
	if (PendingRenderPlanet.Get() != Planet)
	{
		FlushPendingRenderUpdate();
		PendingRenderPlanet = Planet;
	}
```

（`FlushPendingRenderUpdate` 内部已经把 `PendingRenderBounds` 清成无效盒，不用再清一次。）

- [ ] **Step 7: 构建并跑测试**

关编辑器 → 构建 `Result: Succeeded` → 跑子集命令。
期望 `StellarEra.Planet.Editor.Schedule.FairCursor ... Success`，其余仍绿。

- [ ] **Step 8: 手工验证（无法自动化，必须真的跑一次）**

自动化测试碰不到 `UPlanetEditComponent::Tick`（需要真实体素世界）。用编辑器验：

1. 打开 `UniverseMap`，PIE，进入编辑模式，把笔刷调到 **≥ 200 体素**（保证走大笔刷队列路径）。
2. 在一颗行星上 **快速点 3 下不同位置**。
3. 期望：三个坑**同时**在长，而不是「第一个长完才开始第二个」。
4. 在同一片区域 **连点 10 下**：期望画面里只有一个坑在长（合并生效），
   控制台 `stat unit` 的 Game 线程没有随点击次数线性变长。
5. 把 `MaxMergedOps` 临时改成 1：再连点，期望不再合并（每次新开一笔）。

结果（通过 / 不通过）写进计划文档末尾的「实施记录」小节。

- [ ] **Step 9: 提交**

```bash
cd /f/workspace/ue/StellarEra && \
git commit -m "feat(行星编辑): 队列改队首优先 + 余量轮转

队首吃 ProgressiveEditHeadBudgetShare（默认 0.7）的预算，剩余预算按跨帧
游标在 1..N-1 之间轮转，多笔同时活跃时每笔都在推进。
换星时先刷旧星的待刷新范围再记新星，不再直接丢掉。

Co-Authored-By: Claude Code <noreply@anthropic.com>" -- \
  Source/StellarEraPlanetEdit/Public/PlanetProgressiveEdit.h \
  Source/StellarEraPlanetEdit/Private/PlanetProgressiveEdit.cpp \
  Source/StellarEraPlanetEdit/Public/PlanetEditComponent.h \
  Source/StellarEraPlanetEdit/Private/PlanetEditComponent.cpp \
  Source/StellarEraPlanetEdit/Private/PlanetProgressiveEditTests.cpp
```

---

## Task 6: 组件接上合并 —— 队列路径先试着并进已有的一笔

**Files:**
- Modify: `Source/StellarEraPlanetEdit/Private/PlanetEditComponent.cpp`（`ApplyBrushAtScreenPosition`）
- Test: `Source/StellarEra/Private/PlanetEditTests.cpp`（扩展既有 CDO 测试）

**Interfaces:**
- Consumes: `FPlanetProgressiveEdit::TryMergeWorld(...)`（Task 3）、`MaxMergedOps`（Task 5）
- Produces: 无新接口；`ApplyBrushAtScreenPosition` 的返回值语义不变（「有没有接受这一笔」）

- [ ] **Step 1: 扩展 CDO 默认值测试**

在 `Source/StellarEra/Private/PlanetEditTests.cpp` 里
`FStellarEraPlanetEditComponentDefaultsTest::RunTest` 的
`TestEqual(TEXT("默认连挖间隔 0.12 秒"), ...)` 之后追加：

```cpp
	TestEqual(TEXT("默认队首预算份额 0.7"),
		EditCDO->ProgressiveEditHeadBudgetShare, 0.7f, 0.001f);
	TestEqual(TEXT("默认单笔最多合并 8 次落笔"), EditCDO->MaxMergedOps, 8);
```

- [ ] **Step 2: 跑测试**

关编辑器 → 构建 → 子集命令。
期望 `StellarEra.Planet.EditComponent.Defaults` 通过（Task 5 已经加好了属性，
所以这条此时应该直接绿；红灯说明 UPROPERTY 名或默认值写错了）。

- [ ] **Step 3: 在 `ApplyBrushAtScreenPosition` 的队列路径前插入合并尝试**

在 `Source/StellarEraPlanetEdit/Private/PlanetEditComponent.cpp` 里，
小笔刷分支 `return true;` 结束之后、`const int32 MaxQueue = FMath::Max(MaxQueuedEdits, 1);`
之前插入：

```cpp
	// 先试着并进已有的笔：同一片区域连点时，不再把 (Size+6)³ 格扫 N 遍。
	// 从队尾往前找，最新的一笔优先接收。合并失败（不相交 / 参数不同 / 到
	// MaxMergedOps）才往下走「新开一笔」—— 所以这里不能动队列。
	for (int32 Index = EditQueue.Num() - 1; Index >= 0; --Index)
	{
		const TSharedPtr<FPlanetProgressiveEdit>& Edit = EditQueue[Index];
		if (!Edit.IsValid())
		{
			continue;
		}

		if (Edit->TryMergeWorld(Target.Planet, Center, BrushShape, BrushSizeVoxels,
				MinBrushSizeVoxels, MaxBrushSizeVoxels, bPlace,
				ProgressiveEditChunkSizeVoxels, bMultiThreadedEdits, MaxMergedOps))
		{
			return true;
		}
	}

```

（原注释块「大笔刷：切成块排队……」保持在下面不动。）

- [ ] **Step 4: 构建并跑全量回归**

关编辑器 → 构建 `Result: Succeeded` → 跑**全量**：

```bash
MSYS_NO_PATHCONV=1 MSYS2_ARG_CONV_EXCL='*' \
"D:/Application/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" \
  "F:/workspace/ue/StellarEra/StellarEra.uproject" \
  -ExecCmds="Automation RunTests StellarEra" \
  -TestExit="Automation Test Queue Empty" \
  -unattended -nopause -nullrhi -nosplash -stdout \
  -abslog="F:/workspace/ue/StellarEra/Saved/Logs/automation-full-merge.log"
```

期望：**除既有的 `StellarEra.Planet.Geology.EarthPresetValues` 失败外全绿**。
本计划新增的 8 条必须全部出现且 `Success`：

```
StellarEra.Planet.Editor.Kernel.OpsMatchSingle
StellarEra.Planet.Editor.Job.SetupFromRequest
StellarEra.Planet.Editor.Merge.Rejects
StellarEra.Planet.Editor.Merge.DedupesIdenticalOps
StellarEra.Planet.Editor.Merge.PreservesProgress
StellarEra.Planet.Editor.Merge.ResultEqualsSequential
StellarEra.Planet.Editor.Schedule.FairCursor
StellarEra.Planet.Editor.Brush.VoxelBoxesIntersect
```

- [ ] **Step 5: 确认没碰到插件**

```bash
cd /f/workspace/ue/StellarEra && git status --short -- Plugins/
```
必须**没有任何输出**。

- [ ] **Step 6: 提交**

```bash
cd /f/workspace/ue/StellarEra && \
git commit -m "feat(行星编辑): 队列路径先尝试合并同类型相交落笔

大笔刷点击先按队尾→队首方向找可并的笔，合并失败才新开一笔。
CDO 默认值补两条断言（队首份额 0.7、单笔最多 8 次落笔）。

Co-Authored-By: Claude Code <noreply@anthropic.com>" -- \
  Source/StellarEraPlanetEdit/Private/PlanetEditComponent.cpp \
  Source/StellarEra/Private/PlanetEditTests.cpp
```

---

## Task 7: 收尾 —— 实施记录、spec 状态、PIE 验证

**Files:**
- Modify: `docs/superpowers/plans/2026-10-31-planet-edit-merge-round-robin.md`（本文末尾的「实施记录」）
- Modify: `docs/superpowers/specs/2026-10-31-planet-edit-merge-round-robin-design.md`（状态行）

- [ ] **Step 1: PIE 手工验证**

按 Task 5 Step 8 的五条重新跑一遍（此时合并与轮转都已接上）。**编辑器必须在前台**，
否则整进程被限到 3 FPS，看起来像「轮转没生效」。逐条记录通过 / 不通过。

- [ ] **Step 2: 写实施记录**

在本文件末尾追加：

```markdown
## 实施记录

- **构建**：`Result: Succeeded`（UBT 直调 dotnet）
- **全量回归**：`<N> tests, <N-1> Success, 1 Fail`，唯一失败是既有的
  `StellarEra.Planet.Geology.EarthPresetValues`
- **新增测试**：8 条，全部 Success
- **PIE 手工验证**：
  1. 大笔刷快速点 3 下不同位置 → <结果>
  2. 同一片区域连点 10 下 → <结果>
  3. `MaxMergedOps = 1` 时不再合并 → <结果>
- **与 spec 的偏差**：去重判据从「球心距离 < 0.5 体素」改成「包围盒完全相同」
  （理由见 Task 3 开头）
```

`<...>` 全部替换成实测值，不许留占位符。任何一条没验就写「未验证」并说明原因。

- [ ] **Step 3: 改 spec 状态行**

把 `docs/superpowers/specs/2026-10-31-planet-edit-merge-round-robin-design.md` 第 5 行
的 `- **状态**：设计已确认，待实现` 改成 `- **状态**：已实现（见 plans/2026-10-31-planet-edit-merge-round-robin.md）`。

- [ ] **Step 4: 提交**

```bash
cd /f/workspace/ue/StellarEra && \
git commit -m "docs(行星编辑): 落笔合并与队列轮转的实施记录

Co-Authored-By: Claude Code <noreply@anthropic.com>" -- \
  docs/superpowers/plans/2026-10-31-planet-edit-merge-round-robin.md \
  docs/superpowers/specs/2026-10-31-planet-edit-merge-round-robin-design.md
```

---

## 自查（写计划时已跑过）

**spec 覆盖：**

| spec 章节 | 落在哪个任务 |
|---|---|
| §3 数据模型（op 列表 / 块带 NumOps） | Task 1、Task 2 |
| §3.2 一个块一次 ParallelSet、逐格按序过 op | Task 1（`EvaluateOps`）、Task 2（`ApplyChunk`） |
| §4.1 `TryMerge` 六步 | Task 3 |
| §4.2 插在 `NextChunkIndex`（进度不丢） | Task 3（实现）+ `Merge.PreservesProgress`（断言） |
| §4.3 重叠格写两次但同值 | Task 4（逐格全等） |
| §5 队首优先 + 余量轮转、游标跨帧、夹不取模 | Task 5 |
| §6 跨行星轮转要先 flush | Task 5 Step 6 |
| §6 op 数上限 `MaxMergedOps` | Task 5（属性）+ Task 3（判定） |
| §6 队列满保留 `RemoveAt(1)` | 不改（Task 6 的合并降低了触发概率） |
| §8 验证 1/2/3/4 | Task 3、Task 4 的自动化测试 |
| §8 验证 5（两笔同时推进） | **无法自动化**，Task 5 Step 8 手工验证 |
| §8 验证 6/7（编译、全量回归） | Task 7 |
| §10 不碰插件 | Task 6 Step 5 用 `git status -- Plugins/` 钉住 |

**已知缺口（诚实记录）：** spec §8 #5 的「两笔同时活跃时都能推进」**没有**自动化覆盖 ——
`UPlanetEditComponent::TickProgressiveEdits` 需要真实体素世界才能驱动，而本模块
现有测试里没有这种夹具。本计划用两条纯函数测试（`Schedule.FairCursor`）钉住游标
逻辑，用 PIE 手工验证钉住端到端行为。**如果将来有了世界夹具，第一个该补的就是它。**

**占位符扫描：** 正文无 TBD / TODO；Task 7 Step 2 的 `<...>` 是要求实测值填入的模板，
已明确写了「不许留占位符」。

**类型一致性：** `FPlanetEditRequest` 的字段（`Shape` / `CenterVoxels` / `RadiusVoxels` /
`SizeVoxels` / `MinSizeVoxels` / `MaxSizeVoxels` / `bPlace` / `bMultiThreaded` /
`ChunkSizeVoxels`）在 Task 2 定义、Task 3/4 使用；`SetupFromRequest` / `TryMerge` /
`TryMergeWorld` / `GetOps` / `GetChunks` / `GetOpCount` / `GetNextChunkIndex` /
`GetFairCandidateCount` / `GetNextFairIndex` 的签名在各任务间一致。

## 实施记录

2026-10-31 实施完成。提交链：`c0ab5a5`（基线）→ `b4953b1`（Task 1）→ `81ba872` + `f611538`（Task 3）
→ `0c62591`（Task 4）→ `3a04c08` + `8fb6a57` + `131a5f7` + `9aa3b99` + `829918a`（Task 5）
→ `ac60bcf` + `472e3f7`（Task 6）。

- **构建**：`Result: Succeeded`（UBT 直调 dotnet，工作目录在项目外）；最后一次构建在 `472e3f7`。
- **全量回归**：`163 tests, 162 Success, 1 Fail, 0 Not Run`，唯一失败是既有的
  `StellarEra.Planet.Geology.EarthPresetValues`（本计划未触碰地质模块）。
  日志：`Saved/Logs/automation-full-merge.log`，跑于 `ac60bcf`。
- **新增测试**：8 条，全部 Success：
  `Kernel.OpsMatchSingle`、`Job.SetupFromRequest`、`Merge.Rejects`、
  `Merge.DedupesIdenticalOps`、`Merge.PreservesProgress`、
  `Merge.ResultEqualsSequential`、`Schedule.FairCursor`、`Brush.VoxelBoxesIntersect`。
- **PIE 手工验证**：**未验证**，原因与待办清单见下。
- **与 spec 的偏差**：
  1. 去重判据从「球心距离 < 0.5 体素」改成「包围盒完全相同」（理由见 Task 3 开头）。
  2. 额外加了 `#if WITH_DEV_AUTOMATION_TESTS` 的 `SetNextChunkIndexForTest` 测试缝：
     否则 `Merge.PreservesProgress` 在 `NextChunkIndex == 0` 下分不清「插在已做前缀之后」
     与「追加到末尾」，等于没钉住这条不变量。
  3. 只有一笔活跃时队首吃整份预算（`EditQueue.Num() > 1 ? BudgetMs * Share : BudgetMs`）。
     无条件按 0.7 拆分会把单笔挖掘从 4 ms 砍到 2.8 ms —— 用户最常用的动作不该为轮转买单。

### 未做：PIE 手工验证

本计划的两项用户可见效果 ——「不同位置的几笔同时长」与「同一片区域连点不爆队列」——
都无法在自动化里覆盖：`UPlanetEditComponent::TickProgressiveEdits` 需要真实体素世界
（`AVoxelPlanetActor::IsCreated()`），现有测试没有这种夹具。实施时编辑器未运行、
编辑器 MCP 端点不通，而这类「画面里同时出现三个坑」的判断本来就需要人眼。
**按计划要求记为「未验证」，不记为通过。**

待办（编辑器在前台时一次做完，约 5 分钟）：

1. 打开 `UniverseMap`，PIE，进编辑模式，笔刷调到 **≥ 200 体素**（保证走大笔刷队列路径）。
2. 在行星表面**快速点 3 下不同位置**：期望三个坑**同时**在长，而不是第一个长完才开始第二个。
3. 在同一片区域**连点 10 下**：期望画面里只有一个坑在长（合并生效），且 `stat unit` 的
   Game 线程不随点击次数线性变长。
4. 把 `MaxMergedOps` 临时改成 1：再连点，期望不再合并（每次新开一笔）。
