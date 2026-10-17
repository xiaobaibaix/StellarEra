# 星球轨迹系统 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 给 N 体系统里的每颗天体画出三样东西 —— 未来轨迹（浅色、补一点成闭合）、已走过的拖尾（深色）、初始速度矢量。

**Architecture:** 求解器加三个**只读**的预测方法（复制一份值类型求解器、推进副本、记录目标天体，原对象一个字节不动）；子系统加三个薄转发查询；新增一个哑渲染组件 `UCosmosTrajectoryComponent` 持有引擎自带的 `ULineBatchComponent` 画线，它的参数全从已有的 `UCosmosOrbitComponent` 上读 —— 于是所有可调项都在详情面板里有取色器、逐天体可覆盖，且**零编辑器侧手工操作**（不用动蓝图、不用手动加组件）。

**Tech Stack:** UE 5.8 / C++（4 模块 UBT 项目）、`ULineBatchComponent`、`IMPLEMENT_SIMPLE_AUTOMATION_TEST`

**Spec:** `docs/superpowers/specs/2026-10-17-trajectory-system-design.md`

## Global Constraints

- **构建**（编辑器必须**先关掉** —— Live Coding 会挡住 UBT）：
  `powershell -NoProfile -File "C:/Users/23080/AppData/Local/Temp/ue_mcp/ue_build.ps1"`
  这个脚本用捆绑 dotnet 直调 `UnrealBuildTool.dll`、cwd 在 `C:\`。**不要用 `Build.bat`** —— 它的锁机制会让 UBT 报「找不到 .uproject」。
  当前有个后台 shell `bo414k4ug` 正开着编辑器 GUI，构建前先用 `TaskStop` 把它停掉。
- **UTF-8 BOM**：任何**含中文**的源文件必须带 BOM（`EF BB BF`），否则 VS Code 的 IntelliSense 按 GBK 误读、满屏假红波浪线。新文件写完后跑：
  `powershell -NoProfile -File "C:/Users/23080/AppData/Local/Temp/ue_mcp/add_bom.ps1" -Path "<绝对路径>"`
  改动**已有**文件后也要复查 BOM 还在（Edit/Write 可能把它剥掉）：
  `head -c3 <file> | xxd -p` 应输出 `efbbbf`。
- **换行**：不强制。**不要**为了「统一」去转换既有文件的换行 —— 仓库本来就是混的
  （`Source/` 里 17 个 LF、8 个 CRLF，`core.autocrlf=true` 下 LF 是过渡态，git 下次碰它会转成 CRLF）。
  新文件写成哪种都行。真正要紧的是上面那条 **UTF-8 BOM**，与换行无关。
- **文件头**：项目里的源文件统一以 `// Copyright Voxel Plugin SAS. All Rights Reserved.` 开头（`NBodySimulator.h` 是唯一例外，用了一句中文说明）。新文件照这个来。
- **插件纯净**：自定义代码**一律不进** `Plugins/VoxelFree`。本计划全部落在项目自己的 `Source/StellarEra`（框架）与 `Source/StellarEraCore`（天体力学）里。
- **不改任何 `Build.cs`**：`ULineBatchComponent` 在 `Engine` 模块里，而两个目标模块都已经依赖 `Engine`。新增源文件由 UBT 自动 glob 到，也不需要登记。
- **C4458 是硬错误**（UE 5.8）：局部变量**不要**与继承成员同名（`Owner` / `World` / `Tick`）。用 `OwnerActor` / `OwningWorld` 之类。
- **IDE 是 VS Code**：没有也不该有 `.vcxproj` / `.sln`，不要去生成它们。
- **`Config/DefaultEngine.ini` 里的 `[CoreRedirects]` 是永久兼容层，不要删。** 本计划不需要动它（全是新增类，没有东西换模块）。
- **提交**：本项目规则是**只在用户明确要求时提交**。所以下面每个 Task 的末尾**都不自动提交**；Task 7 里给出提交命令，等用户发话再执行。`git add` 必须用**显式完整路径**（`Content/` 长期是脏的）。
- **单位速查**：游戏单位 AU / 年 / M☉，`G = 4π² = 39.478417604357434`，`CentimetersPerAu = 1e6`（1 AU = 10 公里），`TimeScale = 0.00333` 年/秒。因为 G = 4π²，配对周期 `2π√(r³/(G·M))` 化简成 **`√(r³/M)`** —— 0.718 AU 绕 1 M☉ 是 **0.608 年**，不是 3.82 年。这个数字看着小是单位制使然，不是笔误。

## 关键引擎事实（已对着 UE 5.8 源码核实，不是猜的）

| 事实 | 出处 | 对我们的意义 |
|---|---|---|
| `UCLASS(MinimalAPI)`，但 `DrawLine` / `DrawLines` / `Flush` / `CreateSceneProxy` / `CalcBounds` / `TickComponent` 各自带 `ENGINE_API` | `Engine/Classes/Components/LineBatchComponent.h:127` | 跨模块可用。头文件在 `Classes/` 下，属 Engine 的公开包含路径 |
| `DrawLines(TArrayView<FBatchedLine>)` = 一次 `Append` + 一次 `MarkRenderStateDirty`；注释原文「faster than calling DrawLine many times」 | `.cpp` `DrawLines` | 771 条线一次交出去，而不是 771 次调用 |
| `DrawLines` / `DrawLine` **只追加，不清空** | 同上 | 每次重画前必须自己先 `Flush()` |
| `Flush()` 清空三个缓冲并 `MarkRenderStateDirty()` | `.cpp` `Flush` | 每帧重画前调一次 |
| `DrawLine` 把 `LifeTime` 原样写进 `FBatchedLine::RemainingLifeTime`，不回退到 `DefaultLifeTime`；`TickComponent` 只递减 `RemainingLifeTime > 0` 的线 | `.cpp:158-160` + `TickComponent` | `LifeTime = 0` 的线**永不过期**，一直留到 `Flush()` —— 这正是「画一次、要清时整体清」 |
| `Thickness = 0` 走 `LineVertices`（真正的 1 像素 GPU 线）；`Thickness > 0` 走 `FBatchedThickLines`（**世界单位**的四边形，因为行批把 `bScreenSpace` 写死成 `false`） | `BatchedElements.cpp:165` + `PrimitiveDrawInterface.h:65` | **线宽必须传 0**：1 像素、与距离无关。传正值近了变粗带、远了消失 |
| `FBatchedLine` 构造参数序 `(Start, End, Color, LifeTime, Thickness, DepthPriority, BatchID)`，而 `DrawLine` 的函数参数序是 `(..., DepthPriority, Thickness, LifeTime, ...)` | `LineBatchComponent.h` 两处 | **两者相反**。自己填数组时按前者；写反了不报错（都是 float），但会把寿命当粗细 |
| 行批的线是**世界坐标**：代理直接 `PDI->DrawTranslucentLine(Line.Start, Line.End, ...)`，不套自己的变换 | `.cpp:78` | 线批组件**不要挂到移动中的星球下**（那只会让包围盒跟着跑、算错）。引擎自己也这么用：`NewObject<ULineBatchComponent>()` + `RegisterComponentWithWorld`，不挂父 |
| `Development` 构建里 `UE_ENABLE_DEBUG_DRAWING` 为真 → 走 `GetDebugPDI`；它是 `FSimpleElementCollector`（真 PDI），不是空实现 | `EngineDefines.h:15` + `SceneManagement.cpp:484` | Development 下能正常渲染，不必担心「调试路径被编译掉」 |
| `ULineBatchComponent` 自己的注释：「This can be useful for debug drawing, but is not very performant for runtime use.」 | 类注释 | 每帧不到 4 千条线、一次 `Flush` + 一次 `Append` 落在它撑得住的范围内，但帧时间检查是必做项 |

## 文件结构

**新增（3）**

| 文件 | 职责 |
|---|---|
| `Source/StellarEra/Public/CosmosTrajectoryComponent.h` | 轨迹渲染组件声明。无面向用户属性，只持有渲染状态 |
| `Source/StellarEra/Private/CosmosTrajectoryComponent.cpp` | 拖尾采样、预测重算、整批画线 |
| `Source/StellarEraCore/Private/NBodySimulatorTests.cpp` | 预测功能的自动化测试（纯数学，唯一能自动验证的地方） |

**修改（6）**

| 文件 | 改什么 |
|---|---|
| `Source/StellarEraCore/Public/NBodySimulator.h` | 三个预测方法声明 + 私有 `GetG()` |
| `Source/StellarEraCore/Private/NBodySimulator.cpp` | 三个方法实现 + `GetG()` 并消掉两处重复的 G 三元表达式 |
| `Source/StellarEra/Public/CosmosSimulationSubsystem.h` | 三个查询声明 |
| `Source/StellarEra/Private/CosmosSimulationSubsystem.cpp` | 三个实现 + `FinalizeRegistration` 改用 `PairOrbitalPeriod` |
| `Source/StellarEra/Public/CosmosOrbitComponent.h` | 8 个 `Cosmos\|Trajectory` 属性 + 缓存的 `InitialWorldLocation` |
| `Source/StellarEra/Private/CosmosOrbitComponent.cpp` | BeginPlay 缓存初始位置 + 创建渲染组件 |

**不需要动**：任何 `Build.cs`、`.uproject`、`Config/*.ini`、任何蓝图或资产。

**与 spec 的三处有意偏差**（都写在这里，免得执行时以为是漏了）：

1. **拖尾用「先进先出」而不是 spec §7 说的环形缓冲。** 采样间隔是标称值 `TrailInterval = TrailSpan / TrailSamples`，而**保留的点数随帧率变，但点数 ≤ `TrailSamples + 1` = 513**（裁剪按模拟时间），`RemoveAt(0)` 的 O(n) 搬迁量仍然很小 —— 完全不值得为它写一套取模索引，而 FIFO 天然就是画线要的先后顺序。行为等价，bug 面更小。
   **裁剪按模拟时间、不按点数** —— 只按时间采样是不够的，见 Task 6 Step 2 的论证。
2. **spec §4 只列了两个子系统查询，这里要三个。** 自动预测窗口需要「该天体参与的最短周期」，所以必须再加一个 `GetOrbitalPeriodYears`。
3. **spec §9 的闭合断言在 plan 里改成了「正好一圈」。** 1.05 圈对应 18° 弧、末点离首点 31% 的半径 —— 那是故意画的「补一点」，不是闭合误差，用它当断言会永远失败。（spec 已同步更正。）

---

### Task 1: Core 的三个预测方法 + 自动化测试

