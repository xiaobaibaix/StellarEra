# 恒星天体（三恒星 + 一行星）实现计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 让 `UniverseMap` 里的三颗恒星既在天空里看得见、又参与 N 体引力，并让行星大气的明暗分界跟随离它最近的那颗恒星。

**Architecture:** 恒星的身份拆成两个互不认识的组件 —— 物理身份 `UCosmosOrbitComponent`（Cosmos 模块），视觉/大气身份新增的 `UStarBodyComponent`（Planet 模块）。这样 Planet 不需要反向依赖 Cosmos（会成环）。`UPlanetAtmosphereComponent::ResolveSunDirection()` 改成两级解析：优先最近的恒星，没有则回退到第一盏 `ADirectionalLight`（保持旧行为不变）；游戏世界里顺带把那盏灯的朝向同步到恒星方向。恒星 Actor 本身是一个新的 C++ 类 `AStarBodyActor`，住在 Cosmos（唯一能同时引用两个模块的地方）。

**Tech Stack:** Unreal Engine 5.8 C++、UE Automation Test（`IMPLEMENT_SIMPLE_AUTOMATION_TEST`）、无头 Python 编辑器脚本（`UnrealEditor-Cmd -run=pythonscript`）。

**Spec:** `docs/superpowers/specs/2026-11-02-sun-star-bodies-design.md`

## Global Constraints

- UE 5.8.3 / Win64，C++ 为主，蓝图只承载资产配置。
- 模块依赖必须无环：`StellarEraPlanet` 不得依赖 `StellarEraCosmos`；`StellarEraCosmos` 只以 **Private** 依赖 `StellarEraPlanet`。
- `*Tests.cpp` 只能放在 `Source/<Module>/Src/Tests/`，且测试必须放在所 include 头文件的下游模块。
- 含中文的 `.h`/`.cpp` 存 **UTF-8 with BOM**；不要统一换行符（CRLF/LF 混排是仓库常态）。
- 构建：先关编辑器，然后用捆绑 dotnet 直调 UBT，cwd 在项目外；**不要用 `Build.bat`**：
  ```bash
  "D:/Application/Epic Games/UE_5.8/Engine/Binaries/ThirdParty/DotNet/10.0/win-x64/dotnet.exe" \
    "D:/Application/Epic Games/UE_5.8/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.dll" \
    StellarEraEditor Win64 Development \
    -Project="F:/workspace/ue/StellarEra/StellarEra.uproject" -WaitMutex -FromMsBuild
  ```
- 跑测试（编辑器开着也行）：
  ```bash
  "D:/Application/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" \
    "F:/workspace/ue/StellarEra/StellarEra.uproject" \
    -ExecCmds="Automation RunTests StellarEra" \
    -TestExit="Automation Test Queue Empty" \
    -unattended -nopause -nullrhi -nosplash -stdout -abslog="C:/Users/23080/ue_scratch/tests.log"
  ```
  单跑一条把 `StellarEra` 换成完整测试名。
- 提交必须带 pathspec：`git commit -m "..." -- <paths>`（索引里有常驻暂存文件，裸 `git commit` 会吞掉全部）。
- 编辑器里只填人类单位：`MassEarth` 用 M⊕，`InitialVelocityKmPerSecond` 用 km/s。

---

## File Structure

| 文件 | 动作 | 职责 |
|---|---|---|
| `Source/StellarEraPlanet/Src/Atmosphere/StarBodyComponent.h` | 新建 | `UStarBodyComponent`：恒星标记 + 主光接管开关 + 最近恒星查找 |
| `Source/StellarEraPlanet/Src/Atmosphere/StarBodyComponent.cpp` | 新建 | 同上实现 |
| `Source/StellarEraPlanet/Src/Atmosphere/PlanetAtmosphereComponent.h` | 改 | `ResolveSunDirection` 文档 + 新增静态 `ComputeSunDirection` |
| `Source/StellarEraPlanet/Src/Atmosphere/PlanetAtmosphereComponent.cpp` | 改 | 两级太阳方向解析 + 主光同步 |
| `Source/StellarEraPlanet/Src/Star/SunMaterialBuilder.cpp` | 改 | 三处材质路径 `/Game/Code/Star` → `/Game/Code/Res/Star` |
| `Source/StellarEraPlanet/Src/Tests/PlanetStarBodyComponentTests.cpp` | 新建 | `FindNearest` + `ComputeSunDirection` 的单元测试 |
| `Source/StellarEraCosmos/Src/StarBodyActor.h` | 新建 | `AStarBodyActor`：恒星 Actor 骨架 |
| `Source/StellarEraCosmos/Src/StarBodyActor.cpp` | 新建 | 同上实现 |
| `Source/StellarEraCosmos/Src/Tests/StarBodyActorTests.cpp` | 新建 | 骨架组件齐备性测试 |
| `Source/StellarEraCosmos/Src/Tests/FourBodyStabilityTests.cpp` | 新建 | 三恒星 + 一行星 10 模拟年能量漂移测试 |
| `C:/Users/23080/ue_scratch/build_sun_system.py` | 新建（仓库外） | 关卡数据脚本：删两颗行星、搬动、摆三颗恒星 |

---

### Task 1: `UStarBodyComponent` + 最近恒星查找

**Files:**
- Create: `Source/StellarEraPlanet/Src/Atmosphere/StarBodyComponent.h`
- Create: `Source/StellarEraPlanet/Src/Atmosphere/StarBodyComponent.cpp`
- Test: `Source/StellarEraPlanet/Src/Tests/PlanetStarBodyComponentTests.cpp`

**Interfaces:**
- Consumes: 无
- Produces:
  - `class UStarBodyComponent : public UActorComponent`
  - `bool bDrivesDirectionalLight`（`UPROPERTY(EditAnywhere)`，默认 `true`）
  - `static UStarBodyComponent* FindNearest(const UWorld* World, const FVector& FromLocation)`
  - 恒星位置一律取 `GetOwner()->GetActorLocation()`，组件自己不存位置。

- [ ] **Step 1: 写失败的测试**

新建 `Source/StellarEraPlanet/Src/Tests/PlanetStarBodyComponentTests.cpp`（**UTF-8 with BOM**）：

