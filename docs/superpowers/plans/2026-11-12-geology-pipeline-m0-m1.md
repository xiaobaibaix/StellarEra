# 地质流水线 M0 + M1 实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 在 `Src/Geo/` 平行建起地质流水线的核心数据结构、烘焙骨架、阶段 1（圈层）与体素适配器，挂到独立测试星球上验证，并用原型定出材质承载方案。

**Architecture:** 烘焙期（`Geo/Bake`，可慢、可虚函数）→ 不可变快照（`Geo/Snapshot`，无 UObject）→ 求值期 inline 核。`Geo/Voxel` 只做 VoxelFree 适配，`Geo/Assets` 只放 UObject 资产。旧的 `Terrain/` `Hydrology/` `Minerals/` 在 M0–M1 期间一行不改。

**Tech Stack:** UE 5.8.3 C++、VoxelFree 插件（只读）、UE Automation Test、编辑器 Python。

**Spec:** `docs/superpowers/specs/2026-11-12-geology-pipeline-design.md`（附录 `docs/superpowers/specs/2026-11-12-geology-pipeline/`）

## Global Constraints

- 星球半径 **R = 40000 体素**（UniverseMap 实际值）；`VoxelSize` 由 Task 1 读资产确认，计划默认 100 cm。
- 目录依赖：`Geo/Core`、`Geo/Snapshot` 只许 include Core / Math，**禁止** Engine、CoreUObject、Voxel；`Geo/Bake` 另可 include `Geo/Core` `Geo/Snapshot`，禁止 World / Actor / Component / Voxel；`Geo/Assets` 禁止 Voxel。
- 求值期代码（`Snapshot/PlanetShapeEval.h`、`Snapshot/PlanetMaterialEval.h`、`Core` 的采样函数）：无虚函数、无 `new`、无锁、无日志。
- 确定性：同一构建下同一 `FPlanetRecipeConfig` 烘焙出的快照 `ContentHash` 必须相同；随机只来自 `FGeoRng`，禁用 `FMath::Rand` / `FMath::FRand`。
- 测试放 `Source/StellarEraPlanet/Src/Tests/`，名字前缀 `StellarEra.Planet.Geo.`，标志 `EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter`，整个文件包在 `#if WITH_DEV_AUTOMATION_TESTS` 里。
- 含中文的 `.h/.cpp` 存 **UTF-8 with BOM**；注释只写「为什么」，单处 1–3 行。
- 不改 `Plugins/VoxelFree`；不改旧 `Terrain/` `Hydrology/` `Minerals/`。
- 提交必须带 pathspec：`git add -- <paths> && git commit -m "…" -- <paths>`；提交信息末尾一行 `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`。
- **构建**（先关编辑器；cwd 在项目外）：
  ```bash
  cd /f/workspace && "D:/Application/Epic Games/UE_5.8/Engine/Binaries/ThirdParty/DotNet/10.0/win-x64/dotnet.exe" \
    "D:/Application/Epic Games/UE_5.8/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.dll" \
    StellarEraEditor Win64 Development -Project="F:/workspace/ue/StellarEra/StellarEra.uproject" -WaitMutex -FromMsBuild 2>&1 | tail -5
  ```
  期望 `Result: Succeeded`。报 `Live Coding is active` 说明编辑器没关。
- **测试**（`<Filter>` 换成任务给的名字；编辑器开着也能跑）：
  ```bash
  "D:/Application/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "F:/workspace/ue/StellarEra/StellarEra.uproject" \
    -ExecCmds="Automation RunTests <Filter>" -TestExit="Automation Test Queue Empty" \
    -unattended -nopause -nullrhi -nosplash -stdout -abslog="$TEMP/geo_test.log" > /dev/null 2>&1; \
  grep -a "Test Completed" "$TEMP/geo_test.log"
  ```
  期望每行 `Result={Success}`。失败时 `grep -a "Error" "$TEMP/geo_test.log"` 看原因。新测试文件必须先构建再跑，否则测试名不存在、输出为空（空输出 ≠ 通过）。

---

## 文件结构

| 文件 | 职责 |
|---|---|
| `Src/Geo/Core/GeoRng.h` | 确定性哈希；按 (Seed, Stage, Purpose) 派生的随机流 |
| `Src/Geo/Core/GeoNoise.h` | 确定性值噪声与 fBm（不用插件的 FastNoise） |
| `Src/Geo/Core/GeoField2D.h/.cpp` | 6 面 cubemap 标量场：方向↔面坐标、双线性、mip |
| `Src/Geo/Core/GeoPrimitive.h` | 扁平原语结构体 + `switch` 求值与值域（M1 只实现团块） |
| `Src/Geo/Core/GeoPrimitiveIndex.h/.cpp` | 原语 BVH |
| `Src/Geo/Core/GeoLithology.h` | M1 岩性 Id 与调试色（M2 换成目录资产） |
| `Src/Geo/Core/GeoProfile.h` | 径向剖面：深度比例 → 岩性 |
| `Src/Geo/Snapshot/GeoMaterialCodec.h` | `FGeoSample` ↔ 8 字节，全项目唯一编解码点 |
| `Src/Geo/Snapshot/PlanetSnapshot.h/.cpp` | 冻结快照 + 内容哈希 |
| `Src/Geo/Snapshot/PlanetShapeEval.h` | 密度、值域、地表半径（inline） |
| `Src/Geo/Snapshot/PlanetMaterialEval.h` | 单点 `FGeoSample`（inline） |
| `Src/Geo/Bake/PlanetRecipeConfig.h` | 烘焙配方 POD + 哈希 |
| `Src/Geo/Bake/PlanetBakeTypes.h` | 字段位掩码、`FPlanetBakeFields`、上下文、报告 |
| `Src/Geo/Bake/PlanetStage.h` | `IPlanetStage` |
| `Src/Geo/Bake/PlanetBakePipeline.h/.cpp` | 读写检查、顺序执行、冻结 |
| `Src/Geo/Bake/Stages/Stage01Sphere.h/.cpp` | 阶段 1 |
| `Src/Geo/Assets/PlanetRecipe.h/.cpp` | `UPlanetRecipe` DataAsset → `ToConfig()` |
| `Src/Geo/Voxel/PlanetGeoGenerator.h/.cpp` | `UPlanetGeoGenerator` + 生成器实例 |
| `Src/Geo/Voxel/GeoTestPlanetActor.h/.cpp` | 独立测试星球 |
| `Src/Tests/PlanetGeo*Tests.cpp` | 各任务测试 |

任务顺序：Task 1 测量；Task 2–6 是 M0 的核心数据结构；Task 7–13 是 M1；Task 14 是 M0 的材质原型（它要用 M1 的测试星球，所以排在后面）；Task 15 收尾。

## Task 1：确认 R 与 VoxelSize，按 R=40000 重算附录预算（M0，纯文档）

**Files:**
- Modify: `docs/superpowers/specs/2026-11-12-geology-pipeline-design.md`（§3 Q5′ 一行）
- Modify: `docs/superpowers/specs/2026-11-12-geology-pipeline/00-pipeline.md`（§3 顶部插入新小节）

**Interfaces:** Produces：`VoxelSize` 实测值，后续任务的默认值 `VoxelSizeCm` 以它为准。

- [ ] **Step 1：读资产值**（编辑器开着走 MCP `execute_python_code`，关着走无头）

  脚本放 `$TEMP/geo_m0/read_planet.py`：
  ```python
  import unreal
  cls = unreal.EditorAssetLibrary.load_blueprint_class('/Game/Code/Planet/BP_VoxelPlanet')
  cdo = unreal.get_default_object(cls)
  unreal.log('GEO_M0 Radius=%s VoxelSize=%s' % (cdo.get_editor_property('radius'), cdo.get_editor_property('voxel_size')))
  ```
  无头：
  ```bash
  "D:/Application/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "F:/workspace/ue/StellarEra/StellarEra.uproject" \
    -run=pythonscript -script="$TEMP/geo_m0/read_planet.py" -unattended -nullrhi -abslog="$TEMP/geo_m0.log" > /dev/null 2>&1
  grep -a "GEO_M0" "$TEMP/geo_m0.log"
  ```
  再从运行日志取实例值（关卡实例可能覆盖 CDO）：
  ```bash
  grep -a "星球半径 [0-9]* 体素" /f/workspace/ue/StellarEra/Saved/Logs/StellarEra.log | tail -1
  ```
  期望日志里是 `星球半径 40000 体素`。CDO 的 `Radius` 若与日志不同，以日志（关卡实例）为准。

- [ ] **Step 2：写回规格 §3 Q5′**：把「`VoxelSize` 按 100 cm…待 M0 读资产确认」改成「`VoxelSize = <Step 1 的值> cm`（M0 读 CDO 确认）」。

- [ ] **Step 3：在 `00-pipeline.md` 的 `## 3. 分辨率预算` 标题下第一行插入下面这节**（`VoxelSize ≠ 100` 时把「体素」列按 `100 / VoxelSize` 等比换算，米数不变）：

  ```markdown
  ### 3.0 实际星球 R = 40000 体素（2026-11-12，以本节为准）

  §3.1–3.4 的 `clamp` 公式按 R = 100–1000 推导，在 R = 40000 下全部撞上限，失效。大星球改用物理尺寸，再换算体素：

  | 特征 | 米 | 体素（1 m/体素） | 说明 |
  |---|---|---|---|
  | 陆台高 `A_up` | 120 | 120 | 地球约 800 m，按 1/6.5 竖向缩 |
  | 洋盆深 `A_down` | 240 | 240 | 保持 2·A_up |
  | 山带单层幅度 `A_mtn` | 300 | 300 | 峰值约 2·A_mtn |
  | 造山带宽 `W_belt` | 3000 | 3000 | |
  | 火山锥底径 / 高 | 1500 / 400 | 1500 / 400 | |
  | 弧火山间距 | 3000 | 3000 | |
  | 斑岩体宽 | 150 | 150 | |
  | 海沟宽 | 800 | 800 | |
  | 宏观场 `faceRes` | — | 1024 | cell ≈ πR/2N ≈ 61 体素；只承载宏观量，细节在求值期解析生成（规格 §12） |
  | 早退累计带宽 | ≈ 1100 | ≈ 1100 | ≈ 2.8% R，远低于 15% 准入线 |
  ```

- [ ] **Step 4：提交**
  ```bash
  cd /f/workspace/ue/StellarEra && P="docs/superpowers/specs/2026-11-12-geology-pipeline-design.md docs/superpowers/specs/2026-11-12-geology-pipeline/00-pipeline.md"
  git add -- $P && git commit -m "docs(地质): M0 确认 VoxelSize，按 R=40000 补物理尺寸预算

  Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>" -- $P
  ```

- [ ] **Step 5：存回归基线**：跑一次全部项目测试（Filter = `StellarEra`），日志另存为 `$TEMP/geo_baseline.log`，并保存失败清单：
  `grep -a "Test Completed" "$TEMP/geo_baseline.log" | grep -v "Success" > "$TEMP/geo_baseline_fail.txt"`。Task 15 用它对比。

## Task 2：确定性随机与噪声（M0）

**Files:**
- Create: `Source/StellarEraPlanet/Src/Geo/Core/GeoRng.h`
- Create: `Source/StellarEraPlanet/Src/Geo/Core/GeoNoise.h`
- Test: `Source/StellarEraPlanet/Src/Tests/PlanetGeoRngNoiseTests.cpp`

**Interfaces:**
- Produces：`GeoHash::Mix64(uint64)`、`GeoHash::Combine(uint64, uint64)`、`GeoHash::Float(float) -> uint64`、`constexpr GeoHash::Str(const char*)`；
  `FGeoRng::Derive(uint64 Seed, uint32 Stage, uint64 Purpose)`、`NextU64()`、`NextFloat()` ∈ [0,1)、`Range(float, float)`；
  `GeoNoise::Value3(uint64 Seed, const FVector3f&)` ∈ [-1,1]、`GeoNoise::Fbm3(uint64 Seed, FVector3f P, int32 Octaves, float Lacunarity = 2, float Gain = 0.5)` ∈ [-1,1]。

- [ ] **Step 1：写失败测试** `PlanetGeoRngNoiseTests.cpp`

```cpp
﻿// 确定性是存档只存配方的前提：同种子必须同流，噪声必须连续且有界。
#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "GeoRng.h"
#include "GeoNoise.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGeoRngNoiseTest, "StellarEra.Planet.Geo.RngNoise",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGeoRngNoiseTest::RunTest(const FString& Parameters)
{
	FGeoRng A = FGeoRng::Derive(1337, 1, GeoHash::Str("test"));
	FGeoRng B = FGeoRng::Derive(1337, 1, GeoHash::Str("test"));
	FGeoRng C = FGeoRng::Derive(1337, 2, GeoHash::Str("test"));
	bool bSame = true, bDiffer = false, bInRange = true;
	for (int32 i = 0; i < 1000; ++i)
	{
		const uint64 a = A.NextU64(), b = B.NextU64(), c = C.NextU64();
		bSame &= (a == b);
		bDiffer |= (a != c);
		const float F = A.NextFloat();
		B.NextFloat();
		bInRange &= (F >= 0.f && F < 1.f);
	}
	TestTrue(TEXT("同种子同阶段同用途 → 同一条流"), bSame);
	TestTrue(TEXT("换阶段 → 不同的流"), bDiffer);
	TestTrue(TEXT("NextFloat ∈ [0,1)"), bInRange);

	FGeoRng P = FGeoRng::Derive(7, 0, 0);
	float MinV = 1e9f, MaxV = -1e9f, MaxJump = 0.f, MinF = 1e9f, MaxF = -1e9f;
	bool bDeterministic = true;
	for (int32 i = 0; i < 5000; ++i)
	{
		const FVector3f X(P.Range(-50.f, 50.f), P.Range(-50.f, 50.f), P.Range(-50.f, 50.f));
		const float V = GeoNoise::Value3(99, X);
		bDeterministic &= (V == GeoNoise::Value3(99, X));
		MinV = FMath::Min(MinV, V);
		MaxV = FMath::Max(MaxV, V);
		MaxJump = FMath::Max(MaxJump, FMath::Abs(V - GeoNoise::Value3(99, X + FVector3f(1e-3f, 0.f, 0.f))));
		const float Fb = GeoNoise::Fbm3(99, X, 4);
		MinF = FMath::Min(MinF, Fb);
		MaxF = FMath::Max(MaxF, Fb);
	}
	TestTrue(TEXT("Value3 确定"), bDeterministic);
	TestTrue(TEXT("Value3 ∈ [-1,1]"), MinV >= -1.f && MaxV <= 1.f);
	TestTrue(TEXT("Value3 有实际起伏"), MaxV - MinV > 1.f);
	TestTrue(TEXT("Value3 连续（1e-3 步长跳变 < 0.01）"), MaxJump < 0.01f);
	TestTrue(TEXT("Fbm3 ∈ [-1,1]"), MinF >= -1.f && MaxF <= 1.f);
	return true;
}

#endif
```

- [ ] **Step 2：构建，确认因缺头文件而失败**：跑 Global Constraints 的构建命令，期望 `cannot open include file 'GeoRng.h'`。

- [ ] **Step 3：实现 `GeoRng.h`**

```cpp
﻿// 确定性哈希与随机流。烘焙期一切随机都从这里派生，禁止 FMath::Rand（全局状态，跨线程不确定）。
#pragma once
#include "CoreMinimal.h"

namespace GeoHash
{
	/** splitmix64 终结器：雪崩好、无状态。 */
	FORCEINLINE uint64 Mix64(uint64 X)
	{
		X += 0x9E3779B97F4A7C15ull;
		X = (X ^ (X >> 30)) * 0xBF58476D1CE4E5B9ull;
		X = (X ^ (X >> 27)) * 0x94D049BB133111EBull;
		return X ^ (X >> 31);
	}
	FORCEINLINE uint64 Combine(uint64 A, uint64 B)
	{
		return Mix64(A ^ (B + 0x9E3779B97F4A7C15ull + (A << 6) + (A >> 2)));
	}
	/** 按位哈希 float：-0 与 +0 不同是可接受的，烘焙产物里两者不会混用。 */
	FORCEINLINE uint64 Float(float V)
	{
		uint32 Bits;
		FMemory::Memcpy(&Bits, &V, sizeof(Bits));
		return Mix64(Bits);
	}
	/** FNV-1a，编译期把用途名换成 64 位标签。 */
	constexpr uint64 Str(const char* S)
	{
		uint64 H = 1469598103934665603ull;
		while (*S) { H = (H ^ uint8(*S++)) * 1099511628211ull; }
		return H;
	}
}

struct FGeoRng
{
	uint64 State = 0;

	static FGeoRng Derive(uint64 Seed, uint32 Stage, uint64 Purpose)
	{
		FGeoRng R;
		R.State = GeoHash::Combine(GeoHash::Combine(GeoHash::Mix64(Seed), Stage), Purpose);
		return R;
	}
	uint64 NextU64() { State += 0x9E3779B97F4A7C15ull; return GeoHash::Mix64(State); }
	/** 取高 24 位：float 尾数只有 24 位，多取会出现 1.0。 */
	float NextFloat() { return float(NextU64() >> 40) * (1.f / 16777216.f); }
	float Range(float Lo, float Hi) { return Lo + (Hi - Lo) * NextFloat(); }
};
```

- [ ] **Step 4：实现 `GeoNoise.h`**