这是唯一能自动验证的任务，也是唯一纯数学的任务。TDD 在这里是真的（先写测试、看它编译失败、再实现）。

**Files:**
- Modify: `Source/StellarEraCore/Public/NBodySimulator.h`（在 `private:` 之前加一节；`private:` 里加 `GetG()`）
- Modify: `Source/StellarEraCore/Private/NBodySimulator.cpp`
- Test: `Source/StellarEraCore/Private/NBodySimulatorTests.cpp`（新建）

**Interfaces:**
- Consumes: 已有的 `FCosmosBodyState`、`FNBodySimulator`、`CosmosUnits::GravitationalConstant_Game`
- Produces:
  - `static double FNBodySimulator::PairOrbitalPeriod(const FCosmosBodyState& A, const FCosmosBodyState& B, double G)`
  - `double FNBodySimulator::EstimateOrbitalPeriod(int32 BodyIndex) const`
  - `bool FNBodySimulator::PredictTrajectory(int32 BodyIndex, double Horizon, int32 NumSamples, TArray<FVector3d>& OutPositions) const`
  - `double FNBodySimulator::GetG() const`（私有）

**为什么能保证不改原对象：** 积分器的全部状态就是每颗星的 `(Position, Velocity)` —— `StepVelocityVerlet` 只读 `Velocity` 推位置、然后重算 `Acceleration`，从不把 `Acceleration` 读回来当状态。所以**复制一份求解器就是复制了完整状态**，预测推进副本在结构上不可能影响原对象。

- [ ] **Step 1: 写失败的测试**

新建 `Source/StellarEraCore/Private/NBodySimulatorTests.cpp`：

```cpp
// Copyright Voxel Plugin SAS. All Rights Reserved.
//
// N 体求解器「预测」功能的自动化测试。
//
// 这几组断言挡的是同一类错误：预测看起来像那么回事、其实是错的。
// 全是纯数学 —— 不依赖世界、不依赖编辑器，这是本项目唯一能自动验证的地方。
//
// 跑法见 plan 的 Task 1 Step 4。

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "NBodySimulator.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	/** 造一个「大质量中心 + 一颗圆轨道行星」的两体系统。 */
	FNBodySimulator MakeCircularTwoBody(double OrbitRadiusAu, double CentralMassSolar)
	{
		FNBodySimulator Simulator;
		Simulator.SetUnitSystem(ECosmosUnitSystem::Game);
		Simulator.SetIntegrator(ECosmosIntegrator::VelocityVerlet);
		Simulator.SetSofteningLength(1.0e-5);
		Simulator.SetCentimetersPerAu(1.0e6);

		FCosmosBodyState Central;
		Central.Mass = CentralMassSolar;
		Central.Position = FVector3d::ZeroVector;
		Simulator.AddBody(Central);

		// 圆轨道速度 v = √(G·M/r)。G = 4π² 时也就是 2π√(M/r)。
		const double Speed = FMath::Sqrt(
			CosmosUnits::GravitationalConstant_Game * CentralMassSolar / OrbitRadiusAu);

		FCosmosBodyState Planet;
		Planet.Mass = 3.0034893e-6;   // 地球质量
		Planet.Position = FVector3d(OrbitRadiusAu, 0.0, 0.0);
		Planet.Velocity = FVector3d(0.0, Speed, 0.0);
		Simulator.AddBody(Planet);

		Simulator.MarkEnergyBaseline();
		return Simulator;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraNBodyPredictionTest,
	"StellarEra.Core.NBodyPrediction",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraNBodyPredictionTest::RunTest(const FString& Parameters)
{
	const double G = CosmosUnits::GravitationalConstant_Game;

	// ── 1. 配对周期公式：钉住具体数值 ──
	// 期望值是 √(r³/(m_A+m_B))，因为 2π 与 G = 4π² 抵消。写成小数值而不是再写一遍
	// 公式，是为了让「G 改了」或「漏了/多了个 2π」真的会让测试红。
	{
		FCosmosBodyState A;
		A.Mass = 1.0;
		A.Position = FVector3d::ZeroVector;

		FCosmosBodyState B;
		B.Mass = 1.0;
		B.Position = FVector3d(1.0, 0.0, 0.0);
		TestEqual(TEXT("周期 1AU/两倍太阳质量"),
			FNBodySimulator::PairOrbitalPeriod(A, B, G), 0.70710678, 1.0e-6);

		B.Position = FVector3d(4.0, 0.0, 0.0);
		TestEqual(TEXT("周期 4AU/两倍太阳质量"),
			FNBodySimulator::PairOrbitalPeriod(A, B, G), 5.65685425, 1.0e-6);

		// 本关卡的真实几何：0.718 AU 绕 1 M☉（行星质量 3e-6 可忽略）。
		B.Mass = 3.0034893e-6;
		B.Position = FVector3d(0.718, 0.0, 0.0);
		TestEqual(TEXT("周期 0.718AU/1太阳质量（本关卡）"),
			FNBodySimulator::PairOrbitalPeriod(A, B, G), 0.60840673, 1.0e-6);

		// 退化情形必须返回 0（而不是 inf / nan）。
		B.Position = A.Position;
		TestEqual(TEXT("间距为零返回 0"),
			FNBodySimulator::PairOrbitalPeriod(A, B, G), 0.0);

		B.Position = FVector3d(1.0, 0.0, 0.0);
		B.Mass = 0.0;
		A.Mass = 0.0;
		TestEqual(TEXT("总质量为零返回 0"),
			FNBodySimulator::PairOrbitalPeriod(A, B, G), 0.0);
	}

	// ── 2. 最短时标取的是「配对最小值」 ──
	// 恒星 + 近行星(0.5AU) + 远行星(4AU)。恒星与近行星那一对最短。
	{
		FNBodySimulator Simulator;
		Simulator.SetUnitSystem(ECosmosUnitSystem::Game);

		FCosmosBodyState Star;
		Star.Mass = 1.0;
		Simulator.AddBody(Star);

		FCosmosBodyState NearPlanet;
		NearPlanet.Mass = 3.0034893e-6;
		NearPlanet.Position = FVector3d(0.5, 0.0, 0.0);
		Simulator.AddBody(NearPlanet);

		FCosmosBodyState FarPlanet;
		FarPlanet.Mass = 3.0034893e-6;
		FarPlanet.Position = FVector3d(4.0, 0.0, 0.0);
		Simulator.AddBody(FarPlanet);

		// √(0.5³/1) 与 √(4³/1)
		TestEqual(TEXT("恒星的最短时标 = 与近行星那一对"),
			Simulator.EstimateOrbitalPeriod(0), 0.35355339, 1.0e-6);
		TestEqual(TEXT("近行星的最短时标 = 与恒星那一对"),
			Simulator.EstimateOrbitalPeriod(1), 0.35355339, 1.0e-6);
		TestEqual(TEXT("远行星的最短时标 = 与恒星那一对"),
			Simulator.EstimateOrbitalPeriod(2), 8.0, 1.0e-6);

		// 只有一颗天体时无解。
		FNBodySimulator Single;
		Single.SetUnitSystem(ECosmosUnitSystem::Game);
		Single.AddBody(Star);
		TestEqual(TEXT("单天体系统的时标为 0"),
			Single.EstimateOrbitalPeriod(0), 0.0);
	}

	// ── 3. 预测一圈之后闭合 ──
	{
		FNBodySimulator Simulator = MakeCircularTwoBody(0.718, 1.0);
		const double Period = Simulator.EstimateOrbitalPeriod(1);
		TestTrue(TEXT("估算周期为正"), Period > 0.0);

		TArray<FVector3d> Predicted;
		TestTrue(TEXT("预测调用成功"),
			Simulator.PredictTrajectory(1, Period, 256, Predicted));
		TestEqual(TEXT("预测点数 = 采样数"), Predicted.Num(), 256);

		const double ClosureError = (Predicted.Last() - Predicted[0]).Size();
		TestTrue(TEXT("一圈之后末点回到首点附近（< 1% 轨道半径）"),
			ClosureError < 0.01 * 0.718);
	}

	// ── 4. 预测不得改动被预测的求解器 ──
	// 这条挡的是最危险的 bug：预测把真实模拟带跑偏。那种错在 PIE 里表现为
	// 「轨道慢慢变形」，很容易被当成积分器精度问题查半天。
	{
		FNBodySimulator Simulator = MakeCircularTwoBody(0.718, 1.0);

		TArray<FVector3d> PositionsBefore;
		TArray<FVector3d> VelocitiesBefore;
		for (int32 i = 0; i < Simulator.GetNumBodies(); ++i)
		{
			PositionsBefore.Add(Simulator.GetBody(i).Position);
			VelocitiesBefore.Add(Simulator.GetBody(i).Velocity);
		}
		const double TimeBefore = Simulator.GetSimulatedTime();
		const double DriftBefore = Simulator.GetEnergyDriftRatio();
		const int32 CountBefore = Simulator.GetNumBodies();

		TArray<FVector3d> Predicted;
		Simulator.PredictTrajectory(1, 0.608, 64, Predicted);

		TestEqual(TEXT("预测后天体数不变"), Simulator.GetNumBodies(), CountBefore);
		TestTrue(TEXT("预测后模拟时间逐位不变"),
			Simulator.GetSimulatedTime() == TimeBefore);
		TestTrue(TEXT("预测后能量漂移逐位不变"),
			Simulator.GetEnergyDriftRatio() == DriftBefore);

		for (int32 i = 0; i < Simulator.GetNumBodies(); ++i)
		{
			const FCosmosBodyState& State = Simulator.GetBody(i);
			TestTrue(*FString::Printf(TEXT("预测后天体 %d 位置逐位不变"), i),
				State.Position == PositionsBefore[i]);
			TestTrue(*FString::Printf(TEXT("预测后天体 %d 速度逐位不变"), i),
				State.Velocity == VelocitiesBefore[i]);
		}
	}

	// ── 5. 非法入参走失败分支 ──
	{
		FNBodySimulator Simulator = MakeCircularTwoBody(0.718, 1.0);
		TArray<FVector3d> Predicted;

		Predicted.Add(FVector3d(1.0, 2.0, 3.0));
		TestFalse(TEXT("索引越界返回 false"), Simulator.PredictTrajectory(99, 1.0, 16, Predicted));
		TestEqual(TEXT("失败时清空输出"), Predicted.Num(), 0);

		TestFalse(TEXT("窗口为零返回 false"), Simulator.PredictTrajectory(1, 0.0, 16, Predicted));
		TestFalse(TEXT("采样点不足返回 false"), Simulator.PredictTrajectory(1, 1.0, 1, Predicted));
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
```

