# 矿物等值面矿体 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 把 H 键矿物观察里的「稀疏小方块」换成每元素一张 marching-cubes 等值面网格，球内呈现成团的平滑不规则矿体。

**Architecture:** 地质场新增按元素的强度出口 → 纯函数构建器 `PlanetMineralSurface::Build` 在 40³ 均匀网格上一次性算出所有元素场值，再逐元素跑 `UE::Geometry::FMarchingCubes` → 观察体为每个元素建一个 `UDynamicMeshComponent`（法线由 `FMeshNormals` 补齐）→ 组件按（行星、半径、缩放、密度）缓存构建结果。方块路径（`PlanetMineralBlocks`）整体删除。

**Tech Stack:** UE 5.8.3 / Win64；`GeometryCore`（`FMarchingCubes`、`FDynamicMesh3`、`FMeshNormals`）与 `GeometryFramework`（`UDynamicMeshComponent`），都是引擎自带运行时模块，不需要启用任何插件。

**Spec:** `docs/superpowers/specs/2026-10-25-mineral-ghost-view-design.md`（2026-10-25 修订版）。执行者开工前必须读它的 §4.2 与 §5.2。

## 与 spec 的两处实现级偏离（已定，不要改回去）

1. **`FPlanetMineralMesh` 不带 `Normals` 字段。** `FMarchingCubes` 只输出顶点与三角形，不输出法线；法线在视图 Actor 转 `FDynamicMesh3` 时由 `FMeshNormals::InitializeOverlayToPerVertexNormals` 按面积/角度加权算出。留一个恒空的字段只会误导后来人。
2. **材质用 `0.6 + 0.4 × |dot(N, V)|` 且 `TwoSided = true`。** spec §4.2 写的是 `saturate(dot(N, V))` 单面；但 MC 的绕序不保证朝外，单面渲染会把一半矿体的背面剔掉，`saturate` 也会让朝内那面变成死平。取绝对值 + 双面把它们一次消掉，平均亮度不变。

## Global Constraints

以下每条适用于**每个**任务，不再逐任务重复：

- **单位**：几何一律**厘米**；地质深度一律**体素**（基准球面为 0，向内为正），1 体素 = `AVoxelPlanetActor::VoxelSize` 厘米。
- **等比缩放假设 S = 1**：厘米换算不含 Actor 缩放。缓存键里带 `GetActorScale3D()` 只是为了让缩放变化时不静默复用旧网格，不代表 S ≠ 1 能算对。
- **模块分层**：`StellarEra → StellarEraVehicle → StellarEraPlanet`，`StellarEra → StellarEraCore`；`StellarEraVehicle` **不能**依赖 `StellarEra`。
- **自定义代码不进第三方插件目录**（`Plugins/VoxelFree/`、`Plugins/VibeUE/`），一律写进 `Source/` 下四个模块并改对应 `Build.cs`。
- **本轮不加第三方依赖**：只用引擎模块。`GeometryCore`、`GeometryFramework` 进 `StellarEraPlanet.Build.cs` 的 **Private** 依赖。
- **含中文的 `.h` / `.cpp` 必须存 UTF-8 with BOM**，否则 MSVC 按 GBK 读会报假错误（IntelliSense 红波浪线）。
- **不要统一换行符**：仓库本来就是 CRLF/LF 混合，`autocrlf=true` 下 CRLF 是稳定态。
- **构建必须关编辑器**，用捆绑 dotnet 直调 UBT、cwd 在项目外，**不要用 `Build.bat`**（锁机制会让 UBT 报找不到 .uproject）：
  ```bash
  cd "C:/" && "d:/Application/Epic Games/UE_5.8/Engine/Binaries/ThirdParty/DotNet/10.0/win-x64/dotnet.exe" \
    "d:/Application/Epic Games/UE_5.8/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.dll" \
    StellarEraEditor Win64 Development \
    -Project="F:/workspace/ue/StellarEra/StellarEra.uproject" -WaitMutex -FromMsBuild 2>&1 \
    | tee "F:/workspace/ue/StellarEra/Saved/Logs/MineralIsoBuild.log"
  ```
- **自动化测试**（编辑器开着也能跑）：
  ```bash
  "d:/Application/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" \
    "F:/workspace/ue/StellarEra/StellarEra.uproject" \
    -ExecCmds="Automation RunTests <过滤前缀>" \
    -TestExit="Automation Test Queue Empty" \
    -unattended -nopause -nullrhi -nosplash -stdout \
    -abslog="F:/workspace/ue/StellarEra/Saved/Logs/<日志名>.log"
  ```
  判定只认 `grep -c "Result={Success}"` 与 `grep -c "Result={Fail}"`；**不要**用 Passed/Failed 两个词去 grep。
- **提交必须带 pathspec**：索引里有常驻暂存文件，裸 `git commit` 会一口气吞掉全部。一律 `git add <具体路径>` / `git rm <具体路径>` 之后 `git commit -- <同一批路径>`。**不要用 `git add -A`，不要 `--amend`。**
- **绝不提交** `Content/Saves/PlanetVoxel/UniverseMap/BP_VoxelPlanet3.uasset` 与 `Content/ToolsPro/`。
- **二进制资产走 Git LFS**；`.uasset` 改动前先确认 `git lfs` 已装（`git lfs version`）。
- **判性能必须让编辑器在前台**：编辑器不在前台时进程被硬限 3 FPS，构建耗时数字没有意义。

---

### Task 1: 按元素的场出口 + 等值面构建器（纯函数）

**Files:**
- Modify: `Source/StellarEraPlanet/Public/PlanetGeologyField.h`（新增公开方法声明、`Accumulate` 加一个可选出参）
- Modify: `Source/StellarEraPlanet/Private/PlanetGeologyField.cpp`（`Accumulate` 填数组 + 新方法实现）
- Modify: `Source/StellarEraPlanet/StellarEraPlanet.Build.cs`（Private 依赖 `GeometryCore`）
- Create: `Source/StellarEraPlanet/Public/PlanetMineralSurface.h`
- Create: `Source/StellarEraPlanet/Private/PlanetMineralSurface.cpp`
- Test: `Source/StellarEraPlanet/Private/PlanetMineralSurfaceTests.cpp`（新建；本任务只放夹具 + 3 个场测试，Task 2 再往同一文件追加网格测试）

**Interfaces:**
- Consumes: 现有的 `FPlanetGeologyField::GetAppearance()`（`.ColorThreshold`）、`GetMaxReachDepth()`、`NumElements()`、`GetSampledElementDefinitions()`。
- Produces（Task 2 / Task 3 依赖，名字与类型必须一字不差）：
  - `void FPlanetGeologyField::SampleElementNorms(const FVector& UnitDir, double Depth, TArray<float>& OutNorms) const;`
  - `struct FPlanetMineralMesh { TArray<FVector3f> VerticesCm; TArray<int32> Triangles; int32 OccupiedCells = 0; bool IsEmpty() const; };`
  - `namespace PlanetMineralSurface`：`FBuildParams{SurfaceRadiusCm, VoxelSizeCm, GridResolution=40, IsoValue}`、`FBuildResult{MeshesByElement, GridPoints, BuildSeconds}`、`FCacheKey{Planet, SurfaceRadiusCm, Scale, GridResolution}`（带 `operator==` / `operator!=`）、`void Build(const FPlanetGeologyField&, const FBuildParams&, FBuildResult&)`。

**背景（实现者需要知道的全部机制）：**

- `FPlanetGeologyField::Accumulate` 已经为每个元素算出 `ElementNorm = max over bands(Gaussian × Vein)`，但只把最大值写进 `DominantNorm`。等值面要「每个元素各取一面」，所以要把整条数组暴露出来。
- `FMarchingCubes` 的网格由它自己算：`NX = (int)(Bounds.Width() / CubeSize) + 1`。传 `Bounds = (中心 0, HalfWidth)` 且 `CubeSize = 2R/Resolution` 时，浮点除法可能给出 `Resolution - 1`，最外一圈（深度 0 附近，硅带就在那儿）会被整片裁掉 —— 所以盒子要放宽 1e-9 的相对量。
- `FMarchingCubes::Implicit` 是 `TFunction<double(TVector<double>)>`，我们只做**网格三线性插值**，不再调地质场（那是 68,921 × 6 次噪声，太贵）。

- [ ] **Step 1: 先写测试（会编译失败，因为新 API 还不存在）**

新建 `Source/StellarEraPlanet/Private/PlanetMineralSurfaceTests.cpp`（**UTF-8 with BOM**）：