```cpp
// 恒星组件的两件事：找到离给定点最近的那颗，以及恒星方向解析。

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/DirectionalLight.h"
#include "Components/SceneComponent.h"
#include "StarBodyComponent.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	/**
	 * 取编辑器世界。
	 *
	 * 不能写 GEngine->GetEditorWorldContext()：那是 UEditorEngine 的方法，本模块
	 * 刻意不带 UnrealEd 依赖。按 WorldType 扫一遍 UEngine 的世界上下文等价。
	 */
	UWorld* FindStarBodyEditorWorld()
	{
		if (!GEngine)
		{
			return nullptr;
		}
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			if (Context.WorldType == EWorldType::Editor && Context.World())
			{
				return Context.World();
			}
		}
		return nullptr;
	}

	/**
	 * 在世界里放一个挂好 UStarBodyComponent 的 Actor。
	 *
	 * **必须挂根组件**：SpawnActor 出来的裸 AActor 没有根组件，GetActorLocation()
	 * 恒为零（见 CLAUDE.md「测试裸 Actor 的位置恒为 0」），组件会全部堆在原点。
	 * 返回 Actor 供调用方销毁。
	 */
	AActor* SpawnStarActorAt(UWorld* World, const FVector& Location, UStarBodyComponent*& OutStar)
	{
		AActor* Actor = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform(Location));
		if (!Actor)
		{
			OutStar = nullptr;
			return nullptr;
		}

		USceneComponent* Root = NewObject<USceneComponent>(Actor);
		Actor->SetRootComponent(Root);
		Root->RegisterComponent();
		Actor->SetActorLocation(Location);

		UStarBodyComponent* Star = NewObject<UStarBodyComponent>(Actor);
		Actor->AddInstanceComponent(Star);
		Star->RegisterComponent();

		OutStar = Star;
		return Actor;
	}
} // namespace

// 找最近的那颗。位置刻意放得离关卡里的天体很远，避免依赖关卡内容。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraStarBodyFindNearestTest,
	"StellarEra.Planet.Atmosphere.StarBody.FindNearest",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FStellarEraStarBodyFindNearestTest::RunTest(const FString& Parameters)
{
	TestNull(TEXT("空世界返回 nullptr"), UStarBodyComponent::FindNearest(nullptr, FVector::ZeroVector));

	UWorld* World = FindStarBodyEditorWorld();
	if (!TestNotNull(TEXT("编辑器世界"), World))
	{
		return false;
	}

	const FVector NearLocation(1.0e9, 0.0, 0.0);
	const FVector FarLocation(1.0e9 + 10000.0, 0.0, 0.0);

	UStarBodyComponent* NearStar = nullptr;
	UStarBodyComponent* FarStar = nullptr;
	AActor* NearActor = SpawnStarActorAt(World, NearLocation, NearStar);
	AActor* FarActor = SpawnStarActorAt(World, FarLocation, FarStar);

	bool bOk = true;
	bOk &= TestNotNull(TEXT("近处恒星组件"), NearStar);
	bOk &= TestNotNull(TEXT("远处恒星组件"), FarStar);

	if (bOk)
	{
		// 站在近处那颗旁边探：必须返回近处那颗，而不是更远处（或关卡里原有的）恒星。
		UStarBodyComponent* Found = UStarBodyComponent::FindNearest(World, NearLocation + FVector(200.0, 0.0, 0.0));
		TestEqual(TEXT("离近处恒星 200cm 时找到它"), Found, NearStar);
		TestEqual(TEXT("近处恒星的位置"), NearStar->GetOwner()->GetActorLocation(), NearLocation);

		Found = UStarBodyComponent::FindNearest(World, FarLocation - FVector(200.0, 0.0, 0.0));
		TestEqual(TEXT("离远处恒星 200cm 时找到它"), Found, FarStar);
	}

	if (NearActor)
	{
		NearActor->Destroy();
	}
	if (FarActor)
	{
		FarActor->Destroy();
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
```

- [ ] **Step 2: 跑测试确认它失败**

Run: 用 Global Constraints 里的测试命令，把 `StellarEra` 换成 `StellarEra.Planet.Atmosphere.StarBody.FindNearest`。
Expected: 编译失败 —— `StarBodyComponent.h` 不存在。这是预期的（先有测试，后有实现）。

- [ ] **Step 3: 写最小实现**

新建 `Source/StellarEraPlanet/Src/Atmosphere/StarBodyComponent.h`（**UTF-8 with BOM**）：

```cpp
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "StarBodyComponent.generated.h"

class UWorld;

/**
 * 「这个 Actor 是一颗恒星」的标记 + 主光接管开关。
 *
 * ── 为什么不能只靠 UCosmosOrbitComponent 判断恒星 ──────────────
 * 判断恒星的代码是大气组件，它在本模块；而 UCosmosOrbitComponent 在 Cosmos 模块。
 * `StellarEraCosmos → StellarEraPlanet` 这条依赖已经存在，反向再来一条就成环
 * （见 docs/superpowers/specs/2026-11-02-sun-star-bodies-design.md §4）。
 *
 * 于是恒星的身份拆成两个互不认识的组件，由关卡里的同一个 Actor 同时挂上：
 *   - UCosmosOrbitComponent（Cosmos）：质量多少、初速多少 —— 物理身份
 *   - 本组件（Planet）           ：这是一颗恒星、是否接管主光 —— 视觉 / 大气身份
 * Cosmos 不需要知道「恒星」这个概念，Planet 也不需要知道 N 体。
 *
 * 与 UCosmosOrbitComponent 同样的取舍：**不 Tick，组件自己不存位置**。
 * 恒星位置就是 Owner 的位置，每帧现取 —— 恒星在 N 体里是会动的。
 */
UCLASS(ClassGroup = (StellarEra), meta = (BlueprintSpawnableComponent))
class STELLARERAPLANET_API UStarBodyComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UStarBodyComponent();

	/**
	 * 这颗恒星是否接管关卡主光（第一盏 ADirectionalLight）的朝向。
	 *
	 * 只对该行星最近的那颗恒星有意义：大气按"最近的恒星"算明暗分界，地形若还按
	 * 另一盏灯的朝向算，两边会打架。多颗恒星时只有最近那颗生效，所以同时只有
	 * 一个写者。
	 *
	 * 关掉它就退回纯装饰的恒星：天空照得到它，但它不转灯。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "StellarEra|Star")
	bool bDrivesDirectionalLight = true;

	/**
	 * 世界上离 FromLocation 最近的恒星组件；World 为空或世界里没有恒星时返回 nullptr。
	 *
	 * 用「最近」而不是「唯一」：三颗恒星下每颗行星应当被自己那颗恒星照亮。
	 * 每次都完整遍历 —— 关卡里天体是个位数，成本可以忽略，换来的是恒星移动、
	 * 增删时不需要任何失效通知。
	 */
	static UStarBodyComponent* FindNearest(const UWorld* World, const FVector& FromLocation);
};
```

新建 `Source/StellarEraPlanet/Src/Atmosphere/StarBodyComponent.cpp`（**UTF-8 with BOM**）：

```cpp
#include "StarBodyComponent.h"

#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "EngineUtils.h"

UStarBodyComponent::UStarBodyComponent()
{
	// 只存参数，不参与 Tick。
	PrimaryComponentTick.bCanEverTick = false;
}

UStarBodyComponent* UStarBodyComponent::FindNearest(const UWorld* World, const FVector& FromLocation)
{
	if (!World)
	{
		return nullptr;
	}

	UStarBodyComponent* Nearest = nullptr;
	double NearestSquared = TNumericLimits<double>::Max();

	// TActorIterator 只收非 const UWorld*。这里不改世界，const_cast 是有意的。
	for (TActorIterator<AActor> It(const_cast<UWorld*>(World)); It; ++It)
	{
		UStarBodyComponent* Star = It->FindComponentByClass<UStarBodyComponent>();
		if (!Star)
		{
			continue;
		}

		const double Squared = FVector::DistSquared(Star->GetOwner()->GetActorLocation(), FromLocation);
		if (Squared < NearestSquared)
		{
			NearestSquared = Squared;
			Nearest = Star;
		}
	}

	return Nearest;
}
```

- [ ] **Step 4: 构建**

Run: Global Constraints 里的构建命令（先关编辑器）。
Expected: `Result: Succeeded`。

- [ ] **Step 5: 跑测试确认通过**

Run: `Automation RunTests StellarEra.Planet.Atmosphere.StarBody.FindNearest`
Expected: 1 条测试通过，退出码 0。

- [ ] **Step 6: 提交**