```cpp
﻿// 确定性值噪声。不用插件的 FastNoise：Core 目录禁止依赖 Voxel，且要能逐位复现。
#pragma once
#include "CoreMinimal.h"
#include "GeoRng.h"

namespace GeoNoise
{
	FORCEINLINE float Lattice(uint64 Seed, int32 X, int32 Y, int32 Z)
	{
		uint64 H = GeoHash::Combine(Seed, uint64(uint32(X)) | (uint64(uint32(Y)) << 32));
		H = GeoHash::Combine(H, uint64(uint32(Z)));
		return float(H >> 40) * (2.f / 16777216.f) - 1.f;
	}
	/** 五次缓动：一阶、二阶导在格点处为 0，法线不出折痕。 */
	FORCEINLINE float Fade(float T) { return T * T * T * (T * (T * 6.f - 15.f) + 10.f); }

	/** ∈ [-1,1]。 */
	FORCEINLINE float Value3(uint64 Seed, const FVector3f& P)
	{
		const int32 X0 = FMath::FloorToInt(P.X), Y0 = FMath::FloorToInt(P.Y), Z0 = FMath::FloorToInt(P.Z);
		const float TX = Fade(P.X - float(X0)), TY = Fade(P.Y - float(Y0)), TZ = Fade(P.Z - float(Z0));
		const float X00 = FMath::Lerp(Lattice(Seed, X0, Y0, Z0),         Lattice(Seed, X0 + 1, Y0, Z0), TX);
		const float X10 = FMath::Lerp(Lattice(Seed, X0, Y0 + 1, Z0),     Lattice(Seed, X0 + 1, Y0 + 1, Z0), TX);
		const float X01 = FMath::Lerp(Lattice(Seed, X0, Y0, Z0 + 1),     Lattice(Seed, X0 + 1, Y0, Z0 + 1), TX);
		const float X11 = FMath::Lerp(Lattice(Seed, X0, Y0 + 1, Z0 + 1), Lattice(Seed, X0 + 1, Y0 + 1, Z0 + 1), TX);
		return FMath::Lerp(FMath::Lerp(X00, X10, TY), FMath::Lerp(X01, X11, TY), TZ);
	}

	/** 按振幅和归一化，∈ [-1,1]；每个 octave 换种子，避免各层格点对齐。 */
	FORCEINLINE float Fbm3(uint64 Seed, FVector3f P, int32 Octaves, float Lacunarity = 2.f, float Gain = 0.5f)
	{
		float Sum = 0.f, Amp = 1.f, Norm = 0.f;
		for (int32 i = 0; i < Octaves; ++i)
		{
			Sum += Amp * Value3(GeoHash::Combine(Seed, uint64(i)), P);
			Norm += Amp;
			Amp *= Gain;
			P *= Lacunarity;
		}
		return Norm > 0.f ? Sum / Norm : 0.f;
	}
}
```

- [ ] **Step 5：构建并跑测试** `StellarEra.Planet.Geo.RngNoise`，期望 `Result={Success}`。

- [ ] **Step 6：提交**
  ```bash
  cd /f/workspace/ue/StellarEra && P="Source/StellarEraPlanet/Src/Geo/Core/GeoRng.h Source/StellarEraPlanet/Src/Geo/Core/GeoNoise.h Source/StellarEraPlanet/Src/Tests/PlanetGeoRngNoiseTests.cpp"
  git add -- $P && git commit -m "feat(地质): 确定性随机流与值噪声

  Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>" -- $P
  ```

## Task 3：球面场 `FGeoField2D`（M0）

**Files:**
- Create: `Source/StellarEraPlanet/Src/Geo/Core/GeoField2D.h`、`GeoField2D.cpp`
- Test: `Source/StellarEraPlanet/Src/Tests/PlanetGeoField2DTests.cpp`

**Interfaces:**
- Consumes：`GeoHash::Combine`、`GeoHash::Float`（Task 2）。
- Produces：`FGeoField2D`：`Init(int32 Res)`、`int32 Res`、`MipRes(int32 Mip)`、`NumMips()`、`At(Face, X, Y, Mip = 0)`（const 与非 const）、
  `static DirToFace(const FVector3f&, int32& Face, float& U, float& V)`、`static FaceToDir(int32 Face, float U, float V)`、`TexelDir(Face, X, Y)`、
  `Fill(TFunctionRef<float(const FVector3f&)>, bool bSingleThread = false)`、`BuildMips(int32 Count)`、`Sample(const FVector3f& Dir, int32 Mip = 0)`、`Hash()`、`GetAllocatedSize()`。
  面编号 0..5 = +X −X +Y −Y +Z −Z；U、V ∈ [-1,1]。

- [ ] **Step 1：写失败测试** `PlanetGeoField2DTests.cpp`

```cpp
﻿// 球面场是所有宏观量的载体：方向换算必须可逆，跨面采样必须连续，否则地形在 cubemap 接缝处裂开。
#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "GeoField2D.h"
#include "GeoRng.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGeoField2DTest, "StellarEra.Planet.Geo.Field2D",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGeoField2DTest::RunTest(const FString& Parameters)
{
	FGeoRng Rng = FGeoRng::Derive(1, 0, 0);
	const auto RandomDir = [&Rng]()
	{
		const float Z = Rng.Range(-1.f, 1.f), Phi = Rng.Range(0.f, 2.f * PI), S = FMath::Sqrt(1.f - Z * Z);
		return FVector3f(S * FMath::Cos(Phi), S * FMath::Sin(Phi), Z);
	};

	float MaxRoundTrip = 0.f;
	for (int32 i = 0; i < 2000; ++i)
	{
		const FVector3f D = RandomDir();
		int32 Face; float U, V;
		FGeoField2D::DirToFace(D, Face, U, V);
		MaxRoundTrip = FMath::Max(MaxRoundTrip, (FGeoField2D::FaceToDir(Face, U, V) - D).Size());
	}
	TestTrue(TEXT("方向 → 面坐标 → 方向 可逆"), MaxRoundTrip < 1e-5f);

	const auto Linear = [](const FVector3f& D) { return D.X + 2.f * D.Y + 3.f * D.Z; };
	FGeoField2D Field;
	Field.Init(64);
	Field.Fill(Linear);
	float MaxErr = 0.f;
	for (int32 i = 0; i < 2000; ++i)
	{
		const FVector3f D = RandomDir();
		MaxErr = FMath::Max(MaxErr, FMath::Abs(Field.Sample(D) - Linear(D)));
	}
	TestTrue(TEXT("64² 双线性误差 < 0.15（边缘钳到边缘 texel，误差约半格梯度）"), MaxErr < 0.15f);

	const FVector3f A = FVector3f(1.f, 0.999f, 0.3f).GetSafeNormal(), B = FVector3f(0.999f, 1.f, 0.3f).GetSafeNormal();
	TestTrue(TEXT("+X / +Y 接缝两侧连续（< 0.15）"), FMath::Abs(Field.Sample(A) - Field.Sample(B)) < 0.15f);

	FGeoField2D Same, SingleThread;
	Same.Init(64); Same.Fill(Linear);
	SingleThread.Init(64); SingleThread.Fill(Linear, /*bSingleThread=*/true);
	TestTrue(TEXT("同输入同哈希"), Field.Hash() == Same.Hash());
	TestTrue(TEXT("单线程与并行填充逐位一致"), Field.Hash() == SingleThread.Hash());

	FGeoField2D Const;
	Const.Init(32); Const.Fill([](const FVector3f&) { return 7.f; }); Const.BuildMips(3);
	TestEqual(TEXT("mip 数"), Const.NumMips(), 3);
	TestEqual(TEXT("mip2 分辨率"), Const.MipRes(2), 8);
	TestEqual(TEXT("常量场的 mip2 仍为常量"), Const.Sample(RandomDir(), 2), 7.f);
	return true;
}

#endif
```

- [ ] **Step 2：构建**，期望因缺 `GeoField2D.h` 失败。

- [ ] **Step 3：实现 `GeoField2D.h`**

```cpp
﻿// 6 面 cubemap 标量场。面编号 0..5 = +X −X +Y −Y +Z −Z；面内 U、V ∈ [-1,1]，texel 中心在 ((i+0.5)/Res)*2-1。
#pragma once
#include "CoreMinimal.h"

class STELLARERAPLANET_API FGeoField2D
{
public:
	void Init(int32 InRes);
	int32 Res = 0;
	int32 NumMips() const { return Levels.Num(); }
	int32 MipRes(int32 Mip) const { return FMath::Max(1, Res >> Mip); }

	float& At(int32 Face, int32 X, int32 Y, int32 Mip = 0) { const int32 R = MipRes(Mip); return Levels[Mip][(Face * R + Y) * R + X]; }
	float At(int32 Face, int32 X, int32 Y, int32 Mip = 0) const { const int32 R = MipRes(Mip); return Levels[Mip][(Face * R + Y) * R + X]; }

	static void DirToFace(const FVector3f& Dir, int32& OutFace, float& OutU, float& OutV);
	static FVector3f FaceToDir(int32 Face, float U, float V);
	FVector3f TexelDir(int32 Face, int32 X, int32 Y) const;

	/** 每个 texel 只写自己，单线程与并行逐位一致（确定性前提）。 */
	void Fill(TFunctionRef<float(const FVector3f&)> Func, bool bSingleThread = false);
	/** Count 含第 0 级；2×2 盒式下采样。 */
	void BuildMips(int32 Count);

	/** 面内双线性，越界钳到边缘 texel：接缝误差约半个 texel，M1 的宏观量够用。 */
	float Sample(const FVector3f& Dir, int32 Mip = 0) const;

	uint64 Hash() const;
	SIZE_T GetAllocatedSize() const;

private:
	TArray<TArray<float>> Levels;
};
```

- [ ] **Step 4：实现 `GeoField2D.cpp`**

```cpp
﻿#include "GeoField2D.h"
#include "Async/ParallelFor.h"
#include "GeoRng.h"

void FGeoField2D::Init(int32 InRes)
{
	Res = FMath::Max(1, InRes);
	Levels.SetNum(1);
	Levels[0].SetNumZeroed(6 * Res * Res);
}

void FGeoField2D::DirToFace(const FVector3f& D, int32& OutFace, float& OutU, float& OutV)
{
	const float AX = FMath::Abs(D.X), AY = FMath::Abs(D.Y), AZ = FMath::Abs(D.Z);
	if (AX >= AY && AX >= AZ) { OutFace = D.X > 0.f ? 0 : 1; OutU = D.Y / AX; OutV = D.Z / AX; }
	else if (AY >= AZ)        { OutFace = D.Y > 0.f ? 2 : 3; OutU = D.X / AY; OutV = D.Z / AY; }
	else                      { OutFace = D.Z > 0.f ? 4 : 5; OutU = D.X / AZ; OutV = D.Y / AZ; }
}

FVector3f FGeoField2D::FaceToDir(int32 Face, float U, float V)
{
	const float S = (Face & 1) ? -1.f : 1.f;
	switch (Face >> 1)
	{
	case 0:  return FVector3f(S, U, V).GetSafeNormal();
	case 1:  return FVector3f(U, S, V).GetSafeNormal();
	default: return FVector3f(U, V, S).GetSafeNormal();
	}
}

FVector3f FGeoField2D::TexelDir(int32 Face, int32 X, int32 Y) const
{
	const float U = (float(X) + 0.5f) / float(Res) * 2.f - 1.f;
	const float V = (float(Y) + 0.5f) / float(Res) * 2.f - 1.f;
	return FaceToDir(Face, U, V);
}

void FGeoField2D::Fill(TFunctionRef<float(const FVector3f&)> Func, bool bSingleThread)
{
	Levels.SetNum(1);
	ParallelFor(6 * Res, [this, &Func](int32 Row)
	{
		const int32 Face = Row / Res, Y = Row % Res;
		for (int32 X = 0; X < Res; ++X) { At(Face, X, Y) = Func(TexelDir(Face, X, Y)); }
	}, bSingleThread ? EParallelForFlags::ForceSingleThread : EParallelForFlags::None);
}

void FGeoField2D::BuildMips(int32 Count)
{
	Levels.SetNum(FMath::Max(1, Count));
	for (int32 Mip = 1; Mip < Levels.Num(); ++Mip)
	{
		const int32 R = MipRes(Mip), P = MipRes(Mip - 1);
		Levels[Mip].SetNumUninitialized(6 * R * R);
		for (int32 Face = 0; Face < 6; ++Face)
		for (int32 Y = 0; Y < R; ++Y)
		for (int32 X = 0; X < R; ++X)
		{
			const int32 X0 = FMath::Min(2 * X, P - 1), X1 = FMath::Min(2 * X + 1, P - 1);
			const int32 Y0 = FMath::Min(2 * Y, P - 1), Y1 = FMath::Min(2 * Y + 1, P - 1);
			At(Face, X, Y, Mip) = 0.25f * (At(Face, X0, Y0, Mip - 1) + At(Face, X1, Y0, Mip - 1)
				+ At(Face, X0, Y1, Mip - 1) + At(Face, X1, Y1, Mip - 1));
		}
	}
}

float FGeoField2D::Sample(const FVector3f& Dir, int32 Mip) const
{
	Mip = FMath::Clamp(Mip, 0, Levels.Num() - 1);
	const int32 R = MipRes(Mip);
	int32 Face; float U, V;
	DirToFace(Dir, Face, U, V);
	const float FX = (U + 1.f) * 0.5f * float(R) - 0.5f, FY = (V + 1.f) * 0.5f * float(R) - 0.5f;
	const int32 X0 = FMath::FloorToInt(FX), Y0 = FMath::FloorToInt(FY);
	const float TX = FX - float(X0), TY = FY - float(Y0);
	const auto C = [R](int32 I) { return FMath::Clamp(I, 0, R - 1); };
	const float A = FMath::Lerp(At(Face, C(X0), C(Y0), Mip), At(Face, C(X0 + 1), C(Y0), Mip), TX);
	const float B = FMath::Lerp(At(Face, C(X0), C(Y0 + 1), Mip), At(Face, C(X0 + 1), C(Y0 + 1), Mip), TX);
	return FMath::Lerp(A, B, TY);
}

uint64 FGeoField2D::Hash() const
{
	uint64 H = GeoHash::Combine(uint64(Res), uint64(Levels.Num()));
	for (const TArray<float>& Level : Levels)
	{
		for (const float V : Level) { H = GeoHash::Combine(H, GeoHash::Float(V)); }
	}
	return H;
}

SIZE_T FGeoField2D::GetAllocatedSize() const
{
	SIZE_T Size = Levels.GetAllocatedSize();
	for (const TArray<float>& Level : Levels) { Size += Level.GetAllocatedSize(); }
	return Size;
}
```

- [ ] **Step 5：构建并跑** `StellarEra.Planet.Geo.Field2D`，期望 `Result={Success}`。

- [ ] **Step 6：提交**
  ```bash
  cd /f/workspace/ue/StellarEra && P="Source/StellarEraPlanet/Src/Geo/Core/GeoField2D.h Source/StellarEraPlanet/Src/Geo/Core/GeoField2D.cpp Source/StellarEraPlanet/Src/Tests/PlanetGeoField2DTests.cpp"
  git add -- $P && git commit -m "feat(地质): 6 面 cubemap 球面场

  Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>" -- $P
  ```

## Task 4：形态原语与 BVH（M0）

**Files:**
- Create: `Source/StellarEraPlanet/Src/Geo/Core/GeoPrimitive.h`
- Create: `Source/StellarEraPlanet/Src/Geo/Core/GeoPrimitiveIndex.h`、`GeoPrimitiveIndex.cpp`
- Test: `Source/StellarEraPlanet/Src/Tests/PlanetGeoPrimitiveTests.cpp`

**Interfaces:**
- Consumes：`GeoHash`、`FGeoRng`（Task 2）。
- Produces：`FGeoRange { float Lo, Hi; static Point(float); Contains(float) }`；`EGeoPrimitiveKind::Blob`；
  `FGeoPrimitive`（`Kind`、`MineralId`、`LithologyId`、`Priority`、`Center`、`Radii`、`Hard`、`Bounds`；`static MakeBlob(const FVector3f& Center, const FVector3f& Radii, float Hard)`）；
  `GeoPrimitiveEvaluate(const FGeoPrimitive&, const FVector3f&) -> float ∈ [0,1]`；`GeoPrimitiveRange(const FGeoPrimitive&, const FBox3f&) -> FGeoRange`；
  `FGeoPrimitiveIndex`：`Build(TArray<FGeoPrimitive>)`、`GetPrimitives()`、`IsEmpty()`、模板 `QueryPoint(P, Visit(int32))`、`QueryBox(Box, Visit(int32))`、`Hash()`、`GetAllocatedSize()`。
  M1 只实现团块；其余 6 种原语在用到它们的里程碑里加 `case`。

- [ ] **Step 1：写失败测试** `PlanetGeoPrimitiveTests.cpp`