- [ ] **Step 2: 加 BOM，然后跑构建确认它失败**

```bash
powershell -NoProfile -File "C:/Users/23080/AppData/Local/Temp/ue_mcp/add_bom.ps1" \
  -Path "F:/workspace/ue/StellarEra/Source/StellarEraCore/Private/NBodySimulatorTests.cpp"
powershell -NoProfile -File "C:/Users/23080/AppData/Local/Temp/ue_mcp/ue_build.ps1"
```

预期：**编译失败**，报 `PairOrbitalPeriod` / `EstimateOrbitalPeriod` / `PredictTrajectory` 不是 `FNBodySimulator` 的成员。这正是 TDD 的第一步 —— 测试还没法编译，因为被测的东西还不存在。

- [ ] **Step 3: 实现三个方法**

在 `Source/StellarEraCore/Public/NBodySimulator.h` 的 `private:` **之前**插入：

```cpp
	//////////////////////////////////////////////////////////////////////
	// 预测 —— 只读，不改本对象

	/**
	 * 一对天体相互绕转的周期：T = 2π√( r³ / (G·(m_i + m_j)) )，r 取当前间距。
	 *
	 * 注意项目里 G = 4π²，所以 2π 与 G 抵消，游戏单位下这个式子化简成
	 * √(r³/(m_i+m_j))。看着「偏小」的周期（0.718 AU 绕 1 M☉ → 0.608 年）
	 * 就是这么来的，不是写错。
	 *
	 * G 由调用方传入而不是从 UnitSystem 推：这样它是纯函数，能脱离求解器
	 * 单独测试。间距或质量之和非正时返回 0，表示「这一对没有有限的绕转周期」。
	 */
	static double PairOrbitalPeriod(const FCosmosBodyState& A, const FCosmosBodyState& B, double G);

	/** 天体 i 参与的最短动力学时标 = min over j≠i 的 PairOrbitalPeriod。无解返回 0。 */
	double EstimateOrbitalPeriod(int32 BodyIndex) const;

	/**
	 * 预测天体 i 未来 Horizon 年的轨迹，输出 AU 坐标序列。
	 *
	 * 含首尾共 NumSamples 个点：首点 = 当前位置，末点 = 恰好 Horizon 处。
	 * **不修改本对象** —— 内部复制一份求解器再推进副本。之所以这样是安全的：
	 * 积分器的全部状态就是每颗星的 (Position, Velocity)，复制求解器即复制状态。
	 *
	 * 返回 false（并清空 OutPositions）当：索引无效、Horizon ≤ 0、NumSamples < 2。
	 */
	bool PredictTrajectory(int32 BodyIndex, double Horizon, int32 NumSamples,
	                       TArray<FVector3d>& OutPositions) const;
```

在 `private:` 段里、`void ComputeAccelerations();` 上方插入：

```cpp
	/** 当前单位制下的引力常数。ComputeAccelerations / GetTotalEnergy / 预测三处共用。 */
	double GetG() const;
```

在 `Source/StellarEraCore/Private/NBodySimulator.cpp` 的 `ComputeAccelerations` 上方插入：

```cpp
double FNBodySimulator::GetG() const
{
	return (UnitSystem == ECosmosUnitSystem::Game)
		? CosmosUnits::GravitationalConstant_Game
		: CosmosUnits::GravitationalConstant_SI;
}
```

把 `ComputeAccelerations` 里这两行

```cpp
	const double G = (UnitSystem == ECosmosUnitSystem::Game)
		? CosmosUnits::GravitationalConstant_Game
		: CosmosUnits::GravitationalConstant_SI;
```

替换成

```cpp
	const double G = GetG();
```

`GetTotalEnergy` 里有一模一样的三行，同样替换成 `const double G = GetG();`。（纯文本替换，语义逐字相同 —— Task 2 的回归检查会确认模拟行为没变。）

在 `NBodySimulator.cpp` 末尾追加：

```cpp
///////////////////////////////////////////////////////////////////////////////
// 预测 —— 只读

double FNBodySimulator::PairOrbitalPeriod(const FCosmosBodyState& A, const FCosmosBodyState& B, double G)
{
	const double Separation = (B.Position - A.Position).Size();
	const double Mu = G * (A.Mass + B.Mass);

	// 与 FinalizeRegistration 里原来那段判定完全一致：间距为零或总质量为零时
	// 这一对没有意义，返回 0 让调用方跳过。
	if (Separation <= 0.0 || Mu <= 0.0)
	{
		return 0.0;
	}

	return 2.0 * PI * FMath::Sqrt(Separation * Separation * Separation / Mu);
}

double FNBodySimulator::EstimateOrbitalPeriod(int32 BodyIndex) const
{
	if (!Bodies.IsValidIndex(BodyIndex))
	{
		return 0.0;
	}

	const double G = GetG();
	double Shortest = 0.0;

	for (int32 j = 0; j < Bodies.Num(); ++j)
	{
		if (j == BodyIndex)
		{
			continue;
		}

		const double Period = PairOrbitalPeriod(Bodies[BodyIndex], Bodies[j], G);
		if (Period > 0.0 && (Shortest <= 0.0 || Period < Shortest))
		{
			Shortest = Period;
		}
	}

	return Shortest;
}

bool FNBodySimulator::PredictTrajectory(int32 BodyIndex, double Horizon, int32 NumSamples,
                                        TArray<FVector3d>& OutPositions) const
{
	OutPositions.Reset();

	if (!Bodies.IsValidIndex(BodyIndex) || Horizon <= 0.0 || NumSamples < 2)
	{
		return false;
	}

	// 值类型，复制即得一份完整状态（见头文件注释）。
	FNBodySimulator Copy = *this;

	// 步长取 Horizon / (NumSamples - 1)，于是末点**恰好**落在 Horizon 上，
	// 而不是 Horizon 差一步的位置。
	const double Step = Horizon / double(NumSamples - 1);

	OutPositions.Reserve(NumSamples);
	OutPositions.Add(Copy.GetBody(BodyIndex).Position);

	for (int32 SampleIndex = 1; SampleIndex < NumSamples; ++SampleIndex)
	{
		Copy.StepExact(Step);
		OutPositions.Add(Copy.GetBody(BodyIndex).Position);
	}

	return true;
}
```

- [ ] **Step 4: 跑构建 + 跑测试**

```bash
powershell -NoProfile -File "C:/Users/23080/AppData/Local/Temp/ue_mcp/ue_build.ps1"
```

预期 `Result: Succeeded`，无新增警告。

然后跑测试。**先试无头**：

```bash
"/d/Application/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" \
  "F:/workspace/ue/StellarEra/StellarEra.uproject" \
  -ExecCmds="Automation RunTests StellarEra.Core.NBodyPrediction" \
  -TestExit="Automation Test Queue Empty" \
  -unattended -nopause -nullrhi -log 2>&1 | tail -40
```

预期输出里有 `Test Completed. Result={Success}` 与 `1 test(s) started`。

> **如果这条命令挂住、或者报 `0 test(s)` 启动**：这个环境里之前遇到过引擎主循环在无头模式下不推进的情况。那就**改用编辑器的界面跑**，这条路一定可行：
> 1. 打开编辑器（`UnrealEditor.exe <uproject>`）
> 2. `Tools → Session Frontend`（或 `Window → Test Automation`）→ **Automation** 标签页
> 3. 过滤框输入 `StellarEra`，勾选 `StellarEra.Core.NBodyPrediction`
> 4. 点 **Start Tests**，看结果是不是全绿
>
> 界面这条路永远可用，所以测试本身不是「跑不了的假测试」；无头那条只是省事。

- [ ] **Step 5: 复查 BOM**

```bash
for f in Source/StellarEraCore/Public/NBodySimulator.h \
         Source/StellarEraCore/Private/NBodySimulator.cpp \
         Source/StellarEraCore/Private/NBodySimulatorTests.cpp ; do
  printf '%-60s %s\n' "$f" "$(head -c3 "$f" | xxd -p)"
done
```

三个都必须是 `efbbbf`。若有缺的，重跑 `add_bom.ps1`。

---

### Task 2: 子系统查询 + `FinalizeRegistration` 复用配对周期

**Files:**
- Modify: `Source/StellarEra/Public/CosmosSimulationSubsystem.h`
- Modify: `Source/StellarEra/Private/CosmosSimulationSubsystem.cpp:155-179`（`FinalizeRegistration` 的自动步长段）

**Interfaces:**
- Consumes: Task 1 的 `FNBodySimulator::PairOrbitalPeriod` / `EstimateOrbitalPeriod` / `PredictTrajectory`
- Produces:
  - `bool UCosmosSimulationSubsystem::PredictTrajectoryWorld(int32 SimulatorIndex, double HorizonYears, int32 NumSamples, TArray<FVector>& OutWorldPositions) const`
  - `double UCosmosSimulationSubsystem::GetSimulatedTimeYears() const`
  - `double UCosmosSimulationSubsystem::GetOrbitalPeriodYears(int32 SimulatorIndex) const`

- [ ] **Step 1: 加三个查询声明**

在 `CosmosSimulationSubsystem.h` 的 `void LogDiagnostics() const;` **之后**插入：

```cpp
	//////////////////////////////////////////////////////////////////////////
	// 只读查询 —— 供轨迹渲染层用，渲染层因此不必碰求解器

	/**
	 * 预测某个天体未来 HorizonYears 年的轨迹，输出**世界坐标（厘米）**。
	 * AU → 厘米 的换算在这里做。
	 *
	 * 返回 false（并清空 OutWorldPositions）当：索引无效、窗口非正、采样点少于 2。
	 */
	bool PredictTrajectoryWorld(int32 SimulatorIndex, double HorizonYears, int32 NumSamples,
	                            TArray<FVector>& OutWorldPositions) const;

	/** 已模拟的天文时间（年）。拖尾按模拟时间采样，不按帧 —— 否则覆盖时长会随帧率漂移。 */
	double GetSimulatedTimeYears() const;

	/** 天体 i 参与的最短动力学时标（年）。算不出（单天体 / 退化配对）返回 0。 */
	double GetOrbitalPeriodYears(int32 SimulatorIndex) const;
```

- [ ] **Step 2: 加三个实现**

在 `CosmosSimulationSubsystem.cpp` 的 `LogDiagnostics()` 之后插入：

