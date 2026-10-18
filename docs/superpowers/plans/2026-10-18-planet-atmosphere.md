# 行星大气 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 让 `UniverseMap` 里**每一颗**体素行星从太空看都有一圈大气辉光，同时让玩家落到任意一颗行星表面时头顶有正确的天空与空中透视。

**Architecture:** 两个互不干扰的机制各解决一半。**壳**——每颗行星一个 `UStaticMeshComponent` 子类，套一个自写单次散射像素着色器的半透明球，全部同时可见，负责"从太空看"。**引擎大气**——场景里唯一的 `ASkyAtmosphere` 由 `UCosmosSimulationSubsystem` 逐帧搬到离相机最近的那颗行星，按该行星半径缩放参数，负责"站在行星上"。球面法线朝外，相机在球内时壳被背面剔除，两者天然不打架。

**Tech Stack:** Unreal Engine 5.8 / C++ / UBT / `UMaterialExpressionCustom`（HLSL 作为仓库内字符串）/ 自动化测试 `IMPLEMENT_SIMPLE_AUTOMATION_TEST`

**Spec:** `docs/superpowers/specs/2026-10-18-planet-atmosphere-design.md`

## Global Constraints

- 引擎：UE 5.8，位于 `D:\Application\Epic Games\UE_5.8\Engine`。
- 模块分层**不得成环**：`StellarEra → StellarEraVehicle → StellarEraPlanet`，`StellarEra → StellarEraCore`。本次只往 `StellarEraPlanet`（新增两个文件）与 `StellarEra`（新增三个文件）里加东西。
- 自定义代码**不进第三方插件目录**，一律写进项目自己的模块。
- **不修改引擎**。一个 world 只渲染一套 `SkyAtmosphere` 是架构性的（`FScene` 只有单个 `FSkyAtmosphereRenderSceneInfo*`），本次接受它。
- 大气厚度比 `AtmosphereHeightRatio` 默认 **`0.05`**；壳与引擎大气**必须用同一个值**，否则玩家穿过壳边界会看到跳变。
- 散射系数用**几何缩放**：`s = 行星半径km / 6360`，长度 `×s`，系数 `×1/s`。地球基准值（来自 `Engine/Private/Components/SkyAtmosphereComponent.cpp:96-118`，已核对）：`BottomRadius 6360 km`、`AtmosphereHeight 60 km`、`RayleighScatteringScale 0.033100`、`MieScatteringScale 0.003996`、`MieAbsorptionScale 0.000444`、`RayleighExponentialDistribution 8.0 km`、`MieExponentialDistribution 1.2 km`、`RayleighScattering` 归一化颜色 `(0.175287, 0.409607, 1.0)`。
- **构建**：`%TEMP%\ue_mcp\ue_build.ps1`（捆绑 dotnet 直调 `UnrealBuildTool.dll`，cwd 在项目外，绕开 `Build.bat` 的锁）。**构建前必须关闭编辑器**。
- **自动化测试**：`UnrealEditor-Cmd.exe` + `-ExecCmds="Automation RunTests <filter>"`，编辑器开着也能跑（只有 build 需要关它）。
- **提交**：pathspec 必须挂在 `commit` 上（`git commit -m ... -- <paths>`）。裸 `git commit` 会吞掉索引里全部暂存文件。
- **源文件换行符**：不要"统一成 LF"。`autocrlf=true` 下 CRLF 才是稳定态。
- 中文源文件保存为 **UTF-8 with BOM**，否则 VS/IntelliSense 会按 GBK 误读报假错。
- 每个 commit message 结尾加 `Co-Authored-By: Claude Code <noreply@anthropic.com>`。

---

## 与规格的偏差与补全

写计划时逐条核对规格，发现四处需要修正或补全。**实施时以本计划为准**，实施完把这四条回写进规格。

1. **§5.5 说 `StellarEraPlanet.Build.cs` 要加 `UnrealEd`——不需要。**
   已核对：`UPackage::SavePackage` 声明在 `CoreUObject`（`CoreUObject/Public/UObject/Package.h:1204`，`COREUOBJECT_API`）。
   材质与材质表达式节点全在 `Engine`。**本次不引入任何新的模块依赖**，`StellarEraPlanet.Build.cs` 一个字不改。

2. **§5.2 与 §6 互相指认，壳的散射系数 `RayleighBeta` / `MieBeta` 没有初值。**
   §5.2 的"初值来源"栏写"见 §6"，§6 写"壳的参数不用这套，直接按 §5.2 用 `ShellRadius` 的比例给初值"——循环。
   **补全办法**：让壳的**垂直光学厚度**等于地球的。光学厚度 `τ = β × H` 是无量纲量，也是真正决定观感的量；壳的厚度是观感值（比物理值厚约 5.3 倍），所以系数必须相应变薄，观感才对。
   `β_Rayleigh(B通道) = (0.033100 × 8.0) / RayleighScaleHeightCm`，`β_Mie = ((0.003996 + 0.000444) × 1.2) / MieScaleHeightCm`，RGB 比例取自地球的归一化颜色。

3. **§5.3 写米氏 Henyey-Greenstein 的 `g = 0.76`，改为 `g = 0.8`。**
   引擎 `USkyAtmosphereComponent` 的 `MieAnisotropy` 默认就是 `0.8`（`SkyAtmosphereComponent.cpp:117`）。两边取同一个值，壳与引擎大气的**前向散射波瓣形状一致**，穿过壳边界时不会有观感突变。0.76 与 0.8 肉眼无法分辨，这个改动纯粹是为了消掉一个不必要的差异。

4. **§5.2 那条"空间一致性（实现第一步就要钉死）"已经不是未知数了，从引擎源码钉死了。**
   - `UMaterialExpressionWorldPosition::Compile` 调 `Compiler->WorldPosition(WorldPositionShaderOffset)`（`MaterialExpressions.cpp:12263-12267`）。
   - `WPT_Default` 的节点标题字面就是 **"Absolute World Position"**（`MaterialExpressions.cpp:12273-12276`）。
   - `UMaterialExpressionCustom` 的输入声明表里，`MCT_LWCVector3` 生成的是 **`float3 <名字>, FWSVector3 LWC<名字>`**（`HLSLMaterialTranslator.cpp:14184-14189`）——**第一个参数就是我们用的那个 `float3`**，注释原文：`// This way legacy custom expressions can continue to operate on regular float values`（`:14385-14386`）。第二个 `LWC<名字>` 参数我们不引用即可。
   - `WSDemote` 走 `LWCToFloat(FLWCVector3 Value) { return LWCGetTileOffset(Value) + Value.Offset; }`（`LargeWorldCoordinates.ush:249`），返回**绝对值**。
   - `GetWorldPosition(FMaterialPixelParameters)` 返回 `Parameters.LWCData.AbsoluteWorldPosition`（`MaterialTemplate.ush:989-991`）。

   **结论**：`WorldPosition` 节点（`WPT_Default`）的输出 `WorldPos` 就是**绝对世界坐标（cm）**，与我们在 C++ 里用 `FVector` 写进 `CameraPos` / `PlanetCenter` 向量参数的空间**一致**。三个值同空间，可以直接做射线求交。
   **精度**：绝对空间下这些值在 `5e6 cm` 量级，float32 分辨率约 `0.5 cm`；壳厚 `3750 cm`、瑞利标高 `1181 cm`，相对误差 `4e-4`，可忽略。

---

## 文件结构

| 文件 | 职责 |
|---|---|
| `Source/StellarEraPlanet/Public/PlanetAtmosphereParams.h` | 纯数学：地球基准常量、壳半径、引擎大气缩放参数、壳散射参数。无 UObject。 |
| `Source/StellarEraPlanet/Private/PlanetAtmosphereParams.cpp` | 上者的实现。 |
| `Source/StellarEraPlanet/Private/PlanetAtmosphereParamsTests.cpp` | 上者的自动化测试。 |
| `Source/StellarEraPlanet/Private/PlanetAtmosphereMaterialBuilder.cpp` | 编辑器专用：HLSL 字符串 + 用 C++ 造材质资产 + 控制台命令。整文件 `#if WITH_EDITOR`。 |
| `Source/StellarEraPlanet/Public/PlanetAtmosphereComponent.h` | 壳组件声明。 |
| `Source/StellarEraPlanet/Private/PlanetAtmosphereComponent.cpp` | 壳组件实现：球网格、动态材质实例、逐帧推参数。 |
| `Source/StellarEraPlanet/Public/VoxelPlanetActor.h` | 改：加壳组件指针与取值函数。 |
| `Source/StellarEraPlanet/Private/VoxelPlanetActor.cpp` | 改：构造函数里建壳组件。 |
| `Source/StellarEra/Public/PlanetAtmosphereTargeting.h` | 纯函数：在一堆行星里找"表面距离最近"的那颗。 |
| `Source/StellarEra/Private/PlanetAtmosphereTargeting.cpp` | 上者的实现。 |
| `Source/StellarEra/Private/PlanetAtmosphereTargetingTests.cpp` | 上者的自动化测试。 |
| `Source/StellarEra/Public/CosmosSimulationSubsystem.h` | 改：声明重定位方法与配置项。 |
| `Source/StellarEra/Private/CosmosSimulationSubsystem.cpp` | 改：Tick 里加一步重定位。 |
| `Content/Materials/M_PlanetAtmosphere.uasset` | 由控制台命令生成，随仓库提交。 |

---

### Task 1: 大气参数推导（纯函数 + 自动化测试）

把三条公式做成纯函数并用自动化测试锁住。这是后面所有任务的数值地基：参数写错，后面调半天都调不出来，而且调错了看不出来。

**Files:**
- Create: `Source/StellarEraPlanet/Public/PlanetAtmosphereParams.h`
- Create: `Source/StellarEraPlanet/Private/PlanetAtmosphereParams.cpp`
- Test: `Source/StellarEraPlanet/Private/PlanetAtmosphereParamsTests.cpp`

**Interfaces:**
- Consumes: 无（本任务是最底层）
- Produces:
  - `namespace PlanetAtmosphere` 里的常量：`EarthBottomRadiusKm`、`EarthAtmosphereHeightKm`、`EarthRayleighScatteringScale`、`EarthMieScatteringScale`、`EarthMieAbsorptionScale`、`EarthRayleighScaleHeightKm`、`EarthMieScaleHeightKm`、`EarthRayleighScatteringColor`（`FVector3f`）
  - `float PlanetAtmosphere::ComputeShellRadiusCm(float SurfaceRadiusCm, float AtmosphereHeightRatio)`
  - `struct PlanetAtmosphere::FSkyAtmosphereScaleParams`，成员 `float BottomRadiusKm; float AtmosphereHeightKm; float RayleighScatteringScale; float MieScatteringScale; float MieAbsorptionScale; float RayleighExponentialDistributionKm; float MieExponentialDistributionKm; float Scale;`
  - `FSkyAtmosphereScaleParams PlanetAtmosphere::ComputeSkyAtmosphereScaleParams(float SurfaceRadiusKm, float AtmosphereHeightRatio)`
  - `struct PlanetAtmosphere::FShellScatteringParams`，成员 `FVector3f RayleighBeta; FVector3f MieBeta; float RayleighScaleHeightCm; float MieScaleHeightCm;`
  - `FShellScatteringParams PlanetAtmosphere::ComputeShellScatteringParams(float ShellRadiusCm, float RayleighScaleHeightRatio, float MieScaleHeightRatio)`

- [ ] **Step 1: 写失败的测试**

创建 `Source/StellarEraPlanet/Private/PlanetAtmosphereParamsTests.cpp`（**保存为 UTF-8 with BOM**）：

