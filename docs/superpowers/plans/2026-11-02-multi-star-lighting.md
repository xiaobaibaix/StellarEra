# 多恒星光照实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 让 `UniverseMap` 里三颗恒星同时照亮行星——大气与体积云按太阳列表求和着色，每颗恒星驱动自己那盏定向光，只有主星的光占用前向着色 / 大气太阳光名额。

**Architecture:** 恒星身份继续由 `UStarBodyComponent`（Planet 模块）承载；它新增可绑定的 `SunLight`、从自身材质实例读出的颜色与相对亮度，以及一个把灯指向行星的 `DriveSunLight()`。`UPlanetAtmosphereComponent` 每帧收集全部恒星、按距离排序、取前 `PlanetClouds::MaxSuns`（4）颗写进材质的 `SunDirection0..3` / `SunRadiance0..3` / `SunCount`，并逐颗驱动灯。HLSL 侧把单太阳改成固定 4 槽的 `[unroll]` 循环求和；云自遮挡只对槽位 0（最近的星）跑，环境光底也只算一次，保证 N=1 时与改动前逐像素等价。

**Tech Stack:** Unreal Engine 5.8.3 / Win64 / C++17；UE 材质 Custom 节点 + `Shaders/PlanetClouds.ush`（HLSL）；UE Automation Test。

**Spec:** `docs/superpowers/specs/2026-11-02-multi-star-lighting-design.md`

## Global Constraints

- 回复用中文；代码、命令、日志原文不译。
- 含中文的 `.h` / `.cpp` 必须存 **UTF-8 with BOM**。`Shaders/PlanetClouds.ush` 反过来：**纯 ASCII、无 BOM**（它喂给 shader 预处理器）。
- 不要统一换行符。仓库本来就是 CRLF/LF 混合，`autocrlf=true` 下 CRLF 是稳定态。
- 构建前**必须关闭编辑器**（Live Coding 会锁 DLL）。构建命令（cwd 在项目外）：
  ```bash
  "D:/Application/Epic Games/UE_5.8/Engine/Binaries/ThirdParty/DotNet/10.0/win-x64/dotnet.exe" \
    "D:/Application/Epic Games/UE_5.8/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.dll" \
    StellarEraEditor Win64 Development \
    -Project="F:/workspace/ue/StellarEra/StellarEra.uproject" -WaitMutex -FromMsBuild
  ```
  不要用 `Build.bat`。
- 自动化测试（编辑器开着也能跑）：
  ```bash
  "D:/Application/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" \
    "F:/workspace/ue/StellarEra/StellarEra.uproject" \
    -ExecCmds="Automation RunTests StellarEra" \
    -TestExit="Automation Test Queue Empty" \
    -unattended -nopause -nullrhi -nosplash -stdout -abslog="<临时日志>"
  ```
  判据看日志里的 `**** TEST COMPLETE. EXIT CODE` 与各测试的 `Success`/`Fail` 行。
- 提交时 pathspec 必须挂在 commit 上：`git commit -m "..." -- <paths>`。索引里有常驻暂存文件，裸 `git commit` 会吞掉全部。
- 提交信息末尾加一行：`Co-Authored-By: Claude Code <noreply@anthropic.com>`
- 自定义代码不进 `Plugins/VoxelFree`、`Plugins/VibeUE`。
- 模块依赖只允许 `StellarEraCosmos → StellarEraPlanet`，本计划不改 `Build.cs`。
- 改 `Shaders/PlanetClouds.ush` **必须**同步把 `PlanetAtmosphereMaterialBuilder.cpp` 里的 `#define PLANET_CLOUDS_VERSION` 加 1，否则材质不重编、画面零变化。
- 材质资产只能靠重跑 `StellarEra.BuildAtmosphereMaterial` 重建（不要在编辑器里手改）。

## 文件结构

**新建**

| 文件 | 职责 |
|---|---|
| `Source/StellarEraPlanet/Src/Tests/PlanetMultiSunTests.cpp` | 本功能全部 C++ 自动化测试（太阳列表、颜色/亮度换算、灯绑定）。用具名命名空间 `StellarEraMultiSunTests`，避免 unity build 撞名。 |

**修改**

| 文件 | 改动 |
|---|---|
| `Source/StellarEraPlanet/Src/Atmosphere/StarBodyComponent.h/.cpp` | 新增 `SunLight`、`GetSunColor`、`GetRelativeBrightness`、`DriveSunLight`、`ResolveSunLight`、`FindAll`、两个纯函数；`FindNearest` 改为基于 `FindAll`。 |
| `Source/StellarEraPlanet/Src/Atmosphere/PlanetCloudParams.h` | 新增 `constexpr int32 MaxSuns = 4;`（槽位数唯一来源）。 |
| `Source/StellarEraPlanet/Src/Atmosphere/PlanetAtmosphereMaterialBuilder.cpp` | 删 `SunDirection` 参数，加 `SunDirection0..3` / `SunRadiance0..3` / `SunCount`；`PLANET_CLOUDS_VERSION` 6 → 7；Code 里的调用改新签名。 |
| `Shaders/PlanetClouds.ush` | 加 `FPlanetSunSet` + `CloudBuildSunSet`；`CloudAtmoSegment` / `CloudAtmosphereRaymarch` 改成太阳集合。 |
| `Source/StellarEraPlanet/Src/Atmosphere/PlanetAtmosphereComponent.h/.cpp` | 新增 `FPlanetSun`、`CollectSunsFor`、`ApplySuns`、`DriveSunLights`、`BaseLightIntensity`；删 `ResolveSunDirection` / `CachedSunLight` / `LastSunDirection`。 |
| `Source/StellarEraPlanet/Src/Tests/PlanetAtmosphereMaterialTests.cpp` | 参数名表更新（`SunDirection` → `SunDirection0..3` 等）与 HLSL 断言。 |
| `Content/Maps/UniverseMap`（及三颗星的外部 Actor 包） | 三颗星绑各自 `SunLight`；主星的灯 `ForwardShadingPriority=1` + `bAtmosphereSunLight=True`，另两盏 `0` / `False`。 |

---

## Task 1: 恒星组件的太阳参数与灯驱动

**Files:**
- Modify: `Source/StellarEraPlanet/Src/Atmosphere/StarBodyComponent.h`
- Modify: `Source/StellarEraPlanet/Src/Atmosphere/StarBodyComponent.cpp`
- Test: `Source/StellarEraPlanet/Src/Tests/PlanetMultiSunTests.cpp`（新建）

**Interfaces:**
- Produces:
  - `FLinearColor UStarBodyComponent::GetSunColor() const` — `HotColor` 按最大分量归一化；读不到回退白色。
  - `float UStarBodyComponent::GetRelativeBrightness() const` — `Brightness / 150` 钳到 `[0.05, 4]`。
  - `static FLinearColor UStarBodyComponent::NormalizeSunColor(FLinearColor)`、`static float UStarBodyComponent::BrightnessToRelative(float)` — 纯函数。
  - `ADirectionalLight* UStarBodyComponent::ResolveSunLight()` — 显式 `SunLight` 优先，否则自动认领一盏没被别的恒星占用的灯。
  - `void UStarBodyComponent::DriveSunLight(const FVector& PlanetCenter, float BaseIntensity, bool bIsPrimary)` — 只在游戏世界写灯。
  - `static void UStarBodyComponent::FindAll(const UWorld*, TArray<UStarBodyComponent*>&)`。
  - `UPROPERTY(EditInstanceOnly) TObjectPtr<ADirectionalLight> SunLight`。

- [ ] **Step 1: 写失败的测试**

新建 `Source/StellarEraPlanet/Src/Tests/PlanetMultiSunTests.cpp`（UTF-8 with BOM，因为含中文）：

