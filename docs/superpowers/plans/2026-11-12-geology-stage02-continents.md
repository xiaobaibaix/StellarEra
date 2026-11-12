# 地质流水线阶段 2（板块与陆块）实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development. Steps 用 `- [ ]` 跟踪。
> 本计划的终点是**用户验收点**：大陆做完就停，阶段 3 及以后等用户测过再排。

**Goal:** 让测试星球从「纯球 + 圈层」变成「有板块、有大陆、有洋盆」的星球：阶段 2 烘焙出
构造框架（板块 / 边界 / 地壳年龄）与宏观高程 `ElevationBase`，快照与求值核接入，PIE 里能看见海岸线、
能挖到大陆与洋底两种地壳。

**Architecture:** 沿用 M1 的三层：烘焙期（`Geo/Bake`，可慢）→ 不可变快照（`Geo/Snapshot`，无 UObject）
→ 求值期 inline 核。阶段 2 是**第一个改密度的阶段**，因此它第一次真正检验「快照值域 / 最低地表半径 /
三入口同源」这三条契约。

**Tech Stack:** UE 5.8.3 C++、VoxelFree 插件（只读）、UE Automation Test、编辑器 MCP Python。

**Spec:** `docs/superpowers/specs/2026-11-12-geology-pipeline-design.md` §5 §7 §9 §12；
阶段细节 `docs/superpowers/specs/2026-11-12-geology-pipeline/02-plates.md`；
分辨率基准 `00-pipeline.md` §3.0。

## Global Constraints

- `R = 40000` 体素、`VoxelSize = 50 cm`、`FaceRes = 1024`（macro 场 256）。所有长度写**体素**，不写 `k·R`。
- 目录依赖：`Geo/Core`、`Geo/Snapshot` 只许 include Core / Math，禁止 Engine / CoreUObject / Voxel；
  `Geo/Bake` 另可 include `Geo/Core` `Geo/Snapshot`，禁止 World / Actor / Component / Voxel；
  `Geo/Assets` 禁止 Voxel。`Src/Tests/PlanetGeoDependencyTests.cpp` 自动扫。
- 求值期（`Snapshot/` 里的 `FORCEINLINE`、`Core` 采样函数）无虚函数、无分配、无锁、无日志。
- 确定性：随机只来自 `FGeoRng` / `GeoHash`，禁用 `FMath::Rand*`；`FGeoField2D::Fill` 每个 texel 只写自己，
  单线程与并行必须逐位一致。
- 测试放 `Source/StellarEraPlanet/Src/Tests/`，名字前缀 `StellarEra.Planet.Geo.`，
  标志 `EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter`，整文件包在
  `#if WITH_DEV_AUTOMATION_TESTS`。
- 含中文的 `.h/.cpp` 存 **UTF-8 with BOM**；注释只写「为什么」，单处 1–3 行。
- 不改 `Plugins/VoxelFree`；不改旧 `Terrain/` `Hydrology/` `Minerals/`。
- 提交带 pathspec：`git add -- <paths> && git commit -m "…" -- <paths>`；信息末尾
  `Co-Authored-By: Claude Code <noreply@anthropic.com>`。
- **构建**（先关编辑器；cwd 在项目外）：
  ```bash
  cd /f/workspace && "D:/Application/Epic Games/UE_5.8/Engine/Binaries/ThirdParty/DotNet/10.0/win-x64/dotnet.exe" \
    "D:/Application/Epic Games/UE_5.8/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.dll" \
    StellarEraEditor Win64 Development -Project="F:/workspace/ue/StellarEra/StellarEra.uproject" -WaitMutex -FromMsBuild 2>&1 | tail -5
  ```
  期望 `Result: Succeeded`。报 `Live Coding is active` = 编辑器没关。
- **测试**（编辑器开着也能跑）：
  ```bash
  "D:/Application/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "F:/workspace/ue/StellarEra/StellarEra.uproject" \
    -ExecCmds="Automation RunTests <Filter>" -TestExit="Automation Test Queue Empty" \
    -unattended -nopause -nullrhi -nosplash -stdout -abslog="$TEMP/geo_test.log" > /dev/null 2>&1; \
  grep -a "Test Completed" "$TEMP/geo_test.log"
  ```
  新测试文件必须先构建再跑，输出为空 ≠ 通过。

## 本阶段定死的决定（不要再自行推导）

### 尺度表（体素；`0.5 m/体素`，物理尺寸来自 `00-pipeline.md` §3.0）

| 量 | 体素 | 物理 | 备注 |
|---|---|---|---|
| `A_up` 陆台高 | 240 | 120 m | 大陆台面高出海平面 |
| `A_down` 洋盆深 | 480 | 240 m | `2·A_up` |
| `w_s` 陆坡宽 | 240 | 120 m | FaceRes=1024 时约 4 texel，海岸线可辨 |
| `amp` 板块风格 | 120 | 60 m | 低频起伏幅度 |
| `Δ` 板块偏移 | ±48 | ±24 m | `0.2·A_up` |
| 陆核半径 | `α·0.12R` | — | `α ∈ [0.5, 6]` 由陆地占比反解（附录上限 2.0 在 R=40000 下不够） |
| 核间最小中心角 | `0.25R` | — | 弦距近似 |
| 最大高程绝对值 | ≈ 890 | 445 m | `A_up+A_down+amp·1.4+|Δ|`，值域上界按此保守取 |

### 场分辨率

