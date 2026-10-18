# 飞船推力接入 N 体 实现计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 把飞船的移动控制从 `UFloatingPawnMovement` 的位移改成施加在它自己那个 N 体上的加速度，由求解器积分。

**Architecture:** `FCosmosBodyState` 加两项状态量（`ExternalAcceleration` 外力加速度、`LinearDamping` 线性阻尼），`ComputeAccelerations()` 把它们并进加速度。力的方向只能是「子系统下来读飞船」：`StellarEraVehicle` 不能依赖 `StellarEra`（成环），所以飞船只把旋钮暴露出去，`UCosmosSimulationSubsystem` 每帧推进之前读走、换算、写进求解器，再把求解器速度写回飞船。`UFloatingPawnMovement` 整个删掉 —— 留着它就会和求解器抢着写位置。

**Tech Stack:** Unreal Engine 5.8、C++20、UBT（经捆绑 dotnet 直调 `UnrealBuildTool.dll`）、UE Automation Test（`StellarEra.Core.*`）、无头 `UnrealEditor-Cmd` probe。

**Spec:** `docs/superpowers/specs/2026-10-18-spaceship-nbody-thrust-design.md`

## Global Constraints

这些约束对本计划的**每一个**任务都生效，逐字照做。

**仓库常驻规则（用户明令，优先级高于本计划的任何一步）**

- **不要提交、不要推送，除非用户明确要求。** 每个任务最后一步的 `git commit` 命令是给「用户点头之后」用的；没点头就停下，把 `git diff --stat` 与验证结果报给用户，等指示。
- 关闭编辑器会丢掉未保存的关卡/资产改动 —— **`Stop-Process -Force` 之前必须先问用户**。
- **构建需要编辑器关闭**（Live Coding 会挡住 UBT；Live Coding 也不会应用构造函数里的默认值改动）。
- 自定义代码只写进项目自己的 `Source/StellarEra*` 模块，**绝不**写进第三方插件目录。
- UBT 必须用捆绑的 dotnet 直调（`%TEMP%\ue_mcp\ue_build.ps1`），**不要**用 `Build.bat`。
- bash 访问 D: 盘不稳定 —— D: 上的操作一律走 PowerShell。
- 查编辑器是否在跑用 PowerShell `Get-Process`，**不要**用 `tasklist`（沙箱里看不见）。
- 新源文件必须是 **UTF-8 带 BOM**。
- 源文件换行符本来就是混的，**保持原样**，不要「统一成 LF」。
- 回复用中文。

**技术约束（来自 spec §3）**

- **推力下限被当地重力锁死**：本关卡中心天体 M = 100 M☉、飞船在 r = 0.07425 AU，表面重力 = G·M/r² = 716,089 AU/年² = **7.94e6 cm/s²**。推力加速度必须大于它，否则飞船一步都升不起来。
- **换算常数**：`CentimetersPerAu = 1e6`、`TimeScale = 0.00333` 时，`1 AU/年² = CentimetersPerAu × TimeScale² = 11.089 厘米/秒²`，`1 AU/年 = CentimetersPerAu × TimeScale = 3330 厘米/秒`。
- **模块依赖无环**：`StellarEra → StellarEraVehicle → StellarEraPlanet`、`StellarEra → StellarEraCore`。`StellarEraVehicle` **不能**依赖 `StellarEra`。飞船里不许 include `CosmosOrbitComponent.h` 或 `CosmosSimulationSubsystem.h`。
- **数值标定**（照抄，不要「优化」）：`ThrustAcceleration = 1.2e7f`、`BoostMultiplier = 3.0f`、`DampingTime = 0.01f`、体素 `LODRange` / `CollisionsRange = 450000.f`（原稿写 `300000.f`，漏算了 `BoostMultiplier`；见 Task 2 Step 2 (c) 末尾那条订正）。
- **求解器里的等价量**：推力 1.082e6 AU/年²、`LinearDamping` k = 3.0e4 /年。稳定性 `k·dt ≪ 2`（`dt` 逐关卡派生、约 `1e-7` 年量级，`k·dt` 落在 `1e-3` 量级），**积分器不用改**。

---

## 文件结构

| 文件 | 职责 | 本计划的动作 |
|---|---|---|
| `Source/StellarEraCore/Public/NBodySimulator.h` | 天体状态 + 求解器接口 | 加 2 个字段、修 2 处过时注释 |
| `Source/StellarEraCore/Private/NBodySimulator.cpp` | 引力计算与积分 | `ComputeAccelerations` 并项、`PredictTrajectory` 副本清零 |
| `Source/StellarEraCore/Private/NBodySimulatorTests.cpp` | 求解器的自动化测试（唯一能自动验证的地方） | 加第 3 个用例 |
| `Source/StellarEraVehicle/Public/SpaceshipPawn.h` | 飞船的对外接口与参数 | 加旋钮 API、删移动组件与 6 个成员 |
| `Source/StellarEraVehicle/Private/SpaceshipPawn.cpp` | 飞船实现 | 构造函数、Tick 第 4 段、`GetStatus` |
| `Source/StellarEra/Private/StellarEraHUD.cpp` | 状态面板 | 跟进 `FSpaceshipStatus` 的字段变化 |
| `Source/StellarEra/Public/CosmosSimulationSubsystem.h` | N 体驱动接口 | 加 2 个私有函数 + 1 个标志 |
| `Source/StellarEra/Private/CosmosSimulationSubsystem.cpp` | N 体驱动实现 | 新增 `ApplyControlledBodyForces`、速度写回、诊断措辞 |

**任务顺序与中间态**：Task 1 只碰 `StellarEraCore`（纯数学，可独立跑测试）。Task 2 删掉移动组件、**此刻起飞船在引擎里不能动**（求解器还没拿到推力），Task 3 接上之后才恢复 —— 这个中间态是有意的，不是 bug。Task 2 与 Task 3 之间不要做 PIE 验证。

---

## Task 1: 求解器支持外力与线性阻尼

**Files:**
- Modify: `Source/StellarEraCore/Public/NBodySimulator.h`
- Modify: `Source/StellarEraCore/Private/NBodySimulator.cpp`
- Test: `Source/StellarEraCore/Private/NBodySimulatorTests.cpp`

**Interfaces:**
- Consumes: 无（本任务是第一环）
- Produces:
  - `FCosmosBodyState::ExternalAcceleration`（`FVector3d`，默认零，单位 AU/年²）
  - `FCosmosBodyState::LinearDamping`（`double`，默认 0，单位 1/年，`a_damp = −k·v`）
  - 语义：`a_i = G·Σ_j m_j·Δ/r³ + ExternalAcceleration_i − LinearDamping_i·Velocity_i`
  - 自动化测试用例名 `StellarEra.Core.NBodyExternalForce`

- [ ] **Step 1: 写失败的测试**

在 `Source/StellarEraCore/Private/NBodySimulatorTests.cpp` 里插入下面这个用例：**插在最后一个用例的收尾大括号（第 370 行）之后、`#endif // WITH_DEV_AUTOMATION_TESTS`（第 372 行）之前。**

> ⚠ **不要追加到文件末尾。** `#endif` 在第 372 行，也是文件的最后一行。追加到它后面会把整个用例放到 `#if WITH_DEV_AUTOMATION_TESTS` 之外 —— 结果是**编译得过、测试不注册**：跑起来只看到「两个用例」，一个错都不报。这个失败模式很安静，所以特意写在这里。

**注意**：此时 `ExternalAcceleration` / `LinearDamping` 两个字段还不存在，编译会失败 —— 这正是 Step 2 要看到的。

