# 飞船刚体驱动实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 1× 时飞船改为 UE 刚体：求解器只算受力并施力，Chaos 积分与解接触，物理步后把刚体状态读回求解器。

**Architecture:** 给 `FCosmosBodyState` 加 `bPhysicsDriven`，求解器跳过该体的积分与接触；`ICosmosBody` 增加刚体开关/施力接口，`ASpaceshipPawn` 用 Hull 实现；`UCosmosSimulationSubsystem` 负责读回、施力、写回取舍与 1×/非 1× 模式切换。行星与恒星保持现状。

**Tech Stack:** UE 5.8 C++ / Chaos 刚体 / Automation Tests。

**Spec:** `docs/superpowers/specs/2026-11-26-ship-force-driven-design.md`

## Global Constraints

- 编译前必须关编辑器；构建用捆绑 dotnet 直调 UBT（见 CLAUDE.md「构建与测试」），不要用 `Build.bat`。
- 单位换算只走 `CosmosUnits::`；位置用 `Simulator.ToWorldLocation` / `ToGameUnitPosition`。
- 只改飞船；行星/恒星的积分与写回不动。
- 注释只写为什么，单处 1–3 行。
- 提交带 pathspec（`git commit -m "..." -- <paths>`），索引里有常驻暂存文件。
- 新测试文件里的工具函数加文件名前缀（unity 构建下匿名命名空间会合并）。

---

### Task 1: 求解器跳过物理驱动的体（积分与接触）

**Files:**
- Modify: `Source/StellarEraCosmos/Src/NBody/NBodySimulator.h`（`FCosmosBodyState` 加字段）
- Modify: `Source/StellarEraCosmos/Src/NBody/NBodySimulator.cpp:125-162`（两个积分器）、`:320-342`（接触配对）
- Test: `Source/StellarEraCosmos/Src/Tests/PhysicsDrivenTests.cpp`（新建）

**Interfaces:**
- Produces: `FCosmosBodyState::bPhysicsDriven`（`bool`，默认 `false`）。

- [ ] **Step 1: 写失败测试**

新建 `Source/StellarEraCosmos/Src/Tests/PhysicsDrivenTests.cpp`：

```cpp
﻿// Copyright Voxel Plugin SAS. All Rights Reserved.
// bPhysicsDriven：积分与接触都要跳过该体，但引力加速度照常算（施力要用）。

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "NBodySimulator.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	FNBodySimulator PhysDriven_MakeSimulator()
	{
		FNBodySimulator Simulator;
		Simulator.SetUnitSystem(ECosmosUnitSystem::Game);
		Simulator.SetIntegrator(ECosmosIntegrator::VelocityVerlet);
		Simulator.SetGravityCutoff(1.0e-9);
		return Simulator;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraCosmosPhysicsDrivenSkipTest,
	"StellarEra.Cosmos.PhysicsDriven.Skip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraCosmosPhysicsDrivenSkipTest::RunTest(const FString& Parameters)
{
	// ── 1. 驱动体不积分，但引力加速度照常 ──
	{
		FNBodySimulator Simulator = PhysDriven_MakeSimulator();

		FCosmosBodyState Anchor;
		Anchor.Mass = 1.0;
		Anchor.Position = FVector3d::ZeroVector;
		Simulator.AddBody(Anchor);

		FCosmosBodyState Driven;
		Driven.Mass = 1.0e-9;
		Driven.Position = FVector3d(1.0, 0.0, 0.0);
		Driven.Velocity = FVector3d(0.0, 1.0, 0.0);
		Driven.bPhysicsDriven = true;
		const int32 DrivenIndex = Simulator.AddBody(Driven);

		FCosmosBodyState Normal;
		Normal.Mass = 1.0e-9;
		Normal.Position = FVector3d(2.0, 0.0, 0.0);
		const int32 NormalIndex = Simulator.AddBody(Normal);

		Simulator.StepExact(1.0e-4);

		const FCosmosBodyState& D = Simulator.GetBody(DrivenIndex);
		TestEqual(TEXT("驱动体位置不动"), D.Position.X, 1.0, 1.0e-15);
		TestEqual(TEXT("驱动体速度不动"), D.Velocity.Y, 1.0, 1.0e-15);
		TestTrue(TEXT("驱动体仍算出了引力加速度"), D.Acceleration.SizeSquared() > 0.0);

		const FCosmosBodyState& N = Simulator.GetBody(NormalIndex);
		TestTrue(TEXT("非驱动体照常被吸引"), N.Position.X < 2.0);
	}

	// ── 2. 驱动体不参与接触 ──
	{
		FNBodySimulator Simulator = PhysDriven_MakeSimulator();

		FCosmosBodyState Sphere;
		Sphere.Mass = 1.0e6;
		Sphere.Position = FVector3d::ZeroVector;
		Sphere.Radius = 10.0;
		Simulator.AddBody(Sphere);

		FCosmosBodyState Driven;
		Driven.Mass = 1.0;
		Driven.Position = FVector3d(0.0, 0.0, 8.0);   // 穿透 2
		Driven.bPhysicsDriven = true;
		const int32 DrivenIndex = Simulator.AddBody(Driven);

		Simulator.StepExact(1.0e-5);

		const FCosmosBodyState& D = Simulator.GetBody(DrivenIndex);
		TestEqual(TEXT("驱动体不被推出"), D.Position.Z, 8.0, 1.0e-15);
		TestTrue(TEXT("驱动体速度仍为零"), D.Velocity.IsZero());
	}

	return true;
}

#endif
```

