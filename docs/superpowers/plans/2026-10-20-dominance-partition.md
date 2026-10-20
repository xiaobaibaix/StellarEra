# 瞬时引力主导剖分 实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 在星图/编辑器里画出任意天体数、任意质量比都成立的「此刻谁的引力最强」边界（Apollonius 球/中垂面），覆盖等质量三星系统。

**Architecture:** 不改 N 体驱动。给 `UCosmosTrajectoryComponent` 增加两个静态纯函数（边界几何、前两名裁剪）和一个绘制函数；绘制复用现有 `ULineBatchComponent`、星图门控与 `DrawInEditor` 离线路径。每对天体只由较重一方绘制，等重按 Actor 名去重。

**Tech Stack:** Unreal Engine 5.8 C++（double / LWC）、`ULineBatchComponent`、UE Automation Test。

**Spec:** `docs/superpowers/specs/2026-10-20-dominance-partition-design.md`

## Global Constraints

- 只改这三个文件：
  - `Source/StellarEra/Public/CosmosTrajectoryComponent.h`
  - `Source/StellarEra/Private/CosmosTrajectoryComponent.cpp`
  - `Source/StellarEra/Private/CosmosTrajectoryComponentTests.cpp`
- 不碰 `Plugins/VoxelFree`、`Plugins/VibeUE`；不碰 `Content/` 下任何资产；**不提交、不推送**（用户未要求）。完成后只报告验证证据。
- `.h`/`.cpp` 含中文，必须保持 **UTF-8 with BOM**；只做局部插入，不要整文件重存、不要统一换行符。
- 纯函数只做几何，不做单位换算：世界坐标单位 cm，质量单位 M⊕（只用作比值，单位抵消）。
- 构建前必须关闭编辑器；用捆绑 dotnet 直调 UBT，cwd 在项目外（`$TEMP`）：
  ```bash
  "D:/Application/Epic Games/UE_5.8/Engine/Binaries/ThirdParty/DotNet/10.0/win-x64/dotnet.exe" \
    "D:/Application/Epic Games/UE_5.8/Engine/Binaries/DOTNET/UnrealBuildTool/UnrealBuildTool.dll" \
    StellarEraEditor Win64 Development \
    -Project="F:/workspace/ue/StellarEra/StellarEra.uproject" -WaitMutex -FromMsBuild
  ```
- 自动化测试（编辑器开着也能跑）：
  ```bash
  "D:/Application/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" \
    "F:/workspace/ue/StellarEra/StellarEra.uproject" \
    -ExecCmds="Automation RunTests StellarEra" \
    -TestExit="Automation Test Queue Empty" \
    -unattended -nopause -nullrhi -nosplash -stdout -abslog="$TEMP/StellarEra-DominanceTests.log"
  ```
- N 体只有一个驱动：本功能只读 Actor 当前位置与 `UCosmosOrbitComponent` 参数，不推进求解器、不写回位置。

---

## File Structure

| 文件 | 改动 |
|---|---|
| `CosmosTrajectoryComponent.h` | 2 个静态纯函数声明；3 组 UPROPERTY；2 个私有方法声明；`class AActor;` 前置声明 |
| `CosmosTrajectoryComponent.cpp` | `#include <limits>`；2 个纯函数实现；2 个绘制/去重实现；运行时 Tick 与 `DrawInEditor` 各接一行 |
| `CosmosTrajectoryComponentTests.cpp` | `StellarEra.Cosmos.DominanceBoundary`、`StellarEra.Cosmos.DominancePartition` 两个测试 |

---

### Task 1: Apollonius 边界几何 + 前两名裁剪（纯函数）

**Files:**
- Modify: `Source/StellarEra/Public/CosmosTrajectoryComponent.h:21-22`（前置声明）、`:70-71`（静态函数声明之后）
- Modify: `Source/StellarEra/Private/CosmosTrajectoryComponent.cpp:1-15`（include 区）、`:533`（`ComputeHillRadiusCm` 之后）
- Test: `Source/StellarEra/Private/CosmosTrajectoryComponentTests.cpp:188`（希尔球测试之后、`#endif` 之前）

**Interfaces:**
- Consumes: 无（只依赖 `FVector3d`、`TArray`）
- Produces:
  - `static bool ComputeDominanceBoundary(const FVector3d& PositionA, double MassA, const FVector3d& PositionB, double MassB, FVector3d& OutCenter, double& OutRadiusCm, FVector3d& OutNormal)`
  - `static bool ArePairDominantAt(const FVector3d& Point, const TArray<FVector3d>& Positions, const TArray<double>& Masses, int32 IndexA, int32 IndexB)`

- [ ] **Step 1: 在头文件加声明**

在 `class UCosmosOrbitComponent;` 下一行加：

```cpp
class AActor;
```