```cpp
bool UCosmosSimulationSubsystem::PredictTrajectoryWorld(int32 SimulatorIndex, double HorizonYears,
                                                        int32 NumSamples, TArray<FVector>& OutWorldPositions) const
{
	OutWorldPositions.Reset();

	// 输出走调用方给的数组；AU → 厘米 的中间结果用一个局部数组接一下。
	// 每帧每个天体多两次分配，N ≤ 10 时无关紧要。
	TArray<FVector3d> GameUnitPositions;
	if (!Simulator.PredictTrajectory(SimulatorIndex, HorizonYears, NumSamples, GameUnitPositions))
	{
		return false;
	}

	OutWorldPositions.Reserve(GameUnitPositions.Num());
	for (const FVector3d& Position : GameUnitPositions)
	{
		OutWorldPositions.Add(Simulator.ToWorldLocation(Position));
	}
	return true;
}

double UCosmosSimulationSubsystem::GetSimulatedTimeYears() const
{
	return Simulator.GetSimulatedTime();
}

double UCosmosSimulationSubsystem::GetOrbitalPeriodYears(int32 SimulatorIndex) const
{
	return Simulator.EstimateOrbitalPeriod(SimulatorIndex);
}
```

- [ ] **Step 3: `FinalizeRegistration` 改用 `PairOrbitalPeriod`**

把 `CosmosSimulationSubsystem.cpp` 里这段（约 155–174 行）

```cpp
	double ShortestPeriod = TNumericLimits<double>::Max();

	for (int32 I = 0; I < Simulator.GetNumBodies(); ++I)
	{
		for (int32 J = I + 1; J < Simulator.GetNumBodies(); ++J)
		{
			const FCosmosBodyState& BodyA = Simulator.GetBody(I);
			const FCosmosBodyState& BodyB = Simulator.GetBody(J);

			const double Separation = (BodyB.Position - BodyA.Position).Size();
			const double Mu = CosmosUnits::GravitationalConstant_Game * (BodyA.Mass + BodyB.Mass);
			if (Separation <= 0.0 || Mu <= 0.0)
			{
				continue;
			}

			ShortestPeriod = FMath::Min(ShortestPeriod,
				2.0 * PI * FMath::Sqrt(Separation * Separation * Separation / Mu));
		}
	}
```

替换成

```cpp
	double ShortestPeriod = TNumericLimits<double>::Max();

	for (int32 I = 0; I < Simulator.GetNumBodies(); ++I)
	{
		for (int32 J = I + 1; J < Simulator.GetNumBodies(); ++J)
		{
			// 与求解器里 EstimateOrbitalPeriod 用的是同一个函数 —— 两处曾经各写
			// 一遍同样的公式，抽出来之后就只有一个定义。
			const double PairPeriod = FNBodySimulator::PairOrbitalPeriod(
				Simulator.GetBody(I), Simulator.GetBody(J),
				CosmosUnits::GravitationalConstant_Game);

			if (PairPeriod > 0.0)
			{
				ShortestPeriod = FMath::Min(ShortestPeriod, PairPeriod);
			}
		}
	}
```

语义逐字相同：原来「间距为零或 Mu 为零 → `continue`（跳过这一对）」现在等价于「`PairPeriod == 0` → 不参与取最小」。两种写法在只有一对天体时都是 `ShortestPeriod` 保持 `Max`，而 `Max` 会被下面那句 `FMath::IsFinite(ShortestPeriod)` 挡住。

- [ ] **Step 4: 构建**

```bash
powershell -NoProfile -File "C:/Users/23080/AppData/Local/Temp/ue_mcp/ue_build.ps1"
```

预期 `Result: Succeeded`。

- [ ] **Step 5: 回归检查 —— 日志里的最短周期与步长必须与改动前一致**

打开编辑器、PIE 跑 `UniverseMap`，在 `Saved/Logs/` 或 Output Log 里找这一行：

```
[Cosmos] 初始化完成：N 个天体，最短周期 X 年，步长 Y 年，TimeScale ...
```

**X 必须还是 `0.608...` 量级、Y 必须还是 `3.0e-05` 量级**（本关卡几何固定，这两个数不该动）。数值一变就说明 Step 3 的重构改错了行为，回去查。

> 这是本次改动里唯一可能悄悄改坏既有模拟的地方，所以这一步是必做的，不是走过场。

- [ ] **Step 6: 复查 BOM**

```bash
for f in Source/StellarEra/Public/CosmosSimulationSubsystem.h \
         Source/StellarEra/Private/CosmosSimulationSubsystem.cpp ; do
  printf '%-60s %s\n' "$f" "$(head -c3 "$f" | xxd -p)"
done
```

两个都必须是 `efbbbf`。

---

### Task 3: 8 个逐天体属性 + 缓存初始位置 + 创建渲染组件（空壳）

这个任务只搭骨架：属性就位、组件被创建、Tick 每帧跑但什么都不画。可验证的交付物是**日志里每个天体各出现一行「轨迹组件已创建」**。

**Files:**
- Modify: `Source/StellarEra/Public/CosmosOrbitComponent.h`
- Modify: `Source/StellarEra/Private/CosmosOrbitComponent.cpp`
- Create: `Source/StellarEra/Public/CosmosTrajectoryComponent.h`
- Create: `Source/StellarEra/Private/CosmosTrajectoryComponent.cpp`

**Interfaces:**
- Consumes: Task 2 的三个子系统查询
- Produces:
  - `UCosmosOrbitComponent` 的 8 个新属性（名字见下）+ `FVector InitialWorldLocation`
  - `void UCosmosTrajectoryComponent::Initialize(UCosmosOrbitComponent* InOrbit)`

- [ ] **Step 1: 给 `UCosmosOrbitComponent.h` 加 8 个属性**

在 `SimulatorIndex` 那个属性**之后**、`//~ Begin UActorComponent Interface` **之前**插入：

```cpp
	//////////////////////////////////////////////////////////////////////////
	// 轨迹显示
	//
	// 这些参数放在 Orbit 组件上而不是轨迹组件上：轨迹组件是运行时 NewObject
	// 出来的，编辑器里调不到它的属性。Orbit 组件每个天体上都已经有了，参数放
	// 这儿才能逐天体在详情面板里调（颜色有取色器），且零编辑器侧手工操作。

	/** 是否画这个天体的轨迹（预测 + 拖尾）。关掉可省下它的全部开销。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cosmos|Trajectory")
	bool bDrawTrajectory = true;

	/** 预测轨迹的颜色。浅色 —— 与深色的拖尾形成对比。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cosmos|Trajectory")
	FColor PredictionColor = FColor(90, 170, 255);

	/** 已走过的拖尾颜色。深色。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cosmos|Trajectory")
	FColor TrailColor = FColor(20, 50, 110);

	/** 初始速度矢量的颜色。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cosmos|Trajectory")
	FColor InitialVelocityColor = FColor(255, 210, 80);

	/**
	 * 预测多长，单位模拟年。0 = 自动。
	 *
	 * 自动值 = 1.05 × 该天体参与的最短轨道周期 —— 比一圈多一点，于是画出来的
	 * 椭圆首尾相接、看起来是闭合的。算不出周期时回退到 1.0 年。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cosmos|Trajectory", meta = (ClampMin = "0.0"))
	double PredictionYears = 0.0;

	/**
	 * 拖尾回溯多长，单位模拟年。0 = 自动（= 预测窗口的 1/4）。
	 *
	 * 拖尾按**模拟时间**采样并裁剪，不按帧 —— 按帧的话覆盖时长会随帧率漂移。
	 * 这个窗口决定**覆盖时长**，点的密度另由标称采样间隔决定（论证见 spec §6）。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cosmos|Trajectory", meta = (ClampMin = "0.0"))
	double TrailYears = 0.0;

	/** 是否画初始速度矢量。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cosmos|Trajectory")
	bool bDrawInitialVelocity = true;

	/**
	 * 初始速度矢量的长度系数：每 1 AU/年 画多少厘米。
	 *
	 * 这**不是**物理速度的世界长度。真实换算下 1 AU/年 ≈ 0.032 厘米/秒，本关卡
	 * 7.4 AU/年 的初速画出来只有 0.24 厘米 —— 看不见。所以箭头长度是
	 * 「速度大小 × 这个系数」的示意图，只保证相对长短正确。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cosmos|Trajectory")
	double VelocityArrowScaleCm = 10000.0;

	/**
	 * 开局时的世界位置。速度矢量的锚点。
	 *
	 * 运行时在 BeginPlay 里缓存，不要手改 —— 它记的是天体**出发**的地方，
	 * 星球飞走之后箭头要留在原地标出起点。
	 */
	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "Cosmos|Trajectory")
	FVector InitialWorldLocation = FVector::ZeroVector;
```

- [ ] **Step 2: 写 `CosmosTrajectoryComponent.h`**

```cpp
// Copyright Voxel Plugin SAS. All Rights Reserved.
//
// 天体轨迹渲染：预测轨迹（浅）、已走过的拖尾（深）、初始速度矢量。
//
// 这是一个**哑渲染器**：它不存任何面向用户的参数，参数全从兄弟
// UCosmosOrbitComponent 上读（那边在编辑器详情面板里能调、有取色器）。
// 它也不决定天体在哪 —— 位置由 UCosmosSimulationSubsystem 每帧写回 Actor，
// 它只读。

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Components/LineBatchComponent.h"
#include "CosmosTrajectoryComponent.generated.h"

class UCosmosOrbitComponent;
class UCosmosSimulationSubsystem;

/**
 * 把一颗天体的轨迹画出来。
 *
 * 由 UCosmosOrbitComponent::BeginPlay 用 NewObject + RegisterComponent 创建 ——
 * 于是不需要动蓝图、不需要手工加组件。
 *
 * 三样东西共用一个 ULineBatchComponent：预测折线、拖尾折线、速度箭头。
 * 线用 LifeTime = 0 画上去（永不过期），所以每帧先 Flush 再整批重画。
 *
 * tick group 是 TG_PostUpdateWork：必须排在 UCosmosSimulationSubsystem 把位置
 * 写回 Actor **之后**，否则拖尾采到的是上一帧的位置。实际上排错了也只差一帧
 * （约 4 米），肉眼看不出来，但没理由不排对。
 */
UCLASS(ClassGroup = (Cosmos))
class UCosmosTrajectoryComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UCosmosTrajectoryComponent();

	/** 由 UCosmosOrbitComponent::BeginPlay 调用。InOrbit 必须非空。 */
	void Initialize(UCosmosOrbitComponent* InOrbit);

	//~ Begin UActorComponent Interface
	virtual void TickComponent(float DeltaTime, ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;
	//~ End UActorComponent Interface

private:
	/** 实际的预测窗口（年）：手填优先 → 1.05 × 估算周期 → 回退常量。 */
	double ResolvePredictionHorizonYears(const UCosmosSimulationSubsystem& Subsystem) const;

	/** 把一段折线追加进 Lines。 */
	void AppendPolyline(TArrayView<const FVector> Points, const FLinearColor& Color);

	/** 把速度箭头（杆 + 两段箭头）追加进 Lines。 */
	void AppendVelocityArrow(const FLinearColor& Color);

	/** 参数来源。每帧重读，所以运行期改属性立刻生效。 */
	UPROPERTY(Transient)
	TObjectPtr<UCosmosOrbitComponent> Orbit;

	/** 画线用。自己造、自己注册。 */
	UPROPERTY(Transient)
	TObjectPtr<ULineBatchComponent> LineBatch;

	/**
	 * 拖尾的历史位置（世界坐标）。
	 *
	 * 用「先进先出」而不是环形缓冲：容量 512、约每 6 帧才追加一个点，
	 * RemoveAt(0) 的 O(n) 搬迁每秒才几十 KB，完全不值得为它写一套取模索引 ——
	 * 而 FIFO 天然就是画线需要的先后顺序。
	 */
	TArray<FVector> Trail;

	/** 上次采样时的模拟时间（年）。 */
	double LastTrailSampleTime = 0.0;

	/** 复用的中间数组，避免每帧重新分配。 */
	TArray<FVector> PredictedWorld;
	TArray<FBatchedLine> Lines;

	/** 上一帧画过没有 —— 关掉时只清一次，不必每帧 MarkRenderStateDirty。 */
	bool bDrewLastFrame = false;
};
```

