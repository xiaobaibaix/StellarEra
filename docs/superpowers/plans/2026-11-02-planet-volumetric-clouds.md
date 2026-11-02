# 行星体积云实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 在现有大气壳的同一次 ray march 里加入程序化体积云，让行星从轨道和地表都能看到成块的云。

**Architecture:** 不新增材质与组件。云密度采样写进 `M_PlanetAtmosphere` 的 `MaterialExpressionCustom`，与大气共用同一条射线积分；`UPlanetAtmosphereComponent` 增加云参数并推进 MID。

**Tech Stack:** UE 5.8.3 / C++ / HLSL（`MaterialExpressionCustom`）/ UE 自动化测试。

**Spec:** `docs/superpowers/specs/2026-11-02-planet-volumetric-clouds-design.md`

**Baseline:** `eefaaac847e9484fda8ed2cfa598e96f8efd131f`（分支 `feat/volumetric-clouds`）

## Global Constraints

- 含中文的 `.h` / `.cpp` 必须存成 **UTF-8 with BOM**，否则 MSVC 按 GBK 读会报假错。
- 测试文件名必须以 `Tests.cpp` 结尾，且**只能**放 `Source/StellarEraPlanet/Src/Tests/`。
- 测试里的辅助函数放**具名**命名空间 —— UBT 默认开 unity build，匿名命名空间会与同一 TU 里的其它 `.cpp` 撞名（C2086 / C2374）。
- 提交必须把 pathspec 挂在 commit 上：`git commit -m "..." -- <paths>`。索引里有常驻暂存文件，裸 commit 会吞掉它们。
- 材质参数名在 C++ 常量与生成器里必须**逐字一致**：不一致的症状是"参数静默不生效"，没有任何报错。
- 编辑器后台整进程硬限 3 FPS，观感验收必须让编辑器在前台。

---

## 文件结构

| 文件 | 职责 |
|---|---|
| `Source/StellarEraPlanet/Src/Atmosphere/PlanetCloudParams.h` | 云层的纯数学：半径换算、覆盖率→噪声阈值 |
| `Source/StellarEraPlanet/Src/Atmosphere/PlanetCloudParams.cpp` | 上述实现 |
| `Source/StellarEraPlanet/Src/Atmosphere/PlanetAtmosphereComponent.h/.cpp` | 云参数的持有者与 MID 写入方 |
| `Source/StellarEraPlanet/Src/Atmosphere/PlanetAtmosphereMaterialBuilder.cpp` | HLSL 与材质参数节点 |
| `Source/StellarEraPlanet/Src/Tests/PlanetCloudParamsTests.cpp` | 纯函数测试 |
| `Source/StellarEraPlanet/Src/Tests/PlanetAtmosphereMaterialTests.cpp` | 材质结构与 HLSL 关键字测试（扩展） |

---

### Task 1: 纯函数层（PlanetCloudParams）

**Files:**
- Create: `Source/StellarEraPlanet/Src/Atmosphere/PlanetCloudParams.h`
- Create: `Source/StellarEraPlanet/Src/Atmosphere/PlanetCloudParams.cpp`
- Test: `Source/StellarEraPlanet/Src/Tests/PlanetCloudParamsTests.cpp`

**Interfaces:**
- Consumes: `PlanetAtmosphere::ComputeShellRadiusCm(float, float)`（已存在）
- Produces:
  - `PlanetClouds::FCloudLayerRadii { float BaseCm; float TopCm; }`
  - `PlanetClouds::ComputeCloudLayerRadiiCm(float SurfaceRadiusCm, float BaseRatio, float TopRatio) -> FCloudLayerRadii`
  - `PlanetClouds::CoverageToNoiseThreshold(float Coverage) -> float`

- [ ] **Step 1: 写失败测试**

创建 `Source/StellarEraPlanet/Src/Tests/PlanetCloudParamsTests.cpp`（UTF-8 with BOM）：

