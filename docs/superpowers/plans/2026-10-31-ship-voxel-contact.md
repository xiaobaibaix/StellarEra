# 飞船接触跟着体素地形走 — 实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 让飞船的 N 体接触解算读挖过之后的真实体素地表，而不是生成器的理想地表。

**Architecture:** 行星侧新增「一个方向 → 真实地表半径」的查询（解析半径当种子，沿径向 march 体素找空/实翻转）；子系统在每帧推进求解器**之前**为飞船的每个探针各查一次、按方向去重后缓存进 `FCosmosBodyState`；求解器的 `ResolveProbeContact` 从「对一颗球求穿透」改成「对每个探针各自的半径求穿透」。求解器内部因此**零体素查询** —— 它一帧最多跑 200 个子步。

**Tech Stack:** UE 5.8.3 / C++，VoxelFree 插件（`FVoxelData`、`FVoxelReadScopeLock`），UE Automation Test。

**Spec:** `docs/superpowers/specs/2026-10-31-ship-voxel-contact-design.md`

## Global Constraints

- 构建**必须关着编辑器**（Live Coding 会锁 DLL）。用捆绑 dotnet 直调 UBT，cwd 在项目外。
- 含中文的 `.h`/`.cpp` 存 **UTF-8 with BOM**。用 Edit 改现有文件不会丢 BOM；新建文件要加。
- 不要统一换行符：仓库本来就是 CRLF/LF 混合。
- 提交时 pathspec 挂在 commit 上（`git commit -m ... -- <paths>`）：索引里有常驻暂存文件，裸 `git commit` 会吞掉全部。
- `*Tests.cpp` 只能放 `<模块>/Src/Tests/`。
- 第三方插件目录（`Plugins/VoxelFree`、`Plugins/VibeUE`）**一个字节都不改**。
- 模块依赖不新增：`StellarEraCosmos → StellarEraPlanet` 已存在。

## 构建与测试命令（各任务反复用）

构建（约 40 秒）：

```bash
cd /f/workspace/ue && \
"D:/Application/Epic Games/UE_5.8/Engine/Binaries/ThirdParty/DotNet/10.0/win-x64/dotnet.exe" \
  "D:/Application/Epic Games/UE_5.8/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.dll" \
  StellarEraEditor Win64 Development \
  -Project="F:/workspace/ue/StellarEra/StellarEra.uproject" -WaitMutex -FromMsBuild
```

单跑一组测试（约 25 秒，编辑器开着也能跑）。把下面 `Automation RunTests` 后面的
前缀换成要跑的那一组 —— 本计划用到三个：

| 前缀 | 跑什么 |
|---|---|
| `StellarEra.Planet.SurfaceRay` | Task 1 的纯函数 |
| `StellarEra.Cosmos` | Task 3 的求解器 |
| `StellarEra` | Task 5 的全量 |

```bash
"D:/Application/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" \
  "F:/workspace/ue/StellarEra/StellarEra.uproject" \
  -ExecCmds="Automation RunTests StellarEra.Planet.SurfaceRay" \
  -TestExit="Automation Test Queue Empty" \
  -unattended -nopause -nullrhi -nosplash -stdout -abslog="%TEMP%/stellar_tests.log"
```

看到 `LogAutomationController: ... (Success)` 与进程 0 退出即通过。

---

### Task 1: 纯函数 `PlanetSurfaceRay::FindSurfaceBoundaryStep`

把「起点空就往里找第一格实、起点实就往外找第一格空、边界取两格中点」这套规则
从 `AVoxelPlanetActor::RaycastSurfaceCm` 里抽出来，做成不碰 UObject、不碰体素的
纯函数。Task 2 会用它。

**Files:**
- Modify: `Source/StellarEraPlanet/Src/Surface/PlanetSurfaceRay.h`（在 `ComputeMarchStepRange` 声明之后追加）
- Modify: `Source/StellarEraPlanet/Src/Surface/PlanetSurfaceRay.cpp`（在 `ComputeMarchStepRange` 实现之后追加）
- Test: `Source/StellarEraPlanet/Src/Tests/PlanetSurfaceRayTests.cpp`（文件末尾追加）

**Interfaces:**
- Consumes: 无
- Produces:
  ```cpp
  namespace PlanetSurfaceRay
  {
  	STELLARERAPLANET_API bool FindSurfaceBoundaryStep(
  		int32 MaxForwardSteps, int32 MaxBackwardSteps,
  		TFunctionRef<bool(int32 Step)> SampleEmptyAt,
  		double& OutBoundaryStep);
  }
  ```
  `Step` 是相对起点（Step 0）的体素步数，正 = 沿查询方向。成功返回 true 并写
  `OutBoundaryStep = ±(边界步 − 0.5)`；失败返回 false 且**不写** `OutBoundaryStep`。

- [ ] **Step 1: 写失败的测试**

在 `Source/StellarEraPlanet/Src/Tests/PlanetSurfaceRayTests.cpp` 文件**末尾**（`#endif` 之前）追加：

