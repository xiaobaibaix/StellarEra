# 飞船凸包接触地形 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 让飞船与地形的接触形状从「半径 300 cm 的球」变成「Hull 网格的 12 个凸包顶点」。

**Architecture:** 地形接触发生在 `FNBodySimulator::ResolveContacts`，不在 UE 物理里。给 `FCosmosBodyState` 加一组「相对天体中心的接触探针」，求解器遇到带探针的配对时改用「最深探针 vs 对方球体」解算；探针由 `ASpaceshipPawn` 从 Hull 的 Simple Collision 凸包生成，子系统每帧转到 AU 坐标写入。

**Tech Stack:** UE 5.8.3 C++、UBT、Automation Test（editor world）。

**Spec:** 无独立 spec —— 设计在 2026-10-20 会话中经用户批准（对话记录）。核心约束：撞地行为沿用现有 `bZeroVelocityOnContact`（撞上即停、随行星一起动）。

## Global Constraints

- 只改四个项目模块内的文件；`Plugins/VoxelFree`、`Plugins/VibeUE` 不动。
- `StellarEraVehicle` 不能依赖 `StellarEra`；`StellarEraCore` 不依赖任何 Actor/引擎资产（本计划只加纯数学字段）。
- 含中文的 `.h`/`.cpp` 必须 UTF-8 with BOM；不要统一换行符。
- 提交 pathspec 挂在 commit 上：`git commit -- <paths>`。
- 构建前关闭编辑器；构建用捆绑 dotnet 直调 UBT，cwd 在项目外（见下）。
- 测试结果以日志里 `Result={Fail}` 为零为准，退出码 0 不算通过。
- 坐标系：探针在 Actor 本地（厘米，已含 Hull 相对变换与缩放）；子系统按 Actor 变换转世界，再除以 `CentimetersPerAu`。

## 构建 / 测试命令（本 worktree）

构建（编辑器必须关闭）：

```bash
env "D:/Application/Epic Games/UE_5.8/Engine/Binaries/ThirdParty/DotNet/10.0/win-x64/dotnet.exe" \
  "D:/Application/Epic Games/UE_5.8/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.dll" \
  StellarEraEditor Win64 Development \
  -Project="F:/workspace/ue/StellarEra/.claude/worktrees/ship-convex-collision/StellarEra.uproject" \
  -WaitMutex -FromMsBuild
```

测试（编辑器开着也能跑）：

```bash
env "D:/Application/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" \
  "F:/workspace/ue/StellarEra/.claude/worktrees/ship-convex-collision/StellarEra.uproject" \
  -ExecCmds="Automation RunTests StellarEra" \
  -TestExit="Automation Test Queue Empty" \
  -unattended -nopause -nullrhi -nosplash -stdout -abslog="C:/Users/23080/AppData/Local/Temp/StellarEra_worktree_tests.log"
```

---

### Task 1: Core —— 接触探针与探针接触解算

**Files:**
- Modify: `Source/StellarEraCore/Public/NBodySimulator.h`（`FCosmosBodyState` 与 `FNBodySimulator` 私有声明）
- Modify: `Source/StellarEraCore/Private/NBodySimulator.cpp`（`ResolveContacts` + 新函数 `ResolveProbeContact`）
- Test: `Source/StellarEraCore/Private/NBodySimulatorTests.cpp`（追加 `StellarEra.Core.NBodyProbeContact`）

**Interfaces:**
- Produces:
  - `FCosmosBodyState::ContactProbes`（`TArray<FVector3d>`，相对天体中心，求解器单位）
  - `FCosmosBodyState::ContactProbeRadius`（`double`，只读，由 SetContactProbes 维护）
  - `FCosmosBodyState::SetContactProbes(TArray<FVector3d> InProbes)`
  - `FNBodySimulator::ResolveProbeContact(int32 ProbeIndex, int32 SphereIndex)`（private）

- [ ] **Step 1: 在 `FCosmosBodyState` 里加探针字段与 setter**