```cpp

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraNBodyExternalForceTest,
	"StellarEra.Core.NBodyExternalForce",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraNBodyExternalForceTest::RunTest(const FString& Parameters)
{
	// ── 1. 常力下的抛物线是**精确**的 ──
	//
	// Velocity Verlet 对「加速度与位置、速度都无关」的力是精确解，不是近似解：
	// 每一步的 x += v·dt/2 → 算 a → v += a·dt → x += v·dt/2 串起来正好等于
	// x₀ + v₀t + ½at²，与步长无关。所以这里可以用 1e-9 这种量级的容差 ——
	// 留出来的余量只装浮点累加误差（1000 次加法，量级 1e-13）。
	//
	// 这条钉住的是「外力真的进了积分器」。写错成「每帧只加一次」「忘了乘 dt」
	// 「加到了位置上而不是加速度上」都会让末点偏出十万八千里。
	{
		FNBodySimulator Simulator;
		Simulator.SetUnitSystem(ECosmosUnitSystem::Game);
		Simulator.SetIntegrator(ECosmosIntegrator::VelocityVerlet);
		Simulator.SetSofteningLength(1.0e-5);

		FCosmosBodyState Body;            // 单天体 = 没有引力，只剩外力
		Body.Mass = 0.0;
		Body.Position = FVector3d::ZeroVector;
		Body.Velocity = FVector3d(1.0, 0.0, 0.0);
		Body.ExternalAcceleration = FVector3d(0.0, -2.0, 0.0);
		Simulator.AddBody(Body);

		const double Dt = 0.01;
		const int32 NumSteps = 1000;      // t = 10 年
		for (int32 Step = 0; Step < NumSteps; ++Step)
		{
			Simulator.StepExact(Dt);
		}

		const FVector3d FinalPosition = Simulator.GetBody(0).Position;
		const FVector3d FinalVelocity = Simulator.GetBody(0).Velocity;

		// x = 0 + 1×10 = 10；y = 0 − ½×2×10² = −100；z 不动
		TestEqual(TEXT("常力：末位置 X"), FinalPosition.X, 10.0, 1.0e-9);
		TestEqual(TEXT("常力：末位置 Y"), FinalPosition.Y, -100.0, 1.0e-9);
		TestEqual(TEXT("常力：末位置 Z"), FinalPosition.Z, 0.0, 1.0e-9);

		// vx 不变 = 1；vy = 0 − 2×10 = −20
		TestEqual(TEXT("常力：末速度 X"), FinalVelocity.X, 1.0, 1.0e-9);
		TestEqual(TEXT("常力：末速度 Y"), FinalVelocity.Y, -20.0, 1.0e-9);
	}

	// ── 2. 阻尼是指数衰减，且误差随步长**线性**下降 ──
	//
	// 阻尼项在 StepVelocityVerlet 里用的是**踢之前**的速度（步骤②重算加速度时
	// 速度还没更新），所以它是显式欧拉：v_{n+1} = v_n·(1 − k·dt)，闭式解
	// v_N = v₀·(1 − k·dt)^N，恒小于解析解 v₀·e^(−k·t)。显式欧拉是一阶的，
	// 所以步长减半、误差也减半 —— 这正是下面那条「一阶收敛」断言钉的东西。
	//
	// 为什么要钉「一阶」而不只钉数值：只钉数值的话，把阻尼写成用**踢之后**的
	// 速度（隐式欧拉，误差反号）照样能过。阶数对了，说明实现方式就是设计里那一种。
	{
		const double K = 2.0;             // 1/年
		const double V0 = 1.0;
		const double Exact = V0 * FMath::Exp(-K * 1.0);   // t = 1 年

		// 单天体 + 只有阻尼，没有引力也没有外力。
		auto RunDecay = [](double Dt, int32 NumSteps, double K, double V0) -> double
		{
			FNBodySimulator Simulator;
			Simulator.SetUnitSystem(ECosmosUnitSystem::Game);
			Simulator.SetIntegrator(ECosmosIntegrator::VelocityVerlet);

			FCosmosBodyState Body;
			Body.Mass = 0.0;
			Body.Velocity = FVector3d(V0, 0.0, 0.0);
			Body.LinearDamping = K;
			Simulator.AddBody(Body);

			for (int32 Step = 0; Step < NumSteps; ++Step)
			{
				Simulator.StepExact(Dt);
			}
			return Simulator.GetBody(0).Velocity.X;
		};

		const double Coarse = RunDecay(1.0e-4, 10000, K, V0);   // 10000 × 1e-4 = 1 年
		const double Fine = RunDecay(5.0e-5, 20000, K, V0);     // 20000 × 5e-5 = 1 年

		const double ErrCoarse = FMath::Abs(Coarse - Exact);
		const double ErrFine = FMath::Abs(Fine - Exact);

		TestTrue(TEXT("阻尼：粗步长解低于解析解（显式欧拉的符号）"), Coarse < Exact);
		TestTrue(TEXT("阻尼：误差在 1e-4 以内"), ErrCoarse < 1.0e-4);
		TestTrue(TEXT("阻尼：误差不为零（真的在积分，不是抄了解析解）"), ErrCoarse > 1.0e-8);
		// 期望比值 ≈ 0.5（一阶）。放到 0.6 留余量，仍挡得住「零阶」（比值 1）
		// 与「二阶」（比值 0.25）两种写错。
		TestTrue(TEXT("阻尼：步长减半，误差减半（一阶收敛）"), ErrFine < ErrCoarse * 0.6);
	}

	// ── 3. 终端速度 v → A/k ──
	//
	// 这是飞船赖以工作的那一条：阻尼模型下按 W 会收敛到「推力 × τ」，而 τ = 1/k。
	// 离散系统的不动点**恰好**是 A/k（解 v = A·dt + v·(1 − k·dt) 即得），
	// 所以长跑之后应该贴在 A/k 上。写成 A/(k·dt) 或 A·k 都会在这里红。
	{
		const double A = 2.0;
		const double K = 4.0;
		const double Terminal = A / K;    // 0.5

		FNBodySimulator Simulator;
		Simulator.SetUnitSystem(ECosmosUnitSystem::Game);
		Simulator.SetIntegrator(ECosmosIntegrator::VelocityVerlet);

		FCosmosBodyState Body;            // 从静止起，只有常力 + 阻尼
		Body.Mass = 0.0;
		Body.ExternalAcceleration = FVector3d(A, 0.0, 0.0);
		Body.LinearDamping = K;
		Simulator.AddBody(Body);

		const double Dt = 1.0e-3;         // k·dt = 4e-3，稳定性余量三个数量级
		for (int32 Step = 0; Step < 5000; ++Step)   // t = 5 年 ≫ 1/k = 0.25 年
		{
			Simulator.StepExact(Dt);
		}

		const double Final = Simulator.GetBody(0).Velocity.X;

		TestTrue(TEXT("终端速度：从下方逼近 A/k"), Final < Terminal);
		TestEqual(TEXT("终端速度 = A/k"), Final, Terminal, 1.0e-6);
	}

	// ── 4. 预测副本不继承外力 ──
	//
	// PredictTrajectory 是 `FNBodySimulator Copy = *this;` 再往前跑。值拷贝会把
	// ExternalAcceleration / LinearDamping 一起带过去，星图上就会画出一条「一直
	// 加速」的线 —— 那不是玩家要的信息（预测的是**松手之后**的纯引力轨迹）。
	//
	// 用「同一初态、一份设外力一份不设」的对照跑逐点比对，比「主求解器没被改动」
	// 更严：后者只挡得住「忘了用副本」，挡不住「副本继承了外力」。
	{
		auto MakePair = [](bool bForced)
		{
			FNBodySimulator Simulator = MakeCircularTwoBody(0.718, 1.0);
			if (bForced)
			{
				// 施在**下标 1** 那个天体身上（也就是下面预测的那个）—— 一旦副本
				// 继承了外力，预测线会被推得肉眼可见（0.608 年里偏 ½×5×0.608² ≈ 0.92 AU）。
				Simulator.GetBodyMutable(1).ExternalAcceleration = FVector3d(0.0, 5.0, 0.0);
				Simulator.GetBodyMutable(1).LinearDamping = 3.0;
			}
			return Simulator;
		};

		FNBodySimulator Forced = MakePair(true);
		FNBodySimulator Control = MakePair(false);

		TArray<FVector3d> ForcedPoints;
		TArray<FVector3d> ControlPoints;
		TestTrue(TEXT("预测调用成功（带外力）"),
			Forced.PredictTrajectory(1, 0.608, 64, ForcedPoints));
		TestTrue(TEXT("预测调用成功（无外力对照）"),
			Control.PredictTrajectory(1, 0.608, 64, ControlPoints));

		TestEqual(TEXT("预测点数一致"), ForcedPoints.Num(), ControlPoints.Num());

		// 逐点比，只累加「不一样」的个数 —— 64 次 Printf 会淹掉日志。
		// 两份求解器由同一个函数、同一串运算造出来，浮点结果必须逐位相同。
		const int32 Count = FMath::Min(ForcedPoints.Num(), ControlPoints.Num());
		int32 Mismatched = 0;
		for (int32 Index = 0; Index < Count; ++Index)
		{
			if (ForcedPoints[Index] != ControlPoints[Index])
			{
				++Mismatched;
			}
		}
		TestEqual(TEXT("预测点逐位相同：外力没有渗进预测副本"), Mismatched, 0);
	}

	return true;
}
```

同时把文件顶部注释里列用例的那两行（第 8–9 行）改成三个：

```cpp
// 跑法：用 UnrealEditor-Cmd 跑自动化测试 StellarEra.Core（三个用例：
// StellarEra.Core.NBodyPrediction、StellarEra.Core.NBodyContact、
// StellarEra.Core.NBodyExternalForce）。
```

- [ ] **Step 2: 跑测试确认它失败（编译不过）**

先确认编辑器已关（构建需要）：