| 场 | 分辨率 | 理由 |
|---|---|---|
| `Elevation`（进快照） | `FaceRes` = 1024 | 海岸线要 4 texel 宽；快照 25 MB，全快照 ≈ 27 MB < 60 MB 预算 |
| 其余阶段 2 辅助场（`PlateId` `BoundaryType` `BoundaryDistance` `CrustAge` `ContinentalMask` `ContinentalGrad` `CrustType` `CrustThickness`） | `MacroRes = max(32, FaceRes/4)` = 256 | 全是宏观量，256 已是 245 体素/texel；只给阶段 3+ 用，**不进快照** |
| `StyleHeight` | `MacroRes` | 风格噪声基频是「每半球 1.5 个波长」，256 足够；求值时双线性取 |

### 与附录 `02-plates.md` 的差异（本计划为准，Task 6 回写附录）

1. **尺度**：附录的 `clamp(k·R, floor, cap)` 在 R=40000 全部撞 cap，改用上表的物理尺寸。
2. **接缝单位分解**：附录的 `w_p = clamp(distToOutside_p / w)` 需要每块板块「到自身边界距离」，
   三节点处 `Σw` 会塌。改为 **softmax 混合**：
   `W_p = exp(−(d_p − d_min)/w_R) / Σ_q exp(−(d_q − d_min)/w_R)`，`ΣW_p ≡ 1` 恒成立（分母即归一化），
   三节点自动三块共担。成本是每 texel N 次 `exp`，只在 `MacroRes` 上做一次。
3. **板块边界自然度**：Voronoi 之前先做 **domain warp**（`dir' = normalize(dir + WarpAmp·fBm3(dir))`），
   否则边界是完美圆弧。
4. **`CrustAge`**：真实年龄要积板块运动，第一版取 `clamp(到边界距离 / (AgeScaleRatio·R), 0, 1)`，
   陆区再抬到 `≥ 0.75`。验收「从洋中脊对称递减」按这个近似判，阶段 3 要更真再细化。
5. **`Alpha` 上限**：附录 `[0.5, 2.0]`。R=40000 时 12 个核按 `α=2` 只覆盖约 7% 面积，到不了 30%，
   **上限放到 6.0**，由二分收敛。

### 快照增量（只有两项）

```cpp
FGeoField2D Elevation;              // 体素；SurfaceRadius = R0 + Elevation.Sample(Dir)
float CrustSplitElevation = -240.f; // 低于它算洋壳岩性（= −A_down/2）
```
`MinSurfaceRadius` / `MaxSurfaceRadius` 由 `Elevation` 实测 min/max 加 `R0` 得出（实测比保守表更紧）。

## 文件结构

| 文件 | 职责 |
|---|---|
| `Src/Geo/Bake/PlanetRecipeConfig.h` | 追加 `FPlanetPlateParams`（Task 1） |
| `Src/Geo/Bake/GeoPlates.h/.cpp` | 板块框架：加权 Voronoi、domain warp、陆核铺点、边界类型表（Task 2） |
| `Src/Geo/Bake/PlanetBakeTypes.h` | 追加 `EPlanetField` 三值 + 9 张宏观场 + `PlateFrame` + `LandAreaFrac`/`MinElevation`/`MaxElevation`（Task 3） |
| `Src/Geo/Bake/Stages/Stage02Plates.h/.cpp` | 阶段 2 烘焙（Task 3） |
| `Src/Geo/Core/GeoLithology.h` | 追加 `ContinentalCrust` / `OceanicCrust`（Task 4） |
| `Src/Geo/Snapshot/PlanetSnapshot.h/.cpp` | 追加 `Elevation` / `CrustSplitElevation`，哈希与尺寸（Task 4） |
| `Src/Geo/Snapshot/PlanetShapeEval.h` | `SurfaceRadius` 用 `Elevation`（Task 4） |
| `Src/Geo/Snapshot/PlanetMaterialEval.h` | 地壳层按高程细分陆壳 / 洋壳（Task 4） |
| `Src/Geo/Bake/PlanetBakePipeline.cpp` | `MakeDefault` 加阶段 2；冻结 `Elevation` 与上下界（Task 4） |
| `Src/Geo/Assets/PlanetRecipe.h/.cpp` | 暴露可调参数（Task 6） |
| `Src/Tests/PlanetGeoPlateFrameTests.cpp` | Task 2 |
| `Src/Tests/PlanetGeoStage02Tests.cpp` | Task 3 |
| `Src/Tests/PlanetGeoEvalStage02Tests.cpp` | Task 4 |
| `Src/Tests/PlanetGeoStage02AcceptanceTests.cpp` | Task 5（R=40000 全尺寸） |

---

## Task 1：配方参数 `FPlanetPlateParams`

**Files:** Modify `Src/Geo/Bake/PlanetRecipeConfig.h`；Modify `Src/Tests/PlanetGeoRecipeTests.cpp`

**Interfaces (Produces):** `FPlanetRecipeConfig::Plates`（`FPlanetPlateParams`），Task 2/3/6 全部读它。

- [ ] **Step 1：加结构体**（放在 `FPlanetSphereParams` 之后）：