```cpp
﻿// BVH 漏掉基元不会报错，只表现为「矿变少」；值域估窄会让插件整块跳过、表面出洞。两条都只能靠对拍守住。
#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "GeoPrimitive.h"
#include "GeoPrimitiveIndex.h"
#include "GeoRng.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGeoPrimitiveTest, "StellarEra.Planet.Geo.Primitive",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGeoPrimitiveTest::RunTest(const FString& Parameters)
{
	FGeoRng Rng = FGeoRng::Derive(42, 0, 0);
	const auto RandomPoint = [&Rng](float Extent)
	{
		return FVector3f(Rng.Range(-Extent, Extent), Rng.Range(-Extent, Extent), Rng.Range(-Extent, Extent));
	};

	const FGeoPrimitive Blob = FGeoPrimitive::MakeBlob(FVector3f(10.f, 0.f, 0.f), FVector3f(4.f, 4.f, 2.f), 0.05f);
	TestEqual(TEXT("团块中心强度 1"), GeoPrimitiveEvaluate(Blob, FVector3f(10.f, 0.f, 0.f)), 1.f);
	TestEqual(TEXT("团块外强度 0"), GeoPrimitiveEvaluate(Blob, FVector3f(10.f, 0.f, 2.5f)), 0.f);

	bool bRangeCovers = true;
	for (int32 i = 0; i < 10000; ++i)
	{
		const FVector3f C = RandomPoint(16.f);
		const float H = Rng.Range(0.5f, 6.f);
		const FBox3f Box(C - FVector3f(H), C + FVector3f(H));
		const FGeoRange R = GeoPrimitiveRange(Blob, Box);
		const FVector3f P(Rng.Range(Box.Min.X, Box.Max.X), Rng.Range(Box.Min.Y, Box.Max.Y), Rng.Range(Box.Min.Z, Box.Max.Z));
		bRangeCovers &= R.Contains(GeoPrimitiveEvaluate(Blob, P));
	}
	TestTrue(TEXT("值域覆盖盒内任意采样"), bRangeCovers);

	TArray<FGeoPrimitive> Prims;
	for (int32 i = 0; i < 1000; ++i)
	{
		const FVector3f Radii(Rng.Range(1.f, 8.f), Rng.Range(1.f, 8.f), Rng.Range(1.f, 8.f));
		Prims.Add(FGeoPrimitive::MakeBlob(RandomPoint(500.f), Radii, 0.05f));
	}
	FGeoPrimitiveIndex Index;
	Index.Build(Prims);

	bool bMatch = true;
	int32 Hits = 0;
	for (int32 i = 0; i < 1000; ++i)
	{
		const FVector3f P = (i % 2 == 0) ? Prims[i].Center : RandomPoint(500.f);
		TArray<int32> Brute, Fast;
		for (int32 k = 0; k < Prims.Num(); ++k) { if (Prims[k].Bounds.IsInsideOrOn(P)) { Brute.Add(k); } }
		Index.QueryPoint(P, [&Fast](int32 K) { Fast.Add(K); });
		Brute.Sort(); Fast.Sort();
		bMatch &= (Brute == Fast);
		Hits += Brute.Num();
	}
	TestTrue(TEXT("BVH 点查询 == 暴力遍历"), bMatch);
	TestTrue(TEXT("对拍样本里确实有命中"), Hits >= 500);

	FGeoPrimitiveIndex Again;
	Again.Build(Prims);
	TestTrue(TEXT("同输入同哈希"), Index.Hash() == Again.Hash());
	return true;
}

#endif
```

- [ ] **Step 2：构建**，期望因缺 `GeoPrimitive.h` 失败。

- [ ] **Step 3：实现 `GeoPrimitive.h`**

```cpp
﻿// 形态原语：扁平结构体 + switch，进快照、在体素线程上求值，所以不用虚函数。
// M1 只有团块（轴对齐椭球）；其余 6 种原语在用到它的里程碑加 case。
#pragma once
#include "CoreMinimal.h"

struct FGeoRange
{
	float Lo = 0.f;
	float Hi = 0.f;
	static FGeoRange Point(float V) { return { V, V }; }
	bool Contains(float V) const { return V >= Lo && V <= Hi; }
};

enum class EGeoPrimitiveKind : uint8
{
	Blob,
};

struct FGeoPrimitive
{
	EGeoPrimitiveKind Kind = EGeoPrimitiveKind::Blob;
	uint8 MineralId = 0;
	uint8 LithologyId = 0;
	uint8 Grade = 0;
	uint8 Priority = 0;
	FVector3f Center = FVector3f::ZeroVector;
	FVector3f Radii = FVector3f::OneVector;
	/** 软边宽度占半径比例：0.02–0.05 是硬接触（侵入体），0.2 是浸染状软边。 */
	float Hard = 0.05f;
	FBox3f Bounds = FBox3f(ForceInit);

	static FGeoPrimitive MakeBlob(const FVector3f& InCenter, const FVector3f& InRadii, float InHard)
	{
		FGeoPrimitive P;
		P.Kind = EGeoPrimitiveKind::Blob;
		P.Center = InCenter;
		P.Radii = InRadii;
		P.Hard = InHard;
		P.Bounds = FBox3f(InCenter - InRadii, InCenter + InRadii);
		return P;
	}
};

/** 归一化距离 D → 强度：D ≤ 1−Hard 为 1，D ≥ 1 为 0，单调不增（值域推导依赖单调）。 */
FORCEINLINE float GeoBlobFalloff(float D, float Hard)
{
	return 1.f - FMath::SmoothStep(1.f - Hard, 1.f, D);
}

FORCEINLINE float GeoPrimitiveEvaluate(const FGeoPrimitive& P, const FVector3f& X)
{
	switch (P.Kind)
	{
	case EGeoPrimitiveKind::Blob:
		return GeoBlobFalloff(((X - P.Center) / P.Radii).Size(), P.Hard);
	}
	return 0.f;
}

FORCEINLINE FGeoRange GeoPrimitiveRange(const FGeoPrimitive& P, const FBox3f& Box)
{
	switch (P.Kind)
	{
	case EGeoPrimitiveKind::Blob:
	{
		// 盒子变到椭球的单位球空间仍是轴对齐盒：最近点取钳位，最远点取离原点远的那一角。
		const FVector3f Lo = (Box.Min - P.Center) / P.Radii;
		const FVector3f Hi = (Box.Max - P.Center) / P.Radii;
		const FVector3f Near(FMath::Clamp(0.f, Lo.X, Hi.X), FMath::Clamp(0.f, Lo.Y, Hi.Y), FMath::Clamp(0.f, Lo.Z, Hi.Z));
		const FVector3f Far(FMath::Max(FMath::Abs(Lo.X), FMath::Abs(Hi.X)),
			FMath::Max(FMath::Abs(Lo.Y), FMath::Abs(Hi.Y)), FMath::Max(FMath::Abs(Lo.Z), FMath::Abs(Hi.Z)));
		return { GeoBlobFalloff(Far.Size(), P.Hard), GeoBlobFalloff(Near.Size(), P.Hard) };
	}
	}
	return FGeoRange::Point(0.f);
}
```

- [ ] **Step 4：实现 `GeoPrimitiveIndex.h`**

```cpp
﻿// 原语 BVH。烘焙期建、体素线程查；查询只读、无分配（固定深度栈）。
#pragma once
#include "CoreMinimal.h"
#include "GeoPrimitive.h"

class STELLARERAPLANET_API FGeoPrimitiveIndex
{
public:
	void Build(TArray<FGeoPrimitive> InPrims);
	const TArray<FGeoPrimitive>& GetPrimitives() const { return Prims; }
	bool IsEmpty() const { return Prims.Num() == 0; }

	/** Visit(int32) 收到的是 GetPrimitives() 的原始下标。 */
	template <typename TVisit> void QueryPoint(const FVector3f& Point, TVisit&& Visit) const
	{
		QueryImpl([&Point](const FBox3f& B) { return B.IsInsideOrOn(Point); }, Visit);
	}
	template <typename TVisit> void QueryBox(const FBox3f& Box, TVisit&& Visit) const
	{
		QueryImpl([&Box](const FBox3f& B) { return B.Intersect(Box); }, Visit);
	}

	uint64 Hash() const;
	SIZE_T GetAllocatedSize() const;

private:
	struct FNode
	{
		FBox3f Box = FBox3f(ForceInit);
		int32 Left = -1, Right = -1, First = 0, Count = 0;
	};
	TArray<FGeoPrimitive> Prims;
	TArray<int32> Order;
	TArray<FNode> Nodes;

	int32 BuildNode(int32 First, int32 Count);

	template <typename TTest, typename TVisit> void QueryImpl(TTest&& Test, TVisit&& Visit) const
	{
		if (Nodes.Num() == 0) { return; }
		int32 Stack[64];
		int32 Top = 0;
		Stack[Top++] = 0;
		while (Top > 0)
		{
			const FNode& N = Nodes[Stack[--Top]];
			if (!Test(N.Box)) { continue; }
			if (N.Left < 0)
			{
				for (int32 i = N.First; i < N.First + N.Count; ++i)
				{
					if (Test(Prims[Order[i]].Bounds)) { Visit(Order[i]); }
				}
			}
			else
			{
				Stack[Top++] = N.Left;
				Stack[Top++] = N.Right;
			}
		}
	}
};
```

- [ ] **Step 5：实现 `GeoPrimitiveIndex.cpp`**

```cpp
﻿#include "GeoPrimitiveIndex.h"
#include "Algo/Sort.h"
#include "GeoRng.h"

void FGeoPrimitiveIndex::Build(TArray<FGeoPrimitive> InPrims)
{
	Prims = MoveTemp(InPrims);
	Order.SetNumUninitialized(Prims.Num());
	for (int32 i = 0; i < Order.Num(); ++i) { Order[i] = i; }
	Nodes.Reset();
	if (Prims.Num() > 0) { BuildNode(0, Prims.Num()); }
}

int32 FGeoPrimitiveIndex::BuildNode(int32 First, int32 Count)
{
	const int32 Index = Nodes.AddDefaulted();
	FBox3f Box(ForceInit), Centers(ForceInit);
	for (int32 i = First; i < First + Count; ++i)
	{
		Box += Prims[Order[i]].Bounds;
		Centers += Prims[Order[i]].Bounds.GetCenter();
	}
	Nodes[Index].Box = Box;
	if (Count <= 4)
	{
		Nodes[Index].First = First;
		Nodes[Index].Count = Count;
		return Index;
	}
	const FVector3f Extent = Centers.GetExtent();
	const int32 Axis = (Extent.X >= Extent.Y && Extent.X >= Extent.Z) ? 0 : (Extent.Y >= Extent.Z ? 1 : 2);
	// 平局按原下标排：排序不稳定，不加次序键会让同输入建出不同的树，哈希不一致。
	Algo::Sort(MakeArrayView(Order.GetData() + First, Count), [this, Axis](int32 A, int32 B)
	{
		const float CA = Prims[A].Bounds.GetCenter()[Axis], CB = Prims[B].Bounds.GetCenter()[Axis];
		return CA < CB || (CA == CB && A < B);
	});
	const int32 Half = Count / 2;
	const int32 Left = BuildNode(First, Half);
	const int32 Right = BuildNode(First + Half, Count - Half);
	Nodes[Index].Left = Left;
	Nodes[Index].Right = Right;
	return Index;
}

uint64 FGeoPrimitiveIndex::Hash() const
{
	uint64 H = GeoHash::Combine(uint64(Prims.Num()), uint64(Nodes.Num()));
	for (const FGeoPrimitive& P : Prims)
	{
		H = GeoHash::Combine(H, uint64(P.Kind) | (uint64(P.MineralId) << 8) | (uint64(P.LithologyId) << 16)
			| (uint64(P.Grade) << 24) | (uint64(P.Priority) << 32));
		const float Vals[7] = { P.Center.X, P.Center.Y, P.Center.Z, P.Radii.X, P.Radii.Y, P.Radii.Z, P.Hard };
		for (const float V : Vals) { H = GeoHash::Combine(H, GeoHash::Float(V)); }
	}
	for (const int32 I : Order) { H = GeoHash::Combine(H, uint64(I)); }
	return H;
}

SIZE_T FGeoPrimitiveIndex::GetAllocatedSize() const
{
	return Prims.GetAllocatedSize() + Order.GetAllocatedSize() + Nodes.GetAllocatedSize();
}
```

- [ ] **Step 6：构建并跑** `StellarEra.Planet.Geo.Primitive`，期望 `Result={Success}`。

- [ ] **Step 7：提交**
  ```bash
  cd /f/workspace/ue/StellarEra && P="Source/StellarEraPlanet/Src/Geo/Core/GeoPrimitive.h Source/StellarEraPlanet/Src/Geo/Core/GeoPrimitiveIndex.h Source/StellarEraPlanet/Src/Geo/Core/GeoPrimitiveIndex.cpp Source/StellarEraPlanet/Src/Tests/PlanetGeoPrimitiveTests.cpp"
  git add -- $P && git commit -m "feat(地质): 团块原语与 BVH

  Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>" -- $P
  ```

## Task 5：岩性常量与径向剖面（M0）

**Files:**
- Create: `Source/StellarEraPlanet/Src/Geo/Core/GeoLithology.h`
- Create: `Source/StellarEraPlanet/Src/Geo/Core/GeoProfile.h`
- Test: `Source/StellarEraPlanet/Src/Tests/PlanetGeoProfileTests.cpp`

**Interfaces:**
- Consumes：`GeoHash`（Task 2）。
- Produces：`GeoLithology::{None=0, Crust=1, UpperMantle=2, Transition=3, LowerMantle=4, OuterCore=5, InnerCore=6, Count=7}`（`constexpr uint8`）、`GeoLithology::DebugColor(uint8) -> FColor`；
  `FGeoProfileLayer { float BottomDepthRatio; uint8 LithologyId; float WobbleRatio; }`；
  `FGeoProfile { TArray<FGeoProfileLayer> Layers; uint8 LithologyAt(float DepthRatio, float Wobble) const; uint64 Hash() const; }`。
  `DepthRatio = (R0 − |p|) / R0`（地表 0、球心 1，地表以上为负）；`Wobble ∈ [-1,1]`，每层底界按 `BottomDepthRatio + Wobble·WobbleRatio` 起伏。

- [ ] **Step 1：写失败测试** `PlanetGeoProfileTests.cpp`

```cpp
﻿// 剖面决定「挖到多深是什么」：层序必须单调，起伏只能挪界面、不能把两层顺序翻过来。
#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "GeoLithology.h"
#include "GeoProfile.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGeoProfileTest, "StellarEra.Planet.Geo.Profile",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGeoProfileTest::RunTest(const FString& Parameters)
{
	FGeoProfile P;
	P.Layers = {
		{ 0.25f, GeoLithology::Crust,       0.01f },
		{ 0.38f, GeoLithology::UpperMantle, 0.001f },
		{ 0.42f, GeoLithology::Transition,  0.001f },
		{ 0.55f, GeoLithology::LowerMantle, 0.005f },
		{ 0.80f, GeoLithology::OuterCore,   0.001f },
		{ 1.00f, GeoLithology::InnerCore,   0.f },
	};
	TestEqual(TEXT("地表以上按地壳"), P.LithologyAt(-0.1f, 0.f), GeoLithology::Crust);
	TestEqual(TEXT("0.10 地壳"), P.LithologyAt(0.10f, 0.f), GeoLithology::Crust);
	TestEqual(TEXT("0.30 上地幔"), P.LithologyAt(0.30f, 0.f), GeoLithology::UpperMantle);
	TestEqual(TEXT("0.40 过渡带"), P.LithologyAt(0.40f, 0.f), GeoLithology::Transition);
	TestEqual(TEXT("0.50 下地幔"), P.LithologyAt(0.50f, 0.f), GeoLithology::LowerMantle);
	TestEqual(TEXT("0.70 外核"), P.LithologyAt(0.70f, 0.f), GeoLithology::OuterCore);
	TestEqual(TEXT("0.95 内核"), P.LithologyAt(0.95f, 0.f), GeoLithology::InnerCore);
	TestEqual(TEXT("球心 内核"), P.LithologyAt(1.0f, 0.f), GeoLithology::InnerCore);
	TestEqual(TEXT("莫霍面上抬：0.245 在 wobble=-1 时已是上地幔"), P.LithologyAt(0.245f, -1.f), GeoLithology::UpperMantle);
	TestEqual(TEXT("莫霍面下压：0.255 在 wobble=+1 时仍是地壳"), P.LithologyAt(0.255f, 1.f), GeoLithology::Crust);

	bool bMonotonic = true;
	for (const float W : { -1.f, 0.f, 1.f })
	{
		uint8 Prev = 0;
		for (int32 i = 0; i <= 1000; ++i)
		{
			const uint8 L = P.LithologyAt(float(i) / 1000.f, W);
			bMonotonic &= (L >= Prev);
			Prev = L;
		}
	}
	TestTrue(TEXT("任意 wobble 下层序单调"), bMonotonic);

	FGeoProfile Q = P;
	TestTrue(TEXT("同剖面同哈希"), P.Hash() == Q.Hash());
	Q.Layers[0].BottomDepthRatio = 0.26f;
	TestTrue(TEXT("改界面哈希变"), P.Hash() != Q.Hash());
	TestTrue(TEXT("各圈层调试色互不相同"), GeoLithology::DebugColor(GeoLithology::Crust) != GeoLithology::DebugColor(GeoLithology::UpperMantle));
	return true;
}

#endif
```

- [ ] **Step 2：构建**，期望因缺头文件失败。

- [ ] **Step 3：实现 `GeoLithology.h`**

