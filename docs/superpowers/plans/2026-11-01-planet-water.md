# 行星水体（海洋 / 湖泊 / 河流）实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 给体素行星加一层程序化水体 —— 海平面以下的海洋、内陆盆地的湖泊、从高处流向低处的河流，水面平滑半透明、河床真的被下切，全部由地形高度场自动推演。

**Architecture:** 先在 `StellarEraPlanet` 里加一条**水文烘焙**链路：拿地形模块的高度回调，用 Priority-Flood+ε 填洼、按 4 邻接汇流、算出每格的「水位 / 下切深度 / 河流强度」，存成一张只读的 `FPlanetHydrologyField`。这张场同时喂给两个消费者 —— 体素生成器（地表减去下切、按水深配色）与全新的水面几何构建器（海洋球壳 / 湖泊球面片 / 河流条带，全部刻意探入地形，岸线由深度测试自然裁出）。

**Tech Stack:** UE 5.8.3 / C++ / VoxelFree（体素世界与生成器）/ GeometryFramework 的 `UDynamicMeshComponent`（水面网格）/ 编辑器态 C++ 材质构建器（照 `PlanetAtmosphereMaterialBuilder.cpp` 的写法）。

**Spec:** `docs/superpowers/specs/2026-11-01-planet-water-design.md`（实施者必须同时读它；本计划只负责「怎么做」，不重复论证「为什么」）

## Global Constraints

- **引擎**：`D:/Application/Epic Games/UE_5.8`，Win64 Development，UE 5.8.3。编辑器用的是 VS Code，仓库里没有 `.sln`/`.vcxproj` 是正常的。
- **构建前必须关闭编辑器**（Live Coding 会锁 DLL）。构建命令（cwd 在项目外，别用 `Build.bat`）：

  ```bash
  "D:/Application/Epic Games/UE_5.8/Engine/Binaries/ThirdParty/DotNet/10.0/win-x64/dotnet.exe" \
    "D:/Application/Epic Games/UE_5.8/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.dll" \
    StellarEraEditor Win64 Development \
    -Project="F:/workspace/ue/StellarEra/StellarEra.uproject" -WaitMutex -FromMsBuild
  ```

- **自动化测试**（编辑器开着也能跑，约 25 s 干净退出）：

  ```bash
  "D:/Application/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" \
    "F:/workspace/ue/StellarEra/StellarEra.uproject" \
    -ExecCmds="Automation RunTests StellarEra.Planet" \
    -TestExit="Automation Test Queue Empty" \
    -unattended -nopause -nullrhi -nosplash -stdout -abslog="<临时目录>/water.log"
  ```

  编辑器世界不执行 `BeginPlay`；本计划的测试全部是纯函数 / 纯计算，不依赖 BeginPlay。

- **模块边界**：所有代码进 `Source/StellarEraPlanet/`。`Plugins/` 下的 VoxelFree 与 VibeUE **一个字都不改**。
- **不新增插件依赖**：水面用已有的 `GeometryFramework`（`UDynamicMeshComponent`，`StellarEraPlanet.Build.cs` 里已经是 Private 依赖）。
- **目录约定**：`Src/<功能>/Foo.h` 与 `Foo.cpp` 同目录并列；`*Tests.cpp` 只能放 `Src/Tests/`；新增子目录**不用**改 `Build.cs`（它用循环展开 `Src/` 下所有子目录）。
- **编码**：含中文的 `.h`/`.cpp` 必须存 **UTF-8 with BOM**。不要统一换行符（CRLF/LF 混合是仓库常态）。
- **命名**：Epic 风格，`A`/`U`/`F`/`E`/`T` 前缀，布尔 `bXxx`，UObject 指针用 `TObjectPtr<>`。
- **单位**：水文场里一律存**体素半径**（到球心的距离，体素）。归一化位移只在 `ComputeRawPlanetOffset` 与地形模块参数里出现。水面几何在本地空间用厘米（`体素 × VoxelSize`）。
- **测试前缀**：`StellarEra.Planet.Hydrology.*`、`StellarEra.Planet.Water.*`、`StellarEra.Planet.Terrain.*`。
- **提交**：`git add <路径>` 之后 **pathspec 必须挂在 commit 上**（`git commit -m "..." -- <路径>`）；索引里有常驻暂存文件，裸 `git commit` 会吞掉全部。
- **测试文件模板**：

  ```cpp
  #include "CoreMinimal.h"
  #include "Misc/AutomationTest.h"
  #if WITH_DEV_AUTOMATION_TESTS

  IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraXxxTest,
      "StellarEra.Planet.Xxx",
      EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

  bool FStellarEraXxxTest::RunTest(const FString& Parameters)
  {
      // ...
      return true;
  }
  #endif
  ```

## 执行前基线（**先做，别跳**）

- [x] **把工作区现有未提交改动落盘。** 已在写计划时完成（`git status --short -- Source/` 现在应为空）。当时 5 个未提交文件拆成了 3 个提交：

  - `956d4b6` `fix(行星): biome MID 改用瞬态包当 Outer` —— `VoxelPlanetActor.h/.cpp` + `VoxelPlanetBiomeMaterialTests.cpp`
  - `e988f2d` `fix(恒星): 太阳材质构建器改用具名命名空间` —— unity build 撞名
  - `123cacc` `test(视点): 登船判据跟上 bBoarded`

  开工时只需确认 `git status --short -- Source/` 是空的；不是空就说明又叠了新工作，先弄明白再动手。

- [ ] **确认目标关卡里的行星参数**（`Content/Code/Maps/UniverseMap.umap`，带体素行星的那颗）：记录 `Radius`、`VoxelSize`、`Terrain` 指向的 `DA_Terrain_*` 以及其中的 `SeaLevel` / `Amplitude`。Task 9 的验收要用它。用编辑器 MCP（`execute_python_code`）：

  ```python
  import unreal
  for a in unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors():
      if isinstance(a, unreal.VoxelPlanetActor):
          print(a.get_actor_label(), a.get_editor_property("radius"), a.voxel_size,
                a.get_editor_property("terrain"))
  ```

  编辑器没开就跳过，Task 9 再补。**别把这里的读数当成后面任务的前提** —— 本计划的代码不依赖具体数值。

---

## 文件结构

```
Source/StellarEraPlanet/Src/
├── Hydrology/                          新增。纯计算，不 include Terrain
│   ├── PlanetHydrologyTypes.h          参数 + 湖泊 + 河流折线（纯数据）
│   ├── PlanetHydrologyGrid.h           cubemap 格 ↔ 方向 的索引约定（只头文件）
│   ├── PlanetHydrologyField.h/.cpp     只读快照 + 双线性采样
│   └── PlanetHydrologyBake.h/.cpp      填洼 → 汇流 → 下切 → 特征提取
├── Water/                              新增。消费 Hydrology，不被它依赖
│   ├── PlanetWaterConfig.h             水体配置结构体（纯数据）
│   ├── PlanetWaterSurfaceBuilder.h/.cpp 水文场 → 三个 FDynamicMesh3
│   ├── PlanetWaterMaterialBuilder.h/.cpp 建 /Game/Code/Planet/Materials/M_PlanetWater
│   └── PlanetWaterComponent.h/.cpp     挂在行星上的水面组件
├── Terrain/                            改动
│   ├── VoxelPlanetTerrainModule.h/.cpp 新增 GetRawOffset / GetWaterConfig 两个虚函数
│   ├── VoxelPlanetNoiseTerrain.h/.cpp  ComputePlanetOffset → ComputeRawPlanetOffset；下切；配色
│   ├── PlanetBiomeCubemap.h/.cpp       烘焙时把水文场传进配色
│   ├── VoxelPlanetGenerator.h/.cpp     水文章缓存 + 塞进 Context
│   └── VoxelPlanetActor.cpp            RebuildPlanet 里驱动烘焙与水面重建
└── Tests/                              改动 + 新增 5 个 *Tests.cpp
```

**依赖方向（不能反向）**：`Hydrology` 不认识 `Terrain`；`Terrain` 持有 `TSharedPtr<const FPlanetHydrologyField>`；`Water` 依赖两者。`PlanetBiomeCubemap` 之所以要看到水文场，是因为配色要按水深分色 —— 它属于 `Terrain`，同层，不违反方向。

---

### Task 1: 去掉海平面削平，位移改叫 `ComputeRawPlanetOffset`

现在 `ComputePlanetOffset` 末尾是 `FMath::Max(Noise, Params.SeaLevel)` —— 低于海平面的地形被削成一块实心平面。删掉它，输出域从 `[SeaLevel, 1]` 变成 `[-1, 1]`，`SeaLevel` 从此**不参与地形形状**。

**Files:**
- Modify: `Source/StellarEraPlanet/Src/Terrain/VoxelPlanetNoiseTerrain.h:54-55`（声明）、`:218-225`（`MinSurfaceRadius` 注释）
- Modify: `Source/StellarEraPlanet/Src/Terrain/VoxelPlanetNoiseTerrain.cpp:40-68`（函数体）、`:149-155`（实例构造）、`:209`（GetValueImpl）、`:234`（配色）、`:353-354`（区间）
- Modify: `Source/StellarEraPlanet/Src/Terrain/PlanetBiomeCubemap.cpp:115`
- Modify: `Source/StellarEraPlanet/Src/Tests/PlanetBiomeCubemapTests.cpp:169`
- Modify: `Source/StellarEraPlanet/Src/Tests/PlanetTerrainEarlyOutTests.cpp:91-142`
- Create: `Source/StellarEraPlanet/Src/Tests/PlanetTerrainRawOffsetTests.cpp`

**Interfaces:**
- Produces: `float ComputeRawPlanetOffset(const FPlanetNoiseParams& Params, const FVector& UnitDir);` —— 输出严格落在 `[-1, 1]`，且**与 `Params.SeaLevel` 无关**。Task 5 与 `PlanetBiomeCubemap` 都调用它。

- [ ] **Step 1: 写失败的测试**

新文件 `Source/StellarEraPlanet/Src/Tests/PlanetTerrainRawOffsetTests.cpp`（UTF-8 with BOM）：

```cpp
// 「海平面不再削平地形」的回归测试。
//
// 旧的 ComputePlanetOffset 末尾有一道 FMath::Max(Noise, SeaLevel)：低于海平面的
// 地形被削成一块实心平面。水体的前提是地形在海底继续起伏，所以那道 Max 被删掉，
// 函数改名为 ComputeRawPlanetOffset，输出域从 [SeaLevel, 1] 变成 [-1, 1]。
//
// 这里**不**断言「一定存在低于海平面的方向」—— 那取决于噪声分布，是数据性质
// 不是契约。钉住的是契约本身：输出范围，以及 SeaLevel 再也不能改变形状。

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "VoxelPlanetNoiseTerrain.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraTerrainRawOffsetTest,
	"StellarEra.Planet.Terrain.RawOffsetRange",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraTerrainRawOffsetTest::RunTest(const FString& Parameters)
{
	FPlanetNoiseParams Params;
	Params.SeedOffset = FVector(137.5, -42.25, 911.75);

	// 均匀铺满球面的方向集：斐波那契球，避免极点堆积也避免规则网格的对称性。
	constexpr int32 NumDirections = 4096;
	TArray<FVector> Directions;
	Directions.Reserve(NumDirections);
	const double GoldenAngle = PI * (3.0 - FMath::Sqrt(5.0));
	for (int32 Index = 0; Index < NumDirections; ++Index)
	{
		const double Z = 1.0 - 2.0 * (double(Index) + 0.5) / double(NumDirections);
		const double R = FMath::Sqrt(FMath::Max(1.0 - Z * Z, 0.0));
		const double Theta = GoldenAngle * double(Index);
		Directions.Add(FVector(R * FMath::Cos(Theta), R * FMath::Sin(Theta), Z));
	}

	// --- 契约 1：输出落在 [-1, 1] ---
	double MinOffset = TNumericLimits<double>::Max();
	double MaxOffset = -TNumericLimits<double>::Max();
	for (const FVector& Dir : Directions)
	{
		const double Offset = double(ComputeRawPlanetOffset(Params, Dir));
		MinOffset = FMath::Min(MinOffset, Offset);
		MaxOffset = FMath::Max(MaxOffset, Offset);
	}
	TestTrue(FString::Printf(TEXT("位移下界 %.4f 必须 >= -1"), MinOffset), MinOffset >= -1.0 - 1e-4);
	TestTrue(FString::Printf(TEXT("位移上界 %.4f 必须 <= 1"), MaxOffset), MaxOffset <= 1.0 + 1e-4);

	// --- 契约 2：SeaLevel 不参与形状 ---
	// 旧实现下，把 SeaLevel 设成 0.9 会把整颗星削成 0.9 的平面，两个参数算出的
	// 值会大面积不同。新实现必须逐点完全相等。
	FPlanetNoiseParams HighSea = Params;
	HighSea.SeaLevel = 0.9f;
	int32 NumMismatches = 0;
	for (const FVector& Dir : Directions)
	{
		if (ComputeRawPlanetOffset(Params, Dir) != ComputeRawPlanetOffset(HighSea, Dir))
		{
			++NumMismatches;
		}
	}
	TestEqual(TEXT("SeaLevel 改变形状的采样点数（必须为 0）"), NumMismatches, 0);

	return true;
}

#endif
```

- [ ] **Step 2: 构建，确认它编译失败**

Run: Global Constraints 里的 UBT 命令。
Expected: 编译错误 `ComputeRawPlanetOffset` 未声明（函数还不存在）。

- [ ] **Step 3: 改实现**

`VoxelPlanetNoiseTerrain.h`，把第 54-55 行的声明换成：

```cpp
/**
 * 归一化位移，输出必然落在 [-1, 1]。模块与实例共用的唯一实现。
 *
 * **海平面不参与形状**：低于海平面的地形照常起伏，是海底而不是一块平地。
 * 海平面只在两处出现：水文烘焙的种子判据（哪里是海洋），与水面几何的高度。
 */
float ComputeRawPlanetOffset(const FPlanetNoiseParams& Params, const FVector& UnitDir);
```

`VoxelPlanetNoiseTerrain.cpp`，函数体去掉末尾那道 Max：

```cpp
float ComputeRawPlanetOffset(const FPlanetNoiseParams& Params, const FVector& UnitDir)
{
	// 采在单位方向上而不是 UV 上：球面因此天然无缝，也没有极点奇点。
	const FVector SamplePos = UnitDir * Params.Frequency + Params.SeedOffset;

	float Sum = 0.f;
	float LayerAmplitude = 1.f;
	float AmplitudeSum = 0.f;
	float LayerFrequency = 1.f;

	for (int32 Index = 0; Index < Params.Octaves; ++Index)
	{
		Sum += FMath::PerlinNoise3D(SamplePos * LayerFrequency) * LayerAmplitude;
		AmplitudeSum += LayerAmplitude;
		LayerAmplitude *= Params.Gain;
		LayerFrequency *= Params.Lacunarity;
	}

	// 除以权重和，把结果夹回 [-1, 1]。
	//
	// 这一步是**区间正确性的全部依据**：每层 Perlin 的输出都在 [-1, 1]，权重和
	// 恰为 AmplitudeSum，所以归一化后的值必然在范围内。实例的区间查询直接把
	// ±1 当作位移包络用，不再复核 —— 改这里必须同步改那里。
	//
	// 旧版本末尾还有一道 FMath::Max(Noise, SeaLevel)（海平面削平）。删掉它是
	// 水体功能的前提：海平面以下必须是真实起伏的海底，否则水面之下一片平。
	return (AmplitudeSum > 0.f) ? Sum / AmplitudeSum : 0.f;
}
```

把文件里其余 `ComputePlanetOffset` 全部改成 `ComputeRawPlanetOffset`（`:124`、`:209`、`:234`），`PlanetBiomeCubemap.cpp:115` 同理。

实例构造的区间下界（`:149-155`）：

```cpp
	// 与 GetValueRangeImpl 完全相同的包络，只是在这里一次算好。
	, MinSurfaceRadius(double(Radius) + double(Params.Amplitude) * MinNormalizedOffset)
	, MaxSurfaceRadius(double(Radius) + double(Params.Amplitude) * MaxNormalizedOffset
		+ (Context.Geology.IsValid() ? double(Context.Geology->GetMaxGeometryDelta()) : 0.0))
```

在 .cpp 的匿名 namespace 里、`PlanetCenterEpsilon` 旁边加两个常量（**只有一个定义处**，实例构造与 `GetValueRangeImpl` 都用它们）：

```cpp
	// ComputeRawPlanetOffset 的解析输出包络。写成常量而不是在两处手抄 ±1：
	// 这个值错了，地表会静默出洞（估小）或整颗星卡死（估大）。
	constexpr double MinNormalizedOffset = -1.0;
	constexpr double MaxNormalizedOffset = 1.0;
```

`GetValueRangeImpl`（`:353-354`）改成：

```cpp
	// 位移的确定包络，由 ComputeRawPlanetOffset 的归一化直接给出（见上）。
	const double MaxOffset = MaxNormalizedOffset;
	const double MinOffset = MinNormalizedOffset;
```

同步改 `.h` 里 `MinSurfaceRadius` 的成员注释（`:218-225`）：把「下界 = Radius + Amplitude × SeaLevel」改成「下界 = Radius + Amplitude × (−1)」，并说明「SeaLevel 已经不参与距离包络」。

- [ ] **Step 4: 改被这次语义变更波及的既有测试**

`PlanetTerrainEarlyOutTests.cpp:91-142`：带底不再是 `Radius + Amplitude × SeaLevel`。

```cpp
	// --- 实体早退：低于最低可能地表（Radius + Amplitude × (−1)）5 体素以下 ---
	// 默认 Amplitude 8，最低地表在 Radius - 8，所以 -100 处必然走早退。
	// 真值此时必然 <= -1，存进 FVoxelValue 就是 Full()。
```

```cpp
	// --- 早退带必须真的窄 ---
	// 只有 [Radius - Amplitude - 2 × DensityScale, Radius + Amplitude + 2 × DensityScale]
	// 这条带需要算噪声与地质场，其余全部早退。
	// 默认 Amplitude 8、无地质 → 带底 = Radius - 8，带顶 = Radius + 8。
	const FPlanetNoiseParams Defaults;
	const double BandBottom = double(Radius) - double(Defaults.Amplitude);
	const double BandTop = double(Radius) + double(Defaults.Amplitude);
	TestTrue(FString::Printf(TEXT("带宽 %.1f 体素必须不超过 32"), BandTop - BandBottom),
		BandTop - BandBottom <= 32.0);
```

`PlanetBiomeCubemapTests.cpp:169` 只改函数名。**其余断言不要动** —— 若因此变红，说明改动影响了本不该影响的东西，先查原因再改断言。

- [ ] **Step 5: 构建并跑测试**

Run: UBT 构建 → `Automation RunTests StellarEra.Planet`
Expected: 全绿。特别确认 `StellarEra.Planet.Terrain.EarlyOut`、`StellarEra.Planet.Terrain.RawOffsetRange`、`StellarEra.Planet.BiomeCubemap.*`。

- [ ] **Step 6: 提交**

```bash
git -C F:/workspace/ue/StellarEra add Source/StellarEraPlanet/Src/Terrain Source/StellarEraPlanet/Src/Tests
git -C F:/workspace/ue/StellarEra commit -m "refactor(地形): 位移改叫 ComputeRawPlanetOffset，去掉海平面削平

海平面从此只定义水面高度，不再参与地形形状：海平面以下是真实起伏的海底。
区间包络下界由 SeaLevel 改为 -1，并抽成 MinNormalizedOffset 单一常量。" \
  -- Source/StellarEraPlanet/Src/Terrain Source/StellarEraPlanet/Src/Tests
```

---

### Task 2: cubemap 栅格约定 + 只读水文场

水文场是一张**六面 cubemap 栅格**，每面 `(N+2)²` 个格，四周各留一圈「幽灵格」——
幽灵格复刻相邻面边缘的值。这样双线性采样永远不需要判断「是否跨面」。

格与 UV 的换算**只有一个定义**（`PlanetHydrologyGrid`），烘焙、采样、水面几何三处共用：

```
格 i 的中心 → UV：U = (i - 0.5) / N      // i ∈ [0, N+1]
UV → 连续格坐标：g = U * N + 0.5          // U ∈ [0,1] ⇒ g ∈ [0.5, N+0.5]
双线性取 X0 = floor(g) ∈ [0, N]，X1 = X0 + 1 ∈ [1, N+1]
```

`(i - 0.5)/N` 这个偏移是刻意的：它与现有 `PlanetBiomeCubemap::BakeFaceColors` 的
像素中心约定逐字相同，且让幽灵格 `i = 0` / `i = N+1` 恰好是**向外一格的延拓** ——
`FaceUVToDirection` 不夹 U/V，直接支持外插。上面那三行式子保证双线性**永远不越界**，
不需要任何判断分支。

**Files:**
- Create: `Source/StellarEraPlanet/Src/Hydrology/PlanetHydrologyTypes.h`
- Create: `Source/StellarEraPlanet/Src/Hydrology/PlanetHydrologyGrid.h`
- Create: `Source/StellarEraPlanet/Src/Hydrology/PlanetHydrologyField.h`
- Create: `Source/StellarEraPlanet/Src/Hydrology/PlanetHydrologyField.cpp`
- Create: `Source/StellarEraPlanet/Src/Tests/PlanetHydrologyFieldTests.cpp`

**Interfaces:**
- Produces:
  - `FPlanetHydrologyParams`（字段见下）、`FPlanetLakeBasin`、`FPlanetRiverPoint`、`FPlanetRiverPolyline`
  - `PlanetHydrologyGrid::{Padding, FaceCount, PaddedSize, CellCount, CellToU, UToGrid, CellIndex, DecodeCell, CellDirection, DirectionToCell}`
  - `FPlanetHydrologyField::Create(...)` 与 `FPlanetHydrologyField::{IsValid, Sample, GetSeaLevelRadius, GetMaxCarveDepth, GetResolution, GetLakes, GetRivers, GetWaterLevelGrid}`
- Consumes: `PlanetBiomeCubemap::FaceUVToDirection` / `DirectionToFaceUV`（已存在）

- [ ] **Step 1: 写三个头文件**

`Src/Hydrology/PlanetHydrologyTypes.h`：

```cpp
// 水文域的纯数据：参数、湖泊、河流折线。没有 UObject，可跨线程，可被测试直接构造。

#pragma once

#include "CoreMinimal.h"

/**
 * 水文烘焙的参数快照。
 *
 * 由地形模块的 GetWaterConfig 打包（见 VoxelPlanetNoiseTerrain），烘焙层不认识
 * 任何地形子类 —— 它只接受「高度回调 + 这份参数 + 半径」。
 */
struct FPlanetHydrologyParams
{
	/** 海平面（归一化，与地形模块的 SeaLevel 同源）。只在种子判据里用到。 */
	float SeaLevel = -0.35f;

	/** 归一化位移 → 体素的换算系数（= 地形模块的 Amplitude）。 */
	float Amplitude = 8.f;

	/** 汇流量阈值（格）。越大河网越稀。 */
	float MinRiverFlow = 24.f;

	/** 河道最大下切（体素）。 */
	float MaxCarveDepth = 4.f;

	/** 河宽系数。 */
	float RiverWidthScale = 1.f;

	/** 每面格数。512 是默认；256 预览，1024 更细。 */
	int32 Resolution = 512;

	/**
	 * Priority-Flood 的 ε（体素）。
	 *
	 * 填洼时每往低处溢一层就把水位抬高 ε，湖面因此不是绝对平的一整片，
	 * 而是一条确定的、单调下降的「排水阶梯」—— 平地也能解出唯一流向。
	 *
	 * 取值必须大于 float32 在行星半径量级上的间距（半径 20000 体素时约 2e-3），
	 * 否则 ε 会被浮点吃掉。0.01 在半径 ≤ 65000 体素时安全。
	 */
	float Epsilon = 0.01f;
};

/** 一片湖泊的水面。 */
struct FPlanetLakeBasin
{
	/** 湖心方向（单位向量）。 */
	FVector CentroidDir = FVector::UpVector;

	/** 水面半径（体素，含 Radius）。 */
	float WaterLevelRadius = 0.f;

	/** 角半径（弧度）：湖面覆盖的球冠半角，已含 1 格余量。 */
	float AngularRadius = 0.f;
};

/** 河流折线上的一个采样点。 */
struct FPlanetRiverPoint
{
	/** 中轴方向（单位向量）。 */
	FVector Dir = FVector::UpVector;

	/** 该点的水面半径（体素）。沿折线单调不增。 */
	float WaterLevelRadius = 0.f;

	/** 河宽（体素）。 */
	float WidthVoxels = 1.f;
};

/** 一条从源头到汇入点的河流。 */
struct FPlanetRiverPolyline
{
	TArray<FPlanetRiverPoint> Points;
};
```

`Src/Hydrology/PlanetHydrologyGrid.h`：