```cpp
/** 阶段 2 的尺度一律是体素：按 00-pipeline.md §3.0 的物理尺寸，不再写 k·R（R=40000 下 clamp 全撞上限）。 */
struct FPlanetPlateParams
{
	int32 PlateCount = 7;                 // 3–15；> 15 处处是山，< 3 出不了三节点
	float PlateWeightMin = 0.6f;          // 加权 Voronoi 的权重区间，乘性才出得了巨型板块
	float PlateWeightMax = 1.8f;
	float WarpAmp = 0.06f;                // 方向单位（≈弧度）：不做 domain warp 边界是完美圆弧
	float ContinentalCoreMin = 2.f;
	float ContinentalCoreMax = 4.f;
	float MixedCoreMin = 1.f;
	float MixedCoreMax = 2.f;
	float MicroCoreChance = 0.15f;        // 大洋型板块出现微陆核的概率
	float CoreRadiusRatio = 0.12f;        // × R，再乘二分反解出的 α
	float CoreMinSpacingRatio = 0.25f;    // × R，核间最小中心角
	float SigmaRatio = 0.55f;             // σ = SigmaRatio · α · r
	float AlphaMin = 0.5f;
	float AlphaMax = 6.0f;                // 附录写的 2.0 在 R=40000 下达不到 30% 陆地占比
	float TargetLandFrac = 0.30f;
	float LandTableVoxels = 240.f;        // A_up
	float OceanBasinVoxels = 480.f;       // A_down
	float SlopeWidthVoxels = 240.f;       // w_s
	float StyleAmpVoxels = 120.f;
	float StyleNoiseWavelengths = 1.5f;   // 每半球波长数，与 R 无关
	float AgeScaleRatio = 0.25f;
	float BoundaryBandRatio = 0.02f;      // BoundaryType 只在距边界 2%R 内非零
};
```

- [ ] **Step 2：字段与哈希**：`FPlanetRecipeConfig` 加 `FPlanetPlateParams Plates;`；`Hash()` 末尾按
  `PlateCount` 与全部 `float` 字段逐个 `Combine`，写法照 Sphere 那段。
  **`AlphaMin/AlphaMax` 也要进哈希**（改搜索区间会改结果）。

- [ ] **Step 3：测试**（追加到 `PlanetGeoRecipeTests.cpp` 的 RunTest 内）：

```cpp
	// 阶段 2 参数必须进哈希，否则改了大陆形状快照哈希不变、存档校验失效。
	FPlanetRecipeConfig PlateA, PlateB = PlateA;
	PlateB.Plates.LandTableVoxels = PlateA.Plates.LandTableVoxels + 10.f;
	TestTrue(TEXT("陆台高进哈希"), PlateA.Hash() != PlateB.Hash());
	FPlanetRecipeConfig PlateC = PlateA;
	PlateC.Plates.AlphaMax = 3.f;
	TestTrue(TEXT("α 搜索区间进哈希"), PlateA.Hash() != PlateC.Hash());
	TestEqual(TEXT("默认板块数 7"), PlateA.Plates.PlateCount, 7);
```

- [ ] **Step 4：构建 + 跑 `StellarEra.Planet.Geo.Recipe`，提交**
  （`git add -- Source/StellarEraPlanet/Src/Geo/Bake/PlanetRecipeConfig.h Source/StellarEraPlanet/Src/Tests/PlanetGeoRecipeTests.cpp`）

---

## Task 2：板块框架 `GeoPlates`

**Files:** Create `Src/Geo/Bake/GeoPlates.h`, `Src/Geo/Bake/GeoPlates.cpp`, `Src/Tests/PlanetGeoPlateFrameTests.cpp`

**Interfaces (Produces):** Task 3 用 `FGeoPlateFrame`、`GeoPlates::Build`、`GeoPlates::WarpDirection`、`GeoPlates::NearestTwo`。

- [ ] **Step 1：头文件**

```cpp
// 阶段 2 的构造框架：加权 Voronoi 板块 + 陆核铺点 + 板块对边界类型。
// 只在烘焙期存在；求值期不许重算 Voronoi（每点 7 次距离就是预算的 3 倍）。
#pragma once
#include "CoreMinimal.h"
#include "PlanetRecipeConfig.h"

enum class EGeoPlateKind : uint8 { Continental, Oceanic, Mixed };

struct FGeoPlate
{
	FVector3f Center = FVector3f::UpVector;
	float Weight = 1.f;
	EGeoPlateKind Kind = EGeoPlateKind::Oceanic;
	float StyleOffset = 0.f;   // 体素
	float StyleAmp = 0.f;      // 体素
	uint64 StyleSeed = 0;
};

struct FGeoContinentCore
{
	FVector3f Center = FVector3f::UpVector;
	float RadiusRatio = 0.f;   // 占 R，未乘 α；α 在阶段里二分
};

struct FGeoPlateFrame
{
	TArray<FGeoPlate> Plates;
	TArray<FGeoContinentCore> Cores;
	/** 上三角对类型：角标 A*N+B（A<B），值 1…5，见 02-plates.md 的 BoundaryType 字典。 */
	TArray<uint8> PairBoundaryType;
	uint64 WarpSeed = 0;

	int32 Num() const { return Plates.Num(); }
	uint8 BoundaryTypeBetween(int32 A, int32 B) const;
	uint64 Hash() const;
};

namespace GeoPlates
{
	/** 只用 Config.Seed 与 Plates 参数；连调两次必须逐位一致。 */
	FGeoPlateFrame Build(const FPlanetRecipeConfig& Config);

	/** 板块边界扰动：不加 warp 时 Voronoi 边界是完美圆弧，一眼假。 */
	FVector3f WarpDirection(const FGeoPlateFrame& Frame, const FVector3f& Dir, float WarpAmp);

	/** 加权距离 = 弦距 / 权重；输出最近与次近两块及到的加权距离（方向单位）。 */
	void NearestTwo(const FGeoPlateFrame& Frame, const FVector3f& Dir,
		int32& OutNear, int32& OutSecond, float& OutNearDist, float& OutSecondDist);
}
```