```cpp
﻿// M1 的岩性 Id，按深度递增编号；M2 换成目录资产后这里只留 None。
#pragma once
#include "CoreMinimal.h"

namespace GeoLithology
{
	constexpr uint8 None = 0;
	constexpr uint8 Crust = 1;
	constexpr uint8 UpperMantle = 2;
	constexpr uint8 Transition = 3;
	constexpr uint8 LowerMantle = 4;
	constexpr uint8 OuterCore = 5;
	constexpr uint8 InnerCore = 6;
	constexpr uint8 Count = 7;

	/** 调试色：M1 在测试星球上肉眼区分圈层用，不是最终美术。 */
	inline FColor DebugColor(uint8 Id)
	{
		static const FColor Colors[Count] = {
			FColor(255, 0, 255), FColor(150, 130, 110), FColor(70, 110, 60), FColor(60, 90, 120),
			FColor(110, 70, 110), FColor(230, 120, 30), FColor(250, 210, 80) };
		return Id < Count ? Colors[Id] : Colors[0];
	}
}
```

- [ ] **Step 4：实现 `GeoProfile.h`**

```cpp
﻿// 径向剖面：深度比例 → 岩性。层按 BottomDepthRatio 升序，最后一层底界为 1（球心）。
#pragma once
#include "CoreMinimal.h"
#include "GeoRng.h"

struct FGeoProfileLayer
{
	float BottomDepthRatio = 1.f;
	uint8 LithologyId = 0;
	/** 本层底界随方向的起伏（占 R）。必须小于相邻层厚度的一半，否则层序翻转。 */
	float WobbleRatio = 0.f;
};

struct FGeoProfile
{
	TArray<FGeoProfileLayer> Layers;

	FORCEINLINE uint8 LithologyAt(float DepthRatio, float Wobble) const
	{
		for (const FGeoProfileLayer& L : Layers)
		{
			if (DepthRatio < L.BottomDepthRatio + Wobble * L.WobbleRatio) { return L.LithologyId; }
		}
		return Layers.Num() > 0 ? Layers.Last().LithologyId : 0;
	}

	uint64 Hash() const
	{
		uint64 H = uint64(Layers.Num());
		for (const FGeoProfileLayer& L : Layers)
		{
			H = GeoHash::Combine(H, GeoHash::Float(L.BottomDepthRatio));
			H = GeoHash::Combine(H, uint64(L.LithologyId));
			H = GeoHash::Combine(H, GeoHash::Float(L.WobbleRatio));
		}
		return H;
	}
};
```

- [ ] **Step 5：构建并跑** `StellarEra.Planet.Geo.Profile`，期望 `Result={Success}`。

- [ ] **Step 6：提交**
  ```bash
  cd /f/workspace/ue/StellarEra && P="Source/StellarEraPlanet/Src/Geo/Core/GeoLithology.h Source/StellarEraPlanet/Src/Geo/Core/GeoProfile.h Source/StellarEraPlanet/Src/Tests/PlanetGeoProfileTests.cpp"
  git add -- $P && git commit -m "feat(地质): 岩性常量与径向剖面

  Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>" -- $P
  ```

## Task 6：材质编解码 `FGeoMaterialCodec`（M0）

**Files:**
- Create: `Source/StellarEraPlanet/Src/Geo/Snapshot/GeoMaterialCodec.h`
- Test: `Source/StellarEraPlanet/Src/Tests/PlanetGeoMaterialCodecTests.cpp`

**Interfaces:**
- Produces：`FGeoSample { uint8 Lithology, Mineral, Grade, Clue, ClueStrength, SecondaryMineral, SecondaryGrade; }`（`Clue`、`ClueStrength` ∈ 0..15）；
  `FGeoPackedMaterial { uint8 R, G, B, A, U0, V0, U1, V1; }`；`GeoMaterialCodec::Encode(const FGeoSample&) -> FGeoPackedMaterial`、`Decode(const FGeoPackedMaterial&) -> FGeoSample`。
  `Snapshot/` 不能 include Voxel，所以这里只产出 8 字节；拷进 `FVoxelMaterial` 在 Task 11 的适配器里做。布局 = 附录 `11-material-field.md` §2，Task 14 选定方案后可能改。

- [ ] **Step 1：写失败测试** `PlanetGeoMaterialCodecTests.cpp`

```cpp
﻿// 编解码是全项目唯一的通道约定，shader 与仪器都按它解码；往返不一致就是「看到的」和「挖到的」分叉。
#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "GeoMaterialCodec.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGeoMaterialCodecTest, "StellarEra.Planet.Geo.MaterialCodec",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGeoMaterialCodecTest::RunTest(const FString& Parameters)
{
	bool bRoundTrip = true;
	for (int32 L = 0; L < 256; ++L)
	for (int32 G = 0; G < 256; ++G)
	{
		FGeoSample S;
		S.Lithology = uint8(L); S.Mineral = uint8(255 - L); S.Grade = uint8(G);
		S.Clue = uint8(L & 15); S.ClueStrength = uint8(G & 15);
		S.SecondaryMineral = uint8(G); S.SecondaryGrade = uint8(L);
		const FGeoSample D = GeoMaterialCodec::Decode(GeoMaterialCodec::Encode(S));
		bRoundTrip &= D.Lithology == S.Lithology && D.Mineral == S.Mineral && D.Grade == S.Grade
			&& D.Clue == S.Clue && D.ClueStrength == S.ClueStrength
			&& D.SecondaryMineral == S.SecondaryMineral && D.SecondaryGrade == S.SecondaryGrade;
	}
	TestTrue(TEXT("256×256 往返一致"), bRoundTrip);

	FGeoSample Over;
	Over.Clue = 200; Over.ClueStrength = 99;
	const FGeoSample D = GeoMaterialCodec::Decode(GeoMaterialCodec::Encode(Over));
	TestEqual(TEXT("线索类型钳到 15"), D.Clue, uint8(15));
	TestEqual(TEXT("线索强度钳到 15"), D.ClueStrength, uint8(15));
	TestEqual(TEXT("R 通道就是岩性"), GeoMaterialCodec::Encode(FGeoSample{ 7 }).R, uint8(7));
	return true;
}

#endif
```

- [ ] **Step 2：构建**，期望因缺 `GeoMaterialCodec.h` 失败。

- [ ] **Step 3：实现 `GeoMaterialCodec.h`**

```cpp
﻿// 全项目唯一的材质通道约定（附录 11-material-field.md §2）。改布局只改这里 + MF_GeoDecode。
#pragma once
#include "CoreMinimal.h"

struct FGeoSample
{
	uint8 Lithology = 0;
	uint8 Mineral = 0;
	uint8 Grade = 0;
	uint8 Clue = 0;          // 0..15
	uint8 ClueStrength = 0;  // 0..15
	uint8 SecondaryMineral = 0;
	uint8 SecondaryGrade = 0;
};

struct FGeoPackedMaterial
{
	uint8 R = 0, G = 0, B = 0, A = 0, U0 = 0, V0 = 0, U1 = 0, V1 = 0;
};

namespace GeoMaterialCodec
{
	FORCEINLINE FGeoPackedMaterial Encode(const FGeoSample& S)
	{
		FGeoPackedMaterial M;
		M.R = S.Lithology;
		M.G = S.Mineral;
		M.B = S.Grade;
		M.A = uint8((FMath::Min<uint8>(S.Clue, 15) << 4) | FMath::Min<uint8>(S.ClueStrength, 15));
		M.U0 = S.SecondaryMineral;
		M.V0 = S.SecondaryGrade;
		return M;
	}

	FORCEINLINE FGeoSample Decode(const FGeoPackedMaterial& M)
	{
		FGeoSample S;
		S.Lithology = M.R;
		S.Mineral = M.G;
		S.Grade = M.B;
		S.Clue = uint8(M.A >> 4);
		S.ClueStrength = uint8(M.A & 15);
		S.SecondaryMineral = M.U0;
		S.SecondaryGrade = M.V0;
		return S;
	}
}
```

- [ ] **Step 4：构建并跑** `StellarEra.Planet.Geo.MaterialCodec`，期望 `Result={Success}`。

- [ ] **Step 5：提交**
  ```bash
  cd /f/workspace/ue/StellarEra && P="Source/StellarEraPlanet/Src/Geo/Snapshot/GeoMaterialCodec.h Source/StellarEraPlanet/Src/Tests/PlanetGeoMaterialCodecTests.cpp"
  git add -- $P && git commit -m "feat(地质): 材质通道编解码

  Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>" -- $P
  ```

## Task 7：快照与求值核（M1）

**Files:**
- Create: `Source/StellarEraPlanet/Src/Geo/Snapshot/PlanetSnapshot.h`、`PlanetSnapshot.cpp`
- Create: `Source/StellarEraPlanet/Src/Geo/Snapshot/PlanetShapeEval.h`
- Create: `Source/StellarEraPlanet/Src/Geo/Snapshot/PlanetMaterialEval.h`
- Test: `Source/StellarEraPlanet/Src/Tests/PlanetGeoSnapshotTests.cpp`

**Interfaces:**
- Consumes：`FGeoProfile`、`FGeoField2D`、`FGeoPrimitiveIndex`、`FGeoRange`、`FGeoSample`、`GeoHash`。
- Produces：
  - `FPlanetSnapshot { double R0; float DensityScale; FGeoProfile Radial; FGeoField2D MohoWobble; FGeoPrimitiveIndex Primitives; double MinSurfaceRadius, MaxSurfaceRadius; uint64 ContentHash; void FinalizeHash(); SIZE_T GetAllocatedSize() const; }`。单位：体素，球心在体素坐标原点。
  - `PlanetShapeEval::SurfaceRadius(const FPlanetSnapshot&, const FVector3f& Dir) -> double`、`Value(const FPlanetSnapshot&, const FVector3d& P) -> float`（< 0 实心）、`ValueRange(const FPlanetSnapshot&, const FVector3d& BoxMin, const FVector3d& BoxMax) -> FGeoRange`。
  - `PlanetMaterialEval::Sample(const FPlanetSnapshot&, const FVector3d& P) -> FGeoSample`。
  - M1 没有地形起伏：地表就是 `R0` 球面。M3 在 `SurfaceRadius` 里叠 `Elevation`，签名不变。

- [ ] **Step 1：写失败测试** `PlanetGeoSnapshotTests.cpp`

```cpp
﻿// 求值核是体素线程唯一读的东西：符号、值域、岩性三件事必须与快照内容一致，值域估窄会让插件整块跳过。
#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "PlanetSnapshot.h"
#include "PlanetShapeEval.h"
#include "PlanetMaterialEval.h"
#include "GeoLithology.h"
#include "GeoRng.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGeoSnapshotTest, "StellarEra.Planet.Geo.Snapshot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGeoSnapshotTest::RunTest(const FString& Parameters)
{
	FPlanetSnapshot S;
	S.R0 = 1000.0;
	S.Radial.Layers = { { 0.25f, GeoLithology::Crust, 0.f }, { 1.f, GeoLithology::InnerCore, 0.f } };
	S.MohoWobble.Init(16);
	S.MohoWobble.Fill([](const FVector3f&) { return 0.f; });
	S.MinSurfaceRadius = S.MaxSurfaceRadius = S.R0;
	S.FinalizeHash();

	const FVector3d Dir = FVector3d(1.0, 2.0, 3.0).GetSafeNormal();
	TestTrue(TEXT("地表以下实心"), PlanetShapeEval::Value(S, Dir * 900.0) < 0.f);
	TestTrue(TEXT("地表以上空气"), PlanetShapeEval::Value(S, Dir * 1100.0) > 0.f);
	TestTrue(TEXT("地表处接近 0"), FMath::Abs(PlanetShapeEval::Value(S, Dir * 1000.0)) < 1e-3f);
	TestTrue(TEXT("球心不出 NaN"), FMath::IsFinite(PlanetShapeEval::Value(S, FVector3d::ZeroVector)));

	TestEqual(TEXT("深度 0.1 是地壳"), PlanetMaterialEval::Sample(S, Dir * 900.0).Lithology, GeoLithology::Crust);
	TestEqual(TEXT("深度 0.5 是内核层"), PlanetMaterialEval::Sample(S, Dir * 500.0).Lithology, GeoLithology::InnerCore);

	FGeoRng Rng = FGeoRng::Derive(5, 0, 0);
	bool bCovers = true;
	for (int32 i = 0; i < 20000; ++i)
	{
		const FVector3d C(Rng.Range(-1200.f, 1200.f), Rng.Range(-1200.f, 1200.f), Rng.Range(-1200.f, 1200.f));
		const double H = Rng.Range(1.f, 64.f);
		const FVector3d Lo = C - FVector3d(H), Hi = C + FVector3d(H);
		const FGeoRange R = PlanetShapeEval::ValueRange(S, Lo, Hi);
		const FVector3d P(FMath::Lerp(Lo.X, Hi.X, double(Rng.NextFloat())),
			FMath::Lerp(Lo.Y, Hi.Y, double(Rng.NextFloat())), FMath::Lerp(Lo.Z, Hi.Z, double(Rng.NextFloat())));
		bCovers &= R.Contains(PlanetShapeEval::Value(S, P));
	}
	TestTrue(TEXT("值域覆盖盒内任意采样"), bCovers);
	TestTrue(TEXT("深部盒整块实心"), PlanetShapeEval::ValueRange(S, FVector3d(-50.0), FVector3d(50.0)).Hi < 0.f);

	FPlanetSnapshot T = S;
	TestTrue(TEXT("同内容同哈希"), T.ContentHash == S.ContentHash);
	T.R0 = 1001.0;
	T.FinalizeHash();
	TestTrue(TEXT("改半径哈希变"), T.ContentHash != S.ContentHash);
	return true;
}

#endif
```

- [ ] **Step 2：构建**，期望因缺 `PlanetSnapshot.h` 失败。

- [ ] **Step 3：实现 `PlanetSnapshot.h`**

```cpp
﻿// 冻结产物：不可变、无 UObject，以 TSharedPtr<const> 交给体素线程。只放求值期真的要读的字段。
#pragma once
#include "CoreMinimal.h"
#include "GeoField2D.h"
#include "GeoPrimitiveIndex.h"
#include "GeoProfile.h"

struct STELLARERAPLANET_API FPlanetSnapshot
{
	/** 海平面半径（体素），也是 M1 的地表。 */
	double R0 = 0.0;
	/** 密度值 = 距离差 / DensityScale；1 表示「1 体素对应 1 个单位」。 */
	float DensityScale = 1.f;
	FGeoProfile Radial;
	/** 莫霍面等界面的起伏，∈ [-1,1]，乘各层 WobbleRatio 得到半径比例。 */
	FGeoField2D MohoWobble;
	FGeoPrimitiveIndex Primitives;
	/** 地表半径的保守上下界，值域与早退都以它为准。 */
	double MinSurfaceRadius = 0.0;
	double MaxSurfaceRadius = 0.0;
	uint64 ContentHash = 0;

	void FinalizeHash();
	SIZE_T GetAllocatedSize() const;
};
```

- [ ] **Step 4：实现 `PlanetSnapshot.cpp`**

```cpp
﻿#include "PlanetSnapshot.h"
#include "GeoRng.h"

void FPlanetSnapshot::FinalizeHash()
{
	uint64 H = GeoHash::Combine(GeoHash::Float(float(R0)), GeoHash::Float(DensityScale));
	H = GeoHash::Combine(H, Radial.Hash());
	H = GeoHash::Combine(H, MohoWobble.Hash());
	H = GeoHash::Combine(H, Primitives.Hash());
	H = GeoHash::Combine(H, GeoHash::Float(float(MinSurfaceRadius)));
	H = GeoHash::Combine(H, GeoHash::Float(float(MaxSurfaceRadius)));
	ContentHash = H;
}

SIZE_T FPlanetSnapshot::GetAllocatedSize() const
{
	return Radial.Layers.GetAllocatedSize() + MohoWobble.GetAllocatedSize() + Primitives.GetAllocatedSize();
}
```

- [ ] **Step 5：实现 `PlanetShapeEval.h`**

```cpp
﻿// 密度求值核：体素线程每次挖掘调用 ~10⁹ 次，只许 inline 算术，不许虚函数、分配、日志。
#pragma once
#include "CoreMinimal.h"
#include "PlanetSnapshot.h"

namespace PlanetShapeEval
{
	FORCEINLINE double SurfaceRadius(const FPlanetSnapshot& S, const FVector3f& /*Dir*/)
	{
		return S.R0;   // M3 起叠加 Elevation
	}

	FORCEINLINE float Value(const FPlanetSnapshot& S, const FVector3d& P)
	{
		const double D = P.Size();
		const FVector3f Dir = D > 1e-6 ? FVector3f(P / D) : FVector3f::UpVector;
		return float((D - SurfaceRadius(S, Dir)) / double(S.DensityScale));
	}

	/** 盒内到球心的最近 / 最远距离分别配地表上 / 下界，得到保守区间。 */
	FORCEINLINE FGeoRange ValueRange(const FPlanetSnapshot& S, const FVector3d& BoxMin, const FVector3d& BoxMax)
	{
		const FVector3d Near(FMath::Clamp(0.0, BoxMin.X, BoxMax.X), FMath::Clamp(0.0, BoxMin.Y, BoxMax.Y), FMath::Clamp(0.0, BoxMin.Z, BoxMax.Z));
		const FVector3d Far(FMath::Max(FMath::Abs(BoxMin.X), FMath::Abs(BoxMax.X)),
			FMath::Max(FMath::Abs(BoxMin.Y), FMath::Abs(BoxMax.Y)), FMath::Max(FMath::Abs(BoxMin.Z), FMath::Abs(BoxMax.Z)));
		const double Scale = double(S.DensityScale);
		return { float((Near.Size() - S.MaxSurfaceRadius) / Scale), float((Far.Size() - S.MinSurfaceRadius) / Scale) };
	}
}
```

- [ ] **Step 6：实现 `PlanetMaterialEval.h`**

