# 行星天光（自建大气版 SkyLight）实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 给自建大气壳配一个天光：按玩家视角在 CPU 上积分出一张低分辨率天空立方图，喂给关卡里现成的 `USkyLightComponent`，让环境光随昼夜与日色变化。

**Architecture:** 纯 C++ 单次散射积分器（`PlanetSky` 命名空间，可单测）算出 `6 × 16²` 线性 HDR 辐亮度 → 每次更新新建一张 `UTextureCube`（`PF_FloatRGBA`）→ `USkyLightComponent::SetCubemap`。组件 `UPlanetSkyLightComponent` 是 `AVoxelPlanetActor` 的 CDO 子对象，自己找同 Actor 的大气壳与关卡里的 SkyLight，按世界空间变化阈值节流到 5 Hz。

**Tech Stack:** UE 5.8.3 / C++20 / `StellarEraPlanet` 模块 / 自动化测试 `StellarEra.Planet.SkyLight.*`

**Spec:** `docs/superpowers/specs/2026-11-07-planet-skylight-design.md`

## Global Constraints

- 自定义代码只进 `Source/` 的五个项目模块，**不进** `Plugins/VoxelFree`、`Plugins/VibeUE`。
- `*Tests.cpp` 只能放 `Src/Tests/`，且必须放所 include 头的下游模块。本计划全部测试在 `StellarEraPlanet/Src/Tests/`。
- 含中文的 `.h`/`.cpp` 存 **UTF-8 with BOM**，否则 MSVC 按 GBK 读会报假错误。
- 不要统一换行符；仓库本来就是 CRLF/LF 混合。
- 注释只写「为什么」，单处 1–3 行；不复述代码。
- 提交必须带 pathspec：`git commit -m "..." -- <paths>`。索引里有常驻暂存文件，裸 `git commit` 会吞掉全部。
- 长度单位一律 cm、方向一律单位向量；系数单位 1/cm。与壳材质（`Shaders/PlanetClouds.ush`）同一套，**不引入第二套单位**。
- 颜色一律 `SRGB=false` 的线性值；立方图必须 `PF_FloatRGBA`（8 位会把夜空压成 0）。
- 构建前**必须关闭编辑器**（Live Coding 锁 DLL）。构建用捆绑 dotnet 直调 UBT：

```bash
"D:/Application/Epic Games/UE_5.8/Engine/Binaries/ThirdParty/DotNet/10.0/win-x64/dotnet.exe" \
  "D:/Application/Epic Games/UE_5.8/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.dll" \
  StellarEraEditor Win64 Development \
  -Project="F:/workspace/ue/StellarEra/StellarEra.uproject" -WaitMutex -FromMsBuild
```

- 测试可在编辑器开着时跑：

```bash
"D:/Application/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" \
  "F:/workspace/ue/StellarEra/StellarEra.uproject" \
  -ExecCmds="Automation RunTests StellarEra.Planet.SkyLight" \
  -TestExit="Automation Test Queue Empty" \
  -unattended -nopause -nullrhi -nosplash -stdout -abslog="D:/Temp/skylight_test.log"
```

判据看 `D:/Temp/skylight_test.log` 里 `Result: Passed` / `Failed` 行与退出码。

## 对 spec 的两处修正（先读这段）

spec 是本次实现的合同，下面两条在写计划时被发现站不住，**以本计划为准**：

1. **§4.2 注 4 与 §4.4 的「4 × 壳半径」距离闸门互相矛盾。** 注 4 要求深空里也要驱动
   （往外看全是星空），闸门又把深空挡在外面。改为**不设距离闸门**：节流键里的高度被
   钳到 `[0, 壳厚]`，玩家飞远以后高度不再触发重算，而方位/太阳方向在太空里几乎不变，
   自然收敛到「算一次就不再算」。成本因此为零，两条要求同时满足。
   `InfluenceRadiusMultiplier` 这个属性**不要实现**（只会在编辑器里骗人）。
2. **§7 测试表前三条（正午天顶更蓝、日落更红、夜面 <1%）在单次散射模型里不成立。**
   模型照抄壳着色器，没有太阳路径透射项 —— 日落时天顶依然是蓝的（着色器本来就画成这样），
   地平线是否更蓝取决于光学厚度量级，没跑过不能断言。这三条降级为「结构性断言」
   （见 Task 1），**色相与观感移到 Task 4 的 PIE 目视**，不写成自动化断言。

---

## 开始前

- [ ] **检查工作区基线**

```bash
cd /f/workspace/ue/StellarEra && git status --short
```

当前分支 `fix/planet-shadow-frame-lag` 上已经压着未提交的 `SkyFill` 补光改动
（`PlanetAtmosphereComponent.h/.cpp`、`StarBodyComponent.cpp`、`UniverseMap.umap` 与一个
外部 Actor 包）。它是本计划的对照组，**先单独提交掉**，否则后面基于 diff 的复审会把旧代码
当新增：

```bash
cd /f/workspace/ue/StellarEra && git commit -m "feat(大气): SkyFill 定向补光与恒星灯排除" -- \
  Source/StellarEraPlanet/Src/Atmosphere/PlanetAtmosphereComponent.h \
  Source/StellarEraPlanet/Src/Atmosphere/PlanetAtmosphereComponent.cpp \
  Source/StellarEraPlanet/Src/Atmosphere/StarBodyComponent.cpp \
  Content/Code/Maps/UniverseMap.umap \
  Content/__ExternalActors__/Code/Maps/UniverseMap/E/I0/BZEJJXWV9FW8AVIKGGUKUP.uasset
```

若 `git status` 显示的东西与此不符，先弄清楚再动。

---

## 文件结构

| 动作 | 文件 | 职责 |
|---|---|---|
| 新增 | `Source/StellarEraPlanet/Src/Sky/PlanetSkyModel.h` | 纯 C++ 输入结构 + 三个函数声明，无 UObject |
| 新增 | `Source/StellarEraPlanet/Src/Sky/PlanetSkyModel.cpp` | 射线-球求交、单次散射积分、立方图面序 |
| 新增 | `Source/StellarEraPlanet/Src/Tests/PlanetSkyModelTests.cpp` | 模型的结构性断言 |
| 新增 | `Source/StellarEraPlanet/Src/Sky/PlanetSkyLightComponent.h` | 组件声明、可调参数、节流状态 |
| 新增 | `Source/StellarEraPlanet/Src/Sky/PlanetSkyLightComponent.cpp` | 找目标、节流、建 `UTextureCube`、`SetCubemap` |
| 修改 | `Source/StellarEraPlanet/Src/VoxelPlanetActor.h` | 加 `PlanetSkyLight` 成员 |
| 修改 | `Source/StellarEraPlanet/Src/VoxelPlanetActor.cpp` | 构造函数里 `CreateDefaultSubobject` |
| 修改 | `Content/Code/Maps/UniverseMap.umap`（含外部 Actor 包） | SkyLight 打标签、Mobility、强度 |
| 不改 | `Shaders/PlanetClouds.ush` | 本次不动着色器，所以**不需要** bump `PLANET_CLOUDS_VERSION` |

`Sky/` 目录已存在（`SpaceSkyMaterialBuilder.cpp`），新文件放进去即可，
`StellarEraPlanet.Build.cs` 的循环会把子目录自动加进 include 路径，**不需要改 Build.cs**。

---

### Task 1: `PlanetSky` 模型 + 自动化测试

**Files:**
- Create: `Source/StellarEraPlanet/Src/Sky/PlanetSkyModel.h`
- Create: `Source/StellarEraPlanet/Src/Sky/PlanetSkyModel.cpp`
- Test: `Source/StellarEraPlanet/Src/Tests/PlanetSkyModelTests.cpp`

**Interfaces:**
- Consumes: `PlanetAtmosphere::FShellScatteringParams`（`Source/StellarEraPlanet/Src/Atmosphere/PlanetAtmosphereParams.h`），
  测试里用 `PlanetAtmosphere::ComputeShellScatteringParams(float ShellRadiusCm, float RayleighScaleHeightRatio, float MieScaleHeightRatio)`。
- Produces:
  - `namespace PlanetSky`
  - `struct PlanetSky::FSun { FVector Direction; FLinearColor Radiance; };`
  - `struct PlanetSky::FModelInput { ... };`（字段见下）
  - `FVector PlanetSky::CubeFaceDirection(int32 FaceIndex, int32 PixelX, int32 PixelY, int32 Resolution);`
  - `FLinearColor PlanetSky::SampleDirection(const PlanetSky::FModelInput& Input, const FVector& Direction);`
  - `void PlanetSky::RenderCube(const PlanetSky::FModelInput& Input, int32 Resolution, TArray<FLinearColor>& OutPixels);`
    —— `OutPixels.Num() == 6 * Resolution * Resolution`，**面主序**，同一面内先行后列。