在 `ComputeHillRadiusCm` 声明（`double DistanceCm, double& OutRadiusCm);`）之后、`//~ Begin UActorComponent Interface` 之前加：

```cpp
	/**
	 * 两颗天体的瞬时引力主导边界：m_i/|P-r_i|² = m_j/|P-r_j|²，
	 * 即 |P-r_i|/|P-r_j| = sqrt(m_i/m_j)。
	 *
	 * 该轨迹是空间的 Apollonius 球：质量不等时是球（OutNormal 为轴方向），
	 * 等质量时退化为过中点的中垂面（OutRadiusCm == 0）。
	 * 不假设 m_i ≪ m_j，也不假设天体共线。
	 *
	 * 无效输入（非有限、非正质量、两点重合）返回 false，输出清零。
	 * 纯函数，自动化测试直接调用。
	 */
	static bool ComputeDominanceBoundary(const FVector3d& PositionA, double MassA,
		const FVector3d& PositionB, double MassB,
		FVector3d& OutCenter, double& OutRadiusCm, FVector3d& OutNormal);

	/**
	 * 点 Point 处，Positions/Masses 里的 i、j 是不是**拉得最狠的两颗**（并列算通过）。
	 *
	 * 只和 PairPull = max(g_i, g_j) 比即可：max(g_i, g_j) = PairPull 是恒等式，
	 * 所以「两人都不输给第三名」等价于「两人就是前两名」。
	 * 允许 1e-9 相对误差，避免采样点落在边界上时抖动。
	 */
	static bool ArePairDominantAt(const FVector3d& Point,
		const TArray<FVector3d>& Positions, const TArray<double>& Masses,
		int32 IndexA, int32 IndexB);
```

- [ ] **Step 2: 先写测试（红）**

在 `CosmosTrajectoryComponentTests.cpp` 的 `FStellarEraHillSphereTest` 之后、`#endif` 之前加：

```cpp
// 瞬时引力主导边界：|P-r_i| / |P-r_j| = sqrt(m_i/m_j) 的 Apollonius 球，
// 以及「这一对是不是点上的前两名」的裁剪规则。纯函数，不建世界。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraDominanceBoundaryTest,
	"StellarEra.Cosmos.DominanceBoundary",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraDominanceBoundaryTest::RunTest(const FString& Parameters)
{
	FVector3d Center;
	double RadiusCm = -1.0;
	FVector3d Normal;

	// 等质量：中垂面 —— 球心在中点、半径 0、法线沿 A→B。
	TestTrue(TEXT("等质量可算"),
		UCosmosTrajectoryComponent::ComputeDominanceBoundary(
			FVector3d(0.0, 0.0, 0.0), 1.0, FVector3d(10.0, 0.0, 0.0), 1.0,
			Center, RadiusCm, Normal));
	TestEqual(TEXT("等质量球心在中点 X"), Center.X, 5.0, 1.0e-9);
	TestEqual(TEXT("等质量半径为零（平面）"), RadiusCm, 0.0, 1.0e-9);
	TestEqual(TEXT("法线沿 A→B"), Normal.X, 1.0, 1.0e-9);

	// 质量比 4:1 → k=2：球心在 40/3，半径 20/3。
	TestTrue(TEXT("不等质量可算"),
		UCosmosTrajectoryComponent::ComputeDominanceBoundary(
			FVector3d::ZeroVector, 4.0, FVector3d(10.0, 0.0, 0.0), 1.0,
			Center, RadiusCm, Normal));
	TestEqual(TEXT("球心 X"), Center.X, 40.0 / 3.0, 1.0e-9);
	TestEqual(TEXT("半径"), RadiusCm, 20.0 / 3.0, 1.0e-9);

	// 球面上沿 +X 的点：到 A 距离 20、到 B 距离 10，引力比正好 4:1。
	TestEqual(TEXT("边界点两侧引力相等"),
		4.0 / (20.0 * 20.0), 1.0 / (10.0 * 10.0), 1.0e-15);

	// 第三体裁剪：C 远而轻 → 这对仍是前两名；C 压在边界点上 → 不是。
	const TArray<FVector3d> FarPositions = {
		FVector3d::ZeroVector, FVector3d(10.0, 0.0, 0.0), FVector3d(1000.0, 0.0, 0.0) };
	const TArray<double> FarMasses = { 4.0, 1.0, 1.0 };
	TestTrue(TEXT("第三颗很远时边界点仍归这对"),
		UCosmosTrajectoryComponent::ArePairDominantAt(
			FVector3d(20.0, 0.0, 0.0), FarPositions, FarMasses, 0, 1));

	const TArray<FVector3d> NearPositions = {
		FVector3d::ZeroVector, FVector3d(10.0, 0.0, 0.0), FVector3d(20.0, 0.0, 0.0) };
	const TArray<double> NearMasses = { 4.0, 1.0, 100.0 };
	TestFalse(TEXT("更重的第三颗把边界裁掉"),
		UCosmosTrajectoryComponent::ArePairDominantAt(
			FVector3d(20.0, 0.0, 0.0), NearPositions, NearMasses, 0, 1));

	TestFalse(TEXT("采样点压在天体上不算边界"),
		UCosmosTrajectoryComponent::ArePairDominantAt(
			FVector3d::ZeroVector, FarPositions, FarMasses, 0, 1));

	// 无效输入。
	TestFalse(TEXT("两点重合时拒绝"),
		UCosmosTrajectoryComponent::ComputeDominanceBoundary(
			FVector3d::ZeroVector, 1.0, FVector3d::ZeroVector, 1.0,
			Center, RadiusCm, Normal));
	TestFalse(TEXT("零质量拒绝"),
		UCosmosTrajectoryComponent::ComputeDominanceBoundary(
			FVector3d::ZeroVector, 0.0, FVector3d(10.0, 0.0, 0.0), 1.0,
			Center, RadiusCm, Normal));

	return true;
}
```