```cpp
// cubemap 栅格的索引约定。烘焙、采样、水面几何三处共用同一份 —— 抄一份就必然会
// 有一处对不上，而错位一格表现为「水面比地面高/低一格」，很难定位。

#pragma once

#include "CoreMinimal.h"
#include "PlanetBiomeCubemap.h"

namespace PlanetHydrologyGrid
{
	/** 每面四周各留一圈幽灵格。 */
	constexpr int32 Padding = 1;

	constexpr int32 FaceCount = PlanetBiomeCubemap::NumFaces;

	FORCEINLINE int32 PaddedSize(int32 Resolution) { return Resolution + 2 * Padding; }

	FORCEINLINE int32 CellCount(int32 Resolution)
	{
		const int32 Size = PaddedSize(Resolution);
		return FaceCount * Size * Size;
	}

	/**
	 * 格 → 归一化 UV。**只有一个定义**，烘焙与采样都从这里取。
	 *
	 * U = (Index - 0.5) / N：Index = 0 落在面外半格处，Index = N+1 落在另一侧面外
	 * 半格处 —— 幽灵格因此是向外的线性延拓，可以直接喂给 FaceUVToDirection
	 * （它不夹 U/V）。
	 */
	FORCEINLINE float CellToU(int32 Index, int32 Resolution)
	{
		return (float(Index) - 0.5f) / float(Resolution);
	}

	/** UV → 连续格坐标：U ∈ [0,1] ⇒ 返回值 ∈ [0.5, N+0.5]。 */
	FORCEINLINE float UToGrid(float U, int32 Resolution)
	{
		return U * float(Resolution) + 0.5f;
	}

	FORCEINLINE int32 CellIndex(int32 Face, int32 X, int32 Y, int32 Resolution)
	{
		const int32 Size = PaddedSize(Resolution);
		return (Face * Size + Y) * Size + X;
	}

	FORCEINLINE void DecodeCell(int32 Cell, int32 Resolution, int32& OutFace, int32& OutX, int32& OutY)
	{
		const int32 Size = PaddedSize(Resolution);
		OutFace = Cell / (Size * Size);
		const int32 Remainder = Cell % (Size * Size);
		OutY = Remainder / Size;
		OutX = Remainder % Size;
	}

	/** 格 → 单位方向。幽灵格会外插到面外，方向仍然有意义（只是不精确归一化到格心）。 */
	FORCEINLINE FVector CellDirection(int32 Face, int32 X, int32 Y, int32 Resolution)
	{
		return PlanetBiomeCubemap::FaceUVToDirection(
			Face, CellToU(X, Resolution), CellToU(Y, Resolution));
	}

	/**
	 * 方向 → 最近的格。
	 *
	 * 幽灵格的方向外插之后，最靠近的格有可能落在 [0, N+1] 之外（差 1 格），
	 * 所以夹到边界而不是断言。夹出来的最坏结果是「邻接到自己」—— 调用方必须丢弃
	 * 自环（见 PlanetHydrologyBake 的邻接表构造）。
	 */
	FORCEINLINE int32 DirectionToCell(const FVector& UnitDir, int32 Resolution)
	{
		int32 Face = 0;
		float U = 0.f;
		float V = 0.f;
		PlanetBiomeCubemap::DirectionToFaceUV(UnitDir, Face, U, V);
		const int32 X = FMath::Clamp(FMath::RoundToInt(UToGrid(U, Resolution)), 0, Resolution + 1);
		const int32 Y = FMath::Clamp(FMath::RoundToInt(UToGrid(V, Resolution)), 0, Resolution + 1);
		return CellIndex(Face, X, Y, Resolution);
	}
}
```

`Src/Hydrology/PlanetHydrologyField.h`：

```cpp
// 烘焙结果的只读快照：每格的水位 / 下切深度 / 河流强度，外加湖泊与河流特征。

#pragma once

#include "CoreMinimal.h"
#include "PlanetHydrologyTypes.h"

/**
 * 水文场。
 *
 * **单位一律是体素半径**（到球心的距离，体素）：生成器在体素空间直接比较，
 * 水面几何乘上 VoxelSize 就是本地空间厘米。运行时不需要再乘 Radius。
 *
 * 三个栅格同布局（见 PlanetHydrologyGrid），长度为 CellCount(Resolution)。
 * 场一旦构造就不再改变，可以安全地在后台体素线程上共享 —— 与 FPlanetGeologyField
 * 同一个理由。
 */
class STELLARERAPLANET_API FPlanetHydrologyField
{
public:
	/** 一个方向的四邻域插值结果。 */
	struct FSample
	{
		/** 水面半径（体素）。陆地上等于地形高度（= 没有水）。 */
		float WaterLevelRadius = 0.f;

		/** 河道下切深度（体素）。非河道为 0。 */
		float CarveDepth = 0.f;

		/** 河流强度 0..1（由汇流量归一化）。非河道为 0。 */
		float RiverStrength = 0.f;
	};

	/** 唯一构造点：烘焙层填好三个栅格与特征列表后调用。 */
	static TSharedRef<FPlanetHydrologyField> Create(
		int32 InResolution,
		float InSeaLevelRadius,
		float InMaxCarveDepth,
		TArray<float>&& InWaterLevel,
		TArray<float>&& InCarveDepth,
		TArray<float>&& InRiverStrength,
		TArray<FPlanetLakeBasin>&& InLakes,
		TArray<FPlanetRiverPolyline>&& InRivers);

	bool IsValid() const;

	/** cubemap 双线性采样。场无效时返回全零（调用方拿到的水位 0 意味着「没有水」）。 */
	FSample Sample(const FVector& UnitDir) const;

	/** 海面半径（体素）= Radius + Amplitude × SeaLevel。 */
	float GetSeaLevelRadius() const { return SeaLevelRadius; }

	/** 给区间查询用的下切包络：地表半径要再减去这么多。 */
	float GetMaxCarveDepth() const { return MaxCarveDepth; }

	int32 GetResolution() const { return Resolution; }

	const TArray<FPlanetLakeBasin>& GetLakes() const { return Lakes; }
	const TArray<FPlanetRiverPolyline>& GetRivers() const { return Rivers; }

	/** 给水面几何构建器用：它要按格遍历海面，而不是按方向采样。 */
	const TArray<float>& GetWaterLevelGrid() const { return WaterLevel; }

private:
	int32 Resolution = 0;
	float SeaLevelRadius = 0.f;
	float MaxCarveDepth = 0.f;
	TArray<float> WaterLevel;
	TArray<float> CarveDepth;
	TArray<float> RiverStrength;
	TArray<FPlanetLakeBasin> Lakes;
	TArray<FPlanetRiverPolyline> Rivers;
};
```

- [ ] **Step 2: 写失败的测试**

`Src/Tests/PlanetHydrologyFieldTests.cpp`：

```cpp
// 水文场栅格约定与双线性采样的回归测试。
//
// 钉两件事：
//   1) 格 ↔ 方向 的往返：CellDirection 得到的格必须还是自己（否则烘焙写到一个格、
//      采样读另一个格，水位整体错位）；
//   2) 跨面连续：同一个方向在相邻两个面上采样，结果必须一致 —— cubemap 接缝
//      出错时最先在这里显形，而不是在 PIE 里。

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "PlanetHydrologyField.h"
#include "PlanetHydrologyGrid.h"
#include "PlanetBiomeCubemap.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	/** 造一个按方向解析求值的水文场：三格栅格都填同一个函数。 */
	TSharedRef<FPlanetHydrologyField> MakeAnalyticField(
		int32 Resolution, TFunctionRef<float(const FVector&)> WaterFn)
	{
		const int32 Count = PlanetHydrologyGrid::CellCount(Resolution);
		TArray<float> Water;
		TArray<float> Carve;
		TArray<float> Strength;
		Water.SetNumUninitialized(Count);
		Carve.Init(0.f, Count);
		Strength.Init(0.f, Count);

		for (int32 Cell = 0; Cell < Count; ++Cell)
		{
			int32 Face = 0, X = 0, Y = 0;
			PlanetHydrologyGrid::DecodeCell(Cell, Resolution, Face, X, Y);
			Water[Cell] = WaterFn(PlanetHydrologyGrid::CellDirection(Face, X, Y, Resolution));
		}

		return FPlanetHydrologyField::Create(
			Resolution, 1000.f, 4.f,
			MoveTemp(Water), MoveTemp(Carve), MoveTemp(Strength),
			TArray<FPlanetLakeBasin>(), TArray<FPlanetRiverPolyline>());
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraHydrologyGridTest,
	"StellarEra.Planet.Hydrology.GridIndexing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraHydrologyGridTest::RunTest(const FString& Parameters)
{
	constexpr int32 Resolution = 32;

	// --- 往返：格 → 方向 → 格 ---
	// 幽灵格（Padding 区）的方向是外插的，允许落到邻面，因此只检查内部格。
	int32 NumMismatches = 0;
	for (int32 Face = 0; Face < PlanetHydrologyGrid::FaceCount; ++Face)
	{
		for (int32 Y = 1; Y <= Resolution; ++Y)
		{
			for (int32 X = 1; X <= Resolution; ++X)
			{
				const FVector Dir = PlanetHydrologyGrid::CellDirection(Face, X, Y, Resolution);
				if (PlanetHydrologyGrid::DirectionToCell(Dir, Resolution)
					!= PlanetHydrologyGrid::CellIndex(Face, X, Y, Resolution))
				{
					++NumMismatches;
				}
			}
		}
	}
	TestEqual(TEXT("内部格的往返误差数（必须为 0）"), NumMismatches, 0);

	// --- UToGrid / CellToU 互逆 ---
	double MaxRoundTripError = 0.0;
	for (int32 Index = 0; Index <= Resolution + 1; ++Index)
	{
		const float U = PlanetHydrologyGrid::CellToU(Index, Resolution);
		const float Grid = PlanetHydrologyGrid::UToGrid(U, Resolution);
		MaxRoundTripError = FMath::Max(MaxRoundTripError, FMath::Abs(double(Grid) - double(Index)));
	}
	TestTrue(FString::Printf(TEXT("格 ↔ UV 往返误差 %.6f 必须 < 1e-3"), MaxRoundTripError),
		MaxRoundTripError < 1e-3);

	// --- 双线性取 X0/X1 永远不越界 ---
	// U ∈ [0,1] ⇒ g ∈ [0.5, N+0.5] ⇒ X0 ∈ [0, N]、X1 ∈ [1, N+1]。
	// 这是「采样永远不需要分支」的全部依据。
	bool bInBounds = true;
	for (int32 Step = 0; Step <= 256; ++Step)
	{
		const float U = float(Step) / 256.f;
		const float Grid = PlanetHydrologyGrid::UToGrid(U, Resolution);
		const int32 X0 = FMath::FloorToInt(Grid);
		bInBounds &= (X0 >= 0) && (X0 <= Resolution) && (X0 + 1 <= Resolution + 1);
	}
	TestTrue(TEXT("双线性索引必须永远落在 [0, N+1] 内"), bInBounds);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraHydrologyFieldSampleTest,
	"StellarEra.Planet.Hydrology.FieldSample",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraHydrologyFieldSampleTest::RunTest(const FString& Parameters)
{
	constexpr int32 Resolution = 64;

	// --- 常数场：采样必须处处等值（含格点与非格点） ---
	const TSharedRef<FPlanetHydrologyField> Constant =
		MakeAnalyticField(Resolution, [](const FVector&) { return 42.f; });

	double MaxConstantError = 0.0;
	for (int32 Step = 0; Step < 512; ++Step)
	{
		const FVector Dir = FVector(
			FMath::Sin(double(Step) * 0.017),
			FMath::Cos(double(Step) * 0.031),
			FMath::Sin(double(Step) * 0.007 + 0.3)).GetSafeNormal();
		MaxConstantError = FMath::Max(
			MaxConstantError, FMath::Abs(double(Constant->Sample(Dir).WaterLevelRadius) - 42.0));
	}
	TestTrue(FString::Printf(TEXT("常数场采样误差 %.6f 必须 < 1e-3"), MaxConstantError),
		MaxConstantError < 1e-3);

	// --- 线性场：跨面必须连续 ---
	// 场值 = 1000 + 100 × Direction.Z。跨面取相邻两个方向，值之差必须与几何
	// 间距同阶；接缝映射写错时会看到一整格的跳变（>= 100 × 一格的角距）。
	const TSharedRef<FPlanetHydrologyField> Linear = MakeAnalyticField(
		Resolution, [](const FVector& Dir) { return float(1000.0 + 100.0 * Dir.Z); });

	// 沿六个面的交界扫一圈：取面边界上的方向，向两侧各偏一点点。
	double MaxSeamJump = 0.0;
	for (int32 Face = 0; Face < PlanetBiomeCubemap::NumFaces; ++Face)
	{
		for (int32 Along = 0; Along <= 16; ++Along)
		{
			const float T = float(Along) / 16.f;
			const FVector EdgeDir = PlanetBiomeCubemap::FaceUVToDirection(Face, 1.f, T);
			const FVector Inside = PlanetBiomeCubemap::FaceUVToDirection(Face, 0.999f, T);
			const FVector Outside = (EdgeDir * 2.0 - Inside).GetSafeNormal(); // 越界镜像
			MaxSeamJump = FMath::Max(MaxSeamJump, FMath::Abs(
				double(Linear->Sample(Inside).WaterLevelRadius)
				- double(Linear->Sample(Outside).WaterLevelRadius)));
		}
	}
	// 两个方向夹角 ≈ 一格的角距（π/2 / N ≈ 0.025 rad），线性场值差 ≤ 100 × 0.025 × 2。
	TestTrue(FString::Printf(TEXT("跨面采样跳变 %.3f 必须 <= 5（一格量级）"), MaxSeamJump),
		MaxSeamJump <= 5.0);

	return true;
}

#endif
```

- [ ] **Step 3: 实现 `PlanetHydrologyField.cpp`**

```cpp
// 水文场的构造与采样。

#include "PlanetHydrologyField.h"

#include "PlanetBiomeCubemap.h"
#include "PlanetHydrologyGrid.h"

TSharedRef<FPlanetHydrologyField> FPlanetHydrologyField::Create(
	int32 InResolution,
	float InSeaLevelRadius,
	float InMaxCarveDepth,
	TArray<float>&& InWaterLevel,
	TArray<float>&& InCarveDepth,
	TArray<float>&& InRiverStrength,
	TArray<FPlanetLakeBasin>&& InLakes,
	TArray<FPlanetRiverPolyline>&& InRivers)
{
	TSharedRef<FPlanetHydrologyField> Field = MakeShared<FPlanetHydrologyField>();
	Field->Resolution = FMath::Max(InResolution, 0);
	Field->SeaLevelRadius = InSeaLevelRadius;
	Field->MaxCarveDepth = InMaxCarveDepth;
	Field->WaterLevel = MoveTemp(InWaterLevel);
	Field->CarveDepth = MoveTemp(InCarveDepth);
	Field->RiverStrength = MoveTemp(InRiverStrength);
	Field->Lakes = MoveTemp(InLakes);
	Field->Rivers = MoveTemp(InRivers);
	return Field;
}

bool FPlanetHydrologyField::IsValid() const
{
	return Resolution > 0 && WaterLevel.Num() == PlanetHydrologyGrid::CellCount(Resolution);
}

FPlanetHydrologyField::FSample FPlanetHydrologyField::Sample(const FVector& UnitDir) const
{
	FSample Out;
	if (!IsValid())
	{
		return Out;
	}

	int32 Face = 0;
	float U = 0.f;
	float V = 0.f;
	PlanetBiomeCubemap::DirectionToFaceUV(UnitDir, Face, U, V);

	// U ∈ [0,1] ⇒ 连续格坐标 ∈ [0.5, N+0.5]，于是 X0 ∈ [0, N]、X1 = X0+1 ≤ N+1：
	// 双线性**永远**落在栅格内，不需要任何边界分支（见 PlanetHydrologyGrid 的推导）。
	const float GridX = PlanetHydrologyGrid::UToGrid(U, Resolution);
	const float GridY = PlanetHydrologyGrid::UToGrid(V, Resolution);
	const int32 X0 = FMath::Clamp(FMath::FloorToInt(GridX), 0, Resolution);
	const int32 Y0 = FMath::Clamp(FMath::FloorToInt(GridY), 0, Resolution);
	const int32 X1 = X0 + 1;
	const int32 Y1 = Y0 + 1;
	const float Tx = FMath::Clamp(GridX - float(X0), 0.f, 1.f);
	const float Ty = FMath::Clamp(GridY - float(Y0), 0.f, 1.f);

	const int32 Size = PlanetHydrologyGrid::PaddedSize(Resolution);
	const int32 Base = Face * Size * Size;
	const auto Bilinear = [&](const TArray<float>& In) -> float
	{
		const float V00 = In[Base + Y0 * Size + X0];
		const float V10 = In[Base + Y0 * Size + X1];
		const float V01 = In[Base + Y1 * Size + X0];
		const float V11 = In[Base + Y1 * Size + X1];
		return FMath::Lerp(FMath::Lerp(V00, V10, Tx), FMath::Lerp(V01, V11, Tx), Ty);
	};

	Out.WaterLevelRadius = Bilinear(WaterLevel);
	Out.CarveDepth = Bilinear(CarveDepth);
	Out.RiverStrength = Bilinear(RiverStrength);
	return Out;
}
```

> 注意 `TArray<float>` 的 `operator[]` 在 `NDEBUG` 之外带范围检查；这里的索引推导已经保证
> 在界内，不要额外夹 `X1`/`Y1` —— 夹了就把「外插」静默变成「贴边」，接缝处的采样会
> 差一整格。

- [ ] **Step 4: 构建并跑测试**

Run: UBT 构建 → `Automation RunTests StellarEra.Planet.Hydrology`
Expected: `GridIndexing` 与 `FieldSample` 两条绿。

若 `跨越采样跳变` 变红：先确认 `CellToU` 的 `-0.5` 偏移没被写成 `+0.5`，再确认
`DirectionToFaceUV` 的分面判据没被改动（它属于已有代码，不该被本任务碰到）。

- [ ] **Step 5: 提交**

```bash
git -C F:/workspace/ue/StellarEra add Source/StellarEraPlanet/Src/Hydrology Source/StellarEraPlanet/Src/Tests/PlanetHydrologyFieldTests.cpp
git -C F:/workspace/ue/StellarEra commit -m "feat(水文): cubemap 栅格约定与只读水文场

每面 (N+2)² 带幽灵格，格心对齐 (i-0.5)/N，双线性采样因此永远不越界、
不需要跨面分支。场里一律存体素半径。" \
  -- Source/StellarEraPlanet/Src/Hydrology Source/StellarEraPlanet/Src/Tests/PlanetHydrologyFieldTests.cpp
```

---

### Task 3: 水文烘焙（填洼 → 汇流 → 下切 → 湖泊/河流）

用 **Priority-Flood + ε**（Barnes 2014）：从「海洋格」当种子、按水位从低到高往外扩，
遇到比自己低的邻居就把它淹到自己这一层再抬 ε。结果每格得到一个 `W ≥ H` 的水位，
湖泊就是 `W > H` 的连通块，而 ε 让湖面平地也有确定的流向。

**关键取舍（写进代码注释，别省略）**：邻接用 **4 邻接**（不是 D8）。水文场每面 512 格，
1km 半径的行星上一格约 0.6 cm 级（按 `体素 × VoxelSize` 换算），4 邻接的「锯齿」在
这个分辨率下看不出来，而 8 邻接要把跨面邻接表翻一倍。汇流方向因此是 D4。

**Files:**
- Create: `Source/StellarEraPlanet/Src/Hydrology/PlanetHydrologyBake.h`
- Create: `Source/StellarEraPlanet/Src/Hydrology/PlanetHydrologyBake.cpp`
- Create: `Source/StellarEraPlanet/Src/Tests/PlanetHydrologyBakeTests.cpp`

**Interfaces:**
- Consumes: `PlanetHydrologyGrid::*`、`FPlanetHydrologyField::Create`、`PlanetBiomeCubemap::FaceUVToDirection`
- Produces: `TSharedPtr<FPlanetHydrologyField> FPlanetHydrologyBake::Bake(FHeightFn HeightFn, const FPlanetHydrologyParams& Params, double Radius)`，其中 `using FHeightFn = TFunctionRef<float(const FVector& UnitDir)>`（返回**归一化**位移，−1..1）

- [ ] **Step 1: 写失败的测试**

`Src/Tests/PlanetHydrologyBakeTests.cpp`（UTF-8 with BOM）。两条地形都是解析函数，
不依赖噪声种子 —— 断言因此是确定的数，不是「大概对」。