- [ ] **Step 1: 写头文件**

Create `Source/StellarEraPlanet/Src/Sky/PlanetSkyModel.h`（UTF-8 with BOM）：

```cpp
#pragma once

#include "CoreMinimal.h"
#include "PlanetAtmosphereParams.h"

/**
 * 自建大气壳的天空积分器（CPU 侧）。是 Shaders/PlanetClouds.ush 单次散射部分的第二份实现，
 * 改那边就要回来对一遍：光学厚度、相位函数、SunVis 淡出窗口、alpha 合成方式四项。
 * 纯 C++，无 UObject，可直接单测。
 */
namespace PlanetSky
{
	/** 一颗太阳：单位方向（观察点 → 太阳）与颜色×相对亮度。 */
	struct FSun
	{
		FVector Direction = FVector::UpVector;
		FLinearColor Radiance = FLinearColor::White;
	};

	/** 一次立方图渲染的全部输入。长度单位 cm，与壳材质同一套。 */
	struct FModelInput
	{
		/** 观察点（玩家相机）世界坐标。 */
		FVector ObserverLocation = FVector::ZeroVector;
		FVector PlanetCenter = FVector::ZeroVector;
		float SurfaceRadiusCm = 0.0f;
		float ShellRadiusCm = 0.0f;
		PlanetAtmosphere::FShellScatteringParams Scattering;
		/** 最多 4 颗，与 PlanetClouds::MaxSuns 同源；超出部分由调用方截断。 */
		TArray<FSun> Suns;
		/** 壳材质里的补偿系数（默认 14），不是太阳真亮了 14 倍。 */
		FLinearColor SunIntensity = FLinearColor(14.0f, 14.0f, 14.0f, 1.0f);
		/** 0 = 晴空。只做一阶衰减，不参与积分。 */
		float CloudCoverage = 0.0f;
		/** 星空/银河的平均辐亮：逃逸到太空的方向按透射率混进来。 */
		FLinearColor SpaceSkyRadiance = FLinearColor(0.008f, 0.010f, 0.020f, 1.0f);
		/** 与壳着色器的亮度校准倍率，只作用于散射项。 */
		float BrightnessScale = 1.0f;
		/** 单条射线的积分步数，与材质 ATMO_STEPS 默认一致。 */
		int32 Steps = 24;
	};

	/**
	 * 立方图第 FaceIndex 面、第 (PixelX, PixelY) 个像素对应的世界方向（单位向量）。
	 * 面序与 UV 映射抄自 ReflectiveEnvironmentShaders.usf 的 GetCubemapVector：
	 * ScaledUVs = ((FaceCoord + 0.5) / Res) * 2 - 1，FaceCoord = (col, row) = (PixelX, PixelY)。
	 * 这张表错了环境光就会从错误的方向来，改之前先回引擎源码对一遍。
	 */
	FVector CubeFaceDirection(int32 FaceIndex, int32 PixelX, int32 PixelY, int32 Resolution);

	/** 单方向辐亮度，已按壳材质的 alpha 合成折算成「肉眼看到的颜色」。供测试与调试。 */
	FLinearColor SampleDirection(const FModelInput& Input, const FVector& Direction);

	/** 按引擎立方图约定渲染 6 面到 OutPixels（面主序，同一面内先行后列）。 */
	void RenderCube(const FModelInput& Input, int32 Resolution, TArray<FLinearColor>& OutPixels);
}
```

- [ ] **Step 2: 写会失败的测试**

Create `Source/StellarEraPlanet/Src/Tests/PlanetSkyModelTests.cpp`（UTF-8 with BOM）：