```bash
git commit -m "feat(恒星): 新增 UStarBodyComponent 与最近恒星查找" -- \
  Source/StellarEraPlanet/Src/Atmosphere/StarBodyComponent.h \
  Source/StellarEraPlanet/Src/Atmosphere/StarBodyComponent.cpp \
  Source/StellarEraPlanet/Src/Tests/PlanetStarBodyComponentTests.cpp
```

---

### Task 2: 大气太阳方向改两级解析 + 主光同步

**Files:**
- Modify: `Source/StellarEraPlanet/Src/Atmosphere/PlanetAtmosphereComponent.h:131-132`
- Modify: `Source/StellarEraPlanet/Src/Atmosphere/PlanetAtmosphereComponent.cpp:232-265`
- Test: `Source/StellarEraPlanet/Src/Tests/PlanetStarBodyComponentTests.cpp`（追加）

**Interfaces:**
- Consumes: `UStarBodyComponent::FindNearest`（Task 1）、`UStarBodyComponent::bDrivesDirectionalLight`
- Produces:
  - `static FVector UPlanetAtmosphereComponent::ComputeSunDirection(const AActor* StarActor, const ADirectionalLight* FallbackLight, const FVector& FromLocation)`（public static，纯函数，可单测）
  - `ResolveSunDirection()` 行为：恒星优先，其次 `ADirectionalLight`，都没有则返回零向量并只警告一次。

- [ ] **Step 1: 追加失败的测试**

在 `PlanetStarBodyComponentTests.cpp` 的 `#endif` **之前**追加（该文件已 include 了 `Engine/DirectionalLight.h`）：

```cpp
// 太阳方向解析。纯函数，不需要摆恒星组件 —— 恒星只贡献一个位置。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraSunDirectionComputeTest,
	"StellarEra.Planet.Atmosphere.SunDirection.Compute",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FStellarEraSunDirectionComputeTest::RunTest(const FString& Parameters)
{
	// 1) 有恒星：方向 = 从探针点指向恒星的单位向量，与回退的灯无关。
	{
		AActor* Star = NewObject<AActor>();
		Star->SetActorLocation(FVector(1000.0, 0.0, 0.0));

		const FVector Direction = UPlanetAtmosphereComponent::ComputeSunDirection(
			Star, nullptr, FVector::ZeroVector);
		TestEqual(TEXT("恒星在 +X：方向应为 +X"), Direction, FVector(1.0, 0.0, 0.0), 1e-6);

		const FVector Offset = UPlanetAtmosphereComponent::ComputeSunDirection(
			Star, nullptr, FVector(1000.0, 1000.0, 0.0));
		TestEqual(TEXT("探针点在恒星正下方：方向应为 +Y"), Offset, FVector(0.0, 1.0, 0.0), 1e-6);
	}

	// 2) 只有灯：沿用旧行为 —— 灯 forward 是光传播方向，太阳在反向。
	UWorld* World = FindStarBodyEditorWorld();
	if (!TestNotNull(TEXT("编辑器世界"), World))
	{
		return false;
	}

	ADirectionalLight* Light = World->SpawnActor<ADirectionalLight>(
		FVector(0.0, 0.0, 500.0), FRotator(-90.0, 0.0, 0.0));
	if (!TestNotNull(TEXT("方向光"), Light))
	{
		return false;
	}

	const FVector FromLight = UPlanetAtmosphereComponent::ComputeSunDirection(
		nullptr, Light, FVector::ZeroVector);
	TestEqual(TEXT("灯朝下：太阳方向朝上"), FromLight, FVector(0.0, 0.0, 1.0), 1e-4);

	// 3) 都没有：零向量。
	TestTrue(TEXT("没有恒星也没有灯 = 零向量"),
		UPlanetAtmosphereComponent::ComputeSunDirection(nullptr, nullptr, FVector::ZeroVector).IsNearlyZero());

	Light->Destroy();
	return true;
}
```

同时在文件顶部的 include 里补一行：

```cpp
#include "PlanetAtmosphereComponent.h"
```

- [ ] **Step 2: 跑测试确认它失败**

Run: `Automation RunTests StellarEra.Planet.Atmosphere.SunDirection.Compute`
Expected: 编译失败 —— `ComputeSunDirection` 不是 `UPlanetAtmosphereComponent` 的成员。

- [ ] **Step 3: 改头文件**

在 `PlanetAtmosphereComponent.h` 里，把第 131-132 行的两行替换成：

```cpp
public:
	/**
	 * 太阳方向（单位向量，从 FromLocation 指向太阳）。
	 *
	 * 两级：
	 *   1. StarActor 非空 → 从探针点指向它的方向（三颗恒星时由调用方挑最近的那颗）；
	 *   2. 否则回退到 FallbackLight：DirectionalLight 的 forward 是光的**传播**方向
	 *      （天上→地上），太阳在它的反向。这是本组件在此次改动前的唯一行为。
	 * 两者都没有 → 零向量。
	 *
	 * 做成 static 纯函数是为了能被自动化测试覆盖：不碰世界、不碰材质。
	 */
	static FVector ComputeSunDirection(const AActor* StarActor, const ADirectionalLight* FallbackLight,
		const FVector& FromLocation);

private:
	/**
	 * 解析太阳方向：优先离本行星最近的 UStarBodyComponent，没有则回退到关卡里第一盏
	 * ADirectionalLight。都没有就返回零并只警告一次。
	 *
	 * 在游戏世界里还会顺带把缓存的灯转到 −方向（见 ComputeSunDirection 的说明）。
	 */
	FVector ResolveSunDirection();
```

> 注意：`ResolveSunDirection` 原来就在 `private:` 段里，替换时不要留下重复的 `private:`。改完检查该段落只有一个 `public:` 和一个 `private:`。

- [ ] **Step 4: 改实现**

用下面这段整体替换 `PlanetAtmosphereComponent.cpp` 第 232-265 行的 `ResolveSunDirection()`：

```cpp
FVector UPlanetAtmosphereComponent::ComputeSunDirection(const AActor* StarActor,
	const ADirectionalLight* FallbackLight, const FVector& FromLocation)
{
	if (StarActor)
	{
		// 恒星会动（N 体每帧写回位置），所以这里不缓存方向，只缓存指针。
		return (StarActor->GetActorLocation() - FromLocation).GetSafeNormal();
	}

	if (FallbackLight)
	{
		// DirectionalLight 的 forward 是光**传播**的方向（天上→地上）。
		// 从行星指向太阳是它的反向。
		return -FallbackLight->GetActorForwardVector();
	}

	return FVector::ZeroVector;
}

FVector UPlanetAtmosphereComponent::ResolveSunDirection()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return FVector::ZeroVector;
	}

	// 恒星优先。每帧完整遍历：关卡里天体个位数，成本可忽略，
	// 换来恒星增删移动时不需要任何失效通知。
	UStarBodyComponent* Star = UStarBodyComponent::FindNearest(World, GetComponentLocation());

	// 缓存的灯没了（关卡重载、actor 被删）就重新找。
	if (!IsValid(CachedSunLight))
	{
		CachedSunLight = nullptr;
		for (TActorIterator<ADirectionalLight> It(World); It; ++It)
		{
			CachedSunLight = *It;
			break;
		}
	}

	const AActor* StarActor = Star ? Star->GetOwner() : nullptr;
	if (!StarActor && !CachedSunLight)
	{
		if (!bLoggedMissingSun)
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[大气] 关卡里既没有 UStarBodyComponent 也没有 ADirectionalLight，壳的太阳方向无从确定，明暗分界会停在初值。"));
			bLoggedMissingSun = true;
		}
		return FVector::ZeroVector;
	}

	const FVector SunDirection = ComputeSunDirection(StarActor, CachedSunLight, GetComponentLocation());

	// 主光同步：只在游戏世界里转灯。编辑器世界里每帧转灯会把关卡一直标脏。
	if (Star && Star->bDrivesDirectionalLight && CachedSunLight && World->IsGameWorld())
	{
		CachedSunLight->SetActorRotation((-SunDirection).Rotation());
	}

	return SunDirection;
}
```