```cpp
// 径向步进判决：起点空朝里找实、起点实朝外找空，边界取两格中点。
// Task 2 的 GetVoxelSurfaceRadiusCmAtDirection 直接调它，所以这里钉死符号约定。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraSurfaceRayBoundaryStepTest,
	"StellarEra.Planet.SurfaceRay.BoundaryStep",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraSurfaceRayBoundaryStepTest::RunTest(const FString& Parameters)
{
	// 起点空：地表在解析点**里侧** 3 格处（挖低了 2 格多）。
	// Step 0/1/2 为空，Step −3 起为实 → 边界 = −(3 − 0.5) = −2.5。
	{
		const auto SampleEmpty = [](const int32 Step)
		{
			return Step > -3;
		};

		double BoundaryStep = 0.0;
		TestTrue(TEXT("起点空、往里找到实"),
			PlanetSurfaceRay::FindSurfaceBoundaryStep(
				/*MaxForwardSteps=*/8, /*MaxBackwardSteps=*/8,
				SampleEmpty, BoundaryStep));
		TestEqual(TEXT("边界 = −2.5"), BoundaryStep, -2.5, 1.0e-12);
	}

	// 起点实：解析点比真实地表低（地形抬高）。Step 0..3 为实，Step 4 起为空
	// → 边界 = +(4 − 0.5) = +3.5。
	{
		const auto SampleEmpty = [](const int32 Step)
		{
			return Step >= 4;
		};

		double BoundaryStep = 0.0;
		TestTrue(TEXT("起点实、往外找到空"),
			PlanetSurfaceRay::FindSurfaceBoundaryStep(8, 8, SampleEmpty, BoundaryStep));
		TestEqual(TEXT("边界 = +3.5"), BoundaryStep, 3.5, 1.0e-12);
	}

	// 预算走完没翻转（挖穿）：返回 false，并且不写 Out 参数 ——
	// 调用方拿它当「退回解析半径」的信号，残留的旧值会被当成真命中。
	{
		const auto SampleEmpty = [](const int32 Step)
		{
			return Step > -1000;
		};

		double BoundaryStep = -123.0;
		TestFalse(TEXT("全空到预算尽头：找不到翻转"),
			PlanetSurfaceRay::FindSurfaceBoundaryStep(8, 8, SampleEmpty, BoundaryStep));
		TestEqual(TEXT("失败时不写 Out 参数"), BoundaryStep, -123.0, 1.0e-12);
	}

	// 预算 = 1 的两个方向：正好在最后一格翻转 → 成功；下一格才翻转 → 失败。
	{
		double BoundaryStep = 0.0;
		TestTrue(TEXT("预算 1、第 1 格就翻转"),
			PlanetSurfaceRay::FindSurfaceBoundaryStep(
				8, 1, [](const int32 Step) { return Step > -1; }, BoundaryStep));
		TestEqual(TEXT("边界 = −0.5"), BoundaryStep, -0.5, 1.0e-12);

		BoundaryStep = 0.0;
		TestFalse(TEXT("预算 1、第 2 格才翻转 → 失败"),
			PlanetSurfaceRay::FindSurfaceBoundaryStep(
				8, 1, [](const int32 Step) { return Step > -2; }, BoundaryStep));
	}

	// 正向预算为 1 的同一条规则（起点实）。
	{
		double BoundaryStep = 0.0;
		TestTrue(TEXT("起点实、正向预算 1、第 1 格就翻转"),
			PlanetSurfaceRay::FindSurfaceBoundaryStep(
				1, 8, [](const int32 Step) { return Step >= 1; }, BoundaryStep));
		TestEqual(TEXT("边界 = +0.5"), BoundaryStep, 0.5, 1.0e-12);
	}

	return true;
}
```

- [ ] **Step 2: 构建，确认测试编译失败**

Run: 构建命令（见「构建与测试命令」）
Expected: 编译错误，`'FindSurfaceBoundaryStep': is not a member of 'PlanetSurfaceRay'`

- [ ] **Step 3: 写实现**

在 `Source/StellarEraPlanet/Src/Surface/PlanetSurfaceRay.h` 的
`namespace PlanetSurfaceRay { ... }` 内、`ComputeMarchStepRange` 声明**之后**追加：

```cpp
	/**
	 * 沿一条线找空/实翻转。SampleEmptyAt(Step) 抽样，Step 是相对起点（Step 0）
	 * 的体素步数，正 = 沿查询方向。
	 *
	 * 规则与 RaycastSurfaceCm 相同：起点为空就往负方向走、找第一格实心；
	 * 起点为实则往正方向走、找第一格空气。
	 *
	 * 找到时 OutBoundaryStep = ±(边界步 − 0.5)，即两个采样点的中点 —— 真实表面
	 * 一定在这两格之间，误差 ≤ 1 体素。走完预算仍未翻转时返回 false
	 * （挖穿 / 整段全实）且不写 OutBoundaryStep，调用方据此退回解析值。
	 *
	 * 抽样回调的锁由调用方自己管（可以每段一取）—— 本函数只管走哪边、走多远。
	 */
	STELLARERAPLANET_API bool FindSurfaceBoundaryStep(
		int32 MaxForwardSteps, int32 MaxBackwardSteps,
		TFunctionRef<bool(int32 Step)> SampleEmptyAt,
		double& OutBoundaryStep);
```

在 `Source/StellarEraPlanet/Src/Surface/PlanetSurfaceRay.cpp` 的
`ComputeMarchStepRange` 实现**之后**、`SolveAnalyticHit` **之前**追加：

```cpp
	bool FindSurfaceBoundaryStep(
		const int32 MaxForwardSteps, const int32 MaxBackwardSteps,
		const TFunctionRef<bool(int32 Step)> SampleEmptyAt,
		double& OutBoundaryStep)
	{
		// 起点单独抽一次：它决定往哪个方向走。
		const bool bStartsEmpty = SampleEmptyAt(0);

		// 起点空 = 在解析地表之上 → 朝里（负方向）找第一格实。
		// 起点实 = 地形被抬高 → 朝外（正方向）找第一格空。
		const int32 StepSign = bStartsEmpty ? -1 : 1;
		const int32 MaxSteps = bStartsEmpty ? MaxBackwardSteps : MaxForwardSteps;

		for (int32 Step = 1; Step <= MaxSteps; ++Step)
		{
			if (SampleEmptyAt(StepSign * Step) != bStartsEmpty)
			{
				// 边界取两个采样点的中点。
				OutBoundaryStep = static_cast<double>(StepSign) * (Step - 0.5);
				return true;
			}
		}

		return false;
	}
```

- [ ] **Step 4: 构建并跑测试**

Run: 构建命令，然后
`-ExecCmds="Automation RunTests StellarEra.Planet.SurfaceRay"`
Expected: `StellarEra.Planet.SurfaceRay.*` 全 Success，含新增的 `BoundaryStep`；
原有的 `ConstantRadius` / `VaryingRadius` / `Rejects` / `SphereHit` /
`MidIterationMiss` / `SphereFarHit` / `MarchStepRange` 一条都不能红

- [ ] **Step 5: 提交**

```bash
git commit -m "feat(行星表面): 抽出径向步进判决 FindSurfaceBoundaryStep

起点空往里找实、起点实往外找空、边界取两格中点 —— 这套规则原先埋在
RaycastSurfaceCm 里。抽成纯函数，飞船接触的真实地表查询要用同一份。

Co-Authored-By: Claude Code <noreply@anthropic.com>" -- \
  Source/StellarEraPlanet/Src/Surface/PlanetSurfaceRay.h \
  Source/StellarEraPlanet/Src/Surface/PlanetSurfaceRay.cpp \
  Source/StellarEraPlanet/Src/Tests/PlanetSurfaceRayTests.cpp
```

---

### Task 2: `AVoxelPlanetActor::GetVoxelSurfaceRadiusCmAtDirection`

一个方向 → 真实体素地表半径。解析半径当种子，沿**径向线** march，找空/实翻转。
同构于 `RaycastSurfaceCm`，但射线从球心出发。

**Files:**
- Modify: `Source/StellarEraPlanet/Src/VoxelPlanetActor.h:129` 之后（`GetSurfaceRadiusCmAtDirection` 声明之后）
- Modify: `Source/StellarEraPlanet/Src/VoxelPlanetActor.cpp`（`GetSurfaceRadiusCmAtDirection` 实现之后、`GetGeologyField` 之前）