```cpp
// 等值面矿体的自动化测试：按元素强度出口、运行期顺序、几何、对齐、阈值、确定性、空场。

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Math/RandomStream.h"
#include "PlanetGeologyField.h"
#include "PlanetMineralSurface.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace PlanetMineralSurfaceTest
{
	// 三个元素：a/b/c。半径 1000 体素，矿带峰值 150 / 450 / 750，厚度 50。
	// 3σ = 150 体素，三条带分别覆盖 0..300 / 300..600 / 600..900 —— 互不重叠，
	// 于是「顶点落在哪条带」能唯一确定它属于哪个元素。
	const TCHAR* ElementsJson = TEXT(R"JSON(
{
  "version": 1,
  "elements": [
    {
      "id": "a",
      "displayName": "A",
      "color": [1.0, 0.0, 0.0],
      "noise": { "frequency": 3.0, "octaves": 2, "lacunarity": 2.0, "gain": 0.5, "seed": 11, "depthScale": 0.3 }
    },
    {
      "id": "b",
      "displayName": "B",
      "color": [0.0, 1.0, 0.0],
      "noise": { "frequency": 3.0, "octaves": 2, "lacunarity": 2.0, "gain": 0.5, "seed": 22, "depthScale": 0.3 }
    },
    {
      "id": "c",
      "displayName": "C",
      "color": [0.0, 0.0, 1.0],
      "noise": { "frequency": 3.0, "octaves": 2, "lacunarity": 2.0, "gain": 0.5, "seed": 33, "depthScale": 0.3 }
    }
  ]
}
)JSON");

	/**
	 * 行星 JSON。故意按 c / a / b 写，让「写下来的顺序」与 Elements.json 的
	 * a / b / c 不同 —— 运行期顺序由哈希决定，但至少不是数组顺序的简单复制。
	 */
	FString MakePlanetJson(float ColorThreshold)
	{
		return FString::Printf(TEXT(R"JSON(
{
  "version": 1,
  "geometryAmplitude": 1.5,
  "appearance": { "colorThreshold": %f, "fadeRange": 0.15, "purityForFullColor": 0.7 },
  "elements": {
    "c": { "abundance": 1.0, "peakDepth": 750, "layerThickness": 50 },
    "a": { "abundance": 1.0, "peakDepth": 150, "layerThickness": 50 },
    "b": { "abundance": 1.0, "peakDepth": 450, "layerThickness": 50 }
  }
}
)JSON"), ColorThreshold);
	}

	const TCHAR* EmptyPlanetJson = TEXT(R"JSON(
{
  "version": 1,
  "appearance": { "colorThreshold": 0.22, "fadeRange": 0.15, "purityForFullColor": 0.7 },
  "elements": {}
}
)JSON");

	TSharedPtr<const FPlanetGeologyField> MakeField(float ColorThreshold = 0.22f)
	{
		FPlanetGeologyLoadReport Report;
		TSharedPtr<const FPlanetGeologyField> Field = FPlanetGeologyField::LoadFromString(
			ElementsJson, MakePlanetJson(ColorThreshold), Report, /*PlanetRadiusVoxels=*/1000.0);
		check(Field.IsValid());
		return Field;
	}

	PlanetMineralSurface::FBuildParams MakeParams(const FPlanetGeologyField& Field,
		int32 GridResolution)
	{
		PlanetMineralSurface::FBuildParams Params;
		Params.SurfaceRadiusCm = 100000.f; // 1000 体素 × 100 cm
		Params.VoxelSizeCm = 100.f;
		Params.GridResolution = GridResolution;
		Params.IsoValue = Field.GetAppearance().ColorThreshold;
		return Params;
	}

	/** 顶点 → 深度（体素，基准球面为 0，向内为正）。 */
	double VertexDepthVoxels(const FVector3f& VertexCm)
	{
		const double DistanceCm =
			FVector3d(double(VertexCm.X), double(VertexCm.Y), double(VertexCm.Z)).Size();
		return (100000.0 - DistanceCm) / 100.0;
	}

	/** 元素 id → 峰值深度（体素）；未知 id 返回 -1。 */
	double PeakDepthVoxels(const FString& Id)
	{
		if (Id == TEXT("a")) { return 150.0; }
		if (Id == TEXT("b")) { return 450.0; }
		if (Id == TEXT("c")) { return 750.0; }
		return -1.0;
	}
} // namespace PlanetMineralSurfaceTest

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlanetElementNormsMatchesDominantTest,
	"StellarEra.Planet.Geology.ElementNorms.MatchesDominant",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetElementNormsMatchesDominantTest::RunTest(const FString& Parameters)
{
	const TSharedPtr<const FPlanetGeologyField> Field = PlanetMineralSurfaceTest::MakeField();

	// 新出口必须与 Sample 同源：最大值 = DominantNorm，argmax = DominantIndex。
	// 两条路径都只在严格大于时才换赢家，所以并列时也选同一个下标。
	FRandomStream Random(2026);
	const double Probes[] = { 5.0, 150.0, 450.0, 750.0 };
	TArray<float> Norms;

	for (int32 Iteration = 0; Iteration < 64; ++Iteration)
	{
		const FVector UnitDir = Random.GetUnitVector();
		for (const double Depth : Probes)
		{
			Field->SampleElementNorms(UnitDir, Depth, Norms);
			if (!TestEqual(TEXT("每个元素一个强度"), Norms.Num(), Field->NumElements()))
			{
				return false;
			}

			float MaxNorm = 0.f;
			int32 MaxIndex = INDEX_NONE;
			for (int32 Index = 0; Index < Norms.Num(); ++Index)
			{
				if (Norms[Index] > MaxNorm)
				{
					MaxNorm = Norms[Index];
					MaxIndex = Index;
				}
			}

			const FPlanetGeologySample Sample = Field->Sample(UnitDir, Depth);
			TestTrue(TEXT("最大值等于 DominantNorm"),
				FMath::IsNearlyEqual(MaxNorm, float(Sample.DominantNorm), 1e-6f));
			TestEqual(TEXT("argmax 等于 DominantIndex"), MaxIndex, Sample.DominantIndex);
		}
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlanetElementNormsBandProbeTest,
	"StellarEra.Planet.Geology.ElementNorms.BandProbe",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetElementNormsBandProbeTest::RunTest(const FString& Parameters)
{
	const TSharedPtr<const FPlanetGeologyField> Field = PlanetMineralSurfaceTest::MakeField();
	const TArray<FPlanetGeologyElement> Sampled = Field->GetSampledElementDefinitions();

	struct FProbe
	{
		const TCHAR* Id;
		double Depth;
	};
	const FProbe Probes[] = {
		{ TEXT("a"), 150.0 },
		{ TEXT("b"), 450.0 },
		{ TEXT("c"), 750.0 },
	};

	FRandomStream Random(99);
	TArray<float> Norms;
	for (int32 Iteration = 0; Iteration < 32; ++Iteration)
	{
		const FVector UnitDir = Random.GetUnitVector();
		for (const FProbe& Probe : Probes)
		{
			int32 ProbeIndex = INDEX_NONE;
			for (int32 Index = 0; Index < Sampled.Num(); ++Index)
			{
				if (Sampled[Index].Id == FString(Probe.Id))
				{
					ProbeIndex = Index;
				}
			}
			if (!TestTrue(TEXT("夹具里能找到 a/b/c"), Sampled.IsValidIndex(ProbeIndex)))
			{
				return false;
			}

			Field->SampleElementNorms(UnitDir, Probe.Depth, Norms);
			// 别的带在这个深度已经衰减到 3σ 之外（< 1e-4），本带必须是最强的。
			for (int32 Index = 0; Index < Norms.Num(); ++Index)
			{
				if (Index == ProbeIndex)
				{
					continue;
				}
				TestTrue(*FString::Printf(TEXT("%s 的峰值深度上别的元素更弱"), Probe.Id),
					Norms[Index] <= Norms[ProbeIndex] + 1e-6f);
			}
		}
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlanetGeologySampledDefinitionsTest,
	"StellarEra.Planet.Geology.SampledDefinitions",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetGeologySampledDefinitionsTest::RunTest(const FString& Parameters)
{
	const TSharedPtr<const FPlanetGeologyField> Field = PlanetMineralSurfaceTest::MakeField();

	const TArray<FPlanetGeologyElement> Sampled = Field->GetSampledElementDefinitions();
	const TArray<FPlanetGeologyElement> Ordered = Field->GetElementDefinitions();

	TestEqual(TEXT("采样顺序数量 = 运行期元素数"), Sampled.Num(), Field->NumElements());
	TestEqual(TEXT("数组顺序数量 = 运行期元素数"), Ordered.Num(), Field->NumElements());

	// 运行期顺序里每个 id 都必须在数组顺序里出现。
	for (const FPlanetGeologyElement& Element : Sampled)
	{
		TestTrue(FString::Printf(TEXT("'%s' 在元素表里"), *Element.Id),
			Ordered.ContainsByPredicate([&Element](const FPlanetGeologyElement& Candidate)
			{
				return Candidate.Id == Element.Id;
			}));
	}

	// 深度探针：在每条矿带的峰值深度上采样，最强元素必须是该带的元素。
	// Sampled 若错用了 ElementOrder，这里就会指到别的 id 上。
	struct FProbe
	{
		const TCHAR* Id;
		double Depth;
	};
	const FProbe Probes[] = {
		{ TEXT("a"), 150.0 },
		{ TEXT("b"), 450.0 },
		{ TEXT("c"), 750.0 },
	};

	FRandomStream Random(4242);
	int32 Mapped = 0;
	constexpr int32 Iterations = 64;
	for (int32 Iteration = 0; Iteration < Iterations; ++Iteration)
	{
		const FVector UnitDir = Random.GetUnitVector();
		for (const FProbe& Probe : Probes)
		{
			const FPlanetGeologySample Sample = Field->Sample(UnitDir, Probe.Depth);
			if (!Sampled.IsValidIndex(Sample.DominantIndex))
			{
				// 极小概率的噪声退化（整条带含量为 0）：不算错，但要求几乎全部命中。
				continue;
			}
			TestEqual(
				*FString::Printf(TEXT("%s 在深度 %.0f 的归属"), Probe.Id, Probe.Depth),
				Sampled[Sample.DominantIndex].Id,
				FString(Probe.Id));
			++Mapped;
		}
	}

	TestTrue(TEXT("几乎全部探针都命中了元素"), Mapped >= Iterations * 3 - 2);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
```

- [ ] **Step 2: 跑一次测，确认失败原因是「新 API 不存在」**

```bash
"d:/Application/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" \
  "F:/workspace/ue/StellarEra/StellarEra.uproject" \
  -ExecCmds="Automation RunTests StellarEra.Planet.Geology" \
  -TestExit="Automation Test Queue Empty" \
  -unattended -nopause -nullrhi -nosplash -stdout \
  -abslog="F:/workspace/ue/StellarEra/Saved/Logs/MineralIsoT1Red.log"
```

期望：**编译失败**（`PlanetMineralSurface.h` 与 `SampleElementNorms` 都还不存在），日志里有 `error C2039` / `C3861` 一类。把这一步的失败日志留在 `Saved/Logs/` 里（测试要先红）。

- [ ] **Step 3: 加场出口**

`Source/StellarEraPlanet/Public/PlanetGeologyField.h`：在 `FPlanetGeologySample Sample(const FVector& UnitDir, double Depth) const;` 声明之后空一行加：

```cpp
	/**
	 * 每个元素的强度（∈ [0,1]），下标 = 运行期元素下标
	 * （即 GetSampledElementDefinitions 的顺序）。
	 *
	 * 与 Sample().DominantNorm / DominantIndex 同源：DominantNorm 就是本数组的最大值、
	 * DominantIndex 就是它的下标（并列时取靠前的那个）。等值面构建器要按元素各取一面，
	 * 必须拿到整条数组；Sample 只给最大值。
	 *
	 * 内部走同一个 Accumulate，一次循环填完，不额外调噪声。
	 */
	void SampleElementNorms(const FVector& UnitDir, double Depth, TArray<float>& OutNorms) const;
```

同文件私有区的 `Accumulate` 声明改成：

```cpp
	/** 单遍累加；bComputeColor 为假时跳过加权颜色，供几何查询用。
	 *  OutElementNorms 非空时按元素下标写入每个元素的 Norm（含 0），长度由调用方保证。 */
	void Accumulate(const FVector& UnitDir, double Depth, bool bComputeColor,
		FAccumulation& OutAccumulation, FLinearColor& OutWeightedColor,
		TArray<float>* OutElementNorms = nullptr) const;
```

- [ ] **Step 4: 实现场出口**

`Source/StellarEraPlanet/Private/PlanetGeologyField.cpp`：

1. 把 `Accumulate` 的定义（原第 614 行起）改成带新参数：

```cpp
void FPlanetGeologyField::Accumulate(const FVector& UnitDir, double Depth, bool bComputeColor,
	FAccumulation& OutAccumulation, FLinearColor& OutWeightedColor,
	TArray<float>* OutElementNorms) const
```

2. 在带循环结束后、`if (ElementNorm <= 0.0)` 那个 `continue` **之前**插入（必须在这个位置，否则 0 强度元素不会被写入，调用方会读到残留值）：

```cpp
		if (OutElementNorms && OutElementNorms->IsValidIndex(ElementIndex))
		{
			(*OutElementNorms)[ElementIndex] = float(ElementNorm);
		}

```

3. 在 `SampleGeometryDelta` 之后、`Accumulate` 之前加实现：

```cpp
void FPlanetGeologyField::SampleElementNorms(const FVector& UnitDir, double Depth,
	TArray<float>& OutNorms) const
{
	OutNorms.Reset();
	OutNorms.SetNumZeroed(Elements.Num());
	if (Elements.Num() == 0)
	{
		return;
	}

	FAccumulation Accumulation;
	FLinearColor UnusedColor = FLinearColor::Black;
	Accumulate(UnitDir, Depth, false, Accumulation, UnusedColor, &OutNorms);
}
```

- [ ] **Step 5: 加模块依赖**

`Source/StellarEraPlanet/StellarEraPlanet.Build.cs`，在 `Json` 那一行下面加：

```csharp
		// 矿物等值面：FMarchingCubes / FDynamicMesh3 / FMeshNormals 都在 GeometryCore
		// 的运行时模块里（不是编辑器模块）。头文件不向下游暴露这些类型，所以是 Private。
		PrivateDependencyModuleNames.AddRange(new string[] { "GeometryCore" });
```

- [ ] **Step 6: 写构建器头文件**

新建 `Source/StellarEraPlanet/Public/PlanetMineralSurface.h`（**UTF-8 with BOM**）：

```cpp
// Copyright Voxel Plugin SAS. All Rights Reserved.
// 行星内部矿体的等值面构建：纯函数，只产数据，不碰世界、不建组件、不读资产。

#pragma once

#include "CoreMinimal.h"
#include "UObject/WeakObjectPtr.h"

class FPlanetGeologyField;

/**
 * 一个元素的矿体网格。顶点相对行星球心，单位厘米。
 *
 * 法线不在数据层：等值面抽取只给顶点与三角形，视图 Actor 转 FDynamicMesh3 时
 * 用 FMeshNormals 按顶点算法线（材质靠 VertexNormalWS 取它）。
 */
struct FPlanetMineralMesh
{
	TArray<FVector3f> VerticesCm;
	/** 每 3 个 int32 一个三角形，索引进 VerticesCm。 */
	TArray<int32> Triangles;
	/** 场值高于等值线的角点数，只进日志（诊断「这个元素为什么没面」）。 */
	int32 OccupiedCells = 0;

	bool IsEmpty() const { return Triangles.Num() == 0; }
};

namespace PlanetMineralSurface
{
	/** 一次构建的输入。半径为 0、体素为 0 或格数 < 8 时 Build 直接给空结果。 */
	struct FBuildParams
	{
		/** 基准球面半径（厘米），与 AVoxelPlanetActor::GetSurfaceRadiusCm 一致。 */
		float SurfaceRadiusCm = 0.f;
		/** 一个体素多少厘米：地质深度以体素计，换回厘米要用它。 */
		float VoxelSizeCm = 0.f;
		/** 每轴格数（角点数 = GridResolution + 1）。40 首进约 0.1–0.3 秒。 */
		int32 GridResolution = 40;
		/** 等值线，调用方传 Field.GetAppearance().ColorThreshold。 */
		float IsoValue = 0.f;
	};

	struct FBuildResult
	{
		/** 下标 = 运行期元素下标（GetSampledElementDefinitions 顺序）；空元素留空网格。 */
		TArray<FPlanetMineralMesh> MeshesByElement;
		/** 实际求过噪声的角点数（球外、超深的点不算），只进日志。 */
		int32 GridPoints = 0;
		double BuildSeconds = 0.0;
	};

	/** 组件缓存的键：任一项变化都要重算。 */
	struct FCacheKey
	{
		TWeakObjectPtr<const UObject> Planet;
		float SurfaceRadiusCm = 0.f;
		/** 行星 Actor 缩放。S ≠ 1 时厘米换算本来就错，进键里至少不会静默复用旧网格。 */
		FVector Scale = FVector::ZeroVector;
		int32 GridResolution = 0;

		bool operator==(const FCacheKey& Other) const
		{
			// 半径以厘米计：1 mm 之内视为同一颗行星。
			return Planet == Other.Planet
				&& FMath::IsNearlyEqual(SurfaceRadiusCm, Other.SurfaceRadiusCm, 0.01f)
				&& Scale.Equals(Other.Scale, 0.01)
				&& GridResolution == Other.GridResolution;
		}
		bool operator!=(const FCacheKey& Other) const { return !(*this == Other); }
	};

	/**
	 * 逐元素抽等值面。Out 先被清空；参数非法或场为空时返回空结果，不报错。
	 *
	 * 球外（深度 < 0）与深度 > MaxReachDepth 的角点按 0 处理，不花噪声求值。
	 * 各元素场独立取面：两种元素同时高于门限的地方，两块矿体会互相穿插（共生矿脉），
	 * 不做 winner-take-all —— 布尔场做 MC 会退回方块状边缘。
	 */
	STELLARERAPLANET_API void Build(const FPlanetGeologyField& Field, const FBuildParams& Params,
		FBuildResult& Out);
}
```