```cpp
#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Atmosphere/PlanetAtmosphereParams.h"
#include "Atmosphere/PlanetCloudParams.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraCloudParamsRadiiTest,
	"StellarEra.Planet.CloudParams.Radii",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FStellarEraCloudParamsRadiiTest::RunTest(const FString& Parameters)
{
	constexpr float SurfaceRadiusCm = 2180000.0f;   // UniverseMap 的大行星
	constexpr float Tolerance = 1.0f;

	const PlanetClouds::FCloudLayerRadii Radii =
		PlanetClouds::ComputeCloudLayerRadiiCm(SurfaceRadiusCm, 0.02f, 0.07f);

	TestEqual(TEXT("云底 = 表面半径 × 0.02"), Radii.BaseCm, 43600.0f, Tolerance);
	TestEqual(TEXT("云顶 = 表面半径 × 0.07"), Radii.TopCm, 152600.0f, Tolerance);

	// 云必须留在壳内，否则会被射线求交直接裁掉，永远看不见。
	const float ShellRadiusCm = PlanetAtmosphere::ComputeShellRadiusCm(SurfaceRadiusCm, 0.09f);
	TestTrue(TEXT("云顶低于大气壳"), Radii.TopCm < ShellRadiusCm);

	// 比例写反（Top < Base）时钳成零厚度，而不是出一个负厚度。
	const PlanetClouds::FCloudLayerRadii Inverted =
		PlanetClouds::ComputeCloudLayerRadiiCm(1000.0f, 0.09f, 0.02f);
	TestTrue(TEXT("Top 被钳到不小于 Base"), Inverted.TopCm >= Inverted.BaseCm);

	// 负比例按 0 处理。
	const PlanetClouds::FCloudLayerRadii Negative =
		PlanetClouds::ComputeCloudLayerRadiiCm(1000.0f, -1.0f, -2.0f);
	TestEqual(TEXT("负比例钳到 0"), Negative.BaseCm, 0.0f, Tolerance);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraCloudParamsCoverageTest,
	"StellarEra.Planet.CloudParams.Coverage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FStellarEraCloudParamsCoverageTest::RunTest(const FString& Parameters)
{
	// 阈值 = 1 - 覆盖率。噪声值域是 [0,1]，阈值 1 时任何噪声都超不过 → 晴空。
	TestEqual(TEXT("覆盖率 0 → 阈值 1（永远是晴空）"),
		PlanetClouds::CoverageToNoiseThreshold(0.0f), 1.0f, KINDA_SMALL_NUMBER);
	TestEqual(TEXT("覆盖率 1 → 阈值 0（处处是云）"),
		PlanetClouds::CoverageToNoiseThreshold(1.0f), 0.0f, KINDA_SMALL_NUMBER);
	TestEqual(TEXT("覆盖率 0.45 → 阈值 0.55"),
		PlanetClouds::CoverageToNoiseThreshold(0.45f), 0.55f, KINDA_SMALL_NUMBER);
	TestEqual(TEXT("超上界钳制"),
		PlanetClouds::CoverageToNoiseThreshold(2.0f), 0.0f, KINDA_SMALL_NUMBER);
	TestEqual(TEXT("超下界钳制"),
		PlanetClouds::CoverageToNoiseThreshold(-1.0f), 1.0f, KINDA_SMALL_NUMBER);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
```

- [ ] **Step 2: 构建，确认它编译失败（头文件还不存在）**

```bash
export PATH="/d/Application/Epic Games/UE_5.8/Engine/Binaries/ThirdParty/DotNet/10.0/win-x64:$PATH"
dotnet "/d/Application/Epic Games/UE_5.8/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.dll" \
  StellarEraEditor Win64 Development \
  -Project="F:/workspace/ue/StellarEra/.claude/worktrees/volumetric-clouds/StellarEra.uproject" \
  -WaitMutex -FromMsBuild 2>&1 | tail -20
```

预期：`fatal error C1083: 无法打开包括文件: "Atmosphere/PlanetCloudParams.h"`。

> worktree 的 `Intermediate/` 不在仓库里，**首次是全量编译**，比增量慢很多。命令用 PATH 里的 `dotnet` 裸名 —— 带空格的绝对路径当命令名会被沙箱护栏拒掉。

- [ ] **Step 3: 实现头文件**

创建 `Source/StellarEraPlanet/Src/Atmosphere/PlanetCloudParams.h`（UTF-8 with BOM）：

```cpp
#pragma once

#include "CoreMinimal.h"

/**
 * 行星体积云的纯数学部分。
 *
 * 与 PlanetAtmosphereParams 一样不含 UObject，可被任意模块直接单测。
 * 真正的密度计算在材质 HLSL 里（PlanetAtmosphereMaterialBuilder.cpp），
 * 这里只放"必须和 C++ 侧算得一模一样"的那两件事：云层半径、覆盖率阈值。
 */
namespace PlanetClouds
{
	/** 云层的内外半径（cm），以行星中心为原点。 */
	struct FCloudLayerRadii
	{
		float BaseCm = 0.0f;
		float TopCm = 0.0f;
	};

	/**
	 * 云层半径 = 表面半径 × 比例。
	 *
	 * 比例按 0 兜底（负比例没有物理意义），Top 被钳到不小于 Base ——
	 * 比例写反时得到的是零厚度云层而不是负厚度，后者会让 HLSL 里的
	 * 除法变号，云会长到地面以下。
	 */
	STELLARERAPLANET_API FCloudLayerRadii ComputeCloudLayerRadiiCm(
		float SurfaceRadiusCm, float BaseRatio, float TopRatio);

	/**
	 * 覆盖率 → 噪声阈值，公式是 `1 - Coverage`（钳到 0..1）。
	 *
	 * HLSL 里用的就是这个式子，两边必须一致：不一致的症状是
	 * "组件上调了覆盖率，画面上看不出变化"。
	 */
	STELLARERAPLANET_API float CoverageToNoiseThreshold(float Coverage);

} // namespace PlanetClouds
```