```cpp
#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "PlanetSkyModel.h"
#include "PlanetAtmosphereParams.h"

#if WITH_DEV_AUTOMATION_TESTS

// 断言写法说明：浮点一律显式带容差；颜色用 Equals(容差) 而不是 ==。
//
// 这里**不断言色相**（天更蓝/日落更红）。模型照抄壳着色器，没有太阳路径透射项，
// 色相是否如预期要跑起来看，断言它只会得到一条没意义的红。色相验证在 Task 4 的 PIE 里。

namespace
{
	constexpr float TestSurfaceRadiusCm = 100000.0f;   // 1 km
	constexpr float TestShellRadiusCm = 109000.0f;     // 厚度比 0.09
	constexpr float TestRayleighScaleHeightRatio = 0.008f;
	constexpr float TestMieScaleHeightRatio = 0.0015f;

	const FLinearColor TestStarColor(0.008f, 0.010f, 0.020f, 1.0f);

	/** 观察点放在球面正上方（+Z），当地天顶就是 +Z，方向断言好写。 */
	PlanetSky::FModelInput MakeInput()
	{
		PlanetSky::FModelInput Input;
		Input.PlanetCenter = FVector::ZeroVector;
		Input.ObserverLocation = FVector(0.0f, 0.0f, TestSurfaceRadiusCm);
		Input.SurfaceRadiusCm = TestSurfaceRadiusCm;
		Input.ShellRadiusCm = TestShellRadiusCm;
		Input.Scattering = PlanetAtmosphere::ComputeShellScatteringParams(
			TestShellRadiusCm, TestRayleighScaleHeightRatio, TestMieScaleHeightRatio);
		Input.CloudCoverage = 0.0f;
		Input.SpaceSkyRadiance = TestStarColor;
		return Input;
	}

	/** 方位角 0° = +X，仰角 0° = 水平面。 */
	PlanetSky::FSun MakeSun(float ElevationDeg, float AzimuthDeg, const FLinearColor& Color)
	{
		const float El = FMath::DegreesToRadians(ElevationDeg);
		const float Az = FMath::DegreesToRadians(AzimuthDeg);
		PlanetSky::FSun Sun;
		Sun.Direction = FVector(FMath::Cos(El) * FMath::Cos(Az),
			FMath::Cos(El) * FMath::Sin(Az), FMath::Sin(El));
		Sun.Radiance = Color;
		return Sun;
	}
}

// 面序：Resolution = 1 时每个面的中心像素正好是轴方向。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraSkyFaceCentersTest,
	"StellarEra.Planet.SkyLight.FaceCenters",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FStellarEraSkyFaceCentersTest::RunTest(const FString& Parameters)
{
	const FVector Expected[6] = {
		FVector::ForwardVector,
		FVector::BackwardVector,
		FVector::RightVector,
		FVector::LeftVector,
		FVector::UpVector,
		FVector::DownVector,
	};
	for (int32 Face = 0; Face < 6; ++Face)
	{
		const FVector Actual = PlanetSky::CubeFaceDirection(Face, 0, 0, 1);
		TestTrue(FString::Printf(TEXT("face %d center"), Face), Actual.Equals(Expected[Face], 1e-5f));
	}
	return true;
}

// 非轴像素的符号约定：这一条锁的是 GetCubemapVector 里那些不容易看出来的负号。
// Resolution 4、像素 (0,0)：U = V = ((0.5)/4)*2-1 = -0.75。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraSkyFaceCornerTest,
	"StellarEra.Planet.SkyLight.FaceCorner",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FStellarEraSkyFaceCornerTest::RunTest(const FString& Parameters)
{
	// face 0 = +X: (1, -V, -U) = (1, 0.75, 0.75)
	const FVector Expected = FVector(1.0f, 0.75f, 0.75f).GetSafeNormal();
	TestTrue(TEXT("face 0 pixel (0,0)"),
		PlanetSky::CubeFaceDirection(0, 0, 0, 4).Equals(Expected, 1e-4f));

	// face 1 = -X: (-1, -V, U) = (-1, 0.75, -0.75)
	const FVector Expected1 = FVector(-1.0f, 0.75f, -0.75f).GetSafeNormal();
	TestTrue(TEXT("face 1 pixel (0,0)"),
		PlanetSky::CubeFaceDirection(1, 0, 0, 4).Equals(Expected1, 1e-4f));

	// face 5 = -Z: (-U, -V, -1) = (0.75, 0.75, -1)
	const FVector Expected5 = FVector(0.75f, 0.75f, -1.0f).GetSafeNormal();
	TestTrue(TEXT("face 5 pixel (0,0)"),
		PlanetSky::CubeFaceDirection(5, 0, 0, 4).Equals(Expected5, 1e-4f));
	return true;
}

// 夜面：太阳在地平线下 45°，朝天的方向必须完全不含太阳项 —— 换任何颜色的太阳，输出逐位相同。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraSkyNightIgnoresSunTest,
	"StellarEra.Planet.SkyLight.NightIgnoresSun",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FStellarEraSkyNightIgnoresSunTest::RunTest(const FString& Parameters)
{
	PlanetSky::FModelInput RedInput = MakeInput();
	RedInput.Suns.Add(MakeSun(-45.0f, 0.0f, FLinearColor(6.0f, 0.1f, 0.1f, 1.0f)));

	PlanetSky::FModelInput BlueInput = MakeInput();
	BlueInput.Suns.Add(MakeSun(-45.0f, 0.0f, FLinearColor(0.1f, 0.1f, 6.0f, 1.0f)));

	// 只看 +Z 面（仰角 > 40°）。贴近地平线的掠射射线会跑到 20° 以外，那段采样点可能已经
	// 出了行星阴影（壳着色器同样如此），拿它断言「夜面恒无太阳项」会红得莫名其妙。
	bool bAllFinite = true;
	bool bAllNonNegative = true;
	bool bAllBelowStar = true;
	for (int32 Y = 0; Y < 4; ++Y)
	{
		for (int32 X = 0; X < 4; ++X)
		{
			const FVector Dir = PlanetSky::CubeFaceDirection(4, X, Y, 4);
			const FLinearColor Red = PlanetSky::SampleDirection(RedInput, Dir);
			const FLinearColor Blue = PlanetSky::SampleDirection(BlueInput, Dir);

			bAllFinite &= FMath::IsFinite(Red.R) && FMath::IsFinite(Red.G) && FMath::IsFinite(Red.B);
			bAllNonNegative &= Red.R >= 0.0f && Red.G >= 0.0f && Red.B >= 0.0f;
			// 只剩星空乘透射率，不可能比星空本身更亮。
			bAllBelowStar &= Red.R <= TestStarColor.R + 1e-5f
				&& Red.G <= TestStarColor.G + 1e-5f
				&& Red.B <= TestStarColor.B + 1e-5f;

			if (!Red.Equals(Blue, 0.0f))
			{
				AddError(FString::Printf(TEXT("夜面输出依赖太阳颜色 @(%d,%d)"), X, Y));
				break;
			}
		}
	}
	TestTrue(TEXT("finite"), bAllFinite);
	TestTrue(TEXT("non-negative"), bAllNonNegative);
	TestTrue(TEXT("below star radiance"), bAllBelowStar);
	return true;
}

// 朝下的方向：射线在球面处就被地面截断，积分为零、也没有星空项。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraSkyGroundIsBlackTest,
	"StellarEra.Planet.SkyLight.GroundIsBlack",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FStellarEraSkyGroundIsBlackTest::RunTest(const FString& Parameters)
{
	PlanetSky::FModelInput Input = MakeInput();
	Input.Suns.Add(MakeSun(90.0f, 0.0f, FLinearColor::White));

	int32 Checked = 0;
	for (int32 Face = 0; Face < 6; ++Face)
	{
		for (int32 Y = 0; Y < 8; ++Y)
		{
			for (int32 X = 0; X < 8; ++X)
			{
				const FVector Dir = PlanetSky::CubeFaceDirection(Face, X, Y, 8);
				if (Dir.Z > -0.5f)
				{
					continue;
				}
				++Checked;
				const FLinearColor Value = PlanetSky::SampleDirection(Input, Dir);
				if (Value.R > 0.0f || Value.G > 0.0f || Value.B > 0.0f)
				{
					AddError(FString::Printf(TEXT("地面方向有颜色 face=%d x=%d y=%d"), Face, X, Y));
				}
			}
		}
	}
	TestTrue(TEXT("至少检查到若干朝下的像素"), Checked > 0);
	return true;
}

// 太阳越高，天顶越亮：θ=0 时米氏前向相位约为 60° 时的千倍，单调性有巨大余量。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraSkySunElevationMonotonicTest,
	"StellarEra.Planet.SkyLight.SunElevationMonotonic",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FStellarEraSkySunElevationMonotonicTest::RunTest(const FString& Parameters)
{
	PlanetSky::FModelInput Noon = MakeInput();
	Noon.Suns.Add(MakeSun(90.0f, 0.0f, FLinearColor::White));

	PlanetSky::FModelInput Mid = MakeInput();
	Mid.Suns.Add(MakeSun(30.0f, 0.0f, FLinearColor::White));

	const float NoonLuma = PlanetSky::SampleDirection(Noon, FVector::UpVector).GetLuminance();
	const float MidLuma = PlanetSky::SampleDirection(Mid, FVector::UpVector).GetLuminance();

	TestTrue(FString::Printf(TEXT("正午(%f) 亮于 30°(%f)"), NoonLuma, MidLuma), NoonLuma > MidLuma);
	TestTrue(FString::Printf(TEXT("正午至少两倍于 30°: %f vs %f"), NoonLuma, MidLuma),
		NoonLuma > MidLuma * 2.0f);
	return true;
}

// 观察点在壳外朝上看：整条射线不经过大气，输出必须**正好**是星空辐亮度。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraSkyOutsideShellIsStarTest,
	"StellarEra.Planet.SkyLight.OutsideShellIsStar",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FStellarEraSkyOutsideShellIsStarTest::RunTest(const FString& Parameters)
{
	PlanetSky::FModelInput Input = MakeInput();
	Input.ObserverLocation = FVector(0.0f, 0.0f, TestShellRadiusCm * 3.0f);
	Input.Suns.Add(MakeSun(90.0f, 0.0f, FLinearColor::White));

	const FLinearColor Up = PlanetSky::SampleDirection(Input, FVector::UpVector);
	TestTrue(TEXT("壳外朝上 == 星空"), Up.Equals(TestStarColor, 1e-6f));

	// 朝下穿过整层大气再看地面：必须有散射，不能等于星空。
	const FLinearColor Down = PlanetSky::SampleDirection(Input, FVector::DownVector);
	TestFalse(TEXT("壳外朝下 != 星空"), Down.Equals(TestStarColor, 1e-6f));
	TestTrue(TEXT("壳外朝下非负"), Down.R >= 0.0f && Down.G >= 0.0f && Down.B >= 0.0f);
	return true;
}

// 云只有一阶衰减：覆盖率为 1 时朝天的方向归零。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraSkyCloudsDimSkyTest,
	"StellarEra.Planet.SkyLight.CloudsDimSky",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FStellarEraSkyCloudsDimSkyTest::RunTest(const FString& Parameters)
{
	PlanetSky::FModelInput Clear = MakeInput();
	Clear.Suns.Add(MakeSun(60.0f, 0.0f, FLinearColor::White));

	PlanetSky::FModelInput Overcast = Clear;
	Overcast.CloudCoverage = 1.0f;

	const float ClearLuma = PlanetSky::SampleDirection(Clear, FVector::UpVector).GetLuminance();
	const float OvercastLuma = PlanetSky::SampleDirection(Overcast, FVector::UpVector).GetLuminance();

	TestTrue(TEXT("晴天有光"), ClearLuma > 0.0f);
	TestTrue(TEXT("全阴更暗"), OvercastLuma < ClearLuma);
	TestTrue(TEXT("全阴时朝天的方向归零"), OvercastLuma == 0.0f);
	return true;
}

// 整张立方图：像素数、无 NaN、非负、alpha 恒为 1（喂给 UTextureCube 之前不能有脏值）。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraSkyCubeShapeTest,
	"StellarEra.Planet.SkyLight.CubeShape",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FStellarEraSkyCubeShapeTest::RunTest(const FString& Parameters)
{
	PlanetSky::FModelInput Input = MakeInput();
	Input.Suns.Add(MakeSun(72.0f, 30.0f, FLinearColor(1.0f, 0.9f, 0.8f, 1.0f)));

	TArray<FLinearColor> Pixels;
	PlanetSky::RenderCube(Input, 8, Pixels);

	TestEqual(TEXT("pixel count"), Pixels.Num(), 6 * 8 * 8);
	bool bAllFinite = true;
	bool bAllNonNegative = true;
	bool bAlphaOne = true;
	for (const FLinearColor& Pixel : Pixels)
	{
		bAllFinite &= FMath::IsFinite(Pixel.R) && FMath::IsFinite(Pixel.G)
			&& FMath::IsFinite(Pixel.B);
		bAllNonNegative &= Pixel.R >= 0.0f && Pixel.G >= 0.0f && Pixel.B >= 0.0f;
		bAlphaOne &= FMath::IsNearlyEqual(Pixel.A, 1.0f, 1e-6f);
	}
	TestTrue(TEXT("finite"), bAllFinite);
	TestTrue(TEXT("non-negative"), bAllNonNegative);
	TestTrue(TEXT("alpha == 1"), bAlphaOne);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
```