- [ ] **Step 7: 写构建器实现**

新建 `Source/StellarEraPlanet/Private/PlanetMineralSurface.cpp`（**UTF-8 with BOM**）：

```cpp
// Copyright Voxel Plugin SAS. All Rights Reserved.

#include "PlanetMineralSurface.h"

#include "PlanetGeologyField.h"

#include "Async/ParallelFor.h"
#include "Generators/MarchingCubes.h"
#include "HAL/PlatformTime.h"
#include "HAL/ThreadSafeCounter.h"

void PlanetMineralSurface::Build(const FPlanetGeologyField& Field, const FBuildParams& Params,
	FBuildResult& Out)
{
	Out.MeshesByElement.Reset();
	Out.GridPoints = 0;
	Out.BuildSeconds = 0.0;

	const int32 Resolution = Params.GridResolution;
	const double RadiusCm = double(Params.SurfaceRadiusCm);
	if (Field.IsEmpty() || Resolution < 8 || RadiusCm <= 0.0 || Params.VoxelSizeCm <= 0.f)
	{
		return;
	}

	const double StartSeconds = FPlatformTime::Seconds();

	const int32 ElementCount = Field.NumElements();
	Out.MeshesByElement.SetNum(ElementCount);

	const int32 CornerCount = Resolution + 1;
	const int32 TotalPoints = CornerCount * CornerCount * CornerCount;
	const double CubeSize = (2.0 * RadiusCm) / double(Resolution);

	// FMarchingCubes 内部按 (int)(Bounds.Width() / CubeSize) + 1 定角点数；浮点除法若
	// 得到 Resolution - 1，最外一圈（深度 0 附近，硅带就在那儿）会被整片裁掉。
	// 把盒子放宽 1e-9 的相对量（≈ 0.0001 cm），让除法稳定落在 Resolution 上。
	const double HalfExtent = RadiusCm * (1.0 + 1e-9);
	const double GridMin = -HalfExtent;
	const double InvCube = 1.0 / CubeSize;
	const double MaxDepth = double(Field.GetMaxReachDepth());
	const double VoxelSizeCm = double(Params.VoxelSizeCm);

	// [点][元素] 稠密场：一次并行采样，后面每个元素各取一面，不重复调噪声。
	TArray<float> Norms;
	Norms.SetNumZeroed(TotalPoints * ElementCount);

	FThreadSafeCounter EvaluatedPoints;

	ParallelFor(TotalPoints, [&](int32 LinearIndex)
	{
		const int32 X = LinearIndex % CornerCount;
		const int32 Y = (LinearIndex / CornerCount) % CornerCount;
		const int32 Z = LinearIndex / (CornerCount * CornerCount);

		// 角点与 MC 自己的格点重合：Min + k * CubeSize。
		const double PX = GridMin + double(X) * CubeSize;
		const double PY = GridMin + double(Y) * CubeSize;
		const double PZ = GridMin + double(Z) * CubeSize;

		const double Distance = FMath::Sqrt(PX * PX + PY * PY + PZ * PZ);
		if (Distance > RadiusCm || Distance <= UE_DOUBLE_KINDA_SMALL_NUMBER)
		{
			return; // 球外 / 球心：场值恒 0，数组本来就是 0
		}

		const double Depth = (RadiusCm - Distance) / VoxelSizeCm;
		if (Depth < 0.0 || Depth > MaxDepth)
		{
			return; // 高斯尾部之外没有任何元素有贡献
		}

		TArray<float> PointNorms;
		Field.SampleElementNorms(FVector(PX / Distance, PY / Distance, PZ / Distance),
			Depth, PointNorms);

		const int32 Base = LinearIndex * ElementCount;
		for (int32 ElementIndex = 0; ElementIndex < ElementCount; ++ElementIndex)
		{
			Norms[Base + ElementIndex] =
				PointNorms.IsValidIndex(ElementIndex) ? PointNorms[ElementIndex] : 0.f;
		}

		EvaluatedPoints.Increment();
	});

	Out.GridPoints = EvaluatedPoints.GetValue();

	for (int32 ElementIndex = 0; ElementIndex < ElementCount; ++ElementIndex)
	{
		// 三线性插值：把 MC 的任意查询点换成 8 个角点的加权和。
		// 闭包捕获了 ElementIndex，所以必须定义在元素循环里。
		auto SampleGrid = [&Norms, CornerCount, ElementCount, ElementIndex, Resolution](
			const UE::Math::TVector<double>& P, double Min, double InvCell) -> double
		{
			const double GX = FMath::Clamp((P.X - Min) * InvCell, 0.0, double(Resolution));
			const double GY = FMath::Clamp((P.Y - Min) * InvCell, 0.0, double(Resolution));
			const double GZ = FMath::Clamp((P.Z - Min) * InvCell, 0.0, double(Resolution));

			const int32 X0 = FMath::Min(int32(GX), Resolution - 1);
			const int32 Y0 = FMath::Min(int32(GY), Resolution - 1);
			const int32 Z0 = FMath::Min(int32(GZ), Resolution - 1);
			const double FX = GX - double(X0);
			const double FY = GY - double(Y0);
			const double FZ = GZ - double(Z0);

			auto At = [&Norms, CornerCount, ElementCount, ElementIndex](
				int32 InX, int32 InY, int32 InZ) -> double
			{
				return double(Norms[((InZ * CornerCount + InY) * CornerCount + InX)
					* ElementCount + ElementIndex]);
			};

			const double C00 = FMath::Lerp(At(X0, Y0, Z0), At(X0 + 1, Y0, Z0), FX);
			const double C10 = FMath::Lerp(At(X0, Y0 + 1, Z0), At(X0 + 1, Y0 + 1, Z0), FX);
			const double C01 = FMath::Lerp(At(X0, Y0, Z0 + 1), At(X0 + 1, Y0, Z0 + 1), FX);
			const double C11 = FMath::Lerp(At(X0, Y0 + 1, Z0 + 1), At(X0 + 1, Y0 + 1, Z0 + 1), FX);
			const double C0 = FMath::Lerp(C00, C10, FY);
			const double C1 = FMath::Lerp(C01, C11, FY);
			return FMath::Lerp(C0, C1, FZ);
		};

		UE::Geometry::FMarchingCubes Marching;
		Marching.Bounds = UE::Geometry::TAxisAlignedBox3<double>(
			UE::Math::TVector<double>::Zero(), HalfExtent);
		Marching.CubeSize = CubeSize;
		Marching.IsoValue = double(Params.IsoValue);
		// Implicit 只是一次三线性插值，角点值也已经预计算，值缓存的额外开销不值得。
		Marching.bEnableValueCaching = false;
		Marching.bParallelCompute = true;
		Marching.Implicit = [&SampleGrid, GridMin, InvCube](UE::Math::TVector<double> P) -> double
		{
			return SampleGrid(P, GridMin, InvCube);
		};

		Marching.Generate();

		FPlanetMineralMesh& Mesh = Out.MeshesByElement[ElementIndex];
		Mesh.VerticesCm.Reserve(Marching.Vertices.Num());
		for (const UE::Math::TVector<double>& Vertex : Marching.Vertices)
		{
			Mesh.VerticesCm.Add(FVector3f(float(Vertex.X), float(Vertex.Y), float(Vertex.Z)));
		}

		Mesh.Triangles.Reserve(Marching.Triangles.Num() * 3);
		for (const UE::Geometry::FIndex3i& Triangle : Marching.Triangles)
		{
			Mesh.Triangles.Add(Triangle.A);
			Mesh.Triangles.Add(Triangle.B);
			Mesh.Triangles.Add(Triangle.C);
		}

		int32 Occupied = 0;
		for (int32 PointIndex = 0; PointIndex < TotalPoints; ++PointIndex)
		{
			if (Norms[PointIndex * ElementCount + ElementIndex] > Params.IsoValue)
			{
				++Occupied;
			}
		}
		Mesh.OccupiedCells = Occupied;
	}

	Out.BuildSeconds = FPlatformTime::Seconds() - StartSeconds;
}
```

- [ ] **Step 8: 构建**

关掉编辑器，跑 Global Constraints 里那条 UBT 命令（输出进 `Saved/Logs/MineralIsoBuild.log`），期望日志末尾 `Result: Succeeded`。有编译错误先修到干净。

- [ ] **Step 9: 跑本任务的测试**

```bash
"d:/Application/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" \
  "F:/workspace/ue/StellarEra/StellarEra.uproject" \
  -ExecCmds="Automation RunTests StellarEra.Planet.Geology" \
  -TestExit="Automation Test Queue Empty" \
  -unattended -nopause -nullrhi -nosplash -stdout \
  -abslog="F:/workspace/ue/StellarEra/Saved/Logs/MineralIsoT1Green.log"
```

期望：`grep -c "Result={Success}"` = **3**，`grep -c "Result={Fail}"` = **0**。
（此时 `PlanetMineralBlocksTests.cpp` 还在，它的测试名是 `StellarEra.Planet.MineralBlocks.*`，不匹配这个前缀。）

- [ ] **Step 10: 提交**

```bash
cd "F:/workspace/ue/StellarEra"
git add Source/StellarEraPlanet/Public/PlanetGeologyField.h \
        Source/StellarEraPlanet/Private/PlanetGeologyField.cpp \
        Source/StellarEraPlanet/StellarEraPlanet.Build.cs \
        Source/StellarEraPlanet/Public/PlanetMineralSurface.h \
        Source/StellarEraPlanet/Private/PlanetMineralSurface.cpp \
        Source/StellarEraPlanet/Private/PlanetMineralSurfaceTests.cpp
git commit -- Source/StellarEraPlanet/Public/PlanetGeologyField.h \
  Source/StellarEraPlanet/Private/PlanetGeologyField.cpp \
  Source/StellarEraPlanet/StellarEraPlanet.Build.cs \
  Source/StellarEraPlanet/Public/PlanetMineralSurface.h \
  Source/StellarEraPlanet/Private/PlanetMineralSurface.cpp \
  Source/StellarEraPlanet/Private/PlanetMineralSurfaceTests.cpp \
  -m "feat(矿物): 新增按元素的场出口与等值面构建器

- FPlanetGeologyField::SampleElementNorms 暴露每个元素的 Norm，
  与 Sample 的 DominantNorm/DominantIndex 同源（同一遍 Accumulate）
- PlanetMineralSurface::Build 在 40³ 网格上并行采样全部元素，
  再逐元素跑 FMarchingCubes，输出纯数据网格（顶点/三角形/占据格数）
- 网格密度、等值线、缓存键都收在 FBuildParams / FCacheKey 里
- 3 个自动化测试：与 Sample 同源、峰值深度归属、运行期元素顺序

Co-Authored-By: Claude Code <noreply@anthropic.com>"
```

`git add` 与 `git commit --` 的路径列表必须**完全一致**（commit 上的 pathspec 决定提交范围，索引里另有常驻暂存文件）。

---

### Task 2: 观察体改用动态网格 + 组件接等值面构建器（删掉方块路径）

**Files:**
- Modify: `Source/StellarEraPlanet/Public/PlanetMineralViewActor.h`
- Modify: `Source/StellarEraPlanet/Private/PlanetMineralViewActor.cpp`
- Modify: `Source/StellarEraPlanet/Public/PlanetMineralViewComponent.h`
- Modify: `Source/StellarEraPlanet/Private/PlanetMineralViewComponent.cpp`
- Modify: `Source/StellarEraPlanet/Private/PlanetMineralViewTests.cpp`
- Modify: `Source/StellarEraPlanet/Private/PlanetMineralSurfaceTests.cpp`（在 `#endif` 之前追加 7 个网格测试）
- Modify: `Source/StellarEraPlanet/StellarEraPlanet.Build.cs`（Private 依赖 `GeometryFramework`）
- Delete: `Source/StellarEraPlanet/Public/PlanetMineralBlocks.h`
- Delete: `Source/StellarEraPlanet/Private/PlanetMineralBlocks.cpp`
- Delete: `Source/StellarEraPlanet/Private/PlanetMineralBlocksTests.cpp`