加在 `Radius` 字段之后（`NBodySimulator.h`）：

```cpp
	/**
	 * 接触探针（相对天体中心，求解器单位）。非空时**取代** Radius 的球-球接触：
	 * 每个探针按一个点与对方球体求穿透，取最深的一个把天体推出去。
	 *
	 * 用途是飞船：整船若只用一个半径 300 cm 的球，机头/机尾与地形的接触位置
	 * 全是错的。探针由 ASpaceshipPawn 从 Hull 的 Simple Collision 凸包顶点生成，
	 * 子系统每帧转成求解器坐标写入。
	 *
	 * 空数组 = 按 Radius 做球-球接触（行星与所有非飞船天体都走这一支）。
	 */
	TArray<FVector3d> ContactProbes;

	/** 最远探针到中心的距离。由 SetContactProbes 维护，用于接触前的快速排除。 */
	double ContactProbeRadius = 0.0;

	/** 写入探针并同步 ContactProbeRadius。传空数组即退回球-球接触。 */
	void SetContactProbes(TArray<FVector3d> InProbes)
	{
		ContactProbes = MoveTemp(InProbes);
		ContactProbeRadius = 0.0;
		for (const FVector3d& Probe : ContactProbes)
		{
			ContactProbeRadius = FMath::Max(ContactProbeRadius, Probe.Size());
		}
	}
```

- [ ] **Step 2: 在 `ResolveContacts` 里分流到探针分支**

`NBodySimulator.cpp` 的 `ResolveContacts` 内层循环开头（`ContactDistance` 之前）加：

```cpp
			const bool bProbeI = Bodies[i].ContactProbes.Num() > 0;
			const bool bProbeJ = Bodies[j].ContactProbes.Num() > 0;

			// 探针接触：一方是凸包探针、另一方是球体。双方都带探针（凸包 vs 凸包）
			// 目前不存在，退回球-球分支 —— 那时 Radius 必须由调用方给对。
			if (bProbeI != bProbeJ)
			{
				const int32 ProbeIndex = bProbeI ? i : j;
				const int32 SphereIndex = bProbeI ? j : i;
				if (Bodies[SphereIndex].Radius > 0.0)
				{
					ResolveProbeContact(ProbeIndex, SphereIndex);
				}
				// 对方是点质量：没有任何可接触的表面，跳过。
				continue;
			}
```

- [ ] **Step 3: 实现 `ResolveProbeContact`**

加在 `ResolveContacts` 之后（同一个 .cpp），并在头文件 private 区 `void ResolveContacts();` 旁声明：

```cpp
void FNBodySimulator::ResolveProbeContact(int32 ProbeIndex, int32 SphereIndex)
{
	FCosmosBodyState& ProbeBody = Bodies[ProbeIndex];
	FCosmosBodyState& SphereBody = Bodies[SphereIndex];

	const double SphereRadius = SphereBody.Radius;

	// 快速排除：中心距 ≥ 球半径 + 最远探针时，不可能有探针进入球内。
	const double CenterDistance = (ProbeBody.Position - SphereBody.Position).Size();
	if (CenterDistance >= SphereRadius + ProbeBody.ContactProbeRadius)
	{
		return;
	}

	// 找最深穿透的探针。法线取「球心 → 探针」，即从对方表面朝外。
	double DeepestPenetration = 0.0;
	FVector3d Normal = FVector3d(1.0, 0.0, 0.0);

	for (const FVector3d& Probe : ProbeBody.ContactProbes)
	{
		const FVector3d ProbeDelta = ProbeBody.Position + Probe - SphereBody.Position;
		const double Distance = ProbeDelta.Size();
		const double Penetration = SphereRadius - Distance;
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

	// ① 位置：推开到最深探针刚好落在球面上。与球-球分支同样的让路比例。
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

- [ ] **Step 4: 写测试 `StellarEra.Core.NBodyProbeContact`**

追加到 `NBodySimulatorTests.cpp`（文件已含 `#if WITH_DEV_AUTOMATION_TESTS`）：