```cpp
// 水文烘焙的回归测试。
//
// 用两条解析地形（碗 + 谷），断言填洼水位、湖泊特征、河流存在性与跨面连续性。
// 地形刻意不用噪声：噪声的分布是数据性质，不是契约，写成断言会随 seed 漂。

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "PlanetHydrologyBake.h"
#include "PlanetBiomeCubemap.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr double TestRadius = 1000.0;

	FPlanetHydrologyParams MakeTestParams()
	{
		FPlanetHydrologyParams Params;
		Params.SeaLevel = -0.3f;
		Params.Amplitude = 8.f;
		Params.MinRiverFlow = 24.f;
		Params.MaxCarveDepth = 4.f;
		Params.Resolution = 128;
		Params.Epsilon = 0.01f;
		return Params;
	}

	/**
	 * 碗：角半径 0.3 的二次凹坑，碗缘 −0.05（高于海面 −0.3），碗外一律 −0.5（全是海）。
	 * 于是碗里必然蓄成一片湖，水位 ≈ 碗缘，且不与海连通。
	 */
	float BowlHeight(const FVector& Dir)
	{
		const float Angle = FMath::Acos(FMath::Clamp(float(Dir.Z), -1.f, 1.f));
		constexpr float BowlAngle = 0.3f;
		if (Angle > BowlAngle)
		{
			return -0.5f;
		}
		const float T = Angle / BowlAngle;
		return -0.5f + 0.45f * T * T;
	}

	/**
	 * 谷：从 +Z（−0.8，海）向 −Z（+0.5，高山）一路升高的斜坡，再叠一个「离 lon = 0
	 * 越远越高」的横向项。横向项让水从两侧**汇聚**到 lon = 0 的谷底 —— 没有它，
	 * 斜坡上的水只会各自沿经线直下，汇流量永远停在 1，长不出河。
	 */
	float ValleyHeight(const FVector& Dir)
	{
		const double SafeZ = FMath::Clamp(double(Dir.Z), -1.0, 1.0);
		const double Angle = FMath::Acos(SafeZ);
		const double Lon = FMath::Atan2(double(Dir.Y), double(Dir.X));
		const double Normalized = -0.8 + 1.3 * (Angle / PI) + 0.4 * FMath::Square(Lon / PI);
		return float(Normalized);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraHydrologyLakeTest,
	"StellarEra.Planet.Hydrology.BakeLake",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraHydrologyLakeTest::RunTest(const FString& Parameters)
{
	const FPlanetHydrologyParams Params = MakeTestParams();
	const TSharedPtr<FPlanetHydrologyField> Field =
		FPlanetHydrologyBake::Bake(&BowlHeight, Params, TestRadius);
	TestTrue(TEXT("烘焙必须返回有效的场"), Field.IsValid() && Field->IsValid());
	if (!Field.IsValid() || !Field->IsValid())
	{
		return false;
	}

	// --- 海面 ---
	const float ExpectedSea = float(TestRadius + double(Params.Amplitude) * double(Params.SeaLevel));
	TestTrue(FString::Printf(TEXT("海面 %.2f 必须是 Radius + Amplitude × SeaLevel = %.2f"),
		Field->GetSeaLevelRadius(), ExpectedSea),
		FMath::Abs(Field->GetSeaLevelRadius() - ExpectedSea) < 0.01f);

	// --- 恰好一片湖，水位 ≈ 碗缘 ---
	const TArray<FPlanetLakeBasin>& Lakes = Field->GetLakes();
	TestEqual(TEXT("湖泊数量"), Lakes.Num(), 1);
	if (Lakes.Num() == 1)
	{
		const float ExpectedLevel = float(TestRadius + double(Params.Amplitude) * -0.05);
		TestTrue(FString::Printf(TEXT("湖面 %.3f 必须 ≈ 碗缘 %.3f（差 <= 0.6 体素）"),
			Lakes[0].WaterLevelRadius, ExpectedLevel),
			FMath::Abs(Lakes[0].WaterLevelRadius - ExpectedLevel) < 0.6f);
		TestTrue(FString::Printf(TEXT("湖心方向必须接近 +Z（实测 Z=%.4f）"), Lakes[0].CentroidDir.Z),
			Lakes[0].CentroidDir.Z > 0.99);
		TestTrue(FString::Printf(TEXT("湖的角半径 %.4f 必须 ≈ 0.3"), Lakes[0].AngularRadius),
			Lakes[0].AngularRadius > 0.29f && Lakes[0].AngularRadius < 0.33f);
		TestTrue(TEXT("湖水必须高于海面（它是湖，不是海的一部分）"),
			Lakes[0].WaterLevelRadius > Field->GetSeaLevelRadius() + 1.f);
	}

	// --- 点采样：碗心是水，碗外是海，岸边是陆地 ---
	const auto SampleAt = [&](const FVector& Dir) { return Field->Sample(Dir); };

	const FVector BowlCenter = FVector::UpVector;
	TestTrue(TEXT("碗心的水位必须高于地形（是水不是地）"),
		SampleAt(BowlCenter).WaterLevelRadius
			> float(TestRadius + double(Params.Amplitude) * double(BowlHeight(BowlCenter))) + 0.1f);

	const FVector OpenOcean = FVector(1.0, 1.0, -1.0).GetSafeNormal();
	TestTrue(TEXT("碗外必须是海面高度"),
		FMath::Abs(SampleAt(OpenOcean).WaterLevelRadius - Field->GetSeaLevelRadius()) < 0.5f);
	TestEqual(TEXT("海里的下切必须为 0"), SampleAt(OpenOcean).CarveDepth, 0.f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraHydrologyRiverTest,
	"StellarEra.Planet.Hydrology.BakeRiver",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraHydrologyRiverTest::RunTest(const FString& Parameters)
{
	const FPlanetHydrologyParams Params = MakeTestParams();
	const TSharedPtr<FPlanetHydrologyField> Field =
		FPlanetHydrologyBake::Bake(&ValleyHeight, Params, TestRadius);
	TestTrue(TEXT("烘焙必须返回有效的场"), Field.IsValid() && Field->IsValid());
	if (!Field.IsValid() || !Field->IsValid())
	{
		return false;
	}

	// --- 河网 ---
	const TArray<FPlanetRiverPolyline>& Rivers = Field->GetRivers();
	TestTrue(FString::Printf(TEXT("谷地必须至少长出一条河（实测 %d 条）"), Rivers.Num()),
		Rivers.Num() >= 1);

	int32 LongestPoints = 0;
	for (const FPlanetRiverPolyline& River : Rivers)
	{
		LongestPoints = FMath::Max(LongestPoints, River.Points.Num());

		// 沿折线水位单调不增（水只往低处流）。
		bool bMonotone = true;
		for (int32 Index = 1; Index < River.Points.Num(); ++Index)
		{
			bMonotone &= River.Points[Index].WaterLevelRadius
				<= River.Points[Index - 1].WaterLevelRadius + 1e-3f;
		}
		TestTrue(TEXT("河流折线的水位必须单调不增"), bMonotone);

		// 所有的点都必须有正河宽与有限的半径。
		bool bSane = true;
		for (const FPlanetRiverPoint& Point : River.Points)
		{
			bSane &= FMath::IsFinite(Point.WaterLevelRadius)
				&& Point.WidthVoxels > 0.f
				&& FMath::IsFinite(Point.Dir.SizeSquared());
		}
		TestTrue(TEXT("河流折线的点必须是有限值且河宽为正"), bSane);
	}
	TestTrue(FString::Printf(TEXT("最长河流至少 8 个点（实际 %d）"), LongestPoints),
		LongestPoints >= 8);

	// --- 下切：谷底 lon = 0、极角 1.8 rad 的中段必须有河道 ---
	// 方向 = (sin θ, 0, cos θ)，即 lon = 0、离 +Z 极 1.8 rad。
	const FPlanetHydrologyField::FSample Sample =
		Field->Sample(FVector(FMath::Sin(1.8), 0.0, FMath::Cos(1.8)));
	TestTrue(FString::Printf(TEXT("谷底必须有下切（实测 %.3f 体素）"), Sample.CarveDepth),
		Sample.CarveDepth > 0.5f);
	TestTrue(FString::Printf(TEXT("下切 %.3f 不得超过 MaxCarveDepth %.3f"),
		Sample.CarveDepth, Field->GetMaxCarveDepth()),
		Sample.CarveDepth <= Field->GetMaxCarveDepth() + 1e-3f);
	TestTrue(FString::Printf(TEXT("河流强度必须在 (0, 1] 内（实测 %.3f）"), Sample.RiverStrength),
		Sample.RiverStrength > 0.f && Sample.RiverStrength <= 1.f);

	// --- 跨面连续：水面在 cubemap 接缝两侧不得跳变 ---
	// 地形是光滑解析函数，一格的角距约 (π/2)/128 ≈ 0.012 rad，坡度最多 3 体素/rad，
	// 所以两侧的水位差应当在 0.1 体素以内。超过 2 体素就说明接缝映射错了。
	double MaxSeamJump = 0.0;
	for (int32 Face = 0; Face < PlanetBiomeCubemap::NumFaces; ++Face)
	{
		for (int32 Along = 0; Along <= 16; ++Along)
		{
			const float T = float(Along) / 16.f;
			const FVector Edge = PlanetBiomeCubemap::FaceUVToDirection(Face, 1.f, T);
			const FVector Inside = PlanetBiomeCubemap::FaceUVToDirection(Face, 0.99f, T);
			const FVector Outside = (Edge * 2.0 - Inside).GetSafeNormal();
			MaxSeamJump = FMath::Max(MaxSeamJump, FMath::Abs(
				double(Field->Sample(Inside).WaterLevelRadius)
				- double(Field->Sample(Outside).WaterLevelRadius)));
		}
	}
	TestTrue(FString::Printf(TEXT("跨面水位跳变 %.3f 必须 <= 2 体素"), MaxSeamJump),
		MaxSeamJump <= 2.0);

	return true;
}

#endif
```

- [ ] **Step 2: 构建，确认测试编译失败**

Run: UBT 构建。
Expected: `PlanetHydrologyBake.h` 不存在、`FPlanetHydrologyBake` 未声明。

- [ ] **Step 3: 写 `PlanetHydrologyBake.h`**

```cpp
// 水文烘焙：归一化高度场 → 填洼 → 汇流 → 下切 → 湖泊与河流特征。

#pragma once

#include "CoreMinimal.h"
#include "PlanetHydrologyField.h"
#include "PlanetHydrologyTypes.h"

/**
 * 水文烘焙的唯一入口。
 *
 * 刻意做成自由函数而不是 UObject：它只做一次纯计算，输入是回调 + 参数 + 半径，
 * 不认识任何地形子类（不 include Terrain），因此可以在后台线程或测试里直接调用。
 */
namespace FPlanetHydrologyBake
{
	/** 返回**归一化**位移（−1..1，0 = 基准球面）。与地形模块的 GetRawOffset 同源。 */
	using FHeightFn = TFunctionRef<float(const FVector& UnitDir)>;

	/**
	 * @param HeightFn 每格调用一次（并行），必须线程安全且无副作用
	 * @param Radius   基准球面半径（体素）
	 * @return 场；参数非法（Resolution < 4）时返回 nullptr
	 */
	STELLARERAPLANET_API TSharedPtr<FPlanetHydrologyField> Bake(
		FHeightFn HeightFn,
		const FPlanetHydrologyParams& Params,
		double Radius);
}
```

- [ ] **Step 4: 写 `PlanetHydrologyBake.cpp`**

下面按段落给全。**顺序不能调换**：填洼产出的 `PopOrder` 就是汇流要用的降序。

```cpp
// 水文烘焙：Priority-Flood + ε 填洼，4 邻接汇流，河道下切，湖泊与河流特征提取。

#include "PlanetHydrologyBake.h"

#include "Async/ParallelFor.h"
#include "PlanetBiomeCubemap.h"
#include "PlanetHydrologyGrid.h"

namespace
{
	/** 最小堆条目：按 Level 升序弹出。 */
	struct FFloodEntry
	{
		double Level = 0.0;
		int32 Cell = INDEX_NONE;
	};

	/**
	 * 手写二叉最小堆。
	 *
	 * 不用 Algo::HeapPush：那个是**最大堆语义**（比较器为「大于」时先弹最大的），
	 * 这里最容易被写反的就是方向，写在手上 30 行反而更难错，也少一个 API 版本风险。
	 */
	struct FFloodHeap
	{
		TArray<FFloodEntry> Items;

		bool IsEmpty() const { return Items.Num() == 0; }

		void Push(double Level, int32 Cell)
		{
			int32 Index = Items.AddUninitialized();
			while (Index > 0)
			{
				const int32 Parent = (Index - 1) / 2;
				if (Items[Parent].Level <= Level)
				{
					break;
				}
				Items[Index] = Items[Parent];
				Index = Parent;
			}
			Items[Index].Level = Level;
			Items[Index].Cell = Cell;
		}

		FFloodEntry Pop()
		{
			const FFloodEntry Top = Items[0];
			const FFloodEntry Last = Items.Pop(EAllowShrinking::No);
			const int32 Count = Items.Num();
			if (Count > 0)
			{
				int32 Index = 0;
				while (true)
				{
					int32 Child = Index * 2 + 1;
					if (Child >= Count)
					{
						break;
					}
					if (Child + 1 < Count && Items[Child + 1].Level < Items[Child].Level)
					{
						++Child;
					}
					if (Items[Child].Level >= Last.Level)
					{
						break;
					}
					Items[Index] = Items[Child];
					Index = Child;
				}
				Items[Index] = Last;
			}
			return Top;
		}
	};

	/** 格的状态机。0 = 未入队，1 = 已入队，2 = 已访问。 */
	enum class ECellState : uint8
	{
		Unqueued = 0,
		Queued = 1,
		Visited = 2,
	};
}

TSharedPtr<FPlanetHydrologyField> FPlanetHydrologyBake::Bake(
	FHeightFn HeightFn,
	const FPlanetHydrologyParams& Params,
	double Radius)
{
	const int32 Resolution = Params.Resolution;
	if (Resolution < 4)
	{
		return nullptr;
	}

	const int32 CellCount = PlanetHydrologyGrid::CellCount(Resolution);
	const float SeaLevelRadius = float(Radius + double(Params.Amplitude) * double(Params.SeaLevel));
	const float MinFlow = FMath::Max(Params.MinRiverFlow, 1.f);

	// --- 1. 高度栅格（并行；每格一次 HeightFn） ---
	TArray<float> Height;
	Height.SetNumUninitialized(CellCount);
	ParallelFor(CellCount, [&](int32 Cell)
	{
		int32 Face = 0, X = 0, Y = 0;
		PlanetHydrologyGrid::DecodeCell(Cell, Resolution, Face, X, Y);
		const FVector Dir = PlanetHydrologyGrid::CellDirection(Face, X, Y, Resolution);
		Height[Cell] = float(Radius + double(Params.Amplitude) * double(HeightFn(Dir)));
	});

	// --- 2. 4 邻接表 ---
	//
	// 每格 4 个槽（-1 = 空），共 4 × 6 × (N+2)² × 4 B —— N = 512 时约 25 MB，
	// 烘焙结束即释放。
	//
	// 跨面那一半靠「把格坐标外插一格 → 求方向 → 找最近的格」得到。幽灵格让这个
	// 近似**无害**：幽灵格本身也是节点，其值与相邻面的边界格几乎相等，即使最近邻
	// 夹错了 1 格，水位也只是换了一条几乎重合的路传播。
	//
	// 构造完必须**对称化**：填洼只往「未访问的邻居」推，邻接不对称会让某些格永远
	// 不被访问到，表现为湖里出现几个水位恒为 FLT_MAX 的洞。
	static const int32 NeighborOffsets[4][2] = { {-1, 0}, {1, 0}, {0, -1}, {0, 1} };

	TArray<int32> Neighbors;
	Neighbors.Init(INDEX_NONE, CellCount * 4);
	for (int32 Cell = 0; Cell < CellCount; ++Cell)
	{
		int32 Face = 0, X = 0, Y = 0;
		PlanetHydrologyGrid::DecodeCell(Cell, Resolution, Face, X, Y);
		for (int32 Side = 0; Side < 4; ++Side)
		{
			const FVector Dir = PlanetBiomeCubemap::FaceUVToDirection(
				Face,
				PlanetHydrologyGrid::CellToU(X + NeighborOffsets[Side][0], Resolution),
				PlanetHydrologyGrid::CellToU(Y + NeighborOffsets[Side][1], Resolution));
			const int32 Target = PlanetHydrologyGrid::DirectionToCell(Dir, Resolution);
			Neighbors[Cell * 4 + Side] = (Target == Cell) ? INDEX_NONE : Target;
		}
	}
	{
		int32* const Slots = Neighbors.GetData();
		for (int32 Cell = 0; Cell < CellCount; ++Cell)
		{
			for (int32 Side = 0; Side < 4; ++Side)
			{
				const int32 Target = Slots[Cell * 4 + Side];
				if (Target == INDEX_NONE)
				{
					continue;
				}
				int32* TargetSlots = Slots + Target * 4;
				bool bReciprocated = false;
				for (int32 Index = 0; Index < 4; ++Index)
				{
					bReciprocated |= (TargetSlots[Index] == Cell);
				}
				if (bReciprocated)
				{
					continue;
				}
				for (int32 Index = 0; Index < 4; ++Index)
				{
					if (TargetSlots[Index] == INDEX_NONE)
					{
						TargetSlots[Index] = Cell;
						break;
					}
				}
			}
		}
	}

	// --- 3. Priority-Flood + ε ---
	TArray<float> Water;
	Water.Init(TNumericLimits<float>::Max(), CellCount);
	TArray<ECellState> State;
	State.Init(ECellState::Unqueued, CellCount);
	TArray<int32> PopOrder;
	PopOrder.Reserve(CellCount);
	FFloodHeap Heap;

	// 种子：低于海平面的格。海面本身作为它们的起始水位 —— 海是平的。
	int32 NumSeeds = 0;
	for (int32 Cell = 0; Cell < CellCount; ++Cell)
	{
		if (Height[Cell] < SeaLevelRadius)
		{
			Heap.Push(double(SeaLevelRadius), Cell);
			State[Cell] = ECellState::Queued;
			++NumSeeds;
		}
	}
	// 整颗星都高于海平面（内流盆地）：拿最低的一格当种子，水只进湖不出海。
	if (NumSeeds == 0)
	{
		int32 Lowest = 0;
		for (int32 Cell = 1; Cell < CellCount; ++Cell)
		{
			if (Height[Cell] < Height[Lowest])
			{
				Lowest = Cell;
			}
		}
		Heap.Push(double(Height[Lowest]), Lowest);
		State[Lowest] = ECellState::Queued;
	}

	// 每格只入队一次：弹出顺序按水位**非递减**，所以第一次入队时的 Level 就是它能
	// 收到的最小水位，之后的推送都不更优。这同时把堆的上限压到格数。
	while (!Heap.IsEmpty())
	{
		const FFloodEntry Entry = Heap.Pop();
		if (State[Entry.Cell] == ECellState::Visited)
		{
			continue;
		}
		State[Entry.Cell] = ECellState::Visited;
		Water[Entry.Cell] = float(Entry.Level);
		PopOrder.Add(Entry.Cell);

		for (int32 Side = 0; Side < 4; ++Side)
		{
			const int32 Next = Neighbors[Entry.Cell * 4 + Side];
			if (Next == INDEX_NONE || State[Next] != ECellState::Unqueued)
			{
				continue;
			}
			State[Next] = ECellState::Queued;
			if (double(Height[Next]) < Entry.Level)
			{
				// 邻居比自己低 → 淹到自己这一层，再抬 ε。ε 就是「排水阶梯」：
				// 湖面因此不是绝对平的，平地也能解出唯一流向。
				Heap.Push(Entry.Level + double(Params.Epsilon), Next);
			}
			else
			{
				Heap.Push(double(Height[Next]), Next);
			}
		}
	}

	// 兜底：邻接若有断裂（理论上不该有），没访问到的格按「没水」处理，
	// 而不是留着 FLT_MAX —— 后者会让水面几何长到 3e38。
	for (int32 Cell = 0; Cell < CellCount; ++Cell)
	{
		if (State[Cell] != ECellState::Visited)
		{
			Water[Cell] = Height[Cell];
		}
	}

	// --- 4. 流向（D4）：选水位最低的邻居 ---
	TArray<int32> FlowTarget;
	FlowTarget.Init(INDEX_NONE, CellCount);
	for (int32 Cell = 0; Cell < CellCount; ++Cell)
	{
		if (Height[Cell] < SeaLevelRadius)
		{
			continue;  // 海洋格是水体本身，不产生河流
		}
		int32 Best = INDEX_NONE;
		float BestWater = Water[Cell];
		for (int32 Side = 0; Side < 4; ++Side)
		{
			const int32 Next = Neighbors[Cell * 4 + Side];
			if (Next != INDEX_NONE && Water[Next] < BestWater)
			{
				BestWater = Water[Next];
				Best = Next;
			}
		}
		FlowTarget[Cell] = Best;  // 严格小于 ⇒ 不会成环
	}

	// --- 5. 汇流量：按水位降序累加 ---
	// PopOrder 是水位升序，倒着走就是降序；而流向总是指向更低的水位（更早弹出），
	// 所以倒序扫一遍就够了，不需要额外排序。
	TArray<double> Accum;
	Accum.Init(1.0, CellCount);
	for (int32 Index = PopOrder.Num() - 1; Index >= 0; --Index)
	{
		const int32 Cell = PopOrder[Index];
		const int32 Target = FlowTarget[Cell];
		if (Target != INDEX_NONE)
		{
			Accum[Target] += Accum[Cell];
		}
	}

	// --- 6. 湖 / 海 / 河 的分类与下切 ---
	TArray<float> CarveDepth;
	CarveDepth.Init(0.f, CellCount);
	TArray<float> RiverStrength;
	RiverStrength.Init(0.f, CellCount);
	TArray<uint8> IsRiver;
	IsRiver.Init(0, CellCount);

	for (int32 Cell = 0; Cell < CellCount; ++Cell)
	{
		const bool bOcean = Water[Cell] <= SeaLevelRadius + 1e-3f;
		const bool bLake = !bOcean && double(Water[Cell]) > double(Height[Cell]) + double(Params.Epsilon) * 1.5;
		if (bOcean || bLake || Accum[Cell] < double(MinFlow))
		{
			continue;
		}
		// 下切深度按设计 §4.3 的式子：CarveDepth = clamp(1 + 2·√(Accum/MinRiverFlow), 1, Max)。
		// 阈值处就挖 1 体素 —— 「有水但看不见沟」是最难受的观感；4 倍阈值附近饱和。
		// MaxCarveDepth 夹到 ≥ 1：小于 1 会让 Clamp 的下界高于上界（断言）。
		const float MaxCarve = FMath::Max(Params.MaxCarveDepth, 1.f);
		const float Carve = FMath::Clamp(
			1.f + 2.f * FMath::Sqrt(float(Accum[Cell]) / MinFlow), 1.f, MaxCarve);
		CarveDepth[Cell] = Carve;
		// 强度只是下切深度的归一化，**不另立一条曲线** —— 两处各自定义迟早对不上。
		RiverStrength[Cell] = FMath::Saturate((Carve - 1.f) / FMath::Max(MaxCarve - 1.f, 1e-3f));
		IsRiver[Cell] = 1;
	}

	// --- 7. 湖泊连通块 → FPlanetLakeBasin ---
	//
	// 「湖」= 水位高于地形的连通块，海被排除在外（海的水位严格等于海面）。
	// 每片湖记三个量：湖心方向（各格方向的平均）、水位（块内最大水位）、角半径
	// （格到湖心的最大夹角 + 2 格余量 —— 水面片刻意探进岸线，见设计 §6.1）。
	TArray<uint8> LakeVisited;
	LakeVisited.Init(0, CellCount);
	TArray<FPlanetLakeBasin> Lakes;
	TArray<int32> Stack;
	TArray<int32> MemberCells;
	const double CellAngle = (PI * 0.5) / double(Resolution);   // 一格的角距（近似）

	const auto CellDirectionAt = [&](int32 Cell)
	{
		int32 Face = 0, X = 0, Y = 0;
		PlanetHydrologyGrid::DecodeCell(Cell, Resolution, Face, X, Y);
		return PlanetHydrologyGrid::CellDirection(Face, X, Y, Resolution);
	};
	const auto IsLakeCell = [&](int32 Cell)
	{
		const bool bOcean = Water[Cell] <= SeaLevelRadius + 1e-3f;
		return !bOcean
			&& double(Water[Cell]) > double(Height[Cell]) + double(Params.Epsilon) * 1.5;
	};

	for (int32 Start = 0; Start < CellCount; ++Start)
	{
		if (!IsLakeCell(Start) || LakeVisited[Start])
		{
			continue;
		}

		Stack.Reset();
		MemberCells.Reset();
		Stack.Add(Start);
		LakeVisited[Start] = 1;

		while (Stack.Num() > 0)
		{
			const int32 Cell = Stack.Pop(EAllowShrinking::No);
			MemberCells.Add(Cell);

			for (int32 Side = 0; Side < 4; ++Side)
			{
				const int32 Next = Neighbors[Cell * 4 + Side];
				if (Next == INDEX_NONE || LakeVisited[Next] || !IsLakeCell(Next))
				{
					continue;
				}
				LakeVisited[Next] = 1;
				Stack.Add(Next);
			}
		}

		// 太小的水洼不留：它们只会让水面几何多出几十个碎片。
		constexpr int32 MinLakeCells = 8;
		if (MemberCells.Num() < MinLakeCells)
		{
			continue;
		}

		// 第一趟：湖心与水位。
		FVector DirSum = FVector::ZeroVector;
		double MaxWater = -TNumericLimits<double>::Max();
		for (const int32 Cell : MemberCells)
		{
			DirSum += CellDirectionAt(Cell);
			MaxWater = FMath::Max(MaxWater, double(Water[Cell]));
		}
		const FVector Centroid = DirSum.GetSafeNormal();
		if (Centroid.IsNearlyZero())
		{
			continue;
		}

		// 第二趟：最大角距（只在这片湖的格上算，不做全图扫描）。
		double MaxAngle = 0.0;
		for (const int32 Cell : MemberCells)
		{
			const double Cosine = FMath::Clamp(
				double(FVector::DotProduct(Centroid, CellDirectionAt(Cell))), -1.0, 1.0);
			MaxAngle = FMath::Max(MaxAngle, FMath::Acos(Cosine));
		}

		FPlanetLakeBasin Basin;
		Basin.CentroidDir = Centroid;
		Basin.WaterLevelRadius = float(MaxWater);
		Basin.AngularRadius = float(MaxAngle + 2.0 * CellAngle);
		Lakes.Add(Basin);
	}

	// --- 8. 河流折线 ---
	// 上游计数：没有上游的河格就是源头。
	TArray<int32> UpstreamCount;
	UpstreamCount.Init(0, CellCount);
	for (int32 Cell = 0; Cell < CellCount; ++Cell)
	{
		if (IsRiver[Cell] && FlowTarget[Cell] != INDEX_NONE && IsRiver[FlowTarget[Cell]])
		{
			++UpstreamCount[FlowTarget[Cell]];
		}
	}

	TArray<FPlanetRiverPolyline> Rivers;
	TArray<int32> Path;
	for (int32 Start = 0; Start < CellCount; ++Start)
	{
		if (!IsRiver[Start] || UpstreamCount[Start] > 0)
		{
			continue;
		}

		Path.Reset();
		int32 Cell = Start;
		constexpr int32 MaxCellsPerRiver = 256;
		while (Cell != INDEX_NONE && IsRiver[Cell] && Path.Num() < MaxCellsPerRiver)
		{
			Path.Add(Cell);
			Cell = FlowTarget[Cell];
		}
		if (Path.Num() < 4)
		{
			continue;   // 太短的不算河
		}

		FPlanetRiverPolyline Polyline;
		for (const int32 PathCell : Path)
		{
			int32 Face = 0, X = 0, Y = 0;
			PlanetHydrologyGrid::DecodeCell(PathCell, Resolution, Face, X, Y);

			FPlanetRiverPoint Point;
			Point.Dir = PlanetHydrologyGrid::CellDirection(Face, X, Y, Resolution);
			Point.WaterLevelRadius = Water[PathCell];
			Point.WidthVoxels = FMath::Clamp(
				FMath::Sqrt(float(Accum[PathCell]) / MinFlow) * Params.RiverWidthScale, 1.f, 8.f);
			Polyline.Points.Add(Point);
		}
		Rivers.Add(MoveTemp(Polyline));
	}

	return FPlanetHydrologyField::Create(
		Resolution,
		SeaLevelRadius,
		Params.MaxCarveDepth,
		MoveTemp(Water),
		MoveTemp(CarveDepth),
		MoveTemp(RiverStrength),
		MoveTemp(Lakes),
		MoveTemp(Rivers));
}
```

> 内存峰值（N = 512）：`Neighbors` 25 MB + `Water` 6 MB + `Accum` 12 MB +
> `CarveDepth`/`RiverStrength` 各 6 MB + 堆 18 MB ≈ 75 MB，烘焙结束释放。**不要**
> 把这些数组留在 `FPlanetHydrologyField` 里 —— 它只收 `Water`/`Carve`/`Strength`。

- [ ] **Step 5: 构建并跑测试**

Run: UBT 构建 → `Automation RunTests StellarEra.Planet.Hydrology`
Expected: `BakeLake`、`BakeRiver` 绿。