**Interfaces:**
- Consumes: Task 1 的 `FPlanetMineralMesh` / `PlanetMineralSurface::Build` / `FBuildParams` / `FBuildResult` / `FCacheKey`，以及测试用到的 `FPlanetGeologyField::SampleElementNorms`、`GetSampledElementDefinitions`、`GetMaxReachDepth`、`GetAppearance().ColorThreshold`。
- Produces:
  - `bool APlanetMineralViewActor::Build(const TArray<FPlanetMineralMesh>& MeshesByElement, const FLinearColor& ShellColor, float SurfaceRadiusCm, const TArray<FPlanetGeologyElement>& Elements);`
  - `UDynamicMeshComponent* APlanetMineralViewActor::GetElementMesh(int32 Index) const;`
  - `UPlanetMineralViewComponent::GridResolution`（`int32`，默认 40）。

**为什么这是一个任务：** `Build` 的签名一变，组件与测试就同时编译不过；方块路径的删除也必须和签名变更落在同一次提交里，否则中间态是「旧的没人调、新的没人接」。

- [ ] **Step 1: 把观察体测试改成新签名（此时会编译失败）**

把 `Source/StellarEraPlanet/Private/PlanetMineralViewTests.cpp` 整个替换成（**UTF-8 with BOM**）：

```cpp
// 观察体的冒烟测试：每个元素一个动态网格组件、显隐互不影响、空数据不崩。

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "Engine/EngineTypes.h"
#include "Engine/World.h"
#include "Components/DynamicMeshComponent.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "PlanetMineralSurface.h"
#include "PlanetMineralViewActor.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	/**
	 * 取编辑器世界。
	 *
	 * 不能写 GEngine->GetEditorWorldContext()：那个方法是 UEditorEngine 的，而本模块
	 * 刻意不带 UnrealEd 依赖（见 StellarEraPlanet.Build.cs 末尾）。这里按 WorldType
	 * 扫一遍 UEngine 的世界上下文，与 UEditorEngine::GetEditorWorldContext 的实现
	 * （EditorEngine.cpp 里取第一个 EWorldType::Editor）等价。
	 */
	UWorld* FindEditorWorld()
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

	/** 一个三角形，位置随 Index 错开。只验分组与显隐，不验几何。 */
	FPlanetMineralMesh MakeTriangleMesh(int32 Index)
	{
		FPlanetMineralMesh Mesh;
		const float Offset = 1000.f * float(Index + 1);
		Mesh.VerticesCm.Add(FVector3f(Offset, 0.f, 0.f));
		Mesh.VerticesCm.Add(FVector3f(Offset + 1000.f, 0.f, 0.f));
		Mesh.VerticesCm.Add(FVector3f(Offset, 1000.f, 0.f));
		Mesh.Triangles.Add(0);
		Mesh.Triangles.Add(1);
		Mesh.Triangles.Add(2);
		Mesh.OccupiedCells = 1;
		return Mesh;
	}
} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlanetMineralViewActorTest,
	"StellarEra.Planet.MineralView.ActorBuild",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetMineralViewActorTest::RunTest(const FString& Parameters)
{
	UWorld* World = FindEditorWorld();
	if (!TestNotNull(TEXT("编辑器世界"), World))
	{
		return false;
	}

	APlanetMineralViewActor* View = World->SpawnActor<APlanetMineralViewActor>();
	if (!TestNotNull(TEXT("观察体 spawn 成功"), View))
	{
		return false;
	}

	TArray<FPlanetMineralMesh> Meshes;
	Meshes.Add(MakeTriangleMesh(0));
	Meshes.Add(MakeTriangleMesh(1));

	TArray<FPlanetGeologyElement> Elements;
	Elements.SetNum(2);
	Elements[0].Id = TEXT("a");
	Elements[0].DisplayName = TEXT("A");
	Elements[0].Color = FLinearColor::Red;
	Elements[1].Id = TEXT("b");
	Elements[1].DisplayName = TEXT("B");
	Elements[1].Color = FLinearColor::Blue;

	const bool bBuilt = View->Build(Meshes, FLinearColor(0.1f, 0.2f, 0.1f, 1.f),
		/*SurfaceRadiusCm=*/100000.f, Elements);

	TestTrue(TEXT("Build 成功（材质已生成、引擎网格可加载）"), bBuilt);
	if (!bBuilt)
	{
		View->Destroy();
		return false;
	}

	TestEqual(TEXT("每个元素一个动态网格组件"), View->NumElements(), 2);
	TestTrue(TEXT("默认全部可见 0"), View->IsElementVisible(0));
	TestTrue(TEXT("默认全部可见 1"), View->IsElementVisible(1));

	// 三角形真的进了动态网格：AppendTriangle 失败时 Build 不会报错，只会少面。
	UDynamicMeshComponent* ElementMesh = View->GetElementMesh(0);
	if (!TestNotNull(TEXT("元素 0 的组件"), ElementMesh))
	{
		View->Destroy();
		return false;
	}
	const UE::Geometry::FDynamicMesh3* DynamicMesh = ElementMesh->GetMesh();
	if (TestNotNull(TEXT("元素 0 的动态网格"), DynamicMesh))
	{
		TestEqual(TEXT("一个三角形"), DynamicMesh->TriangleCount(), 1);
		TestEqual(TEXT("三个顶点"), DynamicMesh->VertexCount(), 3);
	}

	View->SetElementVisible(0, false);
	TestFalse(TEXT("关掉 0"), View->IsElementVisible(0));
	TestTrue(TEXT("关 0 不影响 1"), View->IsElementVisible(1));

	View->SetAllElementsVisible(true);
	TestTrue(TEXT("全开恢复 0"), View->IsElementVisible(0));

	// 越界下标必须安全：调用方是需求键与鼠标命中，不该让进程崩。
	View->SetElementVisible(99, false);
	TestFalse(TEXT("越界下标不生效"), View->IsElementVisible(99));

	View->Destroy();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlanetMineralViewActorEmptyTest,
	"StellarEra.Planet.MineralView.Empty",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetMineralViewActorEmptyTest::RunTest(const FString& Parameters)
{
	UWorld* World = FindEditorWorld();
	if (!TestNotNull(TEXT("编辑器世界"), World))
	{
		return false;
	}

	APlanetMineralViewActor* View = World->SpawnActor<APlanetMineralViewActor>();
	if (!TestNotNull(TEXT("观察体 spawn 成功"), View))
	{
		return false;
	}

	const bool bBuilt = View->Build({}, FLinearColor::Gray, 100000.f, {});
	TestTrue(TEXT("空元素表也能建出球壳"), bBuilt);
	TestEqual(TEXT("没有元素组件"), View->NumElements(), 0);

	View->Destroy();
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
```

- [ ] **Step 2: 追加 7 个网格测试**

在 `Source/StellarEraPlanet/Private/PlanetMineralSurfaceTests.cpp` 末尾的 `#endif // WITH_DEV_AUTOMATION_TESTS` **之前**插入：