```powershell
Get-Process UnrealEditor,UnrealEditor-Cmd -ErrorAction SilentlyContinue | Select-Object Id,ProcessName
```

**有输出就停下来问用户能不能关编辑器。** 没有输出就构建：

```powershell
powershell -File "$env:TEMP\ue_mcp\ue_build.ps1"
```

期望：编译失败，报 `FCosmosBodyState` 没有 `ExternalAcceleration` 成员（`C2039`）。

- [ ] **Step 3: 加两个字段**

`Source/StellarEraCore/Public/NBodySimulator.h`，在 `SofteningOverride`（第 42 行）之后、`OwnerActor` 的文档注释（第 44 行）之前插入：

```cpp
	/**
	 * 外力加速度（AU/年²）。**不是引力** —— 由外部每帧写入，求解器只加不解释。
	 *
	 * 默认零 = 纯引力天体，现有行为一字不变（`x + 0.0` 在 IEEE 下是精确运算）。
	 * 本项目的飞船用它承载推力，见 UCosmosSimulationSubsystem::ApplyControlledBodyForces。
	 */
	FVector3d ExternalAcceleration = FVector3d::ZeroVector;

	/**
	 * 线性阻尼系数（1/年）：a_damp = −k·v。默认 0 = 无阻尼。
	 *
	 * 单位是 1/年 而不是 1/秒，因为求解器的时间单位是模拟年。
	 *
	 * 必须在求解器里**逐子步**算，不能在外部按帧算好一个常量加速度：一帧最多跑
	 * MaxStepsPerFrame = 200 个子步，这些子步覆盖的世界时间与飞船的阻尼时间常数
	 * τ = 10 毫秒同量级 —— 一帧之内速度要变 2 倍以上，按帧首速度算常量阻力等于
	 * 把阻尼削掉一大截。
	 */
	double LinearDamping = 0.0;
```

- [ ] **Step 4: 把两项并进加速度**

`Source/StellarEraCore/Private/NBodySimulator.cpp` 第 108 行，把

```cpp
		Accelerations[i] = G * Sum;
```

改成

```cpp
		// 外力与阻尼都是**逐子步**求值的：阻尼那一项用的是本步此刻的 Velocity，
		// 而 StepVelocityVerlet 在步骤②重算加速度时速度还没被踢 —— 所以阻尼是
		// 显式欧拉，每步放大因子 1 − k·dt。稳定性要求 k·dt < 2。
		//
		// 判据只写公式，不代具体步长：dt 是逐关卡派生的（最短周期 / StepsPerOrbit，
		// 另有钳制与默认值兜底），换关卡、甚至同关卡改初始条件都会变。飞船的
		// k = 3.0e4 /年，本关卡 dt 在 1e-7 年量级，于是 k·dt 在 1e-3 量级 ——
		// 远小于上限 2，余量两到三个数量级。
		Accelerations[i] = G * Sum
			+ Bodies[i].ExternalAcceleration
			- Bodies[i].LinearDamping * Bodies[i].Velocity;
```

- [ ] **Step 5: 预测副本清零**

同文件 `PredictTrajectory`，第 488 行 `FNBodySimulator Copy = *this;` 之后紧接着加：

```cpp
	// 副本会把 ExternalAcceleration / LinearDamping 一起继承过去，于是星图上的
	// 预测线画成一条「一直加速」的曲线。这里清零 —— 预测的是**松手之后会去哪**
	// 的纯引力轨迹，那才是玩家要的信息。
	//
	// 想要「一直按着 W 会去哪」是另一个特性，应该显式做，不该靠静默继承。
	for (FCosmosBodyState& Body : Copy.Bodies)
	{
		Body.ExternalAcceleration = FVector3d::ZeroVector;
		Body.LinearDamping = 0.0;
	}
```

`Copy.Bodies` 是私有成员，但这里是 `FNBodySimulator` 的成员函数，访问同一个类的私有成员是合法的。

- [ ] **Step 6: 修两处过时注释**

同文件（`NBodySimulator.h`）两处，都改 `FCosmosBodyState` 的文档注释。

**(a) `Radius` 的注释末三段（第 35–37 行）。** 它说飞船半径是 0 —— **已不成立**（A+B 那次改动让飞船的 Radius = 碰撞球半径 300 厘米）。

改前（第 35–37 行，逐字）：

```cpp
	 * 为 0 表示点质量，永不接触 —— 恒星以及任何没设半径的天体都是这样。本项目里
	 * 只有体素星球有地表半径（AddBodyToSimulator 里从 GetSurfaceRadiusCm 取），
	 * 飞船是 0，所以「飞船落到地表」= 飞船的**中心点**被钳在地表半径上。
```

改后（替换上面三行，前后两段与结尾的 `*/` 不动）：

```cpp
	 * 为 0 表示点质量，永不接触 —— 恒星以及任何没设半径的天体都是这样。本项目里
	 * 体素星球与飞船都有半径：星球取当前朝向下的地形地表高度
	 * （RefreshPlanetContactRadii），飞船取碰撞球半径 300 厘米
	 * （ASpaceshipPawn::GetContactRadiusCm）。飞船的半径**不是 0**，否则它的
	 * **中心点**会被钳在地表上、机身下半截埋进地形里。
```

> ⚠ **最终复审轮订正**：上面这个"改后"块在最终复审时又改过一次 —— 原文用「恒星」
> 举例，但本关卡没有网格恒星，改成了代码判据式写法（只有 `AVoxelPlanetActor` 携带人）。
> 以 `Source/StellarEraCore/Public/NBodySimulator.h` 的发货注释为准。

**(b) `ComputeContactShares` 的注释中间三行（第 264–266 行）。** 它说「本项目的飞船就是 `MassSolar = 0`」—— 实际是 `1e-6`。

改前（第 264–266 行，逐字）：

```cpp
	 * 质量 ≤ 0 视为测试粒子（本项目的飞船就是 MassSolar = 0）：它让全部，
	 * 对方一点都不让。取质量倒数会除零，所以这里必须显式分情形，不能写成
	 * m_j/(m_i+m_j)。
```

改后（替换上面三行，首行「接触时两个天体各让多少位移…」与结尾的 `*/` 不动）：

```cpp
	 * 质量 ≤ 0 视为测试粒子：它让全部，对方一点都不让。取质量倒数会除零，
	 * 所以这里必须显式分情形，不能写成 m_j/(m_i+m_j)。
	 *
	 * 本项目的飞船**不是**测试粒子（MassSolar = 1e-6）：它按质量比让路，
	 * 但 1e-6 相对行星的 0.873237 等于让全部，行为上仍是「行星纹丝不动」。
```

- [ ] **Step 7: 跑测试确认全部通过**

跑测试**不需要关编辑器**（只有构建需要）—— `run_nbody_test.ps1` 用 `-abslog` 把日志写到临时目录，不碰 `Saved/Logs`，编辑器开着也能跑。

那个脚本现在只跑一个用例（第 8 行 `$filter = 'StellarEra.Core.NBodyPrediction'`）。**先把那一行改成 `$filter = 'StellarEra.Core'`**，三个用例就会一起跑，然后：

```powershell
powershell -File "$env:TEMP\ue_mcp\run_nbody_test.ps1"
```

（脚本自己会打印 `Found N automation tests`、每条 `Test Completed`、`tests performed` 与失败行。日志留在 `%TEMP%\ue_mcp\nbody_test.log`。）

期望：
- `StellarEra.Core.NBodyExternalForce` 的断言全 Success。
- **`StellarEra.Core.NBodyPrediction` 与 `StellarEra.Core.NBodyContact` 仍然全 Success。** 这两个是回归门：接触用例对位置/速度的容差是 **1e-15**（5000 步），只要新加的两项在零值时不是精确为零（比如把 `+ Ext` 写成了 `+ Ext*dt`、或阻尼符号写反），它就会红。这就是 spec §8.4 那条「结果必须一字不变」的机械校验。
- 汇总行 `N tests performed, 0 failed`。

- [ ] **Step 8: 停下报告，等用户指示是否提交**

把 `git diff --stat` 与上一步的汇总行报给用户。**用户明确同意后**才执行：