- [ ] **Step 2: 确认测试失败**

编译并跑测试（编辑器必须关）：

```bash
cd /d/Temp && "D:/Application/Epic Games/UE_5.8/Engine/Binaries/ThirdParty/DotNet/10.0/win-x64/dotnet.exe" "D:/Application/Epic Games/UE_5.8/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.dll" StellarEraEditor Win64 Development -Project="F:/workspace/ue/StellarEra/StellarEra.uproject" -WaitMutex -FromMsBuild
```

预期：编译失败，`FCosmosBodyState` 没有成员 `bPhysicsDriven`。

- [ ] **Step 3: 加字段**

`FCosmosBodyState`（`NBodySimulator.h`，`bTestParticle` 之后）加：

```cpp
	/**
	 * 由外部物理体驱动：求解器只算它受的力，不积分位置/速度、不做接触。
	 * 读回由子系统的 Tick 负责。
	 */
	bool bPhysicsDriven = false;
```

- [ ] **Step 4: 积分器跳过**

`StepVelocityVerlet` 两个循环体都加早退：

```cpp
	for (int32 i = 0; i < N; ++i)
	{
		// 物理驱动的体位置归 Chaos；这里只保留它的引力贡献（ComputeAccelerations 照跑）。
		if (Bodies[i].bPhysicsDriven)
		{
			continue;
		}
		Bodies[i].Position += Bodies[i].Velocity * (Dt * 0.5);
	}
```

```cpp
	for (int32 i = 0; i < N; ++i)
	{
		if (Bodies[i].bPhysicsDriven)
		{
			continue;
		}
		Bodies[i].Velocity += Bodies[i].Acceleration * Dt;
		Bodies[i].Position += Bodies[i].Velocity * (Dt * 0.5);
	}
```

`StepEuler` 同构：

```cpp
	for (int32 i = 0; i < Bodies.Num(); ++i)
	{
		if (Bodies[i].bPhysicsDriven)
		{
			continue;
		}
		Bodies[i].Velocity += Bodies[i].Acceleration * Dt;
		Bodies[i].Position += Bodies[i].Velocity * Dt;
	}
```

- [ ] **Step 5: 接触配对跳过**

`ResolveContacts` 的双重循环，最前面加：

```cpp
			// 物理驱动的体由 UE 物理解接触，求解器这边不能再碰一次。
			if (Bodies[i].bPhysicsDriven || Bodies[j].bPhysicsDriven)
			{
				continue;
			}
```

- [ ] **Step 6: 编译并跑测试**