```cpp
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlanetMineralSurfaceBuildTest,
	"StellarEra.Planet.MineralSurface.Build",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetMineralSurfaceBuildTest::RunTest(const FString& Parameters)
{
	const TSharedPtr<const FPlanetGeologyField> Field = PlanetMineralSurfaceTest::MakeField();
	const PlanetMineralSurface::FBuildParams Params =
		PlanetMineralSurfaceTest::MakeParams(*Field, /*GridResolution=*/80);

	PlanetMineralSurface::FBuildResult Result;
	PlanetMineralSurface::Build(*Field, Params, Result);

	if (!TestEqual(TEXT("每个元素一个槽"), Result.MeshesByElement.Num(), Field->NumElements()))
	{
		return false;
	}

	TestTrue(TEXT("求过噪声的角点非空"), Result.GridPoints > 0);
	TestTrue(TEXT("构建耗时在合理量级"),
		Result.BuildSeconds > 0.0 && Result.BuildSeconds < 30.0);

	const double RadiusCm = double(Params.SurfaceRadiusCm);
	const double MaxDepth = double(Field->GetMaxReachDepth());

	int32 TotalTriangles = 0;
	for (int32 ElementIndex = 0; ElementIndex < Result.MeshesByElement.Num(); ++ElementIndex)
	{
		const FPlanetMineralMesh& Mesh = Result.MeshesByElement[ElementIndex];
		TestTrue(*FString::Printf(TEXT("元素 #%d 有几何"), ElementIndex), !Mesh.IsEmpty());
		TestEqual(TEXT("三角形下标数组长度是 3 的倍数"), Mesh.Triangles.Num() % 3, 0);
		TotalTriangles += Mesh.Triangles.Num() / 3;

		int32 BadIndices = 0;
		for (const int32 VertexIndex : Mesh.Triangles)
		{
			if (!Mesh.VerticesCm.IsValidIndex(VertexIndex))
			{
				++BadIndices;
			}
		}
		TestEqual(TEXT("三角形下标全部合法"), BadIndices, 0);

		int32 OutsideSphere = 0;
		int32 OutsideDepth = 0;
		for (const FVector3f& Vertex : Mesh.VerticesCm)
		{
			const double DistanceCm =
				FVector3d(double(Vertex.X), double(Vertex.Y), double(Vertex.Z)).Size();
			// 允许 1e-4 的相对越界：等值面端点就是最外一圈角点本身。
			if (DistanceCm > RadiusCm * (1.0 + 1e-4))
			{
				++OutsideSphere;
			}
			const double Depth = PlanetMineralSurfaceTest::VertexDepthVoxels(Vertex);
			if (Depth < -1e-2 || Depth > MaxDepth + 1e-2)
			{
				++OutsideDepth;
			}
		}
		TestEqual(TEXT("顶点都在球内"), OutsideSphere, 0);
		TestEqual(TEXT("顶点深度都在可达范围内"), OutsideDepth, 0);
	}

	TestTrue(TEXT("总三角形数非零"), TotalTriangles > 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlanetMineralSurfaceIsoAccuracyTest,
	"StellarEra.Planet.MineralSurface.IsoAccuracy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetMineralSurfaceIsoAccuracyTest::RunTest(const FString& Parameters)
{
	const TSharedPtr<const FPlanetGeologyField> Field = PlanetMineralSurfaceTest::MakeField();
	const PlanetMineralSurface::FBuildParams Params =
		PlanetMineralSurfaceTest::MakeParams(*Field, /*GridResolution=*/80);

	PlanetMineralSurface::FBuildResult Result;
	PlanetMineralSurface::Build(*Field, Params, Result);

	const double RadiusCm = double(Params.SurfaceRadiusCm);
	const double VoxelSizeCm = double(Params.VoxelSizeCm);
	const float Iso = Params.IsoValue;
	// MC 抽的是「网格三线性插值场」的等值面，顶点处再回采真实场会有插值误差；
	// 0.15 是等值线的 3 倍，宽到不抖，又窄到随机曲面必挂。
	const float Tolerance = 0.15f;

	TArray<float> Norms;
	for (int32 ElementIndex = 0; ElementIndex < Result.MeshesByElement.Num(); ++ElementIndex)
	{
		const FPlanetMineralMesh& Mesh = Result.MeshesByElement[ElementIndex];
		int32 Probes = 0;
		int32 Violations = 0;
		// 每隔 13 个顶点取一个：几千个探针够定性，全量会让测试跑到十几秒。
		for (int32 Index = 0; Index < Mesh.VerticesCm.Num(); Index += 13)
		{
			const FVector3f& Vertex = Mesh.VerticesCm[Index];
			const FVector3d Position(double(Vertex.X), double(Vertex.Y), double(Vertex.Z));
			const double DistanceCm = Position.Size();
			if (DistanceCm <= UE_DOUBLE_KINDA_SMALL_NUMBER)
			{
				continue;
			}

			const double Depth = (RadiusCm - DistanceCm) / VoxelSizeCm;
			Field->SampleElementNorms(FVector(Position / DistanceCm), Depth, Norms);
			if (!Norms.IsValidIndex(ElementIndex))
			{
				continue;
			}

			++Probes;
			if (FMath::Abs(Norms[ElementIndex] - Iso) > Tolerance)
			{
				++Violations;
			}
		}

		TestTrue(*FString::Printf(TEXT("元素 #%d 采到了顶点"), ElementIndex), Probes > 0);
		TestTrue(*FString::Printf(TEXT("元素 #%d 的顶点都在等值线附近（%d/%d 越界）"),
			ElementIndex, Violations, Probes), Violations * 10 <= Probes);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlanetMineralSurfaceElementOrderTest,
	"StellarEra.Planet.MineralSurface.ElementOrder",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetMineralSurfaceElementOrderTest::RunTest(const FString& Parameters)
{
	const TSharedPtr<const FPlanetGeologyField> Field = PlanetMineralSurfaceTest::MakeField();
	const PlanetMineralSurface::FBuildParams Params =
		PlanetMineralSurfaceTest::MakeParams(*Field, /*GridResolution=*/80);

	PlanetMineralSurface::FBuildResult Result;
	PlanetMineralSurface::Build(*Field, Params, Result);

	const TArray<FPlanetGeologyElement> Sampled = Field->GetSampledElementDefinitions();
	const double RadiusCm = double(Params.SurfaceRadiusCm);
	const double VoxelSizeCm = double(Params.VoxelSizeCm);

	for (int32 ElementIndex = 0; ElementIndex < Result.MeshesByElement.Num(); ++ElementIndex)
	{
		const FPlanetMineralMesh& Mesh = Result.MeshesByElement[ElementIndex];
		int32 Probes = 0;
		int32 Mismatches = 0;
		for (int32 Index = 0; Index < Mesh.VerticesCm.Num(); Index += 17)
		{
			const FVector3f& Vertex = Mesh.VerticesCm[Index];
			const FVector3d Position(double(Vertex.X), double(Vertex.Y), double(Vertex.Z));
			const double DistanceCm = Position.Size();
			if (DistanceCm <= UE_DOUBLE_KINDA_SMALL_NUMBER)
			{
				continue;
			}

			// 三条矿带互不重叠，所以顶点处的「最强元素」必须就是这份网格的元素。
			// 若 Build 把网格下标与 Sampled 顺序对错了，这条会立刻红。
			const double Depth = (RadiusCm - DistanceCm) / VoxelSizeCm;
			const FPlanetGeologySample Sample =
				Field->Sample(FVector(Position / DistanceCm), Depth);
			if (!Sampled.IsValidIndex(Sample.DominantIndex))
			{
				continue;
			}

			++Probes;
			if (Sample.DominantIndex != ElementIndex)
			{
				++Mismatches;
			}
		}

		TestTrue(*FString::Printf(TEXT("元素 #%d 采到了顶点"), ElementIndex), Probes > 0);
		TestTrue(*FString::Printf(TEXT("元素 #%d 的顶点归它自己（%d/%d 错配）"),
			ElementIndex, Mismatches, Probes), Mismatches * 20 <= Probes);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlanetMineralSurfaceThresholdTest,
	"StellarEra.Planet.MineralSurface.Threshold",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetMineralSurfaceThresholdTest::RunTest(const FString& Parameters)
{
	auto CountTriangles = [](const PlanetMineralSurface::FBuildResult& Result)
	{
		int32 Count = 0;
		for (const FPlanetMineralMesh& Mesh : Result.MeshesByElement)
		{
			Count += Mesh.Triangles.Num() / 3;
		}
		return Count;
	};
	auto CountOccupied = [](const PlanetMineralSurface::FBuildResult& Result)
	{
		int32 Count = 0;
		for (const FPlanetMineralMesh& Mesh : Result.MeshesByElement)
		{
			Count += Mesh.OccupiedCells;
		}
		return Count;
	};

	const TSharedPtr<const FPlanetGeologyField> Field = PlanetMineralSurfaceTest::MakeField();

	PlanetMineralSurface::FBuildParams LowParams =
		PlanetMineralSurfaceTest::MakeParams(*Field, /*GridResolution=*/80);
	LowParams.IsoValue = 0.05f;

	// 0.5 对应矿带半厚 ±42 体素（0.05 是 ±87）：几何一定还在，占据格数一定更少。
	PlanetMineralSurface::FBuildParams HighParams = LowParams;
	HighParams.IsoValue = 0.5f;

	PlanetMineralSurface::FBuildResult Low;
	PlanetMineralSurface::FBuildResult High;
	PlanetMineralSurface::Build(*Field, LowParams, Low);
	PlanetMineralSurface::Build(*Field, HighParams, High);

	// 占据格数是集合包含关系：值 > 0.5 的角点必然 > 0.05。严格变少是硬保证，
	// 也是「IsoValue 真的接进去了」的判据。
	TestTrue(TEXT("阈值升高，占据格数严格变少"), CountOccupied(High) < CountOccupied(Low));
	TestTrue(TEXT("高阈值仍有几何"), CountTriangles(High) > 0);
	// spec §7 只要求「总三角形数不增加」。别改成严格变小：等值面上移/下移 42 体素
	// 只让表面积少约 8%，格点量化足以把这个差值抹平，断言会偶发变红。
	TestTrue(TEXT("阈值升高，三角形不增加"), CountTriangles(High) <= CountTriangles(Low));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlanetMineralSurfaceEmptyTest,
	"StellarEra.Planet.MineralSurface.Empty",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetMineralSurfaceEmptyTest::RunTest(const FString& Parameters)
{
	FPlanetGeologyLoadReport Report;
	const TSharedPtr<const FPlanetGeologyField> Field = FPlanetGeologyField::LoadFromString(
		PlanetMineralSurfaceTest::ElementsJson, PlanetMineralSurfaceTest::EmptyPlanetJson,
		Report, 1000.0);
	if (!TestTrue(TEXT("空场加载出有效对象"), Field.IsValid()))
	{
		return false;
	}
	TestTrue(TEXT("场是空的"), Field->IsEmpty());

	// 先塞一份假结果，确认 Build 会清干净（缓存复用时走的就是不清空这条路）。
	PlanetMineralSurface::FBuildResult Result;
	Result.MeshesByElement.SetNum(3);
	Result.GridPoints = 123;
	Result.BuildSeconds = 9.0;

	PlanetMineralSurface::Build(*Field,
		PlanetMineralSurfaceTest::MakeParams(*Field, 40), Result);

	TestEqual(TEXT("空场不给网格"), Result.MeshesByElement.Num(), 0);
	TestEqual(TEXT("空场不求点"), Result.GridPoints, 0);
	TestTrue(TEXT("空场耗时清零"), Result.BuildSeconds == 0.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlanetMineralSurfaceDeterministicTest,
	"StellarEra.Planet.MineralSurface.Deterministic",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetMineralSurfaceDeterministicTest::RunTest(const FString& Parameters)
{
	const TSharedPtr<const FPlanetGeologyField> Field = PlanetMineralSurfaceTest::MakeField();
	const PlanetMineralSurface::FBuildParams Params =
		PlanetMineralSurfaceTest::MakeParams(*Field, /*GridResolution=*/40);

	// MC 并行抽取不保证顶点顺序，只保证顶点集合。比较前按 (X,Y,Z) 排序。
	auto SortedVertices = [](const FPlanetMineralMesh& Mesh)
	{
		TArray<FVector3f> Sorted = Mesh.VerticesCm;
		Sorted.Sort([](const FVector3f& Left, const FVector3f& Right)
		{
			if (Left.X != Right.X) { return Left.X < Right.X; }
			if (Left.Y != Right.Y) { return Left.Y < Right.Y; }
			return Left.Z < Right.Z;
		});
		return Sorted;
	};

	PlanetMineralSurface::FBuildResult First;
	PlanetMineralSurface::FBuildResult Second;
	PlanetMineralSurface::Build(*Field, Params, First);
	PlanetMineralSurface::Build(*Field, Params, Second);

	if (!TestEqual(TEXT("两次构建元素数一致"),
		Second.MeshesByElement.Num(), First.MeshesByElement.Num()))
	{
		return false;
	}

	for (int32 ElementIndex = 0; ElementIndex < First.MeshesByElement.Num(); ++ElementIndex)
	{
		const FPlanetMineralMesh& Expected = First.MeshesByElement[ElementIndex];
		const FPlanetMineralMesh& Actual = Second.MeshesByElement[ElementIndex];

		TestEqual(*FString::Printf(TEXT("元素 #%d 三角形数一致"), ElementIndex),
			Actual.Triangles.Num(), Expected.Triangles.Num());

		const TArray<FVector3f> ExpectedVertices = SortedVertices(Expected);
		const TArray<FVector3f> ActualVertices = SortedVertices(Actual);
		if (!TestEqual(*FString::Printf(TEXT("元素 #%d 顶点数一致"), ElementIndex),
			ActualVertices.Num(), ExpectedVertices.Num()))
		{
			continue;
		}

		int32 Differing = 0;
		for (int32 Index = 0; Index < ExpectedVertices.Num(); ++Index)
		{
			if (ExpectedVertices[Index].X != ActualVertices[Index].X
				|| ExpectedVertices[Index].Y != ActualVertices[Index].Y
				|| ExpectedVertices[Index].Z != ActualVertices[Index].Z)
			{
				++Differing;
			}
		}
		TestEqual(*FString::Printf(TEXT("元素 #%d 顶点集合一致"), ElementIndex),
			Differing, 0);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlanetMineralSurfaceCacheKeyTest,
	"StellarEra.Planet.MineralSurface.CacheKey",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetMineralSurfaceCacheKeyTest::RunTest(const FString& Parameters)
{
	PlanetMineralSurface::FCacheKey Base;
	Base.SurfaceRadiusCm = 100000.f;
	Base.Scale = FVector(1.0, 1.0, 1.0);
	Base.GridResolution = 40;

	PlanetMineralSurface::FCacheKey Same = Base;
	TestTrue(TEXT("同参数相等"), Same == Base);
	TestFalse(TEXT("同参数不不等"), Same != Base);

	PlanetMineralSurface::FCacheKey Radius = Base;
	Radius.SurfaceRadiusCm = 100001.f;
	TestTrue(TEXT("半径差 1 cm 不相等"), Radius != Base);

	PlanetMineralSurface::FCacheKey TinyRadius = Base;
	TinyRadius.SurfaceRadiusCm = 100000.003f;
	TestTrue(TEXT("半径差 3 mm 视为同一颗行星"), TinyRadius == Base);

	PlanetMineralSurface::FCacheKey Scale = Base;
	Scale.Scale = FVector(2.0, 2.0, 2.0);
	TestTrue(TEXT("缩放变化不相等"), Scale != Base);

	PlanetMineralSurface::FCacheKey Resolution = Base;
	Resolution.GridResolution = 56;
	TestTrue(TEXT("格数变化不相等"), Resolution != Base);

	return true;
}
```

- [ ] **Step 3: 跑一次测，确认失败原因是签名不匹配**

```bash
"d:/Application/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" \
  "F:/workspace/ue/StellarEra/StellarEra.uproject" \
  -ExecCmds="Automation RunTests StellarEra.Planet.Mineral" \
  -TestExit="Automation Test Queue Empty" \
  -unattended -nopause -nullrhi -nosplash -stdout \
  -abslog="F:/workspace/ue/StellarEra/Saved/Logs/MineralIsoT2Red.log"
```

期望：**编译失败**（`Build` 的参数表对不上、`GetElementMesh` 不存在），日志里是 `error C2660` / `C2039` 一类。

- [ ] **Step 4: 观察体头文件换成动态网格版本**

`Source/StellarEraPlanet/Public/PlanetMineralViewActor.h` 整个替换成（**UTF-8 with BOM**）：

```cpp
// Copyright Voxel Plugin SAS. All Rights Reserved.
// 矿物透视的观察体：半透明球壳 + 每个元素一层等值面矿体。

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "PlanetGeologyTypes.h"
#include "PlanetMineralSurface.h"
#include "PlanetMineralViewActor.generated.h"

class UDynamicMeshComponent;
class UMaterialInstanceDynamic;
class UStaticMeshComponent;

/**
 * 观察体。
 *
 * **必须独立 spawn，不能挂在行星 Actor 下面**：进入模式会
 * SetActorHiddenInGame(true) 隐藏行星，而 AActor::IsHidden 会让**所有**子组件的
 * USceneComponent::ShouldRender 返回 false（SceneComponent.cpp:3517），球壳与矿体
 * 会跟着一起消失。
 *
 * 矿体顶点相对本 Actor 原点（= 球心），组件不随行星旋转缩放。
 */
UCLASS()
class STELLARERAPLANET_API APlanetMineralViewActor : public AActor
{
	GENERATED_BODY()

public:
	APlanetMineralViewActor();

	/**
	 * 建球壳、按元素建动态网格组件。
	 *
	 * MeshesByElement 与 Elements 的下标必须同为**运行期元素下标**
	 * （即 FPlanetGeologyField::GetSampledElementDefinitions 的顺序）。
	 * 空网格照样占一个组件槽，下标才能与图例、数字键对齐。
	 * 网格/材质加载失败或两者长度不一致时返回 false 并打 Error。
	 */
	bool Build(const TArray<FPlanetMineralMesh>& MeshesByElement, const FLinearColor& ShellColor,
		float SurfaceRadiusCm, const TArray<FPlanetGeologyElement>& Elements);

	int32 NumElements() const { return MineralMeshes.Num(); }
	bool IsElementVisible(int32 Index) const;
	void SetElementVisible(int32 Index, bool bVisible);
	void SetAllElementsVisible(bool bVisible);

	/** 球壳组件，仅供测试与调试。 */
	UStaticMeshComponent* GetShellMesh() const { return ShellMesh; }

	/** 某个元素的矿体组件，仅供测试与调试。越界返回 nullptr。 */
	UDynamicMeshComponent* GetElementMesh(int32 Index) const;

private:
	UPROPERTY(VisibleAnywhere, Category = "MineralView")
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> ShellMesh;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> ShellMaterial;

	/** 下标 = 运行期元素下标；空元素对应一个没有三角形的组件。 */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UDynamicMeshComponent>> MineralMeshes;
};
```