```cpp
﻿// 材质求值核：只在被网格化的表面上调用，返回语义样本；编码进体素材质由适配器调 GeoMaterialCodec。
#pragma once
#include "CoreMinimal.h"
#include "GeoMaterialCodec.h"
#include "PlanetSnapshot.h"

namespace PlanetMaterialEval
{
	FORCEINLINE FGeoSample Sample(const FPlanetSnapshot& S, const FVector3d& P)
	{
		const double D = P.Size();
		const FVector3f Dir = D > 1e-6 ? FVector3f(P / D) : FVector3f::UpVector;
		const float DepthRatio = float((S.R0 - D) / S.R0);
		const float Wobble = S.MohoWobble.Res > 0 ? S.MohoWobble.Sample(Dir) : 0.f;
		FGeoSample Out;
		Out.Lithology = S.Radial.LithologyAt(DepthRatio, Wobble);
		return Out;
	}
}
```

- [ ] **Step 7：构建并跑** `StellarEra.Planet.Geo.Snapshot`，期望 `Result={Success}`。

- [ ] **Step 8：提交**
  ```bash
  cd /f/workspace/ue/StellarEra && D=Source/StellarEraPlanet/Src/Geo/Snapshot && P="$D/PlanetSnapshot.h $D/PlanetSnapshot.cpp $D/PlanetShapeEval.h $D/PlanetMaterialEval.h Source/StellarEraPlanet/Src/Tests/PlanetGeoSnapshotTests.cpp"
  git add -- $P && git commit -m "feat(地质): 不可变快照与密度/材质求值核

  Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>" -- $P
  ```

## Task 8：配方、阶段接口与烘焙流水线（M1）

**Files:**
- Create: `Source/StellarEraPlanet/Src/Geo/Bake/PlanetRecipeConfig.h`
- Create: `Source/StellarEraPlanet/Src/Geo/Bake/PlanetBakeTypes.h`
- Create: `Source/StellarEraPlanet/Src/Geo/Bake/PlanetStage.h`
- Create: `Source/StellarEraPlanet/Src/Geo/Bake/PlanetBakePipeline.h`、`PlanetBakePipeline.cpp`
- Test: `Source/StellarEraPlanet/Src/Tests/PlanetGeoPipelineTests.cpp`

**Interfaces:**
- Consumes：`FPlanetSnapshot`（Task 7）、`FGeoRng`、`GeoHash`、`FGeoField2D`、`FGeoProfile`、`FGeoPrimitive`、`FGeoPrimitiveIndex`。
- Produces：
  - `FPlanetSphereParams { float CrustRatio=0.25f, UpperMantleRatio=0.38f, TransitionRatio=0.42f, MantleOuterRatio=0.55f, OuterCoreRatio=0.80f, MohoWobbleRatio=0.01f; int32 WobbleOctaves=3; }`
  - `FPlanetRecipeConfig { double Radius=40000; float VoxelSizeCm=100; uint64 Seed=1337; int32 FaceRes=1024; bool bSingleThreadBake=false; FPlanetSphereParams Sphere; uint64 Hash() const; }`
  - `enum class EPlanetField : uint32 { None=0, Radial=1<<0, MohoWobble=1<<1, Primitives=1<<2 }`（`ENUM_CLASS_FLAGS`）
  - `FPlanetBakeFields { FGeoProfile Radial; FGeoField2D MohoWobble; TArray<FGeoPrimitive> Primitives; }`
  - `FPlanetBakeReport { TArray<FString> Errors; TArray<TPair<FName,double>> StageSeconds; bool HasErrors() const; }`
  - `FPlanetBakeContext { const FPlanetRecipeConfig& Config; FPlanetBakeFields& Fields; FGeoRng Rng; FPlanetBakeReport& Report; }`
  - `IPlanetStage { GetName() -> FName; GetStageIndex() -> uint32; GetReads()/GetWrites() -> EPlanetField; Bake(FPlanetBakeContext&) const; }`
  - `FPlanetBakePipeline { AddStage(TUniquePtr<IPlanetStage>); Validate(FPlanetBakeReport&) const -> bool; Bake(const FPlanetRecipeConfig&, FPlanetBakeReport&) const -> TSharedPtr<const FPlanetSnapshot>; static MakeDefault(); }`
  - 本任务里 `MakeDefault()` 返回空流水线，Task 9 加入阶段 1。

- [ ] **Step 1：写失败测试** `PlanetGeoPipelineTests.cpp`

```cpp
﻿// 「后阶段只读前阶段」靠这里机械检查：读了没人写的字段必须在烘焙前报错，否则下游静默拿到空场。
#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "PlanetBakePipeline.h"
#include "PlanetStage.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	class FFakeStage final : public IPlanetStage
	{
	public:
		FFakeStage(FName InName, uint32 InIndex, EPlanetField InReads, EPlanetField InWrites)
			: Name(InName), Index(InIndex), Reads(InReads), Writes(InWrites) {}
		virtual FName GetName() const override { return Name; }
		virtual uint32 GetStageIndex() const override { return Index; }
		virtual EPlanetField GetReads() const override { return Reads; }
		virtual EPlanetField GetWrites() const override { return Writes; }
		virtual void Bake(FPlanetBakeContext& Ctx) const override
		{
			if (EnumHasAnyFlags(Writes, EPlanetField::Radial)) { Ctx.Fields.Radial.Layers = { { 1.f, 1, 0.f } }; }
		}
	private:
		FName Name; uint32 Index; EPlanetField Reads, Writes;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGeoPipelineTest, "StellarEra.Planet.Geo.Pipeline",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGeoPipelineTest::RunTest(const FString& Parameters)
{
	FPlanetRecipeConfig Config;
	Config.Radius = 1000.0;
	Config.FaceRes = 16;

	FPlanetBakePipeline Bad;
	Bad.AddStage(MakeUnique<FFakeStage>(TEXT("Reader"), 1, EPlanetField::Radial, EPlanetField::None));
	FPlanetBakeReport BadReport;
	TestFalse(TEXT("读了没人写的字段 → 校验失败"), Bad.Validate(BadReport));
	TestTrue(TEXT("校验失败写进报告"), BadReport.HasErrors());
	FPlanetBakeReport BakeBadReport;
	TestFalse(TEXT("校验失败时拒绝烘焙"), Bad.Bake(Config, BakeBadReport).IsValid());

	FPlanetBakePipeline Unordered;
	Unordered.AddStage(MakeUnique<FFakeStage>(TEXT("Reader"), 2, EPlanetField::Radial, EPlanetField::None));
	Unordered.AddStage(MakeUnique<FFakeStage>(TEXT("Writer"), 1, EPlanetField::None, EPlanetField::Radial));
	FPlanetBakeReport Report;
	TestTrue(TEXT("按阶段号排序，不按添加顺序"), Unordered.Validate(Report));
	const TSharedPtr<const FPlanetSnapshot> Snap = Unordered.Bake(Config, Report);
	TestTrue(TEXT("产出快照"), Snap.IsValid());
	if (Snap)
	{
		TestEqual(TEXT("快照 R0 = 配方半径"), Snap->R0, 1000.0);
		TestEqual(TEXT("阶段写的剖面进了快照"), Snap->Radial.Layers.Num(), 1);
		TestTrue(TEXT("快照有内容哈希"), Snap->ContentHash != 0);
	}
	TestEqual(TEXT("每阶段都有计时"), Report.StageSeconds.Num(), 2);

	FPlanetRecipeConfig Other = Config;
	Other.Seed = 1338;
	TestTrue(TEXT("改种子配方哈希变"), Config.Hash() != Other.Hash());
	return true;
}

#endif
```

- [ ] **Step 2：构建**，期望因缺 `PlanetBakePipeline.h` 失败。

- [ ] **Step 3：实现 `PlanetRecipeConfig.h`**

```cpp
﻿// 烘焙配方：纯 POD，游戏线程从 UPlanetRecipe 拷一次后交给后台任务，烘焙期不再碰 UObject。
#pragma once
#include "CoreMinimal.h"
#include "GeoRng.h"

/** 圈层底界，都是从地表向下的累计深度占 R（附录 01-sphere.md）。 */
struct FPlanetSphereParams
{
	float CrustRatio = 0.25f;
	float UpperMantleRatio = 0.38f;
	float TransitionRatio = 0.42f;
	float MantleOuterRatio = 0.55f;
	float OuterCoreRatio = 0.80f;
	/** 莫霍面起伏幅度（占 R）。必须 < 地壳与上地幔厚度的一半。 */
	float MohoWobbleRatio = 0.01f;
	int32 WobbleOctaves = 3;
};

struct FPlanetRecipeConfig
{
	double Radius = 40000.0;
	float VoxelSizeCm = 100.f;
	uint64 Seed = 1337;
	int32 FaceRes = 1024;
	/** 只给确定性测试用：强制单线程烘焙，与并行结果对拍。 */
	bool bSingleThreadBake = false;
	FPlanetSphereParams Sphere;

	uint64 Hash() const
	{
		uint64 H = GeoHash::Combine(GeoHash::Float(float(Radius)), GeoHash::Float(VoxelSizeCm));
		H = GeoHash::Combine(H, Seed);
		H = GeoHash::Combine(H, uint64(FaceRes));
		const float S[6] = { Sphere.CrustRatio, Sphere.UpperMantleRatio, Sphere.TransitionRatio,
			Sphere.MantleOuterRatio, Sphere.OuterCoreRatio, Sphere.MohoWobbleRatio };
		for (const float V : S) { H = GeoHash::Combine(H, GeoHash::Float(V)); }
		return GeoHash::Combine(H, uint64(Sphere.WobbleOctaves));
	}
};
```

- [ ] **Step 4：实现 `PlanetBakeTypes.h`**

```cpp
﻿// 阶段之间的数据总线：类型化结构体 + 位掩码声明，不用 FName 字典（拼错键要在编译期暴露）。
#pragma once
#include "CoreMinimal.h"
#include "GeoField2D.h"
#include "GeoPrimitive.h"
#include "GeoProfile.h"
#include "GeoRng.h"
#include "PlanetRecipeConfig.h"

enum class EPlanetField : uint32
{
	None       = 0,
	Radial     = 1 << 0,
	MohoWobble = 1 << 1,
	Primitives = 1 << 2,
};
ENUM_CLASS_FLAGS(EPlanetField);

struct FPlanetBakeFields
{
	FGeoProfile Radial;
	FGeoField2D MohoWobble;
	TArray<FGeoPrimitive> Primitives;
};

struct FPlanetBakeReport
{
	TArray<FString> Errors;
	TArray<TPair<FName, double>> StageSeconds;
	bool HasErrors() const { return Errors.Num() > 0; }
};

struct FPlanetBakeContext
{
	const FPlanetRecipeConfig& Config;
	FPlanetBakeFields& Fields;
	/** 已按 (Seed, StageIndex) 派生；阶段内再按用途 Derive，不同用途互不干扰。 */
	FGeoRng Rng;
	FPlanetBakeReport& Report;
};
```

- [ ] **Step 5：实现 `PlanetStage.h`**

```cpp
﻿// 只在烘焙期存在。无状态：同一个阶段对象可以连续烘焙多颗星，不许在成员里攒结果。
#pragma once
#include "CoreMinimal.h"
#include "PlanetBakeTypes.h"

class IPlanetStage
{
public:
	virtual ~IPlanetStage() = default;
	virtual FName GetName() const = 0;
	/** 执行顺序；也是派生随机流的阶段号，改它会改变整颗星。 */
	virtual uint32 GetStageIndex() const = 0;
	virtual EPlanetField GetReads() const = 0;
	virtual EPlanetField GetWrites() const = 0;
	virtual void Bake(FPlanetBakeContext& Ctx) const = 0;
};
```

- [ ] **Step 6：实现 `PlanetBakePipeline.h`**

```cpp
﻿// 烘焙流水线：按阶段号排序、校验读写、顺序执行、冻结成快照。全程不碰 UObject，可在后台线程调用。
#pragma once
#include "CoreMinimal.h"
#include "PlanetBakeTypes.h"
#include "PlanetSnapshot.h"

class IPlanetStage;

class STELLARERAPLANET_API FPlanetBakePipeline
{
public:
	FPlanetBakePipeline();
	~FPlanetBakePipeline();
	FPlanetBakePipeline(FPlanetBakePipeline&&);
	FPlanetBakePipeline& operator=(FPlanetBakePipeline&&);

	void AddStage(TUniquePtr<IPlanetStage> Stage);
	bool Validate(FPlanetBakeReport& Report) const;
	/** 校验失败或任一阶段写了 Error 时返回空。 */
	TSharedPtr<const FPlanetSnapshot> Bake(const FPlanetRecipeConfig& Config, FPlanetBakeReport& Report) const;

	/** 项目默认的阶段集合（M1 只有阶段 1）。 */
	static FPlanetBakePipeline MakeDefault();

private:
	TArray<TUniquePtr<IPlanetStage>> Stages;
	TArray<const IPlanetStage*> Sorted() const;
};
```

- [ ] **Step 7：实现 `PlanetBakePipeline.cpp`**

```cpp
﻿#include "PlanetBakePipeline.h"
#include "HAL/PlatformTime.h"
#include "PlanetStage.h"

FPlanetBakePipeline::FPlanetBakePipeline() = default;
FPlanetBakePipeline::~FPlanetBakePipeline() = default;
FPlanetBakePipeline::FPlanetBakePipeline(FPlanetBakePipeline&&) = default;
FPlanetBakePipeline& FPlanetBakePipeline::operator=(FPlanetBakePipeline&&) = default;

void FPlanetBakePipeline::AddStage(TUniquePtr<IPlanetStage> Stage)
{
	Stages.Add(MoveTemp(Stage));
}

TArray<const IPlanetStage*> FPlanetBakePipeline::Sorted() const
{
	TArray<const IPlanetStage*> Out;
	for (const TUniquePtr<IPlanetStage>& S : Stages) { Out.Add(S.Get()); }
	Out.StableSort([](const IPlanetStage& A, const IPlanetStage& B) { return A.GetStageIndex() < B.GetStageIndex(); });
	return Out;
}

bool FPlanetBakePipeline::Validate(FPlanetBakeReport& Report) const
{
	EPlanetField Written = EPlanetField::None;
	bool bOk = true;
	for (const IPlanetStage* S : Sorted())
	{
		const EPlanetField Missing = S->GetReads() & ~Written;
		if (Missing != EPlanetField::None)
		{
			Report.Errors.Add(FString::Printf(TEXT("[地质] 阶段 %s 读了尚未写入的字段 0x%x"),
				*S->GetName().ToString(), uint32(Missing)));
			bOk = false;
		}
		Written |= S->GetWrites();
	}
	return bOk;
}

TSharedPtr<const FPlanetSnapshot> FPlanetBakePipeline::Bake(const FPlanetRecipeConfig& Config, FPlanetBakeReport& Report) const
{
	if (!Validate(Report)) { return nullptr; }

	FPlanetBakeFields Fields;
	for (const IPlanetStage* S : Sorted())
	{
		const double T0 = FPlatformTime::Seconds();
		FPlanetBakeContext Ctx{ Config, Fields, FGeoRng::Derive(Config.Seed, S->GetStageIndex(), 0), Report };
		S->Bake(Ctx);
		Report.StageSeconds.Emplace(S->GetName(), FPlatformTime::Seconds() - T0);
		if (Report.HasErrors()) { return nullptr; }
	}

	TSharedPtr<FPlanetSnapshot> Snap = MakeShared<FPlanetSnapshot>();
	Snap->R0 = Config.Radius;
	Snap->Radial = MoveTemp(Fields.Radial);
	Snap->MohoWobble = MoveTemp(Fields.MohoWobble);
	Snap->Primitives.Build(MoveTemp(Fields.Primitives));
	// M1 没有地形起伏：地表恒为 R0。M3 起由阶段 3 写入实测上下界。
	Snap->MinSurfaceRadius = Snap->MaxSurfaceRadius = Config.Radius;
	Snap->FinalizeHash();
	return Snap;
}

FPlanetBakePipeline FPlanetBakePipeline::MakeDefault()
{
	return FPlanetBakePipeline();
}
```

- [ ] **Step 8：构建并跑** `StellarEra.Planet.Geo.Pipeline`，期望 `Result={Success}`。

- [ ] **Step 9：提交**
  ```bash
  cd /f/workspace/ue/StellarEra && D=Source/StellarEraPlanet/Src/Geo/Bake && P="$D/PlanetRecipeConfig.h $D/PlanetBakeTypes.h $D/PlanetStage.h $D/PlanetBakePipeline.h $D/PlanetBakePipeline.cpp Source/StellarEraPlanet/Src/Tests/PlanetGeoPipelineTests.cpp"
  git add -- $P && git commit -m "feat(地质): 烘焙配方、阶段接口与流水线

  Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>" -- $P
  ```

## Task 9：阶段 1 球体与深部分层（M1）

**Files:**
- Create: `Source/StellarEraPlanet/Src/Geo/Bake/Stages/Stage01Sphere.h`、`Stage01Sphere.cpp`
- Modify: `Source/StellarEraPlanet/Src/Geo/Bake/PlanetBakePipeline.cpp`（`MakeDefault` 加入阶段 1）
- Test: `Source/StellarEraPlanet/Src/Tests/PlanetGeoStage01Tests.cpp`