- [ ] **Step 3: 构建，确认是红**

运行 Global Constraints 里的构建命令。预期：编译/链接失败，报 `ComputeDominanceBoundary` / `ArePairDominantAt` 不是成员（声明已加、实现未加）。

- [ ] **Step 4: 实现纯函数**

在 `.cpp` 顶部 include 区（`#include "CoreMinimal.h"` 之后）加：

```cpp
#include <limits>
```

在 `ComputeHillRadiusCm` 的右花括号（第 533 行）之后、`ResolveHillSphereRadiusCm` 之前插入：

```cpp
bool UCosmosTrajectoryComponent::ComputeDominanceBoundary(const FVector3d& PositionA,
	double MassA, const FVector3d& PositionB, double MassB,
	FVector3d& OutCenter, double& OutRadiusCm, FVector3d& OutNormal)
{
	OutCenter = FVector3d::ZeroVector;
	OutRadiusCm = 0.0;
	OutNormal = FVector3d::ZeroVector;

	if (!FMath::IsFinite(PositionA.X) || !FMath::IsFinite(PositionA.Y)
		|| !FMath::IsFinite(PositionA.Z) || !FMath::IsFinite(PositionB.X)
		|| !FMath::IsFinite(PositionB.Y) || !FMath::IsFinite(PositionB.Z)
		|| !FMath::IsFinite(MassA) || !FMath::IsFinite(MassB)
		|| MassA <= 0.0 || MassB <= 0.0)
	{
		return false;
	}

	const FVector3d Delta = PositionB - PositionA;
	const double Distance = Delta.Size();
	if (!FMath::IsFinite(Distance) || Distance <= 0.0)
	{
		return false;
	}
	OutNormal = Delta / Distance;

	// k = sqrt(mA/mB)：边界上 |P-A|/|P-B| = k。
	const double K = FMath::Sqrt(MassA / MassB);
	const double Denominator = 1.0 - K * K;

	// 等质量：球半径发散，退化为中垂面。用 1e-9 把「数值上等重」也归进来，
	// 否则会得到一个半径约 1/|1-k²| 倍间距的巨球，采样全在裁剪球外，什么都画不出。
	if (FMath::Abs(Denominator) <= 1.0e-9)
	{
		OutCenter = (PositionA + PositionB) * 0.5;
		return true;
	}

	OutCenter = (PositionA - PositionB * (K * K)) / Denominator;
	OutRadiusCm = K * Distance / FMath::Abs(Denominator);

	if (!FMath::IsFinite(OutCenter.X) || !FMath::IsFinite(OutCenter.Y)
		|| !FMath::IsFinite(OutCenter.Z) || !FMath::IsFinite(OutRadiusCm))
	{
		OutCenter = FVector3d::ZeroVector;
		OutRadiusCm = 0.0;
		OutNormal = FVector3d::ZeroVector;
		return false;
	}
	return true;
}

bool UCosmosTrajectoryComponent::ArePairDominantAt(const FVector3d& Point,
	const TArray<FVector3d>& Positions, const TArray<double>& Masses,
	int32 IndexA, int32 IndexB)
{
	if (IndexA == IndexB || Positions.Num() != Masses.Num()
		|| !Positions.IsValidIndex(IndexA) || !Positions.IsValidIndex(IndexB))
	{
		return false;
	}
	if (Masses[IndexA] <= 0.0 || Masses[IndexB] <= 0.0)
	{
		return false;
	}

	const double DistanceA2 = (Point - Positions[IndexA]).SizeSquared();
	const double DistanceB2 = (Point - Positions[IndexB]).SizeSquared();
	if (DistanceA2 <= 0.0 || DistanceB2 <= 0.0)
	{
		return false;   // 采样点正好压在天体上，不在边界上。
	}

	const double PairPull = FMath::Max(Masses[IndexA] / DistanceA2,
		Masses[IndexB] / DistanceB2);
	if (!FMath::IsFinite(PairPull))
	{
		return false;
	}

	// 其余天体只需和 PairPull 比：两人都不输给第三名 ⇔ 两人就是前两名。
	constexpr double Tolerance = 1.0e-9;
	for (int32 Index = 0; Index < Positions.Num(); ++Index)
	{
		if (Index == IndexA || Index == IndexB || Masses[Index] <= 0.0)
		{
			continue;
		}
		const double Distance2 = (Point - Positions[Index]).SizeSquared();
		const double OtherPull = Distance2 > 0.0
			? Masses[Index] / Distance2
			: std::numeric_limits<double>::infinity();
		if (OtherPull > PairPull * (1.0 + Tolerance))
		{
			return false;
		}
	}
	return true;
}
```