```bash
git add Source/StellarEraCore/Public/NBodySimulator.h Source/StellarEraCore/Private/NBodySimulator.cpp Source/StellarEraCore/Private/NBodySimulatorTests.cpp
git commit -m "feat(宇宙): 求解器支持外力与线性阻尼 —— 飞船推力的承载面

FCosmosBodyState 加 ExternalAcceleration（AU/年²）与 LinearDamping（1/年），
ComputeAccelerations 末尾并进加速度：a = G·Σ + Ext − k·v。

阻尼必须在求解器里逐子步算而不是按帧算：一帧最多 200 个子步 ≈ 7.5 毫秒
世界时间，而飞船的 τ 是 10 毫秒，一帧之内速度要变 2 倍以上。阻尼项用的是
踢之前的速度（显式欧拉），放大因子 1 − k·dt，飞船的 k·dt = 3.75e-3。

PredictTrajectory 的副本清零这两项：预测的是松手之后的纯引力轨迹，
不然星图上会画出一条一直加速的线。

新增 StellarEra.Core.NBodyExternalForce 用例（常力抛物线精确、阻尼一阶收敛、
终端速度 = A/k、预测副本不继承外力）。原有两个用例的 1e-15 容差同时充当
「零值时逐位不变」的回归门。

Co-Authored-By: Claude Code <noreply@anthropic.com>" \
  Source/StellarEraCore/Public/NBodySimulator.h Source/StellarEraCore/Private/NBodySimulator.cpp Source/StellarEraCore/Private/NBodySimulatorTests.cpp
```

> ⚠ **最终复审轮订正**：上面这段是 `4ade358` 的**提交信息原文**，其中的 `≈ 7.5 毫秒`
> 与 `k·dt = 3.75e-3` 是写计划时那一次运行的关卡实测值，后来随关卡改动而变；结论
> `k·dt ≪ 2` 不受影响。**引文原样保留，不改写历史记录。**

**提交必须带 pathspec 挂在 `git commit` 上**（索引里有常驻暂存文件，裸 `git commit` 会一口气吞掉全部）。

---

## Task 2: 飞船暴露旋钮，删掉移动组件

**Files:**
- Modify: `Source/StellarEraVehicle/Public/SpaceshipPawn.h`
- Modify: `Source/StellarEraVehicle/Private/SpaceshipPawn.cpp`
- Modify: `Source/StellarEra/Private/StellarEraHUD.cpp`

**Interfaces:**
- Consumes: 无（不依赖 Task 1 的字段 —— 飞船只用引擎类型）
- Produces（Task 3 依赖这些确切签名）：
  - `FVector ASpaceshipPawn::GetThrustAccelerationWorld() const;`
  - `float ASpaceshipPawn::GetDampingTime() const;`
  - `void ASpaceshipPawn::SetCosmosVelocity(const FVector& InVelocity);`
  - `virtual FVector ASpaceshipPawn::GetVelocity() const override;`
  - `FSpaceshipStatus` 删掉 `bThrustLocked`，`MaxSpeed` 语义改为「阻尼极速，0 = 无上限」
  - 成员 `float ThrustAcceleration = 1.2e7f;`、`float BoostMultiplier = 3.0f;`、`float DampingTime = 0.01f;`

> ⚠ **本任务结束时飞船在引擎里动不了** —— 移动组件删了，求解器还没拿到推力（Task 3 才接上）。这是有意的中间态，不要在这个点上做 PIE 验证，也不要「临时加个推力」救它。

- [ ] **Step 1: 改头文件 —— 删掉移动组件的痕迹**

`Source/StellarEraVehicle/Public/SpaceshipPawn.h`：

(a) 第 13 行，删掉前向声明：

```cpp
class UFloatingPawnMovement;
```

(b) 第 52–58 行的类注释，整段替换成：

```cpp
/**
 * 街机式太空飞船。
 *
 * **位移完全归 N 体求解器**：本类不自己搬自己，只把「这一帧想施加多少推力加速度」
 * 暴露出去（GetThrustAccelerationWorld / GetDampingTime），由
 * UCosmosSimulationSubsystem 每帧下来读走、换算成 AU/年² 写进它那个 FCosmosBodyState。
 * 位置与速度都由求解器写回（SetActorLocation + SetCosmosVelocity）。
 *
 * 力只能是这个方向：StellarEraVehicle 不能依赖 StellarEra（会成环），所以飞船
 * 既不能 include CosmosOrbitComponent.h，也不能调那个子系统。
 *
 * 鼠标给的是角速度冲量而不是直接设朝向，松手后指数衰减 —— 这是"开飞机"的惯性感
 * 来源，也是《戴森球计划》伊卡洛斯的做法。姿态是**运动学**的：它只决定推力朝哪，
 * 不参与积分。
 */
```

(c) 第 143 行，`Collision` 的注释：

```cpp
	/** 碰撞球：根组件，也是飞船「占多大地方」的那个形状（接触半径取自它）。 */
```

(d) 第 184–185 行，删掉整个组件成员：

```cpp
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Spaceship")
	TObjectPtr<UFloatingPawnMovement> FloatingMovement;
```

(e) 在 `GetContactRadiusCm()`（第 140 行）之后、`protected:`（第 142 行）之前，插入四个新接口：

```cpp
	/**
	 * 本帧要施加在自己那个 N 体上的推力加速度（厘米/秒²，世界坐标）。零 = 无推力。
	 *
	 * 由 UCosmosSimulationSubsystem 每帧下来读走（依赖方向见类注释）。
	 *
	 * 方向 = 机头 × ThrustInput + 机身上 × VerticalInput，合成向量夹到长度 1 ——
	 * 与 UFloatingPawnMovement 原来的 GetClampedToMaxSize(1) 同规则：斜着同时按
	 * W 和 Space 不该比单按一个多出 41% 的推力。再乘 ThrustAcceleration，
	 * Shift 时乘 BoostMultiplier。
	 */
	FVector GetThrustAccelerationWorld() const;

	/**
	 * 阻尼时间常数（秒）。0 = 不阻尼。
	 *
	 * 巡航（bPreserveVelocity）开着就是 0 —— 「巡航 = 只关阻尼，推力照给」：
	 * 关掉阻尼才有办法一路加速到圆轨道速度（阻尼极速只有它的 0.156 倍，见 spec §4）。
	 * 有阻尼时速度收敛到「推力 × 本值」，这就是极速的来源。
	 */
	float GetDampingTime() const;

	/**
	 * 由 UCosmosSimulationSubsystem 在写回位置时一并写入的**求解器速度**
	 * （厘米/秒，世界坐标）。本类不自己算速度 —— 位移归求解器，速度也就该由它给。
	 */
	void SetCosmosVelocity(const FVector& InVelocity) { CosmosVelocity = InVelocity; }

	/**
	 * 覆写：让引擎侧所有问「这艘船多快」的地方拿到求解器给的真值。
	 *
	 * 单点收口，替掉三个散落的改点：IsLanded() 的第一道判断、GetStatus().Speed、
	 * 以及体素调用器的预测（UVoxelInvokerWithPredictionComponent::
	 * GetInvokerGlobalPosition 读的是 GetOwner()->GetVelocity()）。删掉移动组件后
	 * 根组件的速度不再有人写，不覆写的话预测会静默失效 —— 高速飞行时地形就追不上了。
	 */
	virtual FVector GetVelocity() const override { return CosmosVelocity; }
```

(f) 第 83 行，`SetBoost` 的注释：

```cpp
	/** 加速键（Shift）。按下时推力加速度乘 BoostMultiplier。 */
```

(g) 第 97–107 行，`TogglePreserveVelocity` 的注释，整段替换成：

```cpp
	/**
	 * 巡航开关（Tab）。翻转 bPreserveVelocity。
	 *
	 * 开：关掉阻尼，速度不再自动衰减 —— 按 W 加到稳、按一下 Tab 就能松开 W 继续飞。
	 * 关：有阻尼，松手后速度按 τ 衰减停下。
	 *
	 * **只关阻尼，推力照给**：开着巡航按住 W 才能一路加速到圆轨道速度
	 * （阻尼极速 1.2 km/s 只有圆轨道速度 7.678 km/s 的 0.156 倍）。
	 *
	 * 只翻转、不接收目标值：状态归飞船自己管，输入侧不需要知道当前是开是关。
	 */
```

(h) 第 30–49 行，`FSpaceshipStatus` 整段替换成（删掉 `bThrustLocked`、重定义 `MaxSpeed`）：

```cpp
struct FSpaceshipStatus
{
	/** 速度大小（厘米/秒）。取自求解器写回的速度（见 ASpaceshipPawn::GetVelocity）。 */
	float Speed = 0.f;

	/**
	 * 阻尼极速（厘米/秒）= 推力加速度 × 阻尼时间常数。
	 *
	 * 0 表示**没有上限**（巡航开着，阻尼关掉了），不是「不能动」。
	 */
	float MaxSpeed = 0.f;

	/** 主推力输入，-1 ~ +1。 */
	float ThrustInput = 0.f;

	/** Shift 是否按下。 */
	bool bBoosting = false;

	/** 巡航（速度保持）是否开启。 */
	bool bPreserveVelocity = false;

	/** 是否判定为已落地。 */
	bool bLanded = false;
};
```

(i) 第 269–286 行，`MinThrustSpeed` 与 `BoostAcceleration` 两个块整段替换成三个新参数：