- [ ] **Step 4: 实现 .cpp**

创建 `Source/StellarEraPlanet/Src/Atmosphere/PlanetCloudParams.cpp`（UTF-8 with BOM）：

```cpp
#include "PlanetCloudParams.h"

namespace PlanetClouds
{
	FCloudLayerRadii ComputeCloudLayerRadiiCm(float SurfaceRadiusCm, float BaseRatio, float TopRatio)
	{
		FCloudLayerRadii Out;
		Out.BaseCm = SurfaceRadiusCm * FMath::Max(BaseRatio, 0.0f);
		Out.TopCm = SurfaceRadiusCm * FMath::Max(TopRatio, 0.0f);
		if (Out.TopCm < Out.BaseCm)
		{
			Out.TopCm = Out.BaseCm;
		}
		return Out;
	}

	float CoverageToNoiseThreshold(float Coverage)
	{
		return 1.0f - FMath::Clamp(Coverage, 0.0f, 1.0f);
	}

} // namespace PlanetClouds
```

- [ ] **Step 5: 构建 + 跑测试**

构建命令同 Step 2。然后：

```bash
"/d/Application/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" \
  "F:/workspace/ue/StellarEra/.claude/worktrees/volumetric-clouds/StellarEra.uproject" \
  -ExecCmds="Automation RunTests StellarEra.Planet.CloudParams" \
  -TestExit="Automation Test Queue Empty" \
  -unattended -nopause -nullrhi -nosplash -stdout \
  -abslog="F:/workspace/ue/StellarEra/.claude/worktrees/volumetric-clouds/Saved/Logs/cloudparams.txt"
grep -E "Test (Succeeded|Failed)|LogAutomationController: (Error|Warning)" \
  "F:/workspace/ue/StellarEra/.claude/worktrees/volumetric-clouds/Saved/Logs/cloudparams.txt" | tail -10
```

预期：两条测试都通过。若进程不退出（已知坑），`taskkill //F //PID <pid>`。

- [ ] **Step 6: 提交**

```bash
git add -- Source/StellarEraPlanet/Src/Atmosphere/PlanetCloudParams.h \
            Source/StellarEraPlanet/Src/Atmosphere/PlanetCloudParams.cpp \
            Source/StellarEraPlanet/Src/Tests/PlanetCloudParamsTests.cpp
git commit -m "feat(云): 云层半径与覆盖率的纯函数层" -- \
  Source/StellarEraPlanet/Src/Atmosphere/PlanetCloudParams.h \
  Source/StellarEraPlanet/Src/Atmosphere/PlanetCloudParams.cpp \
  Source/StellarEraPlanet/Src/Tests/PlanetCloudParamsTests.cpp
```

---

### Task 2: 材质 HLSL 与参数

**Files:**
- Modify: `Source/StellarEraPlanet/Src/Atmosphere/PlanetAtmosphereMaterialBuilder.cpp`
- Test: `Source/StellarEraPlanet/Src/Tests/PlanetAtmosphereMaterialTests.cpp`

**Interfaces:**
- Consumes: 无（本任务只在材质层面加参数，参数值由 Task 3 推）
- Produces: 材质标量参数 `CloudBaseRadius`、`CloudTopRadius`、`CloudCoverage`、`CloudExtinction`、`CloudNoiseScale`、`CloudDetailScale`、`CloudDetailStrength`、`CloudDrift`、`CloudBrightness`

- [ ] **Step 1: 扩展测试（先写断言，会红）**

在 `PlanetAtmosphereMaterialTests.cpp` 的 `FStellarEraAtmosphereMaterialStructureTest` 里，把标量参数名数组换成：

```cpp
	const TCHAR* const ScalarNames[] = {
		TEXT("PlanetRadius"), TEXT("ShellRadius"),
		TEXT("RayleighScaleHeight"), TEXT("MieScaleHeight"),
		TEXT("CloudBaseRadius"), TEXT("CloudTopRadius"), TEXT("CloudCoverage"),
		TEXT("CloudExtinction"), TEXT("CloudNoiseScale"), TEXT("CloudDetailScale"),
		TEXT("CloudDetailStrength"), TEXT("CloudDrift"), TEXT("CloudBrightness")
	};
```

在 `FStellarEraAtmosphereMaterialHlslTest` 的 `return true;` 之前追加：