**Interfaces:**
- Consumes: `PlanetSurfaceRay::FindSurfaceBoundaryStep`（Task 1）
  ```cpp
  bool FindSurfaceBoundaryStep(int32 MaxForwardSteps, int32 MaxBackwardSteps,
      TFunctionRef<bool(int32 Step)> SampleEmptyAt, double& OutBoundaryStep);
  ```
  已有：`GetSurfaceRadiusCm()`、`GetSurfaceRadiusCmAtDirection(Dir)`、`IsCreated()`、成员 `VoxelSize`、`GetData()`
- Produces:
  ```cpp
  float AVoxelPlanetActor::GetVoxelSurfaceRadiusCmAtDirection(const FVector& WorldDirection) const;
  ```
  返回真实体素地表半径（厘米），找不到翻转 / 行星未创建时返回解析半径。

- [ ] **Step 1: 加声明**

`Source/StellarEraPlanet/Src/VoxelPlanetActor.h`，在 `GetSurfaceRadiusCmAtDirection`
那个 `UFUNCTION(BlueprintPure...) float GetSurfaceRadiusCmAtDirection(...)` 之后追加：

```cpp
	/**
	 * 指定方向上的**真实体素地表**半径（厘米）。
	 *
	 * 与 GetSurfaceRadiusCmAtDirection 的唯一区别：那个读的是生成器的理想地表
	 * （噪声 + 地质场），挖掘改的是体素数据，它看不见；这个从体素数据里找空/实
	 * 翻转，挖出来的坑它看得见。误差 ≤ 1 体素。
	 *
	 * 做法：解析半径当种子，沿该方向的径向线（从球心出发）march 体素数据 ——
	 * 种子在上方就往里找第一格实，种子埋在地下就往外找第一格空。
	 *
	 * 找不到翻转（挖穿、整段全实）或行星还没创建（IsCreated() 为假）时返回解析
	 * 半径。距离上的闸门（离多远就不必查）由调用方做 —— 本函数没有探针位置。
	 *
	 * 成本：最多 (1.25 × 基准半径 + 解析半径) / 体素 次采样，按 128 体素分段持
	 * 读锁。**每帧每方向调一次就够**，不要逐子步调。
	 */
	float GetVoxelSurfaceRadiusCmAtDirection(const FVector& WorldDirection) const;
```

- [ ] **Step 2: 加实现**

`Source/StellarEraPlanet/Src/VoxelPlanetActor.cpp`，在
`AVoxelPlanetActor::GetSurfaceRadiusCmAtDirection` 的右花括号之后、
`AVoxelPlanetActor::GetGeologyField` 之前插入：

```cpp
float AVoxelPlanetActor::GetVoxelSurfaceRadiusCmAtDirection(const FVector& WorldDirection) const
{
	const FVector Direction = WorldDirection.GetSafeNormal();
	if (Direction.IsNearlyZero())
	{
		return GetSurfaceRadiusCm();
	}

	// 解析半径既是种子，也是所有兜底的答案：没体素数据时它是唯一能给的。
	const float AnalyticRadiusCm = GetSurfaceRadiusCmAtDirection(Direction);
	if (!IsCreated() || VoxelSize <= 0.f)
	{
		return AnalyticRadiusCm;
	}

	// 体素空间：球心在原点，单位 = 体素。与世界空间只差等比缩放与平移、没有旋转
	// （与 RaycastSurfaceCm 里那条说明同源）。本函数同样假定 Actor 缩放 S = 1。
	const double BaseRadiusVoxels = double(GetSurfaceRadiusCm()) / VoxelSize;
	const double AnalyticRadiusVoxels = double(AnalyticRadiusCm) / VoxelSize;
	if (AnalyticRadiusVoxels <= 0.0)
	{
		return AnalyticRadiusCm;
	}

	// 种子点：解析半径上的那个点（体素坐标）。
	const FVector HitLocal = Direction * AnalyticRadiusVoxels;

	// 两个方向的预算：向外到 1.25 倍包围球（与 RaycastSurfaceCm 同一份余量），
	// 向里到球心前一格为止（再往里就穿到对面半球去了）。
	//
	// 不复用 ComputeMarchStepRange：它那两个上界是按「调用方给的射线原点在球外」
	// 定义的，径向线以球心为原点时代入只是把 Inward/Backward 换个名字，读代码
	// 的人会把方向读反。
	const int32 MaxForwardSteps = FMath::Max(1,
		FMath::CeilToInt(BaseRadiusVoxels * 1.25 - AnalyticRadiusVoxels));
	const int32 MaxBackwardSteps = FMath::Max(1,
		FMath::CeilToInt(AnalyticRadiusVoxels) - 1);
	const int32 MaxAbsStep = FMath::Max(MaxForwardSteps, MaxBackwardSteps);

	const auto VoxelOf = [](const FVector& Point)
	{
		return FIntVector(
			static_cast<int32>(FMath::FloorToInt(Point.X)),
			static_cast<int32>(FMath::FloorToInt(Point.Y)),
			static_cast<int32>(FMath::FloorToInt(Point.Z)));
	};

	// 读锁按 128 体素一段取：整条径向线取一把锁会锁住大半棵八叉树，体素线程全被
	// 卡住（RaycastSurfaceCm 里同一条理由）。步进是单调的，所以「当前段」一个就够。
	constexpr int32 SegmentVoxels = 128;
	int32 LockedSign = 0;      // 当前锁覆盖的方向（±1），0 = 还没取
	int32 LockedThrough = 0;   // 已覆盖到 |Step| = LockedThrough
	TUniquePtr<FVoxelReadScopeLock> SegmentLock;

	const auto SampleEmptyAt = [&](const int32 Step) -> bool
	{
		const int32 Sign = Step >= 0 ? 1 : -1;
		const int32 AbsStep = FMath::Abs(Step);

		if (Sign != LockedSign || AbsStep > LockedThrough)
		{
			// 先放旧锁再取新锁：两把同时在手时，另一条线程按相反顺序取锁会死锁。
			SegmentLock.Reset();

			const int32 SegmentBegin = FMath::Max(AbsStep, 1);
			const int32 SegmentEnd =
				FMath::Min(SegmentBegin + SegmentVoxels - 1, MaxAbsStep);

			// 盒子要盖住 SegmentBegin − 1 到 SegmentEnd 的所有采样点（Step 0 也包在
			// 第一段里），+2 是取整余量。
			const FVector PointA =
				HitLocal + Direction * (static_cast<double>(Sign) * (SegmentBegin - 1));
			const FVector PointB =
				HitLocal + Direction * (static_cast<double>(Sign) * SegmentEnd);
			const FIntVector ScanMin(
				FMath::FloorToInt(FMath::Min(PointA.X, PointB.X)) - 2,
				FMath::FloorToInt(FMath::Min(PointA.Y, PointB.Y)) - 2,
				FMath::FloorToInt(FMath::Min(PointA.Z, PointB.Z)) - 2);
			const FIntVector ScanMax(
				FMath::CeilToInt(FMath::Max(PointA.X, PointB.X)) + 2,
				FMath::CeilToInt(FMath::Max(PointA.Y, PointB.Y)) + 2,
				FMath::CeilToInt(FMath::Max(PointA.Z, PointB.Z)) + 2);

			// FVoxelIntBox 的 Max 是开区间，所以 +1。
			SegmentLock = MakeUnique<FVoxelReadScopeLock>(
				GetData(), FVoxelIntBox(ScanMin, ScanMax + FIntVector(1, 1, 1)),
				TEXT("PlanetVoxelSurfaceRadius"));

			LockedSign = Sign;
			LockedThrough = SegmentEnd;
		}

		const FVector Sample = HitLocal + Direction * static_cast<double>(Step);
		return GetData().GetValue(VoxelOf(Sample), /*LOD=*/0).IsEmpty();
	};

	double BoundaryStep = 0.0;
	if (!PlanetSurfaceRay::FindSurfaceBoundaryStep(
		MaxForwardSteps, MaxBackwardSteps, SampleEmptyAt, BoundaryStep))
	{
		// 走完全部预算都没翻转：挖穿了，或整段全是实的。用 Verbose —— 每帧都会走
		// 到这条路时 Warning 会把日志刷满。
		UE_LOG(LogTemp, Verbose,
			TEXT("%s：方向 %s 走了 %d 步仍未找到空/实翻转，退回解析半径 %.1f cm。"),
			*GetName(), *Direction.ToCompactString(), MaxAbsStep, AnalyticRadiusCm);
		return AnalyticRadiusCm;
	}

	// 边界是两格的中点，误差 ≤ 1 体素。
	return static_cast<float>((AnalyticRadiusVoxels + BoundaryStep) * VoxelSize);
}
```