- [ ] **Step 2：实现要点（照此写，不许换算法）**

  - RNG：`FGeoRng Rng = FGeoRng::Derive(Config.Seed, 2, GeoHash::Str("plates"))`；`WarpSeed = Rng.NextU64()`。
  - **种子点**：Fibonacci 球面 `N = clamp(PlateCount, 3, 15)`，`z = 1 − 2(i+0.5)/N`、`theta = i·2.399963`；
    再按抖动幅度 `0.4·sqrt(4π/N)`（≈半个点间距）随机旋转方向。
  - **权重**：`w = Min · pow(Max/Min, Rng.NextFloat())`（均匀抽 log 空间才出得了巨型板块）。
  - **板块类型**：按 `大陆 : 大洋 : 混合 = 3 : 3 : 1` 分配，`Mixed = N − 2·floor(N·3/7)`，顺序用 `Rng` 洗牌。
  - **陆核**：大陆型 `Range(ContinentalCoreMin, ContinentalCoreMax)` 取整、混合型 `MixedCoreMin/Max`、
    大洋型 `MicroCoreChance` 概率 1 个。每个核拒绝采样（最多 64 次）方向：与已有核**弦距 ≥ CoreMinSpacingRatio**；
    半径 `CoreRadiusRatio · Range(0.85, 1.15)`（大洋微核固定 `CoreMinSpacingRatio/5`）。
    **不要求核落在自己板块内**（真实大陆横跨缝合线）。全局核数超 16 停止加核。
  - **风格参数**：每块板块 `StyleOffset = Range(−0.2, 0.2)·LandTableVoxels`、
    `StyleAmp = StyleAmpVoxels·Range(0.6, 1.4)`、`StyleSeed = Rng.NextU64()`。
  - **板块对类型**：`u = float(GeoHash::Mix64(WarpSeed ^ (uint64(A) << 32 | uint32(B))) >> 40) / 2^24`；
    `u < 0.7` 汇聚（两端都陆 → 3、都洋 → 2、否则 1），`u < 0.9` 离散 4，否则转换 5。
  - **WarpDirection**：三个不同种子各取一个分量的 `Fbm3(WarpSeed ^ k, Dir*1.5, 2)`，
    返回 `(Dir + WarpAmp*Off).GetSafeNormal()`；零长度方向直接返回 `Dir`。
  - **NearestTwo**：`d_p = |Dir − Center_p| / Weight_p`，单循环同时维护最小与次小，平手按索引小者优先（稳定）。
    `Plates.Num() == 0` 时输出 0 并把距离置 `TNumericLimits<float>::Max()`。

- [ ] **Step 3：测试**（`StellarEra.Planet.Geo.PlateFrame`）

```cpp
	FPlanetRecipeConfig C;                       // 默认 R=40000、PlateCount=7、Seed=1337
	const FGeoPlateFrame A = GeoPlates::Build(C);
	const FGeoPlateFrame B = GeoPlates::Build(C);
	TestEqual(TEXT("板块数"), A.Num(), 7);
	TestTrue(TEXT("同配方两次构建逐位一致"), A.Hash() == B.Hash());
	TestTrue(TEXT("板块种子不全同"), A.Plates[0].Center != A.Plates[1].Center);
	TestTrue(TEXT("核数在 6–16"), A.Cores.Num() >= 6 && A.Cores.Num() <= 16);
	TestTrue(TEXT("核半径 ≥ 0.05R"), A.Cores[0].RadiusRatio >= 0.05f);
	// 覆盖性：任意方向都落到一块板块，且每块都被访问到；最大板块占比不能吞掉整颗星
	TArray<int32> Hits; Hits.SetNumZeroed(A.Num());
	FGeoRng Rng = FGeoRng::Derive(3, 0, 0);
	int32 MaxHits = 0;
	for (int32 i = 0; i < 4000; ++i)
	{
		const float Z = Rng.Range(-1.f, 1.f), Phi = Rng.Range(0.f, 2.f * PI), S = FMath::Sqrt(1.f - Z * Z);
		const FVector3f D(S * FMath::Cos(Phi), S * FMath::Sin(Phi), Z);
		int32 Near, Second; float DN, DS;
		GeoPlates::NearestTwo(A, D, Near, Second, DN, DS);
		TestTrue(TEXT("最近板块下标合法"), Near >= 0 && Near < A.Num());
		TestTrue(TEXT("次近距离不小于最近"), DS >= DN);
		++Hits[Near];
	}
	for (int32 p = 0; p < A.Num(); ++p)
	{
		TestTrue(FString::Printf(TEXT("板块 %d 有覆盖"), p), Hits[p] > 0);
		MaxHits = FMath::Max(MaxHits, Hits[p]);
	}
	const float MaxShare = float(MaxHits) / 4000.f;
	TestTrue(FString::Printf(TEXT("最大板块占比 %.2f ∈ [0.10, 0.55]"), MaxShare), MaxShare > 0.10f && MaxShare < 0.55f);
	// 边界类型都在 1–5
	bool bTypes = true;
	for (int32 p = 0; p < A.Num(); ++p) for (int32 q = p + 1; q < A.Num(); ++q)
	{ const uint8 T = A.BoundaryTypeBetween(p, q); bTypes &= (T >= 1 && T <= 5); }
	TestTrue(TEXT("边界类型都在 1–5"), bTypes);
	// warp 只改方向不改长度
	const FVector3f W = GeoPlates::WarpDirection(A, FVector3f(0.f, 0.f, 1.f), C.Plates.WarpAmp);
	TestTrue(TEXT("warp 输出是单位向量"), FMath::Abs(W.Size() - 1.f) < 1e-4f);
	TestTrue(TEXT("warp 确实挪了方向"), !W.Equals(FVector3f(0.f, 0.f, 1.f), 1e-3f));
```