```bash
cd /d/Temp && "D:/Application/Epic Games/UE_5.8/Engine/Binaries/ThirdParty/DotNet/10.0/win-x64/dotnet.exe" "D:/Application/Epic Games/UE_5.8/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.dll" StellarEraEditor Win64 Development -Project="F:/workspace/ue/StellarEra/StellarEra.uproject" -WaitMutex -FromMsBuild
"D:/Application/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "F:/workspace/ue/StellarEra/StellarEra.uproject" -ExecCmds="Automation RunTests StellarEra.Cosmos.PhysicsDriven" -TestExit="Automation Test Queue Empty" -unattended -nopause -nullrhi -nosplash -stdout -abslog="D:/Temp/physicsdriven-test.log"
```

预期：`StellarEra.Cosmos.PhysicsDriven.Skip` 通过；`grep -c "Error" D:/Temp/physicsdriven-test.log` 为 0。

- [ ] **Step 7: 提交**

```bash
git add Source/StellarEraCosmos/Src/Tests/PhysicsDrivenTests.cpp Source/StellarEraCosmos/Src/NBody/NBodySimulator.h Source/StellarEraCosmos/Src/NBody/NBodySimulator.cpp
git commit -m "feat(宇宙): FCosmosBodyState 增加 bPhysicsDriven，积分与接触跳过该体" -- Source/StellarEraCosmos/Src/Tests/PhysicsDrivenTests.cpp Source/StellarEraCosmos/Src/NBody/NBodySimulator.h Source/StellarEraCosmos/Src/NBody/NBodySimulator.cpp
```

---

### Task 2: ICosmosBody 刚体接口 + ASpaceshipPawn 实现

**Files:**
- Modify: `Source/StellarEraCosmos/Src/CosmosBody.h`（接口末尾加 4 个纯虚）
- Modify: `Source/StellarEraPawns/Src/Spaceship/SpaceshipPawn.h:384-408`（`GetVelocity` 覆写 + 新方法声明 + `bPhysicsDriven` 成员）
- Modify: `Source/StellarEraPawns/Src/Spaceship/SpaceshipPawn.cpp`（实现）

**Interfaces:**
- Consumes: `FCosmosBodyState::bPhysicsDriven`（Task 1，仅测试侧；本任务不用）。
- Produces:
  - `virtual bool ICosmosBody::SetPhysicsDriven(bool bDriven, float MassKg) = 0;`
  - `virtual bool ICosmosBody::IsPhysicsDriven() const = 0;`
  - `virtual void ICosmosBody::AddCosmosForceWorldCmPerS2(const FVector& AccelerationWorldCmPerS2) = 0;`
  - `virtual void ICosmosBody::SetCosmosLinearDamping(float DampingPerSecond) = 0;`
  - `ASpaceshipPawn::GetVelocity()` 在物理驱动时返回刚体线速度。

- [ ] **Step 1: 接口加方法**

`CosmosBody.h` 在 `IsDebugViewMode` 之前加：

```cpp
	// ── 物理驱动 ──

	/**
	 * 切换 UE 刚体模拟。进入时把刚体质量设成 MassKg，退出时关模拟。
	 * @return false = 引擎侧不可用（无碰撞体/质量非法），调用方应退回求解器驱动。
	 */
	virtual bool SetPhysicsDriven(bool bDriven, float MassKg) = 0;

	virtual bool IsPhysicsDriven() const = 0;

	/** 物理驱动时每帧施加的加速度（世界厘米/秒²）；实现方自己乘质量。 */
	virtual void AddCosmosForceWorldCmPerS2(const FVector& AccelerationWorldCmPerS2) = 0;

	/** 刚体线性阻尼（1/真实秒）。 */
	virtual void SetCosmosLinearDamping(float DampingPerSecond) = 0;
```

- [ ] **Step 2: Pawn 头文件声明**

`SpaceshipPawn.h` 的 `GetVelocity()` 覆写改成：

```cpp
	/** 物理驱动时按刚体真实速度答；否则按求解器写回的 CosmosVelocity。 */
	virtual FVector GetVelocity() const override;
```

在 `SetCosmosVelocity` 附近加声明：