> `TUniquePtr` / `MakeUnique` 由 `CoreMinimal.h` 提供；`FVoxelReadScopeLock`
> 因为 `RaycastSurfaceCm` 已经在用，所需的头文件早就包含了。

- [ ] **Step 3: 构建**

Run: 构建命令
Expected: `Result: Succeeded`（若报 `Result: Failed` 但日志里编译行全过、
只有链接失败，先确认编辑器是不是没关）

- [ ] **Step 4: 自查（无自动化测试，如实记录）**

本步骤**没有**自动化测试：`AVoxelPlanetActor` 需要一个带 Voxel LOD 管理器的
`UWorld` 才有 `IsCreated()`，无头测试里建不起来（见
`PlanetProgressiveEditTests.cpp:1023` 的注释）。改为逐条对照清单，确认后把结论
写进提交说明：

- [ ] 起点 `HitLocal = Direction * AnalyticRadiusVoxels`，与 `RaycastSurfaceCm` 的
      `GlobalToLocalFloat(射线命中点)` 在 S = 1 时同值
- [ ] `MaxForwardSteps` / `MaxBackwardSteps` 都是 `FMath::Max(1, ...)` —— 预算为 0
      会让 `FindSurfaceBoundaryStep` 恒返回 false
- [ ] 段盒覆盖 `SegmentBegin − 1` 与 `SegmentEnd` 两个端点（Step 0 落在第一段内）
- [ ] `SegmentLock.Reset()` 在 `MakeUnique` **之前**
- [ ] 取锁后 `LockedSign` / `LockedThrough` 都已更新
- [ ] 失败分支返回 `AnalyticRadiusCm`，不是 0
- [ ] 最终返回值是 `(AnalyticRadiusVoxels + BoundaryStep) * VoxelSize`，
      没有拿 `AnalyticRadiusCm` 直接加厘米步长（单位混用）

- [ ] **Step 5: 提交**

```bash
git commit -m "feat(行星): 加 GetVoxelSurfaceRadiusCmAtDirection，按方向读真实体素地表

解析半径当种子，沿径向线 march 找空/实翻转，128 体素分段持读锁。
挖掘改的是体素数据，原先只有 RaycastSurfaceCm 看得见坑，接触解算看不见。

Co-Authored-By: Claude Code <noreply@anthropic.com>" -- \
  Source/StellarEraPlanet/Src/VoxelPlanetActor.h \
  Source/StellarEraPlanet/Src/VoxelPlanetActor.cpp
```

---

### Task 3: 求解器按逐探针半径解算

`FCosmosBodyState` 加两个字段，`ResolveProbeContact` 用它们替代单一半径。

**Files:**
- Modify: `Source/StellarEraCosmos/Src/NBody/NBodySimulator.h`（`FCosmosBodyState` 内，`ContactProbeRadius` 之后）
- Modify: `Source/StellarEraCosmos/Src/NBody/NBodySimulator.cpp:325-400`（`ResolveProbeContact`）
- Test: `Source/StellarEraCosmos/Src/Tests/NBodySimulatorTests.cpp`（文件末尾追加）

**Interfaces:**
- Consumes: 无
- Produces:
  ```cpp
  struct FCosmosBodyState
  {
  	TArray<double> ContactProbeSurfaceRadii;   // 与 ContactProbes 一一对应，单位 AU
  	int32 ContactSurfaceSphereIndex = INDEX_NONE;  // 这份半径是对哪个天体算的
  	void SetContactProbes(TArray<FVector3d> InProbes);  // 现在会一并清空上面两项
  };
  ```
  Task 4 会写这两个字段。

- [ ] **Step 1: 写失败的测试**

在 `Source/StellarEraCosmos/Src/Tests/NBodySimulatorTests.cpp` 末尾（`#endif` 之前）追加：