失败时的排查顺序（别跳）：
1. `BakeLake` 说湖泊数量不是 1 → 先把 `SeaLevel`、`Amplitude`、`Radius` 三个数手算一遍，确认碗缘 `1000 + 8 × (−0.05) = 999.6` 确实高于海面 `1000 + 8 × (−0.3) = 997.6`。
2. `BakeRiver` 说没有河 → 打印 `Accum` 的最大值。最大值只有个位数说明流向全是「无」，即 `Water` 没有梯度（检查 ε 是否为 0）；最大值很大但没到阈值说明横向项没起作用（检查 `Lon` 的符号与平方）。
3. `跨面水位跳变` 变红 → 检查第 2 步的对称化循环有没有真的写回 `Neighbors`。

- [ ] **Step 6: 提交**

```bash
git -C F:/workspace/ue/StellarEra add Source/StellarEraPlanet/Src/Hydrology/PlanetHydrologyBake.h \
  Source/StellarEraPlanet/Src/Hydrology/PlanetHydrologyBake.cpp \
  Source/StellarEraPlanet/Src/Tests/PlanetHydrologyBakeTests.cpp
git -C F:/workspace/ue/StellarEra commit -m "feat(水文): Priority-Flood 填洼 + 4 邻接汇流 + 河道下切

烘焙只接受高度回调与参数，不认识地形子类。4 邻接表带跨面对称化，
湖与河的特征（水位、角半径、折线、河宽）一并算出来。" \
  -- Source/StellarEraPlanet/Src/Hydrology/PlanetHydrologyBake.h \
     Source/StellarEraPlanet/Src/Hydrology/PlanetHydrologyBake.cpp \
     Source/StellarEraPlanet/Src/Tests/PlanetHydrologyBakeTests.cpp
```

---

### Task 4: 接线 —— 地形模块出配置，生成器缓存水文场，行星触发烘焙

这一层只做搬运，**不改任何密度逻辑**。做完之后整条链路是通的，但地形还没消费
水文场（那是 Task 5），所以测试只能钉住「没有水的地方不烘焙」这一侧。

**Files:**
- Modify: `Source/StellarEraPlanet/Src/Hydrology/PlanetHydrologyTypes.h`（追加 `FPlanetWaterConfig` 与 `operator==`）
- Modify: `Source/StellarEraPlanet/Src/Terrain/VoxelPlanetTerrainModule.h`
- Modify: `Source/StellarEraPlanet/Src/Terrain/VoxelPlanetTerrainModule.cpp`
- Modify: `Source/StellarEraPlanet/Src/Terrain/VoxelPlanetGenerator.h`
- Modify: `Source/StellarEraPlanet/Src/Terrain/VoxelPlanetGenerator.cpp`
- Modify: `Source/StellarEraPlanet/Src/VoxelPlanetActor.cpp:652`（Step 1 之后插入）与 `:943`
- Test: `Source/StellarEraPlanet/Src/Tests/PlanetHydrologyPlumbingTests.cpp`

**Interfaces:**
- Consumes: `FPlanetHydrologyBake::Bake`、`FPlanetHydrologyField`、`FPlanetHydrologyParams`
- Produces:
  - `virtual float UVoxelPlanetTerrainModule::GetRawOffset(const FVector& UnitDir) const`（返回归一化位移，默认 0）
  - `virtual bool UVoxelPlanetTerrainModule::GetWaterConfig(FPlanetWaterConfig& OutConfig) const`（默认 false）
  - `FVoxelPlanetTerrainContext::Hydrology`（`TSharedPtr<const FPlanetHydrologyField>`）
  - `UVoxelPlanetGenerator::RefreshHydrology()` / `InvalidateHydrology()` / `GetHydrology()`

- [ ] **Step 1: 在 `PlanetHydrologyTypes.h` 末尾追加水体配置**

`FPlanetWaterConfig` **同时**承载水文参数与外观参数（设计 §7）：这样「火星预设」
「金星预设」各是一套完全不同的水貌，而水文层不必认识具体的地形子类。

```cpp
/**
 * 一颗行星的水体配置：水文参数 + 外观参数。
 *
 * 为什么放在一起：地形模块只暴露一个 GetWaterConfig，拆成两个虚函数会让
 * 「有水文参数但没有外观参数」这种半吊子状态在类型上合法。
 */
struct FPlanetWaterConfig
{
	/** 海平面、分辨率、汇流阈值、下切深度、ε。 */
	FPlanetHydrologyParams Params;

	/** 近岸的浅水色。 */
	FLinearColor ShallowColor = FLinearColor(0.055f, 0.22f, 0.28f);

	/** 深海色。 */
	FLinearColor DeepColor = FLinearColor(0.012f, 0.055f, 0.115f);

	/** 岸边泡沫色。 */
	FLinearColor FoamColor = FLinearColor(0.75f, 0.80f, 0.85f);

	/** 水深小于它开始泛泡沫（体素）。 */
	float FoamDepthVoxels = 0.35f;

	/** 水面的最大不透明度。1 = 完全遮住海底。 */
	float MaxOpacity = 0.85f;
};

/**
 * 精确比较，用于生成器的缓存键。
 *
 * 刻意不用 FMath::IsNearlyEqual：缓存键要的是「是不是同一组参数」，
 * 不是「视觉上是否等价」。近似相等会让「改了一点点海平面」被当成没改。
 */
inline bool operator==(const FPlanetHydrologyParams& A, const FPlanetHydrologyParams& B)
{
	return A.SeaLevel == B.SeaLevel
		&& A.Amplitude == B.Amplitude
		&& A.MinRiverFlow == B.MinRiverFlow
		&& A.MaxCarveDepth == B.MaxCarveDepth
		&& A.RiverWidthScale == B.RiverWidthScale
		&& A.Resolution == B.Resolution
		&& A.Epsilon == B.Epsilon;
}

inline bool operator!=(const FPlanetHydrologyParams& A, const FPlanetHydrologyParams& B)
{
	return !(A == B);
}
```

- [ ] **Step 2: 写失败的测试**

`Src/Tests/PlanetHydrologyPlumbingTests.cpp`：

```cpp
// 水文接线的回归测试：没有水的行星不该烘焙，也不该凭空长出水面。
//
// 这一侧只能钉「空」的一侧 —— 「有水的一侧」需要地形模块真正重写
// GetWaterConfig，那在 Task 5 里由 PlanetHydrologyPlumbingTests 的后续用例覆盖。

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "VoxelPlanetGenerator.h"
#include "VoxelPlanetSphereTerrain.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraHydrologyPlumbingTest,
	"StellarEra.Planet.Hydrology.Plumbing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraHydrologyPlumbingTest::RunTest(const FString& Parameters)
{
	// --- 默认模块：不出水 ---
	UVoxelPlanetSphereTerrain* Sphere = NewObject<UVoxelPlanetSphereTerrain>();
	TestNotNull(TEXT("纯球模块必须能实例化"), Sphere);
	if (!Sphere)
	{
		return false;
	}

	FPlanetWaterConfig Config;
	TestFalse(TEXT("默认 GetWaterConfig 必须返回 false（纯球不出水）"),
		Sphere->GetWaterConfig(Config));
	TestEqual(TEXT("默认 GetRawOffset 必须是 0（光滑球）"),
		Sphere->GetRawOffset(FVector(0.3, 0.5, 0.8).GetSafeNormal()), 0.f);

	// --- 生成器：没有水就不烘焙 ---
	UVoxelPlanetGenerator* Generator = NewObject<UVoxelPlanetGenerator>();
	TestNotNull(TEXT("生成器必须能实例化"), Generator);
	if (!Generator)
	{
		return false;
	}

	Generator->Terrain = nullptr;   // 走纯球兜底
	Generator->Radius = 500.f;
	Generator->RefreshHydrology();
	TestNull(TEXT("纯球行星不能有水文场"), Generator->GetHydrology().Get());
	TestNull(TEXT("上下文里也不能有水文场"),
		Generator->MakeTerrainContext().Hydrology.Get());

	// 改半径不该凭空造出水来（缓存重置路径也不能出错）。
	Generator->Radius = 800.f;
	Generator->RefreshHydrology();
	TestNull(TEXT("改半径后仍然没有水"), Generator->GetHydrology().Get());

	// 丢弃缓存不崩。
	Generator->InvalidateHydrology();
	TestNull(TEXT("丢弃缓存后仍然没有水"), Generator->GetHydrology().Get());

	return true;
}

#endif
```

- [ ] **Step 3: 运行测试，确认编译失败**

Run: UBT 构建。
Expected: `GetWaterConfig` / `RefreshHydrology` / `Context.Hydrology` 都不存在。

- [ ] **Step 4: 改 `VoxelPlanetTerrainModule.h`**

三处改动：

1) 加 include 与前向声明（`#include "VoxelMinimal.h"` 之后）：

```cpp
// FPlanetWaterConfig（GetWaterConfig 的参数）需要完整定义。
#include "PlanetHydrologyTypes.h"
```

前向声明区加一行：

```cpp
class FPlanetHydrologyField;
```

2) `FVoxelPlanetTerrainContext` 末尾加字段：

```cpp
	/**
	 * 这颗行星的水文场；为空 = 这颗星没有水（纯球模块，或者模块不重写
	 * GetWaterConfig）。
	 *
	 * 只读指针，构造后不可变，因此可以安全地在后台体素线程上共享 —— 与
	 * Geology 同理。密度场靠它做河道下切。
	 */
	TSharedPtr<const FPlanetHydrologyField> Hydrology;
```

3) 类里 `GetSurfaceRadiusVoxels` 之后加两个虚函数：

```cpp
	/**
	 * 未削平的归一化位移（0 = 基准球面，范围 −1..1）。
	 *
	 * 与 GetSurfaceRadiusVoxels 的区别正是「有没有被海平面削平」：后者回答
	 * 「角色该站在哪」，会避开水面；前者回答「地形本来长多高」，是水文烘焙的
	 * 唯一输入。混用会把海床当成陆地再淹一遍。
	 *
	 * 默认返回 0：抽象类不该被实例化，但真有子类忘了重写时，给一颗光滑球
	 * 总好过让水文烘焙读到随机值。
	 */
	virtual float GetRawOffset(const FVector& UnitDir) const;

	/**
	 * 水体配置。返回 false = 这颗星不要水（纯球模块就是 false）。
	 *
	 * OutConfig 在返回 false 时仍被写成一个合法的默认值 —— 调用方可以先取值
	 * 再看返回值，不必分支初始化。
	 */
	virtual bool GetWaterConfig(FPlanetWaterConfig& OutConfig) const;
```

- [ ] **Step 5: 改 `VoxelPlanetTerrainModule.cpp`**

在 `GetInstance` 的实现之后加：

```cpp
float UVoxelPlanetTerrainModule::GetRawOffset(const FVector& UnitDir) const
{
	return 0.f;
}

bool UVoxelPlanetTerrainModule::GetWaterConfig(FPlanetWaterConfig& OutConfig) const
{
	OutConfig = FPlanetWaterConfig();
	return false;
}
```

- [ ] **Step 6: 改 `VoxelPlanetGenerator.h`**

public 区（`GetGeology` 之前）加：

```cpp
	/**
	 * 当前水文场；为空 = 这颗星不要水。
	 *
	 * 只在 RefreshHydrology() 之后有效。**不要**在 GetInstance 里懒构建 ——
	 * 那个调用点会在体素线程的上下文里触发一次秒级烘焙。
	 */
	TSharedPtr<const FPlanetHydrologyField> GetHydrology() const;

	/**
	 * 需要时重新烘焙。行星在把 Radius / Terrain 推完之后、取样实例之前调用。
	 *
	 * 缓存键 = 地形模块指针 + 半径 + 水体配置。配置整个参与比较，所以即使有人
	 * 忘了调 InvalidateHydrology，改海平面也会重烘。
	 */
	void RefreshHydrology();

	/** 丢弃缓存；下一次 RefreshHydrology 一定重烘。 */
	void InvalidateHydrology();
```

private 区末尾加：

```cpp
	TSharedPtr<FPlanetHydrologyField> HydrologyCache;
	TWeakObjectPtr<UVoxelPlanetTerrainModule> HydrologyCacheTerrain;
	double HydrologyCacheRadius = 0.0;
	FPlanetHydrologyParams HydrologyCacheParams;
```

- [ ] **Step 7: 改 `VoxelPlanetGenerator.cpp`**

顶部加：

```cpp
#include "HAL/PlatformTime.h"
#include "PlanetHydrologyBake.h"
```

`MakeTerrainContext` 里 `Context.Geology = GetGeology();` 之后加一行：

```cpp
	Context.Hydrology = GetHydrology();
```

文件末尾加：

```cpp
TSharedPtr<const FPlanetHydrologyField> UVoxelPlanetGenerator::GetHydrology() const
{
	return HydrologyCache;
}

void UVoxelPlanetGenerator::RefreshHydrology()
{
	UVoxelPlanetTerrainModule* EffectiveTerrain = Terrain;
	FPlanetWaterConfig Config;
	if (!EffectiveTerrain || !EffectiveTerrain->GetWaterConfig(Config))
	{
		InvalidateHydrology();
		return;
	}

	const double CacheRadius = double(Radius);
	if (HydrologyCache.IsValid()
		&& HydrologyCacheTerrain.Get() == EffectiveTerrain
		&& HydrologyCacheRadius == CacheRadius
		&& HydrologyCacheParams == Config.Params)
	{
		return;
	}

	HydrologyCacheTerrain = EffectiveTerrain;
	HydrologyCacheRadius = CacheRadius;
	HydrologyCacheParams = Config.Params;

	// HeightFn 在烘焙里被并行调用：GetRawOffset 是 const、无副作用，可以安全共享。
	// 但**必须是 const 调用** —— 任何内部缓存（比如「上一次的噪声参数」）都会
	// 变成数据竞争。
	const double StartSeconds = FPlatformTime::Seconds();
	HydrologyCache = FPlanetHydrologyBake::Bake(
		[EffectiveTerrain](const FVector& UnitDir)
		{
			return EffectiveTerrain->GetRawOffset(UnitDir);
		},
		Config.Params,
		CacheRadius);
	const double ElapsedSeconds = FPlatformTime::Seconds() - StartSeconds;

	if (!HydrologyCache.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("行星水文烘焙失败：分辨率 %d"), Config.Params.Resolution);
		return;
	}

	UE_LOG(LogTemp, Log,
		TEXT("行星水文烘焙完成：%d 面 × %d² 格，%d 片湖，%d 条河，耗时 %.2f s"),
		PlanetHydrologyGrid::FaceCount,
		Config.Params.Resolution,
		HydrologyCache->GetLakes().Num(),
		HydrologyCache->GetRivers().Num(),
		ElapsedSeconds);
}

void UVoxelPlanetGenerator::InvalidateHydrology()
{
	HydrologyCache.Reset();
	HydrologyCacheTerrain.Reset();
	HydrologyCacheRadius = 0.0;
	HydrologyCacheParams = FPlanetHydrologyParams();
}
```

> 日志那行用到了 `PlanetHydrologyGrid::FaceCount`，所以 .cpp 还要
> `#include "PlanetHydrologyGrid.h"`。**别**写死 6。

- [ ] **Step 8: 改 `VoxelPlanetActor.cpp`**

(a) `RebuildPlanet` 里，`PlanetGenerator->GeologyPreset = GeologyAsset;`（第 652 行）之后插入：

```cpp
	// --- 1.5 水文场 ---
	// 必须在 FVoxelGeneratorPicker 之前：GetInstance 会走 MakeTerrainContext，
	// 而密度场要读 Context.Hydrology 才知道哪里下切。烘焙是同步的（512 分辨率
	// 0.5–1 s），所以只在缓存键变化时发生 —— 拖动星球不会重烘。
	PlanetGenerator->RefreshHydrology();
```

(b) `InvalidateAndRebuild`（第 936 行）里，`LastParamsHash = 0;` 之前插入：

```cpp
	// 地形资产变了，水文场也过期了。RefreshHydrology 自己会比较配置，
	// 这里显式丢弃是为了让「先失效、后重建」的顺序在调用点上就看得见。
	if (PlanetGenerator)
	{
		PlanetGenerator->InvalidateHydrology();
	}
```

- [ ] **Step 9: 构建并跑测试**

Run: UBT 构建 → `Automation RunTests StellarEra.Planet`
Expected: 新增的 `Hydrology.Plumbing` 绿，`Hydrology.BakeLake`/`BakeRiver` 与既有的
`Planet.Terrain.*`、`Planet.AtmosphereParams.*` 全部仍然绿（这一任务不碰密度逻辑，
既有测试**不允许**出现新失败）。

- [ ] **Step 10: 提交**

```bash
git -C F:/workspace/ue/StellarEra add Source/StellarEraPlanet/Src/Hydrology/PlanetHydrologyTypes.h \
  Source/StellarEraPlanet/Src/Terrain/VoxelPlanetTerrainModule.h \
  Source/StellarEraPlanet/Src/Terrain/VoxelPlanetTerrainModule.cpp \
  Source/StellarEraPlanet/Src/Terrain/VoxelPlanetGenerator.h \
  Source/StellarEraPlanet/Src/Terrain/VoxelPlanetGenerator.cpp \
  Source/StellarEraPlanet/Src/VoxelPlanetActor.cpp \
  Source/StellarEraPlanet/Src/Tests/PlanetHydrologyPlumbingTests.cpp
git -C F:/workspace/ue/StellarEra commit -m "feat(水文): 地形模块出配置，生成器缓存水文场

GetRawOffset / GetWaterConfig 两个虚函数把「地形多高」和「要不要水」交给
地形模块；生成器按「模块 + 半径 + 配置」缓存烘焙结果，行星重建时推动。
没有水的行星（纯球兜底）不烘焙、也不产生任何几何。" \
  -- Source/StellarEraPlanet/Src/Hydrology/PlanetHydrologyTypes.h \
     Source/StellarEraPlanet/Src/Terrain/VoxelPlanetTerrainModule.h \
     Source/StellarEraPlanet/Src/Terrain/VoxelPlanetTerrainModule.cpp \
     Source/StellarEraPlanet/Src/Terrain/VoxelPlanetGenerator.h \
     Source/StellarEraPlanet/Src/Terrain/VoxelPlanetGenerator.cpp \
     Source/StellarEraPlanet/Src/VoxelPlanetActor.cpp \
     Source/StellarEraPlanet/Src/Tests/PlanetHydrologyPlumbingTests.cpp
```

---

### Task 5: 地形消费水文场 —— 下切、海床配色、biome 烘焙

做完这一步，行星**地形本身**就有河床和海床了，只是水面还没画出来。此时在 PIE 里
应该看到：海底是沙→深色的实心地面，河是比周围暗一档的浅沟。

**Files:**
- Modify: `Source/StellarEraPlanet/Src/Terrain/VoxelPlanetNoiseTerrain.h`
- Modify: `Source/StellarEraPlanet/Src/Terrain/VoxelPlanetNoiseTerrain.cpp`
- Modify: `Source/StellarEraPlanet/Src/Terrain/PlanetBiomeCubemap.h`
- Modify: `Source/StellarEraPlanet/Src/Terrain/PlanetBiomeCubemap.cpp`
- Modify: `Source/StellarEraPlanet/Src/VoxelPlanetActor.cpp:843`（`BakeTexture` 调用点）
- Modify: `Source/StellarEraPlanet/Src/Tests/PlanetTerrainEarlyOutTests.cpp`
- Test: `Source/StellarEraPlanet/Src/Tests/PlanetTerrainHydrologyCarveTests.cpp`

**Interfaces:**
- Consumes: `FVoxelPlanetTerrainContext::Hydrology`、`FPlanetHydrologyField::Sample`、
  `FPlanetHydrologyField::GetMaxCarveDepth`、`FPlanetHydrologyField::Create`
- Produces: `ComputePlanetSurfaceColor` 的新签名（多一个 `const FPlanetHydrologyField*`，
  在 `Geology` 之后）；`PlanetBiomeCubemap::BakeFaceColors/BakeTexture` 同位置多一个
  同类型参数

- [ ] **Step 1: 写失败的测试**

`Src/Tests/PlanetTerrainHydrologyCarveTests.cpp`：