**Interfaces:**
- Consumes：`IPlanetStage`、`FPlanetBakeContext`、`FPlanetSphereParams`、`GeoNoise::Fbm3`、`GeoLithology`、`FGeoProfileLayer`。
- Produces：`FStage01Sphere`（`GetStageIndex() == 1`，读 `None`，写 `Radial | MohoWobble`）；`MakeDefault()` 从此含阶段 1。
  起伏场分辨率 `clamp(FaceRes / 4, 32, 256)`：莫霍面起伏是低频量。

- [ ] **Step 1：写失败测试** `PlanetGeoStage01Tests.cpp`

```cpp
﻿// 阶段 1 是「挖到哪一层」的唯一来源：层序照配方、起伏有界、非法比例拒绝烘焙。
#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "PlanetBakePipeline.h"
#include "PlanetMaterialEval.h"
#include "GeoLithology.h"
#include "GeoRng.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGeoStage01Test, "StellarEra.Planet.Geo.Stage01",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGeoStage01Test::RunTest(const FString& Parameters)
{
	FPlanetRecipeConfig Config;
	Config.FaceRes = 256;
	FPlanetBakeReport Report;
	const TSharedPtr<const FPlanetSnapshot> Snap = FPlanetBakePipeline::MakeDefault().Bake(Config, Report);
	if (!TestTrue(TEXT("默认配方烘焙成功"), Snap.IsValid())) { return false; }
	TestEqual(TEXT("6 层"), Snap->Radial.Layers.Num(), 6);
	TestEqual(TEXT("起伏场分辨率 = FaceRes/4"), Snap->MohoWobble.Res, 64);

	const float Mid[6] = { 0.12f, 0.31f, 0.40f, 0.48f, 0.67f, 0.90f };
	const uint8 Expect[6] = { GeoLithology::Crust, GeoLithology::UpperMantle, GeoLithology::Transition,
		GeoLithology::LowerMantle, GeoLithology::OuterCore, GeoLithology::InnerCore };
	FGeoRng Rng = FGeoRng::Derive(9, 0, 0);
	bool bLayers = true;
	float MinW = 1.f, MaxW = -1.f;
	for (int32 i = 0; i < 100; ++i)
	{
		const float Z = Rng.Range(-1.f, 1.f), Phi = Rng.Range(0.f, 2.f * PI), S = FMath::Sqrt(1.f - Z * Z);
		const FVector3d Dir(S * FMath::Cos(Phi), S * FMath::Sin(Phi), Z);
		for (int32 L = 0; L < 6; ++L)
		{
			bLayers &= PlanetMaterialEval::Sample(*Snap, Dir * (Config.Radius * (1.0 - Mid[L]))).Lithology == Expect[L];
		}
		const float W = Snap->MohoWobble.Sample(FVector3f(Dir));
		MinW = FMath::Min(MinW, W);
		MaxW = FMath::Max(MaxW, W);
	}
	TestTrue(TEXT("100 个虚拟钻孔层序都符合配方"), bLayers);
	TestTrue(TEXT("起伏场 ∈ [-1,1] 且确有起伏"), MinW >= -1.f && MaxW <= 1.f && MaxW - MinW > 0.2f);

	FPlanetRecipeConfig Single = Config;
	Single.bSingleThreadBake = true;
	FPlanetBakeReport R2;
	const TSharedPtr<const FPlanetSnapshot> SnapSingle = FPlanetBakePipeline::MakeDefault().Bake(Single, R2);
	TestTrue(TEXT("单线程与并行快照哈希一致"), SnapSingle && SnapSingle->ContentHash == Snap->ContentHash);

	FPlanetRecipeConfig Bad = Config;
	Bad.Sphere.UpperMantleRatio = 0.2f;
	FPlanetBakeReport R3;
	TestFalse(TEXT("圈层比例不递增 → 拒绝烘焙"), FPlanetBakePipeline::MakeDefault().Bake(Bad, R3).IsValid());
	TestTrue(TEXT("报告里有原因"), R3.HasErrors());

	FPlanetRecipeConfig TooWobbly = Config;
	TooWobbly.Sphere.MohoWobbleRatio = 0.1f;   // 上地幔厚 0.13，半厚 0.065
	FPlanetBakeReport R4;
	TestFalse(TEXT("起伏超过相邻层半厚 → 拒绝烘焙"), FPlanetBakePipeline::MakeDefault().Bake(TooWobbly, R4).IsValid());
	return true;
}

#endif
```

- [ ] **Step 2：构建并跑**，期望 `6 层` 等断言失败（默认流水线还是空的）。

- [ ] **Step 3：实现 `Stage01Sphere.h`**

```cpp
﻿// 阶段 1：圈层与莫霍面起伏。不产生地表起伏，只回答「深度比例 → 岩性」。
#pragma once
#include "CoreMinimal.h"
#include "PlanetStage.h"

class FStage01Sphere final : public IPlanetStage
{
public:
	virtual FName GetName() const override { return TEXT("Stage01Sphere"); }
	virtual uint32 GetStageIndex() const override { return 1; }
	virtual EPlanetField GetReads() const override { return EPlanetField::None; }
	virtual EPlanetField GetWrites() const override { return EPlanetField::Radial | EPlanetField::MohoWobble; }
	virtual void Bake(FPlanetBakeContext& Ctx) const override;
};
```

- [ ] **Step 4：实现 `Stage01Sphere.cpp`**

```cpp
﻿#include "Stage01Sphere.h"
#include "GeoLithology.h"
#include "GeoNoise.h"

namespace
{
	// 莫霍面以外是相变或成分界面，起伏比莫霍面小一个量级（附录 01-sphere.md）。
	constexpr float DeepWobbleRatio = 0.002f;
}

void FStage01Sphere::Bake(FPlanetBakeContext& Ctx) const
{
	const FPlanetSphereParams& S = Ctx.Config.Sphere;
	const float Bottoms[6] = { S.CrustRatio, S.UpperMantleRatio, S.TransitionRatio, S.MantleOuterRatio, S.OuterCoreRatio, 1.f };
	const uint8 Ids[6] = { GeoLithology::Crust, GeoLithology::UpperMantle, GeoLithology::Transition,
		GeoLithology::LowerMantle, GeoLithology::OuterCore, GeoLithology::InnerCore };
	const float Wobbles[6] = { S.MohoWobbleRatio, DeepWobbleRatio, DeepWobbleRatio, DeepWobbleRatio, DeepWobbleRatio, 0.f };

	float Top = 0.f;
	for (int32 i = 0; i < 6; ++i)
	{
		if (Bottoms[i] <= Top)
		{
			Ctx.Report.Errors.Add(FString::Printf(TEXT("[地质] 阶段 1：第 %d 层底界 %.3f 不大于上一层 %.3f"), i, Bottoms[i], Top));
			return;
		}
		Top = Bottoms[i];
	}
	Top = 0.f;
	for (int32 i = 0; i < 6; ++i)
	{
		const float Thickness = Bottoms[i] - Top;
		const float NextThickness = i < 5 ? Bottoms[i + 1] - Bottoms[i] : 1.f;
		// 起伏超过上下两层半厚的较小者，界面会越过相邻界面，层序翻转。
		if (Wobbles[i] >= 0.5f * FMath::Min(Thickness, NextThickness))
		{
			Ctx.Report.Errors.Add(FString::Printf(TEXT("[地质] 阶段 1：第 %d 层起伏 %.3f 超过相邻层半厚"), i, Wobbles[i]));
			return;
		}
		Ctx.Fields.Radial.Layers.Add({ Bottoms[i], Ids[i], Wobbles[i] });
		Top = Bottoms[i];
	}

	const uint64 NoiseSeed = Ctx.Rng.NextU64();
	const int32 Octaves = FMath::Clamp(S.WobbleOctaves, 1, 6);
	Ctx.Fields.MohoWobble.Init(FMath::Clamp(Ctx.Config.FaceRes / 4, 32, 256));
	// 方向乘 2：整个球面约 2 个低频波长，莫霍面起伏是大陆尺度的。
	Ctx.Fields.MohoWobble.Fill([NoiseSeed, Octaves](const FVector3f& Dir)
	{
		return GeoNoise::Fbm3(NoiseSeed, Dir * 2.f, Octaves);
	}, Ctx.Config.bSingleThreadBake);
}
```

- [ ] **Step 5：`PlanetBakePipeline.cpp` 顶部加 `#include "Stage01Sphere.h"`，`MakeDefault` 改为**

```cpp
FPlanetBakePipeline FPlanetBakePipeline::MakeDefault()
{
	FPlanetBakePipeline P;
	P.AddStage(MakeUnique<FStage01Sphere>());
	return P;
}
```

- [ ] **Step 6：构建并跑** `StellarEra.Planet.Geo.Stage01` 与 `StellarEra.Planet.Geo.Pipeline`，期望都 `Result={Success}`。

- [ ] **Step 7：提交**
  ```bash
  cd /f/workspace/ue/StellarEra && D=Source/StellarEraPlanet/Src/Geo/Bake && P="$D/Stages/Stage01Sphere.h $D/Stages/Stage01Sphere.cpp $D/PlanetBakePipeline.cpp Source/StellarEraPlanet/Src/Tests/PlanetGeoStage01Tests.cpp"
  git add -- $P && git commit -m "feat(地质): 阶段 1 圈层与莫霍面起伏

  Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>" -- $P
  ```

## Task 10：配方资产 `UPlanetRecipe`（M1）

**Files:**
- Create: `Source/StellarEraPlanet/Src/Geo/Assets/PlanetRecipe.h`、`PlanetRecipe.cpp`
- Test: `Source/StellarEraPlanet/Src/Tests/PlanetGeoRecipeTests.cpp`

**Interfaces:**
- Consumes：`FPlanetRecipeConfig`、`FPlanetSphereParams`（Task 8）。
- Produces：`UPlanetRecipe : UPrimaryDataAsset`，字段 `Radius`(float 40000)、`VoxelSizeCm`(100)、`Seed`(int32 1337)、`FaceRes`(1024)、`CrustRatio`(0.25)、`UpperMantleRatio`(0.38)、`TransitionRatio`(0.42)、`MantleOuterRatio`(0.55)、`OuterCoreRatio`(0.80)、`MohoWobbleRatio`(0.01)、`WobbleOctaves`(3)；`FPlanetRecipeConfig ToConfig() const`（只在游戏线程调用）。

- [ ] **Step 1：写失败测试** `PlanetGeoRecipeTests.cpp`

```cpp
﻿// 资产 → POD 是烘焙期唯一碰 UObject 的地方：字段漏拷就是「面板上改了没效果」。
#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "PlanetRecipe.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGeoRecipeTest, "StellarEra.Planet.Geo.Recipe",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGeoRecipeTest::RunTest(const FString& Parameters)
{
	UPlanetRecipe* Recipe = NewObject<UPlanetRecipe>();
	const FPlanetRecipeConfig Default = Recipe->ToConfig();
	TestEqual(TEXT("默认半径 40000"), Default.Radius, 40000.0);

	Recipe->Radius = 1234.f; Recipe->VoxelSizeCm = 50.f; Recipe->Seed = 77; Recipe->FaceRes = 512;
	Recipe->CrustRatio = 0.2f; Recipe->UpperMantleRatio = 0.3f; Recipe->TransitionRatio = 0.35f;
	Recipe->MantleOuterRatio = 0.5f; Recipe->OuterCoreRatio = 0.75f; Recipe->MohoWobbleRatio = 0.02f; Recipe->WobbleOctaves = 4;
	const FPlanetRecipeConfig C = Recipe->ToConfig();
	TestEqual(TEXT("Radius"), C.Radius, 1234.0);
	TestEqual(TEXT("VoxelSizeCm"), C.VoxelSizeCm, 50.f);
	TestEqual(TEXT("Seed"), C.Seed, uint64(77));
	TestEqual(TEXT("FaceRes"), C.FaceRes, 512);
	TestEqual(TEXT("CrustRatio"), C.Sphere.CrustRatio, 0.2f);
	TestEqual(TEXT("UpperMantleRatio"), C.Sphere.UpperMantleRatio, 0.3f);
	TestEqual(TEXT("TransitionRatio"), C.Sphere.TransitionRatio, 0.35f);
	TestEqual(TEXT("MantleOuterRatio"), C.Sphere.MantleOuterRatio, 0.5f);
	TestEqual(TEXT("OuterCoreRatio"), C.Sphere.OuterCoreRatio, 0.75f);
	TestEqual(TEXT("MohoWobbleRatio"), C.Sphere.MohoWobbleRatio, 0.02f);
	TestEqual(TEXT("WobbleOctaves"), C.Sphere.WobbleOctaves, 4);
	TestTrue(TEXT("改了字段配方哈希就变"), C.Hash() != Default.Hash());
	return true;
}

#endif
```

- [ ] **Step 2：构建**，期望因缺 `PlanetRecipe.h` 失败。

- [ ] **Step 3：实现 `PlanetRecipe.h`**

```cpp
﻿// 一颗星的地质配方资产，作者唯一入口。烘焙前在游戏线程 ToConfig() 拷成 POD。
#pragma once
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "PlanetRecipeConfig.h"
#include "PlanetRecipe.generated.h"

UCLASS(BlueprintType)
class STELLARERAPLANET_API UPlanetRecipe : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/** 星球半径（体素）。UniverseMap 的行星是 40000。 */
	UPROPERTY(EditAnywhere, Category = "Planet", meta = (ClampMin = "256"))
	float Radius = 40000.f;

	UPROPERTY(EditAnywhere, Category = "Planet", meta = (ClampMin = "1"))
	float VoxelSizeCm = 100.f;

	UPROPERTY(EditAnywhere, Category = "Planet")
	int32 Seed = 1337;

	/** 宏观球面场每面分辨率。 */
	UPROPERTY(EditAnywhere, Category = "Planet", meta = (ClampMin = "32", ClampMax = "2048"))
	int32 FaceRes = 1024;

	/** 圈层底界：从地表向下的累计深度占 R，必须严格递增（阶段 1 校验）。 */
	UPROPERTY(EditAnywhere, Category = "Stage1|Layers", meta = (ClampMin = "0.01", ClampMax = "0.99"))
	float CrustRatio = 0.25f;
	UPROPERTY(EditAnywhere, Category = "Stage1|Layers", meta = (ClampMin = "0.01", ClampMax = "0.99"))
	float UpperMantleRatio = 0.38f;
	UPROPERTY(EditAnywhere, Category = "Stage1|Layers", meta = (ClampMin = "0.01", ClampMax = "0.99"))
	float TransitionRatio = 0.42f;
	UPROPERTY(EditAnywhere, Category = "Stage1|Layers", meta = (ClampMin = "0.01", ClampMax = "0.99"))
	float MantleOuterRatio = 0.55f;
	UPROPERTY(EditAnywhere, Category = "Stage1|Layers", meta = (ClampMin = "0.01", ClampMax = "0.99"))
	float OuterCoreRatio = 0.80f;

	UPROPERTY(EditAnywhere, Category = "Stage1|Moho", meta = (ClampMin = "0", ClampMax = "0.1"))
	float MohoWobbleRatio = 0.01f;
	UPROPERTY(EditAnywhere, Category = "Stage1|Moho", meta = (ClampMin = "1", ClampMax = "6"))
	int32 WobbleOctaves = 3;

	FPlanetRecipeConfig ToConfig() const;
};
```

- [ ] **Step 4：实现 `PlanetRecipe.cpp`**

```cpp
﻿#include "PlanetRecipe.h"

FPlanetRecipeConfig UPlanetRecipe::ToConfig() const
{
	check(IsInGameThread());
	FPlanetRecipeConfig C;
	C.Radius = double(Radius);
	C.VoxelSizeCm = VoxelSizeCm;
	// 面板是 int32；经 uint32 扩宽，负种子也得到稳定的 64 位值。
	C.Seed = uint64(uint32(Seed));
	C.FaceRes = FaceRes;
	C.Sphere.CrustRatio = CrustRatio;
	C.Sphere.UpperMantleRatio = UpperMantleRatio;
	C.Sphere.TransitionRatio = TransitionRatio;
	C.Sphere.MantleOuterRatio = MantleOuterRatio;
	C.Sphere.OuterCoreRatio = OuterCoreRatio;
	C.Sphere.MohoWobbleRatio = MohoWobbleRatio;
	C.Sphere.WobbleOctaves = WobbleOctaves;
	return C;
}
```

- [ ] **Step 5：构建并跑** `StellarEra.Planet.Geo.Recipe`，期望 `Result={Success}`。

- [ ] **Step 6：提交**
  ```bash
  cd /f/workspace/ue/StellarEra && D=Source/StellarEraPlanet/Src/Geo/Assets && P="$D/PlanetRecipe.h $D/PlanetRecipe.cpp Source/StellarEraPlanet/Src/Tests/PlanetGeoRecipeTests.cpp"
  git add -- $P && git commit -m "feat(地质): 配方资产 UPlanetRecipe

  Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>" -- $P
  ```

## Task 11：体素适配器 `UPlanetGeoGenerator`（M1）

**Files:**
- Create: `Source/StellarEraPlanet/Src/Geo/Voxel/PlanetGeoGenerator.h`、`PlanetGeoGenerator.cpp`
- Modify: `Source/StellarEraPlanet/Src/Geo/Snapshot/GeoMaterialCodec.h`（加 `EncodeDebugColor`）
- Test: `Source/StellarEraPlanet/Src/Tests/PlanetGeoGeneratorTests.cpp`