```cpp
	/** 物理驱动接口，见 ICosmosBody。 */
	bool SetPhysicsDriven(bool bDriven, float MassKg) override;
	bool IsPhysicsDriven() const override { return bPhysicsDriven; }
	void AddCosmosForceWorldCmPerS2(const FVector& AccelerationWorldCmPerS2) override;
	void SetCosmosLinearDamping(float DampingPerSecond) override;
```

`CosmosVelocity` 成员旁加：

```cpp
	/** 位移归 UE 刚体（1× 物理模式）。 */
	bool bPhysicsDriven = false;
```

- [ ] **Step 3: Pawn 实现**

`SpaceshipPawn.cpp` 加：

```cpp
FVector ASpaceshipPawn::GetVelocity() const
{
	// 物理模式下 CosmosVelocity 不再每帧写回，直接读刚体，HUD/阻力/预测都拿真值。
	return bPhysicsDriven && Hull ? Hull->GetPhysicsLinearVelocity() : CosmosVelocity;
}

bool ASpaceshipPawn::SetPhysicsDriven(const bool bDriven, const float MassKg)
{
	if (!Hull || (bDriven && MassKg <= 0.f))
	{
		return false;
	}

	if (bDriven == bPhysicsDriven)
	{
		return true;
	}

	if (bDriven)
	{
		Hull->SetSimulatePhysics(true);
		Hull->SetEnableGravity(false);
		Hull->SetMassOverrideInKg(NAME_None, MassKg, true);
		Hull->SetUseCCD(true);
		// 进入时速度取求解器当前值：切模式不丢轨道速度。
		Hull->SetPhysicsLinearVelocity(CosmosVelocity);
	}
	else
	{
		Hull->SetSimulatePhysics(false);
		Hull->SetLinearDamping(0.f);
	}

	bPhysicsDriven = bDriven;
	return true;
}

void ASpaceshipPawn::AddCosmosForceWorldCmPerS2(const FVector& AccelerationWorldCmPerS2)
{
	if (!bPhysicsDriven || !Hull || !Hull->IsSimulatingPhysics())
	{
		return;
	}

	Hull->AddForce(AccelerationWorldCmPerS2 * Hull->GetMass(), NAME_None, /*bAccelChange=*/false);
}

void ASpaceshipPawn::SetCosmosLinearDamping(const float DampingPerSecond)
{
	if (Hull && bPhysicsDriven)
	{
		Hull->SetLinearDamping(FMath::Max(DampingPerSecond, 0.f));
	}
}
```

- [ ] **Step 4: 编译**

```bash
cd /d/Temp && "D:/Application/Epic Games/UE_5.8/Engine/Binaries/ThirdParty/DotNet/10.0/win-x64/dotnet.exe" "D:/Application/Epic Games/UE_5.8/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.dll" StellarEraEditor Win64 Development -Project="F:/workspace/ue/StellarEra/StellarEra.uproject" -WaitMutex -FromMsBuild
```

预期：`Result: Succeeded`。

- [ ] **Step 5: 提交**

```bash
git add Source/StellarEraCosmos/Src/CosmosBody.h Source/StellarEraPawns/Src/Spaceship/SpaceshipPawn.h Source/StellarEraPawns/Src/Spaceship/SpaceshipPawn.cpp
git commit -m "feat(飞船): ICosmosBody 增加刚体驱动接口并在 SpaceshipPawn 实现" -- Source/StellarEraCosmos/Src/CosmosBody.h Source/StellarEraPawns/Src/Spaceship/SpaceshipPawn.h Source/StellarEraPawns/Src/Spaceship/SpaceshipPawn.cpp
```

---

### Task 3: 子系统接管物理模式（读回 / 施力 / 写回取舍 / 模式切换）

**Files:**
- Modify: `Source/StellarEraCosmos/Src/CosmosSimulationSubsystem.h`（新方法、新字段）
- Modify: `Source/StellarEraCosmos/Src/CosmosSimulationSubsystem.cpp`（Tick、RefreshShipContactProbes、写回）
- Modify: `Source/StellarEra/Src/StellarEraPlayerController.cpp:153-172`（倍率通知）