并在文件顶部 include 区补上（放在已有的 `#include "Engine/DirectionalLight.h"` 附近）：

```cpp
#include "StarBodyComponent.h"
```

- [ ] **Step 5: 构建**

Run: 构建命令。
Expected: `Result: Succeeded`。

- [ ] **Step 6: 跑测试确认通过**

Run: `Automation RunTests StellarEra.Planet.Atmosphere`
Expected: 两条新测试通过；`AtmosphereParams.*` 与 `AtmosphereMaterial.*` 既有测试不回归。

- [ ] **Step 7: 提交**

```bash
git commit -m "feat(大气): 太阳方向优先取最近恒星，并在游戏世界同步主光朝向" -- \
  Source/StellarEraPlanet/Src/Atmosphere/PlanetAtmosphereComponent.h \
  Source/StellarEraPlanet/Src/Atmosphere/PlanetAtmosphereComponent.cpp \
  Source/StellarEraPlanet/Src/Tests/PlanetStarBodyComponentTests.cpp
```

---

### Task 3: 修恒星材质构建器的路径漂移

**Files:**
- Modify: `Source/StellarEraPlanet/Src/Star/SunMaterialBuilder.cpp:52,53,534`

**Interfaces:**
- Consumes: 无
- Produces: 无（常量修正）

**背景：** 提交 `eefaaac` 把恒星材质迁到了 `/Game/Code/Res/Star/Materials`，但构建器里的三个常量还是旧路径。现在跑 `StellarEra.BuildSunMaterial` 会在旧路径**另建一套**，与关卡里引用的不是同一份。

- [ ] **Step 1: 改三处常量**

`SunMaterialBuilder.cpp:52-53`：

```cpp
const TCHAR* const SunMaterialDir = TEXT("/Game/Code/Res/Star/Materials");
const TCHAR* const SunMaterialPath = TEXT("/Game/Code/Res/Star/Materials/M_Sun");
```

`SunMaterialBuilder.cpp:534`（帮助文本）：

```cpp
	TEXT("重建 /Game/Code/Res/Star/Materials/M_Sun 与三个恒星预设（M2V / G2V / B3V）。改完 HLSL 或参数后跑一次。"),
```

- [ ] **Step 2: 确认没有漏网的旧路径**

Run: `grep -rn "Game/Code/Star/Materials" Source/`
Expected: 无输出（退出码 1）。

- [ ] **Step 3: 构建**

Run: 构建命令。
Expected: `Result: Succeeded`。

- [ ] **Step 4: 提交**

```bash
git commit -m "fix(恒星): 材质构建器路径改到 Res/Star，避免另建一套材质" -- \
  Source/StellarEraPlanet/Src/Star/SunMaterialBuilder.cpp
```

> **不要**在这次改动里跑 `StellarEra.BuildSunMaterial`：它会重写磁盘上已调好的 `M_Sun` 与三个实例。路径正确性由第 2 步的 grep 证明，构建器本身的正确性不在本次范围内。

---

### Task 4: `AStarBodyActor`

**Files:**
- Create: `Source/StellarEraCosmos/Src/StarBodyActor.h`
- Create: `Source/StellarEraCosmos/Src/StarBodyActor.cpp`
- Test: `Source/StellarEraCosmos/Src/Tests/StarBodyActorTests.cpp`
- Modify: `Source/StellarEraCosmos/Src/CosmosOrbitComponent.h:14-17`（注释已过时）

**Interfaces:**
- Consumes: `UCosmosOrbitComponent`（同模块）、`UStarBodyComponent`（Planet，**只在 .cpp 里 include**）
- Produces:
  - `class AStarBodyActor : public AActor`
  - 公开成员（均 `VisibleAnywhere, BlueprintReadOnly`）：`TObjectPtr<UStaticMeshComponent> StarMesh`、`TObjectPtr<UCosmosOrbitComponent> Orbit`、`TObjectPtr<UStarBodyComponent> StarBody`
  - 构造默认值：网格 `/Engine/BasicShapes/Sphere`、`relative_scale3d = 10000`（半径 50×10000 = 5e5 cm）、无碰撞、不投影、材质 `/Game/Code/Res/Star/Materials/MI_Sun_G2V`

**为什么放在 Cosmos：** 恒星要同时挂 Cosmos 与 Planet 的组件，而 Planet 不能依赖 Cosmos。Cosmos 对 Planet 是 Private 依赖，所以**头文件里只能前置声明 `UStarBodyComponent`**，完整类型只在 `.cpp` 里 include —— 这与该模块 Build.cs 顶部「公开头不 include 它们」的约定一致。

- [ ] **Step 1: 写失败的测试**

新建 `Source/StellarEraCosmos/Src/Tests/StarBodyActorTests.cpp`（**UTF-8 with BOM**）：

```cpp
// 恒星 Actor 的骨架：三个组件齐备、网格与缩放正确、不 Tick。

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Components/StaticMeshComponent.h"
#include "StarBodyActor.h"
#include "CosmosOrbitComponent.h"
#include "StarBodyComponent.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	UWorld* FindStarBodyActorEditorWorld()
	{
		if (!GEngine)
		{
			return nullptr;
		}
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			if (Context.WorldType == EWorldType::Editor && Context.World())
			{
				return Context.World();
			}
		}
		return nullptr;
	}
} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraStarBodyActorSkeletonTest,
	"StellarEra.Cosmos.StarBodyActor.Skeleton",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FStellarEraStarBodyActorSkeletonTest::RunTest(const FString& Parameters)
{
	UWorld* World = FindStarBodyActorEditorWorld();
	if (!TestNotNull(TEXT("编辑器世界"), World))
	{
		return false;
	}

	AStarBodyActor* Star = World->SpawnActor<AStarBodyActor>(
		AStarBodyActor::StaticClass(), FVector(0.0, 0.0, 0.0), FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("恒星 Actor"), Star))
	{
		return false;
	}

	TestNotNull(TEXT("恒星网格组件"), Star->StarMesh.Get());
	TestNotNull(TEXT("N 体轨道组件"), Star->Orbit.Get());
	TestNotNull(TEXT("恒星标记组件"), Star->StarBody.Get());

	if (Star->StarMesh)
	{
		TestEqual(TEXT("根组件就是恒星网格"), Star->GetRootComponent(), static_cast<USceneComponent*>(Star->StarMesh.Get()));

		const FVector Scale = Star->StarMesh->GetRelativeScale3D();
		TestEqual(TEXT("缩放 X"), Scale.X, 10000.0, 0.5);
		TestEqual(TEXT("缩放 Y"), Scale.Y, 10000.0, 0.5);
		TestEqual(TEXT("缩放 Z"), Scale.Z, 10000.0, 0.5);

		TestEqual(TEXT("不参与碰撞"), Star->StarMesh->GetCollisionEnabled(), ECollisionEnabled::NoCollision);

		const UStaticMesh* Mesh = Star->StarMesh->GetStaticMesh();
		if (TestNotNull(TEXT("静态网格"), Mesh))
		{
			// BasicShapes/Sphere 的外接半径是 50cm，×10000 = 5e5cm = 5 公里。
			const FBoxSphereBounds Bounds = Mesh->GetBounds();
			TestEqual(TEXT("球半径 50cm"), (double)Bounds.SphereRadius, 50.0, 0.1);
		}
	}

	TestFalse(TEXT("恒星 Actor 不 Tick"), Star->PrimaryActorTick.bCanEverTick);
	TestFalse(TEXT("轨道组件不 Tick"), Star->Orbit && Star->Orbit->PrimaryComponentTick.bCanEverTick);

	Star->Destroy();
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
```