```cpp
	/**
	 * 主推力加速度（厘米/秒²）。**本轮唯一被调大的量。**
	 *
	 * 下限被当地重力锁死：本关卡中心天体 100 M☉、飞船在 r = 0.07425 AU，
	 * 表面重力 = G·M/r² = 716,089 AU/年² = 7.94e6 cm/s²。推力必须大于它，否则
	 * 飞船一步都升不起来 —— 这跟怎么接线无关，是物理。改动前飞船的最大推力是
	 * 4000 + 12000 = 16000 cm/s²，只有重力的 0.20 %。
	 *
	 * 1.2e7 = 1.51 × 表面重力，留 51% 净余量。
	 *
	 * 极速 = 本值 × DampingTime = 1.2e7 × 0.01 = 1.2e5 cm/s = 1.2 km/s，只有该处
	 * 圆轨道速度（7.678e5 cm/s）的 0.156 倍 —— 光按 W 飞不出行星，只能悬停、
	 * 爬升、绕圈。要进轨道得开巡航（关阻尼）持续加速。
	 */
	UPROPERTY(EditAnywhere, Category = "Spaceship|Thrust")
	float ThrustAcceleration = 1.2e7f;

	/** 按 Shift 时乘在 ThrustAcceleration 上的倍数。松开即回到 1。 */
	UPROPERTY(EditAnywhere, Category = "Spaceship|Thrust")
	float BoostMultiplier = 3.0f;

	/**
	 * 阻尼时间常数 τ（秒）：速度按 a_damp = −v/τ 衰减，收敛到「推力 × τ」。
	 *
	 * 单位是秒，而求解器的时间单位是模拟年 —— 换算在
	 * UCosmosSimulationSubsystem::ApplyControlledBodyForces 里做（除以 TimeScale）。
	 *
	 * 0.01 秒是**在「有惯性感」和「世界跟得上」之间取的折中**：τ 再大极速就上千
	 * km/s，体素调用器的范围要跟着涨到几十公里。代价是响应很硬 —— 约 3τ = 30 毫秒
	 * 到极速（60 fps 下两帧）。旧的「2 秒加到 80 m/s」是旧尺度（飞船不参与引力）
	 * 的产物，回不去了。
	 */
	UPROPERTY(EditAnywhere, Category = "Spaceship|Thrust")
	float DampingTime = 0.01f;
```

(j) 第 299–300 行，`bPreserveVelocity` 的注释，整段替换成：

```cpp
	/**
	 * 巡航开关。开 = 关掉阻尼（速度保持）；关 = 有阻尼，松手就按 τ 衰减停下。
	 *
	 * **只关阻尼，推力照给** —— 开着巡航按住 W 才能一路加速到圆轨道速度。
	 *
	 * 关掉阻尼之后飞船走纯引力测地线：松手不减速，但**行星引力照样作用**，
	 * 所以「停」不下来，只能悬停或绕圈。
	 *
	 * 运行时按 Tab 切换（见 TogglePreserveVelocity），细节面板里改的是**初始状态**，
	 * 每帧生效，PIE 里也能直接勾。
	 *
	 * 刹车按 S：反向推力会先把速度减到零再反向，不需要额外的刹车键。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spaceship|Thrust")
	bool bPreserveVelocity = false;
```

(k) 第 400–410 行，删掉 `BaseAcceleration` / `BaseDeceleration` / `ForwardSpeed` 三个成员及注释，原地插入：

```cpp
	/**
	 * 求解器每帧写回的当前速度（厘米/秒，世界坐标）。
	 *
	 * 由 UCosmosSimulationSubsystem 在写回位置时一并写入，本类只读不改。
	 * 初值零 —— 第一帧写回之前 GetVelocity() 返回零向量，IsLanded() 因此按
	 * 「速度为零」判断，与旧行为一致。
	 */
	FVector CosmosVelocity = FVector::ZeroVector;
```

- [ ] **Step 2: 改实现 —— 构造函数**

`Source/StellarEraVehicle/Private/SpaceshipPawn.cpp`：

(a) 第 10 行，删掉 include：

```cpp
#include "GameFramework/FloatingPawnMovement.h"
```

(b) 第 68–75 行，整段替换成一句注释（保留「为什么没有移动组件」这件事）：

```cpp
	// 没有移动组件：位移完全归 N 体求解器（见类注释）。曾经的 UFloatingPawnMovement
	// 会每帧 MoveUpdatedComponent(Velocity * dt)，留着它就和求解器的写回抢着改位置
	// —— 而且「不喂它输入」也挡不住，它照样按残留速度搬人。
```

(c) 第 39–54 行，`VoxelInvoker` 的那整段注释。**只改其中两个数所在的句子，末段「数值与角色一致…」原样保留**（它记着 `LODToSet = 0` 的约定和「两个旋钮必须一起调」——那条信息与本次改动无关，别丢）。

改后（整段替换，注意末段被保留了）：

```cpp
	// --- 体素调用器 ---
	//
	// 挂在根节点而不是机头：调用器的位置同时决定 LOD 范围和碰撞范围，碰撞范围
	// 必须罩住飞船自身的物理体，否则飞机会撞进还没生成碰撞体的地形里。
	//
	// 用带预测的版本，而不是角色那种 UVoxelSimpleInvokerComponent：极速
	// 1.2e5 cm/s（= 推力 1.2e7 × 阻尼时间常数 0.01，见 ThrustAcceleration），
	// 而范围只有 200 m 的话不预测就只剩 0.17 秒的提前量，地形还没网格化完人已经
	// 撞上去了。预测把采样点沿速度方向前移 PredictionTime 秒
	// （UVoxelInvokerWithPredictionComponent::GetInvokerGlobalPosition），凭空
	// 多出 1.2 公里预警，且不额外扩大细分区域。停稳时速度为零，退化成普通调用器。
	//
	// ⚠ 调用器的位置是**搬到预测点**去的（同上：Position = 组件位置 + 速度 ×
	// PredictionTime），不是「两个点都罩住」—— 所以范围必须自己罩得住那个偏移量：
	// 硬约束是「范围 ≥ 最大速度 × PredictionTime」。最大速度**含** BoostMultiplier
	// （1.2e5 × 3 = 3.6e5 厘米/秒），PredictionTime 保持 1 秒 → 预测点超前 3.6e5
	// 厘米。两个范围因此取 4.5e5 厘米 = 3.6e5 × 1.25。
	//
	// 巡航（关阻尼）下速度无上界，任何有限范围都盖不住 —— 这是已知上限，不是调
	// 参数能解决的：届时预测点跑到范围外，调用器就盖不到飞船本体了。
	//
	// 数值与角色一致（见 PlanetCharacter.cpp）：LODToSet = 0 是插件对「贴着表面的
	// Pawn」的约定用法。范围留在组件面板上可调 —— 嫌地形追不上就同时调大
	// LODRange 与 CollisionsRange（两个都要，LOD 只影响画质，碰撞由
	// CollisionsRange 单独驱动），或把 bUseForPriorities 打开让飞船附近的区块
	// 优先网格化。
	//
	// 代价未知：本关卡行星半径只有 750 米，4.5 公里的范围等于整颗行星全程满精度 +
	// 带碰撞。这一项要实测；嫌贵就往回收，改完把上面的推导一起改掉。
```

> 三处与原文的差别，都是有意的：`8000 cm/s → 2.5 秒` 变成 `1.2e5 cm/s → 0.17 秒`（速度涨了 15 倍，20000 厘米 ÷ 1.2e5 = 0.167 秒，同样的范围下提前量就缩短 15 倍）；末段里「200 m 是本项目已经在跑的量级」删掉了，因为范围已经不是 200 m。
>
> 第三处是**整条分支复审时的订正**（F2）：上面那段 ⚠ 的推导原本只用了极速 `1.2e5 cm/s`、据此把范围写成 3 公里，**漏了 `BoostMultiplier`** —— 按 Shift 时极速是 `3.6e5 cm/s`，而调用器的位置是搬到预测点去的（偏移 = 速度 × `PredictionTime` = 3.6e5 厘米），3e5 厘米的范围罩不住飞船本体。订正后的推导与数值就是上面这份，范围取 4.5 公里。这里如实记下，免得下一轮再从「3 公里」反推一遍。

(d) 第 59 行与第 61 行，范围改大（**原稿写 `300000.f`，因漏算 `BoostMultiplier` 复审时上调到 `450000.f`**）：

```cpp
	VoxelInvoker->LODRange = 450000.f;
```

```cpp
	VoxelInvoker->CollisionsRange = 450000.f;
```

- [ ] **Step 3: 改实现 —— BeginPlay**

第 205–212 行，整段删掉（没有组件要抄基准值了）：

