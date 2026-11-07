# 飞船刚体接触 — 实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 给 N 体求解器加真正的刚体姿态（朝向、角速度、惯性张量）与多接触点冲量求解，让飞船撞地时翻滚、侧倾、靠摩擦停住，而不是整条速度归零地冻在原地。

**Architecture:** 姿态权威下沉到 `FNBodySimulator` —— `FCosmosBodyState` 增加 `Orientation` / `AngularVelocity` / `InverseInertiaLocal`，`StepExact` 在积分器之后、接触之前积分朝向；`ResolveProbeContact` 从「取最深一个探针、整体平移」改成「收集全部穿透探针、顺序冲量 + 库仑摩擦、位置修正一次」。`ASpaceshipPawn` 退化成一个输入端口：鼠标给角速度冲量、A/D 给横滚角加速度，自身不再 `AddActorLocalRotation`；`UCosmosSimulationSubsystem` 负责单位换算与每帧一次的位置+朝向写回。

**Tech Stack:** Unreal Engine 5.8.3（Win64）、C++、UE Automation Test（`IMPLEMENT_SIMPLE_AUTOMATION_TEST`）、双精度求解器单位 AU / AU每年 / M☉

**Spec:** `docs/superpowers/specs/2026-11-05-ship-rigid-body-contact-design.md`

## Global Constraints

- UE 5.8.3，Windows / Win64，项目 5 个模块，**不新增模块、不新增第三方依赖**。
- 模块分层照 `docs/ARCHITECTURE.md`：`StellarEraCosmos → StellarEraPawns`、`StellarEraPawns → StellarEraPlanet`。本计划只改 Cosmos（求解器 + 子系统）与 Pawns（飞船），依赖方向不变。
- `*Tests.cpp` 只能放 `Src/Tests/`；测试名前缀 `StellarEra.Cosmos.*` / `StellarEra.Spaceship.*`。
- 含中文的 `.h` / `.cpp` 存 **UTF-8 with BOM**（MSVC 否则按 GBK 读）。新建文件必须先加 BOM。
- 求解器单位：位置 AU、时间模拟年、质量 M☉；世界层厘米/秒。换算只走 `CosmosUnits::` 与子系统里的 `TimeScale`，不在求解器里出现「秒」。
- 时间换算（本计划三处唯一依据）：`ω_sim = ω_real / TimeScale`；`α_sim = α_real / TimeScale²`；`k_sim = k_real / TimeScale`。`TimeScale = 0.00333` 模拟年/真实秒。
- 构建前**必须关闭编辑器**（Live Coding 锁 DLL）。构建与测试只能用 bundled dotnet 直调 UBT，不用 `Build.bat`。
- 提交时 pathspec 必须挂在 `git commit` 上（索引里有常驻暂存文件）。本 worktree 里用 `git add -A` + `git commit`，**不要**在命令里写裸 `Source`/`Content` 字样（worktree 的命令护栏会拒）。
- 不改 `bZeroVelocityOnContact` 的字段定义与球-球分支（行星间接触仍走默认光滑硬球语义），只是**不再给飞船置位**。

### 构建命令（每个任务都用这两个）

```bash
DOTNET="D:/Application/Epic Games/UE_5.8/Engine/Binaries/ThirdParty/DotNet/10.0/win-x64/dotnet.exe"
UBT="D:/Application/Epic Games/UE_5.8/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.dll"
PROJ="F:/workspace/ue/StellarEra/.claude/worktrees/ship-rigid-body-contact/StellarEra.uproject"

"$DOTNET" "$UBT" StellarEraEditor Win64 Development -Project="$PROJ" -WaitMutex -FromMsBuild
```

```bash
EDITOR="D:/Application/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe"
PROJ="F:/workspace/ue/StellarEra/.claude/worktrees/ship-rigid-body-contact/StellarEra.uproject"

"$EDITOR" "$PROJ" -ExecCmds="Automation RunTests StellarEra.Cosmos.RigidBody" \
  -TestExit="Automation Test Queue Empty" -unattended -nopause -nullrhi -nosplash -stdout \
  -abslog="F:/tmp/stellar_rigidbody.log"
```

跑完看日志尾部：`Automation Test Queue Empty` 且 `Result={Success}`；失败会打印 `Error: ...` 与测试名。日志路径每次换一个，避免读到旧结果。

### 新文件加 BOM

```bash
python -c "
import sys
p=sys.argv[1]
d=open(p,'rb').read()
if not d.startswith(b'\xef\xbb\xbf'):
    open(p,'wb').write(b'\xef\xbb\xbf'+d)
" <新文件路径>
```

---

## 文件结构

| 文件 | 责任 |
|---|---|
| `Source/StellarEraCosmos/Src/NBody/NBodySimulator.h` | 刚体状态字段、`ApplyInverseInertia`、两个惯量静态构造函数、`IntegrateRigidBodyOrientations` 声明 |
| `Source/StellarEraCosmos/Src/NBody/NBodySimulator.cpp` | 朝向积分、接触冲量求解、惯量公式 |
| `Source/StellarEraCosmos/Src/CosmosSimulationSubsystem.cpp` | 注册刚体、探针按朝向变换、写惯量与角加速度、朝向写回 |
| `Source/StellarEraPawns/Src/Spaceship/SpaceshipPawn.h` | 角加速度端口、待发冲量、`RollAngularAcceleration` |
| `Source/StellarEraPawns/Src/Spaceship/SpaceshipPawn.cpp` | `Tick` 去掉运动学姿态、`Look` 改为累加冲量、纯函数换算 |
| `Source/StellarEraCosmos/Src/Tests/RigidBodyTests.cpp` | 朝向积分的用例（新建） |
| `Source/StellarEraCosmos/Src/Tests/RigidBodyContactTests.cpp` | 接触求解的用例（新建） |
| `Source/StellarEraPawns/Src/Tests/SpaceshipAngularInputTests.cpp` | 角加速度换算的用例（新建） |

---

## Task 1: 求解器刚体状态与姿态积分

**Files:**
- Modify: `Source/StellarEraCosmos/Src/NBody/NBodySimulator.h`（`FCosmosBodyState`，在 `LinearDamping` 之后、`bZeroVelocityOnContact` 之前插入字段；在 `FCosmosBodyState` 内加方法）
- Modify: `Source/StellarEraCosmos/Src/NBody/NBodySimulator.cpp`（新增 `IntegrateRigidBodyOrientations`、两个静态函数；`StepExact` 里调用）
- Test: `Source/StellarEraCosmos/Src/Tests/RigidBodyTests.cpp`（新建）

**Interfaces:**
- Produces:
  - `FCosmosBodyState::FQuat4d Orientation`
  - `FCosmosBodyState::FVector3d AngularVelocity`
  - `FCosmosBodyState::FVector3d InverseInertiaLocal`
  - `FCosmosBodyState::FVector3d ExternalAngularAcceleration`
  - `FCosmosBodyState::double AngularDamping`
  - `FCosmosBodyState::bool bRigidBody`
  - `FVector3d FCosmosBodyState::ApplyInverseInertia(const FVector3d& Torque) const`
  - `static FVector3d FCosmosBodyState::InverseInertiaFromBox(double Mass, const FVector3d& HalfExtent)`
  - `static FVector3d FCosmosBodyState::InverseInertiaFromSphere(double Mass, double Radius)`
  - `void FNBodySimulator::IntegrateRigidBodyOrientations(double TimeStep)`（private）

- [ ] **Step 1: 写失败的测试**

新建 `Source/StellarEraCosmos/Src/Tests/RigidBodyTests.cpp`：