```cpp
#include "CoreMinimal.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInterface.h"
#include "Misc/AutomationTest.h"
#include "StarBodyComponent.h"

#if WITH_DEV_AUTOMATION_TESTS

// 具名命名空间而不是匿名命名空间：UBT 默认开 unity build，
// 匿名命名空间在 unity blob 里其实是同一个作用域，会和别的测试文件撞名。
namespace StellarEraMultiSunTests
{
	UWorld* GetTestWorld()
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

	/** 裸 spawn 的 Actor 没有根组件，GetActorLocation() 恒为 0（踩过），所以必须补一个。 */
	AActor* SpawnRootedActor(UWorld* World, const FVector& Location, TArray<AActor*>& Spawned)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AActor* Actor = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform(Location), Params);
		if (!Actor)
		{
			return nullptr;
		}
		USceneComponent* Root = NewObject<USceneComponent>(Actor);
		Root->RegisterComponent();
		Actor->SetRootComponent(Root);
		Actor->SetActorLocation(Location);
		Spawned.Add(Actor);
		return Actor;
	}

	/** 根组件是 StaticMeshComponent，用来把恒星材质实例塞进材质槽 0。 */
	AActor* SpawnMeshActor(UWorld* World, const FVector& Location, const TCHAR* MaterialPath,
		TArray<AActor*>& Spawned)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AActor* Actor = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform(Location), Params);
		if (!Actor)
		{
			return nullptr;
		}
		UStaticMeshComponent* Mesh = NewObject<UStaticMeshComponent>(Actor);
		Mesh->RegisterComponent();
		Actor->SetRootComponent(Mesh);
		Actor->SetActorLocation(Location);
		if (UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr, MaterialPath))
		{
			Mesh->SetMaterial(0, Material);
		}
		Spawned.Add(Actor);
		return Actor;
	}

	UStarBodyComponent* AddStarComponent(AActor* Actor)
	{
		if (!Actor)
		{
			return nullptr;
		}
		UStarBodyComponent* Star = NewObject<UStarBodyComponent>(
			Actor, UStarBodyComponent::StaticClass(), TEXT("StarBody"));
		Star->RegisterComponent();
		return Star;
	}

	ADirectionalLight* SpawnLight(UWorld* World, const FVector& Location, TArray<AActor*>& Spawned)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		ADirectionalLight* Light =
			World->SpawnActor<ADirectionalLight>(ADirectionalLight::StaticClass(), FTransform(Location), Params);
		if (Light)
		{
			Spawned.Add(Light);
		}
		return Light;
	}

	void DestroyAll(TArray<AActor*>& Spawned)
	{
		for (AActor* Actor : Spawned)
		{
			if (IsValid(Actor))
			{
				Actor->Destroy();
			}
		}
		Spawned.Reset();
	}

	const TCHAR* const MI_SunM2V = TEXT("/Game/Code/Res/Star/Materials/MI_Sun_M2V.MI_Sun_M2V");
	const TCHAR* const MI_SunG2V = TEXT("/Game/Code/Res/Star/Materials/MI_Sun_G2V.MI_Sun_G2V");
	const TCHAR* const MI_SunB3V = TEXT("/Game/Code/Res/Star/Materials/MI_Sun_B3V.MI_Sun_B3V");
} // namespace StellarEraMultiSunTests

// 纯数学：归一化与相对亮度换算。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraSunRadianceMathTest,
	"StellarEra.Planet.Atmosphere.SunRadiance.Math",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FStellarEraSunRadianceMathTest::RunTest(const FString& Parameters)
{
	const FLinearColor Normalized =
		UStarBodyComponent::NormalizeSunColor(FLinearColor(0.4f, 2.0f, 1.0f, 1.0f));
	TestEqual(TEXT("归一化后最大分量（G）为 1"), Normalized.G, 1.0f, 1e-5f);
	TestEqual(TEXT("归一化保持比例 R"), Normalized.R, 0.2f, 1e-5f);
	TestEqual(TEXT("归一化保持比例 B"), Normalized.B, 0.5f, 1e-5f);

	const FLinearColor Black = UStarBodyComponent::NormalizeSunColor(FLinearColor(0.0f, 0.0f, 0.0f, 1.0f));
	TestEqual(TEXT("全黑回退白色 R"), Black.R, 1.0f, 1e-5f);
	TestEqual(TEXT("全黑回退白色 G"), Black.G, 1.0f, 1e-5f);
	TestEqual(TEXT("全黑回退白色 B"), Black.B, 1.0f, 1e-5f);

	TestEqual(TEXT("Brightness 60 → 0.4"), UStarBodyComponent::BrightnessToRelative(60.0f), 0.4f, 1e-4f);
	TestEqual(TEXT("Brightness 150 → 1.0"), UStarBodyComponent::BrightnessToRelative(150.0f), 1.0f, 1e-4f);
	TestEqual(TEXT("Brightness 400 → 2.6667"), UStarBodyComponent::BrightnessToRelative(400.0f), 2.6667f, 1e-3f);
	TestEqual(TEXT("Brightness 0 钳到下限"), UStarBodyComponent::BrightnessToRelative(0.0f), 0.05f, 1e-5f);
	TestEqual(TEXT("Brightness 1e6 钳到上限"), UStarBodyComponent::BrightnessToRelative(1.0e6f), 4.0f, 1e-5f);

	return true;
}

// 从材质实例读颜色与亮度。不写死预设数值（预设调色会红测试），
// 只断言三个预设的相对关系 —— 这才是"真的读到 MI 覆盖值"的证据。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraSunMaterialReadTest,
	"StellarEra.Planet.Atmosphere.SunRadiance.MaterialRead",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FStellarEraSunMaterialReadTest::RunTest(const FString& Parameters)
{
	UWorld* World = StellarEraMultiSunTests::GetTestWorld();
	if (!TestNotNull(TEXT("编辑器世界"), World))
	{
		return false;
	}

	TArray<AActor*> Spawned;
	const FVector Base(2.0e13, 0.0, 0.0);
	UStarBodyComponent* M2V = StellarEraMultiSunTests::AddStarComponent(
		StellarEraMultiSunTests::SpawnMeshActor(World, Base, StellarEraMultiSunTests::MI_SunM2V, Spawned));
	UStarBodyComponent* G2V = StellarEraMultiSunTests::AddStarComponent(
		StellarEraMultiSunTests::SpawnMeshActor(World, Base + FVector(0, 1.0e6, 0),
			StellarEraMultiSunTests::MI_SunG2V, Spawned));
	UStarBodyComponent* B3V = StellarEraMultiSunTests::AddStarComponent(
		StellarEraMultiSunTests::SpawnMeshActor(World, Base + FVector(0, 2.0e6, 0),
			StellarEraMultiSunTests::MI_SunB3V, Spawned));

	if (TestNotNull(TEXT("M2V 恒星"), M2V) && TestNotNull(TEXT("G2V 恒星"), G2V) &&
		TestNotNull(TEXT("B3V 恒星"), B3V))
	{
		TestTrue(TEXT("M2V 比 G2V 暗"), M2V->GetRelativeBrightness() < G2V->GetRelativeBrightness());
		TestTrue(TEXT("G2V 比 B3V 暗"), G2V->GetRelativeBrightness() < B3V->GetRelativeBrightness());
		TestTrue(TEXT("相对亮度都在物理量级内"),
			G2V->GetRelativeBrightness() > 0.1f && G2V->GetRelativeBrightness() < 10.0f);
		TestFalse(TEXT("两颗不同预设的颜色不同"),
			M2V->GetSunColor().Equals(B3V->GetSunColor(), 1e-3f));
		TestEqual(TEXT("归一化后最大分量为 1（G2V）"),
			FMath::Max3(G2V->GetSunColor().R, G2V->GetSunColor().G, G2V->GetSunColor().B), 1.0f, 1e-3f);
	}

	StellarEraMultiSunTests::DestroyAll(Spawned);
	return true;
}

// 灯绑定：显式优先；不显式时两颗星不会认领同一盏灯。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraSunLightBindTest,
	"StellarEra.Planet.Atmosphere.SunLight.Bind",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FStellarEraSunLightBindTest::RunTest(const FString& Parameters)
{
	UWorld* World = StellarEraMultiSunTests::GetTestWorld();
	if (!TestNotNull(TEXT("编辑器世界"), World))
	{
		return false;
	}

	TArray<AActor*> Spawned;
	UStarBodyComponent* CompA = StellarEraMultiSunTests::AddStarComponent(
		StellarEraMultiSunTests::SpawnRootedActor(World, FVector(3.0e13, 0.0, 0.0), Spawned));
	UStarBodyComponent* CompB = StellarEraMultiSunTests::AddStarComponent(
		StellarEraMultiSunTests::SpawnRootedActor(World, FVector(3.0e13, 1.0e8, 0.0), Spawned));
	ADirectionalLight* LightA = StellarEraMultiSunTests::SpawnLight(World, FVector(0, 0, 1.0e10), Spawned);
	ADirectionalLight* LightB = StellarEraMultiSunTests::SpawnLight(World, FVector(0, 1.0e10, 0), Spawned);

	if (!TestNotNull(TEXT("恒星 A"), CompA) || !TestNotNull(TEXT("恒星 B"), CompB) ||
		!TestNotNull(TEXT("灯 A"), LightA) || !TestNotNull(TEXT("灯 B"), LightB))
	{
		StellarEraMultiSunTests::DestroyAll(Spawned);
		return false;
	}

	// 显式绑定优先。
	CompA->SunLight = LightA;
	CompB->SunLight = LightB;
	TestSamePtr(TEXT("A 用显式指定的灯"), CompA->ResolveSunLight(), LightA);
	TestSamePtr(TEXT("B 用显式指定的灯"), CompB->ResolveSunLight(), LightB);

	// 都不显式时各自认领不同的灯。
	CompA->SunLight = nullptr;
	CompB->SunLight = nullptr;
	ADirectionalLight* AutoA = CompA->ResolveSunLight();
	ADirectionalLight* AutoB = CompB->ResolveSunLight();
	TestNotNull(TEXT("A 自动认领到一盏灯"), AutoA);
	TestNotNull(TEXT("B 自动认领到一盏灯"), AutoB);
	TestFalse(TEXT("两颗星不共用同一盏灯"), AutoA == AutoB);

	StellarEraMultiSunTests::DestroyAll(Spawned);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
```

- [ ] **Step 2: 跑测试确认失败**

编辑器关着也能跑自动化测试：

```bash
"D:/Application/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" \
  "F:/workspace/ue/StellarEra/StellarEra.uproject" \
  -ExecCmds="Automation RunTests StellarEra.Planet.Atmosphere.Sun" \
  -TestExit="Automation Test Queue Empty" \
  -unattended -nopause -nullrhi -nosplash -stdout -abslog="$TEMP/ms_task1_before.log"
```

预期：**编译失败**（`NormalizeSunColor` 等成员不存在）。这正是 TDD 的"红"。

- [ ] **Step 3: 改头文件**

把 `Source/StellarEraPlanet/Src/Atmosphere/StarBodyComponent.h` 里的前置声明改成：

```cpp
class ADirectionalLight;
class UMaterialInterface;
class UWorld;
```

类注释里删掉这三行（颜色不再"不冗余一份"了）：

```text
 * 恒星的颜色由各自的材质实例（MI_Sun_*）承载，这里不再冗余一份：
 * 多恒星光照本就列为非目标，留一个没人消费的颜色字段只会误导后来者。
```

换成：

```text
 * 恒星的颜色与相对亮度**不在本组件上冗余字段**，每帧从 Owner 网格的材质实例
 * （MI_Sun_*）的 HotColor / Brightness 读出来并缓存 —— 改动只在材质实例上做，
 * 组件侧不需要同步任何数值。
```

在 `bDrivesDirectionalLight` 之后、`FindNearest` 之前插入：