```cpp
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraNBodyProbeContactTest,
	"StellarEra.Core.NBodyProbeContact",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraNBodyProbeContactTest::RunTest(const FString& Parameters)
{
	// 几何：球体半径 0.1 在原点（质量 3），探针体质量 1 在 x=0.15。
	// 探针偏移 -0.06 落在 x=0.09，穿进球内 0.01；偏移 -0.08 落在 x=0.07，穿 0.03。
	// 深者胜：位移必须由 -0.08 那个探针决定。
	{
		FNBodySimulator Simulator;
		Simulator.SetUnitSystem(ECosmosUnitSystem::Game);
		Simulator.SetIntegrator(ECosmosIntegrator::VelocityVerlet);
		Simulator.SetSofteningLength(1.0e-5);

		FCosmosBodyState Sphere;
		Sphere.Mass = 3.0;
		Sphere.Radius = 0.1;
		Sphere.Position = FVector3d::ZeroVector;
		Simulator.AddBody(Sphere);

		FCosmosBodyState ProbeBody;
		ProbeBody.Mass = 1.0;
		ProbeBody.Position = FVector3d(0.15, 0.0, 0.0);
		TArray<FVector3d> Probes;
		Probes.Add(FVector3d(-0.06, 0.0, 0.0));
		Probes.Add(FVector3d(-0.08, 0.0, 0.0));
		ProbeBody.SetContactProbes(MoveTemp(Probes));
		Simulator.AddBody(ProbeBody);

		TestEqual(TEXT("ContactProbeRadius = 0.08"),
			Simulator.GetBody(1).ContactProbeRadius, 0.08, 1.0e-15);

		Simulator.StepExact(0.0);

		// 穿透 0.03，让路比例：探针体 3/4、球体 1/4。
		TestEqual(TEXT("探针体让 3/4 的穿透量"),
			Simulator.GetBody(1).Position.X, 0.1725, 1.0e-15);
		TestEqual(TEXT("球体反向让 1/4"),
			Simulator.GetBody(0).Position.X, -0.0075, 1.0e-15);
		TestEqual(TEXT("最深探针恰好落在球面上"),
			(Simulator.GetBody(1).Position + FVector3d(-0.08, 0.0, 0.0)
				- Simulator.GetBody(0).Position).Size(), 0.1, 1.0e-15);
	}

	// 空探针 = 退回球-球：半径 0 的点质量即便中心距 0.15 > 0.1 也不会接触。
	{
		FNBodySimulator Simulator;
		Simulator.SetUnitSystem(ECosmosUnitSystem::Game);
		Simulator.SetIntegrator(ECosmosIntegrator::VelocityVerlet);
		Simulator.SetSofteningLength(1.0e-5);

		FCosmosBodyState Sphere;
		Sphere.Mass = 3.0;
		Sphere.Radius = 0.1;
		Simulator.AddBody(Sphere);

		FCosmosBodyState PointMass;
		PointMass.Mass = 1.0;
		PointMass.Position = FVector3d(0.15, 0.0, 0.0);
		Simulator.AddBody(PointMass);

		Simulator.StepExact(0.0);

		TestEqual(TEXT("无探针、半径为 0：不接触"),
			Simulator.GetBody(1).Position.X, 0.15, 1.0e-15);
		TestEqual(TEXT("球体也不动"), Simulator.GetBody(0).Position.X, 0.0, 1.0e-15);
	}

	// 撞上即停：探针体带 bZeroVelocityOnContact 时，整条速度钉到球体上。
	{
		FNBodySimulator Simulator;
		Simulator.SetUnitSystem(ECosmosUnitSystem::Game);
		Simulator.SetIntegrator(ECosmosIntegrator::VelocityVerlet);
		Simulator.SetSofteningLength(1.0e-5);

		FCosmosBodyState Sphere;
		Sphere.Mass = 3.0;
		Sphere.Radius = 0.1;
		Sphere.Velocity = FVector3d(0.0, 0.002, 0.0);
		Simulator.AddBody(Sphere);

		FCosmosBodyState ProbeBody;
		ProbeBody.Mass = 1.0;
		ProbeBody.Position = FVector3d(0.15, 0.0, 0.0);
		ProbeBody.Velocity = FVector3d(-0.01, 0.0, 0.0);
		ProbeBody.bZeroVelocityOnContact = true;
		TArray<FVector3d> Probes;
		Probes.Add(FVector3d(-0.06, 0.0, 0.0));
		ProbeBody.SetContactProbes(MoveTemp(Probes));
		Simulator.AddBody(ProbeBody);

		Simulator.StepExact(0.0);

		TestEqual(TEXT("撞上即停：速度 = 球体速度（X）"),
			Simulator.GetBody(1).Velocity.X, 0.0, 1.0e-15);
		TestEqual(TEXT("撞上即停：速度 = 球体速度（Y）"),
			Simulator.GetBody(1).Velocity.Y, 0.002, 1.0e-15);
	}

	return true;
}
```