- [ ] **Step 4：构建 + 跑 `StellarEra.Planet.Geo.PlateFrame`，提交三个文件**

---

## Task 3：阶段 2 烘焙 `Stage02Plates`

**Files:** Modify `Src/Geo/Bake/PlanetBakeTypes.h`；Create `Src/Geo/Bake/Stages/Stage02Plates.h/.cpp`；
Create `Src/Tests/PlanetGeoStage02Tests.cpp`

**Interfaces (Produces):** `FStage02Plates`（`GetStageIndex()=2`）；`FPlanetBakeFields` 的
`PlateId/BoundaryType/BoundaryDistance/CrustAge/ContinentalMask/ContinentalGrad/CrustType/CrustThickness/StyleHeight/Elevation`
与 `PlateFrame/LandAreaFrac/MinElevation/MaxElevation`。

- [ ] **Step 1：`PlanetBakeTypes.h` 追加**

```cpp
enum class EPlanetField : uint32
{
	None        = 0,
	Radial      = 1 << 0,
	MohoWobble  = 1 << 1,
	Primitives  = 1 << 2,
	Plates      = 1 << 3,   // PlateId / BoundaryType / BoundaryDistance / CrustAge
	Continental = 1 << 4,   // ContinentalMask / ContinentalGrad / CrustType / CrustThickness
	Elevation   = 1 << 5,   // StyleHeight / ElevationBase
};

struct FPlanetBakeFields
{
	FGeoProfile Radial;
	FGeoField2D MohoWobble;
	TArray<FGeoPrimitive> Primitives;

	// 阶段 2 的宏观场一律 MacroRes（= FaceRes/4）；只有 Elevation 是全分辨率，它进快照。
	FGeoPlateFrame PlateFrame;
	FGeoField2D PlateId, BoundaryType, BoundaryDistance, CrustAge;
	FGeoField2D ContinentalMask, ContinentalGrad, CrustType, CrustThickness;
	FGeoField2D StyleHeight, Elevation;
	float LandAreaFrac = 0.f;
	float MinElevation = 0.f, MaxElevation = 0.f;
};
```
（文件顶部 `#include "GeoPlates.h"`。）

- [ ] **Step 2：阶段头文件**

```cpp
// 阶段 2：板块、陆块、宏观高程。第一个改密度的阶段。
#pragma once
#include "CoreMinimal.h"
#include "PlanetStage.h"

class FStage02Plates final : public IPlanetStage
{
public:
	virtual FName GetName() const override { return TEXT("Stage02Plates"); }
	virtual uint32 GetStageIndex() const override { return 2; }
	virtual EPlanetField GetReads() const override { return EPlanetField::Radial; }
	virtual EPlanetField GetWrites() const override
	{
		return EPlanetField::Plates | EPlanetField::Continental | EPlanetField::Elevation;
	}
	virtual void Bake(FPlanetBakeContext& Ctx) const override;
};
```

- [ ] **Step 3：实现（照此结构写）**

  辅助函数放在 `.cpp` 的匿名命名空间：

```cpp
	// 单核高斯团：D 为到核的有符号距离（方向单位），Sigma 已乘 α。
	FORCEINLINE float CoreContribution(float D, float Sigma) { return FMath::Exp(-(D * D) / (2.f * Sigma * Sigma)); }

	/** 陆地占比：64×64/面 上数 M>0.5；只看二分方向，成本可忽略。 */
	float LandFracAt(const FGeoPlateFrame& Frame, float Alpha, float SigmaRatio, bool bSingleThread);
```

  `LandFracAt` 内部建 `FGeoField2D Probe; Probe.Init(64);` 再 `Fill`（`bSingleThread` 透传），
  统计 6 面 64² 里 `M > 0.5` 的占比。

  `Bake` 顺序：

  1. `Ctx.Fields.PlateFrame = GeoPlates::Build(Ctx.Config);`；核为空 → `Report.Errors` 加一行并 `return`。
  2. **二分 α**：`Lo=AlphaMin, Hi=AlphaMax`，10 次：`Mid`、`F = LandFracAt(Mid)`，
     `F < Target → Lo = Mid`，否则 `Hi = Mid`。取 `Alpha = 0.5f*(Lo+Hi)`，
     `Ctx.Fields.LandAreaFrac = LandFracAt(Alpha, …)`（用最终 α **重算**，别用最后一次迭代值）。
     误差 `> 0.05` 时 `Report.Errors` 加「陆地占比无法收敛」但**不 return**（用户仍要看星球）。
  3. **M（全分辨率）**：`Fill` 每 texel `Σ CoreContribution(|D−c_i| − α·r_i, SigmaRatio·α·r_i)`，`clamp(0,1)`。
  4. **梯度（全分辨率）**：对每 texel 取 U/V 方向 ±1 texel 的邻居方向（用 `FGeoField2D::FaceToDir`，
     别用 `TexelDir`，边界处会钳），`Grad = max(|ΔM|/L)`，`L` 取两方向弦长、下限 `1e-6`。
  5. **`h_base`（全分辨率）**：
     ```cpp
     const float wS = Config.Plates.SlopeWidthVoxels / float(Config.Radius);
     const float T  = (M - 0.5f) / FMath::Max(Grad, 1e-4f);
     const float Up = A_up * FMath::SmoothStep(0.f, wS,  T);
     const float Dn = A_down * FMath::SmoothStep(0.f, wS, -T);
     Elevation = Up - Dn;
     ```
  6. **`StyleHeight`（MacroRes）**：每 texel 对全部板块算加权距离 `d_p`，`dmin = min`，
     `W_p = exp(−(d_p−dmin)/wR)`，`H = Σ W_p·(Δ_p + amp_p·Fbm3(StyleSeed_p, D·(2·StyleNoiseWavelengths), 2)) / Σ W_p`；
     `wR = SlopeWidthVoxels / R`（接缝过渡与陆坡同尺度）。
  7. **`Elevation` 加风格**：`Elevation.Fill(D → h_base(D) + StyleHeight.Sample(D))`。
  8. **辅助场（MacroRes）**，一次 `Fill`：先 `WarpDirection` → `NearestTwo`：
     - `PlateId = Near`
     - `b = 0.5*(d2−d1)`；`BoundaryDistance = b`
     - `BoundaryType = b > BoundaryBandRatio ? 0 : BoundaryTypeBetween(Near, Second)`
     - `CrustAge = clamp(b/AgeScaleRatio, 0, 1)`，陆区（`M>0.5`）取 `max(·, 0.75+0.25·M)`
     - `ContinentalMask = M`、`ContinentalGrad = Grad`（从全分辨率 `Sample`）
     - `CrustType`：`E > 0` → 1 陆壳；`E < −A_down/2` → 0 洋壳；否则 2 陆坡
     - `CrustThickness = clamp(0.1 + 0.9·M, 0, 1)`
  9. **值域**：扫 `Elevation` 全部 texel 求 min/max → `Ctx.Fields.MinElevation/MaxElevation`。