```cpp
// Copyright Voxel Plugin SAS. All Rights Reserved.
//
// 求解器刚体姿态的用例：角加速度积分、四元数归一化、角阻尼、非刚体零影响。
//
// 不建 Actor、不碰世界：直接喂 FCosmosBodyState 给求解器，靠 StepExact 驱动。
// 关闭引力截断是为了让「无引力」这件事显式 —— 单天体本来就没有引力项。

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "NBodySimulator.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	/** 一个刚体、无引力、Velocity Verlet 的求解器。返回时 Bodies[0] 即该刚体。 */
	FNBodySimulator MakeSingleRigidBody()
	{
		FNBodySimulator Simulator;
		Simulator.SetUnitSystem(ECosmosUnitSystem::Game);
		Simulator.SetIntegrator(ECosmosIntegrator::VelocityVerlet);
		Simulator.SetGravityCutoff(1.0e-9);

		FCosmosBodyState Body;
		Body.Mass = 1.0;
		Body.bRigidBody = true;
		Body.InverseInertiaLocal = FVector3d(1.0, 1.0, 1.0);
		Simulator.AddBody(Body);

		return Simulator;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraCosmosRigidBodyOrientationTest,
	"StellarEra.Cosmos.RigidBody.Orientation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraCosmosRigidBodyOrientationTest::RunTest(const FString& Parameters)
{
	// ── 1. ω = ∫α dt ──
	{
		FNBodySimulator Simulator = MakeSingleRigidBody();
		Simulator.GetBodyMutable(0).ExternalAngularAcceleration = FVector3d(0.0, 0.0, 2.0);

		const double Dt = 1.0e-3;
		Simulator.StepExact(Dt);

		TestEqual(TEXT("ω_z = α_z·dt"),
			Simulator.GetBody(0).AngularVelocity.Z, 2.0 * Dt, 1.0e-12);
		TestEqual(TEXT("ω_x 仍为零"),
			Simulator.GetBody(0).AngularVelocity.X, 0.0, 1.0e-15);
	}

	// ── 2. 四元数保持单位长度 ──
	{
		FNBodySimulator Simulator = MakeSingleRigidBody();
		Simulator.GetBodyMutable(0).AngularVelocity = FVector3d(0.3, -1.7, 2.9);

		for (int32 Step = 0; Step < 1000; ++Step)
		{
			Simulator.StepExact(1.0e-4);
		}

		const FQuat4d& Q = Simulator.GetBody(0).Orientation;
		const double Norm = FMath::Sqrt(Q.X * Q.X + Q.Y * Q.Y + Q.Z * Q.Z + Q.W * Q.W);
		TestEqual(TEXT("|q| = 1"), Norm, 1.0, 1.0e-12);
		TestTrue(TEXT("确实转了（不是恒等）"), FMath::Abs(Q.Z) > 1.0e-6);
	}

	// ── 3. 角阻尼：ω ← ω·(1 − k·dt) ──
	{
		FNBodySimulator Simulator = MakeSingleRigidBody();
		FCosmosBodyState& State = Simulator.GetBodyMutable(0);
		State.AngularVelocity = FVector3d(1.0, 0.0, 0.0);
		State.AngularDamping = 100.0;

		const double Dt = 1.0e-3;
		Simulator.StepExact(Dt);

		TestEqual(TEXT("ω 按 (1 − k·dt) 衰减"),
			Simulator.GetBody(0).AngularVelocity.X, 1.0 * (1.0 - 100.0 * Dt), 1.0e-12);
	}

	// ── 3b. k·dt ≥ 1 时归零，不反号 ──
	{
		FNBodySimulator Simulator = MakeSingleRigidBody();
		FCosmosBodyState& State = Simulator.GetBodyMutable(0);
		State.AngularVelocity = FVector3d(1.0, 0.0, 0.0);
		State.AngularDamping = 1.0e6;

		Simulator.StepExact(1.0e-3);

		TestTrue(TEXT("严重过阻尼时不反向"), Simulator.GetBody(0).AngularVelocity.IsNearlyZero());
	}

	// ── 4. 非刚体完全不受影响 ──
	{
		FNBodySimulator Simulator = MakeSingleRigidBody();
		FCosmosBodyState& State = Simulator.GetBodyMutable(0);
		State.bRigidBody = false;
		State.AngularVelocity = FVector3d(5.0, 5.0, 5.0);
		State.ExternalAngularAcceleration = FVector3d(5.0, 5.0, 5.0);

		Simulator.StepExact(1.0e-3);

		const FCosmosBodyState& After = Simulator.GetBody(0);
		TestTrue(TEXT("角速度不变"), After.AngularVelocity.Equals(FVector3d(5.0, 5.0, 5.0)));
		TestTrue(TEXT("朝向不变"), After.Orientation.Equals(FQuat4d::Identity));
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
```

- [ ] **Step 2: 跑测试确认它失败（编译不过就是红）**

先加 BOM（见 Global Constraints 的 python 片段），再构建。

Run: 上面的构建命令
Expected: 编译失败，错误含 `'bRigidBody': is not a member of 'FCosmosBodyState'`（或 `'Orientation'` / `'AngularVelocity'`）。

- [ ] **Step 3: 加字段与方法**

在 `NBodySimulator.h` 的 `FCosmosBodyState` 里，`double LinearDamping = 0.0;`（第 145 行）之后、`bZeroVelocityOnContact` 的注释之前插入：

```cpp
	/**
	 * 是否是**刚体**：为 true 时求解器积分姿态、接触冲量产生力矩。
	 * 为 false（行星、恒星）时朝向归关卡摆位，求解器一个字节都不写。
	 */
	bool bRigidBody = false;

	/**
	 * 世界系朝向。只对刚体有意义，非刚体恒为单位四元数。
	 *
	 * 求解器是姿态的**唯一权威**：Actor 的 Rotation 每帧由子系统从这里写回，
	 * 谁也别在别处 AddActorLocalRotation —— 两处都写就是互相抹。
	 */
	FQuat4d Orientation = FQuat4d::Identity;

	/** 世界系角速度（弧度 / 模拟年）。与位置速度同一个时间单位。 */
	FVector3d AngularVelocity = FVector3d::ZeroVector;

	/**
	 * 机体系惯性张量的**逆**，对角元。零向量 = 任何力矩都不产生角加速度
	 * （点质量、或探针退化成一条线）。
	 *
	 * 用逆而不是 I 本身：接触冲量每一步都要乘它，且 I 退化时逆就是零，
	 * 天然把「没有惯量」变成「转不动」，不需要额外的除零保护。
	 */
	FVector3d InverseInertiaLocal = FVector3d::ZeroVector;

	/**
	 * 外部力矩产生的角加速度（rad/模拟年²）。与 ExternalAcceleration 一样由
	 * 外部每帧写入，求解器只加不解释。单位推导见 spec §3.3。
	 */
	FVector3d ExternalAngularAcceleration = FVector3d::ZeroVector;

	/**
	 * 角阻尼（1/模拟年）：ω ← ω·(1 − k·dt)，逐子步乘。
	 *
	 * 用乘法而不是减法，是为了 k·dt ≥ 1 时能干净地归零而不是反号。
	 */
	double AngularDamping = 0.0;

	/** I⁻¹·τ：世界系力矩 → 世界系角速度增量。非刚体返回零。 */
	FVector3d ApplyInverseInertia(const FVector3d& Torque) const
	{
		if (!bRigidBody)
		{
			return FVector3d::ZeroVector;
		}

		// I⁻¹ 是机体系的对角阵，所以「转进机体 → 乘 → 转回世界」。
		const FVector3d Local = Orientation.UnrotateVector(Torque) * InverseInertiaLocal;
		return Orientation.RotateVector(Local);
	}

	/**
	 * 长方体的逆惯量。`HalfExtent` 是**半边长**，公式按边长 2·HalfExtent：
	 *     Ixx = m/12·(h_y² + h_z²)
	 * 任一边长或质量非正时返回零（转不动），不返回 NaN。
	 */
	static FVector3d InverseInertiaFromBox(double Mass, const FVector3d& HalfExtent);

	/** 均匀球：I = 2/5·m·r²。 */
	static FVector3d InverseInertiaFromSphere(double Mass, double Radius);
```

在 `FNBodySimulator` 的 `ResolveProbeContact` 声明旁边（private 区，与其它 `Resolve*` 同处）加：

```cpp
	/**
	 * 刚体姿态积分：ω += α·dt，ω 乘角阻尼，q ← exp(½·ω·dt)⊗q。
	 *
	 * 放在 StepExact 里积分器之后、ResolveContacts 之前 —— 三种积分器共用一处，
	 * 预测轨迹（同样调 StepExact）自动拿到同样的姿态行为。
	 */
	void IntegrateRigidBodyOrientations(double TimeStep);
```

- [ ] **Step 4: 实现**

`NBodySimulator.cpp` 文件末尾追加：