- [ ] **Step 5: 构建并跑测试**

构建 + 测试命令见上。期望：`StellarEra.Core.NBodyProbeContact` `Result={Success}`，全量日志无 `Result={Fail}`。

- [ ] **Step 6: 提交**

```bash
git add Source/StellarEraCore/Public/NBodySimulator.h Source/StellarEraCore/Private/NBodySimulator.cpp Source/StellarEraCore/Private/NBodySimulatorTests.cpp
git commit -m "feat(N体): 接触支持凸包探针，取代飞船的单一球体" -- Source/StellarEraCore/Public/NBodySimulator.h Source/StellarEraCore/Private/NBodySimulator.cpp Source/StellarEraCore/Private/NBodySimulatorTests.cpp
```

---

### Task 2: Vehicle —— 从 Hull 凸包生成接触探针

**Files:**
- Modify: `Source/StellarEraVehicle/Public/SpaceshipPawn.h`
- Modify: `Source/StellarEraVehicle/Private/SpaceshipPawn.cpp`
- Test: `Source/StellarEraVehicle/Private/SpaceshipCollisionTests.cpp`（追加 `StellarEra.Vehicle.ShipContactProbes`）

**Interfaces:**
- Consumes: 无 —— 本模块只用 `FVector`。
- Produces:
  - `bool ASpaceshipPawn::BuildContactProbes(TArray<FVector>& OutProbes) const`（public，纯函数）
  - `const TArray<FVector>& ASpaceshipPawn::GetContactProbesLocalCm() const`
  - `void ASpaceshipPawn::RebuildContactProbes()`（protected）

- [ ] **Step 1: 头文件加接口**

`SpaceshipPawn.h` public 区（`GetContactRadiusCm` 附近）：

```cpp
	/**
	 * 从 Hull 当前的网格算出接触探针（Actor 本地坐标，厘米）。
	 *
	 * 取网格 Simple Collision 里每个凸包的顶点，按「凸包元素变换 × Hull 相对变换」
	 * 搬到 Actor 本地，再按 10 cm 网格去重。不写成员 —— 测试可以直接调它。
	 *
	 * @return false 表示 Hull 没有网格或网格没有 Simple Collision。
	 */
	bool BuildContactProbes(TArray<FVector>& OutProbes) const;

	/**
	 * 接触探针（Actor 本地，厘米）。空数组 = 没有可用的凸包，求解器退回 300 cm 球。
	 * 由 BeginPlay / OnConstruction 在套用 HullMesh 之后重建。
	 */
	const TArray<FVector>& GetContactProbesLocalCm() const { return ContactProbesLocalCm; }
```

protected 区：

```cpp
	/** 重建 ContactProbesLocalCm。Hull 网格或变换变化后调用。 */
	void RebuildContactProbes();

	/** BuildContactProbes 的结果缓存。 */
	TArray<FVector> ContactProbesLocalCm;
```

- [ ] **Step 2: 实现**

`SpaceshipPawn.cpp` 顶部 include：