```cpp
// 逐探针地表半径：飞船停在哪，由「那个探针方向上的真实地表」决定，而不是由
// 行星那一颗解析球的半径决定。挖出来的坑就靠这条落下去。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraNBodyProbeSurfaceRadiiTest,
	"StellarEra.Cosmos.NBodyProbeSurfaceRadii",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraNBodyProbeSurfaceRadiiTest::RunTest(const FString& Parameters)
{
	// 统一几何：球体（行星）质量 3、半径 0.1 在原点；探针体（飞船）质量 1，
	// 一个探针在本地 (−0.06, 0, 0)。

	const auto MakeSimulator = []()
	{
		FNBodySimulator Simulator;
		Simulator.SetUnitSystem(ECosmosUnitSystem::Game);
		Simulator.SetIntegrator(ECosmosIntegrator::VelocityVerlet);
		Simulator.SetSofteningLength(1.0e-5);

		FCosmosBodyState Planet;
		Planet.Mass = 3.0;
		Planet.Radius = 0.1;
		Planet.Position = FVector3d::ZeroVector;
		Simulator.AddBody(Planet);
		return Simulator;
	};

	// 每条分支都要在 SetContactProbes **之后**才写逐探针半径：那个函数会把
	// 它清空（防止探针重建后留下对不上的旧半径）。
	const auto MakeShip = [](const double ShipX)
	{
		FCosmosBodyState Ship;
		Ship.Mass = 1.0;
		Ship.Position = FVector3d(ShipX, 0.0, 0.0);
		Ship.SetContactProbes({FVector3d(-0.06, 0.0, 0.0)});
		return Ship;
	};

	// ── 1. 坑：逐探针半径 0.05 比球半径 0.1 低，船落到 0.05 的面上 ──
	// 船心 0.09 + 探针 (−0.06) = 0.03，低于 0.05 → 穿透 0.02。
	// 让路比例：船 3/4、行星 1/4。
	{
		FNBodySimulator Simulator = MakeSimulator();

		FCosmosBodyState Ship = MakeShip(0.09);
		Ship.ContactProbeSurfaceRadii.Add(0.05);
		Ship.ContactSurfaceSphereIndex = 0;
		Simulator.AddBody(Ship);

		Simulator.StepExact(0.0);

		TestEqual(TEXT("船心让 3/4：0.09 + 0.015 = 0.105"),
			Simulator.GetBody(1).Position.X, 0.105, 1.0e-15);
		TestEqual(TEXT("行星反向让 1/4"),
			Simulator.GetBody(0).Position.X, -0.005, 1.0e-15);
		TestEqual(TEXT("探针恰好落在 0.05 的地表上"),
			(Simulator.GetBody(1).Position + FVector3d(-0.06, 0.0, 0.0)
				- Simulator.GetBody(0).Position).Size(), 0.05, 1.0e-15);
	}

	// ── 2. 同一个坑，船悬在 0.12：探针落在 0.06，比 0.05 高 → 不接触 ──
	// 这条是「停在空气上」的反向断言：装着逐探针半径时，探针已在坑底之下才算碰到。
	{
		FNBodySimulator Simulator = MakeSimulator();

		FCosmosBodyState Ship = MakeShip(0.12);
		Ship.ContactProbeSurfaceRadii.Add(0.05);
		Ship.ContactSurfaceSphereIndex = 0;
		Simulator.AddBody(Ship);

		Simulator.StepExact(0.0);

		TestEqual(TEXT("没碰到 0.05 的地表就不该被推开"),
			Simulator.GetBody(1).Position.X, 0.12, 1.0e-15);

		// 对照组：同样的摆位、不给逐探针半径，必须被 0.1 的球面拦住 ——
		// 否则上一条是恒真的。
		FNBodySimulator Control = MakeSimulator();
		Control.AddBody(MakeShip(0.12));
		Control.StepExact(0.0);
		TestTrue(TEXT("对照组：没有逐探针半径时会被球面拦住"),
			Control.GetBody(1).Position.X > 0.12 + 1.0e-6);
	}

	// ── 3. 索引对不上 → 退回球半径（0.1）──
	{
		FNBodySimulator Simulator = MakeSimulator();

		FCosmosBodyState Ship = MakeShip(0.09);
		Ship.ContactProbeSurfaceRadii.Add(0.05);
		Ship.ContactSurfaceSphereIndex = 99;
		Simulator.AddBody(Ship);

		Simulator.StepExact(0.0);

		// 半径按 0.1：穿透 0.1 − 0.03 = 0.07 → 船心 0.09 + 0.07×0.75 = 0.1425。
		TestEqual(TEXT("索引不匹配：按球半径算"),
			Simulator.GetBody(1).Position.X, 0.1425, 1.0e-15);
	}

	// ── 4. 数量对不上 → 退回球半径 ──
	{
		FNBodySimulator Simulator = MakeSimulator();

		FCosmosBodyState Ship = MakeShip(0.09);
		Ship.ContactProbeSurfaceRadii.Add(0.05);
		Ship.ContactProbeSurfaceRadii.Add(0.05);   // 探针只有 1 个
		Ship.ContactSurfaceSphereIndex = 0;
		Simulator.AddBody(Ship);

		Simulator.StepExact(0.0);

		TestEqual(TEXT("数量不匹配：按球半径算"),
			Simulator.GetBody(1).Position.X, 0.1425, 1.0e-15);
	}

	// ── 5. 抬高地形：逐探针半径**大于**球半径时，快速排除不能误杀 ──
	// 探针在船心（ContactProbeRadius = 0），船心距 0.15 ≥ 球半径 0.1 ——
	// 只按球半径做快速排除会把这条接触整个漏掉。
	{
		FNBodySimulator Simulator = MakeSimulator();

		FCosmosBodyState Ship;
		Ship.Mass = 1.0;
		Ship.Position = FVector3d(0.15, 0.0, 0.0);
		Ship.SetContactProbes({FVector3d::ZeroVector});
		Ship.ContactProbeSurfaceRadii.Add(0.2);
		Ship.ContactSurfaceSphereIndex = 0;
		Simulator.AddBody(Ship);

		Simulator.StepExact(0.0);

		// 穿透 0.2 − 0.15 = 0.05 → 船心 0.15 + 0.05×0.75 = 0.1875。
		TestEqual(TEXT("抬高地表：0.15 + 0.0375"),
			Simulator.GetBody(1).Position.X, 0.1875, 1.0e-15);
		TestEqual(TEXT("探针落在 0.2 的地表上"),
			(Simulator.GetBody(1).Position - Simulator.GetBody(0).Position).Size(),
			0.2, 1.0e-15);
	}

	// ── 6. 三个探针各带半径：最深的那一个说了算 ──
	// 探针 (−0.06) / (−0.08) / (−0.02)，船心 0.15 → 距球心 0.09 / 0.07 / 0.13。
	// 半径 {0.1, 0.06, 0.06} → 穿透 0.01 / −0.01 / −0.07：只有第一个是真穿透。
	{
		FNBodySimulator Simulator = MakeSimulator();

		FCosmosBodyState Ship;
		Ship.Mass = 1.0;
		Ship.Position = FVector3d(0.15, 0.0, 0.0);
		Ship.SetContactProbes({
			FVector3d(-0.06, 0.0, 0.0),
			FVector3d(-0.08, 0.0, 0.0),
			FVector3d(-0.02, 0.0, 0.0)});
		Ship.ContactProbeSurfaceRadii.Append({0.1, 0.06, 0.06});
		Ship.ContactSurfaceSphereIndex = 0;
		Simulator.AddBody(Ship);

		Simulator.StepExact(0.0);

		TestEqual(TEXT("最深的探针决定位移：0.15 + 0.0075"),
			Simulator.GetBody(1).Position.X, 0.1575, 1.0e-15);
	}

	// ── 7. 撞上即停与逐探针半径共存：速度仍钉到行星上 ──
	{
		FNBodySimulator Simulator = MakeSimulator();

		FCosmosBodyState Ship = MakeShip(0.09);
		Ship.Velocity = FVector3d(-0.01, 0.002, 0.0);
		Ship.bZeroVelocityOnContact = true;
		Ship.ContactProbeSurfaceRadii.Add(0.05);
		Ship.ContactSurfaceSphereIndex = 0;
		Simulator.AddBody(Ship);

		Simulator.StepExact(0.0);

		TestEqual(TEXT("撞上即停：速度 X 归零"),
			Simulator.GetBody(1).Velocity.X, 0.0, 1.0e-15);
		TestEqual(TEXT("撞上即停：速度 Y 也钉到行星上"),
			Simulator.GetBody(1).Velocity.Y, 0.0, 1.0e-15);
	}

	return true;
}
```