```cpp
	// 同理抄下加减速度的基准值：Tick 每帧都会改写组件上的这两个数
	// （Shift 叠加、惯性开关归零），没有基准就再也回不去了。
	// 从组件上读而不是写死 4000 / 2000，蓝图或关卡实例改过的值才作数。
	if (FloatingMovement)
	{
		BaseAcceleration = FloatingMovement->Acceleration;
		BaseDeceleration = FloatingMovement->Deceleration;
	}
```

- [ ] **Step 4: 改实现 —— Tick 第 4 段塌缩**

第 296–371 行（从 `// --- 4. 推力 ---` 到那两条 `AddInputVector` 的收尾 `}`），整段替换成：

```cpp
	// --- 4. 推力 ---
	//
	// 推力不再由这里施加 —— 本类只把「想施加多少」暴露出去
	// （GetThrustAccelerationWorld / GetDampingTime），由 UCosmosSimulationSubsystem
	// 每帧下来读走、换算、写进求解器。
	//
	// 本段只剩相机后缩要用的 ThrustInput，见第 5 段。
```

**删掉的东西一并确认不留残骸**：`ForwardSpeed`、`MinThrustSpeed` 保底速度那一段、
`bCruiseCoasting` 那段 slerp、`BaseAcceleration` / `BaseDeceleration`、
`FloatingMovement->Deceleration` / `->Acceleration` / `->Velocity` / `->TurningBoost`、
两条 `AddInputVector`。

**巡航的语义变了**：旧代码在巡航时把速度矢量 slerp 到机头方向（为了锁住速率）。
新模型下不需要 —— 加速度本来就沿机头，速度会自然弯过去；松手时无推力无阻尼，
走纯引力测地线。这比旧行为更对。

- [ ] **Step 5: 改实现 —— 新增两个 getter 与重写 GetStatus**

(a) 在 `ASpaceshipPawn::GetContactRadiusCm()` 之后（文件末尾，第 560 行之后）追加：

```cpp
FVector ASpaceshipPawn::GetThrustAccelerationWorld() const
{
	// 方向 = 机头 × 前后输入 + 机身上 × 升降输入，合成向量夹到长度 1 ——
	// 与 UFloatingPawnMovement::AddInputVector 的 GetClampedToMaxSize(1) 同规则：
	// 斜着同时按 W 和 Space 不该比单按一个多出 41% 的推力。
	const FVector Direction =
		(GetActorForwardVector() * ThrustInput + GetActorUpVector() * VerticalInput)
			.GetClampedToMaxSize(1.0);

	// Shift 乘在**大小**上而不是方向分量上：这样斜着按 W+Space+Shift 时，
	// 总量仍然只有单按一个的 1 倍（而不是 1.41 倍）。
	const float Magnitude = ThrustAcceleration * (bBoosting ? BoostMultiplier : 1.f);
	return Direction * Magnitude;
}

float ASpaceshipPawn::GetDampingTime() const
{
	// 巡航 = 只关阻尼，推力照给。关掉阻尼才有办法一路加速到圆轨道速度 ——
	// 阻尼极速只有它的 0.156 倍。
	return bPreserveVelocity ? 0.f : DampingTime;
}
```

(b) 第 487–509 行，`GetStatus()` 整段替换成：

```cpp
FSpaceshipStatus ASpaceshipPawn::GetStatus() const
{
	FSpaceshipStatus Status;

	// 速度取自求解器写回的值（GetVelocity 的覆写读的就是它）。
	Status.Speed = GetVelocity().Size();

	// 阻尼极速 = 推力 × τ。巡航（无阻尼）时为 0，表示「没有上限」。
	const float Damping = GetDampingTime();
	Status.MaxSpeed = Damping > 0.f
		? (ThrustAcceleration * (bBoosting ? BoostMultiplier : 1.f)) * Damping
		: 0.f;

	Status.ThrustInput = ThrustInput;
	Status.bBoosting = bBoosting;
	Status.bPreserveVelocity = bPreserveVelocity;
	Status.bLanded = IsLanded();

	return Status;
}
```

`IsLanded()` 一字不用改 —— 它的第一道判断 `GetVelocity().Size() > LandingSpeedThreshold` 自动走覆写后的真值。

- [ ] **Step 6: 改 HUD**

`Source/StellarEra/Private/StellarEraHUD.cpp` 第 84–112 行，整段替换成：

```cpp
	// 推力标签：`bThrustLocked`（旧「保底速度锁定」）随移动组件一起没了 ——
	// 阻尼模型下按 W 自然收敛到极速，不需要「保底 + 锁速」这个机制。
	FString ThrustLabel;
	if (Status.ThrustInput > 0.f)
	{
		ThrustLabel = TEXT("W 加速");
	}
	else if (Status.ThrustInput < 0.f)
	{
		ThrustLabel = TEXT("S 反推");
	}
	else
	{
		ThrustLabel = TEXT("无");
	}

	// 极速为 0 表示巡航开着（阻尼关掉了）—— 那是「没有上限」，不是「不能动」，
	// 所以分两种印法而不是直接印 0。
	const FString SpeedLine = Status.MaxSpeed > 0.f
		? FString::Printf(TEXT("速度      %d / %d cm/s"),
			FMath::RoundToInt(Status.Speed), FMath::RoundToInt(Status.MaxSpeed))
		: FString::Printf(TEXT("速度      %d cm/s（巡航，无上限）"),
			FMath::RoundToInt(Status.Speed));

	const TArray<FString> Lines =
	{
		SpeedLine,
		FString::Printf(TEXT("推力      %s"), *ThrustLabel),
		FString::Printf(TEXT("加速      %s"), Status.bBoosting ? TEXT("ON") : TEXT("OFF")),
		FString::Printf(TEXT("巡航      %s"), Status.bPreserveVelocity ? TEXT("开") : TEXT("关")),
		FString::Printf(TEXT("落地      %s"), Status.bLanded ? TEXT("是") : TEXT("否")),
	};
```

- [ ] **Step 7: 构建确认编译通过**

确认编辑器已关（同 Task 1 Step 2 的检查），然后：

```powershell
powershell -File "$env:TEMP\ue_mcp\ue_build.ps1"
```

期望：`Result: Succeeded`。若报 `Result: Failed`，先看是不是**只有 DLL 链接失败**（那也可能是「编译成功」）—— 读日志里 `error C` 开头的行，一条都没有才算通过。

- [ ] **Step 8: 停下报告，等用户指示是否提交**

```bash
git add Source/StellarEraVehicle/Public/SpaceshipPawn.h Source/StellarEraVehicle/Private/SpaceshipPawn.cpp Source/StellarEra/Private/StellarEraHUD.cpp
git commit -m "feat(飞船): 控制量改成对外暴露的推力加速度，删掉移动组件

飞船不再自己搬自己：GetThrustAccelerationWorld 给出「本帧想施加多少」，
GetDampingTime 给出阻尼时间常数，由 UCosmosSimulationSubsystem 每帧下来读走。
依赖方向是被模块分层逼出来的 —— StellarEraVehicle 不能依赖 StellarEra。

GetVelocity() 覆写返回求解器写回的速度，一处收口替掉三个改点：
IsLanded 的阈值判断、GetStatus().Speed、以及体素调用器的预测
（删掉移动组件后根组件速度不再有人写，不覆写预测会静默失效）。

UFloatingPawnMovement 整个删掉 —— 它的 TickComponent 每帧
MoveUpdatedComponent(Velocity*dt)，留着就和求解器的写回抢位置。
连带的 BaseAcceleration/BaseDeceleration/ForwardSpeed/MinThrustSpeed/
BoostAcceleration/bThrustLocked 一并消失；BoostAcceleration 改成
BoostMultiplier。

推力 1.2e7 cm/s²（= 1.51 × 表面重力 7.94e6）、BoostMultiplier 3.0、
DampingTime 0.01 秒 → 极速 1.2 km/s。体素调用器范围 200 米 → 3 公里，
沿用原来的「2.5 秒提前量」口径。

⚠ 本提交之后、子系统接上之前，飞船在引擎里动不了。

Co-Authored-By: Claude Code <noreply@anthropic.com>" \
  Source/StellarEraVehicle/Public/SpaceshipPawn.h Source/StellarEraVehicle/Private/SpaceshipPawn.cpp Source/StellarEra/Private/StellarEraHUD.cpp
```

> ⚠ **上面这段引用保持原样，是 `cb5683c` 当时真实的提交信息，不改** —— 改了就等于谎称那个提交当初是这么写的。但要知道它里面有两个数后来被推翻了：**「极速 1.2 km/s」**只算了裸推力，没乘 `BoostMultiplier`（按 Shift 时是 3.6 km/s）；据此得出的**「范围 200 米 → 3 公里」**也因此不够，整条分支复审时订正为 **4.5 公里**。以 Task 2 Step 2 (c)/(d) 那份订正后的推导为准（也就是最终代码 `SpaceshipPawn.cpp` 里现在写的）。