**Interfaces:**
- Consumes: `ICosmosBody::SetPhysicsDriven/IsPhysicsDriven/AddCosmosForceWorldCmPerS2/SetCosmosLinearDamping`（Task 2）；`FCosmosBodyState::bPhysicsDriven`（Task 1）。
- Produces:
  - `UCosmosSimulationSubsystem::SetTimeScaleMultiplier(float InMultiplier)`
  - `UCosmosSimulationSubsystem::SyncPhysicsDrivenBodies()`（私有）
  - `UCosmosSimulationSubsystem::ApplyShipPhysicsForces()`（私有）

- [ ] **Step 1: 子系统头文件**

`CosmosSimulationSubsystem.h` public 段加：

```cpp
	/**
	 * 控制器每次调倍率时通知。1× 才允许刚体物理：Chaos 按真实时间积分，
	 * 倍率一上去行星一帧就跨过整个星球。
	 */
	void SetTimeScaleMultiplier(float InMultiplier);
```

private 段（`RefreshShipContactProbes` 旁）加：

```cpp
	/** 物理步之后把驱动的刚体状态读回求解器（位置、线速度）。 */
	void SyncPhysicsDrivenBodies();

	/** 把引力 + 外力换算成力加到刚体上，并同步线性阻尼。 */
	void ApplyShipPhysicsForces();
```

字段（`TimeScale` 旁）加：

```cpp
	/** 时间倍率（1 = 实时）。只有 1× 时飞船走 UE 刚体。 */
	float TimeScaleMultiplier = 1.f;
```

- [ ] **Step 2: 实现模式切换与读回**

`CosmosSimulationSubsystem.cpp` 加：

```cpp
void UCosmosSimulationSubsystem::SetTimeScaleMultiplier(const float InMultiplier)
{
	TimeScaleMultiplier = InMultiplier;
}

void UCosmosSimulationSubsystem::SyncPhysicsDrivenBodies()
{
	if (CentimetersPerAu <= 0.0 || TimeScale <= 0.0)
	{
		return;
	}

	for (UCosmosOrbitComponent* Component : Bodies)
	{
		AActor* OwnerActor = Component ? Component->GetOwner() : nullptr;
		ICosmosBody* Ship = Cast<ICosmosBody>(OwnerActor);
		if (!Ship || !Ship->IsPhysicsDriven() || Component->SimulatorIndex == INDEX_NONE
			|| Component->SimulatorIndex >= Simulator.GetNumBodies())
		{
			continue;
		}

		FCosmosBodyState& State = Simulator.GetBodyMutable(Component->SimulatorIndex);
		State.Position = Simulator.ToGameUnitPosition(OwnerActor->GetActorLocation());
		State.Velocity = CosmosUnits::WorldCmPerSecondToGameVelocity(
			FVector3d(OwnerActor->GetVelocity()), CentimetersPerAu, TimeScale);
	}
}

void UCosmosSimulationSubsystem::ApplyShipPhysicsForces()
{
	if (CentimetersPerAu <= 0.0 || TimeScale <= 0.0)
	{
		return;
	}

	for (UCosmosOrbitComponent* Component : Bodies)
	{
		AActor* OwnerActor = Component ? Component->GetOwner() : nullptr;
		ICosmosBody* Ship = Cast<ICosmosBody>(OwnerActor);
		if (!Ship || !Ship->IsPhysicsDriven() || Component->SimulatorIndex == INDEX_NONE
			|| Component->SimulatorIndex >= Simulator.GetNumBodies())
		{
			continue;
		}

		const FCosmosBodyState& State = Simulator.GetBody(Component->SimulatorIndex);
		const FVector3d AccelerationGame = State.GravitationalAcceleration + State.ExternalAcceleration;
		Ship->AddCosmosForceWorldCmPerS2(FVector(CosmosUnits::GameAccelerationToWorldCmPerS2(
			AccelerationGame, CentimetersPerAu, TimeScale)));

		const float DampingTime = Ship->GetDampingTime();
		Ship->SetCosmosLinearDamping(DampingTime > 0.f ? 1.f / DampingTime : 0.f);
	}
}
```

- [ ] **Step 3: Tick 接上**