- [ ] **Step 2: 构建，确认测试编译失败**

Run: 构建命令
Expected: 编译错误，`FCosmosBodyState` 没有成员 `ContactProbeSurfaceRadii`

- [ ] **Step 3: 加字段**

`Source/StellarEraCosmos/Src/NBody/NBodySimulator.h`，在 `ContactProbeRadius` 与
`SetContactProbes` 之间插入两个字段：

```cpp
	/**
	 * 逐探针的真实地表半径（求解器单位 AU），与 ContactProbes 一一对应。
	 *
	 * 空数组 / 数量对不上 = 全部按对方的 Radius 球面接触（加这个字段之前的行为）。
	 * 由子系统每帧写一次（UCosmosSimulationSubsystem::RefreshShipContactProbes），
	 * 求解器只读 —— 求解器一帧最多跑 200 个子步，体素查询不能放在这里。
	 */
	TArray<double> ContactProbeSurfaceRadii;

	/**
	 * 上面这份半径是对着哪个天体（求解器下标）算的。INDEX_NONE = 没有。
	 *
	 * 接触是成对的：同一份半径拿去碰另一颗星就是错的，所以 ResolveProbeContact
	 * 只在「当前这颗星的下标 == 这里记录的下标」时才用逐探针半径。
	 */
	int32 ContactSurfaceSphereIndex = INDEX_NONE;
```

- [ ] **Step 4: 让 `SetContactProbes` 清空新字段**

把现有的 `SetContactProbes` 函数体改成：

```cpp
	/** 写入探针并同步 ContactProbeRadius。传空数组即退回球-球接触。 */
	void SetContactProbes(TArray<FVector3d> InProbes)
	{
		ContactProbes = MoveTemp(InProbes);
		ContactProbeRadius = 0.0;
		for (const FVector3d& Probe : ContactProbes)
		{
			ContactProbeRadius = FMath::Max(ContactProbeRadius, Probe.Size());
		}

		// 探针换了，逐探针地表半径就不再对应任何东西 —— 一并清掉，让接触退回
		// 球面，而不是拿旧世界的半径去碰。子系统每帧先调本函数、再写半径。
		ContactProbeSurfaceRadii.Reset();
		ContactSurfaceSphereIndex = INDEX_NONE;
	}
```

- [ ] **Step 5: 改 `ResolveProbeContact`**

把 `Source/StellarEraCosmos/Src/NBody/NBodySimulator.cpp` 的整个
`FNBodySimulator::ResolveProbeContact` 替换成：

```cpp
void FNBodySimulator::ResolveProbeContact(int32 ProbeIndex, int32 SphereIndex)
{
	FCosmosBodyState& ProbeBody = Bodies[ProbeIndex];
	FCosmosBodyState& SphereBody = Bodies[SphereIndex];

	const double SphereRadius = SphereBody.Radius;

	// 逐探针地表半径只在「算它的那颗球」上有效：索引对不上、或数量对不上
	// （探针重建过）时一律退回球面 —— 与加这个字段之前逐字等价。
	const bool bUsePerProbe =
		ProbeBody.ContactSurfaceSphereIndex == SphereIndex
		&& ProbeBody.ContactProbeSurfaceRadii.Num() == ProbeBody.ContactProbes.Num();

	// 快速排除：中心距 ≥ 排除半径 + 最远探针时不可能有探针进入表面。
	// 抬高地形时逐探针半径会**大于**球半径，所以排除半径取两者的较大者，
	// 否则那条接触会被整个漏掉。
	double RejectRadius = SphereRadius;
	if (bUsePerProbe)
	{
		for (const double ProbeSurfaceRadius : ProbeBody.ContactProbeSurfaceRadii)
		{
			RejectRadius = FMath::Max(RejectRadius, ProbeSurfaceRadius);
		}
	}

	const double CenterDistance = (ProbeBody.Position - SphereBody.Position).Size();
	if (CenterDistance >= RejectRadius + ProbeBody.ContactProbeRadius)
	{
		return;
	}

	// 找最深穿透的探针。法线取「球心 → 探针」，即从对方表面朝外。
	double DeepestPenetration = 0.0;
	FVector3d Normal = FVector3d(1.0, 0.0, 0.0);

	for (int32 Index = 0; Index < ProbeBody.ContactProbes.Num(); ++Index)
	{
		const FVector3d& Probe = ProbeBody.ContactProbes[Index];
		const double ProbeSurfaceRadius =
			bUsePerProbe ? ProbeBody.ContactProbeSurfaceRadii[Index] : SphereRadius;

		const FVector3d ProbeDelta = ProbeBody.Position + Probe - SphereBody.Position;
		const double Distance = ProbeDelta.Size();
		const double Penetration = ProbeSurfaceRadius - Distance;
		if (Penetration > DeepestPenetration)
		{
			DeepestPenetration = Penetration;
			// 探针正好落在球心时方向无定义，沿用上一个法线。
			if (Distance > 1e-12)
			{
				Normal = ProbeDelta / Distance;
			}
		}
	}

	if (DeepestPenetration <= 0.0)
	{
		return;
	}

	double ShareProbe = 0.0;
	double ShareSphere = 0.0;
	ComputeContactShares(ProbeBody.Mass, SphereBody.Mass, ShareProbe, ShareSphere);

	// ① 位置：推开到最深探针刚好落在它自己的地表半径上。与球-球分支同样的让路比例。
	ProbeBody.Position += Normal * (DeepestPenetration * ShareProbe);
	SphereBody.Position -= Normal * (DeepestPenetration * ShareSphere);

	// ② 速度：与球-球分支同一套规则（「撞上即停」特例 + 默认抹法向）。
	const bool bZeroProbe = ProbeBody.bZeroVelocityOnContact;
	const bool bZeroSphere = SphereBody.bZeroVelocityOnContact;

	if (bZeroProbe != bZeroSphere)
	{
		if (bZeroProbe)
		{
			ProbeBody.Velocity = SphereBody.Velocity;
		}
		else
		{
			SphereBody.Velocity = ProbeBody.Velocity;
		}
		return;
	}

	const double Approach = FVector3d::DotProduct(
		SphereBody.Velocity - ProbeBody.Velocity, Normal);

	if (Approach < 0.0)
	{
		ProbeBody.Velocity += Normal * (Approach * ShareProbe);
		SphereBody.Velocity -= Normal * (Approach * ShareSphere);
	}
}
```