- [ ] **Step 2: 跑测试确认它失败**

Run: `Automation RunTests StellarEra.Cosmos.StarBodyActor.Skeleton`
Expected: 编译失败 —— `StarBodyActor.h` 不存在。

- [ ] **Step 3: 写实现**

新建 `Source/StellarEraCosmos/Src/StarBodyActor.h`（**UTF-8 with BOM**）：

```cpp
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "StarBodyActor.generated.h"

class UStaticMeshComponent;
class UCosmosOrbitComponent;
class UStarBodyComponent;

/**
 * 一颗恒星：一个自发光球 + 两个组件。
 *
 * ── 为什么这个类在 Cosmos 而不在 Planet ────────────────────────
 * 恒星的身份跨两个模块：物理身份是 UCosmosOrbitComponent（本模块），
 * 视觉 / 大气身份是 UStarBodyComponent（Planet 模块）。Planet 不允许依赖 Cosmos，
 * 所以只有 Cosmos 能同时引用两者。
 *
 * 因此本头文件**只前置声明** UStarBodyComponent，完整类型只在 .cpp 里 include ——
 * 与 StellarEraCosmos.Build.cs 顶部「Planet 是 Private 依赖、公开头不 include 它的头」
 * 的约定一致。下游模块 include 本头只会看到前置声明，够用。
 *
 * ── 为什么是一个 Actor 类而不是在关卡里拼组件 ──────────────────
 * 骨架组件缺一个就静默坏：少了 UCosmosOrbitComponent 恒星不参与引力，少了
 * UStarBodyComponent 大气不认它。按 CLAUDE.md「Actor 组件归属口径」，这种骨架
 * 写在 C++ 里，蓝图子类 / 关卡实例只改外观。
 *
 * 不 Tick、不自己位移：位置由 UCosmosSimulationSubsystem 每帧写回。
 */
UCLASS()
class STELLARERACOSMOS_API AStarBodyActor : public AActor
{
	GENERATED_BODY()

public:
	AStarBodyActor();

	/** 恒星本体。根组件，缩放决定恒星的物理外观大小。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "StellarEra|Star")
	TObjectPtr<UStaticMeshComponent> StarMesh;

	/** N 体物理身份：质量与初速在关卡实例里填。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "StellarEra|Star")
	TObjectPtr<UCosmosOrbitComponent> Orbit;

	/** 视觉 / 大气身份：大气组件靠它认出「这是一颗恒星」。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "StellarEra|Star")
	TObjectPtr<UStarBodyComponent> StarBody;
};
```

新建 `Source/StellarEraCosmos/Src/StarBodyActor.cpp`（**UTF-8 with BOM**）：

```cpp
#include "StarBodyActor.h"

#include "Components/StaticMeshComponent.h"
#include "UObject/ConstructorHelpers.h"
#include "CosmosOrbitComponent.h"
#include "StarBodyComponent.h"

AStarBodyActor::AStarBodyActor()
{
	PrimaryActorTick.bCanEverTick = false;

	StarMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("StarMesh"));
	SetRootComponent(StarMesh);

	// 必须是 Movable：N 体子系统每帧把位置写回 Actor，Static 会拒绝移动并刷警告。
	StarMesh->SetMobility(EComponentMobility::Movable);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereMesh(
		TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	if (SphereMesh.Succeeded())
	{
		StarMesh->SetStaticMesh(SphereMesh.Object);
	}

	// BasicShapes/Sphere 半径 50cm。×10000 = 5e5cm = 5 公里半径，
	// 在 1e8cm（100 AU）处张角 0.57°，与真太阳的 0.53° 基本一致。
	StarMesh->SetRelativeScale3D(FVector(10000.0f));

	// 恒星不参与碰撞，也不投阴影：它是自发光体，阴影只由 DirectionalLight 产生。
	StarMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	StarMesh->SetCastShadow(false);

	// 默认吃 G2V（类太阳）预设，关卡实例按需换 M2V / B3V。
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> SunMaterial(
		TEXT("/Game/Code/Res/Star/Materials/MI_Sun_G2V.MI_Sun_G2V"));
	if (SunMaterial.Succeeded())
	{
		StarMesh->SetMaterial(0, SunMaterial.Object);
	}

	Orbit = CreateDefaultSubobject<UCosmosOrbitComponent>(TEXT("Orbit"));
	StarBody = CreateDefaultSubobject<UStarBodyComponent>(TEXT("StarBody"));
}
```

- [ ] **Step 4: 更新 `CosmosOrbitComponent.h` 里已过时的注释**

第 14-17 行现在写着「恒星不需要新类 —— 一个 StaticMeshActor 挂上它就是天体」。恒星现在有了 `AStarBodyActor`，改成：

```cpp
 * 刻意做成「组件」而不是「Actor 基类」：
 *   - 行星不需要新类 —— AVoxelPlanetActor 挂上它就是天体，不必让体素星球去继承
 *     一个 N 体的基类；
 *   - 恒星需要一个 Actor 类，但那是为了把本组件与 Planet 模块的 UStarBodyComponent
 *     摆在一起（见 StarBodyActor.h），不是为了让本组件变成基类。
```

- [ ] **Step 5: 构建**

Run: 构建命令。
Expected: `Result: Succeeded`。

- [ ] **Step 6: 跑测试确认通过**

Run: `Automation RunTests StellarEra.Cosmos`
Expected: `StarBodyActor.Skeleton` 通过；`Simulator.*`、`OrbitComponent.*`、`Trajectory.*` 不回归。

- [ ] **Step 7: 提交**

```bash
git commit -m "feat(恒星): 新增 AStarBodyActor，把 N 体与恒星身份两个组件绑在一起" -- \
  Source/StellarEraCosmos/Src/StarBodyActor.h \
  Source/StellarEraCosmos/Src/StarBodyActor.cpp \
  Source/StellarEraCosmos/Src/CosmosOrbitComponent.h \
  Source/StellarEraCosmos/Src/Tests/StarBodyActorTests.cpp
```

---

### Task 5: 四体稳定性 —— 定稿初速并证明它能跑

**Files:**
- Test: `Source/StellarEraCosmos/Src/Tests/FourBodyStabilityTests.cpp`

**Interfaces:**
- Consumes: `FNBodySimulator`（`AddBody` / `StepExact` / `MarkEnergyBaseline` / `GetEnergyDriftRatio` / `GetBody`）
- Produces: 关卡脚本要用的四组初速数值（见下方表格）。