```cpp
#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "PlanetAtmosphereParams.h"
#include "Components/SkyAtmosphereComponent.h"

#if WITH_DEV_AUTOMATION_TESTS

// 断言写法说明：指针一律用 TestTrue/TestFalse 比较。TestEqual 的泛型重载要走
// LexToString，而指针没有可靠的 ToString —— 会编不过或打出没用的东西。
// 浮点一律带容差。

// 这一组锁死"我们的常量 == 引擎的默认值"。
// 引擎升级改了默认值，这里会红，而不是悄悄让所有缩放算错。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraAtmosphereEarthDefaultsTest,
	"StellarEra.Planet.AtmosphereParams.EarthDefaults",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FStellarEraAtmosphereEarthDefaultsTest::RunTest(const FString& Parameters)
{
	USkyAtmosphereComponent* EngineDefaults = NewObject<USkyAtmosphereComponent>();

	TestEqual(TEXT("BottomRadius"), PlanetAtmosphere::EarthBottomRadiusKm,
		EngineDefaults->BottomRadius, 0.01f);
	TestEqual(TEXT("AtmosphereHeight"), PlanetAtmosphere::EarthAtmosphereHeightKm,
		EngineDefaults->AtmosphereHeight, 0.01f);
	TestEqual(TEXT("RayleighScatteringScale"), PlanetAtmosphere::EarthRayleighScatteringScale,
		EngineDefaults->RayleighScatteringScale, 1e-6f);
	TestEqual(TEXT("MieScatteringScale"), PlanetAtmosphere::EarthMieScatteringScale,
		EngineDefaults->MieScatteringScale, 1e-6f);
	TestEqual(TEXT("MieAbsorptionScale"), PlanetAtmosphere::EarthMieAbsorptionScale,
		EngineDefaults->MieAbsorptionScale, 1e-6f);
	TestEqual(TEXT("RayleighExponentialDistribution"), PlanetAtmosphere::EarthRayleighScaleHeightKm,
		EngineDefaults->RayleighExponentialDistribution, 1e-6f);
	TestEqual(TEXT("MieExponentialDistribution"), PlanetAtmosphere::EarthMieScaleHeightKm,
		EngineDefaults->MieExponentialDistribution, 1e-6f);

	// 引擎把 RayleightScatteringRaw 按 B 通道归一化后存进 RayleighScattering，
	// 把原始值存进 RayleighScatteringScale。我们记的是归一化后的颜色。
	TestEqual(TEXT("RayleighScattering.R"), PlanetAtmosphere::EarthRayleighScatteringColor.X,
		EngineDefaults->RayleighScattering.R, 1e-4f);
	TestEqual(TEXT("RayleighScattering.G"), PlanetAtmosphere::EarthRayleighScatteringColor.Y,
		EngineDefaults->RayleighScattering.G, 1e-4f);
	TestEqual(TEXT("RayleighScattering.B"), PlanetAtmosphere::EarthRayleighScatteringColor.Z,
		EngineDefaults->RayleighScattering.B, 1e-4f);

	return true;
}

// 壳半径：几何 + 观感厚度比。用 UniverseMap 的实际尺度（0.75 km 半径）。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraAtmosphereShellRadiusTest,
	"StellarEra.Planet.AtmosphereParams.ShellRadius",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FStellarEraAtmosphereShellRadiusTest::RunTest(const FString& Parameters)
{
	// 75000 cm 半径（0.75 km），厚度比 0.05 → 78750 cm
	TestEqual(TEXT("0.75km planet, ratio 0.05"),
		PlanetAtmosphere::ComputeShellRadiusCm(75000.0f, 0.05f), 78750.0f, 0.5f);

	// 厚度比为 0 时壳与地面重合
	TestEqual(TEXT("ratio 0 collapses to surface"),
		PlanetAtmosphere::ComputeShellRadiusCm(75000.0f, 0.0f), 75000.0f, 0.5f);

	// 线性：厚度比减半，增量减半
	TestEqual(TEXT("ratio scales linearly"),
		PlanetAtmosphere::ComputeShellRadiusCm(75000.0f, 0.025f), 75000.0f * 1.025f, 0.5f);

	return true;
}

// 引擎大气：长度 ×s、系数 ×(1/s)。1/s 在这里正好是整数 8480。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraAtmosphereEngineScaleTest,
	"StellarEra.Planet.AtmosphereParams.EngineScale",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FStellarEraAtmosphereEngineScaleTest::RunTest(const FString& Parameters)
{
	const PlanetAtmosphere::FSkyAtmosphereScaleParams P =
		PlanetAtmosphere::ComputeSkyAtmosphereScaleParams(0.75f, 0.05f);

	// s = 0.75 / 6360 = 1/8480
	TestEqual(TEXT("Scale"), P.Scale, 0.75f / 6360.0f, 1e-9f);

	// 长度按行星半径与厚度比给，不用几何缩放 —— 这是观感厚度，与壳一致
	TestEqual(TEXT("BottomRadiusKm"), P.BottomRadiusKm, 0.75f, 1e-5f);
	TestEqual(TEXT("AtmosphereHeightKm"), P.AtmosphereHeightKm, 0.75f * 0.05f, 1e-5f);

	// 系数 ×(1/s) = ×8480
	TestEqual(TEXT("RayleighScatteringScale"), P.RayleighScatteringScale, 0.0331f * 8480.0f, 1.0f);
	TestEqual(TEXT("MieScatteringScale"), P.MieScatteringScale, 0.003996f * 8480.0f, 0.1f);
	TestEqual(TEXT("MieAbsorptionScale"), P.MieAbsorptionScale, 0.000444f * 8480.0f, 0.01f);

	// 标高是长度，×s
	TestEqual(TEXT("RayleighExponentialDistributionKm"),
		P.RayleighExponentialDistributionKm, 8.0f / 8480.0f, 1e-9f);
	TestEqual(TEXT("MieExponentialDistributionKm"),
		P.MieExponentialDistributionKm, 1.2f / 8480.0f, 1e-9f);

	return true;
}

// 壳散射：垂直光学厚度对齐地球。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraAtmosphereShellScatteringTest,
	"StellarEra.Planet.AtmosphereParams.ShellScattering",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FStellarEraAtmosphereShellScatteringTest::RunTest(const FString& Parameters)
{
	const float ShellRadiusCm = PlanetAtmosphere::ComputeShellRadiusCm(75000.0f, 0.05f); // 78750
	const PlanetAtmosphere::FShellScatteringParams S =
		PlanetAtmosphere::ComputeShellScatteringParams(ShellRadiusCm, 0.015f, 0.003f);

	// 标高是壳半径的比例
	TestEqual(TEXT("RayleighScaleHeightCm"), S.RayleighScaleHeightCm, 78750.0f * 0.015f, 0.5f);
	TestEqual(TEXT("MieScaleHeightCm"), S.MieScaleHeightCm, 78750.0f * 0.003f, 0.5f);

	// 核心不变式：垂直光学厚度 τ = β × H 等于地球的
	// 地球瑞利 τ（B 通道）= 0.033100 × 8.0 = 0.2648
	const float EarthTauRayleigh = PlanetAtmosphere::EarthRayleighScatteringScale
		* PlanetAtmosphere::EarthRayleighScaleHeightKm;
	TestEqual(TEXT("Rayleigh vertical optical depth matches Earth"),
		S.RayleighBeta.Z * S.RayleighScaleHeightCm, EarthTauRayleigh, 1e-4f);

	// 地球米氏 τ = (0.003996 + 0.000444) × 1.2 = 0.005328
	const float EarthTauMie = (PlanetAtmosphere::EarthMieScatteringScale
		+ PlanetAtmosphere::EarthMieAbsorptionScale) * PlanetAtmosphere::EarthMieScaleHeightKm;
	TestEqual(TEXT("Mie vertical optical depth matches Earth"),
		S.MieBeta.X * S.MieScaleHeightCm, EarthTauMie, 1e-5f);

	// RGB 比例取自地球：B 通道归一化为 1，R/G 按比例
	TestEqual(TEXT("Rayleigh RGB ratio R"), S.RayleighBeta.X / S.RayleighBeta.Z, 0.175287f, 1e-4f);
	TestEqual(TEXT("Rayleigh RGB ratio G"), S.RayleighBeta.Y / S.RayleighBeta.Z, 0.409607f, 1e-4f);

	// 米氏是白的
	TestTrue(TEXT("Mie is white"),
		FMath::IsNearlyEqual(S.MieBeta.X, S.MieBeta.Y, 1e-12f)
		&& FMath::IsNearlyEqual(S.MieBeta.Y, S.MieBeta.Z, 1e-12f));

	// 量级护栏：写错单位（cm/km 混用）会让这两个数差 5 个数量级
	TestTrue(TEXT("RayleighBeta magnitude is 1e-4 /cm"), S.RayleighBeta.Z > 1e-5f && S.RayleighBeta.Z < 1e-3f);
	TestTrue(TEXT("MieBeta magnitude is 1e-5 /cm"), S.MieBeta.X > 1e-6f && S.MieBeta.X < 1e-4f);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
```

- [ ] **Step 2: 跑测试，确认它编不过**

关闭编辑器，然后：

```bash
powershell -NoProfile -ExecutionPolicy Bypass -File "$TEMP/ue_mcp/ue_build.ps1"
```

预期：编译失败，报 `Cannot open include file: 'PlanetAtmosphereParams.h'`。

（`ue_build.ps1` 是既有的 UBT 直调脚本。如果它不存在或参数不同，改用仓库里既有的构建方式；关键点是**先关编辑器**，且用捆绑 dotnet 直调 `UnrealBuildTool.dll`、cwd 在项目外。）

- [ ] **Step 3: 写实现**

创建 `Source/StellarEraPlanet/Public/PlanetAtmosphereParams.h`（**UTF-8 with BOM**）：

```cpp
#pragma once

#include "CoreMinimal.h"

/**
 * 行星大气的参数推导。
 *
 * 两条互相独立的规则，别混：
 *
 *  - 引擎大气（ASkyAtmosphere，全场一个）：用几何缩放模型。
 *    长度 ×s、系数 ×(1/s) 时辐射传输方程完全不变，所以这是"物理正确"的缩放。
 *    s = 行星半径km / 6360。
 *
 *  - 壳（每颗行星一个，自写散射）：用观感缩放。
 *    壳的厚度是观感参数（比物理厚度厚约 5.3 倍，否则从太空看是一条看不见的线），
 *    所以系数不能跟着几何走，而是让垂直光学厚度 τ = β × H 与地球相等。
 *
 * 两边唯一必须一致的是**厚度比**，否则玩家穿过壳边界会看到跳变。
 *
 * 本文件不含 UObject，可被任意模块直接单测。
 */
namespace PlanetAtmosphere
{
	// ---- 地球基准。与 USkyAtmosphereComponent 构造函数的默认值一致，
	// ---- 出处 Engine/Private/Components/SkyAtmosphereComponent.cpp:96-118。
	// ---- PlanetAtmosphereParamsTests.cpp 里有一条测试把它们与引擎的默认值对拍，
	// ---- 引擎改默认值会让那条测试变红。
	constexpr float EarthBottomRadiusKm = 6360.0f;
	constexpr float EarthAtmosphereHeightKm = 60.0f;
	constexpr float EarthRayleighScatteringScale = 0.033100f;
	constexpr float EarthMieScatteringScale = 0.003996f;
	constexpr float EarthMieAbsorptionScale = 0.000444f;
	constexpr float EarthRayleighScaleHeightKm = 8.0f;
	constexpr float EarthMieScaleHeightKm = 1.2f;

	/** 引擎把 RayleightScatteringRaw 按 B 通道归一化后存进 RayleighScattering。 */
	inline const FVector3f EarthRayleighScatteringColor(0.175287f, 0.409607f, 1.0f);

	/** 壳半径（cm）= 表面半径 × (1 + 厚度比)。 */
	STELLARERAPLANET_API float ComputeShellRadiusCm(float SurfaceRadiusCm, float AtmosphereHeightRatio);

	/** 引擎原生大气按行星半径缩放后的参数。长度单位 km，系数单位 1/km。 */
	struct FSkyAtmosphereScaleParams
	{
		float BottomRadiusKm = 0.0f;
		float AtmosphereHeightKm = 0.0f;
		float RayleighScatteringScale = 0.0f;
		float MieScatteringScale = 0.0f;
		float MieAbsorptionScale = 0.0f;
		float RayleighExponentialDistributionKm = 0.0f;
		float MieExponentialDistributionKm = 0.0f;
		/** s = 行星半径km / 6360，留作日志与断言。 */
		float Scale = 0.0f;
	};

	/**
	 * 几何缩放：长度 ×s、系数 ×(1/s)。
	 *
	 * 但 AtmosphereHeightKm **不**用几何缩放，而是 = 行星半径km × 厚度比。
	 * 这是刻意的：引擎大气的厚度必须与壳一致（都是观感厚度），否则玩家穿过
	 * 壳边界时会看到跳变。厚度比地球大 5.3 倍**不会**让光学厚度变大 ——
	 * 那是 Task 5 用 AtmosphereCoefficientScale 调的事。
	 */
	STELLARERAPLANET_API FSkyAtmosphereScaleParams ComputeSkyAtmosphereScaleParams(
		float SurfaceRadiusKm, float AtmosphereHeightRatio);

	/** 壳的散射参数。系数单位 1/cm，长度单位 cm。 */
	struct FShellScatteringParams
	{
		FVector3f RayleighBeta = FVector3f::ZeroVector;
		/** 米氏散射 + 吸收的总消光系数（吸收那 11% 折进来一起当散射用，单次散射模型下可忽略）。 */
		FVector3f MieBeta = FVector3f::ZeroVector;
		float RayleighScaleHeightCm = 0.0f;
		float MieScaleHeightCm = 0.0f;
	};

	/**
	 * 壳的散射：标高按壳半径的比例给（观感），系数按"垂直光学厚度等于地球"反推。
	 *
	 * τ = β × H 是无量纲的，也是真正决定观感的量。壳比物理厚度厚约 5.3 倍，
	 * 所以 β 必须相应变薄，观感才对。
	 *
	 * @param ShellRadiusCm             壳半径（cm），来自 ComputeShellRadiusCm
	 * @param RayleighScaleHeightRatio  瑞利标高 / 壳半径，默认 0.015
	 * @param MieScaleHeightRatio       米氏标高 / 壳半径，默认 0.003
	 */
	STELLARERAPLANET_API FShellScatteringParams ComputeShellScatteringParams(
		float ShellRadiusCm, float RayleighScaleHeightRatio, float MieScaleHeightRatio);

} // namespace PlanetAtmosphere
```