**Interfaces:**
- Consumes：`FPlanetSnapshot`、`PlanetShapeEval::{Value, ValueRange}`、`PlanetMaterialEval::Sample`、`GeoMaterialCodec::Encode`、`GeoLithology::DebugColor`。
- Produces：
  - `GeoMaterialCodec::EncodeDebugColor(const FGeoSample&) -> FGeoPackedMaterial`：RGB 写岩性调试色，其余同 `Encode`。只给 M1 测试星球肉眼分层用，Task 14 定方案后删除。
  - `UPlanetGeoGenerator : UVoxelGenerator`：`SetSnapshot(TSharedPtr<const FPlanetSnapshot>)`、`GetSnapshot()`、`UPROPERTY(VisibleAnywhere) bool bDebugLithologyColor = true`、`GetInstance()`。
  - `FPlanetGeoGeneratorInstance : TVoxelGeneratorInstanceHelper<FPlanetGeoGeneratorInstance, UPlanetGeoGenerator>`：`GetValueImpl`、`GetMaterialImpl`、`GetValueRangeImpl`、`GetUpVector`。
  - 体素坐标原点 = 球心。快照为空时 `GetInstance` 打 Error 并返回纯空气实例。

- [ ] **Step 1：写失败测试** `PlanetGeoGeneratorTests.cpp`

```cpp
﻿// 适配器必须是快照的薄转发：密度、值域、材质与求值核逐点一致，否则体素世界与仪器查询分叉。
#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "PlanetGeoGenerator.h"
#include "PlanetBakePipeline.h"
#include "PlanetShapeEval.h"
#include "PlanetMaterialEval.h"
#include "GeoMaterialCodec.h"
#include "GeoRng.h"
#include "VoxelIntBox.h"
#include "VoxelItemStack.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGeoGeneratorTest, "StellarEra.Planet.Geo.Generator",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGeoGeneratorTest::RunTest(const FString& Parameters)
{
	FPlanetRecipeConfig Config;
	Config.Radius = 2000.0;
	Config.FaceRes = 128;
	FPlanetBakeReport Report;
	const TSharedPtr<const FPlanetSnapshot> Snap = FPlanetBakePipeline::MakeDefault().Bake(Config, Report);
	if (!TestTrue(TEXT("烘焙成功"), Snap.IsValid())) { return false; }

	UPlanetGeoGenerator* Gen = NewObject<UPlanetGeoGenerator>();
	Gen->bDebugLithologyColor = false;
	Gen->SetSnapshot(Snap);
	const TVoxelSharedRef<FVoxelGeneratorInstance> Base = Gen->GetInstance();
	const FPlanetGeoGeneratorInstance& Inst = static_cast<const FPlanetGeoGeneratorInstance&>(*Base);
	const FVoxelItemStack& Items = FVoxelItemStack::Empty;

	FGeoRng Rng = FGeoRng::Derive(3, 0, 0);
	bool bValue = true, bMaterial = true, bRange = true;
	for (int32 i = 0; i < 5000; ++i)
	{
		const FVector3d P(Rng.Range(-2200.f, 2200.f), Rng.Range(-2200.f, 2200.f), Rng.Range(-2200.f, 2200.f));
		bValue &= float(Inst.GetValueImpl(v_flt(P.X), v_flt(P.Y), v_flt(P.Z), 0, Items)) == PlanetShapeEval::Value(*Snap, P);
		const FVoxelMaterial M = Inst.GetMaterialImpl(v_flt(P.X), v_flt(P.Y), v_flt(P.Z), 0, Items);
		const FGeoPackedMaterial E = GeoMaterialCodec::Encode(PlanetMaterialEval::Sample(*Snap, P));
		bMaterial &= M.GetR() == E.R && M.GetG() == E.G && M.GetB() == E.B && M.GetA() == E.A && M.GetU0() == E.U0 && M.GetV0() == E.V0;

		const FIntVector C(FMath::FloorToInt(P.X), FMath::FloorToInt(P.Y), FMath::FloorToInt(P.Z));
		const int32 H = 1 + int32(Rng.NextU64() % 32);
		const FVoxelIntBox Box(C - FIntVector(H), C + FIntVector(H));
		const TVoxelRange<v_flt> R = Inst.GetValueRangeImpl(Box, 0, Items);
		const FVector3d Q(FMath::Lerp(double(Box.Min.X), double(Box.Max.X), double(Rng.NextFloat())),
			FMath::Lerp(double(Box.Min.Y), double(Box.Max.Y), double(Rng.NextFloat())),
			FMath::Lerp(double(Box.Min.Z), double(Box.Max.Z), double(Rng.NextFloat())));
		const v_flt V = Inst.GetValueImpl(v_flt(Q.X), v_flt(Q.Y), v_flt(Q.Z), 0, Items);
		bRange &= (V >= R.Min && V <= R.Max);
	}
	TestTrue(TEXT("密度 == 求值核"), bValue);
	TestTrue(TEXT("材质 == 编解码器(求值核)"), bMaterial);
	TestTrue(TEXT("值域覆盖盒内采样"), bRange);
	TestTrue(TEXT("上方向 = 径向"), Inst.GetUpVector(0.0, 0.0, 1000.0).Equals(FVector::UpVector, 1e-6));

	UPlanetGeoGenerator* Empty = NewObject<UPlanetGeoGenerator>();
	AddExpectedError(TEXT("没有快照"), EAutomationExpectedErrorFlags::Contains, 1);
	const TVoxelSharedRef<FVoxelGeneratorInstance> EmptyBase = Empty->GetInstance();
	const FPlanetGeoGeneratorInstance& EI = static_cast<const FPlanetGeoGeneratorInstance&>(*EmptyBase);
	TestTrue(TEXT("无快照 → 空气"), EI.GetValueImpl(0, 0, 0, 0, Items) > 0);
	return true;
}

#endif
```

- [ ] **Step 2：构建**，期望因缺 `PlanetGeoGenerator.h` 失败。

- [ ] **Step 3：`GeoMaterialCodec.h` 顶部加 `#include "GeoLithology.h"`，namespace 末尾追加**

```cpp
	/** M1 调试：RGB 直接写岩性调试色，让现成顶点色材质看得出分层。Task 14 选定方案后删除。 */
	FORCEINLINE FGeoPackedMaterial EncodeDebugColor(const FGeoSample& S)
	{
		FGeoPackedMaterial M = Encode(S);
		const FColor C = GeoLithology::DebugColor(S.Lithology);
		M.R = C.R; M.G = C.G; M.B = C.B;
		return M;
	}
```

- [ ] **Step 4：实现 `PlanetGeoGenerator.h`**

```cpp
﻿// VoxelFree 适配器：只把体素坐标转给快照求值核，自己不含地质数学。
#pragma once
#include "CoreMinimal.h"
#include "VoxelGenerators/VoxelGeneratorHelpers.h"
#include "PlanetSnapshot.h"
#include "PlanetGeoGenerator.generated.h"

UCLASS()
class STELLARERAPLANET_API UPlanetGeoGenerator : public UVoxelGenerator
{
	GENERATED_BODY()

public:
	virtual TVoxelSharedRef<FVoxelGeneratorInstance> GetInstance() override;

	void SetSnapshot(TSharedPtr<const FPlanetSnapshot> InSnapshot) { Snapshot = MoveTemp(InSnapshot); }
	TSharedPtr<const FPlanetSnapshot> GetSnapshot() const { return Snapshot; }

	/** 必须 VisibleAnywhere：UVoxelGenerator 会把 EditAnywhere 属性当生成器参数枚举，只认少数类型。 */
	UPROPERTY(VisibleAnywhere, Category = "Geo")
	bool bDebugLithologyColor = true;

private:
	TSharedPtr<const FPlanetSnapshot> Snapshot;
};

class FPlanetGeoGeneratorInstance : public TVoxelGeneratorInstanceHelper<FPlanetGeoGeneratorInstance, UPlanetGeoGenerator>
{
public:
	using Super = TVoxelGeneratorInstanceHelper<FPlanetGeoGeneratorInstance, UPlanetGeoGenerator>;

	FPlanetGeoGeneratorInstance(UPlanetGeoGenerator& Owner, TSharedPtr<const FPlanetSnapshot> InSnapshot, bool bInDebugColor);

	v_flt GetValueImpl(v_flt X, v_flt Y, v_flt Z, int32 LOD, const FVoxelItemStack& Items) const;
	FVoxelMaterial GetMaterialImpl(v_flt X, v_flt Y, v_flt Z, int32 LOD, const FVoxelItemStack& Items) const;
	TVoxelRange<v_flt> GetValueRangeImpl(const FVoxelIntBox& Bounds, int32 LOD, const FVoxelItemStack& Items) const;
	/** 基类纯虚，Helper 不提供默认实现：行星上「上」是径向。 */
	virtual FVector GetUpVector(v_flt X, v_flt Y, v_flt Z) const override final;

private:
	const TSharedPtr<const FPlanetSnapshot> Snapshot;
	const bool bDebugColor;
};
```

- [ ] **Step 5：实现 `PlanetGeoGenerator.cpp`**

```cpp
﻿#include "PlanetGeoGenerator.h"
#include "GeoMaterialCodec.h"
#include "PlanetMaterialEval.h"
#include "PlanetShapeEval.h"
#include "VoxelIntBox.h"

TVoxelSharedRef<FVoxelGeneratorInstance> UPlanetGeoGenerator::GetInstance()
{
	if (!Snapshot)
	{
		UE_LOG(LogTemp, Error, TEXT("[地质] UPlanetGeoGenerator 没有快照，生成空气"));
	}
	return MakeVoxelShared<FPlanetGeoGeneratorInstance>(*this, Snapshot, bDebugLithologyColor);
}

FPlanetGeoGeneratorInstance::FPlanetGeoGeneratorInstance(
	UPlanetGeoGenerator& Owner, TSharedPtr<const FPlanetSnapshot> InSnapshot, bool bInDebugColor)
	: Super(&Owner)
	, Snapshot(MoveTemp(InSnapshot))
	, bDebugColor(bInDebugColor)
{
}

v_flt FPlanetGeoGeneratorInstance::GetValueImpl(v_flt X, v_flt Y, v_flt Z, int32 LOD, const FVoxelItemStack& Items) const
{
	if (!Snapshot) { return 1; }
	return v_flt(PlanetShapeEval::Value(*Snapshot, FVector3d(X, Y, Z)));
}

FVoxelMaterial FPlanetGeoGeneratorInstance::GetMaterialImpl(v_flt X, v_flt Y, v_flt Z, int32 LOD, const FVoxelItemStack& Items) const
{
	FVoxelMaterial M(ForceInit);
	if (!Snapshot) { return M; }
	const FGeoSample S = PlanetMaterialEval::Sample(*Snapshot, FVector3d(X, Y, Z));
	const FGeoPackedMaterial P = bDebugColor ? GeoMaterialCodec::EncodeDebugColor(S) : GeoMaterialCodec::Encode(S);
	M.SetR(P.R); M.SetG(P.G); M.SetB(P.B); M.SetA(P.A);
	M.SetU0(P.U0); M.SetV0(P.V0); M.SetU1(P.U1); M.SetV1(P.V1);
	return M;
}

TVoxelRange<v_flt> FPlanetGeoGeneratorInstance::GetValueRangeImpl(const FVoxelIntBox& Bounds, int32 LOD, const FVoxelItemStack& Items) const
{
	if (!Snapshot) { return TVoxelRange<v_flt>(1); }
	const FGeoRange R = PlanetShapeEval::ValueRange(*Snapshot,
		FVector3d(Bounds.Min.X, Bounds.Min.Y, Bounds.Min.Z), FVector3d(Bounds.Max.X, Bounds.Max.Y, Bounds.Max.Z));
	return TVoxelRange<v_flt>(v_flt(R.Lo), v_flt(R.Hi));
}

FVector FPlanetGeoGeneratorInstance::GetUpVector(v_flt X, v_flt Y, v_flt Z) const
{
	const FVector P(X, Y, Z);
	return P.SizeSquared() < UE_DOUBLE_SMALL_NUMBER ? FVector::UpVector : P.GetSafeNormal();
}
```

- [ ] **Step 6：构建并跑** `StellarEra.Planet.Geo.Generator`，期望 `Result={Success}`。

- [ ] **Step 7：提交**
  ```bash
  cd /f/workspace/ue/StellarEra && G=Source/StellarEraPlanet/Src/Geo && P="$G/Voxel/PlanetGeoGenerator.h $G/Voxel/PlanetGeoGenerator.cpp $G/Snapshot/GeoMaterialCodec.h Source/StellarEraPlanet/Src/Tests/PlanetGeoGeneratorTests.cpp"
  git add -- $P && git commit -m "feat(地质): VoxelFree 适配器 UPlanetGeoGenerator

  Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>" -- $P
  ```

## Task 12：依赖边界测试（M1）

**Files:**
- Test: `Source/StellarEraPlanet/Src/Tests/PlanetGeoDependencyTests.cpp`

**Interfaces:** 无新 API；守住 Global Constraints 的目录依赖规则。

- [ ] **Step 1：写测试** `PlanetGeoDependencyTests.cpp`

```cpp
﻿// 目录边界靠这条测试守：Core/Snapshot 一旦 include 了 Engine 或 Voxel，求值核就不能脱离引擎测试。
#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGeoDependencyTest, "StellarEra.Planet.Geo.Dependency",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGeoDependencyTest::RunTest(const FString& Parameters)
{
	const FString GeoDir = FPaths::Combine(FPaths::ProjectDir(), TEXT("Source/StellarEraPlanet/Src/Geo"));
	struct FRule { const TCHAR* SubDir; TArray<FString> Banned; };
	const TArray<FString> PureBanned = { TEXT("Engine/"), TEXT("CoreUObject"), TEXT("UObject/"), TEXT("GameFramework/"),
		TEXT("Components/"), TEXT("\"Voxel"), TEXT(".generated.h") };
	const FRule Rules[] = {
		{ TEXT("Core"), PureBanned },
		{ TEXT("Snapshot"), PureBanned },
		{ TEXT("Bake"), { TEXT("Engine/World"), TEXT("GameFramework/"), TEXT("Components/"), TEXT("\"Voxel"), TEXT(".generated.h") } },
		// Assets 自己的 .generated.h 合法，只禁体素插件。
		{ TEXT("Assets"), { TEXT("\"Voxel") } },
	};

	int32 Scanned = 0;
	for (const FRule& Rule : Rules)
	{
		TArray<FString> Files;
		IFileManager::Get().FindFilesRecursive(Files, *FPaths::Combine(GeoDir, Rule.SubDir), TEXT("*.*"), true, false);
		for (const FString& File : Files)
		{
			if (!File.EndsWith(TEXT(".h")) && !File.EndsWith(TEXT(".cpp"))) { continue; }
			TArray<FString> Lines;
			FFileHelper::LoadFileToStringArray(Lines, *File);
			++Scanned;
			for (const FString& Line : Lines)
			{
				if (!Line.TrimStart().StartsWith(TEXT("#include"))) { continue; }
				for (const FString& Bad : Rule.Banned)
				{
					if (Line.Contains(Bad))
					{
						AddError(FString::Printf(TEXT("%s 违反 Geo/%s 依赖规则：%s"),
							*FPaths::GetCleanFilename(File), Rule.SubDir, *Line.TrimStartAndEnd()));
					}
				}
			}
		}
	}
	TestTrue(TEXT("确实扫到了文件"), Scanned >= 15);
	return true;
}

#endif
```

  `"Voxel` 带引号前缀：只匹配 `#include "Voxel…"`，不误伤 `PlanetGeoGenerator.h` 这类不在受限目录里的文件名。

- [ ] **Step 2：构建并跑** `StellarEra.Planet.Geo.Dependency`，期望 `Result={Success}`。
  反向验证（不提交）：在 `GeoRng.h` 临时加 `#include "Engine/World.h"`，重建再跑，期望 `Result={Fail}` 且错误点名 `GeoRng.h`；然后删掉这行。

- [ ] **Step 3：提交**
  ```bash
  cd /f/workspace/ue/StellarEra && P="Source/StellarEraPlanet/Src/Tests/PlanetGeoDependencyTests.cpp"
  git add -- $P && git commit -m "test(地质): Geo 目录依赖边界扫描

  Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>" -- $P
  ```

## Task 13：独立测试星球 `AGeoTestPlanetActor`（M1）

**Files:**
- Create: `Source/StellarEraPlanet/Src/Geo/Voxel/GeoTestPlanetActor.h`、`GeoTestPlanetActor.cpp`
- Create（编辑器）: `Content/Code/Maps/GeoTestMap.umap`、`Content/Code/Planet/Geo/DA_GeoRecipe_Test.uasset`（R=40000）、`DA_GeoRecipe_Small.uasset`（R=256，用来挖剖面）
- Test: `Source/StellarEraPlanet/Src/Tests/PlanetGeoTestPlanetTests.cpp`

**Interfaces:**
- Consumes：`UPlanetRecipe::ToConfig`、`FPlanetBakePipeline::MakeDefault().Bake`、`UPlanetGeoGenerator::SetSnapshot`。
- Produces：`AGeoTestPlanetActor : AVoxelWorld`：`UPROPERTY(EditAnywhere) TObjectPtr<UPlanetRecipe> Recipe`；`void StartBake()`（游戏线程拷配方 → 后台烘焙 → 回游戏线程建世界）；`GetSnapshot()`；`IsBakeInFlight()`；
  `static int32 ComputeWorldSize(double RadiusVoxels)`（半径 3 倍取 2 的幂，与 `AVoxelPlanetActor` 同口径）。
  旧行星 `AVoxelPlanetActor` 不改；新星球只出现在 `GeoTestMap`。