- [ ] **Step 4：测试**（`StellarEra.Planet.Geo.Stage02`；直接构造 `FPlanetBakeFields` + `FPlanetBakeContext`，
  不经管线——快照要到 Task 4 才接）

```cpp
	FPlanetRecipeConfig C; C.FaceRes = 256;      // 测速度；全尺寸在 Task 5
	FPlanetBakeFields Fields; FPlanetBakeReport R1;
	FPlanetBakeContext C1{ C, Fields, FGeoRng::Derive(C.Seed, 2, 0), R1 };
	FStage02Plates().Bake(C1);
	TestFalse(TEXT("无错误"), R1.HasErrors());
	TestEqual(TEXT("Elevation 全分辨率"), Fields.Elevation.Res, 256);
	TestEqual(TEXT("宏观场 = FaceRes/4"), Fields.PlateId.Res, 64);
	TestTrue(TEXT("陆地占比 0.30 ± 2%"), FMath::Abs(Fields.LandAreaFrac - 0.30f) <= 0.02f);
	// M 值域、高程保界
	TestTrue(TEXT("M ∈ [0,1] 且不是常数"), MinM >= 0.f && MaxM <= 1.f && MaxM - MinM > 0.3f);
	const float Limit = C.Plates.LandTableVoxels + C.Plates.OceanBasinVoxels
		+ 1.4f * C.Plates.StyleAmpVoxels + 0.2f * C.Plates.LandTableVoxels;
	TestTrue(TEXT("高程在保守界内"), FMath::Abs(MinE) <= Limit && FMath::Abs(MaxE) <= Limit);
	TestTrue(TEXT("记录的值域与实际一致"), FMath::Abs(Fields.MinElevation - MinE) < 1.f);
	// 穿越次数：8 条大圆 × 512 步
	TestTrue(FString::Printf(TEXT("穿越次数 ∈ [2,12]（最差 %d）"), WorstCrossings), WorstCrossings >= 2 && WorstCrossings <= 12);
	// 海岸线：M 穿过 0.5 处 |Elevation| < 0.3·A_up
	// 年龄场：洋壳 texel 按 BoundaryDistance 分两半，远的一半 CrustAge 均值更大
	// 确定性：连烘两次场哈希一致；单线程 vs 并行逐位一致
```

  穿越次数的实现：8 条大圆各用一对正交基，`Dir(t) = B1·cos + B2·sin`，比较相邻 `Sample` 的符号，
  环回不重复计。**阈值 12 是 R=40000 的实测标定**（附录的 6–10 是按小星球推的），原因写进测试注释。
  MinM/MaxM/MinE/MaxE 这四个量要在上面就地扫场求出（`ContinentalMask` / `Elevation` 全 texel）。

- [ ] **Step 5：构建 + 跑 `StellarEra.Planet.Geo.Stage02`，提交三个文件**

---

## Task 4：快照与求值接入

**Files:** Modify `Src/Geo/Core/GeoLithology.h`、`Src/Geo/Snapshot/PlanetSnapshot.h/.cpp`、
`Src/Geo/Snapshot/PlanetShapeEval.h`、`Src/Geo/Snapshot/PlanetMaterialEval.h`、
`Src/Geo/Bake/PlanetBakePipeline.cpp`；Create `Src/Tests/PlanetGeoEvalStage02Tests.cpp`

**Interfaces (Produces):** `FPlanetSnapshot::Elevation/CrustSplitElevation`；非平凡 `SurfaceRadius`；
`PlanetMaterialEval::Sample` 返回 `ContinentalCrust` / `OceanicCrust`。

- [ ] **Step 1：岩性**：`GeoLithology` 加 `ContinentalCrust = 7`、`OceanicCrust = 8`、`Count = 9`；
  `DebugColor` 数组补两色（陆壳 `FColor(90, 120, 80)`、洋壳 `FColor(40, 70, 130)`，
  与上地幔 `(70,110,60)` 拉开）。

- [ ] **Step 2：快照**