- [ ] **Step 3: 写 `CosmosTrajectoryComponent.cpp`（空壳：只创建线批组件）**

```cpp
// Copyright Voxel Plugin SAS. All Rights Reserved.

#include "CosmosTrajectoryComponent.h"

#include "CosmosOrbitComponent.h"
#include "CosmosSimulationSubsystem.h"
#include "Engine/World.h"

namespace
{
	/**
	 * 线宽。**必须传 0**。
	 *
	 * 行批把 Thickness = 0 走成真正的 1 像素 GPU 线（BatchedElements.cpp:167），
	 * 而正值走的是「世界单位」的四边形（行批把 bScreenSpace 写死成 false）。
	 * 传正值的话近了变成粗带、远了直接消失；0 则任何距离都是 1 像素，正是想要的。
	 */
	constexpr float LineThickness = 0.0f;

	/** 深度优先级。SDPG_World = 0：走常规深度测试，星球能挡住它背后的轨道线。 */
	constexpr uint8 LineDepthPriority = SDPG_World;
}

UCosmosTrajectoryComponent::UCosmosTrajectoryComponent()
{
	PrimaryComponentTick.bCanEverTick = true;

	// 排在位置写回之后（见类注释）。
	PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
}

void UCosmosTrajectoryComponent::Initialize(UCosmosOrbitComponent* InOrbit)
{
	Orbit = InOrbit;

	// 线批组件自己造、自己注册。**不挂到任何父组件下**：行批的线是用世界坐标
	// 直接交给 PDI 的（LineBatchComponent.cpp 里 DrawTranslucentLine(Line.Start,
	// Line.End, ...)），所以它自己的变换只影响包围盒、不影响线。挂在移动中的
	// 星球下面反而会让包围盒跟着跑、算错。引擎自己的行批也是这么造的。
	LineBatch = NewObject<ULineBatchComponent>(GetOwner());
	if (LineBatch)
	{
		LineBatch->RegisterComponent();
	}

	UE_LOG(LogTemp, Log, TEXT("[Cosmos] 轨迹组件已创建：%s"),
		*GetNameSafe(GetOwner()));
}

void UCosmosTrajectoryComponent::TickComponent(float DeltaTime, ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// 空壳：Task 4 起往这里填内容。
}

double UCosmosTrajectoryComponent::ResolvePredictionHorizonYears(
	const UCosmosSimulationSubsystem& Subsystem) const
{
	return 0.0;
}

void UCosmosTrajectoryComponent::AppendPolyline(TArrayView<const FVector> Points,
	const FLinearColor& Color)
{
}

void UCosmosTrajectoryComponent::AppendVelocityArrow(const FLinearColor& Color)
{
}
```

> 空壳里的四个函数暂时返回 0 / 什么都不做，是为了让 Task 4–6 每次只改**一个**函数。`TickComponent` 里的 `Super::TickComponent` 调用要保留。

- [ ] **Step 4: `UCosmosOrbitComponent.cpp` 缓存初始位置 + 创建组件**

把 `BeginPlay` 改成：

```cpp
void UCosmosOrbitComponent::BeginPlay()
{
	Super::BeginPlay();

	// 开局位置 = 速度矢量的锚点。缓存下来，因为「初始位置」指的是天体**出发**
	// 的地方：星球飞走之后箭头要留在原地标出起点。
	if (const AActor* OwnerActor = GetOwner())
	{
		InitialWorldLocation = OwnerActor->GetActorLocation();
	}

	CreateTrajectoryComponent();

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
```

在 `EndPlay` **之前**插入新函数：

```cpp
void UCosmosOrbitComponent::CreateTrajectoryComponent()
{
	AActor* OwnerActor = GetOwner();
	if (!OwnerActor)
	{
		return;
	}

	// 无条件创建，即使 bDrawTrajectory 为假 —— 这样运行期把它打开能立刻生效，
	// 代价只是关掉时多一次空 Tick。
	UCosmosTrajectoryComponent* Component = NewObject<UCosmosTrajectoryComponent>(OwnerActor);
	if (!Component)
	{
		return;
	}

	Component->RegisterComponent();

	// 显式初始化，不等引擎替我们调 BeginPlay：这个组件是在 owner 的 BeginPlay
	// 里才造出来的，那时 AActor::HasActorBegunPlay() 还是 false，引擎不会自动
	// 给它走一遍 BeginPlay。所以初始化必须由我们自己来。
	Component->Initialize(this);
}
```

在 `CosmosOrbitComponent.cpp` 顶部加 `#include "CosmosTrajectoryComponent.h"`，并在 `CosmosOrbitComponent.h` 的 `UCosmosOrbitComponent` 类里、`BeginPlay` 声明旁边加：

```cpp
	/** 创建并注册轨迹渲染组件。只调一次。 */
	void CreateTrajectoryComponent();
```

- [ ] **Step 5: 加 BOM，构建**

```bash
powershell -NoProfile -File "C:/Users/23080/AppData/Local/Temp/ue_mcp/add_bom.ps1" \
  -Path "F:/workspace/ue/StellarEra/Source/StellarEra/Public/CosmosTrajectoryComponent.h"
powershell -NoProfile -File "C:/Users/23080/AppData/Local/Temp/ue_mcp/add_bom.ps1" \
  -Path "F:/workspace/ue/StellarEra/Source/StellarEra/Private/CosmosTrajectoryComponent.cpp"
powershell -NoProfile -File "C:/Users/23080/AppData/Local/Temp/ue_mcp/ue_build.ps1"
```

预期 `Result: Succeeded`。

> **若报 `TObjectPtr<UCosmosOrbitComponent>` 不完整类型**：把 `CosmosTrajectoryComponent.h` 里的
> `class UCosmosOrbitComponent;` 换成 `#include "CosmosOrbitComponent.h"`。两个头之间没有
> 循环依赖（`CosmosOrbitComponent.h` 不 include 轨迹组件头），所以这一改是安全的。

- [ ] **Step 6: PIE 验证组件真的被创建了**

打开编辑器跑 `UniverseMap`，Output Log 里应当每个天体出现一行：

```
[Cosmos] 轨迹组件已创建：BP_VoxelPlanet_C_0
[Cosmos] 轨迹组件已创建：...
```

天体数 = 关卡里 `UCosmosOrbitComponent` 的数量（本关卡 5 个左右）。**一行都没有说明 BeginPlay 没跑到或创建失败**；行数对不上说明有的天体没挂 Orbit 组件。

此时画面上**什么都还看不到** —— 这是预期的，Task 4 才开始画。

---

### Task 4: 初始速度矢量 —— 打通渲染链路

这是**第一条真正画出来的线**。它证明「`ULineBatchComponent` → Flush/DrawLines → 屏幕上出现线」这条链路是通的。把它放在预测和拖尾之前，是因为万一不显示，问题范围就限定在渲染管道上，而不是 768 条线的几何算错了。

**Files:**
- Modify: `Source/StellarEra/Private/CosmosTrajectoryComponent.cpp`（`TickComponent` + `AppendVelocityArrow` + 常量）

**Interfaces:**
- Consumes: `UCosmosOrbitComponent::InitialVelocity` / `InitialWorldLocation` / `VelocityArrowScaleCm` / `InitialVelocityColor` / `bDrawInitialVelocity`
- Produces: 无新接口（内部实现）

- [ ] **Step 1: 加箭头相关的常量**

在 `CosmosTrajectoryComponent.cpp` 的匿名 namespace 里、`LineDepthPriority` **之后**加：

```cpp
	/** 箭头段长度 = 该系数 × 杆长。 */
	constexpr double ArrowHeadLengthFraction = 0.15;

	/** 箭头段与杆的夹角。150° 即「从箭尖往回折 30°」。 */
	constexpr double ArrowHeadAngleDegrees = 150.0;
```

- [ ] **Step 2: 实现 `AppendVelocityArrow`**

把空壳换成：