`UCosmosSimulationSubsystem::Tick`，在 `if (!bInitialized) FinalizeRegistration();` 之后、`RefreshPlanetContactRadii()` 之前插入：

```cpp
	// 物理驱动的体：先把上一物理步的结果读回求解器，再决定本帧模式。
	SyncPhysicsDrivenBodies();

	// 1× 才开刚体；切模式时对齐位置/速度，避免瞬移后弹开。
	const bool bWantPhysics = FMath::IsNearlyEqual(TimeScaleMultiplier, 1.f);
	for (UCosmosOrbitComponent* Component : Bodies)
	{
		AActor* OwnerActor = Component ? Component->GetOwner() : nullptr;
		ICosmosBody* Ship = Cast<ICosmosBody>(OwnerActor);
		if (!Ship || Component->SimulatorIndex == INDEX_NONE
			|| Component->SimulatorIndex >= Simulator.GetNumBodies())
		{
			continue;
		}

		FCosmosBodyState& State = Simulator.GetBodyMutable(Component->SimulatorIndex);
		if (bWantPhysics && !Ship->IsPhysicsDriven())
		{
			OwnerActor->SetActorLocation(Simulator.ToWorldLocation(State.Position), false);
			Ship->SetCosmosVelocity(ToWorldVelocityScaled(State.Velocity));
			const float MassKg = float(State.Mass * CosmosUnits::SolarMassToKilogram);
			if (Ship->SetPhysicsDriven(true, MassKg))
			{
				State.bPhysicsDriven = true;
				UE_LOG(LogTemp, Display, TEXT("[Ship] 刚体物理开启（质量 %.0f kg）"), MassKg);
			}
			else
			{
				UE_LOG(LogTemp, Error, TEXT("[Ship] 刚体不可用，保持求解器驱动"));
			}
		}
		else if (!bWantPhysics && Ship->IsPhysicsDriven())
		{
			Ship->SetPhysicsDriven(false, 0.f);
			State.bPhysicsDriven = false;
			UE_LOG(LogTemp, Display, TEXT("[Ship] 刚体物理关闭，回求解器驱动"));
		}
	}
```

在 `ApplyControlledBodyForces(StepRealSeconds);` 所在 `if (NumSteps > 0)` 块**之后**加：

```cpp
	ApplyShipPhysicsForces();
```

- [ ] **Step 4: 船的求解器接触永久关闭**

`RefreshShipContactProbes()` 整个函数体替换成：

```cpp
void UCosmosSimulationSubsystem::RefreshShipContactProbes()
{
	// 船的接触不归求解器了：1× 走 UE 刚体，其它倍率按要求直接关碰撞。
	// 清空探针并把 Radius 置 0，球-球与探针两条分支都不会触发。
	for (UCosmosOrbitComponent* Component : Bodies)
	{
		AActor* OwnerActor = Component ? Component->GetOwner() : nullptr;
		if (!OwnerActor || Component->SimulatorIndex == INDEX_NONE
			|| Component->SimulatorIndex >= Simulator.GetNumBodies())
		{
			continue;
		}

		if (!Cast<ICosmosBody>(OwnerActor))
		{
			continue;
		}

		FCosmosBodyState& State = Simulator.GetBodyMutable(Component->SimulatorIndex);
		State.SetContactProbes({});
		State.Radius = 0.0;
	}
}
```

- [ ] **Step 5: 写回跳过物理驱动的船**

`Tick` 的写回循环里，`State.bRigidBody` 分支改成：

```cpp
			if (State.bRigidBody)
			{
				const FQuat WorldOrientation(
					RenderOrientation.X, RenderOrientation.Y,
					RenderOrientation.Z, RenderOrientation.W);
				if (State.bPhysicsDriven)
				{
					// 位移归 Chaos，这里只写姿态（第一版不做力矩）。
					BodyActor->SetActorRotation(WorldOrientation, ETeleportType::None);
				}
				else
				{
					BodyActor->SetActorTransform(
						FTransform(WorldOrientation,
							Simulator.ToWorldLocation(RenderPosition),
							BodyActor->GetActorScale3D()),
						/*bSweep=*/false, nullptr, ETeleportType::None);
				}
			}
```