- [ ] **Step 5: 构建 + 只跑这个测试**

先构建（预期 `Result: Succeeded`），再运行 `StellarEra` 全量测试（现有 22 个 + 新增 1 个）。预期：
`StellarEra.Cosmos.DominanceBoundary` 通过，总数 23，0 失败。

---

### Task 2: 剖分绘制、去重与接入

**Files:**
- Modify: `Source/StellarEra/Public/CosmosTrajectoryComponent.h:112-126`（开关）、`:147-149`（颜色）、`:204-207`（几何参数）、`:231-235`（私有方法）
- Modify: `Source/StellarEra/Private/CosmosTrajectoryComponent.cpp:274-280`（门控）、`:330`（运行时接入）、`:660`（绘制函数）、`:833`（编辑器接入）
- Test: `Source/StellarEra/Private/CosmosTrajectoryComponentTests.cpp`（Task 1 的测试之后）

**Interfaces:**
- Consumes: Task 1 的 `ComputeDominanceBoundary`、`ArePairDominantAt`
- Produces:
  - UPROPERTY：`bDrawDominancePartition`、`DominanceColor`、`DominanceClipRadiusCm`、`DominanceSphereSegments`、`DominanceSphereRings`、`DominancePlaneSegments`、`MinDominanceRadiusCm`
  - private：`bool ShouldDrawDominancePair(double SelfMassEarth, double OtherMassEarth, const AActor& SelfActor, const AActor& OtherActor) const;`
  - private：`void AppendDominancePartition(const FVector& Anchor, const FLinearColor& Color);`

- [ ] **Step 1: 在头文件加属性与私有方法**

在 `bDrawHillSphere` 的 UPROPERTY 块之后加：

```cpp
	/**
	 * 是否画**瞬时引力主导剖分**：任意天体数、任意质量比都成立的
	 * 「此刻谁的引力最强」边界。默认关。
	 *
	 * 与希尔球并存、回答不同问题：希尔球回答小质量比下的归属范围，
	 * 剖分回答这一瞬间的拉力归属。三星等质量系统里后者仍有定义，前者没有。
	 * 无界区域按 DominanceClipRadiusCm 裁剪。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cosmos|Trajectory")
	bool bDrawDominancePartition = false;
```

在 `HillSphereColor` 之后加：

```cpp
	/** 瞬时引力主导剖分的网格颜色。默认偏蓝紫，与希尔球青色区分。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cosmos|Trajectory")
	FColor DominanceColor = FColor(150, 190, 255);
```

在 `MinHillSphereRadiusCm` 之后、`private:` 之前加：

```cpp
	//////////////////////////////////////////////////////////////////////////
	// 瞬时引力主导剖分

	/** 以本天体为圆心的裁剪半径（厘米）。最重天体的主导区无界，必须裁。默认 150 AU。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cosmos|Trajectory",
		meta = (ClampMin = "1.0"))
	double DominanceClipRadiusCm = 1.5e8;

	/** 球面边界的经向段数。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cosmos|Trajectory",
		meta = (ClampMin = "3", ClampMax = "128"))
	int32 DominanceSphereSegments = 32;

	/** 球面边界的纬向圈数（不含两极）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cosmos|Trajectory",
		meta = (ClampMin = "1", ClampMax = "32"))
	int32 DominanceSphereRings = 8;

	/** 平面边界（等质量）每个方向的格数。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cosmos|Trajectory",
		meta = (ClampMin = "2", ClampMax = "128"))
	int32 DominancePlaneSegments = 32;

	/** 球面半径小于这个值（厘米）就不画 —— 飞船这种微小主导区没有意义。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cosmos|Trajectory",
		meta = (ClampMin = "0.0"))
	double MinDominanceRadiusCm = 100.0;
```

在 `AppendHillSphere` 声明之后加：