- [ ] **Step 5: 观察体实现换成动态网格版本**

`Source/StellarEraPlanet/Private/PlanetMineralViewActor.cpp` 整个替换成（**UTF-8 with BOM**）：

```cpp
// Copyright Voxel Plugin SAS. All Rights Reserved.

#include "PlanetMineralViewActor.h"

#include "PlanetMineralMaterialBuilder.h"

#include "Components/DynamicMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/MeshNormals.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"

namespace
{
	const TCHAR* const SphereMeshPath = TEXT("/Engine/BasicShapes/Sphere.Sphere");

	UMaterialInterface* LoadMaterial(const TCHAR* PackagePath, const TCHAR* AssetName)
	{
		const FString ObjectPath = FString::Printf(TEXT("%s.%s"), PackagePath, AssetName);
		UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr, *ObjectPath);
#if WITH_EDITOR
		if (!Material)
		{
			// 首次运行、或材质资产还没生成：就地建一次再加载。打包版没有这条路。
			FString Error;
			if (PlanetMineralMaterials::Build(Error))
			{
				Material = LoadObject<UMaterialInterface>(nullptr, *ObjectPath);
			}
			else
			{
				UE_LOG(LogTemp, Error, TEXT("[矿物] 材质生成失败：%s"), *Error);
			}
		}
#endif
		return Material;
	}
}

APlanetMineralViewActor::APlanetMineralViewActor()
{
	PrimaryActorTick.bCanEverTick = false;

	// 必须有根组件：没有根，SetActorLocation 不生效（新 spawn 的裸 Actor 位置恒为 0）。
	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	RootComponent = SceneRoot;
}

bool APlanetMineralViewActor::Build(const TArray<FPlanetMineralMesh>& MeshesByElement,
	const FLinearColor& ShellColor, float SurfaceRadiusCm,
	const TArray<FPlanetGeologyElement>& Elements)
{
	if (SurfaceRadiusCm <= 0.f)
	{
		UE_LOG(LogTemp, Error, TEXT("[矿物] 参数非法：半径 %.1f cm"), SurfaceRadiusCm);
		return false;
	}

	if (MeshesByElement.Num() != Elements.Num())
	{
		UE_LOG(LogTemp, Error, TEXT("[矿物] 网格数不等于元素数：%d / %d"),
			MeshesByElement.Num(), Elements.Num());
		return false;
	}

	UStaticMesh* SphereMesh = LoadObject<UStaticMesh>(nullptr, SphereMeshPath);
	UMaterialInterface* GhostMaterial = LoadMaterial(
		PlanetMineralMaterials::GetGhostMaterialPath(), TEXT("M_PlanetGhost"));
	UMaterialInterface* BlockMaterial = LoadMaterial(
		PlanetMineralMaterials::GetBlockMaterialPath(), TEXT("M_MineralBlock"));

	if (!SphereMesh || !GhostMaterial || !BlockMaterial)
	{
		UE_LOG(LogTemp, Error, TEXT("[矿物] 网格或材质加载失败：球=%s 球壳材质=%s 矿体材质=%s"),
			SphereMesh ? TEXT("ok") : TEXT("缺"),
			GhostMaterial ? TEXT("ok") : TEXT("缺"),
			BlockMaterial ? TEXT("ok") : TEXT("缺"));
		return false;
	}

	// --- 球壳：引擎球是 100 cm 直径，缩放到行星直径 ---
	// 运行期建的组件必须在 RegisterComponent 之前设好 Mobility：注册之后再改会走
	// 重新注册的路径，而这块网格从第一帧起就要跟着行星移动。
	const float DiameterCm = SurfaceRadiusCm * 2.f;
	ShellMesh = NewObject<UStaticMeshComponent>(this, TEXT("MineralShell"));
	ShellMesh->SetupAttachment(SceneRoot);
	ShellMesh->SetMobility(EComponentMobility::Movable);
	ShellMesh->SetStaticMesh(SphereMesh);
	ShellMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	ShellMesh->SetCastShadow(false);
	ShellMesh->RegisterComponent();
	ShellMesh->SetRelativeLocation(FVector::ZeroVector);
	ShellMesh->SetRelativeScale3D(FVector(DiameterCm / 100.f));

	ShellMaterial = UMaterialInstanceDynamic::Create(GhostMaterial, this);
	if (ShellMaterial)
	{
		ShellMaterial->SetVectorParameterValue(TEXT("ShellColor"), ShellColor);
		ShellMesh->SetMaterial(0, ShellMaterial);
	}

	// --- 每个元素一个动态网格 ---
	MineralMeshes.Reset();
	MineralMeshes.Reserve(MeshesByElement.Num());

	for (int32 ElementIndex = 0; ElementIndex < MeshesByElement.Num(); ++ElementIndex)
	{
		const FPlanetMineralMesh& Source = MeshesByElement[ElementIndex];

		UDynamicMeshComponent* Component = NewObject<UDynamicMeshComponent>(
			this, *FString::Printf(TEXT("MineralMesh%d"), ElementIndex));
		Component->SetupAttachment(SceneRoot);
		Component->SetMobility(EComponentMobility::Movable);
		Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Component->SetCastShadow(false);
		Component->RegisterComponent();

		UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(BlockMaterial, this);
		if (MID)
		{
			MID->SetVectorParameterValue(TEXT("BlockColor"), Elements[ElementIndex].Color);
			Component->SetMaterial(0, MID);
		}

		UE::Geometry::FDynamicMesh3 Mesh;
		Mesh.EnableAttributes();

		// 下标先过一遍映射表：等值面给出的顶点将来若出现空洞，这里才不会静默错位。
		TArray<int32> VertexIds;
		VertexIds.Reserve(Source.VerticesCm.Num());
		for (const FVector3f& VertexCm : Source.VerticesCm)
		{
			VertexIds.Add(Mesh.AppendVertex(UE::Math::TVector<double>(
				double(VertexCm.X), double(VertexCm.Y), double(VertexCm.Z))));
		}

		// AppendTriangle 在退化三角形（三点共线 / 面积为 0）上返回 -1 而不是抛错。
		// 丢掉它们是正确行为，但数量要进日志 —— 全被丢掉时画面上就是「这个元素没矿」。
		int32 Rejected = 0;
		for (int32 Index = 0; Index + 2 < Source.Triangles.Num(); Index += 3)
		{
			const int32 A = Source.Triangles[Index];
			const int32 B = Source.Triangles[Index + 1];
			const int32 C = Source.Triangles[Index + 2];
			if (!VertexIds.IsValidIndex(A) || !VertexIds.IsValidIndex(B)
				|| !VertexIds.IsValidIndex(C)
				|| Mesh.AppendTriangle(VertexIds[A], VertexIds[B], VertexIds[C]) < 0)
			{
				++Rejected;
			}
		}
		if (Rejected > 0)
		{
			UE_LOG(LogTemp, Warning, TEXT("[矿物] 元素 #%d：%d 个三角形被丢弃（退化或越界）"),
				ElementIndex, Rejected);
		}

		// 等值面抽取只给顶点与三角形，没有法线；材质靠 VertexNormalWS 取它，
		// 所以这里按面积加权的顶点法线补一遍。UV 层留空：材质不采样 UV。
		if (Mesh.TriangleCount() > 0)
		{
			UE::Geometry::FMeshNormals::InitializeOverlayToPerVertexNormals(
				Mesh.Attributes()->PrimaryNormals());
		}

		Component->SetMesh(MoveTemp(Mesh));
		MineralMeshes.Add(Component);
	}

	return true;
}

UDynamicMeshComponent* APlanetMineralViewActor::GetElementMesh(int32 Index) const
{
	return MineralMeshes.IsValidIndex(Index) ? MineralMeshes[Index].Get() : nullptr;
}

bool APlanetMineralViewActor::IsElementVisible(int32 Index) const
{
	return MineralMeshes.IsValidIndex(Index) && MineralMeshes[Index]
		&& MineralMeshes[Index]->IsVisible();
}

void APlanetMineralViewActor::SetElementVisible(int32 Index, bool bVisible)
{
	if (!MineralMeshes.IsValidIndex(Index) || !MineralMeshes[Index])
	{
		return;
	}
	MineralMeshes[Index]->SetVisibility(bVisible, true);
}

void APlanetMineralViewActor::SetAllElementsVisible(bool bVisible)
{
	for (UDynamicMeshComponent* Component : MineralMeshes)
	{
		if (Component)
		{
			Component->SetVisibility(bVisible, true);
		}
	}
}
```

- [ ] **Step 6: 组件头文件换掉方块参数、加缓存成员**

`Source/StellarEraPlanet/Public/PlanetMineralViewComponent.h` 一共四处改动：

1. 在 `#include "PlanetGeologyTypes.h"` 下一行加：

```cpp
#include "PlanetMineralSurface.h"
```

2. 把 `NumElements` 上方那段注释里的「与观察体的 ISM 组数相等」改成「与观察体的动态网格组件数相等」：

```cpp
	/**
	 * 元素数，返回 Elements.Num()（运行期采样顺序）。
	 *
	 * 与观察体的动态网格组件数相等 —— 这个前提由 APlanetMineralViewActor::Build 的
	 * 「网格数 == 元素数」校验保证（不等时 Build 直接失败）。若将来允许部分构建，
	 * 图例布局与命中测试必须改用 ViewActor->NumElements()。
	 */
```

3. 把 `SpacingRatio` / `BlockSizeRatio` / `Seed` 三个 `UPROPERTY` **整段删除**，换成：

```cpp
	/**
	 * 等值面网格密度：每轴格数（角点数 = GridResolution + 1）。
	 *
	 * 40 首进约 0.1–0.3 秒；56 约 0.3–0.8 秒，边缘更圆。改动会让缓存失效。
	 */
	UPROPERTY(EditAnywhere, Category = "Planet|MineralView", meta = (ClampMin = "16", ClampMax = "96"))
	int32 GridResolution = 40;
```

4. 在 `TArray<FPlanetGeologyElement> Elements;` 之后、`UPROPERTY(Transient) TObjectPtr<APlanetMineralViewActor> ViewActor;` 之前加：

```cpp
	/** 上一次构建的结果。Deactivate 不清：再进同一颗行星即时出图。 */
	PlanetMineralSurface::FBuildResult CachedSurface;
	PlanetMineralSurface::FCacheKey CachedKey;
	bool bHasCachedSurface = false;
```

同时把 `GetElements()` 的注释里「方块组下标」改成「矿体组件下标」。

- [ ] **Step 7: 组件实现接上构建器与缓存**

`Source/StellarEraPlanet/Private/PlanetMineralViewComponent.cpp` 三处改动：

1. include 段：删掉 `#include "PlanetMineralBlocks.h"` 与 `#include "HAL/PlatformTime.h"`，改成：

```cpp
#include "PlanetMineralViewComponent.h"

#include "PlanetAtmosphereComponent.h"
#include "PlanetGeologyField.h"
#include "PlanetMineralViewActor.h"
#include "VoxelPlanetActor.h"

#include "Engine/World.h"
```

2. `Activate` 整个函数替换成：