**为什么必须先做这一步：** 三颗等质量恒星共线是一个**周期解**（Euler 共线解），不是稳定平衡；再叠一颗行星更是如此。初速必须按**多体合力**算，不能套二体公式 —— 外侧恒星离中间那颗 d、离另一颗外侧 2d，后者的贡献是 `1/4`。行星也一样：两颗外侧恒星对它的合力让所需速度比纯二体高约 15%。用错公式得到的速度偏差 8%~12%，轨道会明显偏心并提前散架。

**定稿数值（`MassEarth` 是编辑器单位，其余是求解器单位）：**

| 天体 | 位置 (AU) | 质量 | 速度 (AU/年) | 速度 (km/s) | 半径 |
|---|---|---|---|---|---|
| A（中） | (0, 0, 0) | 1e5 M☉ = 3.329460837e10 M⊕ | (−0.5386, 0, 0) | −2.55 | 5e5 cm |
| B | (−200, 0, 0) | 1e5 M☉ | (0, +157.19, 0) | +745.1 | 5e5 cm |
| C | (+200, 0, 0) | 1e5 M☉ | (0, −157.19, 0) | −745.1 | 5e5 cm |
| 行星 P | (0, 100, 0) | 250 M☉ = 8.3236520936e7 M⊕ | (+215.42, 0, 0) | +1021.2 | 2e6 cm |

推导（`G = 4π²`，距离单位 AU）：
- B/C 的向心加速度 `a = G·(M_A/d² + M_C/(2d)² + M_P·cosθ/r_PB²)`，d = 200，
  `M_P=250` 时 `a = 123.55` → `v = sqrt(a·d) = 157.19 AU/年`。
- P 的加速度 `a = G·(M_A/100² + 2·M_C·(100/223.6)/223.6²) = 465.4` → `v = 215.96`；
  扣掉 A 被 P 拉动的部分（`G·250/100² = 0.987`）后相对速度 `215.96`，
  再按零总动量分配 → A `−0.5386`、P `+215.42`。
- 行星质量选 250 M☉ 而不是 1 M⊕：Hill 半径 `r_H = 100·(250/3e5)^(1/3) ≈ 9.4 AU = 9.4e6 cm`，
  远大于行星半径 2e6 cm、也远大于飞船离行星中心 2.39e6 cm（比值 0.25，低于被剥离的 0.36 经验阈值）。
  若用 1 M⊕，`r_H ≈ 2.15e4 cm` 比行星半径还小，飞船在**地表**就会被恒星剥走。

- [ ] **Step 1: 写测试**

新建 `Source/StellarEraCosmos/Src/Tests/FourBodyStabilityTests.cpp`（**UTF-8 with BOM**）：

```cpp
// 三恒星 + 一行星：跑 10 模拟年，能量漂移与逃逸都要在阈值内。
//
// 这是本次改动唯一的"会不会跑飞"的守门测试。参数一改就该重跑它。

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "NBodySimulator.h"
#include "CosmosTypes.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	/** 三恒星 + 一行星的定稿初态。位置 AU、速度 AU/年、质量 M☉。 */
	void AddFourBodySystem(FNBodySimulator& Simulator)
	{
		auto Add = [&Simulator](const FVector& Position, const FVector& Velocity, double MassSolar)
		{
			FCosmosBodyState Body;
			Body.Position = Position;
			Body.Velocity = Velocity;
			Body.Mass = MassSolar;
			Simulator.AddBody(Body);
		};

		const double StarMass = 1.0e5;
		const double PlanetMass = 250.0;

		Add(FVector(0.0, 0.0, 0.0),    FVector(-0.5386, 0.0, 0.0),  StarMass);
		Add(FVector(-200.0, 0.0, 0.0), FVector(0.0, 157.19, 0.0),   StarMass);
		Add(FVector(200.0, 0.0, 0.0),  FVector(0.0, -157.19, 0.0),  StarMass);
		Add(FVector(0.0, 100.0, 0.0),  FVector(215.42, 0.0, 0.0),   PlanetMass);
	}

	FNBodySimulator MakeFourBodySimulator()
	{
		FNBodySimulator Simulator;
		Simulator.SetUnitSystem(ECosmosUnitSystem::Game);
		Simulator.SetIntegrator(ECosmosIntegrator::VelocityVerlet);
		Simulator.SetSofteningLength(1.0e-4);
		Simulator.SetCentimetersPerAu(1.0e6);
		AddFourBodySystem(Simulator);
		return Simulator;
	}
} // namespace

// 10 模拟年 ≈ 50 分钟真实时间（TimeScale 0.00333，1 模拟年 ≈ 300 秒）。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraFourBodyStabilityTest,
	"StellarEra.Cosmos.Simulator.FourBodyStability",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FStellarEraFourBodyStabilityTest::RunTest(const FString& Parameters)
{
	FNBodySimulator Simulator = MakeFourBodySimulator();
	Simulator.MarkEnergyBaseline();

	constexpr double TotalYears = 10.0;
	constexpr double StepYears = 1.0e-3;
	const int32 StepCount = FMath::RoundToInt(TotalYears / StepYears);

	for (int32 Index = 0; Index < StepCount; ++Index)
	{
		Simulator.StepExact(StepYears);
	}

	const double Drift = Simulator.GetEnergyDriftRatio();
	TestTrue(FString::Printf(TEXT("10 模拟年能量漂移 %.3e 应 < 1e-4"), Drift), Drift < 1.0e-4);

	// 没有天体被甩出去：最远的天体应在 500 AU 以内（初态最远 200 AU）。
	for (int32 Index = 0; Index < 4; ++Index)
	{
		const double Radius = Simulator.GetBody(Index).Position.Size();
		TestTrue(FString::Printf(TEXT("天体 %d 的轨道半径 %.1f AU 应 < 500"), Index, Radius), Radius < 500.0);
	}

	return true;
}

// 初速表本身也是事实：把定稿数值钉住，防止有人按二体直觉改回去。
// （二体公式会给 666 km/s = 140.5 AU/年，比正确值低 11%。）
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraFourBodyVelocitiesTest,
	"StellarEra.Cosmos.Simulator.FourBodyInitialVelocities",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FStellarEraFourBodyVelocitiesTest::RunTest(const FString& Parameters)
{
	FNBodySimulator Simulator = MakeFourBodySimulator();

	const double ExpectedAuPerYear[4] = { 0.5386, 157.19, 157.19, 215.42 };
	for (int32 Index = 0; Index < 4; ++Index)
	{
		const double Speed = Simulator.GetBody(Index).Velocity.Size();
		TestEqual(FString::Printf(TEXT("天体 %d 的初速"), Index), Speed, ExpectedAuPerYear[Index], 0.01);
	}

	// 总动量应为零：系统质心不会漂。
	FVector Momentum = FVector::ZeroVector;
	for (int32 Index = 0; Index < 4; ++Index)
	{
		const FCosmosBodyState& Body = Simulator.GetBody(Index);
		Momentum += Body.Velocity * Body.Mass;
	}
	TestTrue(FString::Printf(TEXT("总动量 %.3e 应 ≈ 0"), Momentum.Size()),
		Momentum.Size() < 1.0e-2);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
```

- [ ] **Step 2: 构建并跑，记录实际漂移**

Run: 构建 + `Automation RunTests StellarEra.Cosmos.Simulator`
Expected: 三条模拟器测试通过。**把日志里打印的实际漂移值记下来**，写进提交信息。
若 `FourBodyStability` 失败（漂移 > 1e-4 或半径 > 500），**先不要改阈值**：
把日志里的实际数值报给用户，按 spec §8 的退路调数据（降行星质量 / 拉大 B、C 间距），
改完重跑，直到通过或确认这套构型不可用。