```cpp
// 地形消费水文场的回归测试。
//
// 全部用手工构造的均匀水文场（FPlanetHydrologyField::Create）：均匀意味着双线性
// 采样在哪儿都是精确值，断言因此是确定的数，不需要跑烘焙。

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "PlanetHydrologyField.h"
#include "VoxelPlanetNoiseTerrain.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr double CarveTestRadius = 1000.0;
	constexpr int32 CarveTestGridResolution = 16;

	/** 整颗星一套常数的水文场：不淹（水位远低于地表）、下切 = Carve、河强 = River。 */
	TSharedPtr<FPlanetHydrologyField> MakeUniformField(float Carve, float River)
	{
		const int32 Resolution = CarveTestGridResolution;
		const int32 Padding = 1;
		const int32 PaddedSize = Resolution + 2 * Padding;
		const int32 CellCount = 6 * PaddedSize * PaddedSize;   // PlanetBiomeCubemap::NumFaces == 6

		TArray<float> Water;
		TArray<float> CarveDepth;
		TArray<float> RiverStrength;
		Water.Init(float(CarveTestRadius) - 100.f, CellCount);   // 远低于任何地表
		CarveDepth.Init(Carve, CellCount);
		RiverStrength.Init(River, CellCount);

		return FPlanetHydrologyField::Create(
			Resolution,
			float(CarveTestRadius) - 100.f,
			4.f,
			MoveTemp(Water),
			MoveTemp(CarveDepth),
			MoveTemp(RiverStrength),
			TArray<FPlanetLakeBasin>(),
			TArray<FPlanetRiverPolyline>());
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraTerrainHydrologyCarveTest,
	"StellarEra.Planet.Terrain.HydrologyCarve",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraTerrainHydrologyCarveTest::RunTest(const FString& Parameters)
{
	UVoxelPlanetNoiseTerrain* Terrain = NewObject<UVoxelPlanetNoiseTerrain>();
	TestNotNull(TEXT("噪声地形模块必须能实例化"), Terrain);
	if (!Terrain)
	{
		return false;
	}
	Terrain->Seed = 1234;

	FVoxelPlanetTerrainContext Context;
	Context.Radius = float(CarveTestRadius);

	const FVector Direction = FVector(0.3, -0.6, 0.74).GetSafeNormal();

	// --- 基准：没有水文场 ---
	Context.Hydrology = nullptr;
	const float NoWater = Terrain->GetSurfaceRadiusVoxels(Context, Direction);

	// --- 下切 3 体素：地表必须正好低 3 体素 ---
	Context.Hydrology = MakeUniformField(/*Carve=*/3.f, /*River=*/0.f);
	const float Carved = Terrain->GetSurfaceRadiusVoxels(Context, Direction);

	TestTrue(FString::Printf(TEXT("下切 3 体素后地表 %.3f 必须低于无水文场时的 %.3f"),
		Carved, NoWater), Carved < NoWater);
	TestTrue(FString::Printf(TEXT("下切量必须是 3 体素（实测 %.4f）"), NoWater - Carved),
		FMath::Abs((NoWater - Carved) - 3.f) < 0.01f);

	// --- 下切 0：与没有水文场完全一致（采样不能引入偏移）---
	Context.Hydrology = MakeUniformField(/*Carve=*/0.f, /*River=*/0.f);
	TestTrue(FString::Printf(TEXT("下切 0 时地表必须与无水文场一致（%.4f vs %.4f）"),
		Terrain->GetSurfaceRadiusVoxels(Context, Direction), NoWater),
		FMath::Abs(Terrain->GetSurfaceRadiusVoxels(Context, Direction) - NoWater) < 1e-3f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraTerrainHydrologyConfigTest,
	"StellarEra.Planet.Terrain.HydrologyConfig",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraTerrainHydrologyConfigTest::RunTest(const FString& Parameters)
{
	UVoxelPlanetNoiseTerrain* Terrain = NewObject<UVoxelPlanetNoiseTerrain>();
	TestNotNull(TEXT("噪声地形模块必须能实例化"), Terrain);
	if (!Terrain)
	{
		return false;
	}

	// --- 配置：与模块上的旋钮同源 ---
	Terrain->bEnableWater = true;
	Terrain->SeaLevel = -0.42f;
	Terrain->Amplitude = 11.f;
	Terrain->HydrologyResolution = 128;
	Terrain->MinRiverFlow = 40.f;
	Terrain->MaxCarveDepth = 6.f;

	FPlanetWaterConfig Config;
	TestTrue(TEXT("开了水的模块必须返回 true"), Terrain->GetWaterConfig(Config));
	TestEqual(TEXT("SeaLevel 要透传"), Config.Params.SeaLevel, -0.42f);
	TestEqual(TEXT("Amplitude 要透传"), Config.Params.Amplitude, 11.f);
	TestEqual(TEXT("Resolution 要透传"), Config.Params.Resolution, 128);
	TestEqual(TEXT("MinRiverFlow 要透传"), Config.Params.MinRiverFlow, 40.f);
	TestEqual(TEXT("MaxCarveDepth 要透传"), Config.Params.MaxCarveDepth, 6.f);

	// --- 关掉水：返回 false，但仍然给出合法配置 ---
	Terrain->bEnableWater = false;
	TestFalse(TEXT("关掉水之后必须返回 false"), Terrain->GetWaterConfig(Config));
	TestTrue(TEXT("返回 false 时配置仍必须合法（分辨率 > 0）"), Config.Params.Resolution > 0);

	// --- 原始位移：有界，且与海平面无关 ---
	Terrain->bEnableWater = true;
	const FVector Direction = FVector(-0.5, 0.2, 0.84).GetSafeNormal();
	const float OffsetAtDefaultSea = Terrain->GetRawOffset(Direction);
	Terrain->SeaLevel = 0.5f;
	const float OffsetAtHighSea = Terrain->GetRawOffset(Direction);
	TestTrue(FString::Printf(TEXT("原始位移 %.4f 必须落在 [-1, 1]"), OffsetAtDefaultSea),
		OffsetAtDefaultSea >= -1.f && OffsetAtDefaultSea <= 1.f);
	TestEqual(TEXT("原始位移不能受海平面影响"), OffsetAtHighSea, OffsetAtDefaultSea);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraTerrainSeafloorColorTest,
	"StellarEra.Planet.Terrain.SeafloorColor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraTerrainSeafloorColorTest::RunTest(const FString& Parameters)
{
	// 把海平面抬到 0.99：几乎所有方向都落到「海床」分支。
	// 水深范围压到 0.01：只要低于海平面 1% 的位移就已经是「深水」。
	FPlanetNoiseParams Params;
	Params.SeaLevel = 0.99f;
	Params.WaterDepthRange = 0.01f;
	Params.DeepWaterColor = FLinearColor(0.05f, 0.10f, 0.20f);
	Params.ShallowWaterColor = FLinearColor(0.90f, 0.10f, 0.10f);   // 诱饵：不该出现在深水
	Params.SandColor = FLinearColor(0.95f, 0.90f, 0.70f);
	Params.RockColor = FLinearColor(0.40f, 0.38f, 0.36f);
	Params.SnowColor = FLinearColor::White;

	const FVector Direction = FVector(0.11, 0.55, -0.83).GetSafeNormal();
	const float Offset = ComputeRawPlanetOffset(Params, Direction);
	if (Offset > 0.97f)
	{
		// 这个方向恰好高到不会落进海床分支（概率极低）。换个方向重试没有意义 ——
		// 直接用固定种子下的另一个方向会失去可复现性，所以这里显式记一条跳过。
		AddInfo(TEXT("该方向的位移高于 0.97，跳过海床断言"));
		return true;
	}

	const FLinearColor Color = ComputePlanetSurfaceColor(
		Params, /*Geology=*/nullptr, /*Hydrology=*/nullptr,
		CarveTestRadius, Direction, CarveTestRadius);

	TestTrue(FString::Printf(TEXT("深水海床必须是 DeepWaterColor 而不是旧的水面色（实测 %.3f %.3f %.3f）"),
		Color.R, Color.G, Color.B),
		FMath::Abs(Color.R - Params.DeepWaterColor.R) < 0.02f
			&& FMath::Abs(Color.G - Params.DeepWaterColor.G) < 0.02f
			&& FMath::Abs(Color.B - Params.DeepWaterColor.B) < 0.02f);

	// --- 河床变暗：把海平面压到 −0.9，同一方向就成了陆地 ---
	// 河床变暗只在陆地上做（海底已经被深度插值盖住），所以必须换一套参数。
	// 注意 ComputeRawPlanetOffset 不读 SeaLevel，Offset 与上面那一次相同。
	FPlanetNoiseParams LandParams = Params;
	LandParams.SeaLevel = -0.9f;
	if (Offset > LandParams.SeaLevel)
	{
		const FLinearColor DryLand = ComputePlanetSurfaceColor(
			LandParams, nullptr, nullptr, CarveTestRadius, Direction, CarveTestRadius);
		const FLinearColor Riverbed = ComputePlanetSurfaceColor(
			LandParams, nullptr, MakeUniformField(0.f, 1.f).Get(),
			CarveTestRadius, Direction, CarveTestRadius);
		TestTrue(FString::Printf(TEXT("河床 %.3f 必须比周围地面 %.3f 更暗"), Riverbed.R, DryLand.R),
			Riverbed.R < DryLand.R && Riverbed.G < DryLand.G && Riverbed.B < DryLand.B);
	}
	else
	{
		AddInfo(TEXT("该方向位移低于 −0.9，跳过河床断言"));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraTerrainHydrologyRangeTest,
	"StellarEra.Planet.Terrain.HydrologyCarveRange",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraTerrainHydrologyRangeTest::RunTest(const FString& Parameters)
{
	// 区间覆盖的机械化检查：盒子里的**实际**取值必须落在它自己的区间里。
	//
	// 这条用例专钉「下切项加在哪一侧」。下切让地表变低、最实的点变得更空，
	// 所以 **MaxValue** 必须加 MaxCarveDepth。写在下界一侧就永远查不出来 ——
	// 区间只会变得更宽，而插件据此会把整块判成「全实体」，河床里冒出实体块。
	//
	// 越界的条件是位移 c < −0.5：
	//   真值（最深处） = (D − R − 8c + 4) / 5
	//   上界（漏项）   = (D − R + 8) / 5
	//   前者更大 ⟺ −8c + 4 > 8 ⟺ c < −0.5
	FPlanetNoiseParams Params;   // 默认参数：Amplitude 8，SeedOffset 为零向量
	const float Amplitude = Params.Amplitude;

	FVector Direction = FVector::ZeroVector;
	float Offset = 0.f;
	{
		constexpr int32 NumSamples = 512;
		const double GoldenAngle = PI * (3.0 - FMath::Sqrt(5.0));
		for (int32 Index = 0; Index < NumSamples; ++Index)
		{
			const double Z = 1.0 - 2.0 * (double(Index) + 0.5) / double(NumSamples);
			const double RingRadius = FMath::Sqrt(FMath::Max(1.0 - Z * Z, 0.0));
			const double Theta = GoldenAngle * double(Index);
			const FVector Candidate(
				RingRadius * FMath::Cos(Theta), RingRadius * FMath::Sin(Theta), Z);
			const float CandidateOffset = ComputeRawPlanetOffset(Params, Candidate);
			if (CandidateOffset < -0.6f)
			{
				Direction = Candidate;
				Offset = CandidateOffset;
				break;
			}
		}
	}
	if (!TestTrue(TEXT("默认参数下必须能找到位移 < −0.6 的方向（否则这条用例失去意义）"),
		!Direction.IsNearlyZero()))
	{
		return false;
	}

	FVoxelPlanetTerrainContext Context;
	Context.Radius = float(CarveTestRadius);
	Context.Hydrology = MakeUniformField(/*Carve=*/4.f, /*River=*/0.f);

	const FVoxelPlanetNoiseTerrainInstance Instance(Context, Params);
	const FVoxelItemStack& Items = FVoxelItemStack::Empty;

	// 盒子以「下切后的地表」为中心：最远的角落在更深的实体里，
	// 那里的密度正是上界的定义点。
	const double Surface =
		double(CarveTestRadius) + double(Amplitude) * double(Offset) - 4.0;
	const FVector BoxCenter = Direction * Surface;
	constexpr int32 HalfSize = 16;
	const FIntVector Center(
		FMath::FloorToInt(BoxCenter.X),
		FMath::FloorToInt(BoxCenter.Y),
		FMath::FloorToInt(BoxCenter.Z));
	const FIntVector Min = Center - HalfSize;
	const FIntVector Max = Center + HalfSize;

	const TVoxelRange<v_flt> Range = Instance.GetValueRangeImpl(FVoxelIntBox(Min, Max), 0, Items);

	bool bAllCovered = true;
	double WorstValue = 0.0;
	for (int32 X = Min.X; X <= Max.X; X += 3)
	{
		for (int32 Y = Min.Y; Y <= Max.Y; Y += 3)
		{
			for (int32 Z = Min.Z; Z <= Max.Z; Z += 3)
			{
				const double Value = double(
					Instance.GetValueImpl(v_flt(X), v_flt(Y), v_flt(Z), 0, Items));
				if (Value < double(Range.Min) || Value > double(Range.Max))
				{
					bAllCovered = false;
					WorstValue = Value;
				}
			}
		}
	}

	TestTrue(FString::Printf(
		TEXT("取值必须被区间 [%.4f, %.4f] 覆盖（越界值 %.4f）—— 下切项属于 MaxValue 一侧"),
		double(Range.Min), double(Range.Max), WorstValue),
		bAllCovered);

	return true;
}

#endif
```

- [ ] **Step 2: 构建，确认编译失败**

Run: UBT 构建。
Expected: `bEnableWater` / `GetRawOffset` / `ComputePlanetSurfaceColor` 的三参数版本都不存在。

- [ ] **Step 3: 改 `VoxelPlanetNoiseTerrain.h`**

(a) 加 include 与前向声明：

```cpp
#include "PlanetHydrologyField.h"   // GetWaterConfig 的参数需要完整定义
```
（`#include "PlanetHydrologyTypes.h"` 由它带入。）

(b) `UVoxelPlanetNoiseTerrain` 里，`SeaLevel` 那组旋钮之后加：

```cpp
	// ---- 水体 ----

	/** 关掉 = 回到「一颗没水的星」：不烘焙、不出水面、地形也没有河床。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "水文")
	bool bEnableWater = true;

	/**
	 * 水文栅格每面的分辨率。256 快预览、512 默认、1024 最终画面。
	 *
	 * 它同时决定水面几何的精度上限：湖泊的角半径与河流的折线都来自这张栅格，
	 * 调低它水边会变得方块状。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "水文", meta = (ClampMin = "32", ClampMax = "2048"))
	int32 HydrologyResolution = 512;

	/** 汇流量超过它才算河（格数）。越小河越多、越细。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "水文", meta = (ClampMin = "2"))
	float MinRiverFlow = 24.f;

	/**
	 * 河道最大下切深度（体素）。
	 *
	 * 它同时是密度不确定带的加宽量：调大 1，插件就少跳过一圈区块。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "水文", meta = (ClampMin = "1", ClampMax = "32"))
	float MaxCarveDepth = 4.f;

	/** 河宽的全局缩放。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "水文", meta = (ClampMin = "0.1"))
	float RiverWidthScale = 1.f;
```

> Category 用 `"水文"`；**如果这个文件里已有的 UPROPERTY 用的是别的分类名，就跟着
> 已有的写**（分类不一致会让旋钮在细节面板里散成两组）。

(c) 类里 `GetSurfaceRadiusVoxels` 之后加两个 override：

```cpp
	virtual float GetRawOffset(const FVector& UnitDir) const override;
	virtual bool GetWaterConfig(FPlanetWaterConfig& OutConfig) const override;
```

(d) `ComputePlanetSurfaceColor` 的声明（在 `ComputeRawPlanetOffset` 附近）加参数：

```cpp
FLinearColor ComputePlanetSurfaceColor(
	const FPlanetNoiseParams& Params,
	const FPlanetGeologyField* Geology,
	const FPlanetHydrologyField* Hydrology,
	double Radius,
	const FVector& UnitDir,
	double Distance);
```

- [ ] **Step 4: `VoxelPlanetNoiseTerrain.cpp` —— 两个 override**

放在 `GetSurfaceRadiusVoxels` 之后：

```cpp
float UVoxelPlanetNoiseTerrain::GetRawOffset(const FVector& UnitDir) const
{
	const FVector Direction = UnitDir.GetSafeNormal();
	if (Direction.IsNearlyZero())
	{
		return 0.f;
	}

	// 每次调用都重建一次 Params（含 3 次 FRandomStream 取样）。512 分辨率下
	// 约 157 万次调用、每次 6 次乘法移位，总量在几十毫秒量级。
	//
	// **不要**为此加一个 mutable 缓存：烘焙在 ParallelFor 里并发调这个函数，
	// 缓存就是一个数据竞争。MakeParams() 每次返回新值反而是线程安全的根据。
	return ComputeRawPlanetOffset(MakeParams(), Direction);
}

bool UVoxelPlanetNoiseTerrain::GetWaterConfig(FPlanetWaterConfig& OutConfig) const
{
	// 先填满再返回：返回 false 时调用方也拿得到一份合法配置，不必分支初始化。
	FPlanetWaterConfig Config;
	Config.Params.SeaLevel = SeaLevel;
	Config.Params.Amplitude = Amplitude;
	Config.Params.MinRiverFlow = MinRiverFlow;
	Config.Params.MaxCarveDepth = MaxCarveDepth;
	Config.Params.RiverWidthScale = RiverWidthScale;
	Config.Params.Resolution = HydrologyResolution;

	Config.ShallowColor = ShallowWaterColor;
	Config.DeepColor = DeepWaterColor;

	OutConfig = Config;
	return bEnableWater;
}
```

- [ ] **Step 5: `VoxelPlanetNoiseTerrain.cpp` —— 实例：包络、下切、区间**

(a) 构造函数初始化列表（第 149-154 行）改成：

```cpp
	// 与 GetValueRangeImpl 完全相同的包络，只是在这里一次算好。
	// 下界 = Radius + Amplitude × (−1) − MaxCarveDepth：海平面削平去掉之后，
	// 位移下界从 SeaLevel 变成 −1（见 ComputeRawPlanetOffset 的注释），
	// 河床还要再往下挖。GeoDelta 只往外推，所以只影响上界。
	, MinSurfaceRadius(double(Radius)
		+ double(Params.Amplitude) * MinNormalizedOffset
		- (Context.Hydrology.IsValid() ? double(Context.Hydrology->GetMaxCarveDepth()) : 0.0))
	, MaxSurfaceRadius(double(Radius) + double(Params.Amplitude)
		+ (Context.Geology.IsValid() ? double(Context.Geology->GetMaxGeometryDelta()) : 0.0))
```

同时在初始化列表里加成员 `, Hydrology(Context.Hydrology)`（`.h` 里加
`TSharedPtr<const FPlanetHydrologyField> Hydrology;`，紧挨 `Geology`）。

(b) `GetValueImpl`（第 207-216 行）改成：

```cpp
	const FVector UnitDir = GetUnitDirection(Relative);
	const double BaseSurface =
		double(Radius) + double(Params.Amplitude) * double(ComputeRawPlanetOffset(Params, UnitDir));

	// 河床下切：只往下挖，不动横向。没有水文场时恒为 0。
	const double CarveDepth = Hydrology.IsValid()
		? double(Hydrology->Sample(UnitDir).CarveDepth)
		: 0.0;

	// 深度以基准球面为 0、向内为正。几何增量只往外推，不挖坑。
	const double GeometryDelta = Geology.IsValid()
		? double(Geology->SampleGeometryDelta(UnitDir, double(Radius) - Distance))
		: 0.0;

	const double Value = (Distance - BaseSurface + CarveDepth - GeometryDelta) / DensityScale;
```

(c) `GetValueRangeImpl`（第 347-370 行）改成：

```cpp
	// 位移的确定包络。上界 1 由「归一化除以权重和」直接给出；下界 −1 同理
	// —— 海平面削平去掉之后，ComputeRawPlanetOffset 的输出域就是 [−1, 1]。
	//
	// 这个区间是保守的（真实 fbm 极少同时触到两端），但保守是**安全**的方向：
	// 估高了只是多算一些区块，估低了地表会出洞。
	const double MaxOffset = MaxNormalizedOffset;
	const double MinOffset = MinNormalizedOffset;

	// 地表半径的两端：
	//   上界 ← 位移取最小（−1）**并再减掉下切**（河床是最低的地表）
	//   下界 ← 位移取最大（+1）再加地质增量（矿脉是最高的地表）
	// 下切与地质增量各自只压一侧，方向**不能写反** —— 写反的表现是河床一片
	// 空气洞里冒出实体块，而数组越界不会崩，只会静默出洞。
	const double MaxGeometryDelta =
		Geology.IsValid() ? double(Geology->GetMaxGeometryDelta()) : 0.0;
	const double MaxCarveDepth =
		Hydrology.IsValid() ? double(Hydrology->GetMaxCarveDepth()) : 0.0;

	const double MinValue =
		(MinDistance - double(Radius) - double(Params.Amplitude) * MaxOffset
			- MaxGeometryDelta) / DensityScale;
	const double MaxValue =
		(MaxDistance - double(Radius) - double(Params.Amplitude) * MinOffset
			+ MaxCarveDepth) / DensityScale;
```

> `MinNormalizedOffset` / `MaxNormalizedOffset` 是 Task 1 在匿名命名空间里加的常量
> （−1.0 / 1.0）。它们在 `.cpp` 顶部，实例的构造函数与 `GetValueRangeImpl` 都能用。

(d) `GetSurfaceRadiusVoxels`（第 112-138 行）在 `BaseSurface` 之后加：

```cpp
	// 河床也算地表：角色站在河里（视觉上没过脚踝），而不是浮在水面上 ——
	// 本期不做浮力。水面几何是独立的一张壳，不参与碰撞。
	double CarveDepth = 0.0;
	if (Context.Hydrology.IsValid())
	{
		CarveDepth = double(Context.Hydrology->Sample(Direction).CarveDepth);
	}
```

并把返回值改成 `float(BaseSurface - CarveDepth + GeometryDelta);`

- [ ] **Step 6: `VoxelPlanetNoiseTerrain.cpp` —— 配色重写**

`ComputePlanetSurfaceColor` 整体替换成：

```cpp
FLinearColor ComputePlanetSurfaceColor(
	const FPlanetNoiseParams& Params,
	const FPlanetGeologyField* Geology,
	const FPlanetHydrologyField* Hydrology,
	double Radius,
	const FVector& UnitDir,
	double Distance)
{
	const float Offset = ComputeRawPlanetOffset(Params, UnitDir);

	FLinearColor Color;

	if (Offset < Params.SeaLevel)
	{
		// 海床：浅处是沙，越深越接近 DeepWaterColor。
		//
		// **这里不再是蓝色水面本身** —— 水的蓝色、透明度与高光归水面材质
		// （见 Water/PlanetWaterMaterialBuilder）。地形只如实记录海底的颜色，
		// 于是透过半透明的水看到的是真的沙和真的岩，而不是又一层蓝。
		const float Depth = FMath::Clamp(
			(Params.SeaLevel - Offset) / FMath::Max(Params.WaterDepthRange, 1e-4f), 0.f, 1.f);
		Color = MixColor(Params.SandColor, Params.DeepWaterColor, Depth);
	}
	else
	{
		// 陆地：把 [海平面, 1] 映射到 [0, 1]，再按两个阈值分三段。
		// （以下与改造前逐字相同）
		const float LandHeight = FMath::Clamp(
			(Offset - Params.SeaLevel) / FMath::Max(1.f - Params.SeaLevel, 1e-4f), 0.f, 1.f);

		if (LandHeight < Params.SandToGrassHeight)
		{
			Color = MixColor(Params.SandColor, Params.GrassColor,
				LandHeight / FMath::Max(Params.SandToGrassHeight, 1e-4f));
		}
		else if (LandHeight < Params.GrassToRockHeight)
		{
			Color = MixColor(Params.GrassColor, Params.RockColor,
				(LandHeight - Params.SandToGrassHeight) / FMath::Max(Params.GrassToRockHeight - Params.SandToGrassHeight, 1e-4f));
		}
		else
		{
			Color = MixColor(Params.RockColor, Params.SnowColor,
				(LandHeight - Params.GrassToRockHeight) / FMath::Max(1.f - Params.GrassToRockHeight, 1e-4f));
		}

		// 极地雪：|Z| 越接近 1 越靠两极。叠在高度配色**之上**，所以高山和两极
		// 都会变白 —— 这正是真实行星的样子。
		const float Latitude = FMath::Abs(UnitDir.Z);
		if (Latitude > Params.PolarStart)
		{
			const float SnowBlend = FMath::Clamp(
				(Latitude - Params.PolarStart) / FMath::Max(1.f - Params.PolarStart, 1e-4f), 0.f, 1.f);
			Color = MixColor(Color, Params.SnowColor, SnowBlend);
		}
	}

	// 河床：湿岩 / 湿沙，比周围地面暗一档。只在陆地上做 —— 海底的河道已经
	// 被上面那段深度插值盖住了。
	if (Hydrology && Offset >= Params.SeaLevel)
	{
		const float RiverStrength = Hydrology->Sample(UnitDir).RiverStrength;
		if (RiverStrength > 0.f)
		{
			// 0.35 是「看得出是河」与「不像铺了柏油」之间的折中。
			Color = MixColor(Color, Color * 0.35f, RiverStrength);
		}
	}

	// 矿色：**海底也叠**（本期改动）。海底是真实岩层，透过半透明的水看会自然
	// 混色 —— 旧逻辑「必须高于海平面」会让海床变成一片纯沙。
	if (Geology)
	{
		const double Depth = Radius - Distance;
		const FPlanetGeologySample GeologySample = Geology->Sample(UnitDir, Depth);

		const FPlanetGeologyAppearance& Appearance = Geology->GetAppearance();
		// 用最强元素自身的强度判定显现：TotalNorm 会被「丰度和」稀释，
		// 铀/钛这类低丰度元素即使在自己的矿脉里也永远够不到阈值。
		if (GeologySample.DominantNorm > Appearance.ColorThreshold)
		{
			const float Presence = FMath::Clamp(
				(GeologySample.DominantNorm - Appearance.ColorThreshold)
					/ FMath::Max(Appearance.FadeRange, 1e-4f),
				0.f, 1.f);
			const float PurityMix = FMath::Clamp(
				(GeologySample.Purity - (1.f - Appearance.PurityForFullColor))
					/ FMath::Max(Appearance.PurityForFullColor, 1e-4f),
				0.f, 1.f);
			Color = MixColor(Color, GeologySample.MineralColor, Presence * PurityMix);
		}
	}

	return Color;
}
```

`GetMaterialImpl`（第 314 行）调用点补一个参数：

```cpp
	return MakeVoxelMaterial(ComputePlanetSurfaceColor(
		Params, Geology.Get(), Hydrology.Get(), double(Radius), UnitDir, Relative.Size()));
```

- [ ] **Step 7: `PlanetBiomeCubemap` 同步**

`.h` 里两个函数的参数表，在 `const FPlanetGeologyField* Geology,` 之后各加一行：

```cpp
		const FPlanetHydrologyField* Hydrology,
```

`.cpp` 的 `BakeFaceColors` 里，把

```cpp
	const double BaseSurface = double(Radius) + double(Params.Amplitude) * double(ComputePlanetOffset(Params, ...));
```

改成（注意**已改名**为 `ComputeRawPlanetOffset`，Task 1 做过）：

```cpp
	const float RawOffset = ComputeRawPlanetOffset(Params, UnitDir);
	// 与生成器采样地表时用的是同一式：基准半径 − 下切。地质色的深度因此不会偏。
	const double CarveDepth = Hydrology ? double(Hydrology->Sample(UnitDir).CarveDepth) : 0.0;
	const double BaseSurface =
		double(Radius) + double(Params.Amplitude) * double(RawOffset) - CarveDepth;
```

并把 `ComputePlanetSurfaceColor(...)` 的调用补上 `Hydrology`，`BakeTexture` 里把
`Hydrology` 透传给六个 `BakeFaceColors`。

`.cpp` 顶部加 `#include "PlanetHydrologyField.h"`。

- [ ] **Step 8: 更新调用点与既有测试**

(a) `VoxelPlanetActor.cpp:843` 的 `BakeTexture` 调用加一个参数：

```cpp
		UTextureCube* Cube = PlanetBiomeCubemap::BakeTexture(
			NoiseTerrain->MakeParams(),
			PlanetGenerator->GetGeology().Get(),
			PlanetGenerator->GetHydrology().Get(),
			double(GetScaledRadiusVoxels()),
			BiomeCubemapResolution);
```

(b) `Src/Tests/PlanetTerrainEarlyOutTests.cpp`：**不需要改**。

Task 1 已经把带底写成 `BandBottom = Radius - Amplitude`，而该测试构造的
`FVoxelPlanetTerrainContext` 不填 `Hydrology`，实例算出的 `MaxCarveDepth` 因此是 0，
包络与 Task 1 完全一致。早退测试考的是**区间覆盖**（带外一定是实体/一定是空气），
不是下切；带下切的包络由 Step 1 的 `HydrologyCarve` 那条用例兜底。

> 如果有人后来给这个测试加了水文场，就必须把带底改成
> `Radius - Amplitude - 场的 GetMaxCarveDepth()`，否则「带底以下一定是实体」会
> 在河床处变成假命题。

(c) 跑一遍 `VoxelPlanetBiomeMaterialTests`，若它断言「海平面以下不叠矿色」，
改成「海底也叠」。搜索关键词：`SeaLevel`、`MineralColor`、`DominantNorm`。