- [ ] **Step 6: 构建并跑测试**

Run: 构建命令，然后
`-ExecCmds="Automation RunTests StellarEra.Cosmos"`
Expected: 全部 Success，含新增的 `StellarEra.Cosmos.NBodyProbeSurfaceRadii` 与
原有的 `NBodyProbeContact`（回归）

- [ ] **Step 7: 提交**

```bash
git commit -m "feat(求解器): 探针接触改用逐探针地表半径

FCosmosBodyState 加 ContactProbeSurfaceRadii / ContactSurfaceSphereIndex，
ResolveProbeContact 不再一律用行星那一颗球：索引或数量对不上时退回旧行为。
快速排除用「球半径与逐探针半径的最大者」，否则抬高地形会被整个漏掉。

Co-Authored-By: Claude Code <noreply@anthropic.com>" -- \
  Source/StellarEraCosmos/Src/NBody/NBodySimulator.h \
  Source/StellarEraCosmos/Src/NBody/NBodySimulator.cpp \
  Source/StellarEraCosmos/Src/Tests/NBodySimulatorTests.cpp
```

---

### Task 4: 子系统每帧填逐探针半径

`RefreshShipContactProbes` 里加一段：粗筛最近的行星 → 逐探针查真实地表半径
（方向去重）→ 写进 `FCosmosBodyState`。

**Files:**
- Modify: `Source/StellarEraCosmos/Src/CosmosSimulationSubsystem.h`（新增 `MaxContactSurfaceQueriesPerFrame`，放在 `MaxStepsPerFrame` 之后）
- Modify: `Source/StellarEraCosmos/Src/CosmosSimulationSubsystem.cpp`（`RefreshShipContactProbes` 的循环体末尾）

**Interfaces:**
- Consumes:
  - Task 2 的 `float AVoxelPlanetActor::GetVoxelSurfaceRadiusCmAtDirection(const FVector&) const`
  - Task 3 的 `FCosmosBodyState::ContactProbeSurfaceRadii` / `ContactSurfaceSphereIndex`
  - 已有：`Simulator.GetNumBodies()`、`Simulator.GetBody(i).OwnerActor`、
    `Planet->GetSurfaceRadiusCm()`、`Planet->GetSurfaceRadiusCmAtDirection(Dir)`、
    `Planet->VoxelSize`、成员 `CentimetersPerAu`
- Produces: 无（终端消费者是求解器）

- [ ] **Step 1: 加常量**

`Source/StellarEraCosmos/Src/CosmosSimulationSubsystem.h`，在 `MaxStepsPerFrame`
那个 `UPROPERTY` 之后追加：

```cpp
	/**
	 * 单帧最多做多少次「逐探针真实地表半径」的体素查询。
	 *
	 * 方向按「地表 1 体素」去重，所以正常情况下一帧只有个位数次；32 挡的是病态
	 * 情形（探针被缩放成一大把、地形极碎）。超了的方向退回解析半径。
	 */
	static constexpr int32 MaxContactSurfaceQueriesPerFrame = 32;
```

- [ ] **Step 2: 在 `RefreshShipContactProbes` 里加查询**

在 `Source/StellarEraCosmos/Src/CosmosSimulationSubsystem.cpp` 里找到
`RefreshShipContactProbes` 结尾这三行：

```cpp
		State.SetContactProbes(MoveTemp(Probes));
		State.Radius = 0.0;
	}
}
```

替换成：

```cpp
		State.SetContactProbes(MoveTemp(Probes));
		State.Radius = 0.0;

		// ── 逐探针的真实地表半径 ────────────────────────────────────────────
		// 求解器每个子步都会重算接触（一帧最多 MaxStepsPerFrame = 200 步），
		// 所以体素查询只能在这里做一次、查完缓存 —— 不能放进 ResolveProbeContact。
		//
		// SetContactProbes 已经把这两项清空；没有行星在范围内时就让它们保持空。

		// 粗筛：船心到最近行星的距离要落在包围球（1.25 × 基准半径，与
		// RaycastSurfaceCm 同一份余量）加最远探针之内，否则探针碰不到体素地表。
		int32 NearestPlanetIndex = INDEX_NONE;
		double NearestPlanetDistance = 0.0;

		for (int32 Other = 0; Other < Simulator.GetNumBodies(); ++Other)
		{
			if (Other == Component->SimulatorIndex)
			{
				continue;
			}

			const AVoxelPlanetActor* Candidate =
				Cast<AVoxelPlanetActor>(Simulator.GetBody(Other).OwnerActor.Get());
			if (!Candidate)
			{
				continue;
			}

			const double Distance =
				(State.Position - Simulator.GetBody(Other).Position).Size();
			const double GateCm = double(Candidate->GetSurfaceRadiusCm()) * 1.25
				+ State.ContactProbeRadius * CentimetersPerAu;
			if (Distance * CentimetersPerAu >= GateCm)
			{
				continue;
			}

			if (NearestPlanetIndex == INDEX_NONE || Distance < NearestPlanetDistance)
			{
				NearestPlanetIndex = Other;
				NearestPlanetDistance = Distance;
			}
		}

		if (NearestPlanetIndex == INDEX_NONE)
		{
			continue;
		}

		const AVoxelPlanetActor* Planet =
			Cast<AVoxelPlanetActor>(Simulator.GetBody(NearestPlanetIndex).OwnerActor.Get());
		const FVector3d PlanetPosition = Simulator.GetBody(NearestPlanetIndex).Position;

		// 方向量化的尺度：一格 ≈ 地表 1 体素。船体投影到地表通常只有个位数格，
		// 所以一帧真正 march 的次数是个位数，不是探针数。
		const double BaseRadiusVoxels =
			double(Planet->GetSurfaceRadiusCm()) / FMath::Max(double(Planet->VoxelSize), 1.0);

		TMap<FIntVector, double> SurfaceRadiusCache;
		int32 NumQueries = 0;

		TArray<double> SurfaceRadii;
		SurfaceRadii.Reserve(State.ContactProbes.Num());

		for (const FVector3d& Probe : State.ContactProbes)
		{
			const FVector UnitDirection =
				FVector(State.Position + Probe - PlanetPosition).GetSafeNormal();
			if (UnitDirection.IsNearlyZero())
			{
				// 探针正好落在星心：方向无定义，交给解析半径兜底。
				SurfaceRadii.Add(double(Planet->GetSurfaceRadiusCm()) / CentimetersPerAu);
				continue;
			}

			const FIntVector Key(
				FMath::RoundToInt(UnitDirection.X * BaseRadiusVoxels),
				FMath::RoundToInt(UnitDirection.Y * BaseRadiusVoxels),
				FMath::RoundToInt(UnitDirection.Z * BaseRadiusVoxels));

			if (const double* Cached = SurfaceRadiusCache.Find(Key))
			{
				SurfaceRadii.Add(*Cached);
				continue;
			}

			double SurfaceRadiusCm = 0.0;
			if (NumQueries >= MaxContactSurfaceQueriesPerFrame)
			{
				// 超上限：这一格退回解析半径。只在第一次触发时吭一声。
				if (NumQueries == MaxContactSurfaceQueriesPerFrame)
				{
					UE_LOG(LogTemp, Warning,
						TEXT("[Cosmos] 单帧逐探针地表查询达到上限 %d，其余方向退回解析半径。"),
						MaxContactSurfaceQueriesPerFrame);
				}
				SurfaceRadiusCm = double(Planet->GetSurfaceRadiusCmAtDirection(UnitDirection));
			}
			else
			{
				++NumQueries;
				SurfaceRadiusCm =
					double(Planet->GetVoxelSurfaceRadiusCmAtDirection(UnitDirection));
			}

			// 缓存与逐探针数组都存 AU —— 求解器单位。
			const double SurfaceRadiusAu = SurfaceRadiusCm / CentimetersPerAu;
			SurfaceRadiusCache.Add(Key, SurfaceRadiusAu);
			SurfaceRadii.Add(SurfaceRadiusAu);
		}

		State.ContactProbeSurfaceRadii = MoveTemp(SurfaceRadii);
		State.ContactSurfaceSphereIndex = NearestPlanetIndex;
	}
}
```