```cpp
bool UPlanetMineralViewComponent::Activate(AVoxelPlanetActor* InPlanet)
{
	if (bActive)
	{
		Deactivate();
	}

	if (!InPlanet)
	{
		return false;
	}

	TSharedPtr<const FPlanetGeologyField> Geology = InPlanet->GetGeologyField();
	if (!Geology.IsValid() || Geology->IsEmpty())
	{
		return false;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	const float SurfaceRadiusCm = InPlanet->GetSurfaceRadiusCm();
	// 地质深度以体素计，换回厘米要用单个体素的厘米尺寸（基类属性，与 Radius 同级）。
	// 明确假设行星 Actor 的等比缩放 S = 1：这个换算不含 Actor 缩放（见计划 Global Constraints）。
	const float VoxelSizeCm = InPlanet->VoxelSize;
	if (SurfaceRadiusCm <= 0.f || VoxelSizeCm <= 0.f)
	{
		return false;
	}

	PlanetMineralSurface::FBuildParams Params;
	Params.SurfaceRadiusCm = SurfaceRadiusCm;
	Params.VoxelSizeCm = VoxelSizeCm;
	Params.GridResolution = GridResolution;
	// 等值线跟地表着色共用一份参数：矿体边界与地表明暗是同一套判据。
	Params.IsoValue = Geology->GetAppearance().ColorThreshold;

	// 键里不含 IsoValue / VoxelSizeCm：两者都由行星的地质数据与体素尺寸定，而键里已经
	// 有行星本身。GridResolution 是组件上可编辑的属性，所以必须进键。
	PlanetMineralSurface::FCacheKey Key;
	Key.Planet = InPlanet;
	Key.SurfaceRadiusCm = SurfaceRadiusCm;
	Key.Scale = InPlanet->GetActorScale3D();
	Key.GridResolution = GridResolution;

	const bool bReused = bHasCachedSurface && CachedKey == Key;
	if (!bReused)
	{
		PlanetMineralSurface::Build(*Geology, Params, CachedSurface);
		CachedKey = Key;
		bHasCachedSurface = true;
	}

	const TArray<FPlanetGeologyElement> SampledElements =
		Geology->GetSampledElementDefinitions();

	int32 TotalTriangles = 0;
	for (const FPlanetMineralMesh& Mesh : CachedSurface.MeshesByElement)
	{
		TotalTriangles += Mesh.Triangles.Num() / 3;
	}

	// 每元素「占据格数 / 面数」：诊断「全都没生成」与「某个元素为 0」是两回事，
	// 光看总数分不出来。顺序 = Elements（运行期采样顺序），下标与组件一致。
	TArray<FString> PerElementParts;
	for (int32 Index = 0; Index < CachedSurface.MeshesByElement.Num(); ++Index)
	{
		const FString Label =
			SampledElements.IsValidIndex(Index) && !SampledElements[Index].DisplayName.IsEmpty()
			? SampledElements[Index].DisplayName
			: FString::Printf(TEXT("#%d"), Index);
		PerElementParts.Add(FString::Printf(TEXT("元素%s:%d格/%d面"), *Label,
			CachedSurface.MeshesByElement[Index].OccupiedCells,
			CachedSurface.MeshesByElement[Index].Triangles.Num() / 3));
	}

	UE_LOG(LogTemp, Display, TEXT("[矿物] %s：网格 %d³（%d 个角点），%d 个三角形，构建 %.1f ms%s"),
		*InPlanet->GetName(), GridResolution, CachedSurface.GridPoints, TotalTriangles,
		CachedSurface.BuildSeconds * 1000.0,
		bReused ? TEXT("（复用缓存，耗时为首建）") : TEXT(""));
	UE_LOG(LogTemp, Display, TEXT("[矿物] %s：每元素矿体 %s"),
		*InPlanet->GetName(),
		PerElementParts.IsEmpty() ? TEXT("(无元素)") : *FString::Join(PerElementParts, TEXT(" ")));

	FActorSpawnParameters SpawnParams;
	// Owner 是 AActor*：组件不能当 Owner，取本组件的宿主 Actor（就是这颗行星）。
	SpawnParams.Owner = GetOwner();
	SpawnParams.SpawnCollisionHandlingOverride =
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ViewActor = World->SpawnActor<APlanetMineralViewActor>(
		APlanetMineralViewActor::StaticClass(),
		InPlanet->GetActorLocation(), FRotator::ZeroRotator, SpawnParams);

	if (!ViewActor
		|| !ViewActor->Build(CachedSurface.MeshesByElement, InPlanet->SurfaceColor,
			SurfaceRadiusCm, SampledElements))
	{
		UE_LOG(LogTemp, Error, TEXT("[矿物] 观察体创建失败：%s"), *InPlanet->GetName());
		if (ViewActor)
		{
			ViewActor->Destroy();
			ViewActor = nullptr;
		}
		return false;
	}

	Planet = InPlanet;
	CachedGeology = Geology;
	Elements = SampledElements;

	// 先存后改：恢复时原样写回，别假设进入前一定是「可见 + 有碰撞」。
	bSavedPlanetHidden = InPlanet->IsHidden();
	bSavedCollisionEnabled = InPlanet->GetActorEnableCollision();
	InPlanet->SetActorEnableCollision(false);
	InPlanet->SetActorHiddenInGame(true);

	if (UPlanetAtmosphereComponent* Atmosphere = InPlanet->GetAtmosphereComponent())
	{
		bSavedAtmosphereEnabled = Atmosphere->bAtmosphereEnabled;
		bSavedAtmosphereVisible = Atmosphere->IsVisible();
		Atmosphere->bAtmosphereEnabled = false;
		Atmosphere->SetVisibility(false);
	}

	bActive = true;
	SetComponentTickEnabled(true);
	return true;
}
```

3. `TickComponent` 里「跟着转会把方块拧歪」改成「跟着转会把矿体拧歪」：

```cpp
	// 只跟位置：N 体只平移，行星不转向也不缩放；跟着转会把矿体拧歪。
```

- [ ] **Step 8: 加模块依赖、删掉方块路径**

1. `Source/StellarEraPlanet/StellarEraPlanet.Build.cs` 里，在 Task 1 加的 `GeometryCore` 那一行下面补：

```csharp
		// 矿体渲染走 UDynamicMeshComponent（GeometryFramework 的运行时模块）。
		PrivateDependencyModuleNames.AddRange(new string[] { "GeometryFramework" });
```

2. 删掉方块路径的三个文件（**必须先 `git rm`，让删除进索引**；直接文件管理器删掉会让 head 里的文件继续被测试引用）：

```bash
cd "F:/workspace/ue/StellarEra"
git rm Source/StellarEraPlanet/Public/PlanetMineralBlocks.h \
       Source/StellarEraPlanet/Private/PlanetMineralBlocks.cpp \
       Source/StellarEraPlanet/Private/PlanetMineralBlocksTests.cpp
```

3. 确认没有残留引用：

```bash
grep -rn "PlanetMineralBlocks\|FPlanetMineralBlock\b\|BlockSizeRatio\|SpacingRatio" Source/
```

期望：**无输出**。有输出就是漏改，先补再往下走。

- [ ] **Step 9: 构建**

关掉编辑器，跑 Global Constraints 里那条 UBT 命令（日志进 `Saved/Logs/MineralIsoBuild.log`），期望 `Result: Succeeded`。
常见编译错误：`FDynamicMesh3` 缺命名空间（应为 `UE::Geometry::FDynamicMesh3`）、`FMeshNormals` 头文件名写错（`DynamicMesh/MeshNormals.h`）。

- [ ] **Step 10: 跑本任务的测试**

```bash
"d:/Application/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" \
  "F:/workspace/ue/StellarEra/StellarEra.uproject" \
  -ExecCmds="Automation RunTests StellarEra.Planet.Mineral" \
  -TestExit="Automation Test Queue Empty" \
  -unattended -nopause -nullrhi -nosplash -stdout \
  -abslog="F:/workspace/ue/StellarEra/Saved/Logs/MineralIsoT2Green.log"
```

期望：`grep -c "Result={Success}"` = **9**（7 个 `MineralSurface.*` + 2 个 `MineralView.*`），`grep -c "Result={Fail}"` = **0**。

若 `IsoAccuracy` 或 `Threshold` 红了，先看日志里的 `%d/%d 越界`：越界比例只略高于 10% 说明容差选紧了，把 `Tolerance` 从 0.15 放到 0.2 再跑；若大量越界（比如 100%），那是 `Build` 的插值或 `IsoValue` 接错，回去查代码，别改容差。

- [ ] **Step 11: 提交**

```bash
cd "F:/workspace/ue/StellarEra"
git add Source/StellarEraPlanet/Public/PlanetMineralViewActor.h \
        Source/StellarEraPlanet/Private/PlanetMineralViewActor.cpp \
        Source/StellarEraPlanet/Public/PlanetMineralViewComponent.h \
        Source/StellarEraPlanet/Private/PlanetMineralViewComponent.cpp \
        Source/StellarEraPlanet/Private/PlanetMineralViewTests.cpp \
        Source/StellarEraPlanet/Private/PlanetMineralSurfaceTests.cpp \
        Source/StellarEraPlanet/StellarEraPlanet.Build.cs
git commit -- Source/StellarEraPlanet/Public/PlanetMineralViewActor.h \
  Source/StellarEraPlanet/Private/PlanetMineralViewActor.cpp \
  Source/StellarEraPlanet/Public/PlanetMineralViewComponent.h \
  Source/StellarEraPlanet/Private/PlanetMineralViewComponent.cpp \
  Source/StellarEraPlanet/Private/PlanetMineralViewTests.cpp \
  Source/StellarEraPlanet/Private/PlanetMineralSurfaceTests.cpp \
  Source/StellarEraPlanet/StellarEraPlanet.Build.cs \
  Source/StellarEraPlanet/Public/PlanetMineralBlocks.h \
  Source/StellarEraPlanet/Private/PlanetMineralBlocks.cpp \
  Source/StellarEraPlanet/Private/PlanetMineralBlocksTests.cpp \
  -m "feat(矿物): 观察体改用每元素等值面矿体

- APlanetMineralViewActor 每元素建一个 UDynamicMeshComponent，
  顶点法线由 FMeshNormals 补（等值面抽取不输出法线）
- UPlanetMineralViewComponent 接 PlanetMineralSurface::Build，
  按（行星、半径、缩放、格数）缓存，Deactivate 不清缓存
- 方块路径 PlanetMineralBlocks.* 与其 6 个测试删除；
  新增 7 个等值面测试（几何、等值线精度、元素顺序、阈值、
  空场、确定性、缓存键）

Co-Authored-By: Claude Code <noreply@anthropic.com>"
```

删除的文件也要出现在 commit 的 pathspec 里（它们已在索引中标记为删除），否则提交会漏掉删除。

---

### Task 3: 矿体材质加法线明暗项，并重建资产

**Files:**
- Modify: `Source/StellarEraPlanet/Private/PlanetMineralMaterialBuilder.cpp`
- Regenerate: `Content/Materials/M_MineralBlock.uasset`（Git LFS 跟踪）

**Interfaces:**
- Consumes: 现有的 `AddMineralScalarParam` / `AddMineralVectorParam` / `FindOrCreateMaterial` / `SaveMaterial`，以及控制台命令 `StellarEra.BuildMineralMaterials`。
- Produces: 材质参数名不变（`BlockColor`、`Brightness`），所以 `APlanetMineralViewActor::Build` 里那句 `SetVectorParameterValue(TEXT("BlockColor"), …)` **不用改**。

**为什么放在 Task 2 之后：** 这次要去掉 `MATUSAGE_InstancedStaticMeshes` 使用标记。只有 Task 2 删掉最后一个 ISM 之后，去掉它才是安全的。

- [ ] **Step 1: 扩表达式 include**

`Source/StellarEraPlanet/Private/PlanetMineralMaterialBuilder.cpp` 里，`#if WITH_EDITOR` 之后那段 include 按字母序补 6 个：

```cpp
#include "Materials/MaterialExpressionAbs.h"
#include "Materials/MaterialExpressionAdd.h"
#include "Materials/MaterialExpressionCameraVectorWS.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionDotProduct.h"
#include "Materials/MaterialExpressionVertexNormalWS.h"
```

放在 `#include "Materials/Material.h"` 与 `#include "Materials/MaterialExpressionFresnel.h"` 之间，保持字母序。

- [ ] **Step 2: 加两个小 helper**

在同一段匿名 namespace 里、`AddMineralVectorParam` 之后加（名字必须带 `Mineral` 前缀，理由见文件里那条 unity build 的注释）：

```cpp
	/** 建一个常量节点并登记到材质。 */
	UMaterialExpressionConstant* AddMineralConstant(UMaterial* Material, float Value,
		int32 PosX, int32 PosY)
	{
		UMaterialExpressionConstant* Expr = NewObject<UMaterialExpressionConstant>(Material);
		Expr->Material = Material;
		Expr->R = Value;
		Expr->MaterialExpressionEditorX = PosX;
		Expr->MaterialExpressionEditorY = PosY;
		Material->GetExpressionCollection().AddExpression(Expr);
		return Expr;
	}

	/** 建一个二元节点（A 接 0 号输出、B 接 0 号输出）并登记到材质。 */
	template <typename TExpression>
	TExpression* AddMineralBinaryOp(UMaterial* Material, UMaterialExpression* A,
		UMaterialExpression* B, int32 PosX, int32 PosY)
	{
		TExpression* Expr = NewObject<TExpression>(Material);
		Expr->Material = Material;
		Expr->A.Expression = A;
		Expr->A.OutputIndex = 0;
		Expr->B.Expression = B;
		Expr->B.OutputIndex = 0;
		Expr->MaterialExpressionEditorX = PosX;
		Expr->MaterialExpressionEditorY = PosY;
		Material->GetExpressionCollection().AddExpression(Expr);
		return Expr;
	}
```

- [ ] **Step 3: 重写 `BuildBlockMaterial`**

整个函数替换成：