```cpp
	/**
	 * 这颗恒星驱动的定向光。
	 *
	 * 关卡里显式指定（`EditInstanceOnly`）；留空则自动认领关卡里**没被别的恒星占用**
	 * 的第一盏 ADirectionalLight，并把结果缓存在 AutoSunLight。
	 * 显式绑定存在的意义：自动认领按 Actor 遍历顺序，谁拿哪盏灯不可预期，
	 * 而"哪盏灯是主光"要写进资产才能消掉编辑器里那条竞争警告。
	 */
	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category = "StellarEra|Star")
	TObjectPtr<ADirectionalLight> SunLight;

	/**
	 * 辐射色：恒星网格材质实例的 HotColor，按最大分量归一化。
	 * 材质读不到（没有网格 / 没有材质 / 参数名不对）时回退白色。
	 */
	UFUNCTION(BlueprintCallable, Category = "StellarEra|Star")
	FLinearColor GetSunColor() const;

	/**
	 * 相对亮度：材质里的 Brightness ÷ 150（G2V 预设的现值），钳到 [0.05, 4]。
	 *
	 * 为什么不是物理光度：Brightness 是"太阳在屏幕上看起来多大"的美术旋钮
	 * （60 / 150 / 400），直接当光度用会让 M2V 照的行星黑成一片。
	 * 除以 150 得到的 0.4 / 1.0 / 2.67 是刻意压缩过的艺术比值。
	 */
	UFUNCTION(BlueprintCallable, Category = "StellarEra|Star")
	float GetRelativeBrightness() const;

	/** 按最大分量归一化；全黑或非正时返回白色。纯函数，供测试。 */
	static FLinearColor NormalizeSunColor(FLinearColor Color);

	/** Brightness → 相对亮度（÷150、钳 [0.05, 4]）。纯函数，供测试。 */
	static float BrightnessToRelative(float Brightness);

	/** 显式 SunLight 优先；否则自动认领一盏没被别的恒星占用的灯（结果缓存）。 */
	ADirectionalLight* ResolveSunLight();

	/**
	 * 让灯从 PlanetCenter 的方向照过来，同时写入颜色、强度与主光名额。
	 *
	 * **只在游戏世界执行**：编辑器视口里每帧转灯会把关卡一直标脏。
	 * 编辑器里要消除"多个定向光源竞争"的警告，靠的是关卡资产里的
	 * ForwardShadingPriority / bAtmosphereSunLight（Task 4 写入），不是这里。
	 */
	void DriveSunLight(const FVector& PlanetCenter, float BaseIntensity, bool bIsPrimary);

	/** 世界里全部恒星组件。 */
	static void FindAll(const UWorld* World, TArray<UStarBodyComponent*>& OutStars);
```

在 `FindNearest` 声明之后、类结束前插入私有段：

```cpp
private:
	/** 读材质并更新缓存；材质指针变了才重读。 */
	void RefreshSunParameters() const;

	mutable FLinearColor CachedSunColor = FLinearColor::White;
	mutable float CachedRelativeBrightness = 1.0f;
	mutable TWeakObjectPtr<const UMaterialInterface> CachedColorSource;
	mutable bool bSunParametersValid = false;

	UPROPERTY(Transient)
	TObjectPtr<ADirectionalLight> AutoSunLight;

	/** 上次写进自己那盏灯的优先级 / 大气太阳光，用来做边沿触发。 */
	int32 AppliedForwardShadingPriority = INDEX_NONE;
	bool bAppliedAtmosphereSunLight = false;
```

- [ ] **Step 4: 改实现**

`Source/StellarEraPlanet/Src/Atmosphere/StarBodyComponent.cpp` 整个文件替换为：

```cpp
#include "StarBodyComponent.h"

#include "Components/DirectionalLightComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInterface.h"

namespace
{
	// G2V 预设的 Brightness（Source/StellarEraPlanet/Src/Star/SunMaterialBuilder.cpp 的
	// FSolarPreset 表）。相对亮度的标定基准：G2V = 1.0。
	constexpr float ReferenceBrightness = 150.0f;
	constexpr float MinRelativeBrightness = 0.05f;
	constexpr float MaxRelativeBrightness = 4.0f;

	/** 归一化时"算黑"的门槛，避免除以一个 float 噪声。 */
	constexpr float BlackColorThreshold = 1e-4f;
}

UStarBodyComponent::UStarBodyComponent()
{
	// 只存参数，不参与 Tick。
	PrimaryComponentTick.bCanEverTick = false;
}

FLinearColor UStarBodyComponent::NormalizeSunColor(FLinearColor Color)
{
	const float MaxComponent = FMath::Max3(Color.R, Color.G, Color.B);
	if (MaxComponent <= BlackColorThreshold)
	{
		return FLinearColor::White;
	}
	return FLinearColor(Color.R / MaxComponent, Color.G / MaxComponent, Color.B / MaxComponent, 1.0f);
}

float UStarBodyComponent::BrightnessToRelative(float Brightness)
{
	return FMath::Clamp(Brightness / ReferenceBrightness, MinRelativeBrightness, MaxRelativeBrightness);
}

void UStarBodyComponent::RefreshSunParameters() const
{
	const AActor* Owner = GetOwner();
	const UStaticMeshComponent* Mesh = Owner ? Owner->FindComponentByClass<UStaticMeshComponent>() : nullptr;
	const UMaterialInterface* Material = Mesh ? Mesh->GetMaterial(0) : nullptr;

	if (bSunParametersValid && Material == CachedColorSource.Get())
	{
		return;
	}

	CachedColorSource = Material;
	bSunParametersValid = true;
	CachedSunColor = FLinearColor::White;
	CachedRelativeBrightness = 1.0f;

	if (!Material)
	{
		return;
	}

	// UMaterialInstance 覆写了 GetParameterValue，取到的是实例覆盖后的值，不是父材质默认值。
	FLinearColor HotColor = FLinearColor::White;
	if (Material->GetVectorParameterValue(TEXT("HotColor"), HotColor))
	{
		CachedSunColor = NormalizeSunColor(HotColor);
	}

	float Brightness = ReferenceBrightness;
	if (Material->GetScalarParameterValue(TEXT("Brightness"), Brightness))
	{
		CachedRelativeBrightness = BrightnessToRelative(Brightness);
	}
}

FLinearColor UStarBodyComponent::GetSunColor() const
{
	RefreshSunParameters();
	return CachedSunColor;
}

float UStarBodyComponent::GetRelativeBrightness() const
{
	RefreshSunParameters();
	return CachedRelativeBrightness;
}

void UStarBodyComponent::FindAll(const UWorld* World, TArray<UStarBodyComponent*>& OutStars)
{
	OutStars.Reset();
	if (!World)
	{
		return;
	}

	// TActorIterator 只收非 const UWorld*。这里不改世界，const_cast 是有意的。
	for (TActorIterator<AActor> It(const_cast<UWorld*>(World)); It; ++It)
	{
		if (UStarBodyComponent* Star = It->FindComponentByClass<UStarBodyComponent>())
		{
			OutStars.Add(Star);
		}
	}
}

UStarBodyComponent* UStarBodyComponent::FindNearest(const UWorld* World, const FVector& FromLocation)
{
	TArray<UStarBodyComponent*> Stars;
	FindAll(World, Stars);

	UStarBodyComponent* Nearest = nullptr;
	double NearestSquared = TNumericLimits<double>::Max();
	for (UStarBodyComponent* Star : Stars)
	{
		const AActor* Owner = Star ? Star->GetOwner() : nullptr;
		if (!Owner)
		{
			continue;
		}
		const double Squared = FVector::DistSquared(Owner->GetActorLocation(), FromLocation);
		if (Squared < NearestSquared)
		{
			NearestSquared = Squared;
			Nearest = Star;
		}
	}
	return Nearest;
}

ADirectionalLight* UStarBodyComponent::ResolveSunLight()
{
	if (IsValid(SunLight))
	{
		return SunLight;
	}
	if (IsValid(AutoSunLight))
	{
		return AutoSunLight;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}

	TArray<UStarBodyComponent*> Stars;
	FindAll(World, Stars);

	for (TActorIterator<ADirectionalLight> It(World); It; ++It)
	{
		ADirectionalLight* Candidate = *It;
		if (!IsValid(Candidate))
		{
			continue;
		}

		bool bClaimed = false;
		for (const UStarBodyComponent* Other : Stars)
		{
			if (Other == this)
			{
				continue;
			}
			// 显式绑定与已缓存的自动认领都算"被占用"，否则两颗星会抢同一盏灯。
			if (Other->SunLight == Candidate || Other->AutoSunLight == Candidate)
			{
				bClaimed = true;
				break;
			}
		}
		if (bClaimed)
		{
			continue;
		}

		AutoSunLight = Candidate;
		return Candidate;
	}

	return nullptr;
}

void UStarBodyComponent::DriveSunLight(const FVector& PlanetCenter, float BaseIntensity, bool bIsPrimary)
{
	ADirectionalLight* Light = ResolveSunLight();
	if (!Light)
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World || !World->IsGameWorld())
	{
		// 编辑器视口不转灯：每帧转灯会把关卡一直标脏。
		return;
	}

	const AActor* Owner = GetOwner();
	const FVector StarLocation = Owner ? Owner->GetActorLocation() : FVector::ZeroVector;
	const FVector Travel = (PlanetCenter - StarLocation).GetSafeNormal();

	// Stationary / Static 的组件拒绝运行时转向，并逐帧刷 "Mobility must be 'Movable'"。
	// 关卡里这些灯是静态摆放的，这里切一次。
	if (USceneComponent* LightRoot = Light->GetRootComponent())
	{
		if (LightRoot->Mobility != EComponentMobility::Movable)
		{
			LightRoot->SetMobility(EComponentMobility::Movable);
		}
	}

	if (!Travel.IsNearlyZero())
	{
		Light->SetActorRotation(Travel.Rotation());
	}

	UDirectionalLightComponent* LightComponent = Cast<UDirectionalLightComponent>(Light->GetLightComponent());
	if (!LightComponent)
	{
		return;
	}

	RefreshSunParameters();
	LightComponent->SetLightColor(CachedSunColor, /*bSRGB=*/false);
	LightComponent->SetIntensity(BaseIntensity * CachedRelativeBrightness);

	// 边沿触发：引擎那条"多个定向光源竞争"的警告只看这两项，
	// 每帧重写没有意义，还会无谓地标脏。
	const int32 Priority = bIsPrimary ? 1 : 0;
	if (AppliedForwardShadingPriority != Priority)
	{
		AppliedForwardShadingPriority = Priority;
		LightComponent->SetForwardShadingPriority(Priority);
	}
	if (bAppliedAtmosphereSunLight != bIsPrimary)
	{
		bAppliedAtmosphereSunLight = bIsPrimary;
		LightComponent->SetAtmosphereSunLight(bIsPrimary);
	}
}
```

- [ ] **Step 5: 构建 + 跑测试**

关闭编辑器后：

```bash
cd "/d/Application/Epic Games/UE_5.8/Engine/Binaries/ThirdParty/DotNet/10.0/win-x64"
./dotnet.exe "D:/Application/Epic Games/UE_5.8/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.dll" \
  StellarEraEditor Win64 Development \
  -Project="F:/workspace/ue/StellarEra/StellarEra.uproject" -WaitMutex -FromMsBuild
```

预期：`Result: Succeeded`。

然后跑：