创建 `Source/StellarEraPlanet/Private/PlanetAtmosphereParams.cpp`（**UTF-8 with BOM**）：

```cpp
#include "PlanetAtmosphereParams.h"

namespace PlanetAtmosphere
{

float ComputeShellRadiusCm(float SurfaceRadiusCm, float AtmosphereHeightRatio)
{
	return SurfaceRadiusCm * (1.0f + AtmosphereHeightRatio);
}

FSkyAtmosphereScaleParams ComputeSkyAtmosphereScaleParams(
	float SurfaceRadiusKm, float AtmosphereHeightRatio)
{
	FSkyAtmosphereScaleParams Out;

	// 几何缩放因子。行星半径 0.75 km 时 s = 1/8480。
	const float Scale = SurfaceRadiusKm / EarthBottomRadiusKm;
	if (Scale <= 0.0f)
	{
		// 半径非正无法缩放。返回全零，调用方应据此跳过写入。
		return Out;
	}
	const float InverseScale = 1.0f / Scale;

	Out.Scale = Scale;

	// 长度：半径直接用行星的；厚度用观感厚度比（必须与壳一致，见头文件说明）。
	Out.BottomRadiusKm = SurfaceRadiusKm;
	Out.AtmosphereHeightKm = SurfaceRadiusKm * AtmosphereHeightRatio;

	// 系数：×(1/s)。
	Out.RayleighScatteringScale = EarthRayleighScatteringScale * InverseScale;
	Out.MieScatteringScale = EarthMieScatteringScale * InverseScale;
	Out.MieAbsorptionScale = EarthMieAbsorptionScale * InverseScale;

	// 标高也是长度：×s。
	Out.RayleighExponentialDistributionKm = EarthRayleighScaleHeightKm * Scale;
	Out.MieExponentialDistributionKm = EarthMieScaleHeightKm * Scale;

	return Out;
}

FShellScatteringParams ComputeShellScatteringParams(
	float ShellRadiusCm, float RayleighScaleHeightRatio, float MieScaleHeightRatio)
{
	FShellScatteringParams Out;

	if (ShellRadiusCm <= 0.0f || RayleighScaleHeightRatio <= 0.0f || MieScaleHeightRatio <= 0.0f)
	{
		return Out;
	}

	Out.RayleighScaleHeightCm = ShellRadiusCm * RayleighScaleHeightRatio;
	Out.MieScaleHeightCm = ShellRadiusCm * MieScaleHeightRatio;

	// 地球的垂直光学厚度，无量纲。
	const float EarthTauRayleigh = EarthRayleighScatteringScale * EarthRayleighScaleHeightKm;
	const float EarthTauMie =
		(EarthMieScatteringScale + EarthMieAbsorptionScale) * EarthMieScaleHeightKm;

	// 反推系数：τ = β × H → β = τ / H。
	// 地球的 RayleighScatteringScale 是 B 通道的原始值，所以这里得到的是 B 通道系数，
	// 再乘归一化颜色得到 RGB。
	const float RayleighBetaB = EarthTauRayleigh / Out.RayleighScaleHeightCm;
	Out.RayleighBeta = EarthRayleighScatteringColor * RayleighBetaB;

	const float MieBetaScalar = EarthTauMie / Out.MieScaleHeightCm;
	Out.MieBeta = FVector3f(MieBetaScalar, MieBetaScalar, MieBetaScalar);

	return Out;
}

} // namespace PlanetAtmosphere
```

`STELLARERAPLANET_API` 这个宏名必须与 `StellarEraPlanet.Build.cs` 里 UBT 生成的模块宏一致（`<模块名大写>_API`）。构建时若报未定义的宏，看一眼 `Intermediate/Build/.../Definitions.h` 里 `STELLARERAPLANET_API` 是否存在；不存在就把这三处 `STELLARERAPLANET_API` 去掉（同模块内使用不需要导出宏）。

- [ ] **Step 4: 构建**

```bash
powershell -NoProfile -ExecutionPolicy Bypass -File "$TEMP/ue_mcp/ue_build.ps1"
```

预期：`Result: Succeeded`。

- [ ] **Step 5: 跑测试，确认通过**

```bash
powershell -NoProfile -ExecutionPolicy Bypass -File "$TEMP/ue_mcp/run_nbody_test.ps1" -Filter "StellarEra.Planet.AtmosphereParams"
```

如果脚本不支持 `-Filter`，直接按既有模板改一个：`UnrealEditor-Cmd.exe "<项目>.uproject" -ExecCmds="Automation RunTests StellarEra.Planet.AtmosphereParams" -TestExit="Automation Test Queue Empty" -unattended -nopause -nullrhi -nosplash -stdout -abslog="<日志路径>"`。

预期：4 条测试全 `Success`（`EarthDefaults` / `ShellRadius` / `EngineScale` / `ShellScattering`）。

**如果 `EarthDefaults` 那条红了**：说明我们记的地球基准常量与引擎默认值不符。以引擎为准，改 `PlanetAtmosphereParams.h` 里的常量，**同时**改本任务测试里的期望值（`EngineScale` / `ShellScattering` 两条里的 `8480`、`0.2648`、`0.005328` 是按 `6360 / 0.75` 和地球常量算出来的，常量一改这些数字要跟着重算）。**不要**为了迁就测试改测试的容差。

- [ ] **Step 6: 提交**

```bash
git add Source/StellarEraPlanet/Public/PlanetAtmosphereParams.h \
        Source/StellarEraPlanet/Private/PlanetAtmosphereParams.cpp \
        Source/StellarEraPlanet/Private/PlanetAtmosphereParamsTests.cpp
git commit -m "$(cat <<'EOF'
feat(大气): 行星大气参数推导 + 自动化测试

三条纯函数，各自被测试锁住：

- ComputeShellRadiusCm —— 壳半径 = 表面半径 × (1 + 厚度比)
- ComputeSkyAtmosphereScaleParams —— 引擎大气的几何缩放（长度 ×s、系数 ×1/s），
  但厚度用观感厚度比，以便与壳一致
- ComputeShellScatteringParams —— 壳的标高按壳半径比例给（观感），系数按
  「垂直光学厚度 τ = β×H 等于地球」反推

补上了规格 §5.2/§6 互相指认导致的一个空洞：壳的 RayleighBeta/MieBeta
原本没有初值。

另有一条测试把我们的地球基准常量与 USkyAtmosphereComponent 的默认值对拍，
引擎升级改了默认值会红，而不是让所有缩放悄悄算错。

Co-Authored-By: Claude Code <noreply@anthropic.com>
EOF
)" -- Source/StellarEraPlanet/Public/PlanetAtmosphereParams.h \
       Source/StellarEraPlanet/Private/PlanetAtmosphereParams.cpp \
       Source/StellarEraPlanet/Private/PlanetAtmosphereParamsTests.cpp
```

---

### Task 2: 材质生成器 + 控制台命令

用 C++ 造出 `/Game/Materials/M_PlanetAtmosphere`。HLSL 作为仓库内的字符串，材质资产自带全部源码，零外部路径依赖（不用 `IncludeFilePaths`，理由见规格 §3.3）。

**Files:**
- Create: `Source/StellarEraPlanet/Private/PlanetAtmosphereMaterialBuilder.cpp`
- Create（由控制台命令生成）: `Content/Materials/M_PlanetAtmosphere.uasset`

**Interfaces:**
- Consumes: `PlanetAtmosphere::ComputeShellScatteringParams`（Task 1）——**不直接用**，只在下面 Step 1 的材质参数默认值里对齐它算出的量级
- Produces: 材质资产 `/Game/Materials/M_PlanetAtmosphere`，其参数接口（Task 3 按名字设置）：
  - Scalar: `PlanetRadius`、`ShellRadius`、`RayleighScaleHeight`、`MieScaleHeight`
  - Vector: `RayleighBeta`、`MieBeta`、`CameraPos`、`PlanetCenter`、`SunDirection`、`SunIntensity`

- [ ] **Step 1: 写生成器**

创建 `Source/StellarEraPlanet/Private/PlanetAtmosphereMaterialBuilder.cpp`（**UTF-8 with BOM**）。

这个文件里每一块都必须照抄，不要"类似地"省略 —— 材质节点图是用 C++ 手工拼的，漏一个 `AddExpression` 或漏设 `Material` 指针都会让节点变成孤儿、材质编译报 "no expressions"。