```cpp
	/**
	 * 这一对本天体与 OtherActor 是否由本组件负责画。
	 *
	 * 规则：对方也有开着的剖分组件时，较重者画；等重按 Actor 名排序去重。
	 * 对方没有开着的组件时本组件负责 —— 避免「只有一个组件开了开关却什么都画不出」。
	 */
	bool ShouldDrawDominancePair(double SelfMassEarth, double OtherMassEarth,
		const AActor& SelfActor, const AActor& OtherActor) const;

	/** 画出本天体参与的、裁剪后的 Apollonius 边界（球面或中垂面）。 */
	void AppendDominancePartition(const FVector& Anchor, const FLinearColor& Color);
```

- [ ] **Step 2: 先写集成测试（红）**

在 `FStellarEraDominanceBoundaryTest` 之后、`#endif` 之前加：

```cpp
// 剖分绘制：恒星组件画出 patch；连续 Tick 不累加；双方都开时不重复；
// 只有较轻一方开时由它自己画；加等质量第三体后多出平面边界。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraDominancePartitionTest,
	"StellarEra.Cosmos.DominancePartition",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraDominancePartitionTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Editor, false);
	if (!TestNotNull(TEXT("测试世界"), World))
	{
		return false;
	}

	FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Editor);
	WorldContext.SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());

	AActor* StarActor = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity);
	AActor* PlanetActor = World->SpawnActor<AActor>(AActor::StaticClass(),
		FTransform(FVector(5.0e7, 0.0, 0.0)));
	if (!TestNotNull(TEXT("恒星 Actor"), StarActor)
		|| !TestNotNull(TEXT("行星 Actor"), PlanetActor))
	{
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
		return false;
	}

	AddOrbitComponent(StarActor, 3329460837.433315, FVector::ZeroVector, TEXT("TestStarOrbit"));
	AddOrbitComponent(PlanetActor, 290740.8393297756, FVector::ZeroVector, TEXT("TestPlanetOrbit"));

	UCosmosTrajectoryComponent* StarTrajectory = NewObject<UCosmosTrajectoryComponent>(
		StarActor, TEXT("TestStarTrajectory"));
	StarActor->AddInstanceComponent(StarTrajectory);
	StarTrajectory->bShowInEditor = true;
	StarTrajectory->bDrawPrediction = false;
	StarTrajectory->bDrawVelocityArrow = false;
	StarTrajectory->bDrawGravityArrow = false;
	StarTrajectory->bDrawHillSphere = false;
	StarTrajectory->bDrawDominancePartition = true;
	StarTrajectory->RegisterComponent();

	StarTrajectory->TickComponent(0.016f, LEVELTICK_ViewportsOnly, nullptr);
	ULineBatchComponent* StarBatch = FindLineBatch(StarActor);
	if (!TestNotNull(TEXT("恒星行批"), StarBatch))
	{
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
		return false;
	}
	const int32 FirstCount = StarBatch->BatchedLines.Num();
	TestTrue(TEXT("恒星画出剖分网格"), FirstCount > 0);

	StarTrajectory->TickComponent(0.016f, LEVELTICK_ViewportsOnly, nullptr);
	TestEqual(TEXT("连续 Tick 不累加"), StarBatch->BatchedLines.Num(), FirstCount);

	// 行星也开：恒星更重，仍由恒星画；行星自己的行批必须为空。
	UCosmosTrajectoryComponent* PlanetTrajectory = NewObject<UCosmosTrajectoryComponent>(
		PlanetActor, TEXT("TestPlanetTrajectory"));
	PlanetActor->AddInstanceComponent(PlanetTrajectory);
	PlanetTrajectory->bShowInEditor = true;
	PlanetTrajectory->bDrawPrediction = false;
	PlanetTrajectory->bDrawVelocityArrow = false;
	PlanetTrajectory->bDrawGravityArrow = false;
	PlanetTrajectory->bDrawHillSphere = false;
	PlanetTrajectory->bDrawDominancePartition = true;
	PlanetTrajectory->RegisterComponent();

	StarTrajectory->TickComponent(0.016f, LEVELTICK_ViewportsOnly, nullptr);
	PlanetTrajectory->TickComponent(0.016f, LEVELTICK_ViewportsOnly, nullptr);
	ULineBatchComponent* PlanetBatch = FindLineBatch(PlanetActor);
	if (!TestNotNull(TEXT("行星行批"), PlanetBatch))
	{
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
		return false;
	}
	TestEqual(TEXT("较轻一方不重复画"), PlanetBatch->BatchedLines.Num(), 0);

	// 恒星关、行星开：行星必须自己画，否则「只开一个组件」等于失效。
	StarTrajectory->bDrawDominancePartition = false;
	StarTrajectory->TickComponent(0.016f, LEVELTICK_ViewportsOnly, nullptr);
	PlanetTrajectory->TickComponent(0.016f, LEVELTICK_ViewportsOnly, nullptr);
	TestTrue(TEXT("只有较轻一方开时由它自己画"), PlanetBatch->BatchedLines.Num() > 0);

	// 加一颗与恒星等质量的第三体：恒星多出「中垂面」patch，线段数增加。
	AActor* ThirdActor = World->SpawnActor<AActor>(AActor::StaticClass(),
		FTransform(FVector(-5.0e7, 0.0, 0.0)));
	if (TestNotNull(TEXT("第三体 Actor"), ThirdActor))
	{
		AddOrbitComponent(ThirdActor, 3329460837.433315, FVector::ZeroVector,
			TEXT("TestThirdOrbit"));
		StarTrajectory->bDrawDominancePartition = true;
		StarTrajectory->TickComponent(0.016f, LEVELTICK_ViewportsOnly, nullptr);
		TestTrue(TEXT("三体剖分比两体多出线段"),
			StarBatch->BatchedLines.Num() > FirstCount);
	}

	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
	return true;
}
```