---

## Task 3: 子系统读旋钮、写回速度、诊断措辞

**Files:**
- Modify: `Source/StellarEra/Public/CosmosSimulationSubsystem.h`
- Modify: `Source/StellarEra/Private/CosmosSimulationSubsystem.cpp`

**Interfaces:**
- Consumes（全部来自 Task 2）：`ASpaceshipPawn::GetThrustAccelerationWorld()`、
  `ASpaceshipPawn::GetDampingTime()`、`ASpaceshipPawn::SetCosmosVelocity(const FVector&)`
- Consumes（来自 Task 1）：`FCosmosBodyState::ExternalAcceleration`、`FCosmosBodyState::LinearDamping`
- Produces: 无下游消费者（本任务是最后一环）

- [ ] **Step 1: 改头文件**

`Source/StellarEra/Public/CosmosSimulationSubsystem.h`：

(a) 第 22–27 行的类注释里那串每帧顺序，替换成：

```cpp
 * 每帧顺序（全在一次 Tick 内完成，避免天体之间差一帧）：
 *   0. 刷新行星接触半径 + 把受控天体（飞船）的推力/阻尼喂给求解器
 *   1. 推进求解器若干步
 *   2. 对每个天体：把求解器位置与速度写回 Actor（bSimulated 为真时）
 *   3. 体素星球：同步代理体素世界的变换
 *   4. 把位移施加给骑在这颗星球上的 Pawn
```

(b) 在私有段 `RefreshPlanetContactRadii()`（第 271 行）之后插入两个函数声明：

```cpp
	/**
	 * 每帧推进之前，把**受控天体**（飞船）想施加的力写进求解器。
	 *
	 * 方向只能是「子系统下来读飞船」：StellarEraVehicle 不能依赖 StellarEra，
	 * 飞船不能反过来调本子系统（见 ASpaceshipPawn 的类注释）。
	 *
	 * 只认 ASpaceshipPawn —— 现在只有一艘船，Cast 就是全部；等出现第二个受控天体
	 * （第二艘船、导弹）再抽接口，那时抽出来的形状也才看得清。
	 */
	void ApplyControlledBodyForces();

	/**
	 * 游戏单位速度（AU/年）→ **玩家感知**的世界速度（厘米/秒）。
	 *
	 * 即「飞船在真实一秒里走过多少厘米」。1 模拟年只花 1/TimeScale 真实秒，所以
	 * v_world = v_game × CentimetersPerAu × TimeScale（默认 3330）。
	 *
	 * ⚠ 与 FNBodySimulator::ToWorldVelocity 差 YearToSecond · TimeScale ≈ 1.05e5 倍
	 * —— 那个是「1 模拟年 = 1 真实年」的实时值，屏幕上的数不是它。
	 */
	FVector ToWorldVelocityScaled(const FVector3d& GameUnitVelocity) const;
```

(c) 在 `bStarMapActive`（第 292 行）之后插入成员：

```cpp
	/**
	 * 上一次 ApplyControlledBodyForces 里是否有天体被外力或阻尼作用。
	 *
	 * 有受控天体时，总能量与总动量这两项都不再守恒，这个漂移值于是不再是积分
	 * 误差：推力做功把它推正，阻尼与接触耗散把它拉负（接触抹的是相对法向速度，
	 * 只吃能量、不动总动量）。加一句提醒，免得看日志的人去查积分器。
	 */
	bool bHasForcedBodies = false;
```

- [ ] **Step 2: 改实现 —— Tick 第 0 段**

`Source/StellarEra/Private/CosmosSimulationSubsystem.cpp` 第 312–315 行，替换成：

```cpp
	// ── 0. 刷新行星的接触半径 + 施加受控天体的外力 ──
	// 两者都是「推进之前把引擎侧状态喂给求解器」。
	//
	// 接触半径必须在推进之前：接触解算发生在 StepExact 内部，读的就是这一刻的
	// Radius。放到推进之后，这一帧的接触用的还是上一帧的地形高度。
	//
	// 外力同理：本帧推进读的就是这一刻写进去的值。
	RefreshPlanetContactRadii();
	ApplyControlledBodyForces();
```

- [ ] **Step 3: 改实现 —— Tick 第 2 段的速度写回**

第 373–377 行，那个 `if (Component->bSimulated)` 块替换成：

```cpp
		if (Component->bSimulated)
		{
			const FCosmosBodyState& State = Simulator.GetBody(Component->SimulatorIndex);
			BodyActor->SetActorLocation(Simulator.ToWorldLocation(State.Position), /*bSweep=*/false);

			// 受控天体（飞船）：把求解器速度一并写回 Actor。
			// ASpaceshipPawn::GetVelocity() 的覆写读的就是它 —— 体素调用器的预测、
			// IsLanded() 的阈值判断、HUD 的速度显示都走那一处收口。
			if (ASpaceshipPawn* Ship = Cast<ASpaceshipPawn>(BodyActor))
			{
				Ship->SetCosmosVelocity(ToWorldVelocityScaled(State.Velocity));
			}
		}
```

- [ ] **Step 4: 改实现 —— 两个新函数**

在 `UCosmosSimulationSubsystem::RefreshPlanetContactRadii()` 的定义之后（第 175 行之后）插入：

```cpp
void UCosmosSimulationSubsystem::ApplyControlledBodyForces()
{
	bHasForcedBodies = false;

	// 1 AU/年² 等于多少厘米/秒²。与速度那条是同一个换算族：
	// 1 AU/年² = CentimetersPerAu × TimeScale²，默认 1e6 × 0.00333² = 11.089。
	//
	// 这道守卫挡的是 ini 里把 TimeScale 改成 0：那时 AdvanceAccumulator 每帧只加 0，
	// 求解器根本不推进，所以留下上一帧的力也无所谓；但不守卫的话除零会得到 inf，
	// 而 inf 一旦进了天体状态，GetEnergyDriftRatio 会变成 nan 把日志打花。
	const double CmPerS2PerAuPerY2 = CentimetersPerAu * TimeScale * TimeScale;
	if (CmPerS2PerAuPerY2 <= 0.0)
	{
		return;
	}

	for (UCosmosOrbitComponent* Component : Bodies)
	{
		if (!Component || Component->SimulatorIndex == INDEX_NONE)
		{
			continue;
		}

		const ASpaceshipPawn* Ship = Cast<ASpaceshipPawn>(Component->GetOwner());
		if (!Ship)
		{
			continue;
		}

		// 求解器的 GetBodyMutable 是裸的 Bodies[Index]，越界不报错、直接是 UB ——
		// 校验只能在这一层做，与 GetBodyVelocityGameUnits 同一个理由。
		if (Component->SimulatorIndex >= Simulator.GetNumBodies())
		{
			continue;
		}

		FCosmosBodyState& Body = Simulator.GetBodyMutable(Component->SimulatorIndex);

		Body.ExternalAcceleration =
			FVector3d(Ship->GetThrustAccelerationWorld()) / CmPerS2PerAuPerY2;

		// τ 是真实秒，求解器的时间单位是模拟年，所以除一个 TimeScale。
		const float DampingTime = Ship->GetDampingTime();
		Body.LinearDamping = DampingTime > 0.f
			? 1.0 / (double(DampingTime) * TimeScale)
			: 0.0;

		bHasForcedBodies |= !Body.ExternalAcceleration.IsNearlyZero()
			|| Body.LinearDamping != 0.0;
	}
}
```

再在 `GetBodyVelocityGameUnits()` 的定义之后（第 463 行之后）插入：

```cpp
FVector UCosmosSimulationSubsystem::ToWorldVelocityScaled(const FVector3d& GameUnitVelocity) const
{
	return FVector(GameUnitVelocity) * (CentimetersPerAu * TimeScale);
}
```

- [ ] **Step 5: 改实现 —— 诊断加 `[Cosmos]` 前缀 + 措辞**

> **为什么整块都要改，而不是只改那三行：** `run_game_probe.ps1` 是拿 `\[Cosmos\]` 过滤日志的（`$lines | Where-Object { $_ -match '\[Cosmos\]' } | Select-Object -Last 24`），**只有带前缀的行才进得了 probe 的输出**。而 `[Cosmos]` 在本文件里是**手写进每条日志字面量**的（第 184、253、289、357 行），没有任何宏会替你加。`LogDiagnostics()` 现在一行都没带 —— 不补的话 probe 里一个诊断数都看不到，而「probe 看不见」和「代码没生效」在输出上长得一模一样。只补两行会让整块在 probe 输出里碎成两半，所以整块补齐。

`LogDiagnostics()`（第 407–420 行）整个函数体替换成：