- [ ] **Step 3: 构建确认测试编译但失败**

先关掉正在跑的编辑器（任务栏里那个 `UnrealEditor.exe`，或 `taskkill /F /IM UnrealEditor.exe`），
然后跑构建命令（见 Global Constraints）。

预期：链接失败，报 `error LNK2019: unresolved external symbol ... PlanetSky::RenderCube`
或类似（实现文件还不存在时的正常结果）。把输出抄下来。

- [ ] **Step 4: 写实现**

Create `Source/StellarEraPlanet/Src/Sky/PlanetSkyModel.cpp`（UTF-8 with BOM）：

```cpp
#include "PlanetSkyModel.h"

namespace
{
	// 与 PlanetClouds.ush 的 MIE_G 一致。
	constexpr float MieG = 0.8f;
	constexpr float Pi = 3.14159265358979323846f;

	// 太阳仰角淡出窗口（正弦值），-15° .. +1°，抄自 .ush 的 smoothstep(-0.26, 0.02, ...)。
	constexpr float SunVisFadeStart = -0.26f;
	constexpr float SunVisFadeEnd = 0.02f;

	float RayleighPhase(float CosTheta)
	{
		return 3.0f / (16.0f * Pi) * (1.0f + CosTheta * CosTheta);
	}

	float MiePhase(float CosTheta)
	{
		const float G2 = MieG * MieG;
		const float Denom = FMath::Max(1.0f + G2 - 2.0f * MieG * CosTheta, 1e-4f);
		return (1.0f - G2) / (4.0f * Pi * FMath::Pow(Denom, 1.5f));
	}

	/** 近交点；起点在球内返回 0，未命中返回负值。 */
	float RaySphereNear(const FVector& Origin, const FVector& Dir,
		const FVector& Center, float Radius)
	{
		const FVector Offset = Origin - Center;
		const float B = FVector::DotProduct(Offset, Dir);
		const float C = FVector::DotProduct(Offset, Offset) - Radius * Radius;
		const float Disc = B * B - C;
		if (Disc < 0.0f)
		{
			return -1.0f;
		}
		const float Near = -B - FMath::Sqrt(Disc);
		return Near > 0.0f ? Near : 0.0f;
	}

	/** 远交点；未命中或整个球在身后返回负值。 */
	float RaySphereFar(const FVector& Origin, const FVector& Dir,
		const FVector& Center, float Radius)
	{
		const FVector Offset = Origin - Center;
		const float B = FVector::DotProduct(Offset, Dir);
		const float C = FVector::DotProduct(Offset, Offset) - Radius * Radius;
		const float Disc = B * B - C;
		if (Disc < 0.0f)
		{
			return -1.0f;
		}
		const float Far = -B + FMath::Sqrt(Disc);
		return Far > 0.0f ? Far : -1.0f;
	}
}

FVector PlanetSky::CubeFaceDirection(int32 FaceIndex, int32 PixelX, int32 PixelY,
	int32 Resolution)
{
	const float Res = static_cast<float>(FMath::Max(Resolution, 1));
	const float U = ((PixelX + 0.5f) / Res) * 2.0f - 1.0f;
	const float V = ((PixelY + 0.5f) / Res) * 2.0f - 1.0f;

	FVector Dir;
	switch (FaceIndex)
	{
	case 0:  Dir = FVector( 1.0f, -V, -U); break;   // +X
	case 1:  Dir = FVector(-1.0f, -V,  U); break;   // -X
	case 2:  Dir = FVector( U,  1.0f,  V); break;   // +Y
	case 3:  Dir = FVector( U, -1.0f, -V); break;   // -Y
	case 4:  Dir = FVector( U, -V,  1.0f); break;   // +Z
	default: Dir = FVector(-U, -V, -1.0f); break;   // -Z
	}
	return Dir.GetSafeNormal();
}

FLinearColor PlanetSky::SampleDirection(const FModelInput& Input, const FVector& Direction)
{
	const FVector Dir = Direction.GetSafeNormal();
	const FVector Center = Input.PlanetCenter;

	// 观察点可能在壳内也可能在壳外：近交点小于 0 时从观察点起算。
	const float ShellEnter = RaySphereNear(Input.ObserverLocation, Dir, Center, Input.ShellRadiusCm);
	const float ShellExit = RaySphereFar(Input.ObserverLocation, Dir, Center, Input.ShellRadiusCm);
	if (ShellExit < 0.0f)
	{
		// 射线压根不碰大气：往上看就是星空。BrightnessScale 只校准散射，别乘到星空上。
		return Input.SpaceSkyRadiance;
	}

	float EndT = ShellExit;
	bool bHitGround = false;
	const float GroundHit = RaySphereNear(Input.ObserverLocation, Dir, Center, Input.SurfaceRadiusCm);
	if (GroundHit >= 0.0f && GroundHit < EndT)
	{
		EndT = GroundHit;
		bHitGround = true;
	}

	const int32 StepCount = FMath::Max(Input.Steps, 1);
	const float Span = EndT - ShellEnter;
	FLinearColor Acc = FLinearColor::Black;
	FLinearColor Transmittance = FLinearColor(1.0f, 1.0f, 1.0f, 1.0f);

	if (Span > 0.0f)
	{
		const float Ds = Span / static_cast<float>(StepCount);
		const FLinearColor RayleighBeta(Input.Scattering.RayleighBeta.X,
			Input.Scattering.RayleighBeta.Y, Input.Scattering.RayleighBeta.Z, 0.0f);
		const FLinearColor MieBeta(Input.Scattering.MieBeta.X,
			Input.Scattering.MieBeta.Y, Input.Scattering.MieBeta.Z, 0.0f);
		const float RayleighScaleHeight = FMath::Max(Input.Scattering.RayleighScaleHeightCm, 1.0f);
		const float MieScaleHeight = FMath::Max(Input.Scattering.MieScaleHeightCm, 1.0f);

		for (int32 Step = 0; Step < StepCount; ++Step)
		{
			const float T = ShellEnter + (Step + 0.5f) * Ds;
			const FVector SamplePosition = Input.ObserverLocation + Dir * T;
			const FVector Offset = SamplePosition - Center;
			const float Radius = Offset.Size();
			const float Height = FMath::Max(Radius - Input.SurfaceRadiusCm, 0.0f);

			const float RayleighDensity = FMath::Exp(-Height / RayleighScaleHeight);
			const float MieDensity = FMath::Exp(-Height / MieScaleHeight);

			FLinearColor Scatter = FLinearColor::Black;
			for (const FSun& Sun : Input.Suns)
			{
				// 阴影判据抄 .ush：采样点相对球心的方向与太阳方向的点积 / 半径。
				const float SinSunElevation =
					FVector::DotProduct(Offset, Sun.Direction) / FMath::Max(Radius, 1.0f);
				const float SunVisibility =
					FMath::SmoothStep(SunVisFadeStart, SunVisFadeEnd, SinSunElevation);
				if (SunVisibility <= 0.0f)
				{
					continue;
				}

				const float CosTheta = FVector::DotProduct(Dir, Sun.Direction);
				const FLinearColor Term =
					RayleighBeta * RayleighDensity * RayleighPhase(CosTheta)
					+ MieBeta * MieDensity * MiePhase(CosTheta);
				Scatter += FLinearColor(Sun.Radiance.R, Sun.Radiance.G, Sun.Radiance.B, 0.0f)
					* SunVisibility * Term;
			}

			const FLinearColor Extinction = RayleighBeta * RayleighDensity + MieBeta * MieDensity;
			const FLinearColor SegmentTransmittance(
				FMath::Exp(-Extinction.R * Ds),
				FMath::Exp(-Extinction.G * Ds),
				FMath::Exp(-Extinction.B * Ds), 1.0f);

			// 与 .ush 的累加顺序一致：先乘新透射率再更新 Tr，反过来结果不同。
			Acc += Scatter * Transmittance * SegmentTransmittance * Ds;
			Transmittance *= SegmentTransmittance;
		}
	}

	// 壳材质输出 float4(Acc, 1-T)。BLEND_Translucent 下可见颜色 = Acc × (1-T) + 背景 × T，
	// 所以这里也要乘 (1-T)；漏掉这一步，天光会比看得见的天空亮好几倍。
	const float Alpha = FMath::Clamp(
		1.0f - (Transmittance.R + Transmittance.G + Transmittance.B) / 3.0f, 0.0f, 1.0f);
	const FLinearColor SunScale = Input.SunIntensity * Input.BrightnessScale;
	FLinearColor Result = Acc * SunScale * Alpha;

	if (!bHitGround)
	{
		// 逃逸到太空：背景是星空，按 1-alpha 透上来。
		Result += Input.SpaceSkyRadiance * (1.0f - Alpha);
		// 云只有一阶衰减：覆盖率越高天越暗。全阴时朝天方向归零。
		Result *= 1.0f - FMath::Clamp(Input.CloudCoverage, 0.0f, 1.0f);
	}

	Result.A = 1.0f;
	return Result;
}

void PlanetSky::RenderCube(const FModelInput& Input, int32 Resolution,
	TArray<FLinearColor>& OutPixels)
{
	const int32 Res = FMath::Max(Resolution, 1);
	OutPixels.SetNumUninitialized(6 * Res * Res);

	int32 Index = 0;
	for (int32 Face = 0; Face < 6; ++Face)
	{
		for (int32 Y = 0; Y < Res; ++Y)
		{
			for (int32 X = 0; X < Res; ++X)
			{
				OutPixels[Index++] = SampleDirection(Input, CubeFaceDirection(Face, X, Y, Res));
			}
		}
	}
}
```