- [ ] **Step 3: 构建，确认是红**

运行构建命令。预期：`bDrawDominancePartition` 不是 `UCosmosTrajectoryComponent` 的成员（属性未加时）或 `AppendDominancePartition` 未定义。

- [ ] **Step 4: 实现去重与绘制**

在 `AppendHillSphere` 的右花括号（第 660 行）之后、`DrawInEditor` 之前插入：

```cpp
bool UCosmosTrajectoryComponent::ShouldDrawDominancePair(double SelfMassEarth,
	double OtherMassEarth, const AActor& SelfActor, const AActor& OtherActor) const
{
	// 对方有没有开着的剖分组件：有就交给「较重者/名字」规则去重；
	// 没有就本组件负责，否则只开一个组件时什么都画不出来。
	const bool bEditorWorld = GetWorld() != nullptr && !GetWorld()->IsGameWorld();
	bool bOtherEnabled = false;
	TArray<UCosmosTrajectoryComponent*> OtherTrajectories;
	OtherActor.GetComponents<UCosmosTrajectoryComponent>(OtherTrajectories);
	for (const UCosmosTrajectoryComponent* OtherTrajectory : OtherTrajectories)
	{
		if (OtherTrajectory && OtherTrajectory != this
			&& OtherTrajectory->bDrawDominancePartition
			&& (!bEditorWorld || OtherTrajectory->bShowInEditor))
		{
			bOtherEnabled = true;
			break;
		}
	}
	if (!bOtherEnabled)
	{
		return true;
	}

	// 较重者画；等重按 Actor 名排序，保证全局只有一方画。
	if (SelfMassEarth != OtherMassEarth)
	{
		return SelfMassEarth > OtherMassEarth;
	}
	return SelfActor.GetName() < OtherActor.GetName();
}

void UCosmosTrajectoryComponent::AppendDominancePartition(const FVector& Anchor,
	const FLinearColor& Color)
{
	UWorld* World = GetWorld();
	AActor* SelfActor = GetOwner();
	UCosmosOrbitComponent* SelfOrbit = GetOrbitComponent();
	if (!World || !SelfActor || !SelfOrbit || !SelfOrbit->bExertsGravity)
	{
		return;   // 飞船等不产生引力的天体没有主导区
	}

	// ── 收集本关卡所有产生引力的天体（每个 Actor 一个），按名字排序保证确定性 ──
	TArray<AActor*> Actors;
	for (TObjectIterator<UCosmosOrbitComponent> It; It; ++It)
	{
		UCosmosOrbitComponent* Candidate = *It;
		if (!Candidate || Candidate->GetWorld() != World || !Candidate->bExertsGravity)
		{
			continue;
		}
		AActor* CandidateActor = Candidate->GetOwner();
		if (!CandidateActor || Actors.Contains(CandidateActor))
		{
			continue;
		}
		if (!FMath::IsFinite(Candidate->MassEarth) || Candidate->MassEarth <= 0.0)
		{
			continue;   // 与求解器同口径：无效质量不参与
		}
		Actors.Add(CandidateActor);
	}
	if (Actors.Num() < 2)
	{
		return;
	}
	Actors.Sort([](const AActor* A, const AActor* B)
	{
		return A->GetName() < B->GetName();
	});

	TArray<FVector3d> Positions;
	TArray<double> Masses;
	Positions.Reserve(Actors.Num());
	Masses.Reserve(Actors.Num());

	int32 SelfIndex = INDEX_NONE;
	for (int32 Index = 0; Index < Actors.Num(); ++Index)
	{
		AActor* CandidateActor = Actors[Index];
		Positions.Add(FVector3d(CandidateActor->GetActorLocation()));
		Masses.Add(0.0);

		if (CandidateActor == SelfActor)
		{
			SelfIndex = Index;
			Masses[Index] = SelfOrbit->MassEarth;
			continue;
		}

		// 每个 Actor 取第一个产生引力的 Orbit 组件的质量（正常情况下只有一个）。
		TArray<UCosmosOrbitComponent*> CandidateOrbits;
		CandidateActor->GetComponents<UCosmosOrbitComponent>(CandidateOrbits);
		for (const UCosmosOrbitComponent* CandidateOrbit : CandidateOrbits)
		{
			if (CandidateOrbit && CandidateOrbit->bExertsGravity
				&& FMath::IsFinite(CandidateOrbit->MassEarth)
				&& CandidateOrbit->MassEarth > 0.0)
			{
				Masses[Index] = CandidateOrbit->MassEarth;
				break;
			}
		}
	}

	if (SelfIndex == INDEX_NONE || Masses[SelfIndex] <= 0.0)
	{
		return;
	}

	const double ClipRadiusCm = FMath::Max(DominanceClipRadiusCm, 1.0);
	const FVector3d AnchorD(Anchor);

	for (int32 OtherIndex = 0; OtherIndex < Actors.Num(); ++OtherIndex)
	{
		if (OtherIndex == SelfIndex)
		{
			continue;
		}

		AActor* OtherActor = Actors[OtherIndex];
		if (!ShouldDrawDominancePair(Masses[SelfIndex], Masses[OtherIndex],
			*SelfActor, *OtherActor))
		{
			continue;
		}

		FVector3d Center;
		double RadiusCm = 0.0;
		FVector3d Normal;
		if (!ComputeDominanceBoundary(Positions[SelfIndex], Masses[SelfIndex],
			Positions[OtherIndex], Masses[OtherIndex], Center, RadiusCm, Normal))
		{
			continue;
		}

		auto Keep = [&](const FVector3d& Point)
		{
			if ((Point - AnchorD).SizeSquared() > ClipRadiusCm * ClipRadiusCm)
			{
				return false;   // 无界区域按本天体为中心的半径裁剪
			}
			return ArePairDominantAt(Point, Positions, Masses, SelfIndex, OtherIndex);
		};
		auto AddSegment = [&](const FVector3d& From, const FVector3d& To)
		{
			Lines.Emplace(FVector(From), FVector(To), Color,
				0.0f, LineThickness, LineDepthPriority);
		};

		// 等质量：中垂面上的正交网格（半径 0）。球半径过小时跳过。
		if (RadiusCm < MinDominanceRadiusCm)
		{
			if (RadiusCm > 0.0)
			{
				continue;
			}

			const FVector3d AxisU = Normal.GetSafeNormal();
			const FVector3d Reference = FMath::Abs(AxisU.Z) < 0.9
				? FVector3d(0.0, 0.0, 1.0)
				: FVector3d(1.0, 0.0, 0.0);
			const FVector3d AxisV = (Reference ^ AxisU).GetSafeNormal();
			const FVector3d AxisW = AxisU ^ AxisV;

			const int32 Segments = FMath::Max(DominancePlaneSegments, 2);
			const double CellSize = 2.0 * ClipRadiusCm / double(Segments);
			const double Half = 0.5 * double(Segments) * CellSize;
			const int32 Stride = Segments + 1;

			TArray<uint8> KeepFlags;
			KeepFlags.SetNumZeroed(Stride * Stride);
			auto PointAt = [&](int32 U, int32 V)
			{
				return Center + AxisV * (-Half + double(U) * CellSize)
					+ AxisW * (-Half + double(V) * CellSize);
			};
			auto FlagAt = [&](int32 U, int32 V)
			{
				return KeepFlags[V * Stride + U] != 0;
			};

			for (int32 V = 0; V <= Segments; ++V)
			{
				for (int32 U = 0; U <= Segments; ++U)
				{
					KeepFlags[V * Stride + U] = Keep(PointAt(U, V)) ? 1 : 0;
				}
			}
			for (int32 V = 0; V <= Segments; ++V)
			{
				for (int32 U = 0; U <= Segments; ++U)
				{
					if (!FlagAt(U, V))
					{
						continue;
					}
					if (U < Segments && FlagAt(U + 1, V))
					{
						AddSegment(PointAt(U, V), PointAt(U + 1, V));
					}
					if (V < Segments && FlagAt(U, V + 1))
					{
						AddSegment(PointAt(U, V), PointAt(U, V + 1));
					}
				}
			}
			continue;
		}

		// 球面：以边界轴为极轴的经纬网格，只连两端都保留的相邻点。
		const FVector3d AxisU = Normal.GetSafeNormal();
		const FVector3d Reference = FMath::Abs(AxisU.Z) < 0.9
			? FVector3d(0.0, 0.0, 1.0)
			: FVector3d(1.0, 0.0, 0.0);
		const FVector3d AxisV = (Reference ^ AxisU).GetSafeNormal();
		const FVector3d AxisW = AxisU ^ AxisV;

		const int32 Longitude = FMath::Max(DominanceSphereSegments, 3);
		const int32 LatitudeCount = FMath::Clamp(DominanceSphereRings, 1, 32) + 1;

		TArray<uint8> RingKeep;
		RingKeep.SetNumZeroed(Longitude * LatitudeCount);
		TArray<FVector3d> Points;
		Points.SetNumUninitialized(Longitude * LatitudeCount);

		for (int32 Ring = 0; Ring < LatitudeCount; ++Ring)
		{
			const double Phi = UE_DOUBLE_PI * double(Ring) / double(LatitudeCount);
			const double SinPhi = FMath::Sin(Phi);
			const double CosPhi = FMath::Cos(Phi);
			for (int32 Segment = 0; Segment < Longitude; ++Segment)
			{
				const double Theta = 2.0 * UE_DOUBLE_PI * double(Segment) / double(Longitude);
				const FVector3d Unit = AxisU * CosPhi
					+ AxisV * (SinPhi * FMath::Cos(Theta))
					+ AxisW * (SinPhi * FMath::Sin(Theta));
				const int32 PointIndex = Ring * Longitude + Segment;
				Points[PointIndex] = Center + Unit * RadiusCm;
				RingKeep[PointIndex] = Keep(Points[PointIndex]) ? 1 : 0;
			}
		}

		for (int32 Ring = 0; Ring < LatitudeCount; ++Ring)
		{
			for (int32 Segment = 0; Segment < Longitude; ++Segment)
			{
				const int32 Index = Ring * Longitude + Segment;
				if (!RingKeep[Index])
				{
					continue;
				}
				const int32 NextSegment = (Segment + 1) % Longitude;
				if (RingKeep[Ring * Longitude + NextSegment])
				{
					AddSegment(Points[Index], Points[Ring * Longitude + NextSegment]);
				}
				if (Ring + 1 < LatitudeCount
					&& RingKeep[(Ring + 1) * Longitude + Segment])
				{
					AddSegment(Points[Index], Points[(Ring + 1) * Longitude + Segment]);
				}
			}
		}
	}
}
```