- [ ] **Step 9: 构建并跑测试**

Run: UBT 构建 → `Automation RunTests StellarEra.Planet`
Expected: `Terrain.HydrologyCarve`、`Terrain.HydrologyConfig`、`Terrain.SeafloorColor`、
`Terrain.HydrologyCarveRange` 绿；`Terrain.EarlyOut`、`Hydrology.*`、`StarMap.*` 仍然绿。

> `HydrologyCarveRange` 是这一任务唯一能挡住「区间写错一侧」的护栏。它红了就去
> 看 Step 5(c)：**MaxValue 加 MaxCarveDepth，MinValue 减 MaxGeometryDelta**。

- [ ] **Step 9.5: 检查 `PlanetBiomeCubemapTests` 与 `VoxelPlanetBiomeMaterialTests`**

这两条既有用例都直接调 `ComputePlanetSurfaceColor` / `BakeFaceColors`，签名变了就要
跟着改。搜索 `ComputePlanetSurfaceColor` 与 `BakeFaceColors`，把少了的水文参数补成
`nullptr`；若其中断言了「海平面以下不叠矿色」，改成「海底也叠」（设计 §5.4）。

- [ ] **Step 10: 提交**

```bash
git -C F:/workspace/ue/StellarEra add Source/StellarEraPlanet/Src/Terrain/VoxelPlanetNoiseTerrain.h \
  Source/StellarEraPlanet/Src/Terrain/VoxelPlanetNoiseTerrain.cpp \
  Source/StellarEraPlanet/Src/Terrain/PlanetBiomeCubemap.h \
  Source/StellarEraPlanet/Src/Terrain/PlanetBiomeCubemap.cpp \
  Source/StellarEraPlanet/Src/VoxelPlanetActor.cpp \
  Source/StellarEraPlanet/Src/Tests/PlanetTerrainEarlyOutTests.cpp \
  Source/StellarEraPlanet/Src/Tests/PlanetTerrainHydrologyCarveTests.cpp
git -C F:/workspace/ue/StellarEra commit -m "feat(地形): 消费水文场做河床下切与海床配色

地表半径减去双线性采样的 CarveDepth，密度不确定带跟着加宽 MaxCarveDepth。
海平面以下不再画成蓝色水面，而是沙→深色的海床；矿色也不再被海平面挡住。
biome cubemap 与生成器走同一式，LOD 边界颜色不回归。" \
  -- Source/StellarEraPlanet/Src/Terrain/VoxelPlanetNoiseTerrain.h \
     Source/StellarEraPlanet/Src/Terrain/VoxelPlanetNoiseTerrain.cpp \
     Source/StellarEraPlanet/Src/Terrain/PlanetBiomeCubemap.h \
     Source/StellarEraPlanet/Src/Terrain/PlanetBiomeCubemap.cpp \
     Source/StellarEraPlanet/Src/VoxelPlanetActor.cpp \
     Source/StellarEraPlanet/Src/Tests/PlanetTerrainEarlyOutTests.cpp \
     Source/StellarEraPlanet/Src/Tests/PlanetTerrainHydrologyCarveTests.cpp
```

---

### Task 6: 水面几何 —— 海洋球壳 / 湖泊球冠 / 河流条带

三块几何各自独立，因为**水位来源不同**：海洋是一张恒定海面的球壳，湖泊每片一个
水位，河流逐点取自折线。

顶点位置 = 单位方向 × 水位半径（体素）× `VoxelSize`，即**行星局部空间的厘米**。
组件挂在行星 Actor 上，于是水面天然跟着星球走，不需要任何同步。

法线直接取径向单位方向 —— 水面就是球面片，径向法线是精确解，比从三角形算法线
又快又准。

**Files:**
- Create: `Source/StellarEraPlanet/Src/Water/PlanetWaterSurfaceBuilder.h`
- Create: `Source/StellarEraPlanet/Src/Water/PlanetWaterSurfaceBuilder.cpp`
- Create: `Source/StellarEraPlanet/Src/Tests/PlanetWaterSurfaceTests.cpp`

**Interfaces:**
- Consumes: `FPlanetHydrologyField::{GetSeaLevelRadius, GetResolution, GetLakes, GetRivers, Sample}`、
  `PlanetBiomeCubemap::FaceUVToDirection`、`FDynamicMesh3`
- Produces:
  - `struct PlanetWaterSurfaceBuilder::FBuildParams { double VoxelSize; double PlanetRadiusVoxels; int32 OceanResolution; }`
  - `void BuildOcean/BuildLakes/BuildRivers(const FPlanetHydrologyField&, const FBuildParams&, FDynamicMesh3&)`

- [ ] **Step 1: 写失败的测试**

`Src/Tests/PlanetWaterSurfaceTests.cpp`：

```cpp
// 水面几何的回归测试。
//
// 三条用例各用一条解析地形，断言「该有的顶点有、不该有的顶点没有、半径对得上」。
// 不检查三角面的数量与形状 —— 那是观感问题，由 PIE 验收（Task 9）负责。

#include "CoreMinimal.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "Misc/AutomationTest.h"
#include "PlanetHydrologyBake.h"
#include "PlanetWaterSurfaceBuilder.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr double WaterTestRadius = 1000.0;
	constexpr double WaterTestVoxelSize = 100.0;

	FPlanetHydrologyParams MakeWaterTestParams()
	{
		FPlanetHydrologyParams Params;
		Params.SeaLevel = -0.3f;
		Params.Amplitude = 8.f;
		Params.MinRiverFlow = 24.f;
		Params.MaxCarveDepth = 4.f;
		Params.Resolution = 64;
		return Params;
	}

	PlanetWaterSurfaceBuilder::FBuildParams MakeBuildParams()
	{
		PlanetWaterSurfaceBuilder::FBuildParams Params;
		Params.VoxelSize = WaterTestVoxelSize;
		Params.PlanetRadiusVoxels = WaterTestRadius;
		Params.OceanResolution = 48;
		return Params;
	}

	/** 0.4 rad 以内的高台（陆地），其余全是海。 */
	float CapHeight(const FVector& Dir)
	{
		const float Angle = FMath::Acos(FMath::Clamp(float(Dir.Z), -1.f, 1.f));
		return (Angle < 0.4f) ? 0.5f : -0.5f;
	}

	/** 与 Task 3 的碗一致：碗里蓄湖、碗外是海。 */
	float BowlHeight(const FVector& Dir)
	{
		const float Angle = FMath::Acos(FMath::Clamp(float(Dir.Z), -1.f, 1.f));
		constexpr float BowlAngle = 0.3f;
		if (Angle > BowlAngle)
		{
			return -0.5f;
		}
		const float T = Angle / BowlAngle;
		return -0.5f + 0.45f * T * T;
	}

	/** 与 Task 3 的谷一致：谷底长河。 */
	float ValleyHeight(const FVector& Dir)
	{
		const double SafeZ = FMath::Clamp(double(Dir.Z), -1.0, 1.0);
		const double Angle = FMath::Acos(SafeZ);
		const double Lon = FMath::Atan2(double(Dir.Y), double(Dir.X));
		return float(-0.8 + 1.3 * (Angle / PI) + 0.4 * FMath::Square(Lon / PI));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraWaterOceanTest,
	"StellarEra.Planet.Water.Ocean",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraWaterOceanTest::RunTest(const FString& Parameters)
{
	const TSharedPtr<FPlanetHydrologyField> Field =
		FPlanetHydrologyBake::Bake(&CapHeight, MakeWaterTestParams(), WaterTestRadius);
	TestTrue(TEXT("烘焙必须成功"), Field.IsValid() && Field->IsValid());
	if (!Field.IsValid() || !Field->IsValid())
	{
		return false;
	}

	FDynamicMesh3 Mesh(true, false, false);
	PlanetWaterSurfaceBuilder::BuildOcean(*Field, MakeBuildParams(), Mesh);

	TestTrue(FString::Printf(TEXT("海洋必须有三角形（实际 %d）"), Mesh.TriangleCount()),
		Mesh.TriangleCount() > 0);
	TestTrue(TEXT("海洋必须有顶点"), Mesh.MaxVertexID() > 0);

	const double ExpectedRadiusCm = double(Field->GetSeaLevelRadius()) * WaterTestVoxelSize;
	double WorstRadiusError = 0.0;
	double NearestOnLandCap = TNumericLimits<double>::Max();
	double WorstNormalDot = 1.0;

	// 法线是逐顶点追加的，元素下标与顶点下标一一对应。
	const FDynamicMeshNormalOverlay* Normals = Mesh.Attributes()->PrimaryNormals();

	for (const int32 VertexId : Mesh.VertexIndices())
	{
		const FVector3d Position = Mesh.GetVertex(VertexId);
		const double RadiusCm = Position.Size();
		WorstRadiusError = FMath::Max(WorstRadiusError, FMath::Abs(RadiusCm - ExpectedRadiusCm));

		const FVector3d Direction = Position / FMath::Max(RadiusCm, 1e-9);

		// 法线必须朝外：水面是一张球面片，径向就是精确解。
		const FVector3f Normal = Normals->ElementAt(VertexId);
		WorstNormalDot = FMath::Min(WorstNormalDot,
			FVector3d::DotProduct(FVector3d(Normal.X, Normal.Y, Normal.Z), Direction));

		// 陆地内部的顶点必须被裁掉。允许一格余量：跨岸的那一格会被保留下来
		// （水面探进陆地一格，海岸线由交线自然给出）。
		const double AngleFromCap = FMath::Acos(FMath::Clamp(Direction.Z, -1.0, 1.0));
		NearestOnLandCap = FMath::Min(NearestOnLandCap, AngleFromCap);
	}

	TestTrue(FString::Printf(TEXT("所有顶点必须落在海面半径上（最大误差 %.4f cm）"), WorstRadiusError),
		WorstRadiusError < 1.0);
	TestTrue(FString::Printf(TEXT("法线必须朝外（最差点积 %.4f）"), WorstNormalDot),
		WorstNormalDot > 0.99);
	TestTrue(FString::Printf(TEXT("陆地内部（角距 < 0.35）不得有水面顶点（最近的 %.4f rad）"),
		NearestOnLandCap), NearestOnLandCap > 0.35);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraWaterLakeTest,
	"StellarEra.Planet.Water.Lake",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraWaterLakeTest::RunTest(const FString& Parameters)
{
	const TSharedPtr<FPlanetHydrologyField> Field =
		FPlanetHydrologyBake::Bake(&BowlHeight, MakeWaterTestParams(), WaterTestRadius);
	if (!TestTrue(TEXT("烘焙必须成功"), Field.IsValid() && Field->IsValid()))
	{
		return false;
	}
	if (!TestEqual(TEXT("碗里必须恰好一片湖"), Field->GetLakes().Num(), 1))
	{
		return false;
	}

	FDynamicMesh3 Mesh(true, false, false);
	PlanetWaterSurfaceBuilder::BuildLakes(*Field, MakeBuildParams(), Mesh);

	TestTrue(FString::Printf(TEXT("湖泊必须有三角形（实际 %d）"), Mesh.TriangleCount()),
		Mesh.TriangleCount() > 0);

	const FPlanetLakeBasin& Lake = Field->GetLakes()[0];
	const double ExpectedRadiusCm = double(Lake.WaterLevelRadius) * WaterTestVoxelSize;
	double WorstRadiusError = 0.0;
	double FarthestAngle = 0.0;

	for (const int32 VertexId : Mesh.VertexIndices())
	{
		const FVector3d Position = Mesh.GetVertex(VertexId);
		const double RadiusCm = Position.Size();
		WorstRadiusError = FMath::Max(WorstRadiusError, FMath::Abs(RadiusCm - ExpectedRadiusCm));

		const FVector3d Direction = Position / FMath::Max(RadiusCm, 1e-9);
		const double Cosine = FMath::Clamp(
			FVector3d::DotProduct(Direction, FVector3d(Lake.CentroidDir)), -1.0, 1.0);
		FarthestAngle = FMath::Max(FarthestAngle, FMath::Acos(Cosine));
	}

	TestTrue(FString::Printf(TEXT("湖面顶点必须落在湖的水位上（最大误差 %.4f cm）"), WorstRadiusError),
		WorstRadiusError < 1.0);
	TestTrue(FString::Printf(TEXT("湖面不得超出湖的角半径 %.4f（实测最远 %.4f）"),
		Lake.AngularRadius, FarthestAngle),
		FarthestAngle <= double(Lake.AngularRadius) + 1e-3);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraWaterRiverTest,
	"StellarEra.Planet.Water.River",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraWaterRiverTest::RunTest(const FString& Parameters)
{
	const TSharedPtr<FPlanetHydrologyField> Field =
		FPlanetHydrologyBake::Bake(&ValleyHeight, MakeWaterTestParams(), WaterTestRadius);
	if (!TestTrue(TEXT("烘焙必须成功"), Field.IsValid() && Field->IsValid()))
	{
		return false;
	}

	FDynamicMesh3 Mesh(true, false, false);
	PlanetWaterSurfaceBuilder::BuildRivers(*Field, MakeBuildParams(), Mesh);

	TestTrue(FString::Printf(TEXT("河网必须有三角形（实际 %d）"), Mesh.TriangleCount()),
		Mesh.TriangleCount() > 0);

	bool bAllSane = true;
	TArray<double> PairRadii;
	for (const int32 VertexId : Mesh.VertexIndices())
	{
		const FVector3d Position = Mesh.GetVertex(VertexId);
		bAllSane &= FMath::IsFinite(Position.X) && FMath::IsFinite(Position.Y)
			&& FMath::IsFinite(Position.Z);
		// 河面必须贴着地表那一层：半径落在 [海面, 最高地形] 之间。
		bAllSane &= Position.Size() > double(Field->GetSeaLevelRadius()) * WaterTestVoxelSize;
		PairRadii.Add(Position.Size());
	}
	TestTrue(TEXT("河面顶点必须是有限值且高于海面"), bAllSane);

	// 沿河向下游水位单调不增：顶点是「左岸、右岸」成对追加的，所以每两点取平均。
	bool bMonotone = true;
	double PreviousRadius = TNumericLimits<double>::Max();
	for (int32 Index = 0; Index + 1 < PairRadii.Num(); Index += 2)
	{
		const double PairRadius = (PairRadii[Index] + PairRadii[Index + 1]) * 0.5;
		bMonotone &= PairRadius <= PreviousRadius + 1e-3;
		PreviousRadius = PairRadius;
	}
	TestTrue(TEXT("河面水位必须沿下游单调不增"), bMonotone);

	return true;
}

#endif
```

- [ ] **Step 2: 构建，确认编译失败**

Run: UBT 构建。
Expected: `PlanetWaterSurfaceBuilder.h` 不存在。

- [ ] **Step 3: 写 `PlanetWaterSurfaceBuilder.h`**

```cpp
// 水文场 → 水面几何（FDynamicMesh3，行星局部空间，单位 cm）。

#pragma once

#include "CoreMinimal.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "PlanetHydrologyField.h"

/**
 * 水面几何构建器。
 *
 * 三块几何各自独立，因为它们的水位来源不同：
 *   海洋 —— 一张恒定海面的球壳，按「这一格有没有海」裁掉陆地下面的部分
 *   湖泊 —— 每片湖一张球面冠，水位取该湖的 WaterLevelRadius
 *   河流 —— 沿折线的条带，水位逐点取自折线
 *
 * 顶点位置 = 单位方向 × 水位半径(体素) × VoxelSize，即**行星局部空间的厘米**。
 * 调用方（UPlanetWaterComponent）把网格交给 UDynamicMeshComponent，组件挂在
 * 行星 Actor 上，于是水面天然跟着星球走，不需要任何同步代码。
 *
 * 法线直接取径向单位方向：水面就是球面片，径向法线是精确解，比从三角形反算
 * 又快又准（而且河流条带很窄，三角形反算会放大误差）。
 */
namespace PlanetWaterSurfaceBuilder
{
	struct FBuildParams
	{
		/** 体素边长（cm）。行星局部空间与体素空间的比例。 */
		double VoxelSize = 100.0;

		/** 行星基准半径（体素）。把厘米宽度换算成球面角的依据。 */
		double PlanetRadiusVoxels = 1000.0;

		/** 海洋球壳每面的格数。96 → 1 km 半径行星上三角边长约 3 m。 */
		int32 OceanResolution = 96;
	};

	/** 海洋：半径恒为海面的球壳，陆地内部整格跳过。 */
	STELLARERAPLANET_API void BuildOcean(
		const FPlanetHydrologyField& Field,
		const FBuildParams& Params,
		FDynamicMesh3& OutMesh);

	/** 湖泊：每片湖一张球冠，多余的边缘探进地形里（岸线由深度测试给出）。 */
	STELLARERAPLANET_API void BuildLakes(
		const FPlanetHydrologyField& Field,
		const FBuildParams& Params,
		FDynamicMesh3& OutMesh);

	/** 河流：沿每条折线的四边形条带。 */
	STELLARERAPLANET_API void BuildRivers(
		const FPlanetHydrologyField& Field,
		const FBuildParams& Params,
		FDynamicMesh3& OutMesh);
}
```

- [ ] **Step 4: 写 `PlanetWaterSurfaceBuilder.cpp`**

```cpp
// 水文场 → 水面几何。

#include "PlanetWaterSurfaceBuilder.h"

#include "PlanetBiomeCubemap.h"

namespace
{
	/** 河流沿切线向两侧额外铺开的余量（cm）。岸线由深度测试给出，不靠精度。 */
	constexpr double RiverMarginCm = 50.0;

	/** 河面整体抬起的量（cm）。避免与河床三角形共面打架（z-fighting）。 */
	constexpr double RiverSurfaceLiftCm = 5.0;

	/** 判定「这里的水位就是海面」的容差（体素）。 */
	constexpr float OceanLevelEpsilon = 1e-3f;

	/**
	 * 每次重建都从一张干净的表开始。
	 *
	 * 不用 OutMesh.Clear()：它不清 Attributes，残留的法线槽位会让 AppendElement
	 * 的索引与顶点对不上（表现为第一次重建正常、第二次法线全乱）。
	 */
	void ResetMesh(FDynamicMesh3& Mesh)
	{
		Mesh = FDynamicMesh3(/*bWantNormals=*/true, /*bWantColors=*/false, /*bWantUVs=*/false);
		Mesh.EnableAttributes();
	}

	/** 加一个水面顶点：位置 = 方向 × 半径(cm)，法线 = 方向。 */
	int32 AddWaterVertex(FDynamicMesh3& Mesh, const FVector& Direction, const double RadiusCm)
	{
		const int32 Index = Mesh.AppendVertex(
			FVector3d(Direction.X * RadiusCm, Direction.Y * RadiusCm, Direction.Z * RadiusCm));
		Mesh.Attributes()->PrimaryNormals()->AppendElement(
			FVector3f(Direction.X, Direction.Y, Direction.Z));
		return Index;
	}

	/** 切平面的两个正交基。选哪个轴当参考只影响顶点顺序，不影响形状。 */
	void MakeTangentBasis(const FVector& Normal, FVector& OutT1, FVector& OutT2)
	{
		const FVector Reference = (FMath::Abs(Normal.Z) < 0.9) ? FVector::UpVector : FVector::RightVector;
		OutT1 = FVector::CrossProduct(Normal, Reference).GetSafeNormal();
		OutT2 = FVector::CrossProduct(Normal, OutT1).GetSafeNormal();
	}
}

void PlanetWaterSurfaceBuilder::BuildOcean(
	const FPlanetHydrologyField& Field,
	const FBuildParams& Params,
	FDynamicMesh3& OutMesh)
{
	ResetMesh(OutMesh);
	if (!Field.IsValid() || Params.OceanResolution < 2)
	{
		return;
	}

	const int32 Resolution = Params.OceanResolution;
	const double WaterRadiusCm = double(Field.GetSeaLevelRadius()) * Params.VoxelSize;
	const double SeaLevelRadius = double(Field.GetSeaLevelRadius());

	const auto IsOceanAt = [&Field, SeaLevelRadius](const FVector& Direction)
	{
		return double(Field.Sample(Direction).WaterLevelRadius)
			<= SeaLevelRadius + double(OceanLevelEpsilon);
	};
	const auto DirectionAt = [Resolution](int32 Face, int32 X, int32 Y)
	{
		return PlanetBiomeCubemap::FaceUVToDirection(
			Face, float(X) / float(Resolution), float(Y) / float(Resolution));
	};

	TArray<int32> Vertices;
	Vertices.SetNumUninitialized((Resolution + 1) * (Resolution + 1));

	// 顶点按面独立生成：面边界处两份顶点在球面上完全重合，视觉不可见，
	// 换来的是索引简单、不需要跨面查找。
	for (int32 Face = 0; Face < PlanetBiomeCubemap::NumFaces; ++Face)
	{
		for (int32 Y = 0; Y <= Resolution; ++Y)
		{
			for (int32 X = 0; X <= Resolution; ++X)
			{
				Vertices[Y * (Resolution + 1) + X] =
					AddWaterVertex(OutMesh, DirectionAt(Face, X, Y), WaterRadiusCm);
			}
		}

		for (int32 Y = 0; Y < Resolution; ++Y)
		{
			for (int32 X = 0; X < Resolution; ++X)
			{
				// 四个角**只要有一个**是海就画这一格：水面向陆地底下探进一格，
				// 海岸线由「水面与地形的交线」自然给出，而不是按格裁出的锯齿。
				const bool bAnyOcean =
					IsOceanAt(DirectionAt(Face, X, Y))
					|| IsOceanAt(DirectionAt(Face, X + 1, Y))
					|| IsOceanAt(DirectionAt(Face, X, Y + 1))
					|| IsOceanAt(DirectionAt(Face, X + 1, Y + 1));
				if (!bAnyOcean)
				{
					continue;
				}

				const int32 A = Vertices[Y * (Resolution + 1) + X];
				const int32 B = Vertices[Y * (Resolution + 1) + X + 1];
				const int32 C = Vertices[(Y + 1) * (Resolution + 1) + X + 1];
				const int32 D = Vertices[(Y + 1) * (Resolution + 1) + X];
				// 绕序在双面材质下不影响可见性；光照用法线，不看法线方向与绕序。
				OutMesh.AppendTriangle(A, B, C);
				OutMesh.AppendTriangle(A, C, D);
			}
		}
	}
}

void PlanetWaterSurfaceBuilder::BuildLakes(
	const FPlanetHydrologyField& Field,
	const FBuildParams& Params,
	FDynamicMesh3& OutMesh)
{
	ResetMesh(OutMesh);
	if (!Field.IsValid())
	{
		return;
	}

	// 一格的角距：把「湖的角半径」换算成环数，让湖面的顶点密度与水文栅格同阶。
	// 再密一倍没有意义 —— 湖的水位与边界本来就只精确到一格。
	const double CellAngle = (PI * 0.5) / double(FMath::Max(Field.GetResolution(), 1));

	for (const FPlanetLakeBasin& Lake : Field.GetLakes())
	{
		const double WaterRadiusCm = double(Lake.WaterLevelRadius) * Params.VoxelSize;
		const FVector Center = Lake.CentroidDir.GetSafeNormal();
		if (Center.IsNearlyZero() || Lake.AngularRadius <= 0.f)
		{
			continue;
		}

		FVector T1;
		FVector T2;
		MakeTangentBasis(Center, T1, T2);

		const int32 Rings = FMath::Clamp(
			FMath::CeilToInt(double(Lake.AngularRadius) / CellAngle), 2, 64);
		const int32 Segments = FMath::Clamp(Rings * 4, 8, 256);

		// 中心一圈扇形，往外每环一圈四边形。
		const int32 CenterVertex = AddWaterVertex(OutMesh, Center, WaterRadiusCm);

		TArray<int32> PreviousRing;
		TArray<int32> CurrentRing;
		for (int32 Ring = 1; Ring <= Rings; ++Ring)
		{
			const double Theta = double(Lake.AngularRadius) * double(Ring) / double(Rings);
			const double SinTheta = FMath::Sin(Theta);
			const double CosTheta = FMath::Cos(Theta);

			CurrentRing.Reset();
			for (int32 Segment = 0; Segment < Segments; ++Segment)
			{
				const double Phi = 2.0 * PI * double(Segment) / double(Segments);
				const FVector Direction = (Center * CosTheta
					+ (T1 * FMath::Cos(Phi) + T2 * FMath::Sin(Phi)) * SinTheta).GetSafeNormal();
				CurrentRing.Add(AddWaterVertex(OutMesh, Direction, WaterRadiusCm));
			}

			if (Ring == 1)
			{
				for (int32 Segment = 0; Segment < Segments; ++Segment)
				{
					OutMesh.AppendTriangle(CenterVertex,
						CurrentRing[Segment], CurrentRing[(Segment + 1) % Segments]);
				}
			}
			else
			{
				for (int32 Segment = 0; Segment < Segments; ++Segment)
				{
					const int32 Next = (Segment + 1) % Segments;
					OutMesh.AppendTriangle(PreviousRing[Segment], CurrentRing[Segment], CurrentRing[Next]);
					OutMesh.AppendTriangle(PreviousRing[Segment], CurrentRing[Next], PreviousRing[Next]);
				}
			}

			Swap(PreviousRing, CurrentRing);
		}
	}
}

void PlanetWaterSurfaceBuilder::BuildRivers(
	const FPlanetHydrologyField& Field,
	const FBuildParams& Params,
	FDynamicMesh3& OutMesh)
{
	ResetMesh(OutMesh);
	if (!Field.IsValid() || Params.PlanetRadiusVoxels <= 0.0)
	{
		return;
	}

	// 厘米 → 球面角。河宽是沿途变化的，所以逐点换算、不做全局近似。
	const double PlanetRadiusCm = Params.PlanetRadiusVoxels * Params.VoxelSize;

	for (const FPlanetRiverPolyline& River : Field.GetRivers())
	{
		const int32 NumPoints = River.Points.Num();
		if (NumPoints < 2)
		{
			continue;
		}

		TArray<int32> LeftBank;
		TArray<int32> RightBank;

		for (int32 Index = 0; Index < NumPoints; ++Index)
		{
			const FPlanetRiverPoint& Point = River.Points[Index];
			const FVector Direction = Point.Dir.GetSafeNormal();
			if (Direction.IsNearlyZero())
			{
				break;
			}

			// 切线：前后点的方向差，再投影回切平面（球面上相邻方向的差本来就在
			// 切平面里，投影是防浮点噪声）。
			const int32 PreviousIndex = FMath::Max(Index - 1, 0);
			const int32 NextIndex = FMath::Min(Index + 1, NumPoints - 1);
			FVector Tangent = River.Points[NextIndex].Dir - River.Points[PreviousIndex].Dir;
			Tangent -= FVector::DotProduct(Tangent, Direction) * Direction;
			if (!Tangent.Normalize())
			{
				break;   // 退化：整条河退成一个点，后面的都不可信
			}
			const FVector Side = FVector::CrossProduct(Direction, Tangent).GetSafeNormal();

			const double HalfWidthCm =
				double(Point.WidthVoxels) * 0.5 * Params.VoxelSize + RiverMarginCm;
			const double HalfAngle = HalfWidthCm / PlanetRadiusCm;

			// 河面比水位再抬 5 cm：河床被下切过，理论上不共面，但河岸的顶点
			// 恰好落在体素面上，抬一点可以彻底避免 z-fighting。
			const double SurfaceRadiusCm =
				double(Point.WaterLevelRadius) * Params.VoxelSize + RiverSurfaceLiftCm;

			LeftBank.Add(AddWaterVertex(
				OutMesh, (Direction - Side * HalfAngle).GetSafeNormal(), SurfaceRadiusCm));
			RightBank.Add(AddWaterVertex(
				OutMesh, (Direction + Side * HalfAngle).GetSafeNormal(), SurfaceRadiusCm));
		}

		for (int32 Index = 0; Index + 1 < LeftBank.Num(); ++Index)
		{
			OutMesh.AppendTriangle(LeftBank[Index], RightBank[Index], RightBank[Index + 1]);
			OutMesh.AppendTriangle(LeftBank[Index], RightBank[Index + 1], LeftBank[Index + 1]);
		}
	}
}
```