```bash
"D:/Application/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" \
  "F:/workspace/ue/StellarEra/StellarEra.uproject" \
  -ExecCmds="Automation RunTests StellarEra.Planet.Atmosphere.Sun" \
  -TestExit="Automation Test Queue Empty" \
  -unattended -nopause -nullrhi -nosplash -stdout -abslog="$TEMP/ms_task1_after.log"
```

预期：`StellarEra.Planet.Atmosphere.SunRadiance.Math`、`.SunRadiance.MaterialRead`、`.SunLight.Bind` 三个 `Success`。

若 `MaterialRead` 报"M2V 比 G2V 暗"失败：说明 `GetScalarParameterValue` 没读到实例覆盖值 —— 改用
`Material->GetParameterValue(FHashedMaterialParameterInfo(TEXT("Brightness")), ...)` 再试，
并确认 `MI_Sun_*` 的父材质里参数名确实叫 `Brightness`（大小写敏感）。

- [ ] **Step 6: 提交**

```bash
git add Source/StellarEraPlanet/Src/Atmosphere/StarBodyComponent.h \
        Source/StellarEraPlanet/Src/Atmosphere/StarBodyComponent.cpp \
        Source/StellarEraPlanet/Src/Tests/PlanetMultiSunTests.cpp
git commit -m "feat(恒星): 组件读自身材质的颜色/亮度，并驱动各自的定向光

- SunLight 显式绑定，留空则自动认领一盏没被别的恒星占用的灯
- NormalizeSunColor / BrightnessToRelative 纯函数，可单测
- DriveSunLight 只在游戏世界写灯；主光名额边沿触发

Co-Authored-By: Claude Code <noreply@anthropic.com>" -- \
        Source/StellarEraPlanet/Src/Atmosphere/StarBodyComponent.h \
        Source/StellarEraPlanet/Src/Atmosphere/StarBodyComponent.cpp \
        Source/StellarEraPlanet/Src/Tests/PlanetMultiSunTests.cpp
```

---

## Task 2: 材质槽位常量 + 九参数 + 多太阳 HLSL

**Files:**
- Modify: `Source/StellarEraPlanet/Src/Atmosphere/PlanetCloudParams.h`
- Modify: `Source/StellarEraPlanet/Src/Atmosphere/PlanetAtmosphereMaterialBuilder.cpp`
- Modify: `Shaders/PlanetClouds.ush`
- Test: `Source/StellarEraPlanet/Src/Tests/PlanetAtmosphereMaterialTests.cpp`

**Interfaces:**
- Consumes: 无。
- Produces:
  - `constexpr int32 PlanetClouds::MaxSuns = 4;`（C++ 与 HLSL 槽位数的唯一来源）。
  - 材质参数 `SunDirection0..3`（Vector，默认 `(1,0,0,0)`）、`SunRadiance0..3`（Vector，0 号默认白、其余黑）、`SunCount`（Scalar，默认 1）。`SunDirection` 删除，`SunIntensity` 保留。
  - HLSL：`#define PLANET_MAX_SUNS 4`、`struct FPlanetSunSet`、`FPlanetSunSet CloudBuildSunSet(...)`、
    `CloudAtmoSegment(..., FPlanetSunSet Suns, ...)`、`CloudAtmosphereRaymarch(..., float3 SunDir0..3, float3 SunRadiance0..3, float SunCount, ...)`。

- [ ] **Step 1: 扩测试（先红）**

在 `Source/StellarEraPlanet/Src/Tests/PlanetAtmosphereMaterialTests.cpp` 里做三处改动。

(a) 文件顶部加 include：

```cpp
#include "PlanetCloudParams.h"
```

(b) `FStellarEraAtmosphereMaterialStructureTest` 里，把标量名表与向量名表换成：

```cpp
	const TCHAR* const ScalarNames[] = {
		TEXT("PlanetRadius"), TEXT("ShellRadius"),
		TEXT("RayleighScaleHeight"), TEXT("MieScaleHeight"),
		TEXT("CloudBaseRadius"), TEXT("CloudTopRadius"), TEXT("CloudCoverage"),
		TEXT("CloudExtinction"), TEXT("CloudNoiseScale"), TEXT("CloudDetailScale"),
		TEXT("CloudDetailStrength"), TEXT("CloudDrift"), TEXT("CloudBrightness"),
		TEXT("SunCount")
	};
	for (const TCHAR* Name : ScalarNames)
	{
		TestTrue(FString::Printf(TEXT("标量参数 %s"), Name),
			StellarEraAtmosphereMaterialTests::HasParameter(ScalarInfos, FName(Name)));
	}

	TArray<FMaterialParameterInfo> VectorInfos;
	TArray<FGuid> VectorIds;
	Material->GetAllVectorParameterInfo(VectorInfos, VectorIds);

	const TCHAR* const VectorNames[] = {
		TEXT("RayleighBeta"), TEXT("MieBeta"), TEXT("PlanetCenter"), TEXT("SunIntensity"),
		TEXT("SunDirection0"), TEXT("SunDirection1"), TEXT("SunDirection2"), TEXT("SunDirection3"),
		TEXT("SunRadiance0"), TEXT("SunRadiance1"), TEXT("SunRadiance2"), TEXT("SunRadiance3")
	};
	for (const TCHAR* Name : VectorNames)
	{
		TestTrue(FString::Printf(TEXT("向量参数 %s"), Name),
			StellarEraAtmosphereMaterialTests::HasParameter(VectorInfos, FName(Name)));
	}

	// 单数形式必须已经消失：留着它等于两套太阳方向，HLSL 只认其中一套。
	TestFalse(TEXT("旧的单数 SunDirection 已删除"),
		StellarEraAtmosphereMaterialTests::HasParameter(VectorInfos, FName(TEXT("SunDirection"))));

	// 槽位数只有一个来源：C++ 的 MaxSuns 与 HLSL 的 PLANET_MAX_SUNS 必须一致。
	TestEqual(TEXT("PlanetClouds::MaxSuns"), PlanetClouds::MaxSuns, 4);
```

(c) `FStellarEraAtmosphereMaterialHlslTest` 里，在 `TestTrue(TEXT("Code 含云层底半径"), ...)` 之后加：

```cpp
	TestTrue(TEXT("Code 传第 4 个太阳方向"), Custom->Code.Contains(TEXT("SunDirection3")));
	TestTrue(TEXT("Code 传太阳数量"), Custom->Code.Contains(TEXT("SunCount")));
```

并在 ush 断言段里 `TestTrue(TEXT("ush 含太阳遮挡"), ...)` 之后追加：

```cpp
	TestTrue(TEXT("ush 含太阳集合结构"), CloudUsh.Contains(TEXT("FPlanetSunSet")));
	TestTrue(TEXT("ush 含固定槽位数"), CloudUsh.Contains(TEXT("PLANET_MAX_SUNS 4")));
	TestTrue(TEXT("ush 的太阳循环被展开"), CloudUsh.Contains(TEXT("[unroll]")));
	TestTrue(TEXT("ush 含太阳集合构造函数"), CloudUsh.Contains(TEXT("CloudBuildSunSet(")));
```

- [ ] **Step 2: 跑测试确认失败**

```bash
"D:/Application/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" \
  "F:/workspace/ue/StellarEra/StellarEra.uproject" \
  -ExecCmds="Automation RunTests StellarEra.Planet.AtmosphereMaterial" \
  -TestExit="Automation Test Queue Empty" \
  -unattended -nopause -nullrhi -nosplash -stdout -abslog="$TEMP/ms_task2_before.log"
```

预期：编译失败（`PlanetClouds::MaxSuns` 不存在）。这就是红。

- [ ] **Step 3: 加槽位常量**

`Source/StellarEraPlanet/Src/Atmosphere/PlanetCloudParams.h`，在 `namespace PlanetClouds` 开头（`FCloudLayerRadii` 之前）插入：

```cpp
	/**
	 * 同时参与着色的太阳槽位数。
	 *
	 * **这是唯一来源**：C++ 侧按它截断恒星列表，`Shaders/PlanetClouds.ush` 里的
	 * `#define PLANET_MAX_SUNS 4` 必须与它一致（测试会断言）。
	 * 两者不一致的症状是"第 4 颗星在材质里根本不存在"。
	 *
	 * 降级旋钮：想省 GPU 就把这里和 .ush 一起改成 3（关卡正好 3 颗）。
	 */
	constexpr int32 MaxSuns = 4;
```

- [ ] **Step 4: 改材质生成器**

`Source/StellarEraPlanet/Src/Atmosphere/PlanetAtmosphereMaterialBuilder.cpp`：

(a) `#define PLANET_CLOUDS_VERSION 6` → `#define PLANET_CLOUDS_VERSION 7`。

(b) 加 include（放在现有 `#include` 之后）：

```cpp
#include "PlanetCloudParams.h"
```

(c) 字符串 `AtmosphereHLSL` 里的调用改为：

```cpp
float4 march = CloudAtmosphereRaymarch(
    ro, rd, PlanetCenter,
    SunDirection0.rgb, SunDirection1.rgb, SunDirection2.rgb, SunDirection3.rgb,
    SunRadiance0.rgb, SunRadiance1.rgb, SunRadiance2.rgb, SunRadiance3.rgb,
    SunCount,
    tEnter, tExit, PlanetRadius,
    RayleighScaleHeight, MieScaleHeight,
    RayleighBeta.rgb, MieBeta.rgb,
    CloudBaseRadius, CloudTopRadius, CloudExtinction,
    CloudNoiseScale, CloudDetailScale, CloudCoverage,
    CloudDetailStrength, CloudDrift, CloudBrightness);
```

(d) 参数创建：把现有

```cpp
	UMaterialExpressionVectorParameter* SunDirection = AddAtmoVectorParam(Material, TEXT("SunDirection"),
		FLinearColor(1.0f, 0.0f, 0.0f, 1.0f), -800, 400);
	UMaterialExpressionVectorParameter* SunIntensity = AddAtmoVectorParam(Material, TEXT("SunIntensity"),
		FLinearColor(1.0f, 1.0f, 1.0f, 1.0f), -800, 500);
```

换成：