- [ ] **Step 5: 接入运行时 Tick**

把门控段改成（在 `bWantHillSphere` 后加一行，并把它算进早退）：

```cpp
	const bool bWantHillSphere = bInStarMapView && bDrawHillSphere;
	const bool bWantDominance = bInStarMapView && bDrawDominancePartition;

	if (!bWantTrail && !bWantPrediction && !bWantArrow && !bWantGravityArrow
		&& !bWantHillSphere && !bWantDominance)
	{
		ClearDrawnLines();
		return;
	}
```

在希尔球绘制块（`AppendHillSphere(CurrentLocation, ...)` 的右花括号）之后、`LineBatch->Flush();` 之前加：

```cpp
	// ── 瞬时引力主导剖分：任意质量比都成立，按当前位置逐帧重算 ──
	if (bWantDominance)
	{
		AppendDominancePartition(CurrentLocation, FLinearColor(DominanceColor));
	}
```

- [ ] **Step 6: 接入编辑器路径**

在 `DrawInEditor` 的希尔球块之后、`LineBatch->Flush();` 之前加：

```cpp
	// ── 瞬时引力主导剖分：只用质量与摆放位置，不需要求解器状态 ──
	if (bDrawDominancePartition)
	{
		AppendDominancePartition(Anchor, FLinearColor(DominanceColor));
	}
```