```cpp
#include "CoreMinimal.h"

#if WITH_EDITOR

#include "HAL/IConsoleManager.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionComponentMask.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialExpressionWorldPosition.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

namespace
{

const TCHAR* const AtmosphereAssetPath = TEXT("/Game/Materials/M_PlanetAtmosphere");

// ---------------------------------------------------------------------------
// 壳的单次散射。自包含：不用 IncludeFilePaths（那需要一套 shader 目录映射），
// 不调用引擎的散射函数（那些要 LUT 或全局 Atmosphere uniform，材质里拿不到）。
//
// 坐标空间（已从引擎源码钉死，见计划「与规格的偏差」第 4 条）：
//   WorldPos / CameraPos / PlanetCenter 全是**绝对世界坐标（cm）**。
//   WorldPosition 节点 WPT_Default 的标题字面就是 "Absolute World Position"。
//
// 注意：因为函数体里有 return，UE 不会再自动包一层 return，末尾必须自己 return。
// ---------------------------------------------------------------------------
const TCHAR* const AtmosphereHLSL = TEXT(R"(
#define ATMO_STEPS 16
#define ATMO_PI 3.14159265358979323846

float3 ro = CameraPos;
float3 rd = normalize(WorldPos - CameraPos);

// 与外球（壳）求交
float3 oc = ro - PlanetCenter;
float bOuter = dot(oc, rd);
float cOuter = dot(oc, oc) - ShellRadius * ShellRadius;
float discOuter = bOuter * bOuter - cOuter;
if (discOuter < 0.0)
{
    return float4(0.0, 0.0, 0.0, 0.0);
}
float sqrtOuter = sqrt(discOuter);
float tEnter = max(-bOuter - sqrtOuter, 0.0);
float tExit = -bOuter + sqrtOuter;
if (tExit <= tEnter)
{
    return float4(0.0, 0.0, 0.0, 0.0);
}

// 撞到行星地面就停在表面
float cInner = dot(oc, oc) - PlanetRadius * PlanetRadius;
float discInner = bOuter * bOuter - cInner;
if (discInner >= 0.0)
{
    float tGround = -bOuter - sqrt(discInner);
    if (tGround > tEnter)
    {
        tExit = min(tExit, tGround);
    }
}
if (tExit <= tEnter)
{
    return float4(0.0, 0.0, 0.0, 0.0);
}

// 相位函数：瑞利各向同性、米氏 Henyey-Greenstein。
// g = 0.8 与引擎 USkyAtmosphereComponent 的 MieAnisotropy 默认值一致，
// 这样壳与引擎大气的前向散射波瓣形状相同，穿过壳边界时观感不突变。
float cosTheta = dot(rd, SunDirection.rgb);
float phaseR = 3.0 / (16.0 * ATMO_PI) * (1.0 + cosTheta * cosTheta);
float g = 0.8;
float g2 = g * g;
float phaseM = (1.0 - g2) / (4.0 * ATMO_PI * pow(max(1.0 + g2 - 2.0 * g * cosTheta, 1e-4), 1.5));

// 单次散射积分：固定步长中点法
float ds = (tExit - tEnter) / float(ATMO_STEPS);
float3 transmittance = float3(1.0, 1.0, 1.0);
float3 accumulated = float3(0.0, 0.0, 0.0);
for (int i = 0; i < ATMO_STEPS; ++i)
{
    float t = tEnter + (float(i) + 0.5) * ds;
    float3 p = ro + rd * t;
    float h = max(length(p - PlanetCenter) - PlanetRadius, 0.0);
    float dR = exp(-h / RayleighScaleHeight);
    float dM = exp(-h / MieScaleHeight);
    float3 extinction = RayleighBeta.rgb * dR + MieBeta.rgb * dM;
    transmittance *= exp(-extinction * ds);
    accumulated += (RayleighBeta.rgb * dR * phaseR + MieBeta.rgb * dM * phaseM) * transmittance * ds;
}

float coverage = saturate(1.0 - dot(transmittance, float3(1.0 / 3.0, 1.0 / 3.0, 1.0 / 3.0)));
return float4(accumulated * SunIntensity.rgb, coverage);
)");

/** 建一个标量参数节点并登记到材质。 */
UMaterialExpressionScalarParameter* AddScalarParam(UMaterial* Material, const TCHAR* Name,
	float Default, int32 PosX, int32 PosY)
{
	UMaterialExpressionScalarParameter* Expr = NewObject<UMaterialExpressionScalarParameter>(Material);
	Expr->Material = Material;
	Expr->ParameterName = Name;
	Expr->DefaultValue = Default;
	Expr->MaterialExpressionEditorX = PosX;
	Expr->MaterialExpressionEditorY = PosY;
	Material->GetExpressionCollection().AddExpression(Expr);
	return Expr;
}

/** 建一个向量参数节点并登记到材质。 */
UMaterialExpressionVectorParameter* AddVectorParam(UMaterial* Material, const TCHAR* Name,
	FLinearColor Default, int32 PosX, int32 PosY)
{
	UMaterialExpressionVectorParameter* Expr = NewObject<UMaterialExpressionVectorParameter>(Material);
	Expr->Material = Material;
	Expr->ParameterName = Name;
	Expr->DefaultValue = Default;
	Expr->MaterialExpressionEditorX = PosX;
	Expr->MaterialExpressionEditorY = PosY;
	Material->GetExpressionCollection().AddExpression(Expr);
	return Expr;
}

/** 给 Custom 节点加一个命名输入。InputName 就是 HLSL 里的变量名。 */
void AddCustomInput(UMaterialExpressionCustom* Custom, const TCHAR* InputName, UMaterialExpression* Source)
{
	FCustomInput& In = Custom->Inputs.AddDefaulted_GetRef();
	In.InputName = InputName;
	In.Input.Expression = Source;
	In.Input.OutputIndex = 0;
}

bool BuildPlanetAtmosphereMaterial(FString& OutError)
{
	UPackage* Package = CreatePackage(AtmosphereAssetPath);
	if (!Package)
	{
		OutError = FString::Printf(TEXT("CreatePackage 失败：%s"), AtmosphereAssetPath);
		return false;
	}

	// 已存在就复用，避免同名对象冲突。
	UMaterial* Material = FindObject<UMaterial>(Package, TEXT("M_PlanetAtmosphere"));
	if (!Material)
	{
		Material = NewObject<UMaterial>(Package, TEXT("M_PlanetAtmosphere"), RF_Public | RF_Standalone);
	}
	if (!Material)
	{
		OutError = TEXT("NewObject<UMaterial> 失败");
		return false;
	}

	Material->PreEditChange(nullptr);
	Material->GetExpressionCollection().Empty();

	// --- 几何输入 ---
	UMaterialExpressionWorldPosition* WorldPos = NewObject<UMaterialExpressionWorldPosition>(Material);
	WorldPos->Material = Material;
	WorldPos->WorldPositionShaderOffset = WPT_Default; // "Absolute World Position"
	WorldPos->MaterialExpressionEditorX = -800;
	WorldPos->MaterialExpressionEditorY = -600;
	Material->GetExpressionCollection().AddExpression(WorldPos);

	// --- 参数节点。初值与 Task 1 的 ComputeShellScatteringParams 在 0.75km 行星上
	// --- 算出的量级一致；Task 3 会在 BeginPlay 里按实际行星半径覆盖掉。
	UMaterialExpressionScalarParameter* PlanetRadius = AddScalarParam(Material, TEXT("PlanetRadius"), 75000.0f, -800, -400);
	UMaterialExpressionScalarParameter* ShellRadius = AddScalarParam(Material, TEXT("ShellRadius"), 78750.0f, -800, -300);
	UMaterialExpressionScalarParameter* RayleighScaleHeight = AddScalarParam(Material, TEXT("RayleighScaleHeight"), 1181.25f, -800, -200);
	UMaterialExpressionScalarParameter* MieScaleHeight = AddScalarParam(Material, TEXT("MieScaleHeight"), 236.25f, -800, -100);

	// 2.24169e-4 = 0.2648 / 1181.25（地球瑞利垂直光学厚度 / 壳标高）
	UMaterialExpressionVectorParameter* RayleighBeta = AddVectorParam(Material, TEXT("RayleighBeta"),
		FLinearColor(3.92938e-5f, 9.18247e-5f, 2.24169e-4f, 1.0f), -800, 0);
	// 2.25524e-5 = 0.005328 / 236.25
	UMaterialExpressionVectorParameter* MieBeta = AddVectorParam(Material, TEXT("MieBeta"),
		FLinearColor(2.25524e-5f, 2.25524e-5f, 2.25524e-5f, 1.0f), -800, 100);

	UMaterialExpressionVectorParameter* CameraPos = AddVectorParam(Material, TEXT("CameraPos"),
		FLinearColor::Black, -800, 200);
	UMaterialExpressionVectorParameter* PlanetCenter = AddVectorParam(Material, TEXT("PlanetCenter"),
		FLinearColor::Black, -800, 300);
	UMaterialExpressionVectorParameter* SunDirection = AddVectorParam(Material, TEXT("SunDirection"),
		FLinearColor(1.0f, 0.0f, 0.0f, 1.0f), -800, 400);
	UMaterialExpressionVectorParameter* SunIntensity = AddVectorParam(Material, TEXT("SunIntensity"),
		FLinearColor(1.0f, 1.0f, 1.0f, 1.0f), -800, 500);

	// --- Custom 节点 ---
	UMaterialExpressionCustom* Custom = NewObject<UMaterialExpressionCustom>(Material);
	Custom->Material = Material;
	Custom->Description = TEXT("PlanetAtmosphereSingleScatter");
	Custom->Code = AtmosphereHLSL;
	Custom->OutputType = CMOT_Float4;
	Custom->MaterialExpressionEditorX = -300;
	Custom->MaterialExpressionEditorY = 0;
	Material->GetExpressionCollection().AddExpression(Custom);

	// 名字必须与 HLSL 里的引用一致；顺序只是可读性。
	AddCustomInput(Custom, TEXT("WorldPos"), WorldPos);
	AddCustomInput(Custom, TEXT("CameraPos"), CameraPos);
	AddCustomInput(Custom, TEXT("PlanetRadius"), PlanetRadius);
	AddCustomInput(Custom, TEXT("ShellRadius"), ShellRadius);
	AddCustomInput(Custom, TEXT("PlanetCenter"), PlanetCenter);
	AddCustomInput(Custom, TEXT("SunDirection"), SunDirection);
	AddCustomInput(Custom, TEXT("RayleighScaleHeight"), RayleighScaleHeight);
	AddCustomInput(Custom, TEXT("MieScaleHeight"), MieScaleHeight);
	AddCustomInput(Custom, TEXT("RayleighBeta"), RayleighBeta);
	AddCustomInput(Custom, TEXT("MieBeta"), MieBeta);
	AddCustomInput(Custom, TEXT("SunIntensity"), SunIntensity);

	Custom->RebuildOutputs();

	// --- 拆 float4 输出：RGB → Emissive，A → Opacity ---
	UMaterialExpressionComponentMask* MaskRGB = NewObject<UMaterialExpressionComponentMask>(Material);
	MaskRGB->Material = Material;
	MaskRGB->Input.Expression = Custom;
	MaskRGB->Input.OutputIndex = 0;
	MaskRGB->R = 1; MaskRGB->G = 1; MaskRGB->B = 1; MaskRGB->A = 0;
	MaskRGB->MaterialExpressionEditorX = 0;
	MaskRGB->MaterialExpressionEditorY = -200;
	Material->GetExpressionCollection().AddExpression(MaskRGB);

	UMaterialExpressionComponentMask* MaskA = NewObject<UMaterialExpressionComponentMask>(Material);
	MaskA->Material = Material;
	MaskA->Input.Expression = Custom;
	MaskA->Input.OutputIndex = 0;
	MaskA->R = 0; MaskA->G = 0; MaskA->B = 0; MaskA->A = 1;
	MaskA->MaterialExpressionEditorX = 0;
	MaskA->MaterialExpressionEditorY = 200;
	Material->GetExpressionCollection().AddExpression(MaskA);

	// --- 材质本体 ---
	Material->BlendMode = BLEND_Translucent;
	Material->SetShadingModel(MSM_Unlit);
	Material->TwoSided = false;             // 球内看不见壳，交给引擎大气
	Material->bCastDynamicShadow = false;
	Material->bCastStaticShadow = false;

	UMaterialEditorOnlyData* EditorData = Material->GetEditorOnlyData();
	if (!EditorData)
	{
		OutError = TEXT("GetEditorOnlyData() 返回空");
		return false;
	}
	EditorData->EmissiveColor.Expression = MaskRGB;
	EditorData->EmissiveColor.OutputIndex = 0;
	EditorData->Opacity.Expression = MaskA;
	EditorData->Opacity.OutputIndex = 0;

	Material->PostEditChange();
	Package->MarkPackageDirty();

	const FString PackageFileName = FPackageName::LongPackageNameToFilename(
		AtmosphereAssetPath, FPackageName::GetAssetPackageExtension());

	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	SaveArgs.SaveFlags = SAVE_NoError;
	if (!UPackage::SavePackage(Package, Material, *PackageFileName, SaveArgs))
	{
		OutError = FString::Printf(TEXT("SavePackage 失败：%s"), *PackageFileName);
		return false;
	}

	return true;
}

FAutoConsoleCommandWithWorldAndArgs GBuildAtmosphereMaterialCommand(
	TEXT("StellarEra.BuildAtmosphereMaterial"),
	TEXT("重建 /Game/Materials/M_PlanetAtmosphere。改完 HLSL 字符串后跑一次。"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda(
		[](const TArray<FString>& Args, UWorld* World)
		{
			FString Error;
			if (BuildPlanetAtmosphereMaterial(Error))
			{
				UE_LOG(LogTemp, Display, TEXT("[大气] 材质已生成：%s"), AtmosphereAssetPath);
			}
			else
			{
				UE_LOG(LogTemp, Error, TEXT("[大气] 材质生成失败：%s"), *Error);
			}
		}));

} // namespace

#endif // WITH_EDITOR
```