实现时注意三处**不要**「化简」的地方：

1. `Acc += Scatter * Tr * TrSeg * Ds` 先乘新透射率再更新 `Tr` —— 顺序换了结果不同，且与 .ush 对不上。
2. `Height` 用 `max(Radius - SurfaceRadiusCm, 0)` —— 与 .ush 的 `H = max(RLen - PlanetRadius, 0)` 一致。
3. 星空项与云衰减只在 `!bHitGround` 时加 —— 打到地面的方向背景是地形，不是天空。

- [ ] **Step 5: 构建并跑测试**

构建（编辑器保持关闭）：

```bash
"D:/Application/Epic Games/UE_5.8/Engine/Binaries/ThirdParty/DotNet/10.0/win-x64/dotnet.exe" \
  "D:/Application/Epic Games/UE_5.8/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.dll" \
  StellarEraEditor Win64 Development \
  -Project="F:/workspace/ue/StellarEra/StellarEra.uproject" -WaitMutex -FromMsBuild
```

预期：`Result: Succeeded`。

```bash
"D:/Application/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" \
  "F:/workspace/ue/StellarEra/StellarEra.uproject" \
  -ExecCmds="Automation RunTests StellarEra.Planet.SkyLight" \
  -TestExit="Automation Test Queue Empty" \
  -unattended -nopause -nullrhi -nosplash -stdout -abslog="D:/Temp/skylight_model.log"
```

预期：日志里 `StellarEra.Planet.SkyLight.*` 共 8 条，全部 `Result: Passed`。

失败时按断言文本定位，**不要**放宽容差去迁就实现 —— 容差是按物理量级定的
（`1e-6` 只用于「等于星空辐亮度」这类断言）。

- [ ] **Step 6: 提交**

```bash
cd /f/workspace/ue/StellarEra && git add \
  Source/StellarEraPlanet/Src/Sky/PlanetSkyModel.h \
  Source/StellarEraPlanet/Src/Sky/PlanetSkyModel.cpp \
  Source/StellarEraPlanet/Src/Tests/PlanetSkyModelTests.cpp
git commit -m "feat(天光): 自建大气天空立方图 CPU 积分器与结构性测试" -- \
  Source/StellarEraPlanet/Src/Sky/PlanetSkyModel.h \
  Source/StellarEraPlanet/Src/Sky/PlanetSkyModel.cpp \
  Source/StellarEraPlanet/Src/Tests/PlanetSkyModelTests.cpp
```

---

### Task 2: `UPlanetSkyLightComponent` + 行星 CDO 子对象

**Files:**
- Create: `Source/StellarEraPlanet/Src/Sky/PlanetSkyLightComponent.h`
- Create: `Source/StellarEraPlanet/Src/Sky/PlanetSkyLightComponent.cpp`
- Modify: `Source/StellarEraPlanet/Src/VoxelPlanetActor.h`（成员区，`WaterComponent` 之后，第 226–228 行）
- Modify: `Source/StellarEraPlanet/Src/VoxelPlanetActor.cpp`（构造函数，`WaterComponent` 创建之后、约第 172 行注释之前）
- Test: `Source/StellarEraPlanet/Src/Tests/PlanetSkyModelTests.cpp`（追加一个 CDO 存在性用例）

**Interfaces:**
- Consumes:
  - `PlanetSky::FModelInput`、`PlanetSky::FSun`、`PlanetSky::RenderCube(const FModelInput&, int32, TArray<FLinearColor>&)`（Task 1）
  - `PlanetAtmosphere::ComputeShellRadiusCm(float, float)`、`ComputeShellScatteringParams(float, float, float)`
  - `UPlanetAtmosphereComponent::CollectSunsFor(UWorld*, const FVector&, TArray<FPlanetSun>&)`（static）
  - `UPlanetAtmosphereComponent` 的公开属性：`AtmosphereHeightRatio`、`RayleighScaleHeightRatio`、`MieScaleHeightRatio`、`SunIntensity`、`CloudCoverage`
  - `AVoxelPlanetActor::GetSurfaceRadiusCm()`
- Produces:
  - 类 `UPlanetSkyLightComponent`（`StellarEraPlanet.Sky.PlanetSkyLightComponent`）
  - `AVoxelPlanetActor::PlanetSkyLight`（`UPROPERTY(VisibleAnywhere, BlueprintReadOnly)`）

- [ ] **Step 1: 追加一个会失败的测试**

先在 `Source/StellarEraPlanet/Src/Tests/PlanetSkyModelTests.cpp` 顶部 include 区（最后一条
`#include` 之后）加两行：

```cpp
#include "VoxelPlanetActor.h"
#include "PlanetSkyLightComponent.h"
```

再在文件末尾（`#endif` 之前）追加：

```cpp
// 天光是骨架：行星 CDO 上必须自带，不能靠蓝图补。蓝图 SCS 组件不在 CDO 上，这条能区分两种做法。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraSkyPlanetHasComponentTest,
	"StellarEra.Planet.SkyLight.PlanetHasComponent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FStellarEraSkyPlanetHasComponentTest::RunTest(const FString& Parameters)
{
	const AVoxelPlanetActor* Planet = GetDefault<AVoxelPlanetActor>();
	TestNotNull(TEXT("行星 CDO"), Planet);
	if (!Planet)
	{
		return false;
	}
	const UPlanetSkyLightComponent* Component =
		Planet->FindComponentByClass<UPlanetSkyLightComponent>();
	TestNotNull(TEXT("PlanetSkyLight"), Component);
	return true;
}
```

（顶部两条 include —— `VoxelPlanetActor.h`、`PlanetSkyLightComponent.h` —— 在 Step 2 写组件头文件
之前会编译失败，这正是预期的第一步。）

- [ ] **Step 2: 确认测试失败**

编辑器仍关闭，跑构建命令 —— 预期编译失败，报
`'UPlanetSkyLightComponent': undeclared identifier`。

- [ ] **Step 3: 写组件头文件**

Create `Source/StellarEraPlanet/Src/Sky/PlanetSkyLightComponent.h`（UTF-8 with BOM）：