```cpp
	// 太阳槽位 0..MaxSuns-1。0 号默认白（无恒星时的回退太阳就是这个值），其余默认黑。
	UMaterialExpressionVectorParameter* SunDirection[PlanetClouds::MaxSuns];
	UMaterialExpressionVectorParameter* SunRadiance[PlanetClouds::MaxSuns];
	for (int32 Index = 0; Index < PlanetClouds::MaxSuns; ++Index)
	{
		const FString DirectionName = FString::Printf(TEXT("SunDirection%d"), Index);
		SunDirection[Index] = AddAtmoVectorParam(Material, *DirectionName,
			FLinearColor(1.0f, 0.0f, 0.0f, 0.0f), -800, 400 + Index * 40);

		const FString RadianceName = FString::Printf(TEXT("SunRadiance%d"), Index);
		SunRadiance[Index] = AddAtmoVectorParam(Material, *RadianceName,
			Index == 0 ? FLinearColor::White : FLinearColor::Black, -800, 600 + Index * 40);
	}
	UMaterialExpressionScalarParameter* SunCount =
		AddAtmoScalarParam(Material, TEXT("SunCount"), 1.0f, -800, 800);
	UMaterialExpressionVectorParameter* SunIntensity = AddAtmoVectorParam(Material, TEXT("SunIntensity"),
		FLinearColor(1.0f, 1.0f, 1.0f, 1.0f), -800, 900);
```

(e) Custom 输入：把

```cpp
	AddAtmoCustomInput(Custom, TEXT("SunDirection"), SunDirection);
```

换成：

```cpp
	for (int32 Index = 0; Index < PlanetClouds::MaxSuns; ++Index)
	{
		AddAtmoCustomInput(Custom, *FString::Printf(TEXT("SunDirection%d"), Index), SunDirection[Index]);
		AddAtmoCustomInput(Custom, *FString::Printf(TEXT("SunRadiance%d"), Index), SunRadiance[Index]);
	}
	AddAtmoCustomInput(Custom, TEXT("SunCount"), SunCount);
```

- [ ] **Step 5: 改 .ush**

`Shaders/PlanetClouds.ush`（纯 ASCII，无 BOM）。

(a) 在 `#define CLOUD_WEATHER_OCTAVES 3` 之前插入：

```hlsl
// Suns shaded per sample. Must match PlanetClouds::MaxSuns in
// Source/StellarEraPlanet/Src/Atmosphere/PlanetCloudParams.h - an automation
// test asserts the two agree. Slot 0 is the star nearest the planet and is the
// only one that marches the cloud self-shadow term below.
#define PLANET_MAX_SUNS 4

struct FPlanetSunSet
{
    int    Count;
    float3 Dir[PLANET_MAX_SUNS];       // unit, planet -> star
    float3 Radiance[PLANET_MAX_SUNS];  // colour * relative brightness
    float  PhaseR[PLANET_MAX_SUNS];
    float  PhaseM[PLANET_MAX_SUNS];
    float  PhaseC[PLANET_MAX_SUNS];
};
```

(b) 在 `CloudPhase`（以 `return 0.7 * pF + 0.3 * pB;` 结尾的那个函数）之后插入：

```hlsl
// Precomputes the per-pixel phases for every slot. The Custom node cannot
// define functions, so the four directions/radiances arrive as flat scalars.
FPlanetSunSet CloudBuildSunSet(float3 Rd,
    float3 D0, float3 D1, float3 D2, float3 D3,
    float3 R0, float3 R1, float3 R2, float3 R3,
    int Count)
{
    FPlanetSunSet Suns;
    Suns.Count = clamp(Count, 0, PLANET_MAX_SUNS);
    Suns.Dir[0] = D0; Suns.Dir[1] = D1; Suns.Dir[2] = D2; Suns.Dir[3] = D3;
    Suns.Radiance[0] = R0; Suns.Radiance[1] = R1;
    Suns.Radiance[2] = R2; Suns.Radiance[3] = R3;

    const float G = 0.8;
    const float G2 = G * G;
    [unroll]
    for (int s = 0; s < PLANET_MAX_SUNS; ++s)
    {
        float CosTheta = dot(Rd, Suns.Dir[s]);
        Suns.PhaseR[s] = 3.0 / (16.0 * CLOUD_PI) * (1.0 + CosTheta * CosTheta);
        Suns.PhaseM[s] = (1.0 - G2)
            / (4.0 * CLOUD_PI * pow(max(1.0 + G2 - 2.0 * G * CosTheta, 1e-4), 1.5));
        Suns.PhaseC[s] = CloudPhase(CosTheta);
    }
    return Suns;
}
```

(c) `CloudAtmoSegment` 整个函数替换为：

```hlsl
void CloudAtmoSegment(
    float3 Ro, float3 Rd, float3 Center, FPlanetSunSet Suns,
    float T0, float T1, int Steps,
    float PlanetRadius, float RayleighScaleHeight, float MieScaleHeight,
    float3 RayleighBeta, float3 MieBeta,
    float CloudBaseRadius, float CloudTopRadius, float CloudExtinction,
    float CloudNoiseScale, float CloudDetailScale, float CloudCoverage,
    float CloudDetailStrength, float CloudDrift, float CloudBrightness,
    inout float3 Acc, inout float3 Tr)
{
    if (T1 <= T0)
    {
        return;
    }
    float Ds = (T1 - T0) / float(Steps);
    for (int i = 0; i < Steps; ++i)
    {
        float t = T0 + (float(i) + 0.5) * Ds;
        float3 P = Ro + Rd * t;
        float3 Ops = P - Center;
        float RLen = length(Ops);

        float H = max(RLen - PlanetRadius, 0.0);
        float DR = exp(-H / RayleighScaleHeight);
        float DM = exp(-H / MieScaleHeight);
        float DC = CloudDensity(Ops, RLen, CloudBaseRadius, CloudTopRadius, CloudExtinction,
            CloudNoiseScale, CloudDetailScale, CloudCoverage, CloudDetailStrength, CloudDrift);

        // Cloud extinction is grey, so it just adds to the per-channel one.
        float3 Extinction = RayleighBeta * DR + MieBeta * DM + float3(DC, DC, DC);
        float3 TrSeg = exp(-Extinction * Ds);

        float3 Scatter = 0.0;
        float3 CloudLit = 0.0;

        // [unroll] keeps Suns.Dir[s] a constant index. A dynamic index turns the
        // array into local memory on some platforms.
        [unroll]
        for (int s = 0; s < PLANET_MAX_SUNS; ++s)
        {
            if (s >= Suns.Count)
            {
                continue;
            }

            float3 SunDir = Suns.Dir[s];

            // Sun occluded by the planet body: perpendicular distance from P to
            // the sun ray smaller than the planet radius means P is in the umbra.
            float BS = dot(Ops, SunDir);
            float SunVis = 1.0;
            if (BS < 0.0)
            {
                float DPerp = length(Ops - BS * SunDir);
                SunVis = smoothstep(PlanetRadius * 0.99, PlanetRadius * 1.01, DPerp);
            }

            Scatter += Suns.Radiance[s] * SunVis
                     * (RayleighBeta * DR * Suns.PhaseR[s] + MieBeta * DM * Suns.PhaseM[s]);

            if (DC > 0.0)
            {
                float CloudLight = SunVis;
                // Self-shadowing marches sun-ward through the layer. Only slot 0
                // does it: these CLOUD_SHADOW_STEPS extra CloudDensity taps are
                // the most expensive part of the march, and the structural
                // shadow that reads is the dominant star's.
                if (s == 0)
                {
                    float ShadowStep = (CloudTopRadius - CloudBaseRadius) * (1.0 / float(CLOUD_SHADOW_STEPS));
                    [unroll]
                    for (int j = 1; j <= CLOUD_SHADOW_STEPS; ++j)
                    {
                        float3 SP = P + SunDir * (float(j) * ShadowStep);
                        float3 SOps = SP - Center;
                        CloudLight *= exp(-CloudDensity(SOps, length(SOps),
                            CloudBaseRadius, CloudTopRadius, CloudExtinction,
                            CloudNoiseScale, CloudDetailScale, CloudCoverage,
                            CloudDetailStrength, CloudDrift) * ShadowStep * CLOUD_SHADOW_EXTINCTION_SCALE);
                    }
                    // The ambient floor is applied once, not per sun: applying it
                    // per sun would triple the night-side fill with three stars.
                    CloudLight = max(CloudLight, CLOUD_AMBIENT);
                }
                CloudLit += Suns.Radiance[s] * Suns.PhaseC[s] * CloudLight;
            }
        }

        Acc += Scatter * Tr * TrSeg * Ds;

        if (DC > 0.0)
        {
            Acc += CloudBrightness * DC * CloudLit * Tr * TrSeg * Ds;
        }

        Tr *= TrSeg;
    }
}
```

(d) `CloudAtmosphereRaymarch` 整个函数替换为：