- [ ] **Step 3: 提交**

```bash
git commit -m "test(恒星): 三恒星 + 一行星 10 模拟年的能量漂移守门测试" -- \
  Source/StellarEraCosmos/Src/Tests/FourBodyStabilityTests.cpp
```

---

### Task 6: 关卡数据（`UniverseMap`）

**Files:**
- Create: `C:/Users/23080/ue_scratch/build_sun_system.py`（仓库外，不进 git）
- Modify: `Content/Code/Maps/UniverseMap.umap` 及其外部 Actor 包（由脚本写）

**Interfaces:**
- Consumes: Task 4 的 `/Script/StellarEraCosmos.StarBodyActor`；Task 5 的初速表
- Produces: 关卡里 3 个 `AStarBodyActor` + 1 颗行星

**关卡现状（2026-11-02 实测）：** 12 个 Actor，其中三颗 `BP_VoxelPlanet`：

| 标签 | 位置 (cm) | MassEarth | 初速 (km/s) |
|---|---|---|---|
| `BP_VoxelPlanet` | (−5e7, 0, 0) | 8.3236520936e8（2500 M☉） | (0, +237.02, 0) |
| `BP_VoxelPlanet2` | (+5e7, 0, 0) | 同上 | (0, −237.02, 0) |
| `BP_VoxelPlanet3` | (0, 0, 0) | 3.329460837e9（1e4 M☉） | 0 |

外加 `DirectionalLight`(0,0,90)、`PostProcessVolume`、`BP_SpaceshipPawn`(0,0,2.39e6)、
`PlayerStart`(0,0,2.39e6)、三个 `BP_StarMapCameraPawn`（各在一个行星处）。
`BP_VoxelPlanet3` 的体素表面半径是 2e6 cm（大气壳缩放 43600 × 50cm ÷ 1.09）。

**保留 `BP_VoxelPlanet3`**（唯一在原点、飞船与 PlayerStart 就摆在它上方的那颗），删掉另外两颗及其相机。

> **两条必须遵守的脚本纪律**（都是这个仓库踩过的坑）：
> 1. **增删 Actor 与写属性分两轮。** 同一轮里跑过 `add_new_subobject` 后，
>    `InitialVelocityKmPerSecond` 的写入会被静默还原。
> 2. **每个属性写完立刻读回并打日志**，不读回等于没写。

- [ ] **Step 1: 写脚本（第一轮：删两行星 + 加三恒星）**

新建 `C:/Users/23080/ue_scratch/build_sun_system.py`：

```python
# -*- coding: utf-8 -*-
"""UniverseMap -> 三恒星 + 一行星。

两轮：
  1. 删掉 BP_VoxelPlanet / BP_VoxelPlanet2 及它们的星图相机，加入 3 个 AStarBodyActor；
  2. 搬动保留的行星与随行 Actor，写轨道参数，逐个读回。
"""

import sys
import traceback

import unreal

LOG = unreal.log
MAP = "/Game/Code/Maps/UniverseMap"
KEEP_PLANET = "BP_VoxelPlanet3"
DELETE_PLANETS = ("BP_VoxelPlanet", "BP_VoxelPlanet2")

MASS_STAR = 3.329460837e10      # 1e5 M☉
MASS_PLANET = 8.3236520936e7    # 250 M☉
PLANET_POS = unreal.Vector(0.0, 1.0e8, 0.0)
PLANET_VEL = unreal.Vector(1021.2, 0.0, 0.0)

STARS = (
    ("Star_A", unreal.Vector(0.0, 0.0, 0.0),    unreal.Vector(-2.55, 0.0, 0.0), "MI_Sun_G2V"),
    ("Star_B", unreal.Vector(-2.0e8, 0.0, 0.0), unreal.Vector(0.0, 745.1, 0.0),  "MI_Sun_B3V"),
    ("Star_C", unreal.Vector(2.0e8, 0.0, 0.0),  unreal.Vector(0.0, -745.1, 0.0), "MI_Sun_M2V"),
)

actor_sub = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
level_sub = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)


def actor_by_label(label):
    for a in actor_sub.get_all_level_actors():
        if a.get_actor_label() == label:
            return a
    return None


def orbit_of(actor):
    comps = actor.get_components_by_class(unreal.CosmosOrbitComponent)
    return comps[0] if comps else None


def write_orbit(actor, mass_earth, velocity):
    """写质量与初速，立刻读回。任一项对不上就抛，不留下半改状态。"""
    orbit = orbit_of(actor)
    if orbit is None:
        raise RuntimeError("{} 没有 CosmosOrbitComponent".format(actor.get_actor_label()))

    orbit.set_editor_property("mass_earth", mass_earth)
    orbit.set_editor_property("initial_velocity_km_per_second", velocity)

    back_mass = orbit.get_editor_property("mass_earth")
    back_vel = orbit.get_editor_property("initial_velocity_km_per_second")
    LOG("[sun] {:<16} mass={:.6e} vel=({:.2f},{:.2f},{:.2f})".format(
        actor.get_actor_label(), back_mass, back_vel.x, back_vel.y, back_vel.z))

    if abs(back_mass - mass_earth) > 1e-9 * max(1.0, abs(mass_earth)):
        raise RuntimeError("{} 质量读回不符：{} != {}".format(
            actor.get_actor_label(), back_mass, mass_earth))
    if (back_vel - velocity).length() > 0.01:
        raise RuntimeError("{} 初速读回不符：{} != {}".format(
            actor.get_actor_label(), back_vel, velocity))


def move_to(actor, location):
    actor.set_actor_location(location, False, False)
    back = actor.get_actor_location()
    LOG("[sun] 搬到 {:<20} ({:.0f},{:.0f},{:.0f})".format(
        actor.get_actor_label(), back.x, back.y, back.z))
    return back


def round_one():
    LOG("[sun] ===== 第一轮：删两颗行星、加三颗恒星 =====")

    for label in DELETE_PLANETS:
        actor = actor_by_label(label)
        if actor is None:
            LOG("[sun] 待删的 {} 不存在，跳过".format(label))
            continue
        loc = actor.get_actor_location()
        # 同一位置的星图相机是给这颗行星用的，一起删。
        for cam in actor_sub.get_all_level_actors():
            if isinstance(cam, unreal.StarMapCameraPawn) and (cam.get_actor_location() - loc).length() < 1.0:
                LOG("[sun] 删除相机 {}".format(cam.get_actor_label()))
                actor_sub.destroy_actor(cam)
        LOG("[sun] 删除行星 {} @ ({:.0f},{:.0f},{:.0f})".format(label, loc.x, loc.y, loc.z))
        actor_sub.destroy_actor(actor)

    star_class = unreal.load_class(None, "/Script/StellarEraCosmos.StarBodyActor")
    if star_class is None:
        raise RuntimeError("找不到 /Script/StellarEraCosmos.StarBodyActor（模块没编译？）")

    for label, location, _velocity, material_name in STARS:
        if actor_by_label(label) is not None:
            LOG("[sun] {} 已存在，跳过创建".format(label))
            continue
        star = actor_sub.spawn_actor_from_class(star_class, location, unreal.Rotator(0.0, 0.0, 0.0))
        if star is None:
            raise RuntimeError("生成 {} 失败".format(label))
        star.set_actor_label(label)

        mat_path = "/Game/Code/Res/Star/Materials/{}".format(material_name)
        material = unreal.load_asset(mat_path)
        if material is None:
            raise RuntimeError("找不到材质 {}".format(mat_path))
        for mesh in star.get_components_by_class(unreal.StaticMeshComponent):
            mesh.set_material(0, material)
        LOG("[sun] 生成 {} @ ({:.0f},{:.0f},{:.0f}) 材质 {}".format(
            label, location.x, location.y, location.z, material_name))


def round_two():
    LOG("[sun] ===== 第二轮：搬动 + 写轨道参数 =====")

    planet = actor_by_label(KEEP_PLANET)
    if planet is None:
        raise RuntimeError("保留的行星 {} 不见了".format(KEEP_PLANET))

    delta = PLANET_POS - planet.get_actor_location()

    # 行星、它的星图相机、飞船、PlayerStart 一起平移，保持相对关系。
    move_to(planet, PLANET_POS)
    for label in ("BP_StarMapCameraPawn", "PlayerStart", "BP_SpaceshipPawn"):
        target = actor_by_label(label)
        if target is None:
            LOG("[sun] {} 不存在，跳过".format(label))
            continue
        move_to(target, target.get_actor_location() + delta)

    write_orbit(planet, MASS_PLANET, PLANET_VEL)

    # 飞船与行星同速，否则一出生就被甩在后面。
    ship = actor_by_label("BP_SpaceshipPawn")
    if ship is not None:
        orbit = orbit_of(ship)
        if orbit is not None:
            write_orbit(ship, orbit.get_editor_property("mass_earth"), PLANET_VEL)

    for label, _location, velocity, _material in STARS:
        star = actor_by_label(label)
        if star is None:
            raise RuntimeError("恒星 {} 不见了".format(label))
        write_orbit(star, MASS_STAR, velocity)


def run(round_name):
    LOG("[sun] load_level -> {}".format(level_sub.load_level(MAP)))
    if round_name == "one":
        round_one()
    else:
        round_two()
    LOG("[sun] 当前 Actor 数 = {}".format(len(actor_sub.get_all_level_actors())))
    LOG("[sun] save_current_level -> {}".format(level_sub.save_current_level()))


try:
    _round = "one"
    for _i, _arg in enumerate(sys.argv):
        if _arg == "--round" and _i + 1 < len(sys.argv):
            _round = sys.argv[_i + 1]
    run(_round)
    LOG("[sun] DONE round={}".format(_round))
except Exception:
    LOG("[sun] EXC\n" + traceback.format_exc())
finally:
    unreal.SystemLibrary.quit_editor()
```