同一个循环末尾的 `Ship->SetCosmosVelocity(...)` 改成：

```cpp
			// 物理驱动的船速度由刚体自己答，不再用求解器快照覆盖。
			if (ICosmosBody* Ship = Cast<ICosmosBody>(BodyActor); Ship && !State.bPhysicsDriven)
			{
				Ship->SetCosmosVelocity(ToWorldVelocityScaled(State.Velocity));
			}
```

- [ ] **Step 6: 控制器通知倍率**

`AStellarEraPlayerController::AdjustTimeScale` 末尾（`Cosmos->TimeScale = Base * double(TimeScaleMultiplier);` 之后）加：

```cpp
	Cosmos->SetTimeScaleMultiplier(TimeScaleMultiplier);
```

- [ ] **Step 7: 编译**

```bash
cd /d/Temp && "D:/Application/Epic Games/UE_5.8/Engine/Binaries/ThirdParty/DotNet/10.0/win-x64/dotnet.exe" "D:/Application/Epic Games/UE_5.8/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.dll" StellarEraEditor Win64 Development -Project="F:/workspace/ue/StellarEra/StellarEra.uproject" -WaitMutex -FromMsBuild
```

预期：`Result: Succeeded`。

- [ ] **Step 8: 提交**

```bash
git add Source/StellarEraCosmos/Src/CosmosSimulationSubsystem.h Source/StellarEraCosmos/Src/CosmosSimulationSubsystem.cpp Source/StellarEra/Src/StellarEraPlayerController.cpp
git commit -m "feat(宇宙): 子系统接管刚体模式——读回、施力、写回取舍与 1× 切换" -- Source/StellarEraCosmos/Src/CosmosSimulationSubsystem.h Source/StellarEraCosmos/Src/CosmosSimulationSubsystem.cpp Source/StellarEra/Src/StellarEraPlayerController.cpp
```

---

### Task 4: 验证（PIE 行为核对）

**Files:** 无代码改动；产出结论。

- [ ] **Step 1: 跑自动化测试**

```bash
"D:/Application/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "F:/workspace/ue/StellarEra/StellarEra.uproject" -ExecCmds="Automation RunTests StellarEra.Cosmos" -TestExit="Automation Test Queue Empty" -unattended -nopause -nullrhi -nosplash -stdout -abslog="D:/Temp/ship-physics-tests.log"
```

预期：Cosmos 全部通过。

- [ ] **Step 2: 启动编辑器与 PIE**

编辑器必须在交互会话（console 会话）里启动。PIE 进 `UniverseMap`，观察：

1. 日志有 `[Ship] 刚体物理开启`；
2. HUD 速度读数不为 0、飞船不被行星甩下；
3. 松开推力自由落体（速度持续增大）；
4. 按 `-` 切到非 1×：日志 `[Ship] 刚体物理关闭`，飞船恢复瞬移；按 `+` 回 1×：日志重新开启、位置不跳变。

- [ ] **Step 3: 记录落地现象（不承诺稳定）**

让飞船自由落体撞向行星，截图或日志记录：是否停在网格上、是否抖动、是否有 `LogPhysics` 报错。已知边界写在 spec 里，本轮不作为失败。

- [ ] **Step 4: 提交验证记录**

把结论追加到 spec 末尾的「验证结果」节（有现象贴日志关键行）：

```bash
git add docs/superpowers/specs/2026-11-26-ship-force-driven-design.md
git commit -m "docs(飞船): 刚体驱动首轮 PIE 验证记录" -- docs/superpowers/specs/2026-11-26-ship-force-driven-design.md
```

---

## 自检

- Spec 覆盖：状态标记（T1）、刚体设置/施力（T2）、读回/写回取舍/模式切换/控制器通知（T3）、接触关闭（T3 Step 4）、验证（T4）。姿态不做力矩、浮动原点、碰撞窗口按 spec 明确不做。
- 类型一致：`bPhysicsDriven` 在 `FCosmosBodyState` 与 `ASpaceshipPawn` 各有一份（前者求解器跳过，后者刚体开关），名字相同但作用域不同；接口签名在 T2/T3 一致。
- 占位符：无。