```cpp
void UCosmosTrajectoryComponent::AppendVelocityArrow(const FLinearColor& Color)
{
	if (!Orbit)
	{
		return;
	}

	const double Speed = Orbit->InitialVelocity.Size();
	if (Speed <= 0.0 || Orbit->VelocityArrowScaleCm <= 0.0)
	{
		// 静止天体没有速度矢量可画。
		return;
	}

	// 方向就是 InitialVelocity 的方向：ToWorldVelocity 只是把它按一个**正常数**
	// 缩放（AU/年 → 厘米/秒），归一化之后完全相同，所以直接用游戏单位向量。
	//
	// 长度**不是**物理速度的世界长度 —— 真实换算下 7.4 AU/年 只有 0.24 厘米/秒，
	// 画出来看不见。长度是「速度大小 × VelocityArrowScaleCm」的示意图。
	const FVector Direction = Orbit->InitialVelocity.GetSafeNormal();
	if (Direction.IsNearlyZero())
	{
		return;
	}

	const double ShaftLength = Orbit->VelocityArrowScaleCm * Speed;
	const FVector Tip = Orbit->InitialWorldLocation + Direction * ShaftLength;

	// 杆。FBatchedLine 的构造参数序是 (Start, End, Color, LifeTime, Thickness,
	// DepthPriority) —— 与 DrawLine 的函数参数序相反，别写混。
	Lines.Emplace(Orbit->InitialWorldLocation, Tip, Color,
		/*LifeTime=*/0.0f, LineThickness, LineDepthPriority);

	// 箭头：从箭尖往回折两条短线。需要一个与杆垂直的参考方向；杆若几乎平行于
	// UpVector，就换 RightVector 做叉乘，避免叉乘退化。
	const FVector Reference = FMath::Abs(Direction.Z) < 0.9 ? FVector::UpVector : FVector::RightVector;
	const FVector Perp = FVector::CrossProduct(Direction, Reference).GetSafeNormal();
	if (Perp.IsNearlyZero())
	{
		return;   // 上面已经排除了退化，这里只是兜底
	}

	const double HeadLength = ShaftLength * ArrowHeadLengthFraction;
	const double HeadAngleRad = FMath::DegreesToRadians(ArrowHeadAngleDegrees);
	const double HeadCos = FMath::Cos(HeadAngleRad);
	const double HeadSin = FMath::Sin(HeadAngleRad);

	Lines.Emplace(Tip, Tip + (Direction * HeadCos + Perp * HeadSin) * HeadLength,
		Color, 0.0f, LineThickness, LineDepthPriority);
	Lines.Emplace(Tip, Tip + (Direction * HeadCos - Perp * HeadSin) * HeadLength,
		Color, 0.0f, LineThickness, LineDepthPriority);
}
```

- [ ] **Step 3: `TickComponent` 里调用它**

把 `TickComponent` 换成：

```cpp
void UCosmosTrajectoryComponent::TickComponent(float DeltaTime, ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!LineBatch)
	{
		return;
	}

	// 关掉时把已画的线清一次就够，不必每帧 MarkRenderStateDirty。
	if (!Orbit || !Orbit->bDrawTrajectory)
	{
		if (bDrewLastFrame)
		{
			LineBatch->Flush();
			bDrewLastFrame = false;
		}
		return;
	}

	Lines.Reset();

	if (Orbit->bDrawInitialVelocity)
	{
		AppendVelocityArrow(FLinearColor(Orbit->InitialVelocityColor));
	}

	// Flush 是必须的：线用 LifeTime = 0 画上去就永不过期，不清的话每帧的线会
	// 无限叠加（这是 spec §7.1 对着引擎源码确认过的行为）。
	LineBatch->Flush();
	if (Lines.Num() > 0)
	{
		LineBatch->DrawLines(Lines);
	}
	bDrewLastFrame = Lines.Num() > 0;
}
```

`FLinearColor(FColor)` 这个构造会做 sRGB → 线性转换，正是取色器给的颜色该走的路径。

- [ ] **Step 4: 构建 + PIE 验证**

```bash
powershell -NoProfile -File "C:/Users/23080/AppData/Local/Temp/ue_mcp/ue_build.ps1"
```

PIE 跑 `UniverseMap`，预期：

- 每颗天体**出发的位置**上有一支**琥珀色**的箭头，方向与它开局运动的方向一致
- 箭头**钉在原地不动**，星球飞走之后它还留在起点
- 箭头长度 = `|InitialVelocity| × VelocityArrowScaleCm` 厘米 —— **自己乘一遍，别照抄任何具体数字**（初速度是手填的，改关卡就会变）
- 箭头的**支数 = `InitialVelocity` 非零的天体数**；零速度的天体没有箭头
  （注意：本关卡的**恒星已在早前的编辑中删除**，任何「恒星上应当如何」的预期都已失效，天体数以日志里「N 个天体」为准）

**看不到线时的排查顺序**（从最可能到最不可能）：

1. `bDrawInitialVelocity` 是不是被关了，或者 `InitialVelocity` 是零
2. `[Cosmos] 轨迹组件已创建` 那行日志有没有出现（没有 → 回到 Task 3 Step 6）
3. 箭头是不是被星球挡住了（把相机拉到远处看全景）
4. `LineBatch->RegisterComponent()` 有没有失败（在 `Initialize` 里加一行 `UE_LOG` 打 `LineBatch->IsRegistered()`）

---

### Task 5: 预测轨迹（浅色闭合椭圆）

**Files:**
- Modify: `Source/StellarEra/Private/CosmosTrajectoryComponent.cpp`（常量 + `ResolvePredictionHorizonYears` + `AppendPolyline` + `TickComponent`）

**Interfaces:**
- Consumes: Task 2 的 `PredictTrajectoryWorld` / `GetOrbitalPeriodYears`；`UCosmosOrbitComponent::PredictionColor` / `PredictionYears` / `SimulatorIndex`
- Produces: `double UCosmosTrajectoryComponent::ResolvePredictionHorizonYears(const UCosmosSimulationSubsystem&) const`（实装）

- [ ] **Step 1: 加预测相关的常量**

在匿名 namespace 里追加：

```cpp
	/** 预测采样点数。步长 = 窗口 / (该值 - 1)。 */
	constexpr int32 PredictionSamples = 256;

	/** 自动预测窗口 = 该系数 × 估算周期。略大于 1，让画出的椭圆首尾相接。 */
	constexpr double AutoHorizonPeriodFactor = 1.05;

	/** 估算不出周期时的预测窗口（年）。 */
	constexpr double FallbackPredictionYears = 1.0;
```

- [ ] **Step 2: 实装 `ResolvePredictionHorizonYears`**

```cpp
double UCosmosTrajectoryComponent::ResolvePredictionHorizonYears(
	const UCosmosSimulationSubsystem& Subsystem) const
{
	if (!Orbit)
	{
		return FallbackPredictionYears;
	}

	// 手填优先，与估算完全无关 —— 自动与手填的分支只在这一处判断。
	if (Orbit->PredictionYears > 0.0)
	{
		return Orbit->PredictionYears;
	}

	if (Orbit->SimulatorIndex != INDEX_NONE)
	{
		const double Period = Subsystem.GetOrbitalPeriodYears(Orbit->SimulatorIndex);
		if (Period > 0.0 && FMath::IsFinite(Period))
		{
			return Period * AutoHorizonPeriodFactor;
		}
	}

	// 求解器里只有一颗天体、或所有配对都算不出有限正周期。
	return FallbackPredictionYears;
}
```

- [ ] **Step 3: 实装 `AppendPolyline`**

```cpp
void UCosmosTrajectoryComponent::AppendPolyline(TArrayView<const FVector> Points,
	const FLinearColor& Color)
{
	// 第 0 个点不画线，它是后面每一段的起点 —— 所以从 1 开始。
	for (int32 Index = 1; Index < Points.Num(); ++Index)
	{
		Lines.Emplace(Points[Index - 1], Points[Index], Color,
			/*LifeTime=*/0.0f, LineThickness, LineDepthPriority);
	}
}
```

- [ ] **Step 4: `TickComponent` 里重算并画出预测**

在 `TickComponent` 的 `Lines.Reset();` **之前**插入：

```cpp
	const UCosmosSimulationSubsystem* Subsystem =
		GetWorld() ? GetWorld()->GetSubsystem<UCosmosSimulationSubsystem>() : nullptr;
	if (!Subsystem)
	{
		return;
	}

	// ── 预测：每帧重算 ──
	// 不做节流。理由有两条：拖尾本来就要每帧重画（见 Task 6），而预测一旦节流，
	// 画出的椭圆会比星球的真实位置落后「节流时长 / 一圈时长」那么多圈 ——
	// 凑近星球看时那条线明显接不上。每帧重算把滞后压到 1 帧。
	//
	// 滞后一律用**角度**说，不要用米：本关卡一圈约 7.5 秒，节流 0.2 秒 ≈ 10°，
	// 1 帧（60fps）≈ 0.8°。换关卡时按「时长 ÷ 一圈时长 × 360°」自己算。
	const double Horizon = ResolvePredictionHorizonYears(*Subsystem);

	PredictedWorld.Reset();
	if (Orbit->SimulatorIndex != INDEX_NONE)
	{
		Subsystem->PredictTrajectoryWorld(Orbit->SimulatorIndex, Horizon,
			PredictionSamples, PredictedWorld);
	}
```

并在 `Lines.Reset();` **之后**、`if (Orbit->bDrawInitialVelocity)` **之前**插入：

```cpp
	if (PredictedWorld.Num() >= 2)
	{
		AppendPolyline(PredictedWorld, FLinearColor(Orbit->PredictionColor));
	}
```

- [ ] **Step 5: 构建 + PIE 验证**

```bash
powershell -NoProfile -File "C:/Users/23080/AppData/Local/Temp/ue_mcp/ue_build.ps1"
```

PIE 跑 `UniverseMap`，预期：

- 每颗天体上有一条**浅蓝色**的椭圆，**首尾相接**（这是 1.05 圈的效果 —— 末点越过首点 18°，看起来是闭合的）
- 椭圆的**近端连着星球本身**，中间没有可见的断口
- 两个天体各有一条椭圆；窗口 = `1.05 × 该天体参与的**最短配对周期**`（`EstimateOrbitalPeriod` 取的是**所有配对里的最小值**，不是「到最近天体的距离」那种东西），窗口不同则弧段长度不同
- 飞船若没挂 `UCosmosOrbitComponent` → 没有轨迹

**椭圆不闭合或形状很怪时的排查顺序**：

1. 把 `PredictionYears` 手填成**恰好一圈**（= 日志里「最短周期」那个数；本关卡当前是 `0.025`）—— 应当闭合成一个规整的圆。若这时才闭合，说明自动窗口算错了
2. 打日志确认 `Horizon`：临时在 `ResolvePredictionHorizonYears` 里 `UE_LOG` 出 `Period` 与返回值，自动值应当是 `Period × 1.05`（本关卡当前 `0.025 × 1.05 ≈ 0.02625`）
3. 首尾差得离谱（比如只有一小段弧）→ 检查 `EstimateOrbitalPeriod` 是不是取到了很远的那一对

---

### Task 6: 拖尾（深色彗尾）

**Files:**
- Modify: `Source/StellarEra/Private/CosmosTrajectoryComponent.cpp`（常量 + `TickComponent`）
- Modify: `Source/StellarEra/Public/CosmosTrajectoryComponent.h`（加 `TArray<double> TrailTimes`；改 `Trail` 的注释）
- Modify: `Source/StellarEra/Public/CosmosOrbitComponent.h`（**仅注释**：改 `TrailYears` 上那句过期的「512 个点摊在这个窗口上」）