- [ ] **Step 2: 构建**

关闭编辑器，然后：

```bash
powershell -NoProfile -ExecutionPolicy Bypass -File "$TEMP/ue_mcp/ue_build.ps1"
```

预期：`Result: Succeeded`，且**没有**新增的 `StellarEraPlanet.Build.cs` 依赖 —— 本任务不需要 `UnrealEd`（`UPackage::SavePackage` 在 CoreUObject，`UMaterial` 系列在 Engine）。

- [ ] **Step 3: 生成材质**

打开编辑器，加载 `UniverseMap`，在 Output Log 的控制台输入框（或 `~` 控制台）执行：

```
StellarEra.BuildAtmosphereMaterial
```

预期：日志出现 `[大气] 材质已生成：/Game/Materials/M_PlanetAtmosphere`。

- [ ] **Step 4: 确认材质真的能编译**

在 Content Browser 里打开 `Content/Materials/M_PlanetAtmosphere`。

预期：
- 图上有 1 个 Custom 节点、2 个 ComponentMask、1 个 Absolute World Position、4 个 ScalarParameter、6 个 VectorParameter。
- 右上角没有 "Error" 红框，Output Log 里**没有** `LogMaterial: Warning/Error`。
- 预览球显示为**几乎全黑**（这是对的：`CameraPos` 与 `PlanetCenter` 还是默认的 `(0,0,0)`，相机和行星中心重合，射线退化，散射为 0）。**不要**为了让它好看去调默认值——真正的值由 Task 3 逐帧推。

如果 Output Log 报 HLSL 编译错误，照抄报错信息定位；常见的两个：
- `'WorldPos' : undeclared identifier` → `AddCustomInput` 的名字与 HLSL 里的不一致。
- 函数末尾缺 return → 说明 `Code` 里出现了 `return` 但末尾没有（UE 检测到 `return` 就不再自动包一层）。

- [ ] **Step 5: 提交**

```bash
git add Source/StellarEraPlanet/Private/PlanetAtmosphereMaterialBuilder.cpp \
        Content/Materials/M_PlanetAtmosphere.uasset
git commit -m "$(cat <<'EOF'
feat(大气): C++ 生成壳材质 M_PlanetAtmosphere

HLSL 作为仓库内的字符串，材质资产自带全部源码，零外部路径依赖
（不用 IncludeFilePaths —— 那需要一套 shader 目录映射）。

Custom 节点输出单个 float4，再用两个 ComponentMask 拆成
RGB→Emissive、A→Opacity，避免用 AdditionalOutputs。

坐标系已从引擎源码钉死：WorldPosition 节点 WPT_Default 的输出就是
绝对世界坐标（节点标题字面为 "Absolute World Position"），与我们在
C++ 里写进 CameraPos / PlanetCenter 向量参数的空间一致。

不需要 UnrealEd 依赖：UPackage::SavePackage 在 CoreUObject。
（规格 §5.5 说需要，是错的。）

Co-Authored-By: Claude Code <noreply@anthropic.com>
EOF
)" -- Source/StellarEraPlanet/Private/PlanetAtmosphereMaterialBuilder.cpp \
       Content/Materials/M_PlanetAtmosphere.uasset
```

---

### Task 3: 壳组件 + 挂到每颗行星

每颗行星在构造时自动获得一个壳，不需要手工摆放。

**Files:**
- Create: `Source/StellarEraPlanet/Public/PlanetAtmosphereComponent.h`
- Create: `Source/StellarEraPlanet/Private/PlanetAtmosphereComponent.cpp`
- Modify: `Source/StellarEraPlanet/Public/VoxelPlanetActor.h`
- Modify: `Source/StellarEraPlanet/Private/VoxelPlanetActor.cpp`

**Interfaces:**
- Consumes: `PlanetAtmosphere::ComputeShellRadiusCm` / `ComputeShellScatteringParams`（Task 1）；材质 `/Game/Materials/M_PlanetAtmosphere` 及其参数名（Task 2）；`AVoxelPlanetActor::GetSurfaceRadiusCm()`（已存在）
- Produces: `UPlanetAtmosphereComponent`，公开 `float AtmosphereHeightRatio`、`float RayleighScaleHeightRatio`、`float MieScaleHeightRatio`、`FLinearColor SunIntensity`、`bool bAtmosphereEnabled`、`void RefreshAtmosphereGeometry()`；`AVoxelPlanetActor::GetAtmosphereComponent()`

- [ ] **Step 1: 写组件**

创建 `Source/StellarEraPlanet/Public/PlanetAtmosphereComponent.h`（**UTF-8 with BOM**）：

```cpp
#pragma once

#include "CoreMinimal.h"
#include "Components/StaticMeshComponent.h"
#include "PlanetAtmosphereComponent.generated.h"

class ADirectionalLight;

/**
 * 行星大气壳。
 *
 * 一个套在行星外的半透明球，像素着色器里做单次散射 ray march。
 * 球面法线朝外，所以：
 *   - 相机在球外（太空）→ 看到正面 → 壳渲染，这就是"大气辉光"
 *   - 相机在球内（行星表面）→ 看到背面 → 被背面剔除 → 壳不渲染
 * 球内的天空交给引擎原生 SkyAtmosphere（由 UCosmosSimulationSubsystem 重定位），
 * 两者天然不打架，不需要手工切可见性。
 *
 * 由 AVoxelPlanetActor 在构造时创建并挂到根组件上，每颗行星自动获得一个。
 */
UCLASS(ClassGroup = (StellarEra), meta = (BlueprintSpawnableComponent))
class STELLARERAPLANET_API UPlanetAtmosphereComponent : public UStaticMeshComponent
{
	GENERATED_BODY()

public:
	UPlanetAtmosphereComponent();

	/** 壳厚度 / 行星表面半径。**必须**与引擎大气用的值一致，否则穿过壳边界会跳变。 */
	UPROPERTY(EditAnywhere, Category = "StellarEra|Atmosphere", meta = (ClampMin = "0.0"))
	float AtmosphereHeightRatio = 0.05f;

	/** 瑞利标高 / 壳半径。 */
	UPROPERTY(EditAnywhere, Category = "StellarEra|Atmosphere", meta = (ClampMin = "0.0"))
	float RayleighScaleHeightRatio = 0.015f;

	/** 米氏标高 / 壳半径。 */
	UPROPERTY(EditAnywhere, Category = "StellarEra|Atmosphere", meta = (ClampMin = "0.0"))
	float MieScaleHeightRatio = 0.003f;

	/** 太阳辐照度的线性 RGB 倍率。调到与地形被同一盏 DirectionalLight 照出来的亮度相称。 */
	UPROPERTY(EditAnywhere, Category = "StellarEra|Atmosphere")
	FLinearColor SunIntensity = FLinearColor::White;

	/** 关掉即不渲染壳，用于 stat unit 对比。 */
	UPROPERTY(EditAnywhere, Category = "StellarEra|Atmosphere")
	bool bAtmosphereEnabled = true;

	/** 运行时改半径后调用，重算缩放与材质参数。 */
	UFUNCTION(BlueprintCallable, Category = "StellarEra|Atmosphere")
	void RefreshAtmosphereGeometry();

protected:
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;

private:
	/** 解析太阳方向：取关卡里第一盏 ADirectionalLight 的朝向取反。找不到就保持上一次的值。 */
	FVector ResolveSunDirection();

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> AtmosphereMID;

	UPROPERTY(Transient)
	TObjectPtr<ADirectionalLight> CachedSunLight;

	/** 每帧推给材质的两个世界坐标，只在变化时写。 */
	FVector LastCameraPos = FVector(UE_BIG_NUMBER);
	FVector LastSunDirection = FVector(UE_BIG_NUMBER);
	bool bLoggedMissingSun = false;
};
```

创建 `Source/StellarEraPlanet/Private/PlanetAtmosphereComponent.cpp`（**UTF-8 with BOM**）：

```cpp
#include "PlanetAtmosphereComponent.h"

#include "Engine/DirectionalLight.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerCameraManager.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "PlanetAtmosphereParams.h"
#include "UObject/ConstructorHelpers.h"
#include "VoxelPlanetActor.h"

namespace
{
	/** /Engine/BasicShapes/Sphere 的半径是 50 cm。 */
	constexpr float BasicSphereRadiusCm = 50.0f;

	const TCHAR* const AtmosphereMaterialPath = TEXT("/Game/Materials/M_PlanetAtmosphere");

	// 材质参数名。必须与 PlanetAtmosphereMaterialBuilder.cpp 里的一致。
	const FName ParamPlanetRadius(TEXT("PlanetRadius"));
	const FName ParamShellRadius(TEXT("ShellRadius"));
	const FName ParamRayleighScaleHeight(TEXT("RayleighScaleHeight"));
	const FName ParamMieScaleHeight(TEXT("MieScaleHeight"));
	const FName ParamRayleighBeta(TEXT("RayleighBeta"));
	const FName ParamMieBeta(TEXT("MieBeta"));
	const FName ParamCameraPos(TEXT("CameraPos"));
	const FName ParamPlanetCenter(TEXT("PlanetCenter"));
	const FName ParamSunDirection(TEXT("SunDirection"));
	const FName ParamSunIntensity(TEXT("SunIntensity"));
}

UPlanetAtmosphereComponent::UPlanetAtmosphereComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;

	// 壳纯装饰：不参与碰撞、不投影、不接收贴花。
	SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SetGenerateOverlapEvents(false);
	SetCastShadow(false);
	bReceivesDecals = false;

	// 负值让壳排在其他半透明之前画。
	SetTranslucencySortPriority(-1);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereMesh(
		TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	if (SphereMesh.Succeeded())
	{
		SetStaticMesh(SphereMesh.Object);
	}
}

void UPlanetAtmosphereComponent::BeginPlay()
{
	Super::BeginPlay();

	if (!bAtmosphereEnabled)
	{
		SetVisibility(false);
		return;
	}

	UMaterialInterface* BaseMaterial = LoadObject<UMaterialInterface>(nullptr, AtmosphereMaterialPath);
	if (!BaseMaterial)
	{
		// 材质是 Task 2 用控制台命令生成的资产。没生成过就在这里报一次，不崩。
		UE_LOG(LogTemp, Warning,
			TEXT("[大气] 找不到 %s —— 在编辑器里跑一次 StellarEra.BuildAtmosphereMaterial。壳将不可见。"),
			AtmosphereMaterialPath);
		SetVisibility(false);
		return;
	}

	AtmosphereMID = CreateDynamicMaterialInstance(0, BaseMaterial);
	if (!AtmosphereMID)
	{
		UE_LOG(LogTemp, Warning, TEXT("[大气] CreateDynamicMaterialInstance 失败，壳将不可见。"));
		SetVisibility(false);
		return;
	}

	RefreshAtmosphereGeometry();
}

void UPlanetAtmosphereComponent::RefreshAtmosphereGeometry()
{
	if (!bAtmosphereEnabled || !AtmosphereMID)
	{
		return;
	}

	const AVoxelPlanetActor* Planet = Cast<AVoxelPlanetActor>(GetOwner());
	if (!Planet)
	{
		UE_LOG(LogTemp, Warning, TEXT("[大气] 壳组件的 Owner 不是 AVoxelPlanetActor，跳过。"));
		SetVisibility(false);
		return;
	}

	const float SurfaceRadiusCm = Planet->GetSurfaceRadiusCm();
	if (SurfaceRadiusCm <= 0.0f)
	{
		SetVisibility(false);
		return;
	}

	const float ShellRadiusCm = PlanetAtmosphere::ComputeShellRadiusCm(SurfaceRadiusCm, AtmosphereHeightRatio);
	const PlanetAtmosphere::FShellScatteringParams Scattering =
		PlanetAtmosphere::ComputeShellScatteringParams(
			ShellRadiusCm, RayleighScaleHeightRatio, MieScaleHeightRatio);

	// 网格是半径 50 cm 的球。用 SetWorldScale3D 而不是相对缩放：
	// 行星 actor 自己可能带缩放，相对缩放会把它叠进来。
	SetWorldScale3D(FVector(ShellRadiusCm / BasicSphereRadiusCm));

	AtmosphereMID->SetScalarParameterValue(ParamPlanetRadius, SurfaceRadiusCm);
	AtmosphereMID->SetScalarParameterValue(ParamShellRadius, ShellRadiusCm);
	AtmosphereMID->SetScalarParameterValue(ParamRayleighScaleHeight, Scattering.RayleighScaleHeightCm);
	AtmosphereMID->SetScalarParameterValue(ParamMieScaleHeight, Scattering.MieScaleHeightCm);
	AtmosphereMID->SetVectorParameterValue(ParamRayleighBeta,
		FLinearColor(Scattering.RayleighBeta.X, Scattering.RayleighBeta.Y, Scattering.RayleighBeta.Z, 1.0f));
	AtmosphereMID->SetVectorParameterValue(ParamMieBeta,
		FLinearColor(Scattering.MieBeta.X, Scattering.MieBeta.Y, Scattering.MieBeta.Z, 1.0f));
	AtmosphereMID->SetVectorParameterValue(ParamSunIntensity, SunIntensity);

	SetVisibility(true);
}

FVector UPlanetAtmosphereComponent::ResolveSunDirection()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return FVector::ZeroVector;
	}

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

	if (!CachedSunLight)
	{
		if (!bLoggedMissingSun)
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[大气] 关卡里没有 ADirectionalLight，壳的太阳方向无从确定，明暗分界会停在初值。"));
			bLoggedMissingSun = true;
		}
		return FVector::ZeroVector;
	}

	// DirectionalLight 的 forward 是光**传播**的方向（天上→地上）。
	// 从行星指向太阳是它的反向。
	return -CachedSunLight->GetActorForwardVector();
}

void UPlanetAtmosphereComponent::TickComponent(float DeltaTime, ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!bAtmosphereEnabled || !AtmosphereMID)
	{
		return;
	}

	// 行星中心就是组件自己的世界位置（相对位置是零）。
	const FVector PlanetCenter = GetComponentLocation();
	AtmosphereMID->SetVectorParameterValue(ParamPlanetCenter,
		FLinearColor(PlanetCenter.X, PlanetCenter.Y, PlanetCenter.Z, 1.0f));

	// 相机位置。星图模式下 Possess 的是 AStarMapCameraPawn，GetCameraLocation 一样能取到。
	FVector CameraPos = LastCameraPos;
	if (const UWorld* World = GetWorld())
	{
		if (const APlayerController* PC = World->GetFirstPlayerController())
		{
			if (const APlayerCameraManager* CamMgr = PC->PlayerCameraManager)
			{
				CameraPos = CamMgr->GetCameraLocation();
			}
		}
	}
	if (!CameraPos.Equals(LastCameraPos, 1.0f))
	{
		LastCameraPos = CameraPos;
		AtmosphereMID->SetVectorParameterValue(ParamCameraPos,
			FLinearColor(CameraPos.X, CameraPos.Y, CameraPos.Z, 1.0f));
	}

	const FVector SunDirection = ResolveSunDirection();
	if (!SunDirection.IsNearlyZero() && !SunDirection.Equals(LastSunDirection, 1e-4f))
	{
		LastSunDirection = SunDirection;
		AtmosphereMID->SetVectorParameterValue(ParamSunDirection,
			FLinearColor(SunDirection.X, SunDirection.Y, SunDirection.Z, 0.0f));
	}
}
```