```cpp
	// 云：密度函数、噪声、自遮挡步数、云层半径参数。四个都在，才说明云真进了 shader。
	TestTrue(TEXT("HLSL 含云密度函数"), Custom->Code.Contains(TEXT("CloudDensity")));
	TestTrue(TEXT("HLSL 含噪声函数"), Custom->Code.Contains(TEXT("CloudFbm")));
	TestTrue(TEXT("HLSL 含云自遮挡"), Custom->Code.Contains(TEXT("CLOUD_SHADOW_STEPS")));
	TestTrue(TEXT("HLSL 含云层底半径"), Custom->Code.Contains(TEXT("CloudBaseRadius")));
```

- [ ] **Step 2: 生成器加参数节点**

在 `BuildPlanetAtmosphereMaterial` 里 `SunIntensity` 节点之后追加：

```cpp
	// ---- 云 ----
	// 初值按 21.8 km 行星的默认比例算：云底 0.02、云顶 0.07、光学厚度 6。
	// Task 3 会在 BeginPlay / OnRegister 里按实际行星半径覆盖。
	UMaterialExpressionScalarParameter* CloudBaseRadius =
		AddAtmoScalarParam(Material, TEXT("CloudBaseRadius"), 43600.0f, -800, 800);
	UMaterialExpressionScalarParameter* CloudTopRadius =
		AddAtmoScalarParam(Material, TEXT("CloudTopRadius"), 152600.0f, -800, 900);
	UMaterialExpressionScalarParameter* CloudCoverage =
		AddAtmoScalarParam(Material, TEXT("CloudCoverage"), 0.45f, -800, 1000);
	// 5.5e-5 /cm：云层厚 1.09e5 cm 时 τ = 6。材质要的是消光系数不是光学厚度。
	UMaterialExpressionScalarParameter* CloudExtinction =
		AddAtmoScalarParam(Material, TEXT("CloudExtinction"), 5.5e-5f, -800, 1100);
	UMaterialExpressionScalarParameter* CloudNoiseScale =
		AddAtmoScalarParam(Material, TEXT("CloudNoiseScale"), 8.0f, -800, 1200);
	UMaterialExpressionScalarParameter* CloudDetailScale =
		AddAtmoScalarParam(Material, TEXT("CloudDetailScale"), 24.0f, -800, 1300);
	UMaterialExpressionScalarParameter* CloudDetailStrength =
		AddAtmoScalarParam(Material, TEXT("CloudDetailStrength"), 0.35f, -800, 1400);
	UMaterialExpressionScalarParameter* CloudDrift =
		AddAtmoScalarParam(Material, TEXT("CloudDrift"), 0.0f, -800, 1500);
	UMaterialExpressionScalarParameter* CloudBrightness =
		AddAtmoScalarParam(Material, TEXT("CloudBrightness"), 1.0f, -800, 1600);
```

在既有的 `AddAtmoCustomInput(...)` 那一段末尾追加九个输入（名字必须与 HLSL 里的变量名逐字一致）：

```cpp
	AddAtmoCustomInput(Custom, TEXT("CloudBaseRadius"), CloudBaseRadius);
	AddAtmoCustomInput(Custom, TEXT("CloudTopRadius"), CloudTopRadius);
	AddAtmoCustomInput(Custom, TEXT("CloudCoverage"), CloudCoverage);
	AddAtmoCustomInput(Custom, TEXT("CloudExtinction"), CloudExtinction);
	AddAtmoCustomInput(Custom, TEXT("CloudNoiseScale"), CloudNoiseScale);
	AddAtmoCustomInput(Custom, TEXT("CloudDetailScale"), CloudDetailScale);
	AddAtmoCustomInput(Custom, TEXT("CloudDetailStrength"), CloudDetailStrength);
	AddAtmoCustomInput(Custom, TEXT("CloudDrift"), CloudDrift);
	AddAtmoCustomInput(Custom, TEXT("CloudBrightness"), CloudBrightness);
```

- [ ] **Step 3: 改 HLSL —— 步数与云函数**

把 `#define ATMO_STEPS 16` 换成：

```hlsl
#define ATMO_STEPS 32
#define CLOUD_SHADOW_STEPS 3
#define CLOUD_WEATHER_OCTAVES 3
#define CLOUD_DETAIL_OCTAVES 2
```

在 `float3 toSunDir = SunDirection.rgb;` 之前插入云函数：