```cpp
	/** 海平面以上的地表高程（体素）；SurfaceRadius = R0 + Elevation.Sample(Dir)。 */
	FGeoField2D Elevation;
	/** 低于它算洋壳岩性；取 −OceanBasin/2，即洋盆深度的一半。 */
	float CrustSplitElevation = -240.f;
```
  `FinalizeHash` 追加 `Elevation.Hash()` 与 `GeoHash::Float(CrustSplitElevation)`；
  `GetAllocatedSize` 追加 `Elevation.GetAllocatedSize()`。

- [ ] **Step 3：求值**

```cpp
	FORCEINLINE double SurfaceRadius(const FPlanetSnapshot& S, const FVector3f& Dir)
	{
		return S.R0 + (S.Elevation.Res > 0 ? double(S.Elevation.Sample(Dir)) : 0.0);
	}
```
  `PlanetMaterialEval::Sample` 在 `LithologyAt` 之后加：

```cpp
		// 地壳层不按深度分岩性，按海平面分：陆壳 / 洋壳是两套不同的成矿背景。
		if (Out.Lithology == GeoLithology::Crust)
		{
			const float E = S.Elevation.Res > 0 ? S.Elevation.Sample(Dir) : 0.f;
			Out.Lithology = E < S.CrustSplitElevation ? GeoLithology::OceanicCrust : GeoLithology::ContinentalCrust;
		}
```

- [ ] **Step 4：管线**

```cpp
	Snap->Elevation = MoveTemp(Fields.Elevation);
	Snap->CrustSplitElevation = -0.5f * Config.Plates.OceanBasinVoxels;
	Snap->MinSurfaceRadius = Config.Radius + Fields.MinElevation;
	Snap->MaxSurfaceRadius = Config.Radius + Fields.MaxElevation;
```
  `MakeDefault()` 加 `P.AddStage(MakeUnique<FStage02Plates>());`。
  **顺序**：`Min/MaxSurfaceRadius` 必须在 `FinalizeHash()` 之前赋值。
  只有阶段 1 时（`Fields.Elevation.Res == 0`）沿用 `R0` 作上下界——两条路径都要留。

- [ ] **Step 5：测试**（`StellarEra.Planet.Geo.Eval.Stage02`，`Config.FaceRes = 256`）

```cpp
	// 三入口同源：SurfaceRadius 与 Value 的零点一致
	for (100 个方向) {
		const double Rs = PlanetShapeEval::SurfaceRadius(*Snap, Dir);
		const float V = PlanetShapeEval::Value(*Snap, FVector3d(Dir) * Rs);
		TestTrue(TEXT("地表处密度为 0"), FMath::Abs(V) < 0.05f);
	}
	// 值域包含实测：128³ 盒子里随机 1e4 点，min/max 必须落在 ValueRange 内
	// 高程非平凡：MinSurfaceRadius < R0 < MaxSurfaceRadius
	// 岩性：Elevation > +50 的方向在地表下 20 体素 = ContinentalCrust；
	//       Elevation < −300 的方向 = OceanicCrust（遍历 4000 方向挑样本）
	// 哈希：改 Plates.TargetLandFrac 后快照哈希必须变
```

- [ ] **Step 6：构建 + 跑 `StellarEra.Planet.Geo.Eval.Stage02` + 回归
  `StellarEra.Planet.Geo.Stage01` / `.Snapshot` / `.Stage02`，提交**

  `PlanetGeoSnapshotTests.cpp` 里若有「`SurfaceRadius == R0`」的断言，改成按 `Elevation` 断言。

---

## Task 5：全尺寸验收（R=40000 / FaceRes=1024）

**Files:** Create `Src/Tests/PlanetGeoStage02AcceptanceTests.cpp`

- [ ] **Step 1：测试内容**

```cpp
	FPlanetRecipeConfig C;                    // 默认就是 R=40000 / FaceRes=1024 / Seed=1337
	FPlanetBakeReport Report;
	const double T0 = FPlatformTime::Seconds();
	const TSharedPtr<const FPlanetSnapshot> Snap = FPlanetBakePipeline::MakeDefault().Bake(C, Report);
	const double Seconds = FPlatformTime::Seconds() - T0;
	TestTrue(TEXT("全尺寸烘焙成功"), Snap.IsValid());
	AddInfo(FString::Printf(TEXT("烘焙 %.2f s，快照 %.1f MB"), Seconds, Snap->GetAllocatedSize() / 1048576.0));
	TestTrue(TEXT("烘焙 ≤ 10 s"), Seconds <= 10.0);
	TestTrue(TEXT("快照 ≤ 60 MB"), Snap->GetAllocatedSize() <= 60ull * 1024 * 1024);
	TestEqual(TEXT("Elevation 全分辨率"), Snap->Elevation.Res, 1024);
	TestTrue(TEXT("地表上下界包住 R0"), Snap->MinSurfaceRadius < Snap->R0 && Snap->R0 < Snap->MaxSurfaceRadius);
	TestTrue(TEXT("高程带宽 < 15% R"), (Snap->MaxSurfaceRadius - Snap->MinSurfaceRadius) / Snap->R0 < 0.15);
	// 抽样 1e5 点：ValueRange 必须覆盖（附录 §6 验收 5）
	// 陆地占比：Elevation.Sample(Dir) > 0 的方向占比 ∈ [0.22, 0.38]
```

  陆地占比在快照侧没有 `M`，用 `Elevation.Sample(Dir) > 0` 统计——与 `M>0.5` 等价（`h_base` 在 `M=0.5` 处为 0），
  误差来自风格项，所以区间放宽到 `[0.22, 0.38]`。

- [ ] **Step 2：构建 + 跑 `StellarEra.Planet.Geo.Stage02Acceptance`，把烘焙秒数 / 快照 MB / 陆地占比
  记进 ledger**