- [ ] **Step 5: 构建并跑测试**

Run: UBT 构建 → `Automation RunTests StellarEra.Planet.Water`
Expected: 三条全绿。

失败时的排查顺序：
1. `Ocean` 说半径误差大 → `VoxelSize` 与 `GetSeaLevelRadius()` 的单位对不上（前者 cm、后者
   体素），检查是不是漏乘了一边。
2. `Lake` 说顶点超出角半径 → `Rings` 用了 `CeilToInt`，最后一环正好落在角半径上，
   断言允许 `+1e-3`；若超得多，说明 `MakeTangentBasis` 的 `T1/T2` 不正交。
3. `River` 说没有三角形 → `Rivers` 为空（烘焙问题，回 Task 3），或所有折线都 < 2 个点。

- [ ] **Step 6: 提交**

```bash
git -C F:/workspace/ue/StellarEra add Source/StellarEraPlanet/Src/Water/PlanetWaterSurfaceBuilder.h \
  Source/StellarEraPlanet/Src/Water/PlanetWaterSurfaceBuilder.cpp \
  Source/StellarEraPlanet/Src/Tests/PlanetWaterSurfaceTests.cpp
git -C F:/workspace/ue/StellarEra commit -m "feat(水体): 水面几何构建器 —— 海洋球壳 / 湖泊球冠 / 河流条带

顶点 = 方向 × 水位半径 × VoxelSize，法线取径向单位方向。海洋按「这一格有没有海」
整格跳过，水面向陆地探进一格，海岸线由水面与地形的交线给出。" \
  -- Source/StellarEraPlanet/Src/Water/PlanetWaterSurfaceBuilder.h \
     Source/StellarEraPlanet/Src/Water/PlanetWaterSurfaceBuilder.cpp \
     Source/StellarEraPlanet/Src/Tests/PlanetWaterSurfaceTests.cpp
```

---

### Task 7: 水材质 `M_PlanetWater`

半透明、双面、走场景光照。深度渐变与泡沫用像素深度与 `SceneDepth` 之差 —— 这是
材质里的标准做法，不需要额外的深度预渲染。

> **与设计的偏差（写在计划里，别在实施时另发明）**：设计 §6.3 要求「两层法线贴图
> 错速滚动」。本期**不做法线输入**：那需要一张法线贴图资产，而水面网格没有切线
> （只写了径向法线），补切线 + 贴图会把这一任务拖成一个美术任务。v1 用「低粗糙度
> + 菲涅尔 + 泡沫」拿到可看的水面；法线留给 v2，届时同时补 `FDynamicMeshTangents`
> 与 triplanar 采样。

**Files:**
- Create: `Source/StellarEraPlanet/Src/Water/PlanetWaterMaterialBuilder.h`
- Create: `Source/StellarEraPlanet/Src/Water/PlanetWaterMaterialBuilder.cpp`
- Create: `Source/StellarEraPlanet/Src/Tests/PlanetWaterMaterialTests.cpp`
- 生成资产：`/Game/Code/Planet/Materials/M_PlanetWater`（二进制，走 LFS）

**Interfaces:**
- Produces: `bool BuildPlanetWaterMaterial(FString& OutError)`（`WITH_EDITOR` 内）；
  控制台命令 `StellarEra.BuildPlanetWaterMaterial`；材质参数
  `ShallowColor`/`DeepColor`/`FoamColor`/`DepthFadeDistance`/`FoamDepth`/
  `MaxOpacity`/`ShallowOpacity`/`FresnelPower`/`FoamStrength`

- [ ] **Step 1: 写失败的测试**

`Src/Tests/PlanetWaterMaterialTests.cpp`：

```cpp
// 水材质的资产回归测试。
//
// 它不重建材质（重建是编辑器命令的事），只检查「资产在、是半透明双面、参数齐全」。
// 资产缺失时的失败信息会直接告诉你该跑哪条命令。

#include "CoreMinimal.h"
#include "Materials/Material.h"
#include "Materials/MaterialInterface.h"
#include "Misc/AutomationTest.h"
#include "UObject/ConstructorHelpers.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	const TCHAR* const WaterMaterialPath = TEXT("/Game/Code/Planet/Materials/M_PlanetWater.M_PlanetWater");
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraWaterMaterialTest,
	"StellarEra.Planet.Water.Material",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraWaterMaterialTest::RunTest(const FString& Parameters)
{
	UMaterial* Material = LoadObject<UMaterial>(nullptr, WaterMaterialPath);
	if (!TestNotNull(
		TEXT("M_PlanetWater 不存在。在编辑器里执行 StellarEra.BuildPlanetWaterMaterial 生成它"), Material))
	{
		return false;
	}

	TestEqual(TEXT("必须是半透明"), int32(Material->GetBlendMode()), int32(BLEND_Translucent));
	TestTrue(TEXT("必须双面（水下要能看到水面）"), Material->IsTwoSided());

	// --- 参数齐全 ---
	TArray<FMaterialParameterInfo> ScalarInfos;
	TArray<FGuid> ScalarGuids;
	Material->GetAllScalarParameterInfo(ScalarInfos, ScalarGuids);

	TArray<FMaterialParameterInfo> VectorInfos;
	TArray<FGuid> VectorGuids;
	Material->GetAllVectorParameterInfo(VectorInfos, VectorGuids);

	const auto HasScalar = [&ScalarInfos](const TCHAR* Name)
	{
		return ScalarInfos.ContainsByPredicate(
			[Name](const FMaterialParameterInfo& Info) { return Info.Name == Name; });
	};
	const auto HasVector = [&VectorInfos](const TCHAR* Name)
	{
		return VectorInfos.ContainsByPredicate(
			[Name](const FMaterialParameterInfo& Info) { return Info.Name == Name; });
	};

	for (const TCHAR* Name : { TEXT("DepthFadeDistance"), TEXT("FoamDepth"),
		TEXT("MaxOpacity"), TEXT("ShallowOpacity"), TEXT("FresnelPower"), TEXT("FoamStrength") })
	{
		TestTrue(FString::Printf(TEXT("缺少标量参数 %s"), Name), HasScalar(Name));
	}
	for (const TCHAR* Name : { TEXT("ShallowColor"), TEXT("DeepColor"), TEXT("FoamColor") })
	{
		TestTrue(FString::Printf(TEXT("缺少向量参数 %s"), Name), HasVector(Name));
	}

	return true;
}

#endif
```

- [ ] **Step 2: 构建并跑测试，确认它变红**

Run: UBT 构建 → `Automation RunTests StellarEra.Planet.Water.Material`
Expected: 红，信息是「M_PlanetWater 不存在」。

- [ ] **Step 3: 写 `PlanetWaterMaterialBuilder.h`**

```cpp
// 水材质 M_PlanetWater 的构建器（只在编辑器里存在）。

#pragma once

#include "CoreMinimal.h"

#if WITH_EDITOR

/**
 * 重建 /Game/Code/Planet/Materials/M_PlanetWater 并保存。
 *
 * 与 PlanetAtmosphereMaterialBuilder 同一套路：材质图完全由代码生成，改参数就
 * 重跑一次命令 —— 于是这张材质图是可复现的，不是某个资产里的手工艺品。
 *
 * 失败时返回 false 并把原因写进 OutError（不弹窗、不打日志，由调用方决定怎么说）。
 */
STELLARERAPLANET_API bool BuildPlanetWaterMaterial(FString& OutError);

#endif // WITH_EDITOR
```

- [ ] **Step 4: 写 `PlanetWaterMaterialBuilder.cpp`**

结构照 `PlanetAtmosphereMaterialBuilder.cpp`（同一个目录树里的现成范例）：
匿名命名空间 + 参数节点小工具 + 构建函数 + `FAutoConsoleCommandWithWorldAndArgs`。

```cpp
#include "CoreMinimal.h"

#if WITH_EDITOR

#include "HAL/IConsoleManager.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionCameraPositionWS.h"
#include "Materials/MaterialExpressionComponentMask.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionPixelDepth.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionSceneTexture.h"
#include "Materials/MaterialExpressionScreenPosition.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialExpressionWorldPosition.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

namespace
{

const TCHAR* const WaterAssetPath = TEXT("/Game/Code/Planet/Materials/M_PlanetWater");

// ---------------------------------------------------------------------------
// 水色与不透明度。自包含，不用 IncludeFilePaths（那需要 shader 目录映射）。
//
// 坐标空间全部是**绝对世界坐标（cm）**：
//   WorldPos  —— WorldPosition 节点 WPT_Default 的标题字面就是 "Absolute World Position"
//   CameraPos —— CameraPositionWS 是视图 uniform
//   NormalDir —— 网格顶点法线，我们写的就是径向单位向量（见 PlanetWaterSurfaceBuilder）
//
// 深度渐变用「深度缓冲里的场景深度 − 本像素深度」：正数表示水后面还有东西，
// 数值就是水层厚度。负数（水面前面还有更近的几何）夹到 0。
//
// Custom 节点里有 return，UE 不会再自动包一层，末尾必须自己 return。
// ---------------------------------------------------------------------------
const TCHAR* const WaterHLSL = TEXT(R"(
float3 toCamera = CameraPos - WorldPos;
float viewDistance = length(toCamera);
float3 viewDir = viewDistance > 0.0 ? toCamera / viewDistance : float3(0.0, 0.0, 1.0);

// 菲涅尔：掠射角更不透明、更亮。法线是径向的，垂直看下去几乎全透。
float facing = saturate(abs(dot(normalize(NormalDir), viewDir)));
float fresnel = pow(1.0 - facing, FresnelPower);

// 水层厚度（cm）。深度缓冲里没有东西时（天空）给一个大值，让远处的水接近 DeepColor。
float thickness = max(DepthBuffer - PixelDepth, 0.0);
float depthFactor = saturate(thickness / max(DepthFadeDistance, 1.0));

float3 waterColor = lerp(ShallowColor.rgb, DeepColor.rgb, depthFactor);

// 泡沫：水层很薄的地方（岸边）泛白。
float foam = saturate(1.0 - thickness / max(FoamDepth, 1.0)) * FoamStrength;
waterColor = lerp(waterColor, FoamColor.rgb, saturate(foam));

// 不透明度：浅处透、深处实，再叠菲涅尔与泡沫。
float opacity = lerp(ShallowOpacity, MaxOpacity, depthFactor);
opacity = saturate(opacity + fresnel * 0.35 + foam * 0.5);
opacity = max(opacity, foam);

return float4(waterColor, opacity);
)");

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

void AddCustomInput(UMaterialExpressionCustom* Custom, const TCHAR* InputName, UMaterialExpression* Source)
{
	FCustomInput& In = Custom->Inputs.AddDefaulted_GetRef();
	In.InputName = InputName;
	In.Input.Expression = Source;
	In.Input.OutputIndex = 0;
}

bool BuildPlanetWaterMaterial(FString& OutError)
{
	UPackage* Package = CreatePackage(WaterAssetPath);
	if (!Package)
	{
		OutError = FString::Printf(TEXT("CreatePackage 失败：%s"), WaterAssetPath);
		return false;
	}

	UMaterial* Material = FindObject<UMaterial>(Package, TEXT("M_PlanetWater"));
	if (!Material)
	{
		Material = NewObject<UMaterial>(Package, TEXT("M_PlanetWater"), RF_Public | RF_Standalone);
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
	WorldPos->WorldPositionShaderOffset = WPT_Default;   // 绝对世界位置
	WorldPos->MaterialExpressionEditorX = -1100;
	WorldPos->MaterialExpressionEditorY = -700;
	Material->GetExpressionCollection().AddExpression(WorldPos);

	// 顶点法线：水面网格写的是径向单位向量，所以它天然是「水面的朝向」。
	UMaterialExpressionVertexNormalWS* NormalWs = NewObject<UMaterialExpressionVertexNormalWS>(Material);
	NormalWs->Material = Material;
	NormalWs->MaterialExpressionEditorX = -1100;
	NormalWs->MaterialExpressionEditorY = -600;
	Material->GetExpressionCollection().AddExpression(NormalWs);

	// 深度缓冲与像素深度。
	//
	// ScreenPosition 必须取 **SEM_ViewportUV**：默认的 SEM_ScenePosition 给的是
	// 像素坐标，直接喂给 SceneTexture 会采到贴图外，整片水全黑。
	UMaterialExpressionScreenPosition* ScreenPos = NewObject<UMaterialExpressionScreenPosition>(Material);
	ScreenPos->Material = Material;
	ScreenPos->Mapping = SEM_ViewportUV;
	ScreenPos->MaterialExpressionEditorX = -1100;
	ScreenPos->MaterialExpressionEditorY = -500;
	Material->GetExpressionCollection().AddExpression(ScreenPos);

	UMaterialExpressionSceneTexture* SceneDepth = NewObject<UMaterialExpressionSceneTexture>(Material);
	SceneDepth->Material = Material;
	SceneDepth->SceneTextureId = PPI_SceneDepth;
	SceneDepth->Coordinates.Expression = ScreenPos;   // 不接就是全 0，采出一片黑
	SceneDepth->Coordinates.OutputIndex = 0;
	SceneDepth->MaterialExpressionEditorX = -800;
	SceneDepth->MaterialExpressionEditorY = -500;
	Material->GetExpressionCollection().AddExpression(SceneDepth);

	UMaterialExpressionPixelDepth* PixelDepth = NewObject<UMaterialExpressionPixelDepth>(Material);
	PixelDepth->Material = Material;
	PixelDepth->MaterialExpressionEditorX = -800;
	PixelDepth->MaterialExpressionEditorY = -400;
	Material->GetExpressionCollection().AddExpression(PixelDepth);

	UMaterialExpressionCameraPositionWS* CameraPos = NewObject<UMaterialExpressionCameraPositionWS>(Material);
	CameraPos->Material = Material;
	CameraPos->MaterialExpressionEditorX = -1100;
	CameraPos->MaterialExpressionEditorY = -300;
	Material->GetExpressionCollection().AddExpression(CameraPos);

	// --- 参数 ---
	UMaterialExpressionScalarParameter* DepthFadeDistance = AddScalarParam(Material, TEXT("DepthFadeDistance"), 400.0f, -1100, -200);
	UMaterialExpressionScalarParameter* FoamDepth = AddScalarParam(Material, TEXT("FoamDepth"), 60.0f, -1100, -100);
	UMaterialExpressionScalarParameter* MaxOpacity = AddScalarParam(Material, TEXT("MaxOpacity"), 0.85f, -1100, 0);
	UMaterialExpressionScalarParameter* ShallowOpacity = AddScalarParam(Material, TEXT("ShallowOpacity"), 0.35f, -1100, 100);
	UMaterialExpressionScalarParameter* FresnelPower = AddScalarParam(Material, TEXT("FresnelPower"), 4.0f, -1100, 200);
	UMaterialExpressionScalarParameter* FoamStrength = AddScalarParam(Material, TEXT("FoamStrength"), 0.8f, -1100, 300);

	UMaterialExpressionVectorParameter* ShallowColor = AddVectorParam(Material, TEXT("ShallowColor"),
		FLinearColor(0.055f, 0.22f, 0.28f, 1.0f), -1100, 400);
	UMaterialExpressionVectorParameter* DeepColor = AddVectorParam(Material, TEXT("DeepColor"),
		FLinearColor(0.012f, 0.055f, 0.115f, 1.0f), -1100, 500);
	UMaterialExpressionVectorParameter* FoamColor = AddVectorParam(Material, TEXT("FoamColor"),
		FLinearColor(0.75f, 0.80f, 0.85f, 1.0f), -1100, 600);

	// --- Custom 节点 ---
	UMaterialExpressionCustom* Custom = NewObject<UMaterialExpressionCustom>(Material);
	Custom->Material = Material;
	Custom->Description = TEXT("PlanetWaterSurface");
	Custom->Code = WaterHLSL;
	Custom->OutputType = CMOT_Float4;
	Custom->MaterialExpressionEditorX = -400;
	Custom->MaterialExpressionEditorY = 0;
	Material->GetExpressionCollection().AddExpression(Custom);

	AddCustomInput(Custom, TEXT("WorldPos"), WorldPos);
	AddCustomInput(Custom, TEXT("NormalDir"), NormalWs);
	AddCustomInput(Custom, TEXT("CameraPos"), CameraPos);
	AddCustomInput(Custom, TEXT("DepthBuffer"), SceneDepth);
	AddCustomInput(Custom, TEXT("PixelDepth"), PixelDepth);
	AddCustomInput(Custom, TEXT("DepthFadeDistance"), DepthFadeDistance);
	AddCustomInput(Custom, TEXT("FoamDepth"), FoamDepth);
	AddCustomInput(Custom, TEXT("MaxOpacity"), MaxOpacity);
	AddCustomInput(Custom, TEXT("ShallowOpacity"), ShallowOpacity);
	AddCustomInput(Custom, TEXT("FresnelPower"), FresnelPower);
	AddCustomInput(Custom, TEXT("FoamStrength"), FoamStrength);
	AddCustomInput(Custom, TEXT("ShallowColor"), ShallowColor);
	AddCustomInput(Custom, TEXT("DeepColor"), DeepColor);
	AddCustomInput(Custom, TEXT("FoamColor"), FoamColor);

	Custom->RebuildOutputs();

	// --- 拆 float4：RGB → BaseColor，A → Opacity ---
	UMaterialExpressionComponentMask* MaskRGB = NewObject<UMaterialExpressionComponentMask>(Material);
	MaskRGB->Material = Material;
	MaskRGB->Input.Expression = Custom;
	MaskRGB->Input.OutputIndex = 0;
	MaskRGB->R = 1; MaskRGB->G = 1; MaskRGB->B = 1; MaskRGB->A = 0;
	MaskRGB->MaterialExpressionEditorX = -100;
	MaskRGB->MaterialExpressionEditorY = -200;
	Material->GetExpressionCollection().AddExpression(MaskRGB);

	UMaterialExpressionComponentMask* MaskA = NewObject<UMaterialExpressionComponentMask>(Material);
	MaskA->Material = Material;
	MaskA->Input.Expression = Custom;
	MaskA->Input.OutputIndex = 0;
	MaskA->R = 0; MaskA->G = 0; MaskA->B = 0; MaskA->A = 1;
	MaskA->MaterialExpressionEditorX = -100;
	MaskA->MaterialExpressionEditorY = 200;
	Material->GetExpressionCollection().AddExpression(MaskA);

	// --- 材质本体 ---
	Material->BlendMode = BLEND_Translucent;
	Material->TwoSided = true;   // 水下抬头要看得见水面

	UMaterialEditorOnlyData* EditorData = Material->GetEditorOnlyData();
	if (!EditorData)
	{
		OutError = TEXT("GetEditorOnlyData() 返回空");
		return false;
	}
	EditorData->BaseColor.Expression = MaskRGB;
	EditorData->BaseColor.OutputIndex = 0;
	EditorData->Opacity.Expression = MaskA;
	EditorData->Opacity.OutputIndex = 0;

	Material->PostEditChange();
	Package->MarkPackageDirty();

	const FString PackageFileName = FPackageName::LongPackageNameToFilename(
		WaterAssetPath, FPackageName::GetAssetPackageExtension());

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

FAutoConsoleCommandWithWorldAndArgs GBuildWaterMaterialCommand(
	TEXT("StellarEra.BuildPlanetWaterMaterial"),
	TEXT("重建 /Game/Code/Planet/Materials/M_PlanetWater。改完 HLSL 字符串后跑一次。"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda(
		[](const TArray<FString>& Args, UWorld* World)
		{
			FString Error;
			if (BuildPlanetWaterMaterial(Error))
			{
				UE_LOG(LogTemp, Display, TEXT("[水体] 材质已生成：%s"), WaterAssetPath);
			}
			else
			{
				UE_LOG(LogTemp, Error, TEXT("[水体] 材质生成失败：%s"), *Error);
			}
		}));

} // namespace

#endif // WITH_EDITOR
```

> 还需要 `#include "Materials/MaterialExpressionVertexNormalWS.h"`（顶点法线节点）。
> `SEM_ViewportUV` / `PPI_SceneDepth` 两个枚举由上面 include 的两个节点头文件带进来。

- [ ] **Step 5: 构建 → 在编辑器里生成资产**

1. UBT 构建，确认编译通过。
2. 打开编辑器，在输出日志的控制台输入 `StellarEra.BuildPlanetWaterMaterial`，
   看到 `[水体] 材质已生成`。
3. 在内容浏览器里确认 `/Game/Code/Planet/Materials/M_PlanetWater` 存在，
   双击打开**没有编译错误**（材质编辑器右下角不报红）。

> 材质编译失败不会让 `SavePackage` 失败 —— 命令照样打印「已生成」。所以第 3 步的
> 肉眼确认不能省。

- [ ] **Step 6: 跑测试**

Run: `Automation RunTests StellarEra.Planet.Water.Material`
Expected: 绿。

- [ ] **Step 7: 提交**

```bash
git -C F:/workspace/ue/StellarEra add Source/StellarEraPlanet/Src/Water/PlanetWaterMaterialBuilder.h \
  Source/StellarEraPlanet/Src/Water/PlanetWaterMaterialBuilder.cpp \
  Source/StellarEraPlanet/Src/Tests/PlanetWaterMaterialTests.cpp \
  Content/Code/Planet/Materials/M_PlanetWater.uasset
git -C F:/workspace/ue/StellarEra commit -m "feat(水体): 水材质 M_PlanetWater 与编辑器构建器

半透明双面、走场景光照。水色与不透明度按「深度缓冲 − 像素深度」渐变，
岸边泛泡沫，掠射角叠菲涅尔。v1 不做法线贴图（缺贴图资产与切线）。" \
  -- Source/StellarEraPlanet/Src/Water/PlanetWaterMaterialBuilder.h \
     Source/StellarEraPlanet/Src/Water/PlanetWaterMaterialBuilder.cpp \
     Source/StellarEraPlanet/Src/Tests/PlanetWaterMaterialTests.cpp \
     Content/Code/Planet/Materials/M_PlanetWater.uasset
```