```cpp
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "PlanetSkyModel.h"
#include "PlanetSkyLightComponent.generated.h"

class USkyLightComponent;
class UPlanetAtmosphereComponent;
class UTextureCube;

/**
 * 自建大气壳的天光：按玩家视角在 CPU 上积分出低分辨率天空立方图，喂给关卡里的
 * USkyLightComponent 当指定立方图，由引擎卷积成漫反射环境光。
 * 引擎的 RealTimeCapture 只认 SkyAtmosphere / 体积云 / IsSky 网格体，本项目三者都不是，
 * 抓出来是黑图 —— 这条数据链只能自己搭。编辑器世界不驱动（每帧改立方图会把关卡一直标脏）。
 */
UCLASS(ClassGroup = (StellarEra), meta = (BlueprintSpawnableComponent))
class STELLARERAPLANET_API UPlanetSkyLightComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UPlanetSkyLightComponent();

	/** 要驱动的 SkyLight。留空则按 Actor 标签在关卡里找。 */
	UPROPERTY(EditInstanceOnly, Category = "StellarEra|SkyLight")
	TObjectPtr<USkyLightComponent> TargetSkyLight;

	/** TargetSkyLight 为空时，用它找 SkyLight 所在的 Actor。 */
	UPROPERTY(EditAnywhere, Category = "StellarEra|SkyLight")
	FName SkyLightActorTag = TEXT("PlanetSky");

	/** 立方图边长。漫反射只吃低阶 SH，16 足够；8 更省，32 肉眼看不出差别。 */
	UPROPERTY(EditAnywhere, Category = "StellarEra|SkyLight", meta = (ClampMin = "4", ClampMax = "64"))
	int32 CubeResolution = 16;

	/** 两次重算的最小间隔（秒）。每次重算要新建一张立方图并触发一次 GPU 卷积。 */
	UPROPERTY(EditAnywhere, Category = "StellarEra|SkyLight", meta = (ClampMin = "0.0"))
	float UpdateIntervalSeconds = 0.2f;

	/** 星空/银河的平均辐亮（线性值）。夜面与深空的环境光全靠它，调到 0 就是纯黑。 */
	UPROPERTY(EditAnywhere, Category = "StellarEra|SkyLight")
	FLinearColor SpaceSkyRadiance = FLinearColor(0.008f, 0.010f, 0.020f, 1.0f);

	/** 与壳着色器的亮度校准倍率。画面亮度对不上时调它，不要去改物理量。 */
	UPROPERTY(EditAnywhere, Category = "StellarEra|SkyLight", meta = (ClampMin = "0.0"))
	float BrightnessScale = 1.0f;

	/** 立即重算并推送一次，忽略节流。调试与手动触发用。 */
	UFUNCTION(BlueprintCallable, Category = "StellarEra|SkyLight")
	void RefreshNow();

protected:
	virtual void TickComponent(float DeltaTime, ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;

private:
	// 必须先声明再被下面的函数签名用到（类内成员函数声明的参数类型不享受「完整类上下文」）。
	/** 决定天空是否变化的那几个量。全部在世界空间里比，因为 SkyLight 的 SH 就是世界空间的。 */
	struct FUpdateKey
	{
		FVector Up = FVector::ZeroVector;
		/** 钳到 [0, 壳厚]：飞远以后高度不再触发重算。 */
		float AltitudeCm = 0.0f;
		float SurfaceRadiusCm = 0.0f;
		float ShellRadiusCm = 0.0f;
		TArray<FVector> SunDirections;
	};

	bool ResolveAtmosphere();
	bool ResolveSkyLight();
	bool GetObserverLocation(FVector& OutLocation);
	bool BuildKey(const FVector& Observer, FUpdateKey& OutKey);
	bool NeedsUpdate(const FUpdateKey& Key) const;
	void UpdateCube(const FVector& Observer, const FUpdateKey& Key);

	UPROPERTY(Transient)
	TObjectPtr<UTextureCube> CurrentCubemap;

	TWeakObjectPtr<UPlanetAtmosphereComponent> Atmosphere;
	TWeakObjectPtr<USkyLightComponent> SkyLight;

	/** 每帧复用的缓冲，避免每次更新重新分配。 */
	TArray<PlanetSky::FSun> Suns;
	TArray<FLinearColor> Pixels;

	FUpdateKey LastKey;
	bool bHasUpdated = false;
	double LastUpdateTime = -1.0e9;

	bool bLoggedMissingAtmosphere = false;
	bool bLoggedMissingSkyLight = false;
	bool bLoggedMissingCamera = false;
	bool bLoggedCubemapFailure = false;
};
```

- [ ] **Step 4: 写组件实现**

Create `Source/StellarEraPlanet/Src/Sky/PlanetSkyLightComponent.cpp`（UTF-8 with BOM）：