- [ ] **Step 3：整包回归**

```bash
"…UnrealEditor-Cmd.exe" "…StellarEra.uproject" -ExecCmds="Automation RunTests StellarEra" \
  -TestExit="Automation Test Queue Empty" -unattended -nopause -nullrhi -nosplash -stdout -abslog="$TEMP/geo_all.log"
```
  期望成功数 ≥ 277 + 本阶段新增、0 失败；与改前基线比对，确认没有旧测试被新地形带坏。

- [ ] **Step 4：提交**

---

## Task 6：配方资产 + 文档回填

**Files:** Modify `Src/Geo/Assets/PlanetRecipe.h/.cpp`、`Src/Tests/PlanetGeoRecipeTests.cpp`、
`docs/superpowers/specs/2026-11-12-geology-pipeline/02-plates.md`、`Source/README.md`、
`docs/superpowers/specs/2026-11-12-geology-pipeline-design.md`（§7 M3 行）

- [ ] **Step 1：`UPlanetRecipe`** 暴露 6 个可调项（`PlateCount`、`TargetLandFrac`、`LandTableVoxels`、
  `OceanBasinVoxels`、`StyleAmpVoxels`、`SlopeWidthVoxels`）为独立 `UPROPERTY(EditAnywhere)`，
  `ToConfig()` 逐项赋值，其余走默认。
  **不要**给 `FPlanetPlateParams` 加 `USTRUCT`：那会把 CoreUObject 拖进 `Geo/Bake`，依赖扫描测试会挂。
  这条理由写进代码注释。

- [ ] **Step 2：测试**：6 个字段确实转进 `ToConfig()`；未暴露字段等于默认值。

- [ ] **Step 3：回写 `02-plates.md`**：文首插入「2026-11-12 R=40000 修订」，内容照本计划
  「与附录的差异」五条 + 尺度表，并注明「正文里的 `clamp(k·R,…)` 与 σ/α 区间已被本节取代」。

- [ ] **Step 4：`Source/README.md`** 子目录判据补一句：`Geo/Bake/Stages/` 是第二处三级目录
  （第一处 `Minerals/Geology/Distributions/`），新阶段一律进 `Stages/`。

- [ ] **Step 5：规格 §7 的 M3 行**改成「M3 = 阶段 3（宏观地形）+ 阶段 4 自动弧火山」，
  并注明「阶段 2 已提前完成（用户验收点）」。

- [ ] **Step 6：构建 + 跑 `StellarEra.Planet.Geo.Recipe`，提交**

---

## Task 7：编辑器资产 + 用户验收（**跑到这里就停**）

**Files:** Create `Content/Code/Planet/DA_GeoRecipe_Test.uasset`、`Content/Code/Maps/GeoTestMap.umap`

- [ ] **Step 1：MCP `execute_python_code`**（编辑器开着且在前台）：

```python
import unreal
factory = unreal.DataAssetFactory()
factory.set_editor_property('data_asset_class', unreal.PlanetRecipe.static_class())
asset = unreal.AssetToolsHelpers.get_asset_tools().create_asset('DA_GeoRecipe_Test', '/Game/Code/Planet', None, factory)
asset.set_editor_property('radius', 40000.0)
asset.set_editor_property('voxel_size_cm', 50.0)
asset.set_editor_property('face_res', 1024)
asset.set_editor_property('seed', 1337)
unreal.EditorAssetLibrary.save_loaded_asset(asset)

# 新空关卡：不要「复制后清空」，删 WorldDataLayers 再保存会触发 CanResolveDataLayers 断言
les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
les.new_level('/Game/Code/Maps/GeoTestMap')
actor = unreal.EditorLevelLibrary.spawn_actor_from_class(unreal.GeoTestPlanetActor, unreal.Vector(0, 0, 0))
actor.set_editor_property('recipe', asset)
actor.set_editor_property('is_spatially_loaded', False)   # WP 关卡里 spawn 的 Actor 默认空间加载 → 看不见
les.save_current_level()
```

  属性名以实际 `UPROPERTY` 为准（先读 `PlanetRecipe.h` / `GeoTestPlanetActor.h`）。

- [ ] **Step 2：PIE 验收清单**（写进给用户的说明，不自己判「好看」）：

  1. 进 PIE 后日志出现烘焙耗时行（≤ 10 s，编辑器不卡死）。
  2. 从 PlayerStart 起飞：大陆台面高于海平面、洋盆低下去，海岸线是一条带不是台阶。
  3. 开 `bDebugLithologyColor`：陆壳（绿）/ 洋壳（蓝）在地表分得清；往下挖仍是 6 层圈层。
  4. 地表无折角、无三角面亮点（`SurfaceRadius` 与 `Value` 同源）。
  5. 飞行帧率与 M1 相当（阶段 2 只多 3 次双线性采样）。
  6. 改 `Seed` 后重进 PIE：大陆形状明显不同，陆地占比不变。

- [ ] **Step 3：提交资产，向用户报告「阶段 2 完成，停在验收点」，附 PIE 清单与实测数字。
  不要再往下做阶段 3 / 4。**

---

## 完成判据

- Task 1–6 全部提交，`StellarEra.Planet.Geo.*` 全绿，整包回归无新增失败。
- 全尺寸：烘焙 ≤ 10 s、快照 ≤ 60 MB、陆地占比 30% ± 8%（快照口径）、穿越次数 2–12。
- 用户能在 `GeoTestMap` 里看见大陆、洋盆、海岸线，并挖到陆壳 / 洋壳两种岩性。