- [ ] **Step 2: 挂到行星上**

修改 `Source/StellarEraPlanet/Public/VoxelPlanetActor.h`：在文件顶部加前置声明：

```cpp
class UPlanetAtmosphereComponent;
```

在 `class` 声明体内（跟 `LODInvoker` 同一段可见性）加：

```cpp
	/** 大气壳。构造时自动创建，每颗行星一个。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "StellarEra|Atmosphere")
	TObjectPtr<UPlanetAtmosphereComponent> AtmosphereComponent;

	UFUNCTION(BlueprintPure, Category = "StellarEra|Atmosphere")
	UPlanetAtmosphereComponent* GetAtmosphereComponent() const { return AtmosphereComponent; }
```

修改 `Source/StellarEraPlanet/Private/VoxelPlanetActor.cpp`：在文件顶部加 `#include "PlanetAtmosphereComponent.h"`，并在构造函数里 `LODInvoker` 那段之后加：

```cpp
	// 大气壳。挂到根组件上，相对位置为零 —— 球心与行星球心重合。
	// 材质与缩放都在 BeginPlay 里按 GetSurfaceRadiusCm() 定，构造期拿不到最终半径。
	AtmosphereComponent = CreateDefaultSubobject<UPlanetAtmosphereComponent>(TEXT("PlanetAtmosphere"));
	AtmosphereComponent->SetupAttachment(RootComponent);
```

- [ ] **Step 3: 构建**

关闭编辑器，然后：

```bash
powershell -NoProfile -ExecutionPolicy Bypass -File "$TEMP/ue_mcp/ue_build.ps1"
```

预期：`Result: Succeeded`。

- [ ] **Step 4: 在 PIE 里看**

打开编辑器 → 加载 `UniverseMap` → PIE。

预期：
1. **能看见至少一颗行星边缘有一圈辉光**（蓝白偏亮，朝向太阳那侧更亮）。
2. 把相机拉远到能同时看见多颗行星：**每颗都有**。
3. 飞到行星表面：辉光消失（相机进了壳内，背面剔除），头顶的天空由 Task 4 的引擎大气负责 —— 这一步可能还是全黑，Task 4 没做之前是正常的。
4. Output Log 里**没有** `[大气] 找不到 ...` 警告，**没有** shader 编译错误。

**如果辉光完全看不见**，按这个顺序查（每一步都机械可判，别猜）：
- `LogTemp` 有没有 `[大气] 找不到 /Game/Materials/...` → 材质没生成，回 Task 2 Step 3。
- `[大气] 关卡里没有 ADirectionalLight` → 关卡里没有平行光，`SunDirection` 停在 `(1,0,0)`。确认关卡里有 DirectionalLight（Viewport 切 Lit 模式地形有光照即证明）。
- 都没有，但屏幕还是黑的 → 材质参数没推到。在 `RefreshAtmosphereGeometry()` 末尾临时加一行
  `UE_LOG(LogTemp, Display, TEXT("[大气] R=%.0f Shell=%.0f BetaB=%.3e"), SurfaceRadiusCm, ShellRadiusCm, Scattering.RayleighBeta.Z);`
  确认半径量级是 `1e4~1e5` cm 而不是 `1e2` 或 `1e7`。

**如果辉光是个方块或者形状不对**，那是坐标系问题。用这个办法一次判定：把材质的 `EmissiveColor` 临时改接成 `WorldPos - PlanetCenter`（在材质编辑器里手工连一个 Subtract 到一个 ComponentMask），如果壳表面显示为**沿球面平滑变化**的颜色，说明三个值同空间；如果显示为**一片死色或噪声**，说明空间不一致。查完记得撤回手工改动并重新跑一次 `StellarEra.BuildAtmosphereMaterial`。

- [ ] **Step 5: 提交**

```bash
git add Source/StellarEraPlanet/Public/PlanetAtmosphereComponent.h \
        Source/StellarEraPlanet/Private/PlanetAtmosphereComponent.cpp \
        Source/StellarEraPlanet/Public/VoxelPlanetActor.h \
        Source/StellarEraPlanet/Private/VoxelPlanetActor.cpp
git commit -m "$(cat <<'EOF'
feat(大气): 每颗行星一个大壳组件

UStaticMeshComponent 子类，套 /Engine/BasicShapes/Sphere（半径 50 cm），
缩放 = 壳半径cm / 50。由 AVoxelPlanetActor 构造函数创建并挂到根组件，
每颗行星自动获得一个，不需要手工摆放。

球面法线朝外 → 相机在球外看到正面（辉光），在球内看到背面被剔除（不渲染）。
球内的天空交给引擎大气，两者天然不打架，不需要手工切可见性。

太阳方向取关卡里第一盏 ADirectionalLight 的 forward 取反 ——
与照亮地形的是同一盏灯，所以壳的明暗分界和地形的明暗分界天然一致。

Co-Authored-By: Claude Code <noreply@anthropic.com>
EOF
)" -- Source/StellarEraPlanet/Public/PlanetAtmosphereComponent.h \
       Source/StellarEraPlanet/Private/PlanetAtmosphereComponent.cpp \
       Source/StellarEraPlanet/Public/VoxelPlanetActor.h \
       Source/StellarEraPlanet/Private/VoxelPlanetActor.cpp
```

---

### Task 4: 引擎大气重定位

场景里唯一的 `ASkyAtmosphere` 逐帧搬到离相机最近的行星，按该行星半径缩放参数。这是"站在行星上"那一半。

**Files:**
- Create: `Source/StellarEra/Public/PlanetAtmosphereTargeting.h`
- Create: `Source/StellarEra/Private/PlanetAtmosphereTargeting.cpp`
- Create: `Source/StellarEra/Private/PlanetAtmosphereTargetingTests.cpp`
- Modify: `Source/StellarEra/Public/CosmosSimulationSubsystem.h`
- Modify: `Source/StellarEra/Private/CosmosSimulationSubsystem.cpp`

**Interfaces:**
- Consumes: `PlanetAtmosphere::ComputeSkyAtmosphereScaleParams`（Task 1）；`AVoxelPlanetActor::GetSurfaceRadiusCm()`（已存在）；`UCosmosSimulationSubsystem::Tick`（已存在）
- Produces: `int32 PlanetAtmosphereTargeting::FindNearestPlanet(const TArray<FVector>& Centers, const TArray<float>& SurfaceRadiiCm, const FVector& CameraPos)`；`UCosmosSimulationSubsystem::AtmosphereHeightRatio` / `AtmosphereCoefficientScale` / `bSkyAtmosphereRetargetEnabled`（都是 `UPROPERTY(Config)`）

- [ ] **Step 1: 写失败的测试**

创建 `Source/StellarEra/Private/PlanetAtmosphereTargetingTests.cpp`（**UTF-8 with BOM**）：

```cpp
#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "PlanetAtmosphereTargeting.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraAtmosphereTargetingTest,
	"StellarEra.Atmosphere.Targeting",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FStellarEraAtmosphereTargetingTest::RunTest(const FString& Parameters)
{
	// 三颗行星：原点一颗半径 75000，X 轴 5e6 一颗，Y 轴 1e7 一颗
	TArray<FVector> Centers;
	Centers.Add(FVector(0.0, 0.0, 0.0));
	Centers.Add(FVector(5.0e6, 0.0, 0.0));
	Centers.Add(FVector(0.0, 1.0e7, 0.0));

	TArray<float> Radii;
	Radii.Add(75000.0f);
	Radii.Add(75000.0f);
	Radii.Add(75000.0f);

	// 相机在原点附近（第一颗的内部）
	TestEqual(TEXT("camera at origin picks planet 0"),
		PlanetAtmosphereTargeting::FindNearestPlanet(Centers, Radii, FVector(0.0, 0.0, 0.0)), 0);

	// 相机贴近第二颗的表面：到球心 5e6 + 80000，表面距离 80000
	TestEqual(TEXT("camera near planet 1 surface picks planet 1"),
		PlanetAtmosphereTargeting::FindNearestPlanet(Centers, Radii,
			FVector(5.0e6 + 80000.0, 0.0, 0.0)), 1);

	// 相机在第二颗球心负 X 侧也应该选第二颗 —— 不能只看坐标正负
	TestEqual(TEXT("camera on -X side of planet 1 still picks planet 1"),
		PlanetAtmosphereTargeting::FindNearestPlanet(Centers, Radii,
			FVector(5.0e6 - 90000.0, 0.0, 0.0)), 1);

	// 相机在两颗行星之间，更靠近第三颗
	TestEqual(TEXT("camera closer to planet 2 picks planet 2"),
		PlanetAtmosphereTargeting::FindNearestPlanet(Centers, Radii,
			FVector(0.0, 9.0e6, 0.0)), 2);

	// 用"表面距离"而不是"球心距离"：给第三颗一个巨大的半径，
	// 使它的表面比第二颗更近，尽管球心更远。
	Radii[2] = 4.0e6f;
	// 相机到第三颗球心 9e6，表面距离 5e6；到第二颗球心约 9.05e6，表面距离约 8.97e6
	TestEqual(TEXT("surface distance wins over center distance"),
		PlanetAtmosphereTargeting::FindNearestPlanet(Centers, Radii, FVector(0.0, 9.0e6, 0.0)), 2);

	// 空数组
	TestEqual(TEXT("empty centers returns INDEX_NONE"),
		PlanetAtmosphereTargeting::FindNearestPlanet(TArray<FVector>(), TArray<float>(), FVector::ZeroVector),
		INDEX_NONE);

	// 长度不匹配 —— 防御性返回
	TArray<FVector> OneCenter;
	OneCenter.Add(FVector::ZeroVector);
	TestEqual(TEXT("mismatched array sizes returns INDEX_NONE"),
		PlanetAtmosphereTargeting::FindNearestPlanet(OneCenter, TArray<float>(), FVector::ZeroVector),
		INDEX_NONE);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
```