```hlsl
float4 CloudAtmosphereRaymarch(
    float3 Ro, float3 Rd, float3 Center,
    float3 SunDir0, float3 SunDir1, float3 SunDir2, float3 SunDir3,
    float3 SunRadiance0, float3 SunRadiance1, float3 SunRadiance2, float3 SunRadiance3,
    float SunCount,
    float TEnter, float TExit, float PlanetRadius,
    float RayleighScaleHeight, float MieScaleHeight,
    float3 RayleighBeta, float3 MieBeta,
    float CloudBaseRadius, float CloudTopRadius, float CloudExtinction,
    float CloudNoiseScale, float CloudDetailScale, float CloudCoverage,
    float CloudDetailStrength, float CloudDrift, float CloudBrightness)
{
    // No sun at all: returning black here rather than shading with a zero
    // direction, which would light every pixel as if the sun were straight up.
    if (SunCount <= 0.5)
    {
        return float4(0.0, 0.0, 0.0, 0.0);
    }

    FPlanetSunSet Suns = CloudBuildSunSet(Rd,
        SunDir0, SunDir1, SunDir2, SunDir3,
        SunRadiance0, SunRadiance1, SunRadiance2, SunRadiance3,
        (int)SunCount);

    // Where the ray crosses the cloud layer, clamped to the atmosphere chord.
    // Intersecting the *base* sphere is enough: the top is clamped inside the
    // shell by the component, so [enter, exit] of the shell already bounds it.
    float CIn = 1e30;
    float COut = -1e30;
    if (CloudExtinction > 0.0 && CloudTopRadius > CloudBaseRadius)
    {
        float3 OCo = Ro - Center;
        float B = dot(OCo, Rd);
        float C = dot(OCo, OCo) - CloudBaseRadius * CloudBaseRadius;
        float Disc = B * B - C;
        if (Disc > 0.0)
        {
            float Sq = sqrt(Disc);
            CIn = max(TEnter, -B - Sq);
            COut = min(TExit, -B + Sq);
        }
    }

    float3 Acc = float3(0.0, 0.0, 0.0);
    float3 Tr = float3(1.0, 1.0, 1.0);

    if (COut > CIn)
    {
        // ~0.4 thickness per step: a radial crossing gets CLOUD_MIN_STEPS, a
        // grazing one maxes out at CLOUD_STEPS instead of banding.
        float CloudStepTarget = max((CloudTopRadius - CloudBaseRadius) * 0.4, 1.0);
        int CloudSteps = (int)clamp(ceil((COut - CIn) / CloudStepTarget),
            (float)CLOUD_MIN_STEPS, (float)CLOUD_STEPS);
        CloudAtmoSegment(Ro, Rd, Center, Suns, TEnter, CIn, ATMO_STEPS,
            PlanetRadius, RayleighScaleHeight, MieScaleHeight, RayleighBeta, MieBeta,
            CloudBaseRadius, CloudTopRadius, CloudExtinction, CloudNoiseScale, CloudDetailScale,
            CloudCoverage, CloudDetailStrength, CloudDrift, CloudBrightness, Acc, Tr);
        CloudAtmoSegment(Ro, Rd, Center, Suns, CIn, COut, CloudSteps,
            PlanetRadius, RayleighScaleHeight, MieScaleHeight, RayleighBeta, MieBeta,
            CloudBaseRadius, CloudTopRadius, CloudExtinction, CloudNoiseScale, CloudDetailScale,
            CloudCoverage, CloudDetailStrength, CloudDrift, CloudBrightness, Acc, Tr);
        CloudAtmoSegment(Ro, Rd, Center, Suns, COut, TExit, ATMO_STEPS,
            PlanetRadius, RayleighScaleHeight, MieScaleHeight, RayleighBeta, MieBeta,
            CloudBaseRadius, CloudTopRadius, CloudExtinction, CloudNoiseScale, CloudDetailScale,
            CloudCoverage, CloudDetailStrength, CloudDrift, CloudBrightness, Acc, Tr);
    }
    else
    {
        // No cloud layer on this ray (looking away, or clouds disabled).
        CloudAtmoSegment(Ro, Rd, Center, Suns, TEnter, TExit, ATMO_STEPS * 2,
            PlanetRadius, RayleighScaleHeight, MieScaleHeight, RayleighBeta, MieBeta,
            CloudBaseRadius, CloudTopRadius, CloudExtinction, CloudNoiseScale, CloudDetailScale,
            CloudCoverage, CloudDetailStrength, CloudDrift, CloudBrightness, Acc, Tr);
    }

    float Coverage = saturate(1.0 - dot(Tr, float3(1.0 / 3.0, 1.0 / 3.0, 1.0 / 3.0)));
    return float4(Acc, Coverage);
}
```

- [ ] **Step 6: 构建 + 重建材质 + 跑测试**

1. 关编辑器，跑 Global Constraints 里的 UBT 命令 → 预期 `Result: Succeeded`。
2. 重建材质资产。编辑器开着时用 MCP：

```python
import unreal
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
unreal.SystemLibrary.execute_console_command(world, "StellarEra.BuildAtmosphereMaterial")
```

   编辑器没开时只能无头跑，且**进程不会自己退出**（`QUIT` 不生效），会一直握着 DLL：

```bash
"D:/Application/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" \
  "F:/workspace/ue/StellarEra/StellarEra.uproject" \
  -ExecCmds="StellarEra.BuildAtmosphereMaterial" \
  -unattended -nopause -nullrhi -nosplash -stdout -abslog="$TEMP/ms_material_build.log" &
```

   等日志里出现生成器打印的结果行，再按 PID 结束进程。判据以日志为准，不要指望进程退出码。

3. 跑：

```bash
"D:/Application/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" \
  "F:/workspace/ue/StellarEra/StellarEra.uproject" \
  -ExecCmds="Automation RunTests StellarEra.Planet.AtmosphereMaterial" \
  -TestExit="Automation Test Queue Empty" \
  -unattended -nopause -nullrhi -nosplash -stdout -abslog="$TEMP/ms_task2_after.log"
```

预期：`StellarEra.Planet.AtmosphereMaterial.Structure` 与 `.Hlsl` 都 `Success`。

- [ ] **Step 7: 提交**

```bash
git add Source/StellarEraPlanet/Src/Atmosphere/PlanetCloudParams.h \
        Source/StellarEraPlanet/Src/Atmosphere/PlanetAtmosphereMaterialBuilder.cpp \
        Source/StellarEraPlanet/Src/Tests/PlanetAtmosphereMaterialTests.cpp \
        Shaders/PlanetClouds.ush \
        Content/Code/Planet/Materials/M_PlanetAtmosphere.uasset
git commit -m "feat(大气材质): 4 个太阳槽位求和着色，云自遮挡只算最近那颗

- PlanetClouds::MaxSuns 为槽位数唯一来源，.ush 的 PLANET_MAX_SUNS 与之对齐
- SunDirection0..3 / SunRadiance0..3 / SunCount 取代单数 SunDirection
- PLANET_CLOUDS_VERSION 6 -> 7 强制材质重编

Co-Authored-By: Claude Code <noreply@anthropic.com>" -- \
        Source/StellarEraPlanet/Src/Atmosphere/PlanetCloudParams.h \
        Source/StellarEraPlanet/Src/Atmosphere/PlanetAtmosphereMaterialBuilder.cpp \
        Source/StellarEraPlanet/Src/Tests/PlanetAtmosphereMaterialTests.cpp \
        Shaders/PlanetClouds.ush \
        Content/Code/Planet/Materials/M_PlanetAtmosphere.uasset
```

---

## Task 3: 大气组件收集太阳并驱动灯

**Files:**
- Modify: `Source/StellarEraPlanet/Src/Atmosphere/PlanetAtmosphereComponent.h`
- Modify: `Source/StellarEraPlanet/Src/Atmosphere/PlanetAtmosphereComponent.cpp`
- Test: `Source/StellarEraPlanet/Src/Tests/PlanetMultiSunTests.cpp`（追加）

**Interfaces:**
- Consumes: `UStarBodyComponent::FindAll`、`GetSunColor`、`GetRelativeBrightness`、`DriveSunLight`、`bDrivesDirectionalLight`；`PlanetClouds::MaxSuns`。
- Produces:
  - `struct FPlanetSun { FVector Direction; FLinearColor Radiance; UStarBodyComponent* Star; }`
  - `static void UPlanetAtmosphereComponent::CollectSunsFor(UWorld* World, const FVector& PlanetCenter, TArray<FPlanetSun>& OutSuns)`
  - `UPROPERTY(EditAnywhere) float BaseLightIntensity = 10.0f`

- [ ] **Step 1: 追加失败的测试**

`Source/StellarEraPlanet/Src/Tests/PlanetMultiSunTests.cpp` 顶部 include 加 `"PlanetAtmosphereComponent.h"` 与 `"PlanetCloudParams.h"`（保持字母序），然后在 `#endif` 之前追加：

```cpp
// 太阳列表：按距离升序、截断到 MaxSuns、Direction 是单位向量。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraSunListBuildTest,
	"StellarEra.Planet.Atmosphere.SunList.Build",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FStellarEraSunListBuildTest::RunTest(const FString& Parameters)
{
	UWorld* World = StellarEraMultiSunTests::GetTestWorld();
	if (!TestNotNull(TEXT("编辑器世界"), World))
	{
		return false;
	}

	// 探针点放在离关卡内容极远的地方，保证最近的是下面这几颗。
	const FVector Probe(5.0e13, 0.0, 0.0);
	TArray<AActor*> Spawned;

	// 距离 1e6 / 2e6 / 3e6，方向 +X / +Y / +Z。
	TArray<UStarBodyComponent*> Stars;
	Stars.Add(StellarEraMultiSunTests::AddStarComponent(
		StellarEraMultiSunTests::SpawnRootedActor(World, Probe + FVector(1.0e6, 0, 0), Spawned)));
	Stars.Add(StellarEraMultiSunTests::AddStarComponent(
		StellarEraMultiSunTests::SpawnRootedActor(World, Probe + FVector(0, 2.0e6, 0), Spawned)));
	Stars.Add(StellarEraMultiSunTests::AddStarComponent(
		StellarEraMultiSunTests::SpawnRootedActor(World, Probe + FVector(0, 0, 3.0e6), Spawned)));

	TArray<FPlanetSun> Suns;
	UPlanetAtmosphereComponent::CollectSunsFor(World, Probe, Suns);

	if (TestEqual(TEXT("三颗恒星全部收进来"), Suns.Num(), 3))
	{
		TestTrue(TEXT("槽位 0 是最近的星（+X）"), Suns[0].Direction.Equals(FVector(1, 0, 0), 1e-3f));
		TestTrue(TEXT("槽位 1 是第二近（+Y）"), Suns[1].Direction.Equals(FVector(0, 1, 0), 1e-3f));
		TestTrue(TEXT("槽位 2 是第三近（+Z）"), Suns[2].Direction.Equals(FVector(0, 0, 1), 1e-3f));
		TestEqual(TEXT("Direction 是单位向量"), Suns[0].Direction.Size(), 1.0f, 1e-3f);
		TestSamePtr(TEXT("Star 指回组件"), Suns[0].Star, Stars[0]);
		TestTrue(TEXT("Radiance 非负"), Suns[0].Radiance.R >= 0.0f && Suns[0].Radiance.G >= 0.0f);
	}

	StellarEraMultiSunTests::DestroyAll(Spawned);

	// 截断：5 颗星只留最近的 MaxSuns 颗。
	for (int32 Index = 0; Index < 5; ++Index)
	{
		StellarEraMultiSunTests::AddStarComponent(StellarEraMultiSunTests::SpawnRootedActor(
			World, Probe + FVector(1.0e6 * (Index + 1), 0, 0), Spawned));
	}
	UPlanetAtmosphereComponent::CollectSunsFor(World, Probe, Suns);
	TestEqual(TEXT("超过 MaxSuns 时截断"), Suns.Num(), PlanetClouds::MaxSuns);
	TestTrue(TEXT("截断保留最近的那颗（距离 1e6，+X）"),
		Suns.IsValidIndex(0) && Suns[0].Direction.Equals(FVector(1, 0, 0), 1e-3f));

	StellarEraMultiSunTests::DestroyAll(Spawned);
	return true;
}
```