```cpp
FVector3d FCosmosBodyState::InverseInertiaFromBox(double Mass, const FVector3d& HalfExtent)
{
	const FVector3d Edge = HalfExtent * 2.0;
	if (Mass <= 0.0 || Edge.X <= 0.0 || Edge.Y <= 0.0 || Edge.Z <= 0.0)
	{
		return FVector3d::ZeroVector;
	}

	const double TwelveOverMass = 12.0 / Mass;
	return FVector3d(
		TwelveOverMass / (Edge.Y * Edge.Y + Edge.Z * Edge.Z),
		TwelveOverMass / (Edge.X * Edge.X + Edge.Z * Edge.Z),
		TwelveOverMass / (Edge.X * Edge.X + Edge.Y * Edge.Y));
}

FVector3d FCosmosBodyState::InverseInertiaFromSphere(double Mass, double Radius)
{
	const double Inertia = 0.4 * Mass * Radius * Radius;
	return Inertia > 0.0 ? FVector3d(1.0 / Inertia) : FVector3d::ZeroVector;
}

void FNBodySimulator::IntegrateRigidBodyOrientations(double TimeStep)
{
	if (TimeStep <= 0.0)
	{
		return;
	}

	for (FCosmosBodyState& Body : Bodies)
	{
		if (!Body.bRigidBody)
		{
			continue;
		}

		// ① 角速度：显式欧拉。线速度那边用 Velocity Verlet 是因为有引力这个平滑
		//    加速度场；角加速度只有外力矩与接触冲量，且一帧最多 200 个子步，
		//    一阶足够。
		Body.AngularVelocity += Body.ExternalAngularAcceleration * TimeStep;

		// ② 阻尼：乘法形式，k·dt ≥ 1 时直接归零。
		if (Body.AngularDamping > 0.0)
		{
			const double Decay = 1.0 - Body.AngularDamping * TimeStep;
			Body.AngularVelocity *= Decay > 0.0 ? Decay : 0.0;
		}

		// ③ 朝向：dq = exp(½·ω·dt) ⊗ q。世界系角速度 → 左乘。
		//    归一化每步都做：不归一的话几千步之后 Actor 的变换矩阵会慢慢
		//    变成非正交，表现为缓慢缩放。
		const double Speed = Body.AngularVelocity.Size();
		if (Speed > 0.0)
		{
			const FVector3d Axis = Body.AngularVelocity / Speed;
			const FQuat4d Delta(Axis, Speed * TimeStep);
			Body.Orientation = (Delta * Body.Orientation).GetNormalized();
		}
	}
}
```

在 `NBodySimulator.cpp` 的 `StepExact` 里，`switch` 之后、`ResolveContacts();` 之前插入（就在现有那段「约束放在积分之后」注释的上方）：

```cpp
	// 姿态先于接触积分：接触冲量要读的是**本步**的角速度与朝向，
	// 用它算力臂，再把力矩写回同一个 ω。
	IntegrateRigidBodyOrientations(TimeStep);
```

- [ ] **Step 5: 构建 + 跑测试**

Run: 构建命令，然后测试命令。
Expected: 构建通过；`StellarEra.Cosmos.RigidBody.Orientation` 全部断言通过，日志尾部 `Result={Success}`。

- [ ] **Step 6: 提交**

```bash
git add -A
git commit -m "feat(求解器): 刚体姿态字段与朝向积分" \
  -m "Co-Authored-By: Claude Code <noreply@anthropic.com>"
```

---

## Task 2: 多接触点冲量求解

**Files:**
- Modify: `Source/StellarEraCosmos/Src/NBody/NBodySimulator.cpp`（`ResolveProbeContact` 全量重写 + 文件里的匿名命名空间辅助函数）
- Test: `Source/StellarEraCosmos/Src/Tests/RigidBodyContactTests.cpp`（新建）

**Interfaces:**
- Consumes: Task 1 的 `bRigidBody`、`AngularVelocity`、`ApplyInverseInertia`、`InverseInertiaFromBox`、`InverseInertiaFromSphere`、`IntegrateRigidBodyOrientations`。
- Produces: 无新公开符号。`ResolveProbeContact` 的签名不变（`void(int32 ProbeIndex, int32 SphereIndex)`），行为从「最深一个 + 整体平移」变成「全部接触点 + 冲量」。

- [ ] **Step 1: 写失败的测试**

新建 `Source/StellarEraCosmos/Src/Tests/RigidBodyContactTests.cpp`：

```cpp
// Copyright Voxel Plugin SAS. All Rights Reserved.
//
// 探针接触求解的用例：力矩、摩擦、多接触点不发散、靠外力静止在球面上。
//
// 场景统一是「静止大球 + 一个带探针的小刚体」，关掉引力 —— 接触与引力混在一起
// 会分不清是哪一项把物体推开的。球质量取 1e6 让它的反冲可忽略，断言就能用解析值。

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "NBodySimulator.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	/** 球在原点、半径 SphereRadius；刚体居中放在 BodyCenter，探针为本地坐标。 */
	FNBodySimulator MakeProbeScene(
		double SphereRadius,
		const FVector3d& BodyCenter,
		const TArray<FVector3d>& Probes,
		double BodyMass = 1.0)
	{
		FNBodySimulator Simulator;
		Simulator.SetUnitSystem(ECosmosUnitSystem::Game);
		Simulator.SetIntegrator(ECosmosIntegrator::VelocityVerlet);
		Simulator.SetGravityCutoff(1.0e-9);

		FCosmosBodyState Sphere;
		Sphere.Mass = 1.0e6;
		Sphere.Position = FVector3d::ZeroVector;
		Sphere.Radius = SphereRadius;
		Simulator.AddBody(Sphere);

		FCosmosBodyState Body;
		Body.Mass = BodyMass;
		Body.Position = BodyCenter;
		Body.bRigidBody = true;
		Body.InverseInertiaLocal = FVector3d(1.0, 1.0, 1.0);
		Body.SetContactProbes(Probes);
		Body.Radius = 0.0;
		Simulator.AddBody(Body);

		return Simulator;
	}

	/** 最深的穿透量（<= 0 表示没接触）。探针按刚体当前位置算。 */
	double MaxPenetration(const FNBodySimulator& Simulator)
	{
		const FCosmosBodyState& Body = Simulator.GetBody(1);
		const FCosmosBodyState& Sphere = Simulator.GetBody(0);

		double Max = 0.0;
		for (const FVector3d& Probe : Body.ContactProbes)
		{
			const double Distance = (Body.Position + Probe - Sphere.Position).Size();
			Max = FMath::Max(Max, Sphere.Radius - Distance);
		}
		return Max;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraCosmosRigidBodyContactTest,
	"StellarEra.Cosmos.RigidBody.Contact",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraCosmosRigidBodyContactTest::RunTest(const FString& Parameters)
{
	// ── 1. 惯性张量公式 ──
	{
		// 质量 12、边长 (4, 8, 12)：Ixx = 12/12·(8² + 12²) = 208。
		const FVector3d Inv = FCosmosBodyState::InverseInertiaFromBox(12.0, FVector3d(2.0, 4.0, 6.0));
		TestEqual(TEXT("1/Ixx"), Inv.X, 1.0 / 208.0, 1.0e-15);
		TestEqual(TEXT("1/Iyy"), Inv.Y, 1.0 / (12.0 / 12.0 * (16.0 + 144.0)), 1.0e-15);
		TestEqual(TEXT("1/Izz"), Inv.Z, 1.0 / (12.0 / 12.0 * (16.0 + 64.0)), 1.0e-15);
		TestTrue(TEXT("退化（零质量）→ 零"),
			FCosmosBodyState::InverseInertiaFromBox(0.0, FVector3d(1.0)).IsZero());

		// 球：I = 2/5·m·r² = 0.4·5·4 = 8。
		const FVector3d InvSphere = FCosmosBodyState::InverseInertiaFromSphere(5.0, 2.0);
		TestEqual(TEXT("球 1/I"), InvSphere.X, 0.125, 1.0e-15);
	}

	// ── 2. 偏心探针产生力矩：r × n 非零 → 角速度沿 +X、−Y 方向 ──
	{
		// 球半径 10、刚体中心 (0,0,4)、探针 (5,5,0)。
		// 探针世界位置 (5,5,4)，距球心 8.124，穿透 1.876；
		// 力臂 r = (5,5,0)，r × n = (2.462, −2.462, 0)。
		FNBodySimulator Simulator = MakeProbeScene(
			10.0, FVector3d(0.0, 0.0, 4.0), {FVector3d(5.0, 5.0, 0.0)});
		Simulator.GetBodyMutable(1).Velocity = FVector3d(0.0, 0.0, -1.0);

		Simulator.StepExact(1.0e-5);

		const FVector3d Omega = Simulator.GetBody(1).AngularVelocity;
		TestTrue(TEXT("ω_x > 0"), Omega.X > 0.05);
		TestTrue(TEXT("ω_y < 0"), Omega.Y < -0.05);
		TestEqual(TEXT("ω_z 为零"), Omega.Z, 0.0, 1.0e-12);
	}

	// ── 3. 正中探针不产生力矩 ──
	{
		FNBodySimulator Simulator = MakeProbeScene(
			10.0, FVector3d(0.0, 0.0, 4.0), {FVector3d(0.0, 0.0, 1.0)});
		Simulator.GetBodyMutable(1).Velocity = FVector3d(0.0, 0.0, -1.0);

		Simulator.StepExact(1.0e-5);

		TestTrue(TEXT("力臂与法线共线 → ω 为零"),
			Simulator.GetBody(1).AngularVelocity.IsNearlyZero(1.0e-12));
	}

	// ── 4. 摩擦削掉切向速度，并把它变成角速度 ──
	{
		FNBodySimulator Simulator = MakeProbeScene(
			10.0, FVector3d(0.0, 0.0, 4.0), {FVector3d(0.0, 0.0, 1.0)});
		Simulator.GetBodyMutable(1).ContactFriction = 0.6;
		Simulator.GetBodyMutable(1).Velocity = FVector3d(1.0, 0.0, -1.0);

		Simulator.StepExact(1.0e-5);

		const FCosmosBodyState& Body = Simulator.GetBody(1);
		TestTrue(TEXT("切向速度被摩擦削掉"), FMath::Abs(Body.Velocity.X) < 0.99);
		TestEqual(TEXT("法向速度归零"), Body.Velocity.Z, 0.0, 1.0e-6);
		TestTrue(TEXT("摩擦产生角速度"), FMath::Abs(Body.AngularVelocity.Y) > 0.01);
	}

	// ── 5. 四个对称探针：收敛到「四个都落在球面上」，不越推越远 ──
	{
		FNBodySimulator Simulator = MakeProbeScene(10.0, FVector3d(0.0, 0.0, 4.0),
			{FVector3d(1.0, 0.0, 0.0), FVector3d(-1.0, 0.0, 0.0),
			 FVector3d(0.0, 1.0, 0.0), FVector3d(0.0, -1.0, 0.0)});

		for (int32 Step = 0; Step < 200; ++Step)
		{
			Simulator.StepExact(1.0e-5);
		}

		// 对称解：球心到每个探针都是 10，即 sqrt(1 + z²) = 10 → z = 9.9499。
		// 只沿一个方向猛推的话这里会冲到 4 倍穿透，z 明显大于该值。
		const double Z = Simulator.GetBody(1).Position.Z;
		TestEqual(TEXT("稳定在 sqrt(100−1)"), Z, 9.9499, 0.01);
		TestTrue(TEXT("穿透收敛掉"), MaxPenetration(Simulator) < 1.0e-3);
	}

	// ── 6. 持续外力下沉 → 静止在球面上，不抖不陷 ──
	{
		FNBodySimulator Simulator = MakeProbeScene(
			10.0, FVector3d(0.0, 0.0, 4.0), {FVector3d(0.0, 0.0, 1.0)});

		for (int32 Step = 0; Step < 200; ++Step)
		{
			Simulator.GetBodyMutable(1).ExternalAcceleration = FVector3d(0.0, 0.0, -1.0);
			Simulator.StepExact(1.0e-5);
		}

		const FCosmosBodyState& Body = Simulator.GetBody(1);
		TestEqual(TEXT("停在球面上（z = 9）"), Body.Position.Z, 9.0, 0.01);
		TestTrue(TEXT("没有残余穿透"), MaxPenetration(Simulator) < 1.0e-3);
		TestTrue(TEXT("没有攒出角速度"), Body.AngularVelocity.IsNearlyZero(1.0e-9));
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
```