```hlsl
// ---- 云 ----
// 3D value noise。按单位方向采样（见 CloudDensity 的 q），球面没有接缝、极点不挤。
float CloudHash13(float3 p)
{
    p = frac(p * 0.1031);
    p += dot(p, p.yzx + 33.33);
    return frac((p.x + p.y) * p.z);
}

float CloudValueNoise(float3 p)
{
    float3 i = floor(p);
    float3 f = frac(p);
    f = f * f * (3.0 - 2.0 * f);
    float n000 = CloudHash13(i);
    float n100 = CloudHash13(i + float3(1.0, 0.0, 0.0));
    float n010 = CloudHash13(i + float3(0.0, 1.0, 0.0));
    float n110 = CloudHash13(i + float3(1.0, 1.0, 0.0));
    float n001 = CloudHash13(i + float3(0.0, 0.0, 1.0));
    float n101 = CloudHash13(i + float3(1.0, 0.0, 1.0));
    float n011 = CloudHash13(i + float3(0.0, 1.0, 1.0));
    float n111 = CloudHash13(i + float3(1.0, 1.0, 1.0));
    return lerp(
        lerp(lerp(n000, n100, f.x), lerp(n010, n110, f.x), f.y),
        lerp(lerp(n001, n101, f.x), lerp(n011, n111, f.x), f.y),
        f.z);
}

// 归一化到 [0,1]：每层振幅减半，最后除以振幅总和。
float CloudFbm(float3 p, int Octaves)
{
    float Sum = 0.0;
    float Amp = 0.5;
    float AmpSum = 0.0;
    for (int i = 0; i < Octaves; ++i)
    {
        Sum += Amp * CloudValueNoise(p);
        AmpSum += Amp;
        p *= 2.03;
        Amp *= 0.5;
    }
    return Sum / max(AmpSum, 1e-5);
}

// 云的消光系数（1/cm）。ops 是相对行星中心的向量，rLen 是它的长度（调用方已算）。
float CloudDensity(float3 ops, float rLen)
{
    float Thickness = CloudTopRadius - CloudBaseRadius;
    if (Thickness <= 0.0 || CloudExtinction <= 0.0)
    {
        return 0.0;
    }
    float h = (rLen - CloudBaseRadius) / Thickness;
    if (h <= 0.0 || h >= 1.0)
    {
        return 0.0;   // 早退：云层之外的步不付噪声代价
    }
    float profile = smoothstep(0.0, 0.15, h) * (1.0 - smoothstep(0.72, 1.0, h));

    float3 q = normalize(ops) * CloudNoiseScale;
    float w = CloudFbm(q + CloudDrift, CLOUD_WEATHER_OCTAVES);
    float d = saturate((w - (1.0 - CloudCoverage)) / max(CloudCoverage, 1e-3));
    d *= profile;
    d -= CloudDetailStrength
        * CloudFbm(q * (CloudDetailScale / max(CloudNoiseScale, 1e-3)), CLOUD_DETAIL_OCTAVES)
        * (1.0 - d);
    return saturate(d) * CloudExtinction;
}

// 双瓣 Henyey-Greenstein：前向给银边，后向让云的暗面不至于死黑。
float CloudPhase(float cosTheta)
{
    float gF = 0.6;
    float gB = -0.15;
    float pF = (1.0 - gF * gF) / (4.0 * ATMO_PI * pow(max(1.0 + gF * gF - 2.0 * gF * cosTheta, 1e-4), 1.5));
    float pB = (1.0 - gB * gB) / (4.0 * ATMO_PI * pow(max(1.0 + gB * gB - 2.0 * gB * cosTheta, 1e-4), 1.5));
    return 0.7 * pF + 0.3 * pB;
}
```

- [ ] **Step 4: 改 HLSL —— 把云接进积分循环**

把从 `float3 toSunDir = SunDirection.rgb;` 到循环结束的那一段换成：

```hlsl
float3 toSunDir = SunDirection.rgb;
float phaseC = CloudPhase(cosTheta);

// 单次散射积分：固定步长中点法
float ds = (tExit - tEnter) / float(ATMO_STEPS);
float3 transmittance = float3(1.0, 1.0, 1.0);
float3 accumulated = float3(0.0, 0.0, 0.0);
for (int i = 0; i < ATMO_STEPS; ++i)
{
    float t = tEnter + (float(i) + 0.5) * ds;
    float3 p = ro + rd * t;
    // 太阳被行星本体挡住时，这一段不产生单次散射：求 P 到"指向太阳的射线"的垂距，
    // 小于行星半径即落在本影里。必须 bS < 0 —— 否则最近点落在射线的反向延长线上。
    // 0.99~1.01 的平滑带给出晨昏线附近的过渡，避免一条硬边。
    float3 ops = p - PlanetCenter;
    float rLen = length(ops);
    float bS = dot(ops, toSunDir);
    float sunVis = 1.0;
    if (bS < 0.0)
    {
        float dPerp = length(ops - bS * toSunDir);
        sunVis = smoothstep(PlanetRadius * 0.99, PlanetRadius * 1.01, dPerp);
    }
    float h = max(rLen - PlanetRadius, 0.0);
    float dR = exp(-h / RayleighScaleHeight);
    float dM = exp(-h / MieScaleHeight);
    float dC = CloudDensity(ops, rLen);
    // 云的消光是灰度，直接加进 extinction。
    float3 extinction = RayleighBeta.rgb * dR + MieBeta.rgb * dM + float3(dC, dC, dC);
    transmittance *= exp(-extinction * ds);
    accumulated += (RayleighBeta.rgb * dR * phaseR + MieBeta.rgb * dM * phaseM) * transmittance * sunVis * ds;

    if (dC > 0.0)
    {
        // 云自遮挡：朝太阳方向再采几步，越往云顶透射越多 → 云顶亮、云底暗。
        float cloudLight = sunVis;
        float shadowStepCm = (CloudTopRadius - CloudBaseRadius) * 0.33;
        for (int j = 1; j <= CLOUD_SHADOW_STEPS; ++j)
        {
            float3 sp = p + toSunDir * (float(j) * shadowStepCm);
            float3 sops = sp - PlanetCenter;
            cloudLight *= exp(-CloudDensity(sops, length(sops)) * shadowStepCm);
        }
        accumulated += float3(CloudBrightness, CloudBrightness, CloudBrightness)
            * dC * phaseC * cloudLight * transmittance * ds;
    }
}
```