`_round` 的默认值每一轮手动改：第一轮保持 `"one"`，跑完改成 `"two"` 再跑一次
（`UnrealEditor-Cmd` 会把不认识的命令行参数吃掉，别指望 `--round` 能透传）。

- [ ] **Step 2: 跑第一轮**

```bash
MSYS2_ARG_CONV_EXCL='*' "D:/Application/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" \
  "F:/workspace/ue/StellarEra/StellarEra.uproject" \
  -run=pythonscript -script="C:/Users/23080/ue_scratch/build_sun_system.py" \
  -unattended -nopause -nullrhi -nosplash -stdout \
  -abslog="C:/Users/23080/ue_scratch/sun_round1.log"
```

Expected: 日志里 `删除行星 BP_VoxelPlanet*` 两条、`生成 Star_A/B/C` 三条、`DONE round=one`。
若出现 `EXC`，按 traceback 修脚本，**不要**手工在编辑器里补。

- [ ] **Step 3: 跑第二轮**

把脚本里的 `_round = "one"` 改成 `"two"`，用同样的命令行再跑一次（日志换 `sun_round2.log`）。

Expected: 五行 `mass=... vel=(...)`，数值与 Task 5 的表格一致；`DONE round=two`。

- [ ] **Step 4: 独立复核关卡**

跑 `C:/Users/23080/ue_scratch/dump_sun.py`（本次会话已有的 dump 脚本），
Expected：
- `Actor 总数 = 11`（原 12，删 2 行星 + 2 相机 = −4，加 3 恒星 = +3）
- 三个 `StarBodyActor` 在 (0,0,0) / (±2e8,0,0)，各自 `CosmosOrbit` 质量
  `3.329460837e10`、初速与表格一致
- `BP_VoxelPlanet3` 在 (0,1e8,0)，质量 `8.3236520936e7`，初速 `(1021.2,0,0)`
- `BP_VoxelPlanet` / `BP_VoxelPlanet2` 已不存在

- [ ] **Step 5: 提交关卡资产**

```bash
git status -- Content/Code/Maps
git commit -m "feat(关卡): UniverseMap 改成三恒星 + 一行星，恒星参与 N 体并驱动大气" -- \
  Content/Code/Maps/
```

> `git status -- Content/Code/Maps` 先确认外部 Actor 包确实在变更列表里再提交；
> 只把实际变更的路径放进 pathspec。

---

### Task 7: 全量验证

**Files:** 无改动

- [ ] **Step 1: 构建通过**

Run: 构建命令。
Expected: `Result: Succeeded`，无新增 warning。

- [ ] **Step 2: 全量测试**

Run: `Automation RunTests StellarEra`
Expected: **全部通过**（改动前是 173 条）。新增 5 条：`StarBody.FindNearest`、
`SunDirection.Compute`、`StarBodyActor.Skeleton`、`Simulator.FourBodyStability`、
`Simulator.FourBodyInitialVelocities`。若有既有测试回归，先修回归再继续。

- [ ] **Step 3: 关卡能无头加载**

跑 `dump_sun.py` 再确认一次关卡数据没被后续步骤破坏。

- [ ] **Step 4: 交给用户做画面验证（必须由用户跑）**

1. 打开 `UniverseMap`，PIE。
2. 在行星表面抬头：应看到**一个明显的太阳盘**（约 0.57°，与真太阳的 0.53° 接近），
   另两颗是更小的亮点（各约 0.26°）。
3. 地形明暗面与天空的明暗分界应**朝向同一个方向**（最近的那颗恒星）。
4. 观察 30 秒：三颗恒星应整体绕行，行星沿自己的轨道跑，没有天体飞向无穷远。
5. 日志里不该出现 `[大气] 关卡里既没有 UStarBodyComponent 也没有 ADirectionalLight`。

- [ ] **Step 5: 汇报实际用到的技能与结果**

按 CLAUDE.md 要求说明：本次用了 `superpowers:brainstorming` → `superpowers:writing-plans`
→ `superpowers:executing-plans`，C++ 部分依据 `unreal-engine-cpp-pro`，
收尾依据 `superpowers:verification-before-completion`。

---

## 已知会留下的小瑕疵（不在本次范围）

- **星图面板的地表重力**：恒星是 1e5 M☉ / 5 公里半径，`UStarMapUISystem::ComputeSurfaceGravityMps2`
  会算出天文数字。要修得给恒星单独的口径，属于星图 UI 的事。
- **被删掉的 `BP_VoxelPlanet` / `BP_VoxelPlanet2` 的体素存档**仍留在 `Content/Saves/PlanetVoxel/**`。
  清理要单独确认，别和这次改动混在一起。
- **`Content/Code/Planet/BP_Star.uasset`** 现在是完全无用的空壳（恒星改用 `AStarBodyActor`）。
  留着还是删掉由用户决定。