- [ ] **Step 1：写失败测试** `PlanetGeoTestPlanetTests.cpp`（editor world 不跑 BeginPlay，只测 CDO 与纯函数）

```cpp
﻿// 世界边长取错会把整个立方体切到 MaxLOD（旧行星踩过）；这里钉死与 AVoxelPlanetActor 同一口径。
#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "GeoTestPlanetActor.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGeoTestPlanetTest, "StellarEra.Planet.Geo.TestPlanet",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGeoTestPlanetTest::RunTest(const FString& Parameters)
{
	const AGeoTestPlanetActor* Cdo = GetDefault<AGeoTestPlanetActor>();
	TestFalse(TEXT("不自动建世界（由 StartBake 建）"), Cdo->bCreateWorldAutomatically);
	TestFalse(TEXT("不自建全局池"), Cdo->bCreateGlobalPool);
	TestTrue(TEXT("RGB 材质配置"), Cdo->MaterialConfig == EVoxelMaterialConfig::RGB);
	TestEqual(TEXT("R=40000 → 世界边长 131072"), AGeoTestPlanetActor::ComputeWorldSize(40000.0), 131072);
	TestEqual(TEXT("R=1000 → 4096"), AGeoTestPlanetActor::ComputeWorldSize(1000.0), 4096);
	return true;
}

#endif
```

- [ ] **Step 2：构建**，期望因缺 `GeoTestPlanetActor.h` 失败。

- [ ] **Step 3：实现 `GeoTestPlanetActor.h`**

```cpp
﻿// 新地质流水线的独立测试星球：M0–M3 只在 GeoTestMap 里，不碰旧行星；M4 切换后删除。
#pragma once
#include "CoreMinimal.h"
#include "VoxelWorld.h"
#include "PlanetSnapshot.h"
#include "GeoTestPlanetActor.generated.h"

class UPlanetGeoGenerator;
class UPlanetRecipe;

UCLASS()
class STELLARERAPLANET_API AGeoTestPlanetActor : public AVoxelWorld
{
	GENERATED_BODY()

public:
	AGeoTestPlanetActor();

	UPROPERTY(EditAnywhere, Category = "Geo")
	TObjectPtr<UPlanetRecipe> Recipe;

	/** 拷配方 → 后台烘焙 → 回游戏线程建世界。烘焙中再次调用会被忽略。 */
	void StartBake();
	bool IsBakeInFlight() const { return bBakeInFlight; }
	TSharedPtr<const FPlanetSnapshot> GetSnapshot() const { return Snapshot; }

	static int32 ComputeWorldSize(double RadiusVoxels);

protected:
	virtual void BeginPlay() override;

private:
	UPROPERTY(Transient)
	TObjectPtr<UPlanetGeoGenerator> GeoGenerator;

	TSharedPtr<const FPlanetSnapshot> Snapshot;
	bool bBakeInFlight = false;

	void OnBakeFinished(TSharedPtr<const FPlanetSnapshot> InSnapshot, double RadiusVoxels, double BakeSeconds);
};
```

- [ ] **Step 4：实现 `GeoTestPlanetActor.cpp`**

```cpp
﻿#include "GeoTestPlanetActor.h"
#include "Async/Async.h"
#include "Engine/World.h"
#include "HAL/PlatformTime.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"
#include "VoxelGenerators/VoxelGeneratorPicker.h"
#include "VoxelTools/VoxelBlueprintLibrary.h"
#include "PlanetBakePipeline.h"
#include "PlanetGeoGenerator.h"
#include "PlanetRecipe.h"

AGeoTestPlanetActor::AGeoTestPlanetActor()
{
	// 世界在烘焙完成后显式创建；基类自动建会在快照就绪前用空生成器建一次。
	bCreateWorldAutomatically = false;
	bCreateGlobalPool = false;
	MaterialConfig = EVoxelMaterialConfig::RGB;
	MaxLOD = 12;
	// 与行星同值：基类 1000 ms 预算会让一批网格在一帧内全提交。
	bDitherChunks = false;
	MeshUpdatesBudget = 8.f;
#if WITH_EDITOR
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Mat(
		TEXT("/Game/Code/Planet/Materials/M_PlanetVertexColor.M_PlanetVertexColor"));
	if (Mat.Succeeded()) { VoxelMaterial = Mat.Object; }
#endif
}

int32 AGeoTestPlanetActor::ComputeWorldSize(double RadiusVoxels)
{
	return int32(FMath::RoundUpToPowerOfTwo(uint32(FMath::CeilToInt(RadiusVoxels * 3.0))));
}

void AGeoTestPlanetActor::BeginPlay()
{
	Super::BeginPlay();
	StartBake();
}

void AGeoTestPlanetActor::StartBake()
{
	if (bBakeInFlight) { return; }
	if (!Recipe)
	{
		UE_LOG(LogTemp, Error, TEXT("[地质] %s 没有配方"), *GetName());
		return;
	}
	bBakeInFlight = true;
	const FPlanetRecipeConfig Config = Recipe->ToConfig();   // 唯一一次碰 UObject
	TWeakObjectPtr<AGeoTestPlanetActor> WeakThis(this);
	Async(EAsyncExecution::ThreadPool, [WeakThis, Config]()
	{
		const double T0 = FPlatformTime::Seconds();
		FPlanetBakeReport Report;
		TSharedPtr<const FPlanetSnapshot> Snap = FPlanetBakePipeline::MakeDefault().Bake(Config, Report);
		const double Seconds = FPlatformTime::Seconds() - T0;
		for (const FString& E : Report.Errors) { UE_LOG(LogTemp, Error, TEXT("%s"), *E); }
		AsyncTask(ENamedThreads::GameThread, [WeakThis, Snap, Radius = Config.Radius, Seconds]()
		{
			if (AGeoTestPlanetActor* Self = WeakThis.Get()) { Self->OnBakeFinished(Snap, Radius, Seconds); }
		});
	});
}

void AGeoTestPlanetActor::OnBakeFinished(TSharedPtr<const FPlanetSnapshot> InSnapshot, double RadiusVoxels, double BakeSeconds)
{
	bBakeInFlight = false;
	if (!InSnapshot) { return; }
	Snapshot = InSnapshot;
	UE_LOG(LogTemp, Log, TEXT("[地质] %s 烘焙完成：半径 %.0f 体素，%.2f s，快照 %.1f MB，哈希 %016llx"),
		*GetName(), RadiusVoxels, BakeSeconds, double(Snapshot->GetAllocatedSize()) / (1024.0 * 1024.0), Snapshot->ContentHash);

	if (!GeoGenerator) { GeoGenerator = NewObject<UPlanetGeoGenerator>(this, TEXT("GeoGenerator")); }
	GeoGenerator->SetSnapshot(Snapshot);
	SetWorldSize(ComputeWorldSize(RadiusVoxels));
	// 必须在 CreateWorld 前：插件在创建时快照生成器。
	Generator = FVoxelGeneratorPicker(GeoGenerator.Get());

	if (IsCreated()) { DestroyWorld(); }
	UWorld* World = GetWorld();
	if (World && World->IsGameWorld() && !UVoxelBlueprintLibrary::IsWorldVoxelPoolCreated(World))
	{
		UVoxelBlueprintLibrary::CreateWorldVoxelThreadPool(World, PriorityCategories, PriorityOffsets, NumberOfThreads, bConstantPriorities);
	}
	CreateWorld(FVoxelWorldCreateInfo());
}
```

- [ ] **Step 5：构建并跑** `StellarEra.Planet.Geo.TestPlanet`，期望 `Result={Success}`。若 `SetWorldSize` 编译不过，照抄 `Source/StellarEraPlanet/Src/VoxelPlanetActor.cpp:612` 的调用（同一基类上的同一函数）。

- [ ] **Step 6：建资产与测试关卡**（编辑器开着走 MCP `execute_python_code`；**新建空关卡，不要复制 UniverseMap 再清空**，见 CLAUDE.md 已知坑）

```python
import unreal
at = unreal.AssetToolsHelpers.get_asset_tools()
def make_recipe(name, radius):
    r = at.create_asset(name, '/Game/Code/Planet/Geo', unreal.PlanetRecipe, None)
    r.set_editor_property('radius', radius)
    unreal.EditorAssetLibrary.save_loaded_asset(r)
    return r
big = make_recipe('DA_GeoRecipe_Test', 40000.0)
small = make_recipe('DA_GeoRecipe_Small', 256.0)

les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
les.new_level('/Game/Code/Maps/GeoTestMap')
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
for recipe, loc in ((big, unreal.Vector(0, 0, 0)), (small, unreal.Vector(0, 0, 6000000))):
    a = eas.spawn_actor_from_class(unreal.GeoTestPlanetActor, loc)
    a.set_editor_property('recipe', recipe)
    a.set_editor_property('is_spatially_loaded', False)   # WP 关卡里脚本 spawn 的 Actor 默认看不见
ps = eas.spawn_actor_from_class(unreal.PlayerStart, unreal.Vector(0, 0, 6030000))  # 小星球上方 4 km
ps.set_editor_property('is_spatially_loaded', False)
les.save_current_level()
unreal.log('GEO_M1 map ok')
```
  R=256 的小星球：大星球离它 60 km，彼此不会挡住。它只用来挖剖面，因为 R=40000 时地壳厚 1 万体素，挖不到地幔。

- [ ] **Step 7：PIE 验证**（编辑器在前台）：打开 `GeoTestMap` 进 PIE。
  1. 日志里两条 `[地质] … 烘焙完成`，记下 R=40000 那条的秒数与 MB。
  2. 小星球表面是地壳调试色（灰褐 `150,130,110`）。
  3. PIE 运行中用 MCP 挖穿小星球的一角：
     ```python
     import unreal
     w = unreal.EditorLevelLibrary.get_game_world()
     small = [a for a in unreal.GameplayStatics.get_all_actors_of_class(w, unreal.GeoTestPlanetActor)
              if a.get_editor_property('recipe').get_name() == 'DA_GeoRecipe_Small'][0]
     c = small.get_actor_location()
     unreal.VoxelSphereTools.remove_sphere(small, c + unreal.Vector(0, 0, 25600), 160.0)   # 单位：体素（bConvertToVoxelSpace 默认真时传世界坐标，见下）
     ```
     `remove_sphere` 的 Position 是世界坐标（厘米），Radius 是体素数时把 `convert_to_voxel_space` 保持默认；若 Python 里没有 `remove_sphere`，改用 `set_value_sphere(small, pos, 160.0, 1.0)`。
     挖出的坑壁应依次露出地壳 → 上地幔（绿 `70,110,60`）→ 过渡带（蓝 `60,90,120`）。
  4. `stat unit` 记录静止与绕大星球移动时的 Game / GPU 毫秒，写进 Task 15 的报告。
  任何一条不成立：不要改测试期望，先确认 DLL 是新编的（比 `Binaries/Win64/UnrealEditor-StellarEraPlanet.dll` 的 mtime），再回对应任务排查。

- [ ] **Step 8：提交**（资产走 LFS；WP 外部 Actor 包也要进 pathspec，`git status -- Content` 确认）
  ```bash
  cd /f/workspace/ue/StellarEra && P="Source/StellarEraPlanet/Src/Geo/Voxel/GeoTestPlanetActor.h Source/StellarEraPlanet/Src/Geo/Voxel/GeoTestPlanetActor.cpp Source/StellarEraPlanet/Src/Tests/PlanetGeoTestPlanetTests.cpp Content/Code/Maps/GeoTestMap.umap Content/Code/Planet/Geo Content/__ExternalActors__/Code/Maps/GeoTestMap"
  git add -- $P && git commit -m "feat(地质): 独立测试星球与 GeoTestMap

  Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>" -- $P
  ```
  关卡不是 WP 时没有 `__ExternalActors__` 目录，从 pathspec 里去掉它。

## Task 14：材质承载方案原型（M0，探针）

本任务是**探针**：产出一条决定写回规格 §6。为对比写的代码都是一次性的，不进主干。

**Files:**
- Modify（临时，结束时还原）: `Source/StellarEraPlanet/Src/Geo/Snapshot/PlanetMaterialEval.h`、`Source/StellarEraPlanet/Src/Geo/Voxel/GeoTestPlanetActor.cpp`、`Source/StellarEraPlanet/Src/Geo/Voxel/PlanetGeoGenerator.cpp`
- Modify（最终）: `docs/superpowers/specs/2026-11-12-geology-pipeline-design.md` §6、`docs/superpowers/specs/2026-11-12-geology-pipeline/11-material-field.md` §2

- [ ] **Step 1：造跨大类硬接触**：`PlanetMaterialEval::Sample` 末尾临时加
  ```cpp
  S.Primitives.QueryPoint(FVector3f(P), [&](int32 i) { if (GeoPrimitiveEvaluate(S.Primitives.GetPrimitives()[i], FVector3f(P)) > 0.5f) { Out.Lithology = S.Primitives.GetPrimitives()[i].LithologyId; } });
  ```
  `StartBake` 的 lambda 里 Bake 之后、交回游戏线程之前，临时用 `ConstCastSharedPtr<FPlanetSnapshot>(Snap)->Primitives.Build(...)` 塞 3 个团块：`MakeBlob(Dir*(R-30), FVector3f(40), 0.05f)`，`LithologyId = OuterCore`，只对 R=256 的小星球做。PIE 挖开团块所在处，得到「地壳 ↔ 外核」接触面。
- [ ] **Step 2：方案 B 的基线（证实否决）**：`bDebugLithologyColor = false`，临时材质 `M_GeoDecodeProbe`：顶点色 R×255 取 `floor(x+0.5)` 查 7 色调色板。截图接触面，期望看到中间色带；记下色带宽度（像素与体素）。
- [ ] **Step 3：方案 A（MultiIndex）**：临时把 `MaterialConfig` 改成 `MultiIndex`，`GetMaterialImpl` 改为
  `FVoxelMaterialBuilder B; B.SetMaterialConfig(EVoxelMaterialConfig::MultiIndex); B.AddMultiIndex(S.Lithology, 1.f); return B.Build();`；
  材质用插件 `/Voxel/MaterialHelpers/Voxel_GetMultiIndexBlends` 取索引与权重混合调色板。截图同一接触面，记录是否只出现两端色、1 km 视距是否闪烁、`stat unit` 网格化耗时变化。
- [ ] **Step 4：方案 B 可行性**：在 `Plugins/VoxelFree/Source/Voxel/Public/VoxelRender/`（`VoxelChunkMesh.h`、`VoxelMesherAsyncWork.h`、`IVoxelRenderer.h`）找**项目侧可注册**的网格后处理回调。插件不能改，找不到就判 B 不可行。
- [ ] **Step 5：按规格 §6 判据定方案**（A / B / 退路「线索全走烘焙 cubemap，体素材质只存岩性」），在 §6 判据下加一行 `**结论（日期）**：…`，附截图路径与耗时数字；同步改附录 `11-material-field.md` §2。用 `git diff -- Source/StellarEraPlanet/Src/Geo` 确认只有探针改动，再 `git checkout -- <那三个文件>` 还原。
- [ ] **Step 6：提交文档**
  ```bash
  cd /f/workspace/ue/StellarEra && P="docs/superpowers/specs/2026-11-12-geology-pipeline-design.md docs/superpowers/specs/2026-11-12-geology-pipeline/11-material-field.md"
  git add -- $P && git commit -m "docs(地质): M0 材质承载方案定案

  Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>" -- $P
  ```

## Task 15：M0/M1 收尾验收

- [ ] **Step 1：全量地质测试**：跑 `StellarEra.Planet.Geo`，期望以下 12 条全部 `Result={Success}`：RngNoise、Field2D、Primitive、Profile、MaterialCodec、Snapshot、Pipeline、Stage01、Recipe、Generator、Dependency、TestPlanet。
- [ ] **Step 2：回归**：跑 `StellarEra`（全部项目测试），与开工前的失败清单对比，不得新增失败。旧系统没改，两份清单应完全一致。开工前的清单在 Task 2 开始前先跑一次存进 `$TEMP/geo_baseline.log`。
- [ ] **Step 3：全尺寸确定性与预算**：在 `PlanetGeoStage01Tests.cpp` 末尾追加：`Radius = 40000`、`FaceRes = 1024`，并行与单线程各烘一次，断言哈希相等；`AddInfo` 打出两次的 `StageSeconds` 与 `GetAllocatedSize()`。断言烘焙 ≤ 2 s、快照 ≤ 60 MB。
- [ ] **Step 4：更新 `Source/README.md`** 的「什么时候该建子目录」：补一句 `StellarEraPlanet/Src/Geo/Bake/Stages/` 是第二处三级目录，因为阶段数会随里程碑增长到 7 个。
- [ ] **Step 5：规格回填**：规格 §7 的 M0、M1 两行末尾标 `✅ 日期`，附 Task 13 Step 7 与本任务 Step 3 的实测数字（烘焙秒数、快照 MB、移动时 Game ms）。M1 验收原文「挖到球心」改为「在 R=256 小星球上挖穿地壳看到逐层岩性；R=40000 只验烘焙与可见性」。
- [ ] **Step 6：提交**
  ```bash
  cd /f/workspace/ue/StellarEra && P="Source/StellarEraPlanet/Src/Tests/PlanetGeoStage01Tests.cpp Source/README.md docs/superpowers/specs/2026-11-12-geology-pipeline-design.md"
  git add -- $P && git commit -m "docs(地质): M0/M1 验收记录

  Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>" -- $P
  ```