- [ ] **Step 5: 构建**

构建命令同 Task 1 Step 2。预期编译通过（本任务不新增头文件）。

- [ ] **Step 6: 重建材质资产（不跑这一步测试必红）**

材质由编辑器内的控制台命令生成：

```bash
"/d/Application/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" \
  "F:/workspace/ue/StellarEra/.claude/worktrees/volumetric-clouds/StellarEra.uproject" \
  -ExecCmds="StellarEra.BuildAtmosphereMaterial" \
  -unattended -nopause -nullrhi -nosplash -stdout \
  -abslog="F:/workspace/ue/StellarEra/.claude/worktrees/volumetric-clouds/Saved/Logs/material.txt"
```

判据（`QUIT` 不会让它退出，这是已知坑）：
1. 日志里有 `[大气] 材质已生成：/Game/Code/Planet/Materials/M_PlanetAtmosphere`
2. `ls -la Content/Code/Planet/Materials/M_PlanetAtmosphere.uasset` 字节数变了
3. 按 PID `taskkill //F //PID <pid>` 收掉进程，再确认没有残留

- [ ] **Step 7: 跑材质测试**

```bash
"/d/Application/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" \
  "F:/workspace/ue/StellarEra/.claude/worktrees/volumetric-clouds/StellarEra.uproject" \
  -ExecCmds="Automation RunTests StellarEra.Planet.AtmosphereMaterial" \
  -TestExit="Automation Test Queue Empty" -unattended -nopause -nullrhi -nosplash -stdout \
  -abslog="F:/workspace/ue/StellarEra/.claude/worktrees/volumetric-clouds/Saved/Logs/atmo.txt"
grep -E "Test (Succeeded|Failed)" \
  "F:/workspace/ue/StellarEra/.claude/worktrees/volumetric-clouds/Saved/Logs/atmo.txt" | tail -5
```

预期：`Structure` 与 `Hlsl` 两条都通过。

- [ ] **Step 8: 提交**

```bash
git add -- Source/StellarEraPlanet/Src/Atmosphere/PlanetAtmosphereMaterialBuilder.cpp \
            Source/StellarEraPlanet/Src/Tests/PlanetAtmosphereMaterialTests.cpp \
            Content/Code/Planet/Materials/M_PlanetAtmosphere.uasset
git commit -m "feat(云): 把体积云密度接进大气壳的同一次 ray march" -- \
  Source/StellarEraPlanet/Src/Atmosphere/PlanetAtmosphereMaterialBuilder.cpp \
  Source/StellarEraPlanet/Src/Tests/PlanetAtmosphereMaterialTests.cpp \
  Content/Code/Planet/Materials/M_PlanetAtmosphere.uasset
```

---

### Task 3: 组件参数与 MID 接线

**Files:**
- Modify: `Source/StellarEraPlanet/Src/Atmosphere/PlanetAtmosphereComponent.h`
- Modify: `Source/StellarEraPlanet/Src/Atmosphere/PlanetAtmosphereComponent.cpp`

**Interfaces:**
- Consumes: `PlanetClouds::ComputeCloudLayerRadiiCm`（Task 1）；九个材质参数名（Task 2）
- Produces: 十个 UPROPERTY（`bCloudsEnabled` + 九个数值），供设计器调参

- [ ] **Step 1: 头文件加参数**

在 `SunIntensity` 之后、私有段之前插入：