```cpp
#include "Engine/StaticMesh.h"
#include "PhysicsEngine/BodySetup.h"
#include "PhysicsEngine/ConvexElem.h"
```

实现（放在 `GetContactRadiusCm` 附近）：

```cpp
bool ASpaceshipPawn::BuildContactProbes(TArray<FVector>& OutProbes) const
{
	OutProbes.Reset();

	const UStaticMesh* Mesh = Hull ? Hull->GetStaticMesh() : nullptr;
	const UBodySetup* BodySetup = Mesh ? Mesh->GetBodySetup() : nullptr;
	if (!BodySetup)
	{
		return false;
	}

	// 凸包顶点存的是网格空间坐标，每个元素还可能带自己的变换；再乘 Hull 相对根组件
	// 的变换（含蓝图里拖的缩放），才是 Actor 本地坐标。
	const FTransform HullToLocal = Hull->GetRelativeTransform();

	for (const FKConvexElem& Elem : BodySetup->AggGeom.ConvexElems)
	{
		const FTransform ElemToLocal = Elem.GetTransform() * HullToLocal;
		for (const FVector& Vertex : Elem.VertexData)
		{
			OutProbes.Add(ElemToLocal.TransformPosition(Vertex));
		}
	}

	if (OutProbes.Num() == 0)
	{
		return false;
	}

	// 12 个凸包共几百个顶点，接触只需要形状：按 10 cm 网格去重。
	// 船体 50 m 量级下 10 cm 的误差远小于地形体素边长。
	constexpr double BucketCm = 10.0;
	TSet<FIntVector> Seen;
	TArray<FVector> Unique;
	Unique.Reserve(OutProbes.Num());

	for (const FVector& Probe : OutProbes)
	{
		const FIntVector Key(
			FMath::RoundToInt(Probe.X / BucketCm),
			FMath::RoundToInt(Probe.Y / BucketCm),
			FMath::RoundToInt(Probe.Z / BucketCm));

		if (Seen.Contains(Key))
		{
			continue;
		}
		Seen.Add(Key);
		Unique.Add(Probe);
	}

	OutProbes = MoveTemp(Unique);
	return true;
}

void ASpaceshipPawn::RebuildContactProbes()
{
	TArray<FVector> Probes;
	if (BuildContactProbes(Probes))
	{
		ContactProbesLocalCm = MoveTemp(Probes);
		UE_LOG(LogTemp, Display, TEXT("[Ship] 接触探针 %d 个"),
			ContactProbesLocalCm.Num());
	}
	else
	{
		ContactProbesLocalCm.Reset();
		UE_LOG(LogTemp, Warning,
			TEXT("[Ship] Hull 没有 Simple Collision，接触退回 300 cm 球"));
	}
}
```

在 `OnConstruction` 与 `BeginPlay` 里 `ApplyHullMesh()` 之后各加一行 `RebuildContactProbes();`。

- [ ] **Step 3: 追加测试**

`SpaceshipCollisionTests.cpp` 追加：