```cpp
	/**
	 * 矿体：Unlit / Opaque / TwoSided，BlockColor × Brightness × Shade → Emissive，
	 * 其中 Shade = 0.6 + 0.4 × |dot(N, V)|。
	 *
	 * 纯色 Unlit 看不出体积，加一个轻量法线项后表面有起伏感，整体亮度量级不变。
	 * 取绝对值 + 双面是刻意的：等值面抽取的绕序不保证朝外，单面渲染会把一半矿体剔掉，
	 * 而 saturate(dot) 会让朝内那面变成一片死平。
	 */
	bool BuildBlockMaterial(FString& OutError)
	{
		UMaterial* Material = FindOrCreateMaterial(BlockPath, BlockAssetName, OutError);
		if (!Material)
		{
			return false;
		}

		UMaterialExpressionVectorParameter* BlockColor = AddMineralVectorParam(Material,
			TEXT("BlockColor"), FLinearColor::White, -900, -200);
		UMaterialExpressionScalarParameter* Brightness = AddMineralScalarParam(Material,
			TEXT("Brightness"), 1.4f, -900, -100);

		UMaterialExpressionMultiply* Tint = AddMineralBinaryOp<UMaterialExpressionMultiply>(
			Material, BlockColor, Brightness, -600, -150);

		UMaterialExpressionVertexNormalWS* VertexNormal =
			NewObject<UMaterialExpressionVertexNormalWS>(Material);
		VertexNormal->Material = Material;
		VertexNormal->MaterialExpressionEditorX = -800;
		VertexNormal->MaterialExpressionEditorY = -400;
		Material->GetExpressionCollection().AddExpression(VertexNormal);

		UMaterialExpressionCameraVectorWS* CameraVector =
			NewObject<UMaterialExpressionCameraVectorWS>(Material);
		CameraVector->Material = Material;
		CameraVector->MaterialExpressionEditorX = -800;
		CameraVector->MaterialExpressionEditorY = -500;
		Material->GetExpressionCollection().AddExpression(CameraVector);

		UMaterialExpressionDotProduct* Dot = AddMineralBinaryOp<UMaterialExpressionDotProduct>(
			Material, VertexNormal, CameraVector, -600, -450);

		UMaterialExpressionAbs* Abs = NewObject<UMaterialExpressionAbs>(Material);
		Abs->Material = Material;
		Abs->Input.Expression = Dot;
		Abs->Input.OutputIndex = 0;
		Abs->MaterialExpressionEditorX = -450;
		Abs->MaterialExpressionEditorY = -450;
		Material->GetExpressionCollection().AddExpression(Abs);

		UMaterialExpressionConstant* Scale = AddMineralConstant(Material, 0.4f, -450, -350);
		UMaterialExpressionMultiply* ShadeScale =
			AddMineralBinaryOp<UMaterialExpressionMultiply>(Material, Abs, Scale, -300, -400);

		UMaterialExpressionConstant* Bias = AddMineralConstant(Material, 0.6f, -300, -300);
		UMaterialExpressionAdd* Shade =
			AddMineralBinaryOp<UMaterialExpressionAdd>(Material, ShadeScale, Bias, -150, -350);

		UMaterialExpressionMultiply* Final =
			AddMineralBinaryOp<UMaterialExpressionMultiply>(Material, Tint, Shade, 0, -200);

		UMaterialEditorOnlyData* EditorData = Material->GetEditorOnlyData();
		if (!EditorData)
		{
			OutError = TEXT("GetEditorOnlyData() 返回空（M_MineralBlock）");
			return false;
		}
		EditorData->EmissiveColor.Expression = Final;
		EditorData->EmissiveColor.OutputIndex = 0;

		Material->BlendMode = BLEND_Opaque;
		Material->SetShadingModel(MSM_Unlit);
		// 绕序不保证朝外（见函数注释），必须双面。
		Material->TwoSided = true;
		// 已不再被 UInstancedStaticMeshComponent 使用：矿体走 UDynamicMeshComponent。

		return SaveMaterial(Material, BlockPath, OutError);
	}
```

注意 **删掉** 原来的这一行，不要再写回来：

```cpp
		Material->SetUsageByFlag(MATUSAGE_InstancedStaticMeshes, true);
```

- [ ] **Step 4: 构建**

关掉编辑器，跑 Global Constraints 里那条 UBT 命令，期望 `Result: Succeeded`。

- [ ] **Step 5: 无头重建材质资产**

**不要加 `-nullrhi`**：这一步要真正编译材质并写包。

```bash
"d:/Application/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" \
  "F:/workspace/ue/StellarEra/StellarEra.uproject" \
  -ExecCmds="StellarEra.BuildMineralMaterials" \
  -TestExit="StellarEra.MineralMaterials.Done ok=1" \
  -unattended -nopause -nosplash -stdout \
  -abslog="F:/workspace/ue/StellarEra/Saved/Logs/MineralMaterialRebuild.log"
```

期望：日志里出现 `StellarEra.MineralMaterials.Done ok=1`（命令自带的退出标记），进程自己退出。

- [ ] **Step 6: 确认资产真的重写了**

```bash
cd "F:/workspace/ue/StellarEra"
git status -- Content/Materials/M_MineralBlock.uasset
git diff --stat -- Content/Materials/M_MineralBlock.uasset
```

期望：显示 `modified`，且 stat 显示字节数有变化。若显示无改动，说明命令没跑到（查日志里 `[矿物] 材质已生成` 一行）——**别把没变的资产当成成功**。

- [ ] **Step 7: 复跑一次矿物测试**

```bash
"d:/Application/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" \
  "F:/workspace/ue/StellarEra/StellarEra.uproject" \
  -ExecCmds="Automation RunTests StellarEra.Planet.Mineral" \
  -TestExit="Automation Test Queue Empty" \
  -unattended -nopause -nullrhi -nosplash -stdout \
  -abslog="F:/workspace/ue/StellarEra/Saved/Logs/MineralIsoT3Green.log"
```

期望：9 Success / 0 Fail（材质能加载，观察体 Build 不因缺材质失败）。

- [ ] **Step 8: 提交（确认 uasset 走 LFS 指针）**

```bash
cd "F:/workspace/ue/StellarEra"
git add Source/StellarEraPlanet/Private/PlanetMineralMaterialBuilder.cpp \
        Content/Materials/M_MineralBlock.uasset
# 必须打印 version https://git-lfs.github.com/spec/v1，否则这个 uasset 会以二进制直接入库
git cat-file -p :Content/Materials/M_MineralBlock.uasset | head -1
git commit -- Source/StellarEraPlanet/Private/PlanetMineralMaterialBuilder.cpp \
  Content/Materials/M_MineralBlock.uasset \
  -m "feat(矿物): 矿体材质加法线明暗项并重建资产

- Emissive 乘 (0.6 + 0.4·|dot(N,V)|)，纯色下也能看出体积
- TwoSided = true：等值面绕序不保证朝外
- 去掉 MATUSAGE_InstancedStaticMeshes（矿体已改用动态网格组件）
- 重建 Content/Materials/M_MineralBlock.uasset

Co-Authored-By: Claude Code <noreply@anthropic.com>"
```

---

### Task 4: 全量验证

**Files:** 无（只读验证）

**Interfaces:**
- Consumes: Task 1–3 的全部产出。
- Produces: 验证证据（日志路径 + 计数），给提交说明与人工验收用。

- [ ] **Step 1: 构建 Editor 目标**

关掉编辑器，跑 Global Constraints 里那条 UBT 命令，日志进 `Saved/Logs/MineralIsoBuild.log`。判据只有一条：

```bash
grep -E "Result: (Succeeded|Failed)" "F:/workspace/ue/StellarEra/Saved/Logs/MineralIsoBuild.log" | tail -1
```

期望 `Result: Succeeded`。若显示 `Failed` 但同时有 `error C` 之外的链接错误，先看 `grep -c "error C"` —— 为零说明是历史遗留的链接问题，不是本次改动。

- [ ] **Step 2: 全量自动化**

```bash
"d:/Application/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" \
  "F:/workspace/ue/StellarEra/StellarEra.uproject" \
  -ExecCmds="Automation RunTests StellarEra" \
  -TestExit="Automation Test Queue Empty" \
  -unattended -nopause -nullrhi -nosplash -stdout \
  -abslog="F:/workspace/ue/StellarEra/Saved/Logs/MineralIsoFull.log"
```

```bash
grep -c "Result={Success}" "F:/workspace/ue/StellarEra/Saved/Logs/MineralIsoFull.log"
grep -c "Result={Fail}"    "F:/workspace/ue/StellarEra/Saved/Logs/MineralIsoFull.log"
```

期望 **96 / 0**。算术：改造前 92，删掉 `PlanetMineralBlocksTests.cpp` 的 6 个，新增 10 个（3 个场测试 + 7 个网格测试）= 96。
**报告的计数优先**：只要 `Fail = 0`、下面 10 个新名字全部出现，实际数字与 96 不一致就按日志走并在提交说明里写明（可能有人同时加了别的测试）。

- [ ] **Step 3: 新测试确实在跑**

```bash
grep -oE "Test Started\. Name: \{StellarEra\.Planet\.(Geology\.ElementNorms|MineralSurface|MineralView)\.?[A-Za-z]*\}" \
  "F:/workspace/ue/StellarEra/Saved/Logs/MineralIsoFull.log" | sort -u
```

期望能看到这 12 个名字（3 ElementNorms/SampledDefinitions + 7 MineralSurface + 2 MineralView）：

```
StellarEra.Planet.Geology.ElementNorms.MatchesDominant
StellarEra.Planet.Geology.ElementNorms.BandProbe
StellarEra.Planet.Geology.SampledDefinitions
StellarEra.Planet.MineralSurface.Build
StellarEra.Planet.MineralSurface.IsoAccuracy
StellarEra.Planet.MineralSurface.ElementOrder
StellarEra.Planet.MineralSurface.Threshold
StellarEra.Planet.MineralSurface.Empty
StellarEra.Planet.MineralSurface.Deterministic
StellarEra.Planet.MineralSurface.CacheKey
StellarEra.Planet.MineralView.ActorBuild
StellarEra.Planet.MineralView.Empty
```

- [ ] **Step 4: 干净度检查**

```bash
cd "F:/workspace/ue/StellarEra"
grep -rn "PlanetMineralBlocks\|FPlanetMineralBlock\b\|BlockSizeRatio\|SpacingRatio\|BlockTiltDegrees\|CubeMeshPath" Source/
grep -rn "MineralBlocks" Source/StellarEra/ Source/StellarEraVehicle/
```

两条都必须**无输出**。第一条查残留的方块路径；第二条查 UI/输入层是否还按「方块组」写注释或变量名。

- [ ] **Step 5: 手工 PIE 验收（需要人）**

在 `UniverseMap` 里（脚本改不了这一步，交给人）：

1. 星图里选中体素行星，按 **H**：行星消失、玻璃球出现，球内是**成团的、表面平滑的不规则矿体**，不再是彼此分离的小正方体。
2. 点图例、按数字键 `1..6`：对应元素的整份矿体显隐，其余不受影响；按 `0` 全部恢复。
3. 观察 15–30 秒（天体在 N 体下移动）：球壳与矿体跟着行星走，不漂移、不抖。
4. 再按 **H**：行星、大气、碰撞全部恢复；按 **M** 退出星图也不残留观察体。
5. 打开 `Saved/Logs/` 里 PIE 的日志，找 `[矿物] <行星>：网格 40³（…）… 构建 X ms`：
   - `X` 应在 **300 ms 以内**（首进预算）；超了先把 `GridResolution` 降到 32 再看，别先怀疑缓存。
   - 每元素那一行里，六个元素的 `格` 与 `面` 都不该是 0（是 0 说明该元素的场在等值线以下，回查 `earth.json` 的 `abundance`）。
6. 编辑器必须在前台再做一次第 5 条：后台时整个进程被硬限 3 FPS，耗时数字没有意义。

验收结论写进提交说明或 SDD 台账，写「人已验证」或「未验证」，别写「应该没问题」。

- [ ] **Step 6: 不再改代码的前提下收尾**

若 Step 5 发现矿体边缘仍偏棱角（40³ 的格子太粗），**不要在本计划里改**：spec §9 已给出两条兜底（56³ 或 Laplacian 平滑），另起一个变更。本计划到此为止。

---

## 完成后

- 全分支评审通过后，用 `superpowers:finishing-a-development-branch` 收尾。
- CLAUDE.md 要求报告实际用到的技能 / Agent：本计划由 `superpowers:writing-plans` 编写，执行走 `superpowers:subagent-driven-development`，每个任务后跑 `superpowers:verification-before-completion` 的证据检查。
- 已知与本计划无关的既有失败：Game 目标的 `Source/StellarEraVehicle/Private/PlanetCharacterAnimTests.cpp:44` 编译错误，属改动前就存在的问题，不要在本分支顺手修。