```cpp
	// ---- 体积云 ----
	// 云与大气共用同一次 ray march：两层同心半透明壳无法同时排对序
	// （球内要"先大气后云"，球外要"先云后大气"），见
	// docs/superpowers/specs/2026-11-02-planet-volumetric-clouds-design.md。

	/** 云的总开关。关掉 = 光学厚度传 0，材质不重建。 */
	UPROPERTY(EditAnywhere, Category = "StellarEra|Clouds")
	bool bCloudsEnabled = true;

	/** 云底高度 / 表面半径。必须小于 AtmosphereHeightRatio，否则云会长到壳外被裁掉。 */
	UPROPERTY(EditAnywhere, Category = "StellarEra|Clouds", meta = (ClampMin = "0.0"))
	float CloudBaseRatio = 0.02f;

	/** 云顶高度 / 表面半径。 */
	UPROPERTY(EditAnywhere, Category = "StellarEra|Clouds", meta = (ClampMin = "0.0"))
	float CloudTopRatio = 0.07f;

	/** 覆盖率：0 = 晴空，1 = 全阴。 */
	UPROPERTY(EditAnywhere, Category = "StellarEra|Clouds", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float CloudCoverage = 0.45f;

	/**
	 * 满密度垂直穿层的总光学厚度（无量纲）。
	 *
	 * 材质要的是消光系数（1/cm），由 本值 / 云层厚度 换算 —— 换一颗更大/更小的
	 * 行星时云的浓淡不变。与 ComputeShellScatteringParams 用 τ 反推 β 同一个理由。
	 */
	UPROPERTY(EditAnywhere, Category = "StellarEra|Clouds", meta = (ClampMin = "0.0"))
	float CloudOpticalDepth = 6.0f;

	/** 天气图频率（噪声坐标取在归一化方向上）。越大云块越碎。 */
	UPROPERTY(EditAnywhere, Category = "StellarEra|Clouds", meta = (ClampMin = "0.0"))
	float CloudNoiseScale = 8.0f;

	/** 细节侵蚀的频率。 */
	UPROPERTY(EditAnywhere, Category = "StellarEra|Clouds", meta = (ClampMin = "0.0"))
	float CloudDetailScale = 24.0f;

	/** 细节侵蚀强度。设为 0 可省两层噪声，是首要的降级旋钮。 */
	UPROPERTY(EditAnywhere, Category = "StellarEra|Clouds", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float CloudDetailStrength = 0.35f;

	/** 云随时间漂移的速度（噪声坐标/秒）。0 = 静止。 */
	UPROPERTY(EditAnywhere, Category = "StellarEra|Clouds")
	float CloudDriftPerSecond = 0.02f;

	/**
	 * 云的亮度倍率。
	 *
	 * 云与大气共用 SunIntensity（14，那是为单次散射的大气补的），直接照搬会把
	 * 云顶推到溢出白。这个倍率是给云单独的补偿项 —— 不要去改 SunIntensity。
	 */
	UPROPERTY(EditAnywhere, Category = "StellarEra|Clouds", meta = (ClampMin = "0.0"))
	float CloudBrightness = 1.0f;
```

- [ ] **Step 2: .cpp 加参数名常量**

在匿名命名空间的 `ParamSunIntensity` 之后加：

```cpp
	const FName ParamCloudBaseRadius(TEXT("CloudBaseRadius"));
	const FName ParamCloudTopRadius(TEXT("CloudTopRadius"));
	const FName ParamCloudCoverage(TEXT("CloudCoverage"));
	const FName ParamCloudExtinction(TEXT("CloudExtinction"));
	const FName ParamCloudNoiseScale(TEXT("CloudNoiseScale"));
	const FName ParamCloudDetailScale(TEXT("CloudDetailScale"));
	const FName ParamCloudDetailStrength(TEXT("CloudDetailStrength"));
	const FName ParamCloudDrift(TEXT("CloudDrift"));
	const FName ParamCloudBrightness(TEXT("CloudBrightness"));
```

- [ ] **Step 3: RefreshAtmosphereGeometry 里推云参数**

在 `SetVectorParameterValue(ParamSunIntensity, SunIntensity);` 之后、`SetVisibility(true);` 之前插入：