```cpp
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraShipContactProbesTest,
	"StellarEra.Vehicle.ShipContactProbes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraShipContactProbesTest::RunTest(const FString& Parameters)
{
	UClass* ShipClass = LoadClass<ASpaceshipPawn>(
		nullptr, TEXT("/Game/Code/BP_SpaceshipPawn.BP_SpaceshipPawn_C"));
	TestNotNull(TEXT("BP_SpaceshipPawn_C 可加载"), ShipClass);
	if (!ShipClass)
	{
		return false;
	}

	ASpaceshipPawn* CDO = Cast<ASpaceshipPawn>(ShipClass->GetDefaultObject());
	TestNotNull(TEXT("BP_SpaceshipPawn CDO"), CDO);
	if (!CDO)
	{
		return false;
	}

	TArray<FVector> Probes;
	TestTrue(TEXT("能从 Hull 凸包生成接触探针"), CDO->BuildContactProbes(Probes));
	TestTrue(TEXT("探针数量 > 0"), Probes.Num() > 0);
	if (Probes.Num() == 0)
	{
		return false;
	}

	// 结构断言而不是数字断言：每个探针都必须落在「Hull 网格包围盒 × Hull 相对变换」
	// 里。网格换了形状测试照样有效，只有探针变换接错时才会红。
	const UStaticMeshComponent* Hull = Cast<UStaticMeshComponent>(
		CDO->GetDefaultSubobjectByName(TEXT("Hull")));
	TestNotNull(TEXT("船体组件 Hull"), Hull);
	if (!Hull || !Hull->GetStaticMesh())
	{
		return false;
	}

	const FBox ExpectedBox =
		Hull->GetStaticMesh()->GetBoundingBox().TransformBy(Hull->GetRelativeTransform());

	bool bAllInside = true;
	for (const FVector& Probe : Probes)
	{
		if (!ExpectedBox.IsInsideOrOn(Probe) &&
			FVector::DistSquared(ExpectedBox.GetClosestPointTo(Probe), Probe) > 1.0)
		{
			bAllInside = false;
			break;
		}
	}
	TestTrue(TEXT("所有探针都在 Hull 包围盒内"), bAllInside);

	// 船体腹部在根组件下方（本资源约 -343 cm）：全部探针都在根之上说明
	// 相对变换漏乘了 Hull 的位置。
	double MinZ = TNumericLimits<double>::Max();
	for (const FVector& Probe : Probes)
	{
		MinZ = FMath::Min(MinZ, Probe.Z);
	}
	TestTrue(TEXT("探针覆盖到船体腹部（Z < 0）"), MinZ < 0.0);

	return true;
}
```

- [ ] **Step 4: 构建并跑测试**

期望：`StellarEra.Vehicle.ShipContactProbes` `Result={Success}`，日志里能看到 `[Ship] 接触探针 N 个`。

- [ ] **Step 5: 提交**

```bash
git add Source/StellarEraVehicle/Public/SpaceshipPawn.h Source/StellarEraVehicle/Private/SpaceshipPawn.cpp Source/StellarEraVehicle/Private/SpaceshipCollisionTests.cpp
git commit -m "feat(飞船): 从 Hull 凸包生成接触探针" -- Source/StellarEraVehicle/Public/SpaceshipPawn.h Source/StellarEraVehicle/Private/SpaceshipPawn.cpp Source/StellarEraVehicle/Private/SpaceshipCollisionTests.cpp
```

---

### Task 3: StellarEra —— 每帧把探针送进求解器

**Files:**
- Modify: `Source/StellarEra/Public/CosmosSimulationSubsystem.h`（`RefreshPlanetContactRadii` 旁加声明）
- Modify: `Source/StellarEra/Private/CosmosSimulationSubsystem.cpp`（新函数 + Tick 里调用 + `AddBodyToSimulator` 注释）

**Interfaces:**
- Consumes: `ASpaceshipPawn::GetContactProbesLocalCm()`、`FCosmosBodyState::SetContactProbes`、`FNBodySimulator::GetBodyMutable`、成员 `CentimetersPerAu`。
- Produces: `void UCosmosSimulationSubsystem::RefreshShipContactProbes();`

- [ ] **Step 1: 头文件声明**

`CosmosSimulationSubsystem.h` 中 `RefreshPlanetContactRadii();` 之后：

```cpp
	/**
	 * 把飞船的接触探针（Actor 本地厘米）转成求解器坐标写进天体状态。
	 * 没有探针（网格没导碰撞）时退回 300 cm 球接触 —— Radius 回到旧值。
	 * 与 RefreshPlanetContactRadii 一样，必须在推进之前调用。
	 */
	void RefreshShipContactProbes();
```

- [ ] **Step 2: 实现**

`CosmosSimulationSubsystem.cpp`，放在 `RefreshPlanetContactRadii` 之后：