```cpp
#include "PlanetSkyLightComponent.h"

#include "Components/SkyLightComponent.h"
#include "Engine/TextureCube.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerCameraManager.h"
#include "GameFramework/PlayerController.h"
#include "Math/Float16Color.h"
#include "PixelFormat.h"
#include "PlanetAtmosphereComponent.h"
// FTexture2DMipMap 的完整定义在 TextureResource.h；TextureCube.h 只前向声明它。
#include "TextureResource.h"
#include "VoxelPlanetActor.h"

namespace
{
	/** 方向变化阈值：1°。太阳方向与当地天顶共用。 */
	constexpr float DirectionThresholdDegrees = 1.0f;
}

UPlanetSkyLightComponent::UPlanetSkyLightComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	// 放在帧末：相机位置这一帧已经定下来了，算出来的 L1 方向才是玩家真正看到的方向。
	PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
}

void UPlanetSkyLightComponent::TickComponent(float DeltaTime, ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	UWorld* World = GetWorld();
	// 编辑器世界也会 Tick 本组件，驱动它等于每帧把关卡标脏。
	if (!World || !World->IsGameWorld())
	{
		return;
	}

	if (!ResolveAtmosphere() || !ResolveSkyLight())
	{
		return;
	}

	FVector Observer = FVector::ZeroVector;
	if (!GetObserverLocation(Observer))
	{
		return;
	}

	FUpdateKey Key;
	if (!BuildKey(Observer, Key))
	{
		return;
	}

	if (!NeedsUpdate(Key))
	{
		return;
	}

	UpdateCube(Observer, Key);
	LastKey = MoveTemp(Key);
	bHasUpdated = true;
	LastUpdateTime = World->GetTimeSeconds();
}

void UPlanetSkyLightComponent::RefreshNow()
{
	UWorld* World = GetWorld();
	if (!World || !World->IsGameWorld() || !ResolveAtmosphere() || !ResolveSkyLight())
	{
		return;
	}

	FVector Observer = FVector::ZeroVector;
	if (!GetObserverLocation(Observer))
	{
		return;
	}

	FUpdateKey Key;
	if (!BuildKey(Observer, Key))
	{
		return;
	}

	UpdateCube(Observer, Key);
	LastKey = MoveTemp(Key);
	bHasUpdated = true;
	LastUpdateTime = World->GetTimeSeconds();
}

bool UPlanetSkyLightComponent::ResolveAtmosphere()
{
	if (Atmosphere.IsValid())
	{
		return true;
	}

	AActor* Owner = GetOwner();
	Atmosphere = Owner ? Owner->FindComponentByClass<UPlanetAtmosphereComponent>() : nullptr;
	if (!Atmosphere.IsValid())
	{
		// 没有大气壳就没有天空模型可积；这时候给黑图不如什么都不做，让关卡原有的立方图顶着。
		if (!bLoggedMissingAtmosphere)
		{
			bLoggedMissingAtmosphere = true;
			UE_LOG(LogTemp, Warning,
				TEXT("[天光] %s 上找不到 UPlanetAtmosphereComponent，天光不工作。"),
				*GetNameSafe(Owner));
		}
		return false;
	}
	return true;
}

bool UPlanetSkyLightComponent::ResolveSkyLight()
{
	if (SkyLight.IsValid())
	{
		return true;
	}

	USkyLightComponent* Found = TargetSkyLight;
	if (!Found && GetWorld())
	{
		for (TActorIterator<AActor> It(GetWorld()); It; ++It)
		{
			if (!It->ActorHasTag(SkyLightActorTag))
			{
				continue;
			}
			Found = It->FindComponentByClass<USkyLightComponent>();
			if (Found)
			{
				break;
			}
		}
	}

	if (!Found)
	{
		if (!bLoggedMissingSkyLight)
		{
			bLoggedMissingSkyLight = true;
			UE_LOG(LogTemp, Warning,
				TEXT("[天光] 关卡里没有打了 %s 标签的 SkyLight，天光不工作。"),
				*SkyLightActorTag.ToString());
		}
		return false;
	}

	// 三件事只在第一次做：改用指定立方图（否则 SetCubemap 进去也用不上）、
	// 关掉实时捕获（引擎会 ensure「scheduled capture 与 RealTimeCapture 互斥」）、
	// 改可动（静态组件不接受运行期立方图）。
	Found->bRealTimeCapture = false;
	Found->SourceType = SLS_SpecifiedCubemap;
	Found->SetMobility(EComponentMobility::Movable);

	SkyLight = Found;
	return true;
}

bool UPlanetSkyLightComponent::GetObserverLocation(FVector& OutLocation)
{
	UWorld* World = GetWorld();
	APlayerController* Controller = World ? World->GetFirstPlayerController() : nullptr;
	APlayerCameraManager* CameraManager = Controller ? Controller->PlayerCameraManager : nullptr;
	if (!CameraManager)
	{
		// 没有相机时拿行星中心当观察点算出来的方向是错的，宁可不工作。
		if (!bLoggedMissingCamera)
		{
			bLoggedMissingCamera = true;
			UE_LOG(LogTemp, Warning, TEXT("[天光] 没有玩家相机，天光不工作。"));
		}
		return false;
	}

	OutLocation = CameraManager->GetCameraLocation();
	return true;
}

bool UPlanetSkyLightComponent::BuildKey(const FVector& Observer, FUpdateKey& OutKey)
{
	UWorld* World = GetWorld();
	UPlanetAtmosphereComponent* Atmo = Atmosphere.Get();
	AVoxelPlanetActor* Planet = Atmo ? Cast<AVoxelPlanetActor>(Atmo->GetOwner()) : nullptr;
	if (!World || !Atmo || !Planet)
	{
		return false;
	}

	const float SurfaceRadiusCm = Planet->GetSurfaceRadiusCm();
	if (SurfaceRadiusCm <= 0.0f)
	{
		return false;
	}

	const FVector PlanetCenter = Atmo->GetComponentLocation();
	const FVector Offset = Observer - PlanetCenter;
	const float ShellRadiusCm =
		PlanetAtmosphere::ComputeShellRadiusCm(SurfaceRadiusCm, Atmo->AtmosphereHeightRatio);

	OutKey.Up = Offset.GetSafeNormal();
	OutKey.SurfaceRadiusCm = SurfaceRadiusCm;
	OutKey.ShellRadiusCm = ShellRadiusCm;
	// 钳到壳内：飞到深空以后「高度」每帧都在变，不钳的话节流形同虚设。
	OutKey.AltitudeCm = FMath::Clamp(Offset.Size() - SurfaceRadiusCm,
		0.0f, FMath::Max(ShellRadiusCm - SurfaceRadiusCm, 0.0f));

	UPlanetAtmosphereComponent::CollectSunsFor(World, PlanetCenter, Suns);
	OutKey.SunDirections.Reset(Suns.Num());
	for (const FPlanetSun& Sun : Suns)
	{
		OutKey.SunDirections.Add(Sun.Direction);
	}
	return true;
}

bool UPlanetSkyLightComponent::NeedsUpdate(const FUpdateKey& Key) const
{
	if (!bHasUpdated)
	{
		return true;
	}

	const UWorld* World = GetWorld();
	if (!World || World->GetTimeSeconds() - LastUpdateTime < UpdateIntervalSeconds)
	{
		return false;
	}

	const float CosThreshold = FMath::Cos(FMath::DegreesToRadians(DirectionThresholdDegrees));
	if (FVector::DotProduct(Key.Up, LastKey.Up) < CosThreshold)
	{
		return true;
	}

	if (Key.SunDirections.Num() != LastKey.SunDirections.Num())
	{
		return true;
	}
	for (int32 Index = 0; Index < Key.SunDirections.Num(); ++Index)
	{
		if (FVector::DotProduct(Key.SunDirections[Index], LastKey.SunDirections[Index])
			< CosThreshold)
		{
			return true;
		}
	}

	if (!FMath::IsNearlyEqual(Key.SurfaceRadiusCm, LastKey.SurfaceRadiusCm, 1.0f)
		|| !FMath::IsNearlyEqual(Key.ShellRadiusCm, LastKey.ShellRadiusCm, 1.0f))
	{
		return true;
	}

	// 高度阈值取瑞利标高的四分之一：比这更小的变化看不出来，却会让 5 Hz 的节流白跑。
	const float AltitudeThresholdCm = FMath::Max(
		Atmosphere->RayleighScaleHeightRatio * Key.ShellRadiusCm * 0.25f, 1.0f);
	return FMath::Abs(Key.AltitudeCm - LastKey.AltitudeCm) > AltitudeThresholdCm;
}

void UPlanetSkyLightComponent::UpdateCube(const FVector& Observer, const FUpdateKey& Key)
{
	UPlanetAtmosphereComponent* Atmo = Atmosphere.Get();
	USkyLightComponent* Target = SkyLight.Get();
	if (!Atmo || !Target)
	{
		return;
	}

	PlanetSky::FModelInput Input;
	Input.ObserverLocation = Observer;
	Input.PlanetCenter = Atmo->GetComponentLocation();
	Input.SurfaceRadiusCm = Key.SurfaceRadiusCm;
	Input.ShellRadiusCm = Key.ShellRadiusCm;
	Input.Scattering = PlanetAtmosphere::ComputeShellScatteringParams(
		Key.ShellRadiusCm, Atmo->RayleighScaleHeightRatio, Atmo->MieScaleHeightRatio);
	Input.SunIntensity = Atmo->SunIntensity;
	Input.CloudCoverage = Atmo->CloudCoverage;
	Input.SpaceSkyRadiance = SpaceSkyRadiance;
	Input.BrightnessScale = BrightnessScale;

	Input.Suns.Reset(Suns.Num());
	for (const FPlanetSun& Sun : Suns)
	{
		PlanetSky::FSun Converted;
		Converted.Direction = Sun.Direction;
		Converted.Radiance = Sun.Radiance;
		Input.Suns.Add(Converted);
	}

	const double StartSeconds = FPlatformTime::Seconds();
	PlanetSky::RenderCube(Input, CubeResolution, Pixels);

	const int32 Resolution = FMath::Max(CubeResolution, 1);
	UTextureCube* Cube = UTextureCube::CreateTransient(Resolution, Resolution, PF_FloatRGBA);
	if (!Cube)
	{
		if (!bLoggedCubemapFailure)
		{
			bLoggedCubemapFailure = true;
			UE_LOG(LogTemp, Warning, TEXT("[天光] 立方图创建失败（边长 %d），跳过本次更新。"),
				Resolution);
		}
		return;
	}

	// SRGB 必须关：存的是线性辐亮度，gamma 编码会把夜空压成 0。
	Cube->SRGB = false;

	FTexture2DMipMap& Mip = Cube->GetPlatformData()->Mips[0];
	FFloat16Color* MipData = static_cast<FFloat16Color*>(Mip.BulkData.Lock(LOCK_READ_WRITE));
	const int32 NumTexels = 6 * Resolution * Resolution;
	for (int32 Index = 0; Index < NumTexels; ++Index)
	{
		MipData[Index] = FFloat16Color(Pixels[Index]);
	}
	Mip.BulkData.Unlock();
	Cube->UpdateResource();

	// 每次新建纹理：SetCubemap 只在指针变化时才重新卷积，复用同一张会让环境光停在第一次。
	CurrentCubemap = Cube;
	Target->SetCubemap(Cube);

	const double ElapsedMs = (FPlatformTime::Seconds() - StartSeconds) * 1000.0;
	UE_LOG(LogTemp, Log, TEXT("[天光] 重算 边长=%d 太阳=%d 用时=%.3f ms"),
		Resolution, Input.Suns.Num(), ElapsedMs);
}
```

- [ ] **Step 5: 接进行星 Actor**

Modify `Source/StellarEraPlanet/Src/VoxelPlanetActor.h`：

前置声明区（`class UPlanetWaterComponent;` 附近）加一行：

```cpp
class UPlanetSkyLightComponent;
```

成员区（`WaterComponent` 声明之后）加：

```cpp
	/** 自建大气的天光：按玩家视角算天空立方图喂关卡 SkyLight。缺了环境光就永远是同一张静态图。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Planet")
	TObjectPtr<UPlanetSkyLightComponent> PlanetSkyLight;
```

Modify `Source/StellarEraPlanet/Src/VoxelPlanetActor.cpp`：

- 顶部 include 区加 `#include "Sky/PlanetSkyLightComponent.h"`
- 构造函数里 `WaterComponent = CreateDefaultSubobject<UPlanetWaterComponent>(...)` 之后、
  `// 大气壳、矿物观察与表面编辑网格由 BP_VoxelPlanet 挂载` 注释之前插入：

```cpp
	// 天光不是可选项：没有它环境光永远是同一张静态立方图。骨架留 C++，蓝图不用管。
	PlanetSkyLight = CreateDefaultSubobject<UPlanetSkyLightComponent>(TEXT("PlanetSkyLight"));
```

它是 `UActorComponent`，不参与组件树，**不需要** `SetupAttachment`。