- [ ] **Step 2: 跑测试确认它失败**

先加 BOM。构建。

Expected: 编译失败 —— `'ContactFriction': is not a member of 'FCosmosBodyState'`。

- [ ] **Step 3: 加两个接触旋钮到 `FCosmosBodyState`**

在 `NBodySimulator.h` 里 `double AngularDamping = 0.0;` 之后插入：

```cpp
	/**
	 * 库仑摩擦系数 μ：切向冲量被夹在 [−μ·λ_n, +μ·λ_n] 内。默认 0.6 ≈ 橡胶/金属
	 * 之间的常见值，够让飞船落地后不无限滑走，又不至于像刹车片一样粘住。
	 */
	double ContactFriction = 0.6;

	/**
	 * 恢复系数 e：0 = 完全非弹性（默认，撞上不弹），1 = 完全弹性。
	 * 飞船落地不该弹起来，所以默认 0；参数留着是为了以后的弹性质点。
	 */
	double ContactRestitution = 0.0;
```

- [ ] **Step 4: 重写 `ResolveProbeContact`**

在 `NBodySimulator.cpp` 的匿名命名空间区（`ComputeContactShares` 定义之后、`void FNBodySimulator::ResolveContacts()` 之前）插入：

```cpp
namespace
{
	/** 单个接触点（求解器单位 AU / AU每年）。 */
	struct FProbeContactPoint
	{
		/** 相对探针天体中心的位置（AU）。同时是球体那边的力臂取负。 */
		FVector3d Offset = FVector3d::ZeroVector;

		/** 球心 → 探针的单位方向。 */
		FVector3d Normal = FVector3d::ZeroVector;

		/** 穿透深度（> 0）。 */
		double Depth = 0.0;

		/** 迭代开始前的法向相对速度，恢复系数用它判「接近」而不是迭代中途的值。 */
		double InitialNormalVelocity = 0.0;

		/** 本接触点已累计的法向冲量，摩擦锥的上限。 */
		double AccumulatedNormalImpulse = 0.0;
	};

	/** 每对天体最多解几个接触点。探针通常 6~8 个，16 是留余量。 */
	constexpr int32 MaxContactPointsPerPair = 16;

	/** 顺序冲量迭代次数。4 次在「一对天体、≤16 点」的规模下已收敛到 1e-3。 */
	constexpr int32 ContactIterations = 4;

	double InverseMassOf(const FCosmosBodyState& Body)
	{
		return Body.Mass > 0.0 ? 1.0 / Body.Mass : 0.0;
	}

	/** 接触点速度：v + ω × r。 */
	FVector3d PointVelocity(const FCosmosBodyState& Body, const FVector3d& Offset)
	{
		return Body.Velocity + FVector3d::CrossProduct(Body.AngularVelocity, Offset);
	}

	/**
	 * 沿 Direction 的接触有效质量：1 / (1/m₁ + 1/m₂ + 角项)。
	 *
	 * 角项对非刚体恒为零（ApplyInverseInertia 自己判 bRigidBody），所以行星之间
	 * 仍然退化成经典的两体冲量，与改动前一样。
	 */
	double EffectiveMassAlong(
		const FCosmosBodyState& ProbeBody, const FCosmosBodyState& SphereBody,
		const FVector3d& ProbeOffset, const FVector3d& Direction)
	{
		double InverseMass =
			InverseMassOf(ProbeBody) + InverseMassOf(SphereBody);

		// 探针侧：r × d → I⁻¹(r × d) × r。
		const FVector3d ProbeArm = FVector3d::CrossProduct(ProbeOffset, Direction);
		InverseMass += FVector3d::DotProduct(Direction, FVector3d::CrossProduct(
			ProbeBody.ApplyInverseInertia(ProbeArm), ProbeOffset));

		// 球侧：力臂是 −ProbeOffset（接触点在球心的反方向）。
		const FVector3d SphereOffset = -ProbeOffset;
		const FVector3d SphereArm = FVector3d::CrossProduct(SphereOffset, Direction);
		InverseMass += FVector3d::DotProduct(Direction, FVector3d::CrossProduct(
			SphereBody.ApplyInverseInertia(SphereArm), SphereOffset));

		return InverseMass > 0.0 ? 1.0 / InverseMass : 0.0;
	}

	/** 把冲量施加到两侧：探针侧 +J，球侧 −J。 */
	void ApplyContactImpulse(
		FCosmosBodyState& ProbeBody, FCosmosBodyState& SphereBody,
		const FVector3d& ProbeOffset, const FVector3d& Impulse)
	{
		if (ProbeBody.Mass > 0.0)
		{
			ProbeBody.Velocity += Impulse / ProbeBody.Mass;
		}
		ProbeBody.AngularVelocity += ProbeBody.ApplyInverseInertia(
			FVector3d::CrossProduct(ProbeOffset, Impulse));

		if (SphereBody.Mass > 0.0)
		{
			SphereBody.Velocity -= Impulse / SphereBody.Mass;
		}
		SphereBody.AngularVelocity += SphereBody.ApplyInverseInertia(
			FVector3d::CrossProduct(-ProbeOffset, -Impulse));
	}
}
```