- [ ] **Step 2: 跑测试确认失败**

同 Task 1 Step 2 的命令，过滤 `StellarEra.Planet.Atmosphere.SunList`。预期编译失败（`CollectSunsFor` 不存在）。

- [ ] **Step 3: 改头文件**

`Source/StellarEraPlanet/Src/Atmosphere/PlanetAtmosphereComponent.h`：

(a) 把 `class ADirectionalLight;` 换成：

```cpp
class ADirectionalLight;
class UStarBodyComponent;
```

(b) 在 `class ADirectionalLight;` 之后、`UCLASS` 之前插入结构体：

```cpp
/**
 * 一颗照亮本行星的光源快照。
 *
 * 通常来自关卡里的 UStarBodyComponent；若世界里一颗恒星都没有，则回退成
 * 关卡里第一盏 ADirectionalLight（此时 Star 为空，Radiance 为白）。
 */
struct FPlanetSun
{
	/** 单位向量：行星 → 光源。 */
	FVector Direction = FVector::ForwardVector;

	/** 颜色 × 相对亮度。 */
	FLinearColor Radiance = FLinearColor::White;

	/** 回退太阳时为 nullptr。 */
	UStarBodyComponent* Star = nullptr;
};
```

(c) 在 `SunIntensity` 之后插入：

```cpp
	/**
	 * 驱动定向光时的基础强度。主星实际强度 = 本值 × 该恒星的相对亮度。
	 * 10 与关卡现值一致；三颗星同时照到受光面时调这里。
	 */
	UPROPERTY(EditAnywhere, Category = "StellarEra|Atmosphere", meta = (ClampMin = "0.0"))
	float BaseLightIntensity = 10.0f;
```

(d) 把 `ResolveSunDirection()` 的声明整段删掉，在 public 段 `ComputeSunDirection` 之后换成：

```cpp
	/**
	 * 收集照亮 PlanetCenter 的太阳列表，按距离升序，最多 PlanetClouds::MaxSuns 个。
	 *
	 * **只读世界，不碰材质**：材质参数写入在 ApplySuns 里，灯驱动在 DriveSunLights 里。
	 * 做成 static 是为了能被自动化测试直接喂一个世界和一个探针点。
	 *
	 * 回退：世界里一颗恒星都没有时，取第一盏 ADirectionalLight
	 * （Direction 由它的 forward 反推，Star 为空，Radiance 为白）。
	 */
	static void CollectSunsFor(UWorld* World, const FVector& PlanetCenter, TArray<FPlanetSun>& OutSuns);
```

(e) 私有段里删掉 `CachedSunLight` 与 `LastSunDirection`，换成：

```cpp
	/** 把太阳列表写进 MID（SunDirection0..3 / SunRadiance0..3 / SunCount）。 */
	void ApplySuns(const TArray<FPlanetSun>& Suns);

	/**
	 * 逐颗驱动灯（仅游戏世界）。主星 = 列表里第一颗 bDrivesDirectionalLight 的恒星，
	 * 它可能不是槽位 0 —— 槽位 0 只按距离排，可能被显式关掉了灯。
	 */
	void DriveSunLights(const TArray<FPlanetSun>& Suns);

	/** 每帧复用的缓冲，避免 CollectSunsFor 每帧重新分配。 */
	TArray<FPlanetSun> CachedSuns;
```

- [ ] **Step 4: 改实现**

`Source/StellarEraPlanet/Src/Atmosphere/PlanetAtmosphereComponent.cpp`：

(a) 参数名常量：把

```cpp
	const FName ParamSunDirection(TEXT("SunDirection"));
```

换成：

```cpp
	// 太阳槽位。数组下标即槽位号，必须与 PlanetClouds::MaxSuns 一致。
	const FName ParamSunDirections[PlanetClouds::MaxSuns] = {
		FName(TEXT("SunDirection0")), FName(TEXT("SunDirection1")),
		FName(TEXT("SunDirection2")), FName(TEXT("SunDirection3")) };
	const FName ParamSunRadiances[PlanetClouds::MaxSuns] = {
		FName(TEXT("SunRadiance0")), FName(TEXT("SunRadiance1")),
		FName(TEXT("SunRadiance2")), FName(TEXT("SunRadiance3")) };
	const FName ParamSunCount(TEXT("SunCount"));
```

(b) `RefreshAtmosphereGeometry()` 里把这段

```cpp
	const FVector InitialSunDirection = ResolveSunDirection();
	if (!InitialSunDirection.IsNearlyZero())
	{
		LastSunDirection = InitialSunDirection;
		AtmosphereMID->SetVectorParameterValue(ParamSunDirection,
			FLinearColor(InitialSunDirection.X, InitialSunDirection.Y, InitialSunDirection.Z, 0.0f));
	}
```

换成：

```cpp
	// 太阳列表必须在这里也写一次，理由与 PlanetCenter 完全相同：
	// 编辑器视口不保证 tick 本组件，只靠 TickComponent 会让天空停在初值。
	CollectSunsFor(GetWorld(), PlanetCenter, CachedSuns);
	ApplySuns(CachedSuns);
```

(c) `TickComponent` 末尾这段

```cpp
	const FVector SunDirection = ResolveSunDirection();
	if (!SunDirection.IsNearlyZero() && !SunDirection.Equals(LastSunDirection, 1e-4f))
	{
		LastSunDirection = SunDirection;
		AtmosphereMID->SetVectorParameterValue(ParamSunDirection,
			FLinearColor(SunDirection.X, SunDirection.Y, SunDirection.Z, 0.0f));
	}
```

换成：

```cpp
	// 太阳每帧重算：恒星在 N 体里会动，方向逐帧变化，缓存没有意义。
	// 9 次 MID 参数写入的成本可以忽略。
	CollectSunsFor(GetWorld(), PlanetCenter, CachedSuns);
	ApplySuns(CachedSuns);
	DriveSunLights(CachedSuns);
```

(d) 把 `ResolveSunDirection()` 的整个函数体删掉，在 `ComputeSunDirection` 之后换成三个新函数：

```cpp
void UPlanetAtmosphereComponent::CollectSunsFor(UWorld* World, const FVector& PlanetCenter,
	TArray<FPlanetSun>& OutSuns)
{
	OutSuns.Reset();

	TArray<UStarBodyComponent*> Stars;
	UStarBodyComponent::FindAll(World, Stars);

	// 距离升序。槽位 0 必须是最近的恒星：shader 里只有 s == 0 做云自遮挡。
	// TArray 的指针排序会把元素解引用后再交给谓词，所以谓词收引用。
	Stars.Sort([&PlanetCenter](const UStarBodyComponent& A, const UStarBodyComponent& B)
	{
		const AActor* OwnerA = A.GetOwner();
		const AActor* OwnerB = B.GetOwner();
		const FVector LocationA = OwnerA ? OwnerA->GetActorLocation() : FVector::ZeroVector;
		const FVector LocationB = OwnerB ? OwnerB->GetActorLocation() : FVector::ZeroVector;
		return FVector::DistSquared(LocationA, PlanetCenter) < FVector::DistSquared(LocationB, PlanetCenter);
	});

	static bool bLoggedTooManySuns = false;
	if (Stars.Num() > PlanetClouds::MaxSuns && !bLoggedTooManySuns)
	{
		bLoggedTooManySuns = true;
		UE_LOG(LogTemp, Warning,
			TEXT("[大气] 关卡里有 %d 颗恒星，材质只支持 %d 颗，只取最近的 %d 颗。"),
			Stars.Num(), PlanetClouds::MaxSuns, PlanetClouds::MaxSuns);
	}

	const int32 Count = FMath::Min(Stars.Num(), PlanetClouds::MaxSuns);
	for (int32 Index = 0; Index < Count; ++Index)
	{
		const UStarBodyComponent* Star = Stars[Index];
		const AActor* StarActor = Star ? Star->GetOwner() : nullptr;
		if (!StarActor)
		{
			continue;
		}

		FPlanetSun Sun;
		Sun.Direction = (StarActor->GetActorLocation() - PlanetCenter).GetSafeNormal();
		if (Sun.Direction.IsNearlyZero())
		{
			// 恒星与行星重合：没有方向可言，跳过而不是塞一个零方向。
			continue;
		}
		Sun.Radiance = Star->GetSunColor() * Star->GetRelativeBrightness();
		Sun.Star = const_cast<UStarBodyComponent*>(Star);
		OutSuns.Add(Sun);
	}

	if (OutSuns.Num() > 0)
	{
		return;
	}

	// 回退：没有恒星的世界（NewMap / TestMap）沿用改动前的单一光源行为。
	if (World)
	{
		for (TActorIterator<ADirectionalLight> It(World); It; ++It)
		{
			FPlanetSun Sun;
			Sun.Direction = ComputeSunDirection(nullptr, *It, PlanetCenter);
			if (Sun.Direction.IsNearlyZero())
			{
				continue;
			}
			Sun.Radiance = FLinearColor::White;
			Sun.Star = nullptr;
			OutSuns.Add(Sun);
			return;
		}
	}

	static bool bLoggedMissingSun = false;
	if (!bLoggedMissingSun)
	{
		bLoggedMissingSun = true;
		UE_LOG(LogTemp, Warning,
			TEXT("[大气] 关卡里既没有 UStarBodyComponent 也没有 ADirectionalLight，"
				"大气与云不会发光。"));
	}
}

void UPlanetAtmosphereComponent::ApplySuns(const TArray<FPlanetSun>& Suns)
{
	if (!AtmosphereMID)
	{
		return;
	}

	for (int32 Index = 0; Index < PlanetClouds::MaxSuns; ++Index)
	{
		const FPlanetSun* Sun = Suns.IsValidIndex(Index) ? &Suns[Index] : nullptr;
		const FVector Direction = Sun ? Sun->Direction : FVector::ForwardVector;
		const FLinearColor Radiance = Sun ? Sun->Radiance : FLinearColor::Black;

		AtmosphereMID->SetVectorParameterValue(ParamSunDirections[Index],
			FLinearColor(Direction.X, Direction.Y, Direction.Z, 0.0f));
		AtmosphereMID->SetVectorParameterValue(ParamSunRadiances[Index],
			FLinearColor(Radiance.R, Radiance.G, Radiance.B, 1.0f));
	}
	AtmosphereMID->SetScalarParameterValue(ParamSunCount, static_cast<float>(Suns.Num()));
}

void UPlanetAtmosphereComponent::DriveSunLights(const TArray<FPlanetSun>& Suns)
{
	UWorld* World = GetWorld();
	if (!World || !World->IsGameWorld())
	{
		// 编辑器视口不转灯：每帧转灯会把关卡一直标脏。
		return;
	}

	UStarBodyComponent* PrimaryStar = nullptr;
	for (const FPlanetSun& Sun : Suns)
	{
		if (Sun.Star && Sun.Star->bDrivesDirectionalLight)
		{
			PrimaryStar = Sun.Star;
			break;
		}
	}

	const FVector PlanetCenter = GetComponentLocation();
	for (const FPlanetSun& Sun : Suns)
	{
		if (!Sun.Star || !Sun.Star->bDrivesDirectionalLight)
		{
			continue;
		}
		Sun.Star->DriveSunLight(PlanetCenter, BaseLightIntensity, Sun.Star == PrimaryStar);
	}
}
```