- [ ] **Step 7: 构建 + 跑全量测试**

先构建（预期 `Result: Succeeded`），再跑 `StellarEra` 全量。预期：
`StellarEra.Cosmos.DominanceBoundary`、`StellarEra.Cosmos.DominancePartition` 都通过；
总数 24，0 失败。把测试日志里的 `Total tests` / `Failed` 行与构建的 `Result:` 行贴进报告。

---

## 验收清单（对照 Spec）

- [ ] 任意质量比：`ComputeDominanceBoundary` 对 1:1、4:1 都给出正确球心/半径/平面。
- [ ] 三体系统：`ArePairDominantAt` 用第三体把邻居的边界裁掉。
- [ ] 等质量三星：平面分支产出网格（集成测试的第三体用例）。
- [ ] 无界区域：以绘制天体为中心、`DominanceClipRadiusCm` 裁剪。
- [ ] 不重复：双方都开时只有较重者画；只有一方开时由它自己画。
- [ ] 不累加：连续 Tick 线段数不变。
- [ ] 不破坏现有功能：希尔球、预测、拖尾、箭头测试全部保持通过。
- [ ] 构建 `StellarEraEditor Win64 Development` 成功；自动化 24/24。

## 不做

- 稳定图/捕获盆地、Jacobi 有效势剖分（见 Spec 第 7 节）。
- 不把剖分画进 `UniverseMap` 资产，不改任何 UPROPERTY 默认值之外的资产配置。
- 不提交、不推送。