```cpp
void UCosmosSimulationSubsystem::RefreshShipContactProbes()
{
	for (UCosmosOrbitComponent* Component : Bodies)
	{
		ASpaceshipPawn* Ship = Component ? Cast<ASpaceshipPawn>(Component->GetOwner()) : nullptr;
		if (!Ship || Component->SimulatorIndex == INDEX_NONE)
		{
			continue;
		}

		FCosmosBodyState& State = Simulator.GetBodyMutable(Component->SimulatorIndex);

		const TArray<FVector>& LocalProbes = Ship->GetContactProbesLocalCm();
		if (LocalProbes.Num() == 0)
		{
			// 网格没有凸包：退回改动前的 300 cm 球，不能什么都不接触。
			State.SetContactProbes({});
			State.Radius = double(Ship->GetContactRadiusCm()) / CentimetersPerAu;
			continue;
		}

		// 探针是 Actor 本地坐标：先按 Actor 旋转/缩放转成「相对中心的世界厘米」，
		// 再除以 CentimetersPerAu 进入求解器。位置不需要（探针本来就相对中心）。
		const FTransform ShipTransform = Ship->GetActorTransform();

		TArray<FVector3d> Probes;
		Probes.Reserve(LocalProbes.Num());
		for (const FVector& LocalProbe : LocalProbes)
		{
			const FVector RelativeCm = ShipTransform.TransformVector(LocalProbe);
			Probes.Add(FVector3d(RelativeCm) / CentimetersPerAu);
		}

		State.SetContactProbes(MoveTemp(Probes));

		// 探针取代球体：Radius 必须归零，否则 300 cm 球会先触发、把船提前顶起来。
		State.Radius = 0.0;
	}
}
```

- [ ] **Step 3: Tick 里调用**

在 `RefreshPlanetContactRadii();` 下一行加：

```cpp
	RefreshShipContactProbes();
```

并把上方注释里的「接触半径」扩成同时覆盖探针（保持原有解释，补一句探针在推进前写入）。

`AddBodyToSimulator` 飞船分支的注释改为：首帧兜底半径 300，Tick 里 `RefreshShipContactProbes` 会换成凸包探针并把半径归零。

- [ ] **Step 4: 构建**

只需构建通过；本任务没有新增自动化测试（子系统需要 World，editor world 测试拿不到 PIE 中的飞船实例）。运行期验证放在 Task 4。

- [ ] **Step 5: 提交**

```bash
git add Source/StellarEra/Public/CosmosSimulationSubsystem.h Source/StellarEra/Private/CosmosSimulationSubsystem.cpp
git commit -m "feat(N体): 每帧把飞船凸包探针写进求解器" -- Source/StellarEra/Public/CosmosSimulationSubsystem.h Source/StellarEra/Private/CosmosSimulationSubsystem.cpp
```

---

### Task 4: 全量构建 + 全量自动化 + 收尾

**Files:** 无新增。

- [ ] **Step 1: 全量构建**

命令见上。要求日志 `Result: Succeeded`。

- [ ] **Step 2: 全量自动化**

命令见上。要求：`Result={Fail}` 零条（用 Grep 查，不只看退出码），且下列测试都在：

- `StellarEra.Core.NBodyProbeContact`
- `StellarEra.Vehicle.ShipContactProbes`
- `StellarEra.Vehicle.SpaceshipCollision`
- 其余既有 `StellarEra.*`

- [ ] **Step 3: 记录待人工 PIE 验证项**

代码能证明的到此为止（探针生成正确、Core 解算正确、子系统接线正确）。以下只能在 `UniverseMap` PIE 里看：

- 飞船贴地时机腹（而不是 300 cm 球心）接触地形；
- 撞地后速度归零、随行星一起动。

在最终报告里如实写明「未做 PIE 目视验证」，不把它算作已验证。

- [ ] **Step 4: 提交计划文档**

```bash
git add docs/superpowers/plans/2026-10-20-ship-hull-terrain-contact.md
git commit -m "docs(计划): 飞船凸包接触地形实施计划" -- docs/superpowers/plans/2026-10-20-ship-hull-terrain-contact.md
```