```cpp
	// 整块都带 `[Cosmos]` 前缀 —— probe 只打印带前缀的行（见函数上方说明）。
	UE_LOG(LogTemp, Log, TEXT("[Cosmos] ========== 诊断 =========="));
	UE_LOG(LogTemp, Log, TEXT("[Cosmos] 天体数量  : %d"), Simulator.GetNumBodies());
	UE_LOG(LogTemp, Log, TEXT("[Cosmos] 步长      : %.6e 年"), BaseTimeStep);
	UE_LOG(LogTemp, Log, TEXT("[Cosmos] TimeScale : %.5f 年/秒（公转一圈约 %.0f 秒）"),
		TimeScale, TimeScale > 0.0 ? 1.0 / TimeScale : 0.0);
	UE_LOG(LogTemp, Log, TEXT("[Cosmos] 已模拟    : %.6f 年"), Simulator.GetSimulatedTime());

	// 有受控天体时总能量与总动量都不再守恒，这个漂移值会很大 —— 那是推力做功，
	// 不是积分误差，所以加一句提醒，免得看日志的人去查积分器。
	UE_LOG(LogTemp, Log, TEXT("[Cosmos] 能量漂移  : %.6e  （辛积分应 < 1e-5）%s"),
		Simulator.GetEnergyDriftRatio(),
		bHasForcedBodies ? TEXT("  ⚠ 含受控天体，此值不守恒") : TEXT(""));
	UE_LOG(LogTemp, Log, TEXT("[Cosmos] 总动量    : %.6e"), Simulator.GetTotalMomentum().Size());
	UE_LOG(LogTemp, Log, TEXT("[Cosmos] 角动量    : %.6e"), Simulator.GetTotalAngularMomentum().Size());
	UE_LOG(LogTemp, Log, TEXT("[Cosmos] 1 AU      : %.0f 厘米"), CentimetersPerAu);

	// 受控天体（飞船）单独一行，值非零才打 —— 它的存在本身就是
	// 「子系统真的下来读到飞船了」的证据：没有输入时外力为 0，但阻尼是
	// 1/(DampingTime × TimeScale) = 3.0030e+04 /年，非零。
	for (int32 Index = 0; Index < Simulator.GetNumBodies(); ++Index)
	{
		const FCosmosBodyState& Body = Simulator.GetBody(Index);
		if (Body.ExternalAcceleration.IsNearlyZero() && Body.LinearDamping == 0.0)
		{
			continue;
		}

		UE_LOG(LogTemp, Log, TEXT("[Cosmos] 受控天体 %d：外力 %.4e AU/年²，阻尼 %.4e /年"),
			Index, Body.ExternalAcceleration.Size(), Body.LinearDamping);
	}
	UE_LOG(LogTemp, Log, TEXT("[Cosmos] =========================="));
```

函数签名与收尾大括号不动（改的只是函数体）。表头那行从 `========== Cosmos 诊断 ==========` 简化成 `========== 诊断 ==========` —— 前缀已经带了 `Cosmos`，留着重复；纯外观改动。

- [ ] **Step 6: 构建**

确认编辑器已关，然后：

```powershell
powershell -File "$env:TEMP\ue_mcp\ue_build.ps1"
```

期望：`Result: Succeeded`，且日志里没有 `error C` 开头的行。

- [ ] **Step 7: 无头 `-game` probe 做端到端验证**

```powershell
powershell -File "$env:TEMP\ue_mcp\run_game_probe.ps1"
```

（那个脚本跑 `/Game/Maps/UniverseMap`，`-game -nullrhi`，45 秒，然后按代码页 936 解码日志并打印最后 24 行 `[Cosmos]`。）

逐条核对，**四条都要对上**：

1. **`[Cosmos] 受控天体 <N>：外力 0.0000e+00 AU/年²，阻尼 3.0030e+04 /年`** —— 这一行是正证据：子系统确实找到了飞船、算出了阻尼，且 `1/(0.01 × 0.00333) = 30030.03` 说明单位换算那一串是对的（`DampingTime` 是秒、求解器是年）。外力为 0 是对的 —— probe 没法按键。**若 ini 里改过 `TimeScale`，这个数会跟着变**：用同一份输出里开局那行 `TimeScale %.5f` 反算 `1/(0.01 × TimeScale)` 核对，别照抄 3.0030e+04。
2. **`[Cosmos] 能量漂移` 那行末尾带 `⚠ 含受控天体，此值不守恒`** —— 说明 `bHasForcedBodies` 被置上了（默认 `bPreserveVelocity = false`，所以阻尼一直非零）。
3. **开局那行 `[Cosmos]   天体 N：…半径 0.0003 AU…`（飞船那一行）仍是 `0.0003`** —— A+B 那次给飞船接触半径的改动没被打回去（spec §8.6）。
4. **飞船仍然落在地表上、没有掉穿** —— 与改动前的 probe 输出对照最后几帧的飞船位置，量级应一致。若飞船位置一路往外飘或掉到很远，说明速度写回或外力接错了。

**probe 验不到的东西（要如实说明）**：`-game` 下没法按键，所以「按 W 会上升」这条只能由用户在 PIE 里确认。probe 只能证明接线通了、量纲对了。

> ⚠ **spec §8.7 提的那个替代做法行不通，不要试。** 它写的是「改为临时给 `ThrustAcceleration` 一个非零的默认方向」—— 但推力方向是 `GetActorForwardVector() * ThrustInput + GetActorUpVector() * VerticalInput` 算出来的，没有输入设备时 `ThrustInput` 与 `VerticalInput` **都是 0**，方向向量就是零向量；任何量级乘零还是零。所以 probe 里无论把 `ThrustAcceleration` 默认值调成多大，飞船都一动不动 —— 那不是接线错了。要走这条路得同时伪造输入，那就是往产品代码里塞测试脚手架，不做。

- [ ] **Step 8: 停下报告，等用户指示是否提交**

```bash
git add Source/StellarEra/Public/CosmosSimulationSubsystem.h Source/StellarEra/Private/CosmosSimulationSubsystem.cpp
git commit -m "feat(宇宙): 子系统每帧下来读飞船的推力旋钮，并把速度写回

ApplyControlledBodyForces 在 Tick 第 0 段跑（紧挨着 RefreshPlanetContactRadii，
两者都是推进之前把引擎侧状态喂给求解器）：走一遍 Bodies，只认 ASpaceshipPawn，
把 GetThrustAccelerationWorld() 除以 CentimetersPerAu × TimeScale² = 11.089
写进 ExternalAcceleration，把 GetDampingTime() 取倒数再除 TimeScale 写进
LinearDamping。

位置写回那一段顺手把速度也写回去（同一个 bSimulated 门），
ToWorldVelocityScaled = v × CentimetersPerAu × TimeScale = v × 3330 —— 这是
玩家感知的速度，不是 FNBodySimulator::ToWorldVelocity 那个实时值。

诊断加两句：能量漂移在有受控天体时加「此值不守恒」后缀（那是推力做功不是
积分误差），以及受控天体那一行（外力 + 阻尼），它的存在就是接线通了的证据。

Co-Authored-By: Claude Code <noreply@anthropic.com>" \
  Source/StellarEra/Public/CosmosSimulationSubsystem.h Source/StellarEra/Private/CosmosSimulationSubsystem.cpp
```

---

## 验收（probe 验不到的部分，要用户在 PIE 里确认）

spec §8.7 / §8.8 / §8.9 / §8.10。这四条 probe 做不到（`-game -nullrhi` 没有输入设备、也没有画面）：

1. 飞船能起飞、能悬停、能落地；HUD 的「速度」显示的是真实速度。
2. 按住 W 时高度在若干帧内单调上升，**净上升加速度约 0.5 g**（推力 1.51 g − 重力 1 g）。
3. **松开 W 后速度按 τ ≈ 10 毫秒衰减**（不是旧尺度那种 2 秒）。这条是阻尼真的接上了的判据 —— 只看「飞得起来」分不出「阻尼生效」和「阻尼被漏掉、只是推力大」。
4. 高速飞行时地形跟得上（不穿模）—— 这是 `LODRange` / `CollisionsRange` 那 4.5 公里是否够的判据。嫌贵就往回收，收的时候把 `SpaceshipPawn.cpp` 里那段推导一起改掉。

另外记住 spec §7 的已知后果，别把它们当 bug 查：

- 没有 `UCosmosOrbitComponent` 的关卡（例如 `NewMap` 那样的平地测试图）里飞船**动不了** —— 这是「飞船的移动就是它的 N 体」的直接推论。
- 不按 W 时会掉下来（阻尼作用在自由落体上 → 落地有终端速度），且**停不住在轨道上**，除非开巡航。
- 响应很硬：约 30 毫秒到极速。