- [ ] **Step 2: 跑测试，确认它编不过**

关闭编辑器，然后：

```bash
powershell -NoProfile -ExecutionPolicy Bypass -File "$TEMP/ue_mcp/ue_build.ps1"
```

预期：编译失败，报 `Cannot open include file: 'PlanetAtmosphereTargeting.h'`。

- [ ] **Step 3: 写实现**

创建 `Source/StellarEra/Public/PlanetAtmosphereTargeting.h`（**UTF-8 with BOM**）：

```cpp
#pragma once

#include "CoreMinimal.h"

/**
 * 引擎原生 SkyAtmosphere 全场景只能有一套（FScene 只有单个
 * FSkyAtmosphereRenderSceneInfo*），所以它逐帧被搬到"离相机最近"的那颗行星。
 *
 * "最近"用**表面距离** |相机 - 球心| - 半径，不是球心距离 ——
 * 否则大行星会把相机贴在自己表面时仍被判定为"更远"。
 */
namespace PlanetAtmosphereTargeting
{
	/**
	 * @param Centers        各行星的球心世界坐标
	 * @param SurfaceRadiiCm 各行星的表面半径（cm），与 Centers 一一对应
	 * @param CameraPos      相机世界坐标
	 * @return 表面距离最小的下标；数组为空或长度不匹配时返回 INDEX_NONE
	 */
	STELLARERA_API int32 FindNearestPlanet(const TArray<FVector>& Centers,
		const TArray<float>& SurfaceRadiiCm, const FVector& CameraPos);

} // namespace PlanetAtmosphereTargeting
```

创建 `Source/StellarEra/Private/PlanetAtmosphereTargeting.cpp`（**UTF-8 with BOM**）：

```cpp
#include "PlanetAtmosphereTargeting.h"

namespace PlanetAtmosphereTargeting
{

int32 FindNearestPlanet(const TArray<FVector>& Centers, const TArray<float>& SurfaceRadiiCm,
	const FVector& CameraPos)
{
	if (Centers.Num() == 0 || Centers.Num() != SurfaceRadiiCm.Num())
	{
		return INDEX_NONE;
	}

	int32 BestIndex = INDEX_NONE;
	double BestSurfaceDistance = 0.0;

	for (int32 Index = 0; Index < Centers.Num(); ++Index)
	{
		const double SurfaceDistance =
			FVector::Dist(CameraPos, Centers[Index]) - static_cast<double>(SurfaceRadiiCm[Index]);

		if (BestIndex == INDEX_NONE || SurfaceDistance < BestSurfaceDistance)
		{
			BestIndex = Index;
			BestSurfaceDistance = SurfaceDistance;
		}
	}

	return BestIndex;
}

} // namespace PlanetAtmosphereTargeting
```

`STELLARERA_API` 必须与 `StellarEra.Build.cs` 对应的模块宏一致；若报未定义就把这个宏去掉（同模块内使用不需要导出）。

- [ ] **Step 4: 构建并跑测试**

关闭编辑器 → 构建 → 跑测试：

```bash
powershell -NoProfile -ExecutionPolicy Bypass -File "$TEMP/ue_mcp/ue_build.ps1"
powershell -NoProfile -ExecutionPolicy Bypass -File "$TEMP/ue_mcp/run_nbody_test.ps1" -Filter "StellarEra.Atmosphere.Targeting"
```

预期：构建 `Result: Succeeded`；1 条测试 `StellarEra.Atmosphere.Targeting` 全 `Success`，里面有 7 条断言。

- [ ] **Step 5: 接进子系统**

修改 `Source/StellarEra/Public/CosmosSimulationSubsystem.h`：在 `private:` 段（跟 `RefreshPlanetContactRadii` 同一段）加：

```cpp
	/** 把场景里唯一的 ASkyAtmosphere 搬到离相机最近的行星，并按该行星半径缩放参数。 */
	void UpdateSkyAtmosphereRetarget();
```

在 `public:` 的配置项区域（跟其他 `UPROPERTY(Config)` 放一起）加：

```cpp
	/**
	 * 大气壳厚度 / 行星半径。**必须**与 UPlanetAtmosphereComponent::AtmosphereHeightRatio 一致，
	 * 否则玩家穿过壳边界时会看到跳变。
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Cosmos|Atmosphere", meta = (ClampMin = "0.0"))
	float AtmosphereHeightRatio = 0.05f;

	/**
	 * 引擎大气散射系数的额外倍率。初值 1.0 = 不做任何补偿。
	 *
	 * 几何缩放（长度 ×s、系数 ×1/s）已经让垂直光学厚度 τ = β·H 与地球相等，
	 * 所以观感上**不需要**预先补偿：厚度比地球大 5.3 倍并不会让 τ 变大，
	 * 因为 s 与 1/s 在 β·H 里正好抵消（实算 τ 之比 1.0006）。
	 * 这个值只用于观感微调：天空糊成一片白就往小调，太淡就往大调。
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Cosmos|Atmosphere", meta = (ClampMin = "0.0"))
	float AtmosphereCoefficientScale = 1.0f;

	/** 关掉即不重定位引擎大气（壳不受影响），用于对照实验。 */
	UPROPERTY(Config, EditAnywhere, Category = "Cosmos|Atmosphere")
	bool bSkyAtmosphereRetargetEnabled = true;
```

在 `private:` 段加缓存：

```cpp
	/** 上一次重定位到的行星。只有它变了才写参数，避免每帧抖动。 */
	TWeakObjectPtr<AActor> LastAtmosphereTargetPlanet;

	/** 找不到 ASkyAtmosphere 时只报一次。 */
	bool bLoggedMissingSkyAtmosphere = false;
```

修改 `Source/StellarEra/Private/CosmosSimulationSubsystem.cpp`：在文件顶部加 include（已有的不要重复加）：

```cpp
#include "Camera/PlayerCameraManager.h"
#include "Components/SkyAtmosphereComponent.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "PlanetAtmosphereParams.h"
#include "PlanetAtmosphereTargeting.h"
#include "VoxelPlanetActor.h"
```

在 `Tick()` 里，**紧跟** `RefreshPlanetContactRadii();` 之后、`ApplyControlledBodyForces(DeltaTime);` 之前，插入一行：

```cpp
	UpdateSkyAtmosphereRetarget();
```

在文件末尾加实现：

```cpp
void UCosmosSimulationSubsystem::UpdateSkyAtmosphereRetarget()
{
	if (!bSkyAtmosphereRetargetEnabled)
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// 相机位置。星图模式下 Possess 的是 AStarMapCameraPawn，一样能取到。
	const APlayerController* PC = World->GetFirstPlayerController();
	const APlayerCameraManager* CamMgr = PC ? PC->PlayerCameraManager : nullptr;
	if (!CamMgr)
	{
		return;
	}
	const FVector CameraPos = CamMgr->GetCameraLocation();

	// 收集所有行星
	TArray<FVector> Centers;
	TArray<float> SurfaceRadiiCm;
	TArray<AVoxelPlanetActor*> Planets;
	for (TActorIterator<AVoxelPlanetActor> It(World); It; ++It)
	{
		AVoxelPlanetActor* Planet = *It;
		const float RadiusCm = Planet->GetSurfaceRadiusCm();
		if (RadiusCm <= 0.0f)
		{
			continue;
		}
		Planets.Add(Planet);
		Centers.Add(Planet->GetActorLocation());
		SurfaceRadiiCm.Add(RadiusCm);
	}

	const int32 NearestIndex =
		PlanetAtmosphereTargeting::FindNearestPlanet(Centers, SurfaceRadiiCm, CameraPos);
	if (NearestIndex == INDEX_NONE)
	{
		// 一颗行星都没有：保持上一次的设置。
		// 绝对不能把 AtmosphereHeight 写成 0 —— 着色器里的 exp(-h / H) 会除零。
		return;
	}

	AVoxelPlanetActor* NearestPlanet = Planets[NearestIndex];

	// 只有目标真的换了才写，避免每帧抖动着改渲染状态。
	if (LastAtmosphereTargetPlanet.Get() == NearestPlanet)
	{
		return;
	}
	LastAtmosphereTargetPlanet = NearestPlanet;

	ASkyAtmosphere* SkyAtmosphere = nullptr;
	for (TActorIterator<ASkyAtmosphere> It(World); It; ++It)
	{
		SkyAtmosphere = *It;
		break;
	}
	if (!SkyAtmosphere)
	{
		if (!bLoggedMissingSkyAtmosphere)
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[大气] 关卡里没有 ASkyAtmosphere，行星表面不会有天空。"));
			bLoggedMissingSkyAtmosphere = true;
		}
		return;
	}

	USkyAtmosphereComponent* Atmosphere = SkyAtmosphere->GetComponent();
	if (!Atmosphere)
	{
		return;
	}

	const float SurfaceRadiusKm = NearestPlanet->GetSurfaceRadiusCm() / 100000.0f; // cm → km
	const PlanetAtmosphere::FSkyAtmosphereScaleParams ScaleParams =
		PlanetAtmosphere::ComputeSkyAtmosphereScaleParams(SurfaceRadiusKm, AtmosphereHeightRatio);
	if (ScaleParams.Scale <= 0.0f)
	{
		return;
	}

	SkyAtmosphere->SetActorLocation(NearestPlanet->GetActorLocation());
	Atmosphere->TransformMode = ESkyAtmosphereTransformMode::PlanetCenterAtComponentTransform;
	Atmosphere->BottomRadius = ScaleParams.BottomRadiusKm;
	Atmosphere->AtmosphereHeight = ScaleParams.AtmosphereHeightKm;
	Atmosphere->RayleighScatteringScale = ScaleParams.RayleighScatteringScale * AtmosphereCoefficientScale;
	Atmosphere->MieScatteringScale = ScaleParams.MieScatteringScale * AtmosphereCoefficientScale;
	Atmosphere->MieAbsorptionScale = ScaleParams.MieAbsorptionScale * AtmosphereCoefficientScale;
	Atmosphere->RayleighExponentialDistribution = ScaleParams.RayleighExponentialDistributionKm;
	Atmosphere->MieExponentialDistribution = ScaleParams.MieExponentialDistributionKm;

	UE_LOG(LogTemp, Display,
		TEXT("[大气] 引擎大气重定位到 %s：R=%.4f km，厚度=%.5f km，瑞利系数=%.2f，米氏系数=%.4f"),
		*NearestPlanet->GetName(), ScaleParams.BottomRadiusKm, ScaleParams.AtmosphereHeightKm,
		Atmosphere->RayleighScatteringScale, Atmosphere->MieScatteringScale);
}
```

`PlanetAtmosphereParams.h` 属于 `StellarEraPlanet` 模块。`StellarEra.Build.cs` 的 `PrivateDependencyModuleNames` 里已经有 `StellarEraPlanet`，所以在 `StellarEra` 的 `.cpp` 里 include 它的 Public 头是允许的。