然后把 `ResolveProbeContact` 的函数体整体替换为：

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
	// 抬高地形时逐探针半径会**大于**球半径，所以排除半径取两者的较大者。
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

	// ① 收集所有穿透的探针。不再是「取最深的一个」：机身侧倾着地时是**两三个**
	//    探针同时压在地面上，只解一个的话接触力矩为零，船就永远不会翻过去。
	TArray<FProbeContactPoint, TInlineAllocator<MaxContactPointsPerPair>> Contacts;

	for (int32 Index = 0; Index < ProbeBody.ContactProbes.Num(); ++Index)
	{
		const FVector3d& Probe = ProbeBody.ContactProbes[Index];
		const double ProbeSurfaceRadius =
			bUsePerProbe ? ProbeBody.ContactProbeSurfaceRadii[Index] : SphereRadius;

		const FVector3d Delta = ProbeBody.Position + Probe - SphereBody.Position;
		const double Distance = Delta.Size();
		const double Penetration = ProbeSurfaceRadius - Distance;

		// 探针正好落在球心时法线无定义：既算不出力矩，也没有方向可推，跳过。
		if (Penetration <= 0.0 || Distance <= 1.0e-12)
		{
			continue;
		}

		FProbeContactPoint& Contact = Contacts.AddDefaulted_GetRef();
		Contact.Offset = Probe;
		Contact.Normal = Delta / Distance;
		Contact.Depth = Penetration;
	}

	if (Contacts.Num() == 0)
	{
		return;
	}

	// 超过上限时只留最深的那些 —— 深的是真正承重的，浅的被同一个刚体挡住。
	if (Contacts.Num() > MaxContactPointsPerPair)
	{
		Contacts.Sort([](const FProbeContactPoint& A, const FProbeContactPoint& B)
		{
			return A.Depth > B.Depth;
		});
		Contacts.SetNum(MaxContactPointsPerPair);
	}

	const double Restitution = FMath::Clamp(ProbeBody.ContactRestitution, 0.0, 1.0);
	const double Friction = FMath::Max(ProbeBody.ContactFriction, 0.0);

	// 迭代前的法向速度：恢复系数只对「这一步开始时正在接近」的接触生效。
	for (FProbeContactPoint& Contact : Contacts)
	{
		const FVector3d Relative =
			PointVelocity(ProbeBody, Contact.Offset)
			- PointVelocity(SphereBody, -Contact.Offset);
		Contact.InitialNormalVelocity = FVector3d::DotProduct(Relative, Contact.Normal);
	}

	// ② 顺序冲量：逐点、逐轮把法向相对速度压到目标值，再用库仑摩擦削切向。
	for (int32 Iteration = 0; Iteration < ContactIterations; ++Iteration)
	{
		for (FProbeContactPoint& Contact : Contacts)
		{
			const FVector3d RelativeBefore =
				PointVelocity(ProbeBody, Contact.Offset)
				- PointVelocity(SphereBody, -Contact.Offset);

			const double NormalVelocity =
				FVector3d::DotProduct(RelativeBefore, Contact.Normal);
			const double TargetNormalVelocity = -Restitution * Contact.InitialNormalVelocity;
			const double NormalMass = EffectiveMassAlong(
				ProbeBody, SphereBody, Contact.Offset, Contact.Normal);

			// 法向冲量只能推不能拉（DeltaLambda > 0），物体不会被「粘」回去。
			if (NormalMass > 0.0)
			{
				const double DeltaLambda = (TargetNormalVelocity - NormalVelocity) * NormalMass;
				if (DeltaLambda > 0.0)
				{
					Contact.AccumulatedNormalImpulse += DeltaLambda;
					ApplyContactImpulse(ProbeBody, SphereBody, Contact.Offset,
						Contact.Normal * DeltaLambda);
				}
			}

			// 摩擦用**施加法向冲量之后**的速度：法向先消掉，切向才是真正要削的。
			const FVector3d Relative =
				PointVelocity(ProbeBody, Contact.Offset)
				- PointVelocity(SphereBody, -Contact.Offset);
			const double NormalNow = FVector3d::DotProduct(Relative, Contact.Normal);
			const FVector3d TangentVelocity = Relative - Contact.Normal * NormalNow;
			const double TangentSpeed = TangentVelocity.Size();

			if (Friction > 0.0 && TangentSpeed > 1.0e-12)
			{
				const FVector3d Tangent = TangentVelocity / TangentSpeed;
				const double TangentMass = EffectiveMassAlong(
					ProbeBody, SphereBody, Contact.Offset, Tangent);

				if (TangentMass > 0.0)
				{
					const double MaxFriction = Friction * Contact.AccumulatedNormalImpulse;
					const double DeltaLambda = FMath::Clamp(
						-TangentSpeed * TangentMass, -MaxFriction, MaxFriction);

					if (DeltaLambda != 0.0)
					{
						ApplyContactImpulse(ProbeBody, SphereBody, Contact.Offset,
							Tangent * DeltaLambda);
					}
				}
			}
		}
	}

	// ③ 位置修正：只沿**最深**那个接触点推一次，推掉穿透的那部分。
	//
	//    逐点各推一次会和速度冲量叠加成「越推越远」；只推最深的一个，其余的点
	//    在下一个子步里自然成为最深的那个。位置修正不进迭代，也就不需要保存
	//    除穿透量以外的状态。
	const FProbeContactPoint* Deepest = &Contacts[0];
	for (const FProbeContactPoint& Contact : Contacts)
	{
		if (Contact.Depth > Deepest->Depth)
		{
			Deepest = &Contact;
		}
	}

	double ShareProbe = 0.0;
	double ShareSphere = 0.0;
	ComputeContactShares(ProbeBody.Mass, SphereBody.Mass, ShareProbe, ShareSphere);

	ProbeBody.Position += Deepest->Normal * (Deepest->Depth * ShareProbe);
	SphereBody.Position -= Deepest->Normal * (Deepest->Depth * ShareSphere);
}
```

注意：`bZeroVelocityOnContact` 的两支分支在这里被**删掉**了。这个开关的语义是「相对速度整体归零」，与冲量求解互斥；飞船现在不再置位（Task 4），而带探针的天体只有飞船。球-球分支里的同名特例保留不动。

- [ ] **Step 5: 构建 + 跑测试**

Run: 构建命令，然后测试命令（前缀 `StellarEra.Cosmos.RigidBody` 会同时跑到 Task 1 的用例）。
Expected: 通过。若「四个对称探针」那条差一点，先把迭代次数 `ContactIterations` 调到 8 再判断 —— 那是收敛问题，不是公式问题。

- [ ] **Step 6: 提交**

```bash
git add -A
git commit -m "feat(求解器): 探针接触改为多接触点冲量 + 库仑摩擦" \
  -m "Co-Authored-By: Claude Code <noreply@anthropic.com>"