- [ ] **Step 6: 构建并跑全部天光测试**

```bash
"D:/Application/Epic Games/UE_5.8/Engine/Binaries/ThirdParty/DotNet/10.0/win-x64/dotnet.exe" \
  "D:/Application/Epic Games/UE_5.8/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.dll" \
  StellarEraEditor Win64 Development \
  -Project="F:/workspace/ue/StellarEra/StellarEra.uproject" -WaitMutex -FromMsBuild
```

预期：`Result: Succeeded`。

```bash
"D:/Application/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" \
  "F:/workspace/ue/StellarEra/StellarEra.uproject" \
  -ExecCmds="Automation RunTests StellarEra.Planet.SkyLight" \
  -TestExit="Automation Test Queue Empty" \
  -unattended -nopause -nullrhi -nosplash -stdout -abslog="D:/Temp/skylight_component.log"
```

预期：9 条全部 `Result: Passed`（Task 1 的 8 条 + `PlanetHasComponent`）。

- [ ] **Step 7: 提交**

```bash
cd /f/workspace/ue/StellarEra && git commit -m "feat(天光): 行星天光组件，CPU 立方图驱动关卡 SkyLight" -- \
  Source/StellarEraPlanet/Src/Sky/PlanetSkyLightComponent.h \
  Source/StellarEraPlanet/Src/Sky/PlanetSkyLightComponent.cpp \
  Source/StellarEraPlanet/Src/Tests/PlanetSkyModelTests.cpp \
  Source/StellarEraPlanet/Src/VoxelPlanetActor.h \
  Source/StellarEraPlanet/Src/VoxelPlanetActor.cpp
```

---

### Task 3: 关卡 SkyLight 接线（`UniverseMap`）

**Files:**
- Modify: `Content/Code/Maps/UniverseMap.umap` + 对应的外部 Actor 包
  （`Content/__ExternalActors__/Code/Maps/UniverseMap/**`）

**Interfaces:**
- Consumes: Task 2 的 `UPlanetSkyLightComponent::SkyLightActorTag` 默认值 `PlanetSky`。
- Produces: 关卡里一盏 `Mobility = Movable`、`SourceType = SpecifiedCubemap`、`Intensity = 1`、
  Actor 标签 `PlanetSky` 的 SkyLight。

- [ ] **Step 1: 启动编辑器并跑脚本**

先启动编辑器（必须在 Step 6 构建成功之后，否则加载的是旧 DLL）：

```bash
cd "/d/Application/Epic Games/UE_5.8/Engine/Binaries/Win64" && ./UnrealEditor.exe \
  "F:/workspace/ue/StellarEra/StellarEra.uproject" > /d/Temp/ue_mcp/editor_skylight.log 2>&1 &
```

编辑器起来后，通过 MCP（`http://127.0.0.1:8000/mcp`）的 `execute_python_code` 跑：

```python
import unreal

subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
skylights = [a for a in subsystem.get_all_level_actors() if isinstance(a, unreal.SkyLight)]
print("count", len(skylights))
for light in skylights:
    tags = [str(t) for t in light.get_editor_property("tags")]
    if "PlanetSky" not in tags:
        light.set_editor_property("tags", tags + ["PlanetSky"])

    comp = light.get_editor_property("light_component")
    comp.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)
    comp.set_editor_property("source_type", unreal.SkyLightSourceType.SLS_SPECIFIED_CUBEMAP)
    comp.set_editor_property("intensity", 1.0)
    comp.set_editor_property("light_color", unreal.Color(r=255, g=255, b=255, a=255))
    comp.set_editor_property("real_time_capture", False)

    print(light.get_actor_label(), light.get_editor_property("tags"),
          comp.get_editor_property("intensity"),
          comp.get_editor_property("source_type"))
```

预期输出：`count 1`，且打出来的强度是 `1.0`、来源类型是 `SLS_SPECIFIED_CUBEMAP`、标签里有 `PlanetSky`。
`set_editor_property("tags", ...)` 传字符串数组即可，UE 的 Python 绑定会转成 `TArray<FName>`。

- [ ] **Step 2: 保存并验证落盘**

```python
import unreal
unreal.EditorLevelLibrary.save_current_level()
```

`save_current_level()` 会把这一个关卡的全部脏包一起落盘 —— 做这步之前工作区应该是干净的，
所以只会写入 SkyLight 所在的包。验证：

```bash
cd /f/workspace/ue/StellarEra && git status --short
```

预期：只有 `Content/Code/Maps/UniverseMap.umap` 与它对应的
`Content/__ExternalActors__/Code/Maps/UniverseMap/**` 一两个包被修改。
**若出现别的包，先查是不是脚本副作用，别一起提交。**

`save_current_level()` 的返回值可能是假的（历史上报过假失败）。真判据是
`git status` 里出现了那两个包。若 `git status` 干净，重新跑一次保存。

- [ ] **Step 3: 提交**

```bash
cd /f/workspace/ue/StellarEra && git commit -m "feat(天光): UniverseMap 的 SkyLight 改由行星天光驱动" -- \
  Content/Code/Maps/UniverseMap.umap \
  Content/__ExternalActors__/Code/Maps/UniverseMap
```

---

### Task 4: PIE 目视验证与成本记录

**Files:**
- 不改代码；验证结果追加到 spec 的「风险与未决项」之后（新开一节「实施记录」）

**Interfaces:**
- Consumes: Task 1–3 的全部产出。
- Produces: 一段可复核的验证记录，以及 `SkyFill` 去留的结论。

- [ ] **Step 1: 进 `UniverseMap` 跑 PIE，确认天光在驱动**

在输出日志里过滤 `[天光]`：

- 预期每次重算一条 `LogTemp: [天光] 重算 边长=16 太阳=N 用时=X ms`；
- 站着不动时应该**稀疏**（只在太阳方向/天顶/高度变化超过阈值时出现），不是每帧一条；
- `用时` 应远小于 1 ms。

若一条都没有：先查 `[天光]` 开头的 Warning（缺大气壳 / 缺 SkyLight / 缺相机）。

- [ ] **Step 2: 三处目视判据**

1. **环境光方向**：站在白模球旁边（或把飞船停在晨昏线附近），环境光应从太阳一侧滚到另一侧。
   这一条是 spec §7 唯一锁不住、必须眼睛看的：立方图面序错了的话，光会从相反的方向来。
2. **昼夜**：走到夜面，地面应降到接近黑，只剩星空给的微弱蓝灰。白天应明显偏蓝。
3. **日色**：飞船金属与水面（`M_PlanetWater` 走场景色）的暗部应随日色变化——暖色恒星下偏暖。

- [ ] **Step 3: `SkyFill` 去留**

把大气组件的 `SkyFillIntensity` 设为 0，再看晨昏线：

- 若环境光已经足够填住 N·L=0 的暗面、没有硬边 → **建议**下一轮删掉 `SkyFill` 整条链路
  （`ApplySkyFill`、`SkyFillLight`、`SkyFillTag`、`StarBodyComponent::ResolveSunLight` 里的排除分支）；
- 若暗面还是死黑 → 保留 `SkyFill`，把结论写进记录，不要擅自删。

本任务**不删任何代码**，只做判断并记录。

- [ ] **Step 4: 记录**

在 `docs/superpowers/specs/2026-11-07-planet-skylight-design.md` 末尾追加：

```markdown
## 10. 实施记录（2026-11-07）

- 立方图重算节奏：<实测，例如「站着不动约每 3 s 一次，跑动时贴 5 Hz」>
- 单次 CPU 积分耗时：<实测 ms，边长 16，N 颗太阳>
- 面序与引擎约定是否一致：<白模球环境光方向是否正确>
- 天空亮度与壳的对拍：<是否需要调 BrightnessScale，最终值>
- `SkyFill` 去留：<保留 / 建议删除，理由>
```

```bash
cd /f/workspace/ue/StellarEra && git commit -m "docs(天光): 记录 PIE 验证结果" -- \
  docs/superpowers/specs/2026-11-07-planet-skylight-design.md
```

---

## 完成标准

- `Automation RunTests StellarEra.Planet.SkyLight` 全绿（9 条）。
- `UniverseMap` 里 SkyLight 带 `PlanetSky` 标签、Movable、SpecifiedCubemap、强度 1。
- PIE 里 `[天光]` 日志按阈值稀疏出现，不是每帧。
- 站在夜面时环境光接近黑；白天偏蓝；环境光方向随太阳滚动。
- `SkyFill` 的去留有了明确结论，写在 spec 的实施记录里。