- [ ] **Step 6: 构建**

关闭编辑器，然后：

```bash
powershell -NoProfile -ExecutionPolicy Bypass -File "$TEMP/ue_mcp/ue_build.ps1"
```

预期：`Result: Succeeded`。

- [ ] **Step 7: 在 PIE 里看**

打开编辑器 → 加载 `UniverseMap` → PIE。

预期：
1. 飞到某颗行星表面：**头顶有天空**，有地平线渐变，亮的一侧朝向太阳。
2. 地形近处清晰、远处发白（空中透视）。
3. 从行星 A 飞到行星 B：A 的壳在远处仍可见（Task 3 的成果），B 球内的天空正确。
4. Output Log 里每次换行星出现一条 `[大气] 引擎大气重定位到 ...`，**不是**每帧一条。
5. 无 ensure、无 `LogMaterial` 错误。

**如果头顶还是全黑**：这是规格 §7 风险 #1（引擎大气在 0.75 km 半径下不一定正常）真的发生了。先看那条 `[大气] 引擎大气重定位到 ...` 日志确认参数写进去了，再看 `R=0.7500 km，厚度=0.03750 km` 这个量级对不对。若参数对但没天空，退路是把壳改成双面（`Material->TwoSided = true` 并在材质里翻转内表面的法线），代价是失去 SkyLight 环境光采集与太阳盘 —— 那是一个独立的设计变更，**停下来跟用户确认**，不要自行改。

- [ ] **Step 8: 提交**

```bash
git add Source/StellarEra/Public/PlanetAtmosphereTargeting.h \
        Source/StellarEra/Private/PlanetAtmosphereTargeting.cpp \
        Source/StellarEra/Private/PlanetAtmosphereTargetingTests.cpp \
        Source/StellarEra/Public/CosmosSimulationSubsystem.h \
        Source/StellarEra/Private/CosmosSimulationSubsystem.cpp
git commit -m "$(cat <<'EOF'
feat(大气): 引擎大气逐帧重定位到最近的行星

场景里唯一的 ASkyAtmosphere 由 UCosmosSimulationSubsystem 每帧搬到
离相机最近的行星，按该行星半径做几何缩放（长度 ×s、系数 ×1/s）。

"最近"用表面距离 |相机-球心| - 半径，不是球心距离 —— 否则大行星会把
贴着它表面的相机仍判定为"更远"。这一段是纯函数，有 7 条自动化测试。

只在目标真的换行星时才写参数，避免每帧抖动改渲染状态。
找不到行星时保持上一次的设置 —— 绝不把 AtmosphereHeight 写成 0，
那会让着色器里的 exp(-h / H) 除零。

AtmosphereCoefficientScale 是配置项，初值 1.0 —— 几何缩放（长度 ×s、
系数 ×1/s）让 τ = β·H 与地球相等（实算之比 1.0006），观感上不需要
预先补偿。这个值只用于微调。

Co-Authored-By: Claude Code <noreply@anthropic.com>
EOF
)" -- Source/StellarEra/Public/PlanetAtmosphereTargeting.h \
       Source/StellarEra/Private/PlanetAtmosphereTargeting.cpp \
       Source/StellarEra/Private/PlanetAtmosphereTargetingTests.cpp \
       Source/StellarEra/Public/CosmosSimulationSubsystem.h \
       Source/StellarEra/Private/CosmosSimulationSubsystem.cpp
```

---

### Task 5: 调参 + 验收

把规格 §8 的 6 条验收标准逐条过一遍，把**实测到的**结果写回规格。这一步产出的不是代码，是一份"真的验过了"的记录 —— 以及一组调好的参数值。

**Files:**
- Modify: `docs/superpowers/specs/2026-10-18-planet-atmosphere-design.md`

**Interfaces:**
- Consumes: Task 1–4 的全部产出
- Produces: 规格里一组实测过的参数值

- [ ] **Step 1: 先把 §7 风险 #2 的帧时间量出来**

打开编辑器 → `UniverseMap` → PIE → 把相机拉到能同时看见三颗行星的位置 → 按 `~` 开控制台：

```
stat unit
```

记下 `Frame` / `Game` / `Draw` 三个数。然后选中任意一颗行星的 `PlanetAtmosphere` 组件（或三颗都选），把 `bAtmosphereEnabled` 取消勾选，再记一次。

**预期**：关掉后帧时间下降。把两次的数字都记下来（不预设阈值）。

- [ ] **Step 2: 调壳**

在 PIE 里选中壳组件逐项调，每调一项看一次画面：

| 参数 | 起调值 | 观感不对时的方向 |
|---|---|---|
| `AtmosphereHeightRatio` | 0.05 | 辉光太薄看不见 → 加大；太厚像雾球 → 减小。**改完必须同步改 `UCosmosSimulationSubsystem::AtmosphereHeightRatio`**（Config 项，改完重启编辑器） |
| `RayleighScaleHeightRatio` | 0.015 | 辉光贴着一层薄皮 → 加大；糊成一团 → 减小 |
| `MieScaleHeightRatio` | 0.003 | 太阳侧的白色前向峰太窄/太宽 |
| `SunIntensity` | (1,1,1) | 辉光比地形暗 → 加大；过曝发白 → 减小 |

调好后把值写回 `UPlanetAtmosphereComponent` 的成员初值（否则只在本次 PIE 生效）。

- [ ] **Step 3: 调引擎大气**

如果行星表面的天空糊成一片白，改 `Config/DefaultGame.ini`：

```ini
[/Script/StellarEra.CosmosSimulationSubsystem]
AtmosphereHeightRatio=0.05
AtmosphereCoefficientScale=1.0
```

`1.0` 是**算出来的**：几何缩放让 τ 与地球相等，不需要补偿。只有实测发现天空
确实过曝时才往下调（每次减半试），够蓝就停。改完重启编辑器。

- [ ] **Step 4: 逐条过验收标准**

对着规格 §8 的 6 条，一条一条在 PIE 里确认，**把每条的实测结果**（看到什么、参数是多少）记下来。任何一条不通过就回到对应任务修，不要在这一步"绕过去"。

特别地，第 4 条"时间推进导致太阳方向变化时，壳上的明暗分界跟着转"：本项目里太阳方向来自关卡的静态 `ADirectionalLight`，**世界空间里不随时间变**。真正会变的是**行星绕恒星公转导致行星表面相对太阳朝向变化**。所以这条的验证方式是：开时间加速，看行星公转一圈的过程中明暗分界确实扫过行星表面。如果公转太快看不清，把 `UCosmosSimulationSubsystem` 的时间配置临时调慢。

如果实测下来"明暗分界不跟着转"，那就是真的没实现 —— 因为方向源是静态灯。此时**停下来跟用户确认**是要把方向源换成 n-body 里恒星的位置，那是一个独立的设计变更。

- [ ] **Step 5: 把实测结果与调好的值写回规格**

修改 `docs/superpowers/specs/2026-10-18-planet-atmosphere-design.md`：

1. §8 的 6 条，每条后面加一行「**实测（2026-10-18）**：<观察到的现象 + 相关参数值>」。
2. §5.2 的参数表，"初值来源"栏换成 Step 2 调好的实际值。
3. §5.5 删掉「`StellarEraPlanet.Build.cs` 需要加 `UnrealEd`，包在 `if (Target.bBuildEditor)` 里。这是本仓库第一次引入 `UnrealEd` 依赖，只影响编辑器构建。」整段，换成「不需要新依赖：`UPackage::SavePackage` 在 CoreUObject，`UMaterial` 系列在 Engine。」
4. §5.3 的 `g = 0.76` 改成 `g = 0.8`，并注明「与引擎 `MieAnisotropy` 默认值一致」。
5. §6 末尾补一段，说明壳的散射系数怎么来的（垂直光学厚度对齐地球，见计划「与规格的偏差」第 2 条）。

- [ ] **Step 6: 提交**

```bash
git add docs/superpowers/specs/2026-10-18-planet-atmosphere-design.md
git commit -m "$(cat <<'EOF'
docs(大气): 验收标准逐条补上实测结果

规格 §8 的 6 条从"要满足什么"变成"实测到了什么"，参数表里的
初值换成 PIE 里调好的值。另修正三处写错的地方：

- §5.5 说不加 UnrealEd 就编不过 —— 不需要，UPackage::SavePackage
  在 CoreUObject
- §5.2 与 §6 互相指认，壳的散射系数没有初值 —— 补上来源
  （垂直光学厚度对齐地球）
- §5.3 的米氏 g 从 0.76 改成 0.8，与引擎 MieAnisotropy 默认值一致

Co-Authored-By: Claude Code <noreply@anthropic.com>
EOF
)" -- docs/superpowers/specs/2026-10-18-planet-atmosphere-design.md
```

---

## 自检记录

写完计划后对着规格逐节核了一遍。

**规格覆盖**：

| 规格节 | 落到哪个任务 |
|---|---|
| §3.1 一个 world 一套大气 | 架构前提，Task 4 的 `UpdateSkyAtmosphereRetarget` 就是它的直接后果 |
| §3.2 材质里拿不到 LUT | Task 2 的 HLSL 自带散射数学 |
| §3.3 不用 `IncludeFilePaths` | Task 2，`Code` 是自包含字符串，`IncludeFilePaths` 一个字没写 |
| §3.4 球内天然不可见 | Task 3，`TwoSided = false` + 球面法线朝外 |
| §3.5 `GetSurfaceRadiusCm()` | Task 3 Step 2、Task 4 Step 5 都用它 |
| §5.1 壳组件 | Task 3 |
| §5.2 材质参数表 | Task 2 建节点、Task 3 按名字设置 |
| §5.3 单次散射 HLSL | Task 2 的 `AtmosphereHLSL` |
| §5.4 引擎大气重定位 | Task 4 |
| §5.5 材质生成器 | Task 2（`UnrealEd` 那处已在「偏差」第 1 条修正） |
| §6 参数从哪来 | Task 1 的三条纯函数（壳散射系数的空洞已在「偏差」第 2 条补全） |
| §7 风险表 | 风险 #1 → Task 4 Step 7 的退路；#2 → Task 5 Step 1；#3 → Task 4 的"只在换行星时写"；#4 → Task 2 的混合设置 + Task 3 Step 4 的排查；#5 → 材质随仓库提交 |
| §8 验收标准 | Task 5 Step 4 |
| §9 测试方式 | 每个任务的构建/测试命令 |

**没有覆盖到的**：§7 风险 #3 说的"必要时做平滑过渡"没做 —— 那是"必要时"，本次不预设。

**占位符扫描**：无 TBD / TODO / "类似 Task N"。所有 HLSL、所有 C++、所有测试断言、所有命令都是完整的。

**类型一致性**：跨任务的名字对过一遍 ——
- `PlanetAtmosphere::ComputeShellRadiusCm` / `ComputeShellScatteringParams` / `ComputeSkyAtmosphereScaleParams`：Task 1 定义，Task 3 / Task 4 使用，签名一致。
- `FShellScatteringParams::RayleighBeta` / `MieBeta` / `RayleighScaleHeightCm` / `MieScaleHeightCm`：Task 1 定义，Task 3 使用。
- `FSkyAtmosphereScaleParams` 的 8 个成员：Task 1 定义，Task 4 逐字段使用，无拼写漂移。
- 材质参数名 10 个：Task 2 的 `AddScalarParam`/`AddVectorParam` 字面量与 Task 3 的 `ParamXxx` 常量逐一对齐。
- `AVoxelPlanetActor::GetAtmosphereComponent()`：Task 3 定义，本计划内无人使用（供将来与调试用）。
- `PlanetAtmosphereTargeting::FindNearestPlanet`：Task 4 Step 1/3 定义，Step 5 使用。

**发现并修掉的一处**：Task 4 原本打算让子系统把太阳方向推给壳，与 Task 3 里"组件自己从 `ADirectionalLight` 解析"重复且可能不一致。改成**只有组件解析**，子系统不碰 —— 壳的明暗分界与地形光照由此天然同源。