**Interfaces:**
- Consumes: Task 2 的 `GetSimulatedTimeYears`；`UCosmosOrbitComponent::TrailColor` / `TrailYears` / `bDrawTrajectory`
- Produces: 无新接口

- [ ] **Step 1: 头文件加时间戳数组 + 加拖尾常量**

在 `CosmosTrajectoryComponent.h` 里，紧跟 `TArray<FVector> Trail;` 之后加：

```cpp
	/** 与 Trail 一一对应的采样时刻（模拟年）。按时间裁剪要用它。 */
	TArray<double> TrailTimes;
```

并把 `Trail` 上那段注释里「容量 512、约每 6 帧才追加一个点」删掉 —— 那两个数**都不对**：追加间隔是 `TrailInterval` 与每帧推进两者中的**较大者**，而保留的点数 ≈ `TrailSpan / 那个间隔`（帧率高时点更密，但**点数 ≤ `TrailSamples + 1` = 513**）。注释改成说明「FIFO + 平行时间戳，两个数组只在一起改」。

**同一个毛病还在 `CosmosOrbitComponent.h` 里**：`TrailYears` 的注释结尾写着「512 个点摊在这个窗口上的论证见 spec §6」。
裁剪改成按时间后 512 已不是点数，本步一并改掉（**只动注释，不动那个 `UPROPERTY` 声明**）：

```cpp
	/**
	 * 拖尾回溯多长，单位模拟年。0 = 自动（= 预测窗口的 1/4）。
	 *
	 * 拖尾按**模拟时间**采样并裁剪，不按帧 —— 按帧的话覆盖时长会随帧率漂移。
	 * 这个窗口决定**覆盖时长**，点的密度另由标称采样间隔决定（论证见 spec §6）。
	 */
```

然后在匿名 namespace 里追加：

```cpp
	/**
	 * 拖尾的标称采样点数，**也是保留点数的上界（至多 513）**。
	 *
	 * 采样门限 = TrailSpan / TrailSamples：距上次采样不足这个时长就不采，于是相邻两点
	 * 的间隔 ≥ 门限 → 窗口内的点数 ≤ TrailSamples（含端点至多 +1）。
	 *
	 * 点数随帧率变，但**覆盖时长 ≈ TrailSpan**（至多差一个采样间隔 —— 这正是按模拟时间裁剪要买的东西）：
	 * 帧率越低，每帧推进越大、点越稀（60 fps 约 119 点，30 fps 约 60 点）；帧率越高
	 * 点越密，每帧推进小于门限后趋近上界（本关卡约 1000 fps 时约 493 点，量化所致）。
	 * 想要固定点数就得改回按点数裁剪，那会让覆盖时长随帧率漂移 —— 不换。
	 */
	constexpr int32 TrailSamples = 512;

	/** 自动拖尾时长 = 该系数 × 预测窗口。论证见 spec §6。 */
	constexpr double AutoTrailHorizonFraction = 0.25;
```

- [ ] **Step 2: 在 `TickComponent` 里采样并画出拖尾**

在 `Lines.Reset();` **之前**（紧接预测那段之后）插入：

```cpp
	// ── 拖尾：采样和裁剪**都**按模拟时间，都不按帧 ──
	// 只「按时间采样」是不够的。采样间隔可能比每帧推进还短（本关卡 1.28e-5 年
	// vs 60fps 下每帧推进 5.55e-5 年），那时每帧都会采，「点数上限」就等于
	// 「时长上限」，覆盖时长又变回由帧率决定 —— 正是要避免的事。
	// 所以裁剪也必须按时间：覆盖时长 ≈ TrailSpan，帧率只影响点的密度（缺口至多一个采样间隔）。
	const double SimulatedTime = Subsystem->GetSimulatedTimeYears();

	const double TrailSpan = Orbit->TrailYears > 0.0
		? Orbit->TrailYears
		: Horizon * AutoTrailHorizonFraction;
	const double TrailInterval = TrailSpan / double(TrailSamples);

	const AActor* OwnerActor = GetOwner();
	const FVector CurrentLocation = OwnerActor
		? OwnerActor->GetActorLocation()
		: FVector::ZeroVector;

	if (Trail.Num() == 0)
	{
		// 开局先放一个点，否则拖尾要等到第一个采样时刻才开始长，缺开头一小段。
		Trail.Add(CurrentLocation);
		TrailTimes.Add(SimulatedTime);
		LastTrailSampleTime = SimulatedTime;
	}
	else if (TrailInterval > 0.0 && SimulatedTime - LastTrailSampleTime >= TrailInterval)
	{
		// 一帧最多补一个点：掉帧时宁可让采样间隔临时变长，也不要在一帧里塞进
		// 几十个几乎重合的点。
		Trail.Add(CurrentLocation);
		TrailTimes.Add(SimulatedTime);
		LastTrailSampleTime = SimulatedTime;

		// 按**模拟时间**裁剪，不按点数。这是本步的关键：按点数裁的话，
		// 覆盖时长 = (TrailSamples-1) × max(TrailInterval, 每帧推进)，上面那种
		// 「间隔比每帧推进还小」的情形下就退化成由帧数决定，帧率一变拖尾长度就变。
		// 两个数组只在这里一起改，长度恒等。
		const double TrailStartTime = SimulatedTime - TrailSpan;
		int32 DropCount = 0;
		while (DropCount < TrailTimes.Num() && TrailTimes[DropCount] < TrailStartTime)
		{
			++DropCount;
		}
		if (DropCount > 0)
		{
			// EAllowShrinking::No = 保留容量，别每次裁剪都把分配器折腾一遍。
			// （这是新式的枚举形式，替换了老版的 bool 参数。）
			Trail.RemoveAt(0, DropCount, EAllowShrinking::No);
			TrailTimes.RemoveAt(0, DropCount, EAllowShrinking::No);
		}
	}
```

并在 `Lines.Reset();` **之后**、预测那段**之前**插入：

```cpp
	if (Trail.Num() >= 2)
	{
		AppendPolyline(Trail, FLinearColor(Orbit->TrailColor));
	}
```

> 顺序说明：`Trail` 是「先进先出」，下标 0 是最旧的点、末尾是最新的点，所以直接顺着画就是时间顺序，不需要任何索引换算。这正是选 FIFO 而不是环形缓冲的原因。

- [ ] **Step 3: 构建 + PIE 验证**

```bash
powershell -NoProfile -File "C:/Users/23080/AppData/Local/Temp/ue_mcp/ue_build.ps1"
```

PIE 跑 `UniverseMap`，跑够 `TrailSpan / TimeScale` 秒（本关卡当前 ≈ **2 秒**：自动拖尾窗口 0.0065625 年 ÷ 0.00333）。预期：

- 每颗行星身后拖着一条**深蓝色**的尾巴，像**彗尾**一样跟着星球生长
- 拖尾**从零长起来**（开局没有），约 2 秒后达到稳定长度 = `TrailYears`（自动 = 预测窗口的 1/4 = 0.26 圈）
- 拖尾是**光滑的曲线**，不是一段一段跳的折线。点数由帧率决定（60fps 下约 119 点、118 段覆盖这 0.26 圈 → 每段约 0.8°），肉眼看不出来
- 拖尾的**最新端连着星球本身**
- 拖尾比预测短得多（自动值是预测窗口的 1/4：0.0065625 年 vs 0.02625 年）

**拖尾不出现 / 一段一段跳时的排查顺序**：

1. 跑够时间了吗 —— 前 1 秒内确实什么都看不到
2. 星球是 `bSimulated = false` 吗 —— 那种天体的 Actor 不动，拖尾缩成一个点（spec §12.7 记录的已知取舍，不是 bug）
3. 跳得很明显 → 说明 `TrailInterval` 算大了；临时 `UE_LOG` 出 `TrailInterval` 与 `SimulatedTime - LastTrailSampleTime`（本关卡当前 `0.0065625 / 512 = 1.28e-5` 年，比 60fps 的每帧推进 `5.55e-5` 年还小，所以每帧都会采 —— 这正说明裁剪必须按时间）
4. 只有一两个点 → `Subsystem->GetSimulatedTimeYears()` 是不是一直是 0（求解器没在推进）

---

### Task 7: 开关、性能与收尾

**Files:** 无（只验证）

- [ ] **Step 1: 逐项验证开关**

PIE 里对某颗行星在细节面板（PIE 中选中 Actor → Details）实时改，确认**不用重启就生效**（属性是每帧重读的）：

| 改什么 | 预期 |
|---|---|
| `bDrawTrajectory` 取消勾选 | 该天体的预测、拖尾、箭头**全部立刻消失**；其它天体不受影响 |
| 再勾回来 | 三样立刻回来 |
| `bDrawInitialVelocity` 取消勾选 | 只有箭头消失，两条轨迹线还在 |
| `PredictionColor` / `TrailColor` 改颜色 | 立刻换色（取色器可用） |
| `PredictionYears` 填 `0.0125` | 椭圆变成半圈左右的弧（0.0125 = 本关卡一圈 0.025 的一半）；填回 `0` 恢复自动 |
| `TrailYears` 填 `0.1` | 拖尾变长到约 4 圈（0.1 ÷ 一圈 0.025）；填回 `0` 恢复自动（0.26 圈）。**别填 `1.0`** —— 那是 40 圈，叠成一团什么都看不出来 |
| `VelocityArrowScaleCm` 改 `30000` | 箭头变长 3 倍 |
| `bSimulated` 取消勾选 | 星球停下、拖尾缩成一个点，但预测椭圆还在动（spec §12.7 的已知取舍） |

- [ ] **Step 2: 性能检查**

PIE 里控制台执行 `stat unit`，看 **Game** 那一项：

- 与关掉全部轨迹（把所有天体的 `bDrawTrajectory` 取消勾选）时对比，Game 时间的增量应当在 **0.5 毫秒以内**
- 同时确认 **Frame** 时间没有明显回归（帧率不掉）

若 Game 时间增量超过 1 毫秒，按这个顺序调（**每次只动一个**，改完重测）：

1. `PredictionSamples` 从 256 降到 128（成本直接砍半）。代价：每段弧长翻倍、弦高（弓形高度）变 4 倍 —— 弦高 = `r × (1 − cos(每段角度/2))`，自己按本关卡的半径和每段角度算，**不要照抄旧数字**
2. 给预测重算加回节流（在组件里加一个 `float PredictionTimer`，`>= 0.05f` 才重算）—— 代价是 0.05 秒的滞后，本关卡一圈约 7.5 秒，即约 2.4° 的角度滞后