```

---

## Task 3: 飞船角加速度输入端口

**Files:**
- Modify: `Source/StellarEraPawns/Src/Spaceship/SpaceshipPawn.h`
- Modify: `Source/StellarEraPawns/Src/Spaceship/SpaceshipPawn.cpp`
- Test: `Source/StellarEraPawns/Src/Tests/SpaceshipAngularInputTests.cpp`（新建）

**Interfaces:**
- Consumes: 无（纯 Pawns 内部）。
- Produces:
  - `FVector ASpaceshipPawn::GetAngularAccelerationWorldRadPerS2(float DeltaSeconds)` —— 有副作用：读走并清零待发冲量
  - `void ASpaceshipPawn::SetCosmosAngularVelocity(const FVector& InWorldRadPerSecond)`
  - `float ASpaceshipPawn::GetAngularDampingPerSecond() const`
  - `static FVector ASpaceshipPawn::ComputeAngularAccelerationWorldRadPerS2(const FQuat& BodyRotation, const FVector& PendingImpulseLocalDegPerSec, const FVector& ActualAngularVelocityWorld, float RollInput, float RollAngularAccelerationDegPerS2, float MaxRollRateDegPerSec, float DeltaSeconds)`
  - 新属性 `float RollAngularAcceleration = 1500.f`

- [ ] **Step 1: 写失败的测试**

新建 `Source/StellarEraPawns/Src/Tests/SpaceshipAngularInputTests.cpp`：

```cpp
// Copyright Voxel Plugin SAS. All Rights Reserved.
//
// 飞船角加速度换算的用例。被测函数是纯函数，不建 Actor、不依赖关卡。
//
// 断言三件事：
//   1. 鼠标冲量（机体本地，度/秒）→ 世界系角加速度（rad/s²），除以 dt；
//   2. 横滚是持续角加速度，到 MaxRollRate 时衰减到 0（与旧版「到顶就停」一致）；
//   3. dt 为 0 时返回零向量，不产生 inf。

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Spaceship/SpaceshipPawn.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraShipAngularInputTest,
	"StellarEra.Spaceship.AngularInput",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraShipAngularInputTest::RunTest(const FString& Parameters)
{
	const FQuat Identity = FQuat::Identity;
	const FVector NoAngularVelocity = FVector::ZeroVector;
	const float Dt = 0.02f;

	// ── 1. 鼠标冲量 → α = 冲量 / dt ──
	{
		const FVector Alpha = ASpaceshipPawn::ComputeAngularAccelerationWorldRadPerS2(
			Identity, FVector(0.f, 10.f, 20.f), NoAngularVelocity,
			/*RollInput=*/0.f, /*RollAlpha=*/1500.f, /*MaxRollRate=*/120.f, Dt);

		TestEqual(TEXT("α_y = pitch/dt"),
			Alpha.Y, float(FMath::DegreesToRadians(10.0)) / Dt, 1.0e-5f);
		TestEqual(TEXT("α_z = yaw/dt"),
			Alpha.Z, float(FMath::DegreesToRadians(20.0)) / Dt, 1.0e-5f);
		TestEqual(TEXT("没滚就不给 α_x"), Alpha.X, 0.f, 1.0e-6f);
	}

	// ── 2. 横滚到顶：衰减到 0 ──
	{
		const FVector AtMaxRate(FMath::DegreesToRadians(120.f), 0.f, 0.f);
		const FVector Alpha = ASpaceshipPawn::ComputeAngularAccelerationWorldRadPerS2(
			Identity, FVector::ZeroVector, AtMaxRate,
			/*RollInput=*/1.f, 1500.f, 120.f, Dt);

		TestEqual(TEXT("到 MaxRollRate 后角加速度为 0"), Alpha.X, 0.f, 1.0e-6f);
	}

	// ── 3. 半速：衰减一半 ──
	{
		const FVector HalfRate(FMath::DegreesToRadians(60.f), 0.f, 0.f);
		const FVector Alpha = ASpaceshipPawn::ComputeAngularAccelerationWorldRadPerS2(
			Identity, FVector::ZeroVector, HalfRate, 1.f, 1500.f, 120.f, Dt);

		TestEqual(TEXT("半速时是额定角加速度的一半"),
			Alpha.X, float(FMath::DegreesToRadians(750.0)), 1.0e-3f);
	}

	// ── 4. 反向滚同样有效（对称） ──
	{
		const FVector Alpha = ASpaceshipPawn::ComputeAngularAccelerationWorldRadPerS2(
			Identity, FVector::ZeroVector, NoAngularVelocity, -1.f, 1500.f, 120.f, Dt);

		TestEqual(TEXT("A 键给出负角加速度"),
			Alpha.X, -float(FMath::DegreesToRadians(1500.0)), 1.0e-3f);
	}

	// ── 5. dt = 0：零，不产生 inf ──
	{
		const FVector Alpha = ASpaceshipPawn::ComputeAngularAccelerationWorldRadPerS2(
			Identity, FVector(0.f, 10.f, 0.f), NoAngularVelocity, 0.f, 1500.f, 120.f, 0.f);

		TestTrue(TEXT("dt=0 返回零"), Alpha.IsNearlyZero());
	}

	// ── 6. 机身绕世界 Z 转 90°：本地 pitch 变成世界 −X ──
	{
		const FQuat Yaw90(FVector::UpVector, FMath::DegreesToRadians(90.f));
		const FVector Alpha = ASpaceshipPawn::ComputeAngularAccelerationWorldRadPerS2(
			Yaw90, FVector(0.f, 10.f, 0.f), NoAngularVelocity, 0.f, 1500.f, 120.f, Dt);

		// 本地 +Y 在 90° 偏航后指向世界 −X。
		TestTrue(TEXT("本地 Y 已转到世界 −X"), Alpha.X < -0.1f);
		TestEqual(TEXT("世界 Y 分量为零"), Alpha.Y, 0.f, 1.0e-5f);
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
```

- [ ] **Step 2: 跑测试确认它失败**

先加 BOM，然后构建。
Expected: 编译失败 —— `ComputeAngularAccelerationWorldRadPerS2` 不是 `ASpaceshipPawn` 的成员。

- [ ] **Step 3: 改头文件**

`SpaceshipPawn.h`：

(a) 在 `MaxRollRate` 之后插入新属性：

```cpp
	/**
	 * 横滚角加速度（度/秒²）。A/D 给的是**持续角加速度**而不是直接设角速度：
	 * 求解器持有实际角速度，输入只能通过加速度进得去。
	 *
	 * 默认 1500 = MaxRollRate 120 ÷ 0.08 秒 —— 压杆后 80 ms 到满滚转，
	 * 与旧版「一帧就到位」相比略有惯性，但不至于像鼠标那样甩过头。
	 * 实际角速度到 MaxRollRate 时衰减到 0，所以稳态与旧版一致。
	 */
	UPROPERTY(EditAnywhere, Category = "Spaceship|Handling")
	float RollAngularAcceleration = 1500.f;
```

(b) 在 public 区（`void SetRoll(...)` 附近）之后加：

```cpp
	/**
	 * 本帧要施加的世界系角加速度（rad/s²）。**有副作用**：读走并清零待发冲量，
	 * 所以一帧只能调一次（UCosmosSimulationSubsystem::ApplyControlledBodyForces）。
	 *
	 * 自由视角（落地且没给推力）时返回零，同时丢弃待发冲量 —— 否则退出自由
	 * 视角的那一刻会补发一次落地前攒下的旧输入。
	 */
	FVector GetAngularAccelerationWorldRadPerS2(float DeltaSeconds);

	/** 子系统每帧写回的实际角速度（世界系 rad/s）。横滚到顶的衰减要读它。 */
	void SetCosmosAngularVelocity(const FVector& InWorldRadPerSecond)
	{
		CosmosAngularVelocity = InWorldRadPerSecond;
	}

	/** 角阻尼（1/秒），子系统换算成 1/模拟年 后写进求解器。 */
	float GetAngularDampingPerSecond() const { return AngularDamping; }

	/**
	 * 纯函数：待发冲量 + 横滚输入 → 世界系角加速度（rad/s²）。
	 *
	 * 单独拆出来是为了能无头测试 —— 测试不建 Actor，只喂四元数与系数。
	 */
	static FVector ComputeAngularAccelerationWorldRadPerS2(
		const FQuat& BodyRotation,
		const FVector& PendingImpulseLocalDegPerSec,
		const FVector& ActualAngularVelocityWorld,
		float InRollInput,
		float InRollAngularAccelerationDegPerS2,
		float InMaxRollRateDegPerSec,
		float DeltaSeconds);
```

(c) 删掉 `float YawRate = 0.f;` 与 `float PitchRate = 0.f;`，替换为：

```cpp
	/**
	 * 待发角速度冲量（机体系，度/秒）：Y = 俯仰、Z = 偏航。
	 * Look() 累加，GetAngularAccelerationWorldRadPerS2() 读一次并清零。
	 */
	FVector PendingAngularImpulseLocalDeg = FVector::ZeroVector;

	/** 子系统写回的实际角速度（世界系 rad/s），只用于横滚到顶的衰减。 */
	FVector CosmosAngularVelocity = FVector::ZeroVector;
```

- [ ] **Step 4: 改 `SpaceshipPawn.cpp`**

(a) `Look()` 里把两行 `YawRate` / `PitchRate` 赋值替换为：

```cpp
	// 冲量模型：鼠标位移累加成**角速度冲量**（机体系，度/秒）。子系统每帧把它
	// 读走、换算成角加速度注入求解器，求解器的角阻尼负责「转一会儿再停」。
	//
	// 上限沿用旧版的 MaxYawRate / MaxPitchRate：一帧内最多给出这么大的角速度。
	const float NewYaw = FMath::Clamp(
		float(PendingAngularImpulseLocalDeg.Z) + DeltaYaw * MouseImpulseScale,
		-MaxYawRate, MaxYawRate);
	const float NewPitch = FMath::Clamp(
		float(PendingAngularImpulseLocalDeg.Y) + DeltaPitch * MouseImpulseScale,
		-MaxPitchRate, MaxPitchRate);

	PendingAngularImpulseLocalDeg = FVector(0.f, NewPitch, NewYaw);
```

(b) `Tick` 里第 0 段的「刚进入自由视角清角速度」换成：

```cpp
	// 刚进入：丢弃待发冲量。否则落地那一刻的最后一甩会在自由视角里继续带着船转，
	// 与「落地即冻结」矛盾。
	//
	// 已经转起来的角速度归求解器管 —— 由它那份 AngularDamping 衰减，本类改不了。
	if (bFreeLookActive && !bWasFreeLookActive)
	{
		PendingAngularImpulseLocalDeg = FVector::ZeroVector;
	}
```

(c) 把第 1、2、3 段（从 `// --- 1. 角速度衰减 ---` 到自动回正块的收尾 `}`）整体替换为：

```cpp
	// --- 1–3. 姿态归求解器 ---
	//
	// 这里原来做三件事：角速度 FInterpTo 衰减、AddActorLocalRotation、自动回正。
	// 全部搬走了：
	//   - 鼠标冲量 → GetAngularAccelerationWorldRadPerS2() 交给求解器；
	//   - A/D → 持续角加速度（RollAngularAcceleration）；
	//   - 衰减 → 求解器的 AngularDamping；
	//   - 翻滚 → 接触冲量的力矩。
	//
	// 位置与朝向每帧由 UCosmosSimulationSubsystem 写回一次。**不要再在这里
	// AddActorLocalRotation**：两处都写姿态就是互相抹，表现是机身抖或转不动。
	//
	// AutoLevelSpeed 与 GetAutoLevelUp() 保留：前者是留着的开关（默认 0），
	// 后者还被 GetLandingQueryRadiusCm() 用来定重力方向。
```

(d) 在文件末尾追加两个函数：

```cpp
FVector ASpaceshipPawn::ComputeAngularAccelerationWorldRadPerS2(
	const FQuat& BodyRotation,
	const FVector& PendingImpulseLocalDegPerSec,
	const FVector& ActualAngularVelocityWorld,
	float InRollInput,
	float InRollAngularAccelerationDegPerS2,
	float InMaxRollRateDegPerSec,
	float DeltaSeconds)
{
	if (DeltaSeconds <= KINDA_SMALL_NUMBER)
	{
		return FVector::ZeroVector;
	}

	// 鼠标冲量：机体系（度/秒）→ 世界系（rad/s）→ 角加速度（rad/s²）。
	// 除以 dt 而不是乘：一帧的冲量在一帧内兑现，帧长变了手感不变。
	const FVector LocalRadPerSec(
		FMath::DegreesToRadians(PendingImpulseLocalDegPerSec.X),
		FMath::DegreesToRadians(PendingImpulseLocalDegPerSec.Y),
		FMath::DegreesToRadians(PendingImpulseLocalDegPerSec.Z));

	FVector AlphaWorld = BodyRotation.RotateVector(LocalRadPerSec) / DeltaSeconds;

	// 横滚：持续角加速度，实际滚转速率接近上限时线性衰减到 0。
	// 这个 taper 就是旧版「到 MaxRollRate 就不再增加」的连续版本。
	if (!FMath::IsNearlyZero(InRollInput) && InMaxRollRateDegPerSec > 0.f)
	{
		const FVector LocalAngularVelocity =
			BodyRotation.UnrotateVector(ActualAngularVelocityWorld);
		const float RollRateDeg = FMath::RadiansToDegrees(float(LocalAngularVelocity.X));
		const float Taper = FMath::Clamp(
			1.f - FMath::Abs(RollRateDeg) / InMaxRollRateDegPerSec, 0.f, 1.f);

		const float AlphaRollRad =
			FMath::DegreesToRadians(InRollInput * InRollAngularAccelerationDegPerS2 * Taper);
		AlphaWorld += BodyRotation.RotateVector(FVector(AlphaRollRad, 0.f, 0.f));
	}

	return AlphaWorld;
}

FVector ASpaceshipPawn::GetAngularAccelerationWorldRadPerS2(float DeltaSeconds)
{
	// 自由视角：鼠标归相机。这里也必须清掉待发冲量 —— Look() 在自由视角里
	// 走的是相机分支、不会写它，攒下的是进入自由视角之前那一帧的旧值。
	if (bFreeLookActive)
	{
		PendingAngularImpulseLocalDeg = FVector::ZeroVector;
		return FVector::ZeroVector;
	}

	const FVector Result = ComputeAngularAccelerationWorldRadPerS2(
		GetActorQuat(), PendingAngularImpulseLocalDeg, CosmosAngularVelocity,
		RollInput, RollAngularAcceleration, MaxRollRate, DeltaSeconds);

	PendingAngularImpulseLocalDeg = FVector::ZeroVector;
	return Result;
}
```

- [ ] **Step 5: 确认没有别处再写姿态**

```bash
grep -rn "AddActorLocalRotation\|AddActorWorldRotation" Source/StellarEraPawns/Src
```

Expected: 无输出。有输出就逐个确认它不是飞船姿态的第二个写入方。

- [ ] **Step 6: 构建 + 跑测试**

Run: 构建命令，然后：
```bash
"$EDITOR" "$PROJ" -ExecCmds="Automation RunTests StellarEra.Spaceship" \
  -TestExit="Automation Test Queue Empty" -unattended -nopause -nullrhi -nosplash -stdout \
  -abslog="F:/tmp/stellar_ship.log"
```
Expected: `StellarEra.Spaceship.AngularInput` 与既有的 `StellarEra.Spaceship.ShipFreeLook` 都通过。

- [ ] **Step 7: 提交**

```bash
git add -A
git commit -m "feat(飞船): 姿态输入改为角加速度端口，交给求解器积分" \
  -m "Co-Authored-By: Claude Code <noreply@anthropic.com>"
```

---

## Task 4: 子系统接线

**Files:**
- Modify: `Source/StellarEraCosmos/Src/CosmosSimulationSubsystem.cpp`（四处）

**Interfaces:**
- Consumes: Task 3 的 `GetAngularAccelerationWorldRadPerS2` / `SetCosmosAngularVelocity` / `GetAngularDampingPerSecond`；Task 1 的 `bRigidBody` / `Orientation` / `InverseInertiaLocal` / `ExternalAngularAcceleration` / `AngularDamping`；Task 2 的接触求解。
- Produces: 无新公开符号（全部在已有的 private/局部作用域里）。

本任务没有新增自动化测试 —— 子系统要世界、要 Actor、要 `TimeScale`，无头测试里搭不出来（与既有子系统代码同一口径）。它的正确性由 Task 5 的 PIE 验收 + 现有全量测试兜底。

- [ ] **Step 1: 注册成刚体（`AddBodyToSimulator` 的飞船分支）**

把飞船分支替换为：

```cpp
	else if (const ASpaceshipPawn* Ship = Cast<ASpaceshipPawn>(BodyActor))
	{
		// 这里先给 300 cm 球当**兜底**：第一次 Tick 就会由 RefreshShipContactProbes
		// 换成船体凸包生成的接触探针（Radius 同时置 0）。只有网格没导 Simple
		// Collision 时才真的用这个球。
		State.Radius = double(Ship->GetContactRadiusCm()) / CentimetersPerAu;

		// 刚体：姿态归求解器，Actor 的 Rotation 每帧从这里写回。
		State.bRigidBody = true;

		// 关卡的摆放朝向是初态。FQuat 与 FQuat4d 精度不同，逐个分量转。
		const FQuat InitialRotation = Ship->GetActorQuat();
		State.Orientation = FQuat4d(
			InitialRotation.X, InitialRotation.Y, InitialRotation.Z, InitialRotation.W);

		// 逐步退出的老开关：bZeroVelocityOnContact 会把整条速度钉成行星的速度
		// （撞地即停）。刚体接触靠摩擦停，不再需要它 —— 留着位不置，行星间接触
		// 仍走默认的光滑硬球语义。
	}
```

- [ ] **Step 2: 探针按朝向变换 + 写惯性张量（`RefreshShipContactProbes`）**

(a) 现有的 `Ship->GetActorTransform()` 保持不动，在它上方加一行注释：

```cpp
		// 用的是 Actor 的变换（含上一帧写回的朝向），比求解器的 Orientation 晚一帧。
		// 探针本来就是每帧重采的近似，晚一帧的误差远小于一个探针间距。
```

(b) 在 `State.SetContactProbes(MoveTemp(Probes));` 与 `State.Radius = 0.0;` 之后、逐探针地表半径那段之前插入：

```cpp
		// 惯性张量：用探针点的本地 AABB 当长方体。探针集就是船体凸包的代表点，
		// AABB 即船体包围盒；按包围盒算惯量足以决定「哪条边着地时翻得多快」，
		// 而且不需要读网格资产。
		FBox ProbeBox(ForceInit);
		for (const FVector& LocalProbe : LocalProbes)
		{
			ProbeBox += LocalProbe;
		}

		const FVector HalfExtentCm = ProbeBox.GetExtent();
		if (HalfExtentCm.X > 0.f && HalfExtentCm.Y > 0.f && HalfExtentCm.Z > 0.f)
		{
			State.InverseInertiaLocal = FCosmosBodyState::InverseInertiaFromBox(
				State.Mass, FVector3d(HalfExtentCm) / CentimetersPerAu);
		}
		else
		{
			// 探针全落在一个平面 / 一条线上：长方体公式退化。
			// 退回均匀球，至少不会给出零惯量（零惯量 = 撞了也不转）。
			State.InverseInertiaLocal = FCosmosBodyState::InverseInertiaFromSphere(
				State.Mass, FMath::Max(State.ContactProbeRadius, 1.0e-9));
		}
```

- [ ] **Step 3: 角加速度与角阻尼（`ApplyControlledBodyForces`）**

在 `Body.LinearDamping = ...` 之后、`bHasForcedBodies |= ...` 之前插入：

```cpp
		// 角速度先写给飞船：横滚的「到顶衰减」要读实际角速度，而读的必须是
		// 上一帧写回的值 —— 求解器单位（rad/模拟年）→ 真实单位（rad/秒）。
		Ship->SetCosmosAngularVelocity(FVector(Body.AngularVelocity * TimeScale));

		// 角加速度：真实 rad/s² → 求解器 rad/模拟年²。多除一个 TimeScale 是因为
		// 它是 dt 的二阶量（α·dt² 才是角度）。
		Body.ExternalAngularAcceleration =
			FVector3d(Ship->GetAngularAccelerationWorldRadPerS2(DeltaSeconds))
			/ (TimeScale * TimeScale);

		// 角阻尼同 LinearDamping：τ 是真实秒，求解器的时间单位是模拟年。
		Body.AngularDamping = double(Ship->GetAngularDampingPerSecond()) / TimeScale;
```

并把 `bHasForcedBodies |= ...` 的条件补上角速度项：

```cpp
		bHasForcedBodies |= !Body.ExternalAcceleration.IsNearlyZero()
			|| !Body.ExternalAngularAcceleration.IsNearlyZero()
			|| Body.LinearDamping != 0.0;
```

- [ ] **Step 4: 朝向写回**

把 `if (Component->bSimulated) { ... }` 里的 `SetActorLocation` 那一行替换成：

```cpp
			const FCosmosBodyState& State = Simulator.GetBody(Component->SimulatorIndex);

			// 刚体（飞船）：位置 + 朝向一起写回。朝向的权威在求解器 —— 这里不写
			// 就没人写，而谁在别处 AddActorLocalRotation 就是两个写入方互相抹。
			//
			// 非刚体天体只写位置：它们的 Orientation 恒为单位四元数，若也走
			// SetActorTransform，关卡里摆好的行星朝向会被瞬间抹成零。
			if (State.bRigidBody)
			{
				BodyActor->SetActorTransform(
					FTransform(
						FQuat(
							State.Orientation.X, State.Orientation.Y,
							State.Orientation.Z, State.Orientation.W),
						Simulator.ToWorldLocation(State.Position),
						BodyActor->GetActorScale3D()),
					/*bSweep=*/false, nullptr, ETeleportType::None);
			}
			else
			{
				BodyActor->SetActorLocation(
					Simulator.ToWorldLocation(State.Position), /*bSweep=*/false);
			}
```

注意：原来那行 `const FCosmosBodyState& State = Simulator.GetBody(...)` 要删掉（上面已经声明）。

- [ ] **Step 5: 构建**

Run: 构建命令。
Expected: 通过，无新增警告。

- [ ] **Step 6: 跑全量测试**

```bash
"$EDITOR" "$PROJ" -ExecCmds="Automation RunTests StellarEra" \
  -TestExit="Automation Test Queue Empty" -unattended -nopause -nullrhi -nosplash -stdout \
  -abslog="F:/tmp/stellar_all.log"
```
Expected: 全绿。特别注意既有的 `StellarEra.Cosmos.*`（接触、轨道）没有回归 —— 行星不带探针、`bRigidBody` 为假，走的还是原来的分支。

- [ ] **Step 7: 提交**

```bash
git add -A
git commit -m "feat(飞船): 子系统接线刚体姿态（注册/惯量/角加速度/写回）" \
  -m "Co-Authored-By: Claude Code <noreply@anthropic.com>"
```

---

## Task 5: PIE 人工验收与调参

**Files:** 无（只可能改 `Content/Code/BP_SpaceshipPawn` 的默认值，除非验收发现问题）

- [ ] **Step 1: 打开编辑器与 `UniverseMap`，进 PIE**

编辑器在前台（后台会被限到 3 FPS，手感全部失真）。

- [ ] **Step 2: 逐条验收**

| # | 操作 | 预期 |
|---|---|---|
| 1 | 松开所有输入，从 200 m 高自由落体 | 落地瞬间**不冻结**：机身继续往下沉一点、随姿态翻/滚，然后停住 |
| 2 | 侧着落地（先按住 A 滚 90° 再落） | 侧翼先触地 → 机身被**推着翻**向另一侧，不是原地不动 |
| 3 | 落地后按住 W 起推 | 能滑起来；松 W 后靠摩擦在数秒内停下，不是瞬停 |
| 4 | 空中甩鼠标 | 与改动前手感一致：甩一下转一会儿再停（总转角 ≈ 25°，由 `AngularDamping` 3.5 决定） |
| 5 | 空中按住 A/D | 滚到 `MaxRollRate`（120°/s）后匀速，不超速 |
| 6 | 落地且无输入 | 鼠标转相机、船不动（自由视角），与改动前一致 |
| 7 | 停稳后按 E 下船 / 再上船 | 不变 |
| 8 | 高速斜擦地表 | 会被弹开一点并带滚转，而不是沿地表滑到天荒地老（μ = 0.6 的摩擦） |

- [ ] **Step 3: 手感不对时的旋钮（按顺序试）**

| 症状 | 旋钮 | 方向 |
|---|---|---|
| 落地后翻得太猛、像被掀飞 | `BP_SpaceshipPawn` 的 `AngularDamping` | 调大（8 → 12），角速度衰减更快 |
| 落地后几乎不翻 | 同上 | 调小（3.5 → 2） |
| 松开 W 后滑太远 | `FCosmosBodyState::ContactFriction` 的默认值（0.6） | 调大（0.8） |
| 打滑得像冰面 | 同上 | 调小（0.3） |
| 压 A/D 到满滚转太久 | `RollAngularAcceleration`（1500 度/秒²） | 调大（3000） |
| 滚转一压就到位、没有惯性 | 同上 | 调小（600） |

改 `ContactFriction` 要重新构建（C++ 默认值）；其余是蓝图属性，改完直接 PIE。

- [ ] **Step 4: 记录实测值**

把最终采用的参数写进 spec 的 §7「风险与旋钮」段末，一起提交。

- [ ] **Step 5: 提交**

```bash
git add -A
git commit -m "docs(飞船): 刚体接触 PIE 验收与实测参数" \
  -m "Co-Authored-By: Claude Code <noreply@anthropic.com>"
```

---

## 自评

**Spec 覆盖检查：**

| Spec 章节 | 落在哪个任务 |
|---|---|
| §3.1 状态字段（8 个） | Task 1（6 个物理字段）+ Task 2（2 个接触旋钮） |
| §3.2 朝向积分与调用点 | Task 1 Step 3–4 |
| §3.3 单位换算 | Task 4 Step 3；Global Constraints 一张表 |
| §3.4 惯量 | Task 1 Step 3–4（公式）+ Task 4 Step 2（从探针 AABB 落地） |
| §3.5 接触求解 | Task 2 Step 4 |
| §3.6 Pawn 输入 | Task 3 |
| §3.7 子系统接线与写回守卫 | Task 4 Step 1/3/4 |
| §3.8 一帧数据流 | Task 4 整体（顺序即数据流） |
| §4 边界与兜底 | Task 2 Step 4（近零距离跳过、点上限）、Task 4 Step 2（平面探针退回球） |
| §5 文件清单 | 「文件结构」表 |
| §6 测试 | Task 1/2/3 的测试 + Task 4 Step 6 全量 + Task 5 人工 |
| §7 风险与旋钮 | Task 5 Step 3 的旋钮表 |
| §8 不做的事 | 无任务（自洽） |

**占位符扫描：** 无 TBD / TODO；每个代码步骤都给了可粘贴的完整实现；每个运行步骤给了命令与预期输出。

**类型一致性：** `Orientation` 全程 `FQuat4d`；`AngularVelocity` / `InverseInertiaLocal` / `ExternalAngularAcceleration` 全程 `FVector3d`；Pawn 侧的 `PendingAngularImpulseLocalDeg` / `CosmosAngularVelocity` 是 `FVector`（只在输入边界用）；换算函数名 `GetAngularAccelerationWorldRadPerS2` 在 Task 3 定义、Task 4 调用，一致。

**已知的口径差异（写在这里，免得实施者以为是 bug）：** 探针用的是 Actor 变换（含上一帧朝向），不是求解器本帧的 `Orientation`；差一帧，误差远小于探针间距。

---

## 适配合并（2026-11-07）

本文档写于分支 fork 点，之后 master 做了大规模重构。合并时按 master 现结构重新落地，
**上文的代码片段不再逐字对应仓库**，差异如下：

| 文档里的写法 | 落地写法 |
|---|---|
| `Cast<ASpaceshipPawn>(BodyActor)` | `Cast<ICosmosBody>`：master 抽了接口，Cosmos 不再认识 Pawns |
| 接口 8 个纯虚 | 11 个，新增 `GetAngularAccelerationWorldRadPerS2` / `GetAngularDampingPerSecond` / `SetCosmosAngularVelocity` |
| 姿态输入写在 Pawn 的 Tick 里 | 输入已拆到 `Spaceship/Input/`，`Look()` 只写待发冲量 |
| `ApplyControlledBodyForces` 在 Tick 开头调用 | 挪进 `NumSteps > 0` 分支，按实际消费的真实秒调用（零子步帧不读冲量） |

顺带修了两处 master 上的既存问题（与本特性无关，但拦住了验证）：

- `StellarEraPlayerControllerBase.cpp` 的局部变量 `Character` 遮蔽 `AController::Character`，
  UBT 把 C4458 当错误 → 改名 `Crew`。
- `StellarEra.Planet.AtmosphereMaterial.Structure` 仍断言壳材质有 `PlanetCenter` 向量参数，
  而 `e134414` 已改成渲染侧 `ObjectPositionWS` → 删该断言并加一条「必须不存在」。

Task 5 的 PIE 人工验收（撞地翻滚、摩擦停稳、鼠标转向惯性）仍需人工在 `UniverseMap` 里做。