```cpp
	// ---- 云 ----
	// 先钳比例：云必须留在壳内，否则射线求交会把整层裁掉，而且不会有任何报错。
	const float ClampedTopRatio = FMath::Min(CloudTopRatio, AtmosphereHeightRatio);
	const float ClampedBaseRatio = FMath::Min(CloudBaseRatio, ClampedTopRatio);
	const PlanetClouds::FCloudLayerRadii CloudRadii =
		PlanetClouds::ComputeCloudLayerRadiiCm(SurfaceRadiusCm, ClampedBaseRatio, ClampedTopRatio);

	const float CloudExtinctionPerCm =
		(bCloudsEnabled && CloudOpticalDepth > 0.0f && CloudRadii.TopCm > CloudRadii.BaseCm)
			? CloudOpticalDepth / (CloudRadii.TopCm - CloudRadii.BaseCm)
			: 0.0f;

	AtmosphereMID->SetScalarParameterValue(ParamCloudBaseRadius, CloudRadii.BaseCm);
	AtmosphereMID->SetScalarParameterValue(ParamCloudTopRadius, CloudRadii.TopCm);
	AtmosphereMID->SetScalarParameterValue(ParamCloudCoverage, FMath::Clamp(CloudCoverage, 0.0f, 1.0f));
	AtmosphereMID->SetScalarParameterValue(ParamCloudExtinction, CloudExtinctionPerCm);
	AtmosphereMID->SetScalarParameterValue(ParamCloudNoiseScale, CloudNoiseScale);
	AtmosphereMID->SetScalarParameterValue(ParamCloudDetailScale, CloudDetailScale);
	AtmosphereMID->SetScalarParameterValue(ParamCloudDetailStrength, CloudDetailStrength);
	AtmosphereMID->SetScalarParameterValue(ParamCloudBrightness, CloudBrightness);
```

并在 include 区加 `#include "PlanetCloudParams.h"`（放在 `#include "PlanetAtmosphereParams.h"` 旁边）。

- [ ] **Step 4: TickComponent 里每帧写漂移量**

在写 `ParamPlanetCenter` 的那段之后加：

```cpp
	// 云漂移：每帧一个标量，成本可忽略；CloudDriftPerSecond = 0 时写进去的是常数。
	const float Drift = GetWorld()->GetTimeSeconds() * CloudDriftPerSecond;
	AtmosphereMID->SetScalarParameterValue(ParamCloudDrift, Drift);
```

- [ ] **Step 5: 构建**

构建命令同 Task 1 Step 2。

- [ ] **Step 6: 提交**

```bash
git add -- Source/StellarEraPlanet/Src/Atmosphere/PlanetAtmosphereComponent.h \
            Source/StellarEraPlanet/Src/Atmosphere/PlanetAtmosphereComponent.cpp
git commit -m "feat(云): 大气组件增加云参数并推进 MID" -- \
  Source/StellarEraPlanet/Src/Atmosphere/PlanetAtmosphereComponent.h \
  Source/StellarEraPlanet/Src/Atmosphere/PlanetAtmosphereComponent.cpp
```

---

### Task 4: 应用到关卡并视觉验收

**Files:**
- Modify (binary): `Content/__ExternalActors__/Code/Maps/UniverseMap/` 下三颗行星的包

- [ ] **Step 1: 用 worktree 的编辑器打开关卡**

需要用户配合：**关掉主树的编辑器**，然后打开

```
F:/workspace/ue/StellarEra/.claude/worktrees/volumetric-clouds/StellarEra.uproject
```

MCP 端点 `127.0.0.1:8000` 只能被一个编辑器占用。

- [ ] **Step 2: 确认三颗行星的组件用的是 C++ 默认值**

用 MCP 遍历三颗行星的 `PlanetAtmosphereComponent`，读 `cloud_base_ratio` / `cloud_top_ratio` / `cloud_coverage`。若实例里存的是旧值（SCS 组件不跟随 CDO 新默认），就地写一遍并保存。

- [ ] **Step 3: 从轨道看**

视口相机放到 `(0, -9e6, 0)`、`Rotator(roll=0, pitch=0, yaw=90)`，截图。预期：行星上能看出白色云块与缝隙；云只在昼面发亮，夜面没有云光。

- [ ] **Step 4: 从地表抬头看**

相机放到行星表面上方约 3000 cm（`(0, -2200000, 0)`）朝上 `pitch=90`，截图。预期：头顶是云的底面，能看出云的走向；`CloudDriftPerSecond > 0` 时画面里的云在动。

- [ ] **Step 5: 调参并写回**

按观感调 `CloudBrightness`（云顶是否溢出白）、`CloudCoverage`（多少云）、`CloudOpticalDepth`（厚不厚），把最终值写回三颗行星的组件并保存。最终值记进提交信息。

- [ ] **Step 6: 提交**

```bash
git add -- Content/__ExternalActors__/Code/Maps/UniverseMap
git commit -m "feat(云): 关卡三颗行星启用体积云并定参" -- \
  Content/__ExternalActors__/Code/Maps/UniverseMap
```

---

## 完成判据

1. `Automation RunTests StellarEra.Planet.CloudParams` 全绿
2. `Automation RunTests StellarEra.Planet.AtmosphereMaterial` 全绿（跑之前材质已重建）
3. 两张截图：轨道视角能看出云块，地表视角能看出云层
4. 工作区干净，四个提交都在 `feat/volumetric-clouds` 上