- [ ] **Step 3: 最终 PIE 验收**

跑一遍确认三件事同时成立：

1. 浅色的预测椭圆**闭合**
2. 深色的拖尾从星球身后**生长成彗尾**
3. 出发点有琥珀色箭头，**方向与初速度一致**

并确认既有行为没回归：

4. 天体照常运动、体素地形照常生成
5. 骑在星球上的 Pawn 照常被携带（`CarryRiders` 那条路径）
6. 能量漂移日志仍在打印、且没有 NaN 或发散（Task 2 改动了 `FinalizeRegistration` 的步长计算，这是它的哨兵）。**不要拿 `1e-6` 当阈值** —— 日志正文自己写的就是 `< 1e-5`，历史观测跨 `1e-10` ~ `2.9e-4`

- [ ] **Step 4: 复查所有 BOM**

```bash
for f in \
  Source/StellarEraCore/Public/NBodySimulator.h \
  Source/StellarEraCore/Private/NBodySimulator.cpp \
  Source/StellarEraCore/Private/NBodySimulatorTests.cpp \
  Source/StellarEra/Public/CosmosSimulationSubsystem.h \
  Source/StellarEra/Private/CosmosSimulationSubsystem.cpp \
  Source/StellarEra/Public/CosmosOrbitComponent.h \
  Source/StellarEra/Private/CosmosOrbitComponent.cpp \
  Source/StellarEra/Public/CosmosTrajectoryComponent.h \
  Source/StellarEra/Private/CosmosTrajectoryComponent.cpp ; do
  printf '%-70s %s\n' "$f" "$(head -c3 "$f" | xxd -p)"
done
```

每一行都必须是 `efbbbf`。有缺的就 `add_bom.ps1` 补上，然后重新构建。

- [ ] **Step 5: 更新 spec 状态**

把 `docs/superpowers/specs/2026-10-17-trajectory-system-design.md` 第 5 行的
`- **状态**：设计已确认，待实现` 改成 `- **状态**：已实现`。

- [ ] **Step 6: 提交（等用户发话）**

本项目规则是只在用户明确要求时提交。用户说「提交」之后，用**显式完整路径**（`Content/` 长期是脏的，`git add -A` 会把它一起卷进来）：

```bash
git add \
  Source/StellarEraCore/Public/NBodySimulator.h \
  Source/StellarEraCore/Private/NBodySimulator.cpp \
  Source/StellarEraCore/Private/NBodySimulatorTests.cpp \
  Source/StellarEra/Public/CosmosSimulationSubsystem.h \
  Source/StellarEra/Private/CosmosSimulationSubsystem.cpp \
  Source/StellarEra/Public/CosmosOrbitComponent.h \
  Source/StellarEra/Private/CosmosOrbitComponent.cpp \
  Source/StellarEra/Public/CosmosTrajectoryComponent.h \
  Source/StellarEra/Private/CosmosTrajectoryComponent.cpp \
  docs/superpowers/specs/2026-10-17-trajectory-system-design.md \
  docs/superpowers/plans/2026-10-17-trajectory-system.md

git commit -m "$(cat <<'EOF'
feat: 星球轨迹系统（预测轨迹 / 拖尾 / 初始速度矢量）

求解器加三个只读预测方法：复制一份值类型求解器、推进副本、记录目标天体，
原对象一个字节不动（积分器的全部状态就是每颗星的 Position + Velocity，
所以复制求解器即复制完整状态）。

子系统加三个薄转发查询，并把 FinalizeRegistration 里那段重复的配对周期
公式抽成 FNBodySimulator::PairOrbitalPeriod 共用。

新增 UCosmosTrajectoryComponent（哑渲染器）：参数全从已有的
UCosmosOrbitComponent 上读，所以逐天体可调、有取色器、零编辑器手工操作。
用引擎自带的 ULineBatchComponent 画线，每帧 Flush + 一次 DrawLines。

- 预测轨迹：浅色，默认窗口 1.05 × 估算周期（补一点成闭合），每帧重算
- 拖尾：深色，采样与裁剪都按模拟时间（覆盖时长不随帧率漂移），自动窗口 = 预测窗口的 1/4
- 初始速度矢量：琥珀色，钉在开局位置

配套自动化测试 StellarEra.Core.NBodyPrediction（周期公式数值、配对最小值、
一圈后闭合、预测不改动原求解器）。

Co-Authored-By: Claude Code <noreply@anthropic.com>
EOF
)"
```

---

## 自审记录

**1. Spec 覆盖检查**

| spec 章节 | 落在哪个 Task |
|---|---|
| §1 三样东西 | Task 4（箭头）/ 5（预测）/ 6（拖尾） |
| §2 四个决策 | 全局：渲染方式 = `ULineBatchComponent`（Task 3）、预测窗口逐天体可调（Task 3 Step 1）、代码归属 A（Task 3）、拖尾窗口 = 预测窗口的 1/4 + 采样门限 `TrailSpan / 512`（**保留点数的上界 = `TrailSamples + 1` = 513**，Task 6） |
| §3 三个求解器方法 | Task 1 |
| §3.1 配对最小值的理由 | Task 1 Step 3 的 `EstimateOrbitalPeriod` 实现 + Task 2 Step 3 的共用 |
| §3.2 预测步长论证 | Task 5 Step 1（`PredictionSamples = 256`）+ Task 7 Step 2 的调参顺序 |
| §3.3 算不出周期时的回退 | Task 5 Step 2（`FallbackPredictionYears`） |
| §4 子系统查询 | Task 2（**多加了一个** `GetOrbitalPeriodYears`，理由见「与 spec 的偏差」第 2 条） |
| §5 八个属性 | Task 3 Step 1 |
| §6 拖尾采样论证 | Task 6 Step 1（`AutoTrailHorizonFraction` / `TrailSamples`）+ Step 2 的采样与**裁剪**逻辑（裁剪按模拟时间，理由见 Step 2 注释） |
| §7 渲染组件内部状态 | Task 3 Step 2 的成员表（**环形缓冲换成 FIFO**，理由见「与 spec 的偏差」第 1 条） |
| §7.1 引擎事实 | 「关键引擎事实」表 + Task 4 Step 1 的线宽注释 |
| §8 初始速度矢量 | Task 4 Step 2 |
| §9 测试 | Task 1（断言从 3 条扩到 5 组，理由见下） |
| §10 涉及文件 | 「文件结构」表 |
| §11 验证 | Task 2 Step 5、Task 3 Step 6、Task 4 Step 4、Task 5 Step 5、Task 6 Step 3、Task 7 |
| §12 已知取舍 | Task 7 Step 1 的开关表（含 §12.7 的 `bSimulated = false`）+ Step 2 的调参顺序（§12.1） |

**spec §9 的三条断言与 plan 的五组断言：** spec 的第 1 条「`EstimateOrbitalPeriod` 与 `2π√(r³/M)` 差 < 1%」是个**同义反复** —— `EstimateOrbitalPeriod` 就是照那个公式算的，所以它恒成立，挡不住任何错误。plan 改成两组更强的：把 `PairOrbitalPeriod` 当纯函数、拿**手算的具体数值**去钉它（G 改了、2π 漏了都会红），再加一组「配对最小值」的三体断言（取到最远那对也会红）。spec 的第 2 条闭合断言原来写的是「预测 1.05 × 周期后」，那对应 18° 弧、末点离首点 31% 的半径，用它当断言永远失败 —— plan 改成「正好一圈」（spec 已同步更正）。

**2. 占位符扫描**

无 TBD / TODO /「稍后实现」/「类似 Task N」。每个代码步骤都是完整可粘贴的代码。Task 3 里那四个空壳函数（返回 0 / 空实现）不是占位符 —— 它们是**有意的最小实现**，让 Task 4/5/6 每次只替换一个函数，并已注明各自在哪个 Task 被实装。

**3. 类型一致性**

- `PairOrbitalPeriod(const FCosmosBodyState&, const FCosmosBodyState&, double G)` —— Task 1 定义、Task 2 Step 3 调用，签名一致
- `EstimateOrbitalPeriod(int32) const` —— Task 1 定义、Task 2 Step 2 的 `GetOrbitalPeriodYears` 转发，一致
- `PredictTrajectory(int32, double, int32, TArray<FVector3d>&) const` —— Task 1 定义、Task 2 Step 2 转发，一致
- `PredictTrajectoryWorld(int32, double, int32, TArray<FVector>&) const` —— Task 2 定义、Task 5 Step 4 调用，一致（注意输出是 `TArray<FVector>`，不是 `FVector3d` 那个版本）
- `GetSimulatedTimeYears()` —— Task 2 定义、Task 6 Step 2 调用，一致
- `ResolvePredictionHorizonYears(const UCosmosSimulationSubsystem&) const` —— Task 3 声明、Task 5 Step 2 实装、Task 5 Step 4 与 Task 6 Step 2 调用（`Horizon` 在两处共用），一致
- `AppendPolyline(TArrayView<const FVector>, const FLinearColor&)` —— Task 3 声明、Task 5 Step 3 实装、Task 5 Step 4（传 `PredictedWorld`）与 Task 6 Step 2（传 `Trail`）调用，一致
- `AppendVelocityArrow(const FLinearColor&)` —— Task 3 声明、Task 4 Step 2 实装、Task 4 Step 3 调用，一致
- `Initialize(UCosmosOrbitComponent*)` —— Task 3 Step 2 声明、Step 3 实装、Step 4 调用，一致
- 属性名逐字核对：`bDrawTrajectory` / `PredictionColor` / `TrailColor` / `InitialVelocityColor` / `PredictionYears` / `TrailYears` / `bDrawInitialVelocity` / `VelocityArrowScaleCm` / `InitialWorldLocation` —— Task 3 Step 1 定义，Task 4/5/6/7 引用，全部一致
- `TickComponent` 在 Task 3 是空壳、Task 4 Step 3 换掉、Task 5 Step 4 与 Task 6 Step 2 各往里**插入两段**（`Lines.Reset();` 前后各一段）—— **两个 Task 用的是同一对锚点，不冲突**：每段都插在锚点紧邻处，且顺序唯一确定。
  `Lines.Reset();` **之前**依次是「预测采样（T5）→ 拖尾采样（T6）」；**之后**依次是「拖尾折线（T6）→ 预测折线（T5）→ 速度箭头（T4）」。
  段间顺序只影响同深度下共面线段的覆盖次序，无功能后果