- [ ] **Step 5: 构建 + 跑全量测试**

关编辑器 → UBT 构建（预期 `Result: Succeeded`）→

```bash
"D:/Application/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" \
  "F:/workspace/ue/StellarEra/StellarEra.uproject" \
  -ExecCmds="Automation RunTests StellarEra" \
  -TestExit="Automation Test Queue Empty" \
  -unattended -nopause -nullrhi -nosplash -stdout -abslog="$TEMP/ms_task3_after.log"
```

预期：日志里 `**** TEST COMPLETE. EXIT CODE: 0`，且 `SunList.Build` 为 `Success`。
`StellarEra.Planet.Atmosphere.SunDirection.Compute`（既有测试，测 `ComputeSunDirection`）必须仍然绿。

- [ ] **Step 6: 提交**

```bash
git add Source/StellarEraPlanet/Src/Atmosphere/PlanetAtmosphereComponent.h \
        Source/StellarEraPlanet/Src/Atmosphere/PlanetAtmosphereComponent.cpp \
        Source/StellarEraPlanet/Src/Tests/PlanetMultiSunTests.cpp
git commit -m "feat(大气): 收集全部恒星并按距离排序，逐颗驱动定向光

- CollectSunsFor：静态、只读世界，按距离升序取前 MaxSuns 颗
- ApplySuns：写 SunDirection0..3 / SunRadiance0..3 / SunCount
- DriveSunLights：主星 = 第一颗接管灯的恒星，仅游戏世界写灯
- 删掉 ResolveSunDirection / CachedSunLight / LastSunDirection

Co-Authored-By: Claude Code <noreply@anthropic.com>" -- \
        Source/StellarEraPlanet/Src/Atmosphere/PlanetAtmosphereComponent.h \
        Source/StellarEraPlanet/Src/Atmosphere/PlanetAtmosphereComponent.cpp \
        Source/StellarEraPlanet/Src/Tests/PlanetMultiSunTests.cpp
```

---

## Task 4: 关卡绑定与主光名额

**Files:**
- Modify: `Content/Maps/UniverseMap.umap`（及三颗星、三盏灯的外部 Actor 包）

**Interfaces:**
- Consumes: `UStarBodyComponent.SunLight`（Task 1）。
- Produces: 关卡资产里三颗星 ↔ 三盏灯的绑定，以及主光的 `ForwardShadingPriority=1` / `bAtmosphereSunLight=True`。

**背景事实（先核对，别假设）：** `Star_A` 用 `MI_Sun_G2V`、`Star_B` 用 `MI_Sun_B3V`、`Star_C` 用 `MI_Sun_M2V`；行星在 `y = 1e8` 附近，`Star_A` 在原点，因此 `Star_A` 最近。

- [ ] **Step 1: 读关卡现状**

编辑器开着（MCP 需要），用 `execute_python_code`：

```python
import unreal
les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
les.load_level("/Game/Maps/UniverseMap")

for actor in unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors():
    cls = actor.static_class().get_name()
    if "Star" in actor.get_actor_label():
        print("STAR", actor.get_actor_label(), actor.get_actor_location(), cls)
    if cls == "DirectionalLight":
        print("LIGHT", actor.get_actor_label())
```

把输出记进任务记录：星的位置决定谁是主星，灯的数量决定能不能一星一灯。

- [ ] **Step 2: 绑定并写入主光名额**

```python
import unreal

eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
actors = eas.get_all_level_actors()

stars = sorted([a for a in actors if "Star" in a.get_actor_label()], key=lambda a: a.get_actor_label())
lights = sorted([a for a in actors if a.static_class().get_name() == "DirectionalLight"],
                key=lambda a: a.get_actor_label())
print("stars", [a.get_actor_label() for a in stars])
print("lights", [a.get_actor_label() for a in lights])
assert len(stars) == 3, len(stars)
assert len(lights) >= 3, len(lights)

for star, light in zip(stars, lights[:3]):
    comp = star.get_components_by_class(unreal.StarBodyComponent)[0]
    comp.set_editor_property("sun_light", light)
    print("BIND", star.get_actor_label(), "->", light.get_actor_label())

# 主星 = 离行星最近的星。
planets = [a for a in actors if "VoxelPlanet" in a.static_class().get_name()]
assert len(planets) >= 1, len(planets)
center = planets[0].get_actor_location()
primary = min(stars, key=lambda s: (s.get_actor_location() - center).length())
print("PRIMARY", primary.get_actor_label())

for star, light in zip(stars, lights[:3]):
    lc = light.get_component_by_class(unreal.DirectionalLightComponent)
    is_primary = (star == primary)
    lc.set_editor_property("forward_shading_priority", 1 if is_primary else 0)
    lc.set_editor_property("atmosphere_sun_light", is_primary)
    print("PRIO", light.get_actor_label(), 1 if is_primary else 0, is_primary)
```

若 `set_editor_property` 报属性名找不到，先跑
`print([p for p in dir(unreal.DirectionalLightComponent) if "rior" in p.lower() or "tmos" in p.lower()])`
确认 Python 侧真实属性名，再改脚本。

- [ ] **Step 3: 落盘**

```python
unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level()
```

这条会保存整关（umap + 所有脏外部 Actor 包），可能顺带提交此前遗留的未保存改动 ——
保存前先 `git status --short Content/` 看一眼，把不相干的脏包记进任务说明。

- [ ] **Step 4: 复核（不能只信 save 的返回值）**

保存返回 True 也可能是假的；用**重新加载后读回**做判据：

```python
import unreal
unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).load_level("/Game/Maps/UniverseMap")
for actor in unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors():
    if "Star" in actor.get_actor_label():
        comp = actor.get_components_by_class(unreal.StarBodyComponent)[0]
        light = comp.get_editor_property("sun_light")
        lc = light.get_component_by_class(unreal.DirectionalLightComponent) if light else None
        print("CHECK", actor.get_actor_label(), light,
              lc.get_editor_property("forward_shading_priority") if lc else None,
              lc.get_editor_property("atmosphere_sun_light") if lc else None)
```

预期：三颗星各自打印出一盏**非空且互不相同**的灯；恰好一盏是 `1 / True`。

- [ ] **Step 5: 提交**

```bash
git status --short Content/
```

把与本次绑定相关的路径（`Content/Maps/UniverseMap.umap` 与列出的外部 Actor 包）一并 `git add`，然后：

```bash
git commit -m "feat(关卡): 三颗星各绑一盏定向光，最近的那颗占用主光名额

- UStarBodyComponent.SunLight 显式绑定，不再靠遍历顺序自动认领
- 主星 ForwardShadingPriority=1 + bAtmosphereSunLight=True，其余 0/False
- 编辑器里那条「多个定向光源竞争」的警告随之消失

Co-Authored-By: Claude Code <noreply@anthropic.com>" -- <列出的所有路径>
```

---

## Task 5: 端到端验证

**Files:** 无代码改动（只验证）。

- [ ] **Step 1: 构建与全量测试**

关编辑器 → UBT 构建 → `Automation RunTests StellarEra` 全绿。把日志里的
`**** TEST COMPLETE. EXIT CODE: 0` 与测试计数贴进任务记录。

- [ ] **Step 2: PIE 检查**

在编辑器里 PIE 进 `UniverseMap`：

1. 日志过滤 `ForwardShadingPriority` / `定向光源` —— 那条"多个定向光源竞争"的警告必须**不再出现**。
2. 观察行星明暗分界随三体运动移动；背光面不是纯黑（另外两颗星补光）。
3. 云在夜面不出现"黑洞"。
4. 暂停 PIE 截图，与改动前的截图并排存到 `Saved/VibeUE/Captures`。

- [ ] **Step 3: 性能对比**

PIE 里 `stat unit` 读 GPU 时间，与改动前的读数比较。若超过 +15%，按 spec §8
把 `PLANET_MAX_SUNS` 从 4 降到 3 —— 同时改 `PlanetClouds::MaxSuns` 与 `.ush`，两处必须一起改。

- [ ] **Step 4: 报告**

把以下证据贴进最终回复：
- UBT `Result: Succeeded` 行；
- 测试日志的 `TEST COMPLETE` 行与失败数为 0；
- 警告消失的证据（日志 grep 结果为空）；
- 截图路径与 `stat unit` 前后读数。

---

## 自审记录

- **spec 覆盖**：§5.1 恒星颜色/亮度 → Task 1；§5.2 `CollectSuns` → Task 3；§5.3 材质参数 → Task 2；
  §5.4 HLSL → Task 2；§5.5 一星一灯与主光名额 → Task 1（`DriveSunLight`）+ Task 3（`DriveSunLights`）
  + Task 4（写进资产）；§5.6 关卡 → Task 4；§7 测试 → Task 1/2/3 的测试步 + Task 5。
- **类型一致性**：`FPlanetSun` 三字段（`Direction` / `Radiance` / `Star`）在 Task 3 定义与消费；
  `PlanetClouds::MaxSuns` 在 Task 2 定义、Task 3 消费；`SunDirection0..3` 的命名在 Task 2（生成器）、
  Task 2（测试）、Task 3（MID 写入）三处一致。
- **无占位符**：所有代码步都给了完整可粘贴的实现；Task 4 的 Python 给了断言与失败时的排查命令。