- [ ] **Step 3: 构建**

Run: 构建命令
Expected: `Result: Succeeded`

- [ ] **Step 4: 自查**

- [ ] 新代码在 `Advance` 之前（`Tick` 里 `RefreshPlanetContactRadii` /
      `RefreshShipContactProbes` 的调用位置没动）
- [ ] `SurfaceRadii` 的长度恒等于 `State.ContactProbes` 的长度（每条分支都 `Add` 一次）
- [ ] 缓存存的是 **AU**，与 `SurfaceRadii` 一致（混用会让半径差 1e6 倍）
- [ ] `State.ContactProbeRadius` 在 `SetContactProbes` 之后已经更新（用它做粗筛）
- [ ] 没有行星在范围内时 `continue`，`ContactSurfaceSphereIndex` 保持 `INDEX_NONE`

- [ ] **Step 5: 提交**

```bash
git commit -m "feat(宇宙): 子系统每帧为飞船探针算真实地表半径

RefreshShipContactProbes 里粗筛最近行星、按方向去重的缓存查体素地表，
写进 FCosmosBodyState.ContactProbeSurfaceRadii。查询全在 Advance 之前，
求解器内部仍零体素查询。

Co-Authored-By: Claude Code <noreply@anthropic.com>" -- \
  Source/StellarEraCosmos/Src/CosmosSimulationSubsystem.h \
  Source/StellarEraCosmos/Src/CosmosSimulationSubsystem.cpp
```

---

### Task 5: 全量验证

**Files:** 无代码改动（只跑命令、记录证据）。

**Interfaces:**
- Consumes: Task 1–4 的全部产物
- Produces: 验证结论（构建 + 测试日志 + PIE 观察）

- [ ] **Step 1: 关编辑器，全量构建**

Run: 构建命令
Expected: `Result: Succeeded`

- [ ] **Step 2: 跑全部自动化测试**

Run:

```bash
"D:/Application/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" \
  "F:/workspace/ue/StellarEra/StellarEra.uproject" \
  -ExecCmds="Automation RunTests StellarEra" \
  -TestExit="Automation Test Queue Empty" \
  -unattended -nopause -nullrhi -nosplash -stdout -abslog="%TEMP%/stellar_tests.log"
```

Expected: 进程 0 退出，日志里出现 `Automation Test Queue Empty`。
用 `grep -c "Result.*Fail"` 复核日志，期望 0。
**注意**：既有测试里若本来就有红的，先记下来、别当成这次引入的 —— 在报告里
点名是哪几条、改动前后是否一致。

- [ ] **Step 3: PIE 人工验收（`UniverseMap`）**

这条**没有自动化覆盖**（真实体素世界在无头测试里建不起来）。逐条做、记结果：

- [ ] 挖一个坑（深度至少几米），飞船飞进坑上方 → 会落进坑底，不再停在坑口
- [ ] 落在坑底时 HUD 的「地表高度」与坑底一致（不是解析地表的高度）
- [ ] 平地着陆、起飞、贴地横飞 → 与改动前一致，不抖、不被弹回解析球面
- [ ] 从坑里飞出来 → 平滑过渡回正常着陆高度
- [ ] 日志里**不应**出现 `[Cosmos] 单帧逐探针地表查询达到上限`

- [ ] **Step 4: 记录结论**

把 Step 2 的测试条数与 Step 3 的观察写进最终报告。若 Step 3 有不符合的，
回到对应 Task 修 —— 不要改测试去迁就。

- [ ] **Step 5: 收尾提交（仅当 Step 3 暴露了要改的东西）**

把现象写进提交标题（例如「从坑里飞出来时被弹回解析球面」），pathspec 只列这一步
真正改过的文件：

```bash
git diff --name-only          # 先看这一轮到底动了什么
git commit -m "fix(飞船接触): 从坑里飞出来时被弹回解析球面

Co-Authored-By: Claude Code <noreply@anthropic.com>" -- Source/StellarEraPlanet/Src/VoxelPlanetActor.cpp
```

（上面这行 pathspec 只是格式示例 —— 换成 `git diff --name-only` 给出的实际文件名。）

---

## 附：任务依赖图

```
Task 1 (纯函数)  ──→  Task 2 (行星查询)  ─┐
                                          ├─→  Task 4 (子系统接线)  ─→  Task 5 (验证)
Task 3 (求解器字段与分支)  ───────────────┘
```

Task 1–3 之间只有 1 → 2 是硬依赖；3 可以与 1、2 并行。Task 4 必须等 2 和 3。