---

### Task 8: 水面组件挂上行星

`UPlanetWaterComponent` 是 `USceneComponent`，内部持有若干个 `UDynamicMeshComponent`。
它是行星 Actor 的子组件，变换随行星，不需要任何同步代码。

**Files:**
- Create: `Source/StellarEraPlanet/Src/Water/PlanetWaterComponent.h`
- Create: `Source/StellarEraPlanet/Src/Water/PlanetWaterComponent.cpp`
- Modify: `Source/StellarEraPlanet/Src/VoxelPlanetActor.h`
- Modify: `Source/StellarEraPlanet/Src/VoxelPlanetActor.cpp:705`（`InstallBiomeMaterial` 之后）
- Test: `Source/StellarEraPlanet/Src/Tests/PlanetWaterComponentTests.cpp`

**Interfaces:**
- Consumes: `PlanetWaterSurfaceBuilder::*`、`FPlanetHydrologyField`、`FPlanetWaterConfig`、
  `UVoxelPlanetGenerator::GetHydrology`、`UVoxelPlanetTerrainModule::GetWaterConfig`
  （水配置在**地形模块**上，生成器只转发水文场：写作 `PlanetGenerator->Terrain->GetWaterConfig(Config)`）
- Produces: `void UPlanetWaterComponent::RebuildWater(const FPlanetHydrologyField* Field, const FPlanetWaterConfig& Config, double VoxelSize, double PlanetRadiusVoxels)`、
  `int32 UPlanetWaterComponent::GetWaterMeshCount() const`

- [ ] **Step 1: 写失败的测试**

`Src/Tests/PlanetWaterComponentTests.cpp`：

```cpp
// 水面组件的接线测试。
//
// 只钉**不需要世界**的那一侧：没有水文场、或者没有世界时，组件不能凭空造出
// 未注册的分量（那会泄漏在关卡里，且随重建越积越多）。
// 「有世界时真的画出水面」由 Task 9 的 PIE 验收负责。

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "PlanetHydrologyField.h"
#include "PlanetWaterComponent.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraWaterComponentTest,
	"StellarEra.Planet.Water.Component",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraWaterComponentTest::RunTest(const FString& Parameters)
{
	UPlanetWaterComponent* Component = NewObject<UPlanetWaterComponent>();
	TestNotNull(TEXT("组件必须能实例化"), Component);
	if (!Component)
	{
		return false;
	}

	// --- 没有水文场：什么都不该有 ---
	Component->RebuildWater(/*Field=*/nullptr, FPlanetWaterConfig(), 100.0, 1000.0);
	TestEqual(TEXT("水文场为空时不能有水面分量"), Component->GetWaterMeshCount(), 0);

	// --- 有水文场但没有世界：仍然什么都不该有 ---
	// 未注册的组件会留在关卡里、随每次重建累积 —— 这比「水面暂时不显示」糟得多。
	// 三个栅格都必须按 CellCount 填满：Create 不做补齐，尺寸不对就是无效场。
	const int32 Resolution = 8;
	const int32 CellCount = 6 * (Resolution + 2) * (Resolution + 2);
	TArray<float> WaterLevel;
	TArray<float> CarveDepth;
	TArray<float> RiverStrength;
	WaterLevel.Init(900.f, CellCount);
	CarveDepth.Init(0.f, CellCount);
	RiverStrength.Init(0.f, CellCount);
	const TSharedPtr<FPlanetHydrologyField> Field = FPlanetHydrologyField::Create(
		Resolution, 900.f, 4.f,
		MoveTemp(WaterLevel), MoveTemp(CarveDepth), MoveTemp(RiverStrength),
		TArray<FPlanetLakeBasin>(), TArray<FPlanetRiverPolyline>());

	if (TestTrue(TEXT("手工构造的水文场必须有效"), Field.IsValid() && Field->IsValid()))
	{
		Component->RebuildWater(Field.Get(), FPlanetWaterConfig(), 100.0, 1000.0);
		TestEqual(TEXT("没有世界时不能创建分量"), Component->GetWaterMeshCount(), 0);
	}

	return true;
}

#endif
```

- [ ] **Step 2: 构建，确认编译失败**

Run: UBT 构建。
Expected: `PlanetWaterComponent.h` 不存在。

- [ ] **Step 3: 写 `PlanetWaterComponent.h`**

```cpp
// 行星水面：一组跟随行星的 UDynamicMeshComponent。

#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "PlanetHydrologyTypes.h"
#include "PlanetWaterComponent.generated.h"

class UDynamicMeshComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class FPlanetHydrologyField;

/**
 * 行星的水面组件。
 *
 * 三块几何（海洋 / 湖泊 / 河流）各是一张 UDynamicMeshComponent：它们的重建频率
 * 相同、材质相同，但分开的好处是「没有湖」时不会为湖留一个空分量。
 *
 * 组件本身不做任何 Tick：水面是静态几何，只在行星重建时整体重来一次。
 * 水面的高度取自水文场，与体素世界无关 —— 所以它在编辑器世界里也能显示。
 */
UCLASS(ClassGroup = (StellarEra), meta = (BlueprintSpawnableComponent))
class STELLARERAPLANET_API UPlanetWaterComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	UPlanetWaterComponent();

	/**
	 * 重建水面。Field 为空 = 拆掉全部分量（这颗星不要水）。
	 *
	 * @param VoxelSize          体素边长（cm），把体素半径换算成行星局部空间的厘米
	 * @param PlanetRadiusVoxels 基准半径（体素），把河宽换算成球面角
	 */
	void RebuildWater(
		const FPlanetHydrologyField* Field,
		const FPlanetWaterConfig& Config,
		double VoxelSize,
		double PlanetRadiusVoxels);

	/** 当前水面分量数（0–3）。给测试与调试用。 */
	int32 GetWaterMeshCount() const { return WaterMeshes.Num(); }

	/** 水材质。为空时 RebuildWater 只打一条 Warning 并跳过 —— 不会崩。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "水体")
	TObjectPtr<UMaterialInterface> WaterMaterial;

	/** 海洋球壳每面的格数。96 在 1 km 半径行星上约 3 m 边长。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "水体", meta = (ClampMin = "8", ClampMax = "512"))
	int32 OceanResolution = 96;

private:
	void ClearWaterMeshes();
	void AddWaterMesh(FDynamicMesh3& Mesh, const FPlanetWaterConfig& Config, double VoxelSize);

	UPROPERTY(Transient)
	TArray<TObjectPtr<UDynamicMeshComponent>> WaterMeshes;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> WaterMaterials;
};
```

> `FDynamicMesh3` 只以引用出现在私有函数的参数里，前向声明不够（它是模板/值类型，
> 声明引用可以，但 .h 里还是直接 `#include "DynamicMesh/DynamicMesh3.h"` 最省事）。
> 把上面的 `class UDynamicMeshComponent;` 那一组前向声明保留，并加这一行 include。

- [ ] **Step 4: 写 `PlanetWaterComponent.cpp`**

```cpp
// 行星水面组件。

#include "PlanetWaterComponent.h"

#include "Components/DynamicMeshComponent.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "PlanetHydrologyField.h"
#include "PlanetWaterSurfaceBuilder.h"

UPlanetWaterComponent::UPlanetWaterComponent()
{
	PrimaryComponentTick.bCanEverTick = false;

	// 水面没有碰撞：本期是纯视觉，角色自己走到海底（设计 §1）。
	SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SetGenerateOverlapEvents(false);
}

void UPlanetWaterComponent::RebuildWater(
	const FPlanetHydrologyField* Field,
	const FPlanetWaterConfig& Config,
	const double VoxelSize,
	const double PlanetRadiusVoxels)
{
	ClearWaterMeshes();

	// 没有世界就不建分量：未注册的 UDynamicMeshComponent 会留在关卡里，
	// 每次重建再叠一批，最后既看不见又删不掉。
	if (!Field || !Field->IsValid() || !GetWorld() || !GetOwner())
	{
		return;
	}
	if (!WaterMaterial)
	{
		UE_LOG(LogTemp, Warning, TEXT("行星水面组件没有材质：跳过水面生成"));
		return;
	}

	PlanetWaterSurfaceBuilder::FBuildParams BuildParams;
	BuildParams.VoxelSize = VoxelSize;
	BuildParams.PlanetRadiusVoxels = PlanetRadiusVoxels;
	BuildParams.OceanResolution = OceanResolution;

	FDynamicMesh3 Mesh;
	PlanetWaterSurfaceBuilder::BuildOcean(*Field, BuildParams, Mesh);
	AddWaterMesh(Mesh, Config, VoxelSize);

	PlanetWaterSurfaceBuilder::BuildLakes(*Field, BuildParams, Mesh);
	AddWaterMesh(Mesh, Config, VoxelSize);

	PlanetWaterSurfaceBuilder::BuildRivers(*Field, BuildParams, Mesh);
	AddWaterMesh(Mesh, Config, VoxelSize);
}

void UPlanetWaterComponent::ClearWaterMeshes()
{
	for (const TObjectPtr<UDynamicMeshComponent>& Mesh : WaterMeshes)
	{
		if (IsValid(Mesh))
		{
			Mesh->DestroyComponent();
		}
	}
	WaterMeshes.Reset();
	WaterMaterials.Reset();
}

void UPlanetWaterComponent::AddWaterMesh(
	FDynamicMesh3& Mesh,
	const FPlanetWaterConfig& Config,
	const double VoxelSize)
{
	if (Mesh.TriangleCount() == 0)
	{
		return;
	}

	UDynamicMeshComponent* Component = NewObject<UDynamicMeshComponent>(GetOwner());
	if (!Component)
	{
		return;
	}

	// 先设移动性再注册：注册之后再改会走一次组件重注册，还刷一条 Warning。
	Component->SetMobility(EComponentMobility::Movable);
	Component->SetMesh(MoveTemp(Mesh));

	// 水面不参与碰撞、不投影：它是一张纯视觉的壳。
	Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Component->SetComplexAsSimpleCollisionEnabled(false);
	Component->SetCastShadow(false);

	UMaterialInstanceDynamic* Material = UMaterialInstanceDynamic::Create(WaterMaterial, this);
	if (Material)
	{
		Material->SetVectorParameterValue(TEXT("ShallowColor"), Config.ShallowColor);
		Material->SetVectorParameterValue(TEXT("DeepColor"), Config.DeepColor);
		Material->SetVectorParameterValue(TEXT("FoamColor"), Config.FoamColor);
		Material->SetScalarParameterValue(TEXT("FoamDepth"), Config.FoamDepthVoxels * VoxelSize);
		Material->SetScalarParameterValue(TEXT("MaxOpacity"), Config.MaxOpacity);
		Component->SetMaterial(0, Material);
	}

	Component->RegisterComponent();
	Component->AttachToComponent(this, FAttachmentTransformRules::KeepRelativeTransform);

	WaterMeshes.Add(Component);
	WaterMaterials.Add(Material);
}
```

- [ ] **Step 5: 改 `VoxelPlanetActor.h`**

(a) 前向声明区加 `class UPlanetWaterComponent;`

(b) 在 `InstallBiomeMaterial` 的声明旁边（私有区）加：

```cpp
	/** 按当前水文场重建水面。没有水的行星会拆掉全部分量。 */
	void RebuildWater();
```

(c) 在其它 `UPROPERTY(Transient) TObjectPtr<...>` 组件成员的旁边加：

```cpp
	/** 行星水面。构造函数里创建，挂在根组件下面。 */
	UPROPERTY(Transient, VisibleAnywhere, Category = "水体")
	TObjectPtr<UPlanetWaterComponent> WaterComponent;
```

- [ ] **Step 6: 改 `VoxelPlanetActor.cpp`**

(a) 构造函数里（创建其它子组件的地方旁边）加：

```cpp
	WaterComponent = CreateDefaultSubobject<UPlanetWaterComponent>(TEXT("PlanetWater"));
```

(b) `RebuildPlanet` 里，`InstallBiomeMaterial(/*bForceBake=*/true);`（第 705 行）之后插入：

```cpp
	// --- 3.6 水面 ---
	//
	// 排在 CreateWorld **之前**：编辑器世界在上面那个 Toggle() 早退里就 return 了，
	// 而水面与体素世界无关（它只依赖水文场），所以它必须在这之前装好，
	// 否则编辑器视口里永远看不到水，只有 PIE 里才有。
	RebuildWater();
```

(c) 文件里加实现（放在 `InstallBiomeMaterial` 附近）：

```cpp
void AVoxelPlanetActor::RebuildWater()
{
	if (!WaterComponent)
	{
		return;
	}

	// 组件由 C++ 创建，但根组件可能晚于它出现（RepairLegacyRootComponent），
	// 所以附着放在这里做一次，而不是构造函数里。
	if (!WaterComponent->GetAttachParent() && GetRootComponent())
	{
		WaterComponent->AttachToComponent(
			GetRootComponent(), FAttachmentTransformRules::KeepRelativeTransform);
	}

	FPlanetWaterConfig Config;
	const bool bHasWater = PlanetGenerator
		&& PlanetGenerator->Terrain
		&& PlanetGenerator->Terrain->GetWaterConfig(Config)
		&& PlanetGenerator->GetHydrology().IsValid();

	WaterComponent->RebuildWater(
		bHasWater ? PlanetGenerator->GetHydrology().Get() : nullptr,
		Config,
		double(VoxelSize),
		double(GetScaledRadiusVoxels()));
}
```

(d) 顶部加 `#include "Water/PlanetWaterComponent.h"`（按本文件已有的 include 风格，
不加 `Water/` 前缀 —— 模块的 `PublicIncludePaths` 已经展开到所有子目录）。

- [ ] **Step 7: 构建并跑测试**

Run: UBT 构建 → `Automation RunTests StellarEra.Planet.Water`
Expected: `Water.Component` 绿，其余三条仍然绿。

- [ ] **Step 8: 编辑器里看一眼**

打开 `UniverseMap`，选中带 `UVoxelPlanetNoiseTerrain` 的星球，细节面板里
`水文 | 生成水体` 勾上（默认就是勾上的）。重建后视口里应当出现一层蓝色的水面，
海岸线沿着水下地形起伏 —— 此时没有 PIE，因为它不依赖体素世界。

看不到水时的排查顺序：
1. 输出日志有没有 `行星水文烘焙完成`？没有 → `bEnableWater` 为 false，或地形不是噪声地形。
2. 有没有 Warning `行星水面组件没有材质`？有 → `WaterComponent->WaterMaterial` 为空，
   检查 `StellarEra.BuildPlanetWaterMaterial` 是否跑过、资产是否在
   `/Game/Code/Planet/Materials/`。
3. 水在、但是全黑 → 材质的 `SceneTexture` 没接 `Coordinates`（Task 7 Step 4 的第二条）。
4. 水面整片消失 → `RebuildWater` 在 `Toggle()` 之后被调到了（顺序错了）。

- [ ] **Step 9: 提交**

```bash
git -C F:/workspace/ue/StellarEra add Source/StellarEraPlanet/Src/Water/PlanetWaterComponent.h \
  Source/StellarEraPlanet/Src/Water/PlanetWaterComponent.cpp \
  Source/StellarEraPlanet/Src/VoxelPlanetActor.h \
  Source/StellarEraPlanet/Src/VoxelPlanetActor.cpp \
  Source/StellarEraPlanet/Src/Tests/PlanetWaterComponentTests.cpp
git -C F:/workspace/ue/StellarEra commit -m "feat(水体): 水面组件挂上行星

UPlanetWaterComponent 持有海洋 / 湖泊 / 河流三张 DynamicMesh，重建时整体替换。
排在 CreateWorld 之前，编辑器视口里也能看到水。无世界时不创建分量，避免
未注册组件随重建累积。" \
  -- Source/StellarEraPlanet/Src/Water/PlanetWaterComponent.h \
     Source/StellarEraPlanet/Src/Water/PlanetWaterComponent.cpp \
     Source/StellarEraPlanet/Src/VoxelPlanetActor.h \
     Source/StellarEraPlanet/Src/VoxelPlanetActor.cpp \
     Source/StellarEraPlanet/Src/Tests/PlanetWaterComponentTests.cpp
```

---

### Task 9: PIE 验收与调参

自动化只钉住了「算得对」，观感只能在这里看。`UniverseMap` 是唯一有体素行星的关卡
（`NewMap` 没有 `AVoxelPlanetActor`，在那儿验不了）。

**Files:** 无代码改动（除非验收发现问题）；调参改的是 `DA_Terrain_*` 资产。

- [ ] **Step 1: 构建（编辑器必须关闭）**

```bash
"D:/Application/Epic Games/UE_5.8/Engine/Binaries/ThirdParty/DotNet/10.0/win-x64/dotnet.exe" \
  "D:/Application/Epic Games/UE_5.8/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.dll" \
  StellarEraEditor Win64 Development \
  -Project="F:/workspace/ue/StellarEra/StellarEra.uproject" -WaitMutex -FromMsBuild
```
（cwd 在项目外；不要用 `Build.bat`。）

- [ ] **Step 2: 检查存档格式（踩过两次的坑）**

`Content/Saves/PlanetVoxel/**` 是**旧格式**（`StoreSpecialValue...=1`），而当前 ini 是 0。
新拉下来的仓库直接进 `UniverseMap` 会读出一颗坏星。确认：

```bash
grep -i "StoreSpecialValueForGeneratorValuesInSaves" Config/DefaultEngine.ini
ls -l Content/Saves/PlanetVoxel/ | head
```
若存档文件是 129 字节的 LFS 指针，先 `git lfs checkout`。两种格式的迁移步骤见
CLAUDE.md「已知坑」。

- [ ] **Step 3: 进 PIE，对着设计 §9.2 的五条逐条看**

| # | 看什么 | 不符合时的旋钮 |
|---|---|---|
| 1 | 从轨道看：海面、海岸线、内陆湖泊清晰可见 | 海面太小/太大 → `SeaLevel`；湖太少 → 降 `MinRiverFlow` 或调 `Amplitude` |
| 2 | 落地沿海岸走：水陆交界自然，水半透明、能看见水下地形 | 太实 → `MaxOpacity`；太透 → `ShallowOpacity`；看不见海底 → `DepthFadeDistance` |
| 3 | 沿河走：河道真的凹下去，向上游高度单调上升 | 河太浅 → `MaxCarveDepth`；河太少 → `MinRiverFlow`；河太宽/太窄 → `RiverWidthScale` |
| 4 | 湖：内陆盆地有水面，湖面平坦 | 湖面不平 → 降低 `MaxCarveDepth` 或 `Amplitude`（ε 阶梯会随高差累积） |
| 5 | 帧率：水面占大半屏时无明显掉帧 | 降 `OceanResolution`（96 → 64） |

- [ ] **Step 4: 记一次实测数**

在日志里找 `行星水文烘焙完成：6 面 × 512² 格，N 片湖，M 条河，耗时 X s`，
把 X 与 `stat unit` 的帧时间记进本次会话的收尾报告（不要写进代码注释 —— 那会随
机器不同而腐坏）。

`X` 明显超过 1 秒（比如 > 3 s）时，按这个顺序查：
1. 编辑器是不是在前台？后台整进程限 3 FPS，烘焙会被拖成几十秒。
2. `HydrologyResolution` 是不是被调到了 1024/2048。
3. `GetRawOffset` 里有没有人加了非线程安全的状态。

- [ ] **Step 5: 调好的参数存进资产，提交**

`DA_Terrain_*` 是二进制资产（LFS）。改完在编辑器里保存，然后：

```bash
git -C F:/workspace/ue/StellarEra add Content/Code/DA_Terrain_*.uasset
git -C F:/workspace/ue/StellarEra commit -m "chore(水体): PIE 验收后调参" \
  -- Content/Code/DA_Terrain_*.uasset
```

若五条全部一次通过，**跳过这一步**（没有改动就没有可提交的东西）。

---

## 与规格的偏差

实施时按这些来，不要临时改回规格的写法。每条都附了理由。

1. **汇流方向用 4 邻接（D4），不是规格的 D8。**
   512/面时一格在 1 km 半径的行星上不到 1 米，D8 的「锯齿」看不出来；而 8 邻接要
   把跨面邻接表翻一倍、对称化逻辑翻一倍。D8 留给「河流看起来太正交」时再改。
2. **水材质 v1 没有法线输入。**
   规格 §6.3 的「两层法线贴图错速滚动」需要一张贴图资产**和网格切线**，而水面网格
   只写了径向法线。补这两样会把一个材质任务变成美术任务。v1 用「深度渐变 + 菲涅尔 +
   泡沫 + 低粗糙度」拿到可看的水面。
3. **`ShallowWaterColor` / `DeepWaterColor` 不改名。**
   规格 §5.4 说海床是「浅滩沙 → 深水暗岩」。实施上沿用旧 UPROPERTY 名：
   `ShallowWaterColor` 现在是水面材质的浅水色，`DeepWaterColor` 同时是海床的深色端。
   改名要走 `[CoreRedirects]` + 重存所有 `DA_Terrain_*`，收益只是名字更贴切。
4. **`FPlanetWaterConfig` 放在 `Hydrology/PlanetHydrologyTypes.h`。**
   规格没指定位置。放这里的理由：它必须被 `UVoxelPlanetTerrainModule::GetWaterConfig`
   用在**纯声明**里，而 `Hydrology` 不认识 `Terrain`；反过来 `Terrain` 已经依赖
   `Hydrology`（Context 里有水文场），所以依赖方向是干净的单向。
5. **`AVoxelPlanetActor::RebuildWater` 排在 `CreateWorld` 之前（规格 §8 把它排在最后）。**
   编辑器世界的 `RebuildPlanet` 在 `Toggle(); return;` 处早退，排到最后的步骤在
   编辑器视口里根本不会执行 —— 表现为「只有 PIE 里才有水」。水面不依赖体素世界，
   提前没有任何副作用。
6. **湖泊的角半径比实测最大值多 2 格。**
   规格 §6.1 说「多余部分探入地形」；给 2 格余量是为了让岸线由深度测试自然给出，
   而不是让水面刚好卡在湖的最后一格上、露出地形与水面之间的缝。

---

## 自检记录（写完计划后做的一遍检查）

**1. 规格覆盖**

| 规格条目 | 落在哪个任务 |
|---|---|
| §4.1 `FPlanetHydrologyParams` / `FPlanetLakeBasin` / `FPlanetRiverPoint` / `FPlanetRiverPolyline` | Task 2 |
| §4.2 cubemap 栅格与幽灵格 | Task 2 |
| §4.3 填洼 / 流向 / 汇流 / 下切 / 特征提取 | Task 3 |
| §4.4 成本（0.5–1 s @512） | Task 3 Step 4 内存表 + Task 9 Step 4 实测 |
| §5.1 去掉海平面削平 | Task 1 |
| §5.2 河道下切 | Task 5 Step 5(b) |
| §5.3 区间与早退包络 | Task 5 Step 5(a)(c) + `HydrologyCarveRange` 用例 |
| §5.4 配色（海床 / 河床 / 矿色不设海平面门） | Task 5 Step 6 |
| §5.5 `GetSurfaceRadiusVoxels` 含海底河床 + Context 加 Hydrology | Task 4 Step 4 + Task 5 Step 5(d) |
| §6.1 三块几何 | Task 6 |
| §6.2 组件 | Task 8 |
| §6.3 材质参数 | Task 7（法线贴图 → 偏差 2） |
| §6.4 性能 | Task 9 Step 3 第 5 条 |
| §7 代码结构与依赖 | 任务顺序本身就是依赖顺序（Hydrology → Terrain → Water） |
| §8 时序与缓存 | Task 4 Step 8 + 偏差 5 |
| §9.1 自动化测试 | Task 2/3/5/6/7/8 各自的 Step 1 |
| §9.2 PIE 验收 | Task 9 Step 3 |
| §10 风险 | Task 3 跨面连续用例、Task 9 调参表、偏差 1 |

**2. 占位符扫描**：本计划里没有 TBD / TODO / 「实现时补充」。所有「实现提示」都已
展开成可直接粘贴的代码；两处不得不留给实施者判断的地方（UPROPERTY 的 Category 名、
既有测试里是否有「海底不叠矿色」的断言）都给了**判定方法**而不是「看着办」。

**3. 类型一致性**：`FPlanetHydrologyParams` 的 7 个字段在 Types/Field/Bake/Generator/
Terrain/Water 六处引用中一致；`FPlanetWaterConfig` 的 5 个外观字段在 Task 4 定义、
Task 5 填充、Task 7 材质参数、Task 8 MID 赋值四处一致；`FPlanetHydrologyField::FSample`
的 3 个字段（`WaterLevelRadius` / `CarveDepth` / `RiverStrength`）在 Task 2 定义、
Task 3 填充、Task 5/6 消费三处一致；`PlanetWaterSurfaceBuilder::FBuildParams` 的
3 个字段在 Task 6 定义、Task 8 填充两处一致。

<!-- PLAN-END -->
