# 地质阶段 4a 火山地形 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 新增阶段 4（`FStage04Magmatism`），在阶段 3 的最终高程上叠加层状火山 / 盾状火山链 / 溢流玄武岩 / 破火山口 / 火山渣锥，并输出火山点表与调试图层。

**Architecture:** 先放点（`Stage04::PlaceVolcanoes`，纯函数，读阶段 2/3 的场与板块框架），再把每座火山的剖面（`Stage04::Profile`）按包围角叠到全分辨率 `Elevation` 上：锥体与地形取 max、火口最后统一减。点表进 `FPlanetBakeFields` 与 `FPlanetSnapshot`（进哈希），宏观场只进调试快照。

**Tech Stack:** UE 5.8 C++，`StellarEraPlanet` 模块，UE Automation Test。

**Spec:** `docs/superpowers/specs/2026-11-15-geology-stage04a-volcanoes-design.md`

## Global Constraints

- 所有尺寸占半径比例：角度量用方向单位（单位球弦长），高度 = 比例 × `Cfg.Radius`（体素）。
- 每个配方参数带中文注释；`ToConfig` 透传；进 `FPlanetRecipeConfig::Hash()`。
- 注释只写「为什么」，单处 1–3 行。含中文的 `.h/.cpp` 存 UTF-8 with BOM。
- 随机流：`FGeoRng::Derive(Cfg.Seed, 4, GeoHash::Str("<用途>"))`，每种火山各一条，关掉一种不改变其它种。
- 构建前关编辑器；构建与测试命令见 `CLAUDE.md`「构建与测试」。提交一律 `git commit -- <paths>`。
- 新文件放 `Source/StellarEraPlanet/Src/Geo/...`，测试放 `Source/StellarEraPlanet/Src/Tests/`；Build.cs 已递归加子目录。
- 下文 `BUILD` 指（cwd 在项目外）：
  `cd F:/workspace/ue && "/d/Application/Epic Games/UE_5.8/Engine/Binaries/ThirdParty/DotNet/10.0/win-x64/dotnet.exe" "D:/Application/Epic Games/UE_5.8/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.dll" StellarEraEditor Win64 Development -Project="F:/workspace/ue/StellarEra/StellarEra.uproject" -WaitMutex -FromMsBuild`
  期望最后一行 `Result: Succeeded`。报 `LNK1104` = DLL 被卡死的编辑器锁住：把 `Binaries/Win64/UnrealEditor-StellarEraPlanet*.dll/.pdb` 改名为 `*.locked_<时间>` 再 BUILD。
- 下文 `TEST <过滤> <日志>` 指：
  `"/d/Application/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "F:/workspace/ue/StellarEra/StellarEra.uproject" -ExecCmds="Automation RunTests <过滤>" -TestExit="Automation Test Queue Empty" -unattended -nopause -nullrhi -nosplash -stdout -abslog="F:/workspace/ue/StellarEra/Saved/Logs/<日志>"`
  看结果：`grep -a "Result={" <日志> | grep -av Success; grep -a "Expected\|tests performed" <日志>`。

## 与规格的实施期差异（已对齐用户意图）

- 规格 §5「火山与阶段 3 地形取 max」→ 最终式 `E = max(E0, lerp(BaseElevation+Up, E0, SmoothStep(0.65,1,d))) − maxPit`：火山增量之间取 max，锥底贴地。
- 足印过渡带取 0.65–1.0：山高 0.05R，锥心与足印边缘的高差可达上千体素，窄带会把台阶留在足印内部。
- `VolcanoDelta` 语义 = 相对各自锥底 `BaseElevation` 的锥厚（`Up − Pit`），不是相对阶段 3 地面的净抬升；图层名相应改为「火山锥厚度（相对锥底）」。
- 五个宏观场（`VolcanoMask` / `VolcanicType` / `VolcanicAge` / `VolcanoActive` / `VolcanoDelta`）只在 `bKeepDebugFields` 时构建：`ExportDebugFields` 只在那时被调用，运行时烘焙不付 393k texel 的代价。
- 规格 §8.1 间距「每个都在 [0.5,1.6]×」→ 改为「最小 ≥ 0.65×、中位数 ∈ [0.8,1.9]×」：泊松盘随机顺序贪心的空隙上限是 2 倍接受半径，逐个卡区间会误报。
- 规格 §8.5「不低于阶段 3 减火口深」→ 减「火口深与破火山口深的较大者」。
- 破火山口沿用来源火山的剖面：`Chain >= 0`（热点盾状）用穹顶，否则用锥。点表不另加字段。

## 文件

| 文件 | 动作 | 职责 |
|---|---|---|
| `Geo/Core/GeoVolcano.h` | 新建 | `EGeoVolcanoKind`、`FGeoVolcano`（POD + Hash） |
| `Geo/Bake/PlanetRecipeConfig.h` | 改 | `FPlanetVolcanoParams`、`Config.Volcano`、哈希 |
| `Geo/Bake/PlanetBakeTypes.h` | 改 | `EPlanetField::Volcanoes`、火山字段 |
| `Geo/Snapshot/PlanetSnapshot.{h,cpp}` | 改 | `Volcanoes` 进快照与哈希 |
| `Geo/Bake/PlanetBakePipeline.cpp` | 改 | 注册阶段 4、搬运点表 |
| `Geo/Bake/Stages/Stage04Magmatism.{h,cpp}` | 新建 | 放点、剖面、叠加、调试图层 |
| `Geo/Assets/PlanetRecipe.{h,cpp}` | 改 | `Stage4|Volcano` 属性 + `ToConfig` |
| `Tests/PlanetGeoStage04Tests.cpp` | 新建 | 阶段 4 测试 |
| `Tests/PlanetGeoRecipeTests.cpp` | 改 | 哈希探针 |

测试辅助（Task 2 起所有阶段 4 测试共用，写在 `PlanetGeoStage04Tests.cpp` 顶部匿名命名空间，函数名加 `Stage04` 前缀避免 unity 冲突）：

```cpp
// 跑阶段 2→3→4；返回阶段 3 之后、阶段 4 之前的高程。
FGeoField2D Stage04TestBake(const FPlanetRecipeConfig& C, FPlanetBakeFields& F, FPlanetBakeReport& R, bool bRunStage4 = true)
{
	FPlanetBakeContext C2{ C, F, FGeoRng::Derive(C.Seed, 2, 0), R }; FStage02Plates().Bake(C2);
	FPlanetBakeContext C3{ C, F, FGeoRng::Derive(C.Seed, 3, 0), R }; FStage03Terrain().Bake(C3);
	const FGeoField2D Before = F.Elevation;
	if (bRunStage4) { FPlanetBakeContext C4{ C, F, FGeoRng::Derive(C.Seed, 4, 0), R }; FStage04Magmatism().Bake(C4); }
	return Before;
}
int32 Stage04Count(const FPlanetBakeFields& F, EGeoVolcanoKind K)
{
	int32 N = 0; for (const FGeoVolcano& V : F.Volcanoes) { N += V.Kind == K ? 1 : 0; } return N;
}
```

---

### Task 1: 数据类型、配置、空阶段接进管线

**Files:**
- Create: `Geo/Core/GeoVolcano.h`、`Geo/Bake/Stages/Stage04Magmatism.h`、`Geo/Bake/Stages/Stage04Magmatism.cpp`、`Tests/PlanetGeoStage04Tests.cpp`
- Modify: `Geo/Bake/PlanetRecipeConfig.h`、`Geo/Bake/PlanetBakeTypes.h`、`Geo/Snapshot/PlanetSnapshot.h`、`Geo/Snapshot/PlanetSnapshot.cpp`、`Geo/Bake/PlanetBakePipeline.cpp`

**Interfaces:**
- Produces: `EGeoVolcanoKind`、`FGeoVolcano`（字段见下）、`FPlanetVolcanoParams`、`FPlanetRecipeConfig::Volcano`、
  `FPlanetBakeFields::{Volcanoes, VolcanoMask, VolcanicType, VolcanicAge, VolcanoActive, VolcanoDelta}`、
  `EPlanetField::Volcanoes`、`FPlanetSnapshot::Volcanoes`、`FStage04Magmatism`、
  `namespace Stage04 { void PlaceVolcanoes(const FPlanetRecipeConfig&, const FPlanetBakeFields&, TArray<FGeoVolcano>&); float Profile(const FGeoVolcano&, float Chord, float& OutCrater); }`

- [ ] **Step 1: `Geo/Core/GeoVolcano.h`**（比规格多 `BaseElevation` / `Chain` / `ChainIndex`：叠加要知道锥底在哪个高度，测试要按链检查）

```cpp
// 火山点表条目。烘焙期由阶段 4 产出，进快照供 4b/4c 定位；POD，按值比较。
#pragma once
#include "CoreMinimal.h"
#include "GeoRng.h"

enum class EGeoVolcanoKind : uint8 { None = 0, Strato = 1, Shield, FloodBasalt, Caldera, CinderCone };

struct FGeoVolcano
{
	FVector3f Dir = FVector3f::UpVector;
	EGeoVolcanoKind Kind = EGeoVolcanoKind::None;
	float BaseRadius = 0.f;     // 方向单位（单位球弦长）
	float Height = 0.f;         // 体素，从 BaseElevation 起算
	float CraterRadius = 0.f;   // 方向单位；0 = 无火口
	float CraterDepth = 0.f;    // 体素
	float BaseElevation = 0.f;  // 阶段 3 在火山中心的高程（体素）
	float Age = 0.f;            // 0 新 … 1 老
	bool bActive = false;
	int32 Plate = -1;
	int32 OtherPlate = -1;      // 弧火山：俯冲下去的那块板块；其它为 -1
	int32 Chain = -1;           // 热点链编号；非链上为 -1（破火山口据此决定是盾状还是锥状剖面）
	int32 ChainIndex = -1;      // 链上序号，0 = 热点正上方

	uint64 Hash() const
	{
		const float Fs[10] = { Dir.X, Dir.Y, Dir.Z, BaseRadius, Height, CraterRadius, CraterDepth, BaseElevation, Age, bActive ? 1.f : 0.f };
		uint64 H = GeoHash::Combine(uint64(Kind), uint64(uint32(Plate)) | (uint64(uint32(Chain)) << 32));
		H = GeoHash::Combine(H, uint64(uint32(ChainIndex)) | (uint64(uint32(OtherPlate)) << 32));
		for (const float V : Fs) { H = GeoHash::Combine(H, GeoHash::Float(V)); }
		return H;
	}
	bool operator==(const FGeoVolcano& O) const { return Hash() == O.Hash(); }
};
```

- [ ] **Step 2: 配置 `Geo/Bake/PlanetRecipeConfig.h`**

在 `struct FPlanetTerrainParams { … };` 之后加：

```cpp
struct FPlanetVolcanoParams
{
	bool bArcVolcanoes = true;            // 俯冲弧层状火山
	bool bHotspots = true;                // 热点盾状火山链
	bool bFloodBasalt = true;             // 大陆裂谷溢流玄武岩台地
	bool bCalderas = true;                // 大火山顶部塌陷成破火山口
	bool bCinderCones = true;             // 活火山周围的火山渣锥
	float StratoBaseRatio = 0.024f;       // 层状火山底半径（方向单位，R=40000 时约 960 体素）
	float StratoHeightRatio = 0.012f;     // 层状火山高（480 体素）
	float StratoCraterRatio = 0.004f;     // 火口半径
	float CraterDepthRatio = 0.002f;      // 火口深（80 体素）
	float ArcSpacingRatio = 0.04f;        // 沿弧相邻火山间距
	float ArcOffsetRatio = 0.03f;         // 火山弧离板块边界的距离（上盘一侧）
	float ArcActiveFraction = 0.5f;       // 弧火山活跃比例（挤得越快越活跃）
	float ShieldBaseRatio = 0.04f;        // 盾状火山底半径
	float ShieldHeightRatio = 0.008f;     // 盾状火山高
	int32 HotspotCount = 4;               // 地幔柱热点数
	float HotspotSpacingRatio = 0.06f;    // 热点链上相邻火山间距
	int32 HotspotChainLength = 6;         // 每条链最多几座
	float FloodBasaltChance = 0.3f;       // 每段裂谷出现溢流玄武岩的概率
	float FloodBasaltSizeRatio = 0.06f;   // 溢流玄武岩台地半宽
	float FloodBasaltHeightRatio = 0.003f;// 台地高
	float CalderaFraction = 0.15f;        // 大火山中塌成破火山口的比例
	float CalderaRadiusRatio = 0.016f;    // 破火山口半径（不超过底半径的六成）
	float CalderaDepthRatio = 0.008f;     // 破火山口深
	int32 CinderPerVolcano = 4;           // 每座活火山周围的渣锥个数
	float CinderBaseRatio = 0.006f;       // 渣锥底半径
	float CinderHeightRatio = 0.002f;     // 渣锥高
};
```

`FPlanetRecipeConfig` 里 `FPlanetTerrainParams Terrain;` 下一行加 `FPlanetVolcanoParams Volcano;`。
`Hash()` 里 `for (const float V : Tr) { … }` 之后加：

```cpp
		const FPlanetVolcanoParams& Vp = Volcano;
		H = GeoHash::Combine(H, uint64(Vp.bArcVolcanoes) | (uint64(Vp.bHotspots) << 1) | (uint64(Vp.bFloodBasalt) << 2)
			| (uint64(Vp.bCalderas) << 3) | (uint64(Vp.bCinderCones) << 4));
		H = GeoHash::Combine(H, uint64(uint32(Vp.HotspotCount)) | (uint64(uint32(Vp.HotspotChainLength)) << 32));
		H = GeoHash::Combine(H, uint64(uint32(Vp.CinderPerVolcano)));
		const float Vo[18] = { Vp.StratoBaseRatio, Vp.StratoHeightRatio, Vp.StratoCraterRatio, Vp.CraterDepthRatio, Vp.ArcSpacingRatio,
			Vp.ArcOffsetRatio, Vp.ArcActiveFraction, Vp.ShieldBaseRatio, Vp.ShieldHeightRatio, Vp.HotspotSpacingRatio, Vp.FloodBasaltChance,
			Vp.FloodBasaltSizeRatio, Vp.FloodBasaltHeightRatio, Vp.CalderaFraction, Vp.CalderaRadiusRatio, Vp.CalderaDepthRatio,
			Vp.CinderBaseRatio, Vp.CinderHeightRatio };
		for (const float V : Vo) { H = GeoHash::Combine(H, GeoHash::Float(V)); }
```

- [ ] **Step 3: `Geo/Bake/PlanetBakeTypes.h`**

`#include "GeoVolcano.h"`；`EPlanetField` 加 `Volcanoes = 1 << 7,   // Volcanoes / VolcanoMask / VolcanicType / VolcanicAge`。
`FPlanetBakeFields` 末尾加：

```cpp
	// 阶段 4：点表进快照；宏观场（MacroRes）只给调试图层。Elevation 已叠上火山。
	TArray<FGeoVolcano> Volcanoes;
	FGeoField2D VolcanoMask, VolcanicType, VolcanicAge, VolcanoActive, VolcanoDelta;
```

- [ ] **Step 4: 快照**

`PlanetSnapshot.h`：`#include "GeoVolcano.h"`，`Minerals` 前加
`/** 阶段 4 火山点表：4b/4c 与运行期按它定位火口、矿柱。 */ TArray<FGeoVolcano> Volcanoes;`。
`PlanetSnapshot.cpp::FinalizeHash` 里 `Elevation.Hash()` 一行之后加
`for (const FGeoVolcano& V : Volcanoes) { H = GeoHash::Combine(H, V.Hash()); }`；
`GetAllocatedSize` 加 `+ Volcanoes.GetAllocatedSize()`。

- [ ] **Step 5: 空阶段 `Stage04Magmatism.h/.cpp`**

```cpp
// 阶段 4：岩浆作用。4a 只做火山地形：放点（弧 / 热点链 / 裂谷 / 破火山口 / 渣锥）→ 剖面叠到最终高程。
#pragma once
#include "CoreMinimal.h"
#include "PlanetStage.h"

class STELLARERAPLANET_API FStage04Magmatism final : public IPlanetStage
{
public:
	virtual FName GetName() const override { return TEXT("Stage04Magmatism"); }
	virtual uint32 GetStageIndex() const override { return 4; }
	virtual EPlanetField GetReads() const override
	{
		return EPlanetField::Plates | EPlanetField::Continental | EPlanetField::Elevation | EPlanetField::Relief;
	}
	virtual EPlanetField GetWrites() const override { return EPlanetField::Elevation | EPlanetField::Volcanoes; }
	virtual void Bake(FPlanetBakeContext& Ctx) const override;
	virtual void DescribeDebugLayers(TArray<FGeoDebugLayer>& Out) const override;
	virtual void ExportDebugFields(const FPlanetBakeFields& Fields, TMap<FName, FGeoField2D>& Out) const override;
};

namespace Stage04
{
	/** 只读阶段 2/3 的场，按配方放出全部火山（含破火山口改写与渣锥）。 */
	STELLARERAPLANET_API void PlaceVolcanoes(const FPlanetRecipeConfig& Cfg, const FPlanetBakeFields& F, TArray<FGeoVolcano>& Out);
	/** Chord = 到火山中心的弦长（方向单位）。返回锥体增量（≥0，体素）；OutCrater = 要减去的火口深（≥0）。 */
	STELLARERAPLANET_API float Profile(const FGeoVolcano& V, float Chord, float& OutCrater);
}
```

`.cpp` 先给空实现（`Bake` 里只校验输入、`PlaceVolcanoes` 不放点、`Profile` 返回 0、两个调试函数为空）：

```cpp
#include "Stage04Magmatism.h"
#include "GeoContinentField.h"
#include "GeoNoise.h"

void Stage04::PlaceVolcanoes(const FPlanetRecipeConfig&, const FPlanetBakeFields&, TArray<FGeoVolcano>& Out) { Out.Reset(); }
float Stage04::Profile(const FGeoVolcano&, float, float& OutCrater) { OutCrater = 0.f; return 0.f; }

void FStage04Magmatism::Bake(FPlanetBakeContext& Ctx) const
{
	FPlanetBakeFields& F = Ctx.Fields;
	if (F.Elevation.Res <= 0 || F.PlateId.Res <= 0 || F.ReliefType.Res <= 0)
	{
		Ctx.Report.Errors.Add(TEXT("[地质] 阶段 4：缺阶段 2/3 的高程、板块或地形场"));
		return;
	}
	Stage04::PlaceVolcanoes(Ctx.Config, F, F.Volcanoes);
}
void FStage04Magmatism::DescribeDebugLayers(TArray<FGeoDebugLayer>&) const {}
void FStage04Magmatism::ExportDebugFields(const FPlanetBakeFields&, TMap<FName, FGeoField2D>&) const {}
```

- [ ] **Step 6: 管线 `PlanetBakePipeline.cpp`**

`#include "Stage04Magmatism.h"`；`MakeDefault()` 加 `P.AddStage(MakeUnique<FStage04Magmatism>());`；
快照搬运处 `Snap->Minerals = Config.Minerals;` 后加 `Snap->Volcanoes = MoveTemp(Fields.Volcanoes);`
（放在 `FinalizeHash()` 之前）。

- [ ] **Step 7: 测试文件骨架 + 第一条测试 `PlanetGeoStage04Tests.cpp`**

头部 include：`CoreMinimal.h` `Misc/AutomationTest.h` `GeoContinentField.h` `GeoPlates.h` `GeoRng.h`
`PlanetBakePipeline.h` `Stage02Plates.h` `Stage03Terrain.h` `Stage04Magmatism.h`；
匿名命名空间放上文「测试辅助」两个函数；再加：

```cpp
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGeoStage04PipelineTest, "StellarEra.Planet.Geo.Stage04.Pipeline",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGeoStage04PipelineTest::RunTest(const FString& Parameters)
{
	FPlanetRecipeConfig C;
	C.FaceRes = 256;
	FPlanetBakeReport R;
	const TSharedPtr<const FPlanetSnapshot> Snap = FPlanetBakePipeline::MakeDefault().Bake(C, R);
	TestTrue(FString::Printf(TEXT("烘焙成功（%s）"), *FString::Join(R.Errors, TEXT(" | "))), Snap.IsValid());
	TestTrue(TEXT("阶段 4 在管线里"), R.StageSeconds.ContainsByPredicate([](const TPair<FName, double>& P) { return P.Key == TEXT("Stage04Magmatism"); }));
	return true;
}
```

`PlanetGeoPipelineTests.cpp:61` 的 `StageSeconds.Num()` 是假阶段管线，不受影响；若有别的测试断言默认管线阶段数，改成 4。

- [ ] **Step 8: BUILD + `TEST StellarEra.Planet.Geo s4_t1.log`**

期望：全部通过（新测试 1 条通过）。`Debug.Registry` 此时会报「阶段 4 没登记图层」→ 若它确实失败，记下，Task 4 修；其余失败必须先修。

- [ ] **Step 9: 提交**

```bash
git add -- Source/StellarEraPlanet/Src/Geo Source/StellarEraPlanet/Src/Tests
git commit -m "feat(地质): 阶段 4 骨架：火山点表类型、配置、快照与管线接线" -- Source/StellarEraPlanet/Src/Geo Source/StellarEraPlanet/Src/Tests
```

---

### Task 2: 放点 `Stage04::PlaceVolcanoes`

**Files:**
- Modify: `Geo/Bake/Stages/Stage04Magmatism.cpp`
- Test: `Tests/PlanetGeoStage04Tests.cpp`

**Interfaces:**
- Consumes: Task 1 的全部类型；`GeoPlates::WarpDirection/NearestTwo/BoundaryMotion`、`FGeoPlate::VelocityAt`、`FGeoField2D::Sample/SampleNearest/TexelDir`。
- Produces: 填好的 `F.Volcanoes`（Task 3 只读它）。

- [ ] **Step 1: 写失败测试（放点相关：规格 §8 的 1/2/4/6/7/8）**

```cpp
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGeoStage04PlacementTest, "StellarEra.Planet.Geo.Stage04.Placement",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGeoStage04PlacementTest::RunTest(const FString& Parameters)
{
	FPlanetRecipeConfig C;
	C.FaceRes = 512;
	FPlanetBakeFields F; FPlanetBakeReport R;
	Stage04TestBake(C, F, R);
	TestFalse(TEXT("无错误"), R.HasErrors());
	const FPlanetVolcanoParams& Vp = C.Volcano;
	const int32 NS = Stage04Count(F, EGeoVolcanoKind::Strato), NH = Stage04Count(F, EGeoVolcanoKind::Shield),
		NF = Stage04Count(F, EGeoVolcanoKind::FloodBasalt), NC = Stage04Count(F, EGeoVolcanoKind::Caldera), NK = Stage04Count(F, EGeoVolcanoKind::CinderCone);
	AddInfo(FString::Printf(TEXT("层状 %d，盾状 %d，溢流 %d，破火山口 %d，渣锥 %d"), NS, NH, NF, NC, NK));
	TestTrue(TEXT("有层状火山"), NS > 0);
	TestTrue(TEXT("有盾状火山"), NH > 0);
	TestTrue(TEXT("有破火山口"), NC > 0);
	TestTrue(TEXT("有渣锥"), NK > 0);

	// 弧火山间距：同一俯冲对上的层状火山（含塌成破火山口的弧火山），最近邻距离。
	TArray<float> Nearest;
	for (int32 i = 0; i < F.Volcanoes.Num(); ++i)
	{
		const FGeoVolcano& A = F.Volcanoes[i];
		if (A.OtherPlate < 0) { continue; }
		float Best = TNumericLimits<float>::Max();
		for (int32 j = 0; j < F.Volcanoes.Num(); ++j)
		{
			const FGeoVolcano& B = F.Volcanoes[j];
			if (i == j || B.OtherPlate < 0) { continue; }
			Best = FMath::Min(Best, (A.Dir - B.Dir).Size());
		}
		if (Best < 10.f) { Nearest.Add(Best); }
	}
	Nearest.Sort();
	if (Nearest.Num() > 0)
	{
		const float Med = Nearest[Nearest.Num() / 2];
		AddInfo(FString::Printf(TEXT("弧火山最近邻：最小 %.4f，中位 %.4f（间距 %.4f）"), Nearest[0], Med, Vp.ArcSpacingRatio));
		TestTrue(TEXT("弧火山最小间距 ≥ 0.65×"), Nearest[0] >= 0.65f * Vp.ArcSpacingRatio);
		TestTrue(TEXT("弧火山中位间距 ∈ [0.8, 1.9]×"), Med >= 0.8f * Vp.ArcSpacingRatio && Med <= 1.9f * Vp.ArcSpacingRatio);
	}

	// 上盘：弧火山所在板块 ≠ 俯冲板块，且在 PlateId 场上确实落在自己板块。
	int32 ArcN = 0, ArcUpper = 0;
	for (const FGeoVolcano& V : F.Volcanoes)
	{
		if (V.OtherPlate < 0) { continue; }
		++ArcN;
		ArcUpper += (V.Plate != V.OtherPlate && int32(F.PlateId.SampleNearest(V.Dir)) == V.Plate) ? 1 : 0;
	}
	TestEqual(TEXT("弧火山都在上盘自己的板块上"), ArcUpper, ArcN);

	// 热点链：年龄单调、方向顺着板块运动。
	TMap<int32, TArray<const FGeoVolcano*>> Chains;
	for (const FGeoVolcano& V : F.Volcanoes) { if (V.Chain >= 0) { Chains.FindOrAdd(V.Chain).Add(&V); } }
	int32 Steps = 0, Aligned = 0, Ordered = 0;
	for (auto& Pair : Chains)
	{
		TArray<const FGeoVolcano*>& L = Pair.Value;
		L.Sort([](const FGeoVolcano& A, const FGeoVolcano& B) { return A.ChainIndex < B.ChainIndex; });
		for (int32 k = 1; k < L.Num(); ++k)
		{
			++Steps;
			Ordered += L[k]->Age > L[k - 1]->Age ? 1 : 0;
			const FVector3f Vel = F.PlateFrame.Plates[L[k - 1]->Plate].VelocityAt(L[k - 1]->Dir).GetSafeNormal();
			const FVector3f Step = (L[k]->Dir - L[k - 1]->Dir).GetSafeNormal();
			Aligned += FVector3f::DotProduct(Vel, Step) > 0.5f ? 1 : 0;
		}
	}
	TestTrue(TEXT("至少一条热点链长 ≥ 2"), Steps > 0);
	TestEqual(TEXT("链上年龄单调递增"), Ordered, Steps);
	TestEqual(TEXT("链顺着板块运动方向（夹角 < 60°）"), Aligned, Steps);

	// 活跃：层状 / 链上前两座 / 渣锥（渣锥只围活火山）/ 由上述塌成的破火山口。
	int32 BadActive = 0;
	for (const FGeoVolcano& V : F.Volcanoes)
	{
		if (!V.bActive) { continue; }
		const bool bOk = V.Kind == EGeoVolcanoKind::Strato || V.Kind == EGeoVolcanoKind::CinderCone
			|| (V.Chain >= 0 && V.ChainIndex <= 1) || (V.Kind == EGeoVolcanoKind::Caldera && V.OtherPlate >= 0);
		BadActive += bOk ? 0 : 1;
	}
	TestEqual(TEXT("活跃火山只在弧上或热点附近"), BadActive, 0);

	// 确定性。
	FPlanetBakeFields F2; FPlanetBakeReport R2;
	Stage04TestBake(C, F2, R2);
	TestTrue(TEXT("同配方点表逐项相等"), F.Volcanoes == F2.Volcanoes);

	// 尺度不变：半径减半，个数与方向单位尺寸不变，高度减半。
	FPlanetRecipeConfig Half = C; Half.Radius = C.Radius * 0.5;
	FPlanetBakeFields Fh; FPlanetBakeReport Rh;
	Stage04TestBake(Half, Fh, Rh);
	TestEqual(TEXT("半径减半火山数不变"), Fh.Volcanoes.Num(), F.Volcanoes.Num());
	if (Fh.Volcanoes.Num() == F.Volcanoes.Num() && F.Volcanoes.Num() > 0)
	{
		const FGeoVolcano& A = F.Volcanoes[0]; const FGeoVolcano& B = Fh.Volcanoes[0];
		TestTrue(TEXT("底半径（方向单位）不变"), FMath::IsNearlyEqual(A.BaseRadius, B.BaseRadius, 1e-5f));
		TestTrue(TEXT("高度减半"), FMath::IsNearlyEqual(B.Height, 0.5f * A.Height, 0.01f * A.Height + 1e-3f));
	}

	// 开关：关掉一种只少这一种，另几种个数不变（各用独立随机流）。
	const auto Off = [&](TFunctionRef<void(FPlanetVolcanoParams&)> Set, EGeoVolcanoKind K, const TCHAR* Name)
	{
		FPlanetRecipeConfig X = C; Set(X.Volcano);
		FPlanetBakeFields Fx; FPlanetBakeReport Rx; Stage04TestBake(X, Fx, Rx);
		TestEqual(FString::Printf(TEXT("关闭%s → 0 座"), Name), Stage04Count(Fx, K), 0);
	};
	Off([](FPlanetVolcanoParams& P) { P.bArcVolcanoes = false; }, EGeoVolcanoKind::Strato, TEXT("弧火山"));
	Off([](FPlanetVolcanoParams& P) { P.bHotspots = false; }, EGeoVolcanoKind::Shield, TEXT("热点"));
	Off([](FPlanetVolcanoParams& P) { P.bFloodBasalt = false; }, EGeoVolcanoKind::FloodBasalt, TEXT("溢流玄武岩"));
	Off([](FPlanetVolcanoParams& P) { P.bCalderas = false; }, EGeoVolcanoKind::Caldera, TEXT("破火山口"));
	Off([](FPlanetVolcanoParams& P) { P.bCinderCones = false; }, EGeoVolcanoKind::CinderCone, TEXT("渣锥"));
	return true;
}
```

- [ ] **Step 2: BUILD + `TEST StellarEra.Planet.Geo.Stage04 s4_t2a.log`** — 期望 Placement FAIL（「有层状火山」等）。

- [ ] **Step 3: 实现**（替换 Task 1 的 `PlaceVolcanoes` 空实现；放在 `.cpp` 顶部匿名命名空间之后）

```cpp
namespace
{
	FVector3f Stage04Tangent(const FVector3f& D)
	{
		const FVector3f Ref = FMath::Abs(D.Z) < 0.9f ? FVector3f::UpVector : FVector3f::ForwardVector;
		return FVector3f::CrossProduct(Ref, D).GetSafeNormal();
	}
	/** 从 D 出发沿切向 T 走弦长 S：小角度下 normalize(D + T·S) 足够准。 */
	FVector3f Stage04Step(const FVector3f& D, const FVector3f& T, float S) { return (D + T * S).GetSafeNormal(); }

	bool Stage04FarFromAll(const TArray<FGeoVolcano>& L, const FVector3f& D, float MinChord)
	{
		for (const FGeoVolcano& V : L) { if ((V.Dir - D).Size() < MinChord) { return false; } }
		return true;
	}

	int32 Stage04PlateAt(const FGeoPlateFrame& Frame, const FPlanetPlateParams& P, const FVector3f& D)
	{
		int32 A, B; float Da, Db;
		GeoPlates::NearestTwo(Frame, GeoPlates::WarpDirection(Frame, D, P), A, B, Da, Db);
		return A;
	}
}

void Stage04::PlaceVolcanoes(const FPlanetRecipeConfig& Cfg, const FPlanetBakeFields& F, TArray<FGeoVolcano>& Out)
{
	Out.Reset();
	const FPlanetVolcanoParams& V = Cfg.Volcano;
	const float R = float(Cfg.Radius);
	const FGeoPlateFrame& Frame = F.PlateFrame;
	const int32 MacroRes = F.PlateId.Res;
	const float SpeedRef = FMath::Max(Cfg.Plates.SpeedMax, 1e-3f);

	// —— 层状火山：汇聚边界上盘一侧，泊松盘沿弧排开 ——
	if (V.bArcVolcanoes && V.StratoBaseRatio > 0.f)
	{
		FGeoRng Rng = FGeoRng::Derive(Cfg.Seed, 4, GeoHash::Str("arc"));
		struct FCand { FVector3f Dir; int32 Upper, Lower; float Rate; uint64 Key; };
		TArray<FCand> Cands;
		for (int32 Face = 0; Face < 6; ++Face)
		for (int32 Y = 0; Y < MacroRes; ++Y)
		for (int32 X = 0; X < MacroRes; ++X)
		{
			const float Type = F.BoundaryType.At(Face, X, Y);
			if (Type != 1.f && Type != 2.f) { continue; }   // 3 = 陆陆碰撞：没有俯冲，不长弧火山
			const FVector3f D0 = F.PlateId.TexelDir(Face, X, Y);
			int32 A, B; float Da, Db;
			GeoPlates::NearestTwo(Frame, GeoPlates::WarpDirection(Frame, D0, Cfg.Plates), A, B, Da, Db);
			const FGeoBoundaryMotion Mot = GeoPlates::BoundaryMotion(Frame, D0, A, B, Cfg.Plates);
			if (Mot.Type != 1) { continue; }
			// 上盘：大陆性更强的一侧（洋洋俯冲取编号小者，保证确定）。
			const FVector3f Toward = (Frame.Plates[B].Center - Frame.Plates[A].Center).GetSafeNormal();
			const FVector3f PA = Stage04Step(D0, -(Toward - D0 * FVector3f::DotProduct(Toward, D0)).GetSafeNormal(), V.ArcOffsetRatio);
			const FVector3f PB = Stage04Step(D0, (Toward - D0 * FVector3f::DotProduct(Toward, D0)).GetSafeNormal(), V.ArcOffsetRatio);
			const float MA = F.ContinentalMask.Sample(PA), MB = F.ContinentalMask.Sample(PB);
			const bool bAUpper = FMath::Abs(MA - MB) > 0.1f ? MA > MB : A < B;
			const FVector3f P = bAUpper ? PA : PB;
			const int32 Upper = bAUpper ? A : B, Lower = bAUpper ? B : A;
			if (Stage04PlateAt(Frame, Cfg.Plates, P) != Upper) { continue; }   // 偏移后跨到别的板块：边界太弯，丢掉
			Cands.Add({ P, Upper, Lower, FMath::Clamp(Mot.Convergence / SpeedRef, 0.f, 1.f), Rng.NextU64() });
		}
		// 随机顺序贪心泊松盘：接受半径带 ±30% 抖动。
		Cands.Sort([](const FCand& L, const FCand& Rr) { return L.Key < Rr.Key; });
		for (const FCand& Cd : Cands)
		{
			const float Gap = V.ArcSpacingRatio * (0.85f + 0.3f * float(Cd.Key >> 40) / 16777216.f);
			bool bFar = true;
			for (const FGeoVolcano& Vo : Out) { if (Vo.OtherPlate >= 0 && (Vo.Dir - Cd.Dir).Size() < Gap) { bFar = false; break; } }
			if (!bFar) { continue; }
			FGeoVolcano Vo;
			Vo.Dir = Cd.Dir; Vo.Kind = EGeoVolcanoKind::Strato; Vo.Plate = Cd.Upper; Vo.OtherPlate = Cd.Lower;
			const float Sz = Rng.Range(0.75f, 1.25f);
			Vo.BaseRadius = V.StratoBaseRatio * Sz;
			Vo.Height = V.StratoHeightRatio * R * Sz * FMath::Lerp(0.7f, 1.f, Cd.Rate);
			Vo.CraterRadius = V.StratoCraterRatio * Sz;
			Vo.CraterDepth = V.CraterDepthRatio * R;
			// 挤得越快越可能活跃：比例由 ArcActiveFraction 定，速率只是排序权重。
			Vo.bActive = Rng.NextFloat() < FMath::Clamp(V.ArcActiveFraction * (0.5f + Cd.Rate), 0.f, 1.f);
			Vo.Age = Vo.bActive ? Rng.Range(0.f, 0.3f) : Rng.Range(0.3f, 1.f);
			Out.Add(Vo);
		}
	}

	// —— 盾状火山：地幔柱热点，链顺着板块运动方向变老 ——
	if (V.bHotspots && V.HotspotCount > 0 && V.ShieldBaseRatio > 0.f)
	{
		FGeoRng Rng = FGeoRng::Derive(Cfg.Seed, 4, GeoHash::Str("hotspot"));
		const int32 Len = FMath::Max(V.HotspotChainLength, 1);
		for (int32 h = 0; h < V.HotspotCount; ++h)
		{
			const float Z = Rng.Range(-1.f, 1.f), Phi = Rng.Range(0.f, 2.f * PI), S = FMath::Sqrt(1.f - Z * Z);
			FVector3f P(S * FMath::Cos(Phi), S * FMath::Sin(Phi), Z);
			const int32 Plate = Stage04PlateAt(Frame, Cfg.Plates, P);
			const FGeoPlate& Pl = Frame.Plates[Plate];
			// 板块几乎不动：链方向不定，只放热点正上方一座。
			const int32 N = Pl.VelocityAt(P).Size() < 0.5f * Cfg.Plates.SpeedMin ? 1 : Len;
			for (int32 k = 0; k < N; ++k)
			{
				if (k > 0)
				{
					const FVector3f Vel = Pl.VelocityAt(P);
					if (Vel.IsNearlyZero()) { break; }
					P = Stage04Step(P, Vel.GetSafeNormal(), V.HotspotSpacingRatio);
					if (Stage04PlateAt(Frame, Cfg.Plates, P) != Plate) { break; }   // 漂进别的板块：链断
				}
				FGeoVolcano Vo;
				Vo.Dir = P; Vo.Kind = EGeoVolcanoKind::Shield; Vo.Plate = Plate; Vo.Chain = h; Vo.ChainIndex = k;
				Vo.Age = N > 1 ? float(k) / float(Len - 1) : 0.f;
				Vo.bActive = k <= 1;
				const float Sz = Rng.Range(0.85f, 1.15f) * FMath::Lerp(1.f, 0.7f, Vo.Age);
				Vo.BaseRadius = V.ShieldBaseRatio * Sz;
				Vo.Height = V.ShieldHeightRatio * R * Sz;
				Vo.CraterRadius = 0.08f * Vo.BaseRadius;
				Vo.CraterDepth = 0.5f * V.CraterDepthRatio * R;
				Out.Add(Vo);
			}
		}
	}

	// —— 溢流玄武岩：大陆裂谷宏观 texel 聚成段，每段按概率出一片 ——
	if (V.bFloodBasalt && V.FloodBasaltChance > 0.f && V.FloodBasaltSizeRatio > 0.f)
	{
		FGeoRng Rng = FGeoRng::Derive(Cfg.Seed, 4, GeoHash::Str("flood"));
		TArray<FVector3f> Placed;
		for (int32 Face = 0; Face < 6; ++Face)
		for (int32 Y = 0; Y < MacroRes; ++Y)
		for (int32 X = 0; X < MacroRes; ++X)
		{
			if (F.ReliefType.At(Face, X, Y) != 6.f) { continue; }
			const FVector3f D = F.ReliefType.TexelDir(Face, X, Y);
			// 一段 ≈ 两个台地宽：段内只掷一次骰子。
			bool bNew = true;
			for (const FVector3f& Q : Placed) { if ((Q - D).Size() < 4.f * V.FloodBasaltSizeRatio) { bNew = false; break; } }
			if (!bNew) { continue; }
			Placed.Add(D);
			if (Rng.NextFloat() >= V.FloodBasaltChance) { continue; }
			FGeoVolcano Vo;
			Vo.Dir = D; Vo.Kind = EGeoVolcanoKind::FloodBasalt; Vo.Plate = Stage04PlateAt(Frame, Cfg.Plates, D);
			Vo.BaseRadius = V.FloodBasaltSizeRatio * Rng.Range(0.8f, 1.2f);
			Vo.Height = V.FloodBasaltHeightRatio * R;
			Vo.Age = Rng.Range(0.2f, 1.f);
			Out.Add(Vo);
		}
	}

	// —— 破火山口：在层状 + 盾状中抽底半径大于中位数的，就地改写 ——
	if (V.bCalderas && V.CalderaFraction > 0.f)
	{
		FGeoRng Rng = FGeoRng::Derive(Cfg.Seed, 4, GeoHash::Str("caldera"));
		TArray<float> Sizes;
		for (const FGeoVolcano& Vo : Out) { if (Vo.Kind == EGeoVolcanoKind::Strato || Vo.Kind == EGeoVolcanoKind::Shield) { Sizes.Add(Vo.BaseRadius); } }
		Sizes.Sort();
		const float Median = Sizes.Num() ? Sizes[Sizes.Num() / 2] : 0.f;
		for (FGeoVolcano& Vo : Out)
		{
			if (Vo.Kind != EGeoVolcanoKind::Strato && Vo.Kind != EGeoVolcanoKind::Shield) { continue; }
			// 每座都掷骰（即使不够大），保证随机流消耗与尺寸无关。
			const bool bHit = Rng.NextFloat() < 2.f * V.CalderaFraction;
			if (!bHit || Vo.BaseRadius < Median) { continue; }
			Vo.Kind = EGeoVolcanoKind::Caldera;
			Vo.CraterRadius = FMath::Min(V.CalderaRadiusRatio, 0.6f * Vo.BaseRadius);
			Vo.CraterDepth = V.CalderaDepthRatio * R;
		}
	}

	// —— 火山渣锥：每座活火山周围 1–2 个底半径处 ——
	if (V.bCinderCones && V.CinderPerVolcano > 0 && V.CinderBaseRatio > 0.f)
	{
		FGeoRng Rng = FGeoRng::Derive(Cfg.Seed, 4, GeoHash::Str("cinder"));
		const int32 Parents = Out.Num();
		for (int32 i = 0; i < Parents; ++i)
		{
			const FGeoVolcano Pv = Out[i];
			if (!Pv.bActive || Pv.Kind == EGeoVolcanoKind::CinderCone || Pv.Kind == EGeoVolcanoKind::FloodBasalt) { continue; }
			const FVector3f T1 = Stage04Tangent(Pv.Dir), T2 = FVector3f::CrossProduct(Pv.Dir, T1);
			for (int32 k = 0; k < V.CinderPerVolcano; ++k)
			{
				const float Ang = Rng.Range(0.f, 2.f * PI), Dist = Pv.BaseRadius * Rng.Range(1.f, 2.f);
				FGeoVolcano Vo;
				Vo.Dir = Stage04Step(Pv.Dir, T1 * FMath::Cos(Ang) + T2 * FMath::Sin(Ang), Dist);
				Vo.Kind = EGeoVolcanoKind::CinderCone; Vo.Plate = Pv.Plate; Vo.bActive = true; Vo.Age = 0.f;
				const float Sz = Rng.Range(0.7f, 1.3f);
				Vo.BaseRadius = V.CinderBaseRatio * Sz;
				Vo.Height = V.CinderHeightRatio * R * Sz;
				Vo.CraterRadius = 0.25f * Vo.BaseRadius;
				Vo.CraterDepth = 0.3f * Vo.Height;
				Out.Add(Vo);
			}
		}
	}

	for (FGeoVolcano& Vo : Out) { Vo.BaseElevation = F.Elevation.Sample(Vo.Dir); }
}
```

- [ ] **Step 4: BUILD + `TEST StellarEra.Planet.Geo.Stage04 s4_t2b.log`** — 期望 Placement PASS；把 `AddInfo` 的计数与间距记下来。
  若「中位间距」超上限：候选点太稀（边界带只有 2% R），把候选从 `BoundaryType` 改为「`BoundaryDistance` < 1.5 × BoundaryBandRatio 且 Mot.Type==1」；不许改测试上下限。

- [ ] **Step 5: 提交** `git commit -m "feat(地质): 阶段 4 放点：弧火山 / 热点链 / 溢流玄武岩 / 破火山口 / 渣锥" -- Source/StellarEraPlanet/Src/Geo Source/StellarEraPlanet/Src/Tests`

---

### Task 3: 剖面与叠加到高程

**Files:**
- Modify: `Geo/Bake/Stages/Stage04Magmatism.cpp`
- Test: `Tests/PlanetGeoStage04Tests.cpp`

**Interfaces:**
- Consumes: `F.Volcanoes`（Task 2）。
- Produces: `Stage04::Profile` 实现；`Bake` 写回 `F.Elevation` / `MinElevation` / `MaxElevation` / `LandAreaFrac`
  与宏观场 `VolcanoMask` `VolcanicType` `VolcanicAge` `VolcanoActive` `VolcanoDelta`（Task 4 读）。

- [ ] **Step 1: 写失败测试（规格 §8 的 3/5）**

```cpp
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGeoStage04ReliefTest, "StellarEra.Planet.Geo.Stage04.Relief",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGeoStage04ReliefTest::RunTest(const FString& Parameters)
{
	FPlanetRecipeConfig C;
	C.FaceRes = 1024;   // 层状火口半径约 2.6 texel，512 下只有 1.3 个
	FPlanetBakeFields F; FPlanetBakeReport R;
	const FGeoField2D Before = Stage04TestBake(C, F, R);
	TestFalse(TEXT("无错误"), R.HasErrors());
	const float Texel = 2.f / float(C.FaceRes);

	// 火口：中心比火口外一圈的平均低。
	int32 Checked = 0, Pit = 0;
	for (const FGeoVolcano& V : F.Volcanoes)
	{
		if (V.CraterRadius < 2.f * Texel || V.Kind == EGeoVolcanoKind::CinderCone) { continue; }
		const FVector3f T1 = FVector3f::CrossProduct(FMath::Abs(V.Dir.Z) < 0.9f ? FVector3f::UpVector : FVector3f::ForwardVector, V.Dir).GetSafeNormal();
		const FVector3f T2 = FVector3f::CrossProduct(V.Dir, T1);
		float Rim = 0.f;
		for (int32 k = 0; k < 8; ++k)
		{
			const float A = 2.f * PI * float(k) / 8.f;
			Rim += 0.125f * F.Elevation.Sample((V.Dir + (T1 * FMath::Cos(A) + T2 * FMath::Sin(A)) * (1.2f * V.CraterRadius)).GetSafeNormal());
		}
		++Checked;
		Pit += F.Elevation.Sample(V.Dir) < Rim ? 1 : 0;
	}
	AddInfo(FString::Printf(TEXT("火口检查 %d 座，凹下 %d 座"), Checked, Pit));
	TestTrue(TEXT("有可检查的火口"), Checked > 0);
	TestEqual(TEXT("火口底都低于火口边缘"), Pit, Checked);

	// 陆地占比与「只抬不挖穿」。
	FPlanetBakeFields F3; FPlanetBakeReport R3;
	Stage04TestBake(C, F3, R3, false);
	float MaxPit = 0.f;
	for (const FGeoVolcano& V : F.Volcanoes) { MaxPit = FMath::Max(MaxPit, V.CraterDepth); }
	int32 Below = 0, LandLost = 0;
	for (int32 Face = 0; Face < 6; ++Face) for (int32 Y = 0; Y < Before.Res; ++Y) for (int32 X = 0; X < Before.Res; ++X)
	{
		const float E3 = Before.At(Face, X, Y), E4 = F.Elevation.At(Face, X, Y);
		Below += E4 < E3 - MaxPit - 1.f ? 1 : 0;
		LandLost += (E3 > 0.f && E4 <= 0.f) ? 1 : 0;
	}
	AddInfo(FString::Printf(TEXT("陆地 %.4f → %.4f；火山增量峰值 %.0f"), F3.LandAreaFrac, F.LandAreaFrac, F.MaxElevation - F3.MaxElevation));
	TestTrue(TEXT("陆地占比变化 < 0.01"), FMath::Abs(F.LandAreaFrac - F3.LandAreaFrac) < 0.01f);
	TestEqual(TEXT("没有 texel 被挖得比火口还深"), Below, 0);
	TestEqual(TEXT("陆上火山不把陆地压到海面下"), LandLost, 0);
	TestTrue(TEXT("宏观场已写"), F.VolcanoMask.Res == F.PlateId.Res && F.VolcanicType.Res == F.PlateId.Res);
	return true;
}
```

- [ ] **Step 2: BUILD + `TEST StellarEra.Planet.Geo.Stage04 s4_t3a.log`** — 期望 Relief FAIL（「有可检查的火口」通过，「火口底都低于火口边缘」或「宏观场已写」失败）。

- [ ] **Step 3: 实现 `Profile`**（替换空实现）

```cpp
float Stage04::Profile(const FGeoVolcano& V, float Chord, float& OutCrater)
{
	OutCrater = 0.f;
	if (V.BaseRadius <= 0.f || Chord >= V.BaseRadius) { return 0.f; }
	const float D = Chord / V.BaseRadius;
	const float C = V.CraterRadius / V.BaseRadius;
	// 陆上老火山：削低、坡变缓、火口被填浅。真实剥蚀留阶段 6。
	const float Wear = (V.BaseElevation > 0.f && V.Age > 0.5f) ? V.Age : 0.f;
	const float H = V.Height * (1.f - 0.5f * Wear);
	// 火口内取火口边缘的锥高：顶部是平台，火口才挖得下去（否则锥尖比火口深还高）。
	const float Dc = FMath::Max(D, C);
	const auto Cone = [&](float X) { return H * FMath::Pow(1.f - X, FMath::Lerp(1.6f, 1.1f, Wear)); };
	const auto Dome = [&](float X) { return H * FMath::Pow(FMath::Max(1.f - X * X, 0.f), 1.5f); };
	float Up = 0.f;
	switch (V.Kind)
	{
	case EGeoVolcanoKind::Strato:      Up = Cone(Dc); break;
	case EGeoVolcanoKind::Shield:      Up = Dome(Dc); break;
	case EGeoVolcanoKind::FloodBasalt: Up = H * (1.f - FMath::SmoothStep(0.9f, 1.f, D)); break;
	case EGeoVolcanoKind::CinderCone:  Up = H * (1.f - Dc); break;
	case EGeoVolcanoKind::Caldera:
		Up = (V.Chain >= 0 ? Dome(Dc) : Cone(Dc))
			+ 0.15f * H * FMath::Exp(-FMath::Square((D - C) / FMath::Max(0.15f * C, 1e-4f)));   // 外环壁
		break;
	default: break;
	}
	if (C > 0.f && D < C)
	{
		const float T = D / C;
		const float Depth = V.CraterDepth * (1.f - 0.7f * Wear);
		// 破火山口是平底洼地；普通火口是碗。
		OutCrater = V.Kind == EGeoVolcanoKind::Caldera ? Depth * (1.f - FMath::SmoothStep(0.75f, 1.f, T)) : Depth * (1.f - T * T);
	}
	return Up;
}
```

- [ ] **Step 4: 实现叠加**（`Bake` 里 `PlaceVolcanoes` 之后）

```cpp
	const FPlanetRecipeConfig& Cfg = Ctx.Config;
	const EParallelForFlags PF = Cfg.bSingleThreadBake ? EParallelForFlags::ForceSingleThread : EParallelForFlags::None;
	const int32 Res = F.Elevation.Res, MacroRes = F.PlateId.Res;
	const uint64 NoiseSeed = FGeoRng::Derive(Cfg.Seed, 4, GeoHash::Str("relief")).NextU64();

	// 每个宏观 texel 记下可能影响它的火山：逐 texel 遍历全部火山在 1024² 下太慢。
	TArray<TArray<int32>> Buckets; Buckets.SetNum(6 * MacroRes * MacroRes);
	const float MacroTexel = 2.f / float(MacroRes) * 1.5f;   // 半个对角线有余
	for (int32 i = 0; i < F.Volcanoes.Num(); ++i)
	{
		const FGeoVolcano& Vo = F.Volcanoes[i];
		const float Reach = Vo.BaseRadius * 1.2f + MacroTexel;   // 1.2：外环壁与噪声余量
		for (int32 Face = 0; Face < 6; ++Face) for (int32 Y = 0; Y < MacroRes; ++Y) for (int32 X = 0; X < MacroRes; ++X)
		{
			if ((F.PlateId.TexelDir(Face, X, Y) - Vo.Dir).Size() < Reach) { Buckets[(Face * MacroRes + Y) * MacroRes + X].Add(i); }
		}
	}
	const auto BucketOf = [&](const FVector3f& D) -> const TArray<int32>&
	{
		int32 Fc; float U, Vv; FGeoField2D::DirToFace(D, Fc, U, Vv);
		const int32 X = FMath::Clamp(int32((U + 1.f) * 0.5f * MacroRes), 0, MacroRes - 1), Y = FMath::Clamp(int32((Vv + 1.f) * 0.5f * MacroRes), 0, MacroRes - 1);
		return Buckets[(Fc * MacroRes + Y) * MacroRes + X];
	};
	// 火山之间取 max 再加到地形上：锥底贴地，相邻锥不叠出双倍高。
	const auto Eval = [&](const FVector3f& D, float& OutUp, float& OutPit, int32& OutTop, float& OutMask)
	{
		OutUp = 0.f; OutPit = 0.f; OutTop = -1; OutMask = 0.f;
		for (const int32 i : BucketOf(D))
		{
			const FGeoVolcano& Vo = F.Volcanoes[i];
			const float Chord = (D - Vo.Dir).Size();
			float Pit;
			float Up = Stage04::Profile(Vo, Chord, Pit);
			if (Up <= 0.f && Pit <= 0.f) { continue; }
			// 坡面不规整 + 放射状沟；火口附近不加，免得噪声盖过火口深度。
			const float Near = FMath::SmoothStep(Vo.CraterRadius, 2.f * Vo.CraterRadius + 1e-5f, Chord);
			const FVector3f Q = D * (1.f / FMath::Max(Vo.BaseRadius, 1e-4f));
			const float Nz = GeoNoise::Fbm3(GeoHash::Combine(NoiseSeed, uint64(i)), Q * 1.5f, 2);
			const float Gully = 1.f - 0.25f * FMath::Square(GeoNoise::Value3(GeoHash::Combine(NoiseSeed, uint64(i) + 7777), Q * 6.f));
			Up *= 1.f + Near * (0.15f * Nz - (1.f - Gully));
			const float Mask = 1.f - Chord / Vo.BaseRadius;
			if (Mask > OutMask) { OutMask = Mask; OutTop = i; }
			OutUp = FMath::Max(OutUp, Up);
			OutPit = FMath::Max(OutPit, Pit);
		}
	};

	FGeoField2D& H = F.Elevation;
	ParallelFor(6 * Res, [&](int32 Row)
	{
		const int32 Face = Row / Res, Y = Row % Res;
		for (int32 X = 0; X < Res; ++X)
		{
			float Up, Pit, Mask; int32 Top;
			Eval(H.TexelDir(Face, X, Y), Up, Pit, Top, Mask);
			if (Top < 0) { continue; }
			const float E0 = H.At(Face, X, Y);
			float E = E0 + Up - Pit;
			// 只有洋底火山能改海岸线；陆上火口挖不穿海平面。
			if (E0 > 0.f) { E = FMath::Max(E, FMath::Min(E0, 1.f)); }
			H.At(Face, X, Y) = E;
		}
	}, PF);

	// 统计量要重算：阶段 3 的上下界被火山改了，快照值域靠它。
	float MinE = TNumericLimits<float>::Max(), MaxE = -TNumericLimits<float>::Max();
	double LandW = 0.0, TotalW = 0.0;
	for (int32 Face = 0; Face < 6; ++Face) for (int32 Y = 0; Y < Res; ++Y) for (int32 X = 0; X < Res; ++X)
	{
		const float E = H.At(Face, X, Y);
		MinE = FMath::Min(MinE, E); MaxE = FMath::Max(MaxE, E);
		const double W = GeoContinentField::TexelWeight(H, X, Y);
		TotalW += W; LandW += E > 0.f ? W : 0.0;
	}
	F.MinElevation = MinE; F.MaxElevation = MaxE;
	F.LandAreaFrac = float(LandW / FMath::Max(TotalW, 1e-30));

	F.VolcanoMask.Init(MacroRes); F.VolcanicType.Init(MacroRes); F.VolcanicAge.Init(MacroRes);
	F.VolcanoActive.Init(MacroRes); F.VolcanoDelta.Init(MacroRes);
	ParallelFor(6 * MacroRes, [&](int32 Row)
	{
		const int32 Face = Row / MacroRes, Y = Row % MacroRes;
		for (int32 X = 0; X < MacroRes; ++X)
		{
			float Up, Pit, Mask; int32 Top;
			Eval(F.VolcanoMask.TexelDir(Face, X, Y), Up, Pit, Top, Mask);
			const FGeoVolcano* Vo = Top >= 0 ? &F.Volcanoes[Top] : nullptr;
			F.VolcanoMask.At(Face, X, Y) = Mask;
			F.VolcanicType.At(Face, X, Y) = Vo ? float(uint8(Vo->Kind)) : 0.f;
			F.VolcanicAge.At(Face, X, Y) = Vo ? Vo->Age : 0.f;
			F.VolcanoActive.At(Face, X, Y) = Vo && Vo->bActive ? 1.f : 0.f;
			F.VolcanoDelta.At(Face, X, Y) = Up - Pit;
		}
	}, PF);
```

- [ ] **Step 5: BUILD + `TEST StellarEra.Planet.Geo s4_t3b.log`** — 期望 Stage04 三条全过；阶段 2/3 已有测试不受影响（它们不跑阶段 4，或只查快照上下界不等式）。
  「火口底都低于火口边缘」若少数失败：先 `AddInfo` 打印失败火山的 Kind / CraterRadius / 邻居个数。邻居锥压住火口时，把 `OutUp` 的取 max 改成「同一桶内取 max，但中心火山的火口照减」——不许放宽断言。

- [ ] **Step 6: 提交** `git commit -m "feat(地质): 阶段 4 火山剖面叠加到高程；火口 / 破火山口 / 老火山削低" -- Source/StellarEraPlanet/Src/Geo Source/StellarEraPlanet/Src/Tests`

---

### Task 4: 调试图层

**Files:**
- Modify: `Geo/Bake/Stages/Stage04Magmatism.cpp`

**Interfaces:**
- Consumes: Task 3 的五个宏观场。
- Produces: 图层 `VolcanicType` / `VolcanoActive` / `VolcanicAge` / `VolcanoDelta`（Stage = 4）。

- [ ] **Step 1: 失败测试即现有 `StellarEra.Planet.Geo.Debug.Registry`** — 跑 `TEST StellarEra.Planet.Geo.Debug s4_t4a.log`，期望它报阶段 4 没登记图层（若 Task 1 时它没报，说明 Registry 只查有图层的阶段，跳过这一步）。

- [ ] **Step 2: 实现**（替换两个空函数）

```cpp
namespace
{
	float Stage04DebugField(const FPlanetSnapshot& S, const TCHAR* Name, const FVector3d& P, bool bNearest)
	{
		const FGeoField2D* Fd = S.DebugFields.Find(FName(Name));
		if (!Fd || Fd->Res <= 0) { return FGeoDebugLayer::NoData(); }
		const FVector3f Dir(P.GetSafeNormal());
		return bNearest ? Fd->SampleNearest(Dir) : Fd->Sample(Dir);
	}
}

void FStage04Magmatism::DescribeDebugLayers(TArray<FGeoDebugLayer>& Out) const
{
	{
		FGeoDebugLayer L;
		L.Id = TEXT("VolcanicType"); L.DisplayName = TEXT("火山类型"); L.Stage = 4;
		L.Kind = EGeoDebugKind::Categorical;
		// 下标 = EGeoVolcanoKind。
		L.Legend = { { TEXT("无"), FColor(70, 70, 70) }, { TEXT("层状火山"), FColor(200, 50, 30) }, { TEXT("盾状火山"), FColor(230, 140, 40) },
			{ TEXT("溢流玄武岩"), FColor(90, 60, 90) }, { TEXT("破火山口"), FColor(250, 220, 60) }, { TEXT("火山渣锥"), FColor(140, 30, 30) } };
		L.Sample = [](const FPlanetSnapshot& S, const FVector3d& P) { return Stage04DebugField(S, TEXT("VolcanicType"), P, true); };
		Out.Add(MoveTemp(L));
	}
	{
		FGeoDebugLayer L;
		L.Id = TEXT("VolcanoActive"); L.DisplayName = TEXT("活火山"); L.Stage = 4;
		L.Kind = EGeoDebugKind::Categorical;
		L.Legend = { { TEXT("休眠 / 无"), FColor(70, 70, 70) }, { TEXT("活跃"), FColor(255, 60, 20) } };
		L.Sample = [](const FPlanetSnapshot& S, const FVector3d& P) { return Stage04DebugField(S, TEXT("VolcanoActive"), P, true); };
		Out.Add(MoveTemp(L));
	}
	{
		FGeoDebugLayer L;
		L.Id = TEXT("VolcanicAge"); L.DisplayName = TEXT("火山年龄"); L.Stage = 4;
		L.Unit = TEXT("0 新 … 1 老");
		L.Sample = [](const FPlanetSnapshot& S, const FVector3d& P) { return Stage04DebugField(S, TEXT("VolcanicAge"), P, true); };
		L.Range = [](const FPlanetSnapshot&) { return FVector2f(0.f, 1.f); };
		Out.Add(MoveTemp(L));
	}
	{
		FGeoDebugLayer L;
		L.Id = TEXT("VolcanoDelta"); L.DisplayName = TEXT("火山高程增量"); L.Stage = 4;
		L.Unit = TEXT("体素");
		L.Sample = [](const FPlanetSnapshot& S, const FVector3d& P) { return Stage04DebugField(S, TEXT("VolcanoDelta"), P, false); };
		L.Range = [](const FPlanetSnapshot& S) { return FVector2f(float(-0.01 * S.R0), float(0.015 * S.R0)); };
		Out.Add(MoveTemp(L));
	}
}

void FStage04Magmatism::ExportDebugFields(const FPlanetBakeFields& Fields, TMap<FName, FGeoField2D>& Out) const
{
	Out.Add(TEXT("VolcanicType"), Fields.VolcanicType);
	Out.Add(TEXT("VolcanoActive"), Fields.VolcanoActive);
	Out.Add(TEXT("VolcanicAge"), Fields.VolcanicAge);
	Out.Add(TEXT("VolcanoDelta"), Fields.VolcanoDelta);
}
```

需要 `#include "PlanetSnapshot.h"`（`Stage03Terrain.cpp` 怎么 include 就照抄）。

- [ ] **Step 3: BUILD + `TEST StellarEra.Planet.Geo s4_t4b.log`** — 期望全过（含 Debug.Registry）。
- [ ] **Step 4: 提交** `git commit -m "feat(地质): 阶段 4 调试图层：火山类型 / 活跃 / 年龄 / 高程增量" -- Source/StellarEraPlanet/Src/Geo`

---

### Task 5: 配方属性（`Stage4|Volcano`）+ 哈希探针

**Files:**
- Modify: `Geo/Assets/PlanetRecipe.h`、`Geo/Assets/PlanetRecipe.cpp`、`Tests/PlanetGeoRecipeTests.cpp`

**Interfaces:**
- Consumes: `FPlanetVolcanoParams`（Task 1）。

- [ ] **Step 1: 失败测试**：`PlanetGeoRecipeTests.cpp` 哈希探针段（`Probe = C2; … TestTrue(… Probe.Hash() != Base);` 那组）末尾加：

```cpp
	Probe = C2; Probe.Volcano.StratoHeightRatio = 0.0121f;  TestTrue(TEXT("层状火山高进哈希"), Probe.Hash() != Base);
	Probe = C2; Probe.Volcano.HotspotCount = 5;              TestTrue(TEXT("热点数进哈希"), Probe.Hash() != Base);
	Probe = C2; Probe.Volcano.bCalderas = false;             TestTrue(TEXT("破火山口开关进哈希"), Probe.Hash() != Base);
	Probe = C2; Probe.Volcano.CinderHeightRatio = 0.0021f;   TestTrue(TEXT("渣锥高进哈希"), Probe.Hash() != Base);
```

并在同文件找到「配方 → 配置透传」的测试（读 `ToConfig` 后比对字段的那段），加：

```cpp
	{
		UPlanetRecipe* Rp = NewObject<UPlanetRecipe>();
		Rp->ArcSpacingRatio = 0.05f; Rp->bHotspots = false; Rp->HotspotChainLength = 3;
		const FPlanetRecipeConfig Cv = Rp->ToConfig(40000.0);
		TestEqual(TEXT("ArcSpacingRatio 透传"), Cv.Volcano.ArcSpacingRatio, 0.05f);
		TestFalse(TEXT("bHotspots 透传"), Cv.Volcano.bHotspots);
		TestEqual(TEXT("HotspotChainLength 透传"), Cv.Volcano.HotspotChainLength, 3);
	}
```

BUILD 会因 `ArcSpacingRatio` 等未定义而编译失败 —— 这就是失败步。

- [ ] **Step 2: `PlanetRecipe.h`**：在 Stage3 属性块之后、`MineralCatalog` 之前加（每个都带中文注释）：

```cpp
	/** 俯冲弧火山：大洋板块俯冲到另一板块下方时，上盘一侧排成一串的陡峭层状火山。 */
	UPROPERTY(EditAnywhere, Category = "Stage4|Volcano")
	bool bArcVolcanoes = true;

	/** 热点火山链：地幔柱在板块下方不动，板块漂过时留下一串越来越老、越来越矮的盾状火山。 */
	UPROPERTY(EditAnywhere, Category = "Stage4|Volcano")
	bool bHotspots = true;

	/** 溢流玄武岩：大陆裂谷处大面积喷出的熔岩，形成平顶、边缘陡坎的台地。 */
	UPROPERTY(EditAnywhere, Category = "Stage4|Volcano")
	bool bFloodBasalt = true;

	/** 破火山口：部分大火山顶部塌陷，变成环形洼地加外环壁。 */
	UPROPERTY(EditAnywhere, Category = "Stage4|Volcano")
	bool bCalderas = true;

	/** 火山渣锥：活火山周围成簇的小锥。 */
	UPROPERTY(EditAnywhere, Category = "Stage4|Volcano")
	bool bCinderCones = true;

	/** 层状火山底半径（占半径，0.024 ≈ 960 体素）。 */
	UPROPERTY(EditAnywhere, Category = "Stage4|Volcano", meta = (ClampMin = "0.002", ClampMax = "0.2"))
	float StratoBaseRatio = 0.024f;

	/** 层状火山高（占半径，0.012 ≈ 480 体素）。 */
	UPROPERTY(EditAnywhere, Category = "Stage4|Volcano", meta = (ClampMin = "0", ClampMax = "0.1"))
	float StratoHeightRatio = 0.012f;

	/** 层状火山火口半径（占半径）。 */
	UPROPERTY(EditAnywhere, Category = "Stage4|Volcano", meta = (ClampMin = "0", ClampMax = "0.05"))
	float StratoCraterRatio = 0.004f;

	/** 火口深（占半径，0.002 ≈ 80 体素）。 */
	UPROPERTY(EditAnywhere, Category = "Stage4|Volcano", meta = (ClampMin = "0", ClampMax = "0.05"))
	float CraterDepthRatio = 0.002f;

	/** 沿火山弧相邻两座火山的间距（占半径）。越小火山越密。 */
	UPROPERTY(EditAnywhere, Category = "Stage4|Volcano", meta = (ClampMin = "0.005", ClampMax = "0.5"))
	float ArcSpacingRatio = 0.04f;

	/** 火山弧离板块边界的距离（占半径），总在上盘一侧。 */
	UPROPERTY(EditAnywhere, Category = "Stage4|Volcano", meta = (ClampMin = "0", ClampMax = "0.2"))
	float ArcOffsetRatio = 0.03f;

	/** 弧火山中活火山的比例：挤压越快的段越容易活跃。 */
	UPROPERTY(EditAnywhere, Category = "Stage4|Volcano", meta = (ClampMin = "0", ClampMax = "1"))
	float ArcActiveFraction = 0.5f;

	/** 盾状火山底半径（占半径），宽而缓。 */
	UPROPERTY(EditAnywhere, Category = "Stage4|Volcano", meta = (ClampMin = "0.002", ClampMax = "0.3"))
	float ShieldBaseRatio = 0.04f;

	/** 盾状火山高（占半径）。 */
	UPROPERTY(EditAnywhere, Category = "Stage4|Volcano", meta = (ClampMin = "0", ClampMax = "0.1"))
	float ShieldHeightRatio = 0.008f;

	/** 地幔柱热点个数，每个热点一条火山链。 */
	UPROPERTY(EditAnywhere, Category = "Stage4|Volcano", meta = (ClampMin = "0", ClampMax = "32"))
	int32 HotspotCount = 4;

	/** 热点链上相邻火山的间距（占半径）。 */
	UPROPERTY(EditAnywhere, Category = "Stage4|Volcano", meta = (ClampMin = "0.005", ClampMax = "0.5"))
	float HotspotSpacingRatio = 0.06f;

	/** 每条热点链最多几座火山；漂进别的板块会提前截断。 */
	UPROPERTY(EditAnywhere, Category = "Stage4|Volcano", meta = (ClampMin = "1", ClampMax = "30"))
	int32 HotspotChainLength = 6;

	/** 每段大陆裂谷出现溢流玄武岩台地的概率。 */
	UPROPERTY(EditAnywhere, Category = "Stage4|Volcano", meta = (ClampMin = "0", ClampMax = "1"))
	float FloodBasaltChance = 0.3f;

	/** 溢流玄武岩台地半宽（占半径）。 */
	UPROPERTY(EditAnywhere, Category = "Stage4|Volcano", meta = (ClampMin = "0.005", ClampMax = "0.5"))
	float FloodBasaltSizeRatio = 0.06f;

	/** 溢流玄武岩台地高（占半径）。 */
	UPROPERTY(EditAnywhere, Category = "Stage4|Volcano", meta = (ClampMin = "0", ClampMax = "0.05"))
	float FloodBasaltHeightRatio = 0.003f;

	/** 大火山塌成破火山口的比例（只在底半径大于中位数的火山里抽）。 */
	UPROPERTY(EditAnywhere, Category = "Stage4|Volcano", meta = (ClampMin = "0", ClampMax = "0.5"))
	float CalderaFraction = 0.15f;

	/** 破火山口半径（占半径），最多到火山底半径的六成。 */
	UPROPERTY(EditAnywhere, Category = "Stage4|Volcano", meta = (ClampMin = "0.002", ClampMax = "0.2"))
	float CalderaRadiusRatio = 0.016f;

	/** 破火山口深（占半径）。陆上不会挖到海平面以下。 */
	UPROPERTY(EditAnywhere, Category = "Stage4|Volcano", meta = (ClampMin = "0", ClampMax = "0.05"))
	float CalderaDepthRatio = 0.008f;

	/** 每座活火山周围的火山渣锥个数。 */
	UPROPERTY(EditAnywhere, Category = "Stage4|Volcano", meta = (ClampMin = "0", ClampMax = "20"))
	int32 CinderPerVolcano = 4;

	/** 火山渣锥底半径（占半径）。 */
	UPROPERTY(EditAnywhere, Category = "Stage4|Volcano", meta = (ClampMin = "0.001", ClampMax = "0.05"))
	float CinderBaseRatio = 0.006f;

	/** 火山渣锥高（占半径）。 */
	UPROPERTY(EditAnywhere, Category = "Stage4|Volcano", meta = (ClampMin = "0", ClampMax = "0.05"))
	float CinderHeightRatio = 0.002f;
```

- [ ] **Step 3: `PlanetRecipe.cpp::ToConfig`**：`C.Terrain.OldOrogenWidthVariation = …;` 之后逐项 `C.Volcano.<名> = <名>;`，共 26 项（5 个开关 + 18 个 float + 3 个 int32），名字与上表一一对应。

- [ ] **Step 4: BUILD + `TEST StellarEra.Planet.Geo s4_t5.log`** — 期望全过。
- [ ] **Step 5: 提交** `git commit -m "feat(地质): 配方 Stage4|Volcano 参数（中文注释）与哈希探针" -- Source/StellarEraPlanet/Src/Geo Source/StellarEraPlanet/Src/Tests`

---

### Task 6: 全量回归 + 编辑器目检

- [ ] **Step 1:** `TEST StellarEra all_s4.log` — 期望全部通过（基线 295 条 + 本计划新增 3 条 = 298 条）。
  若阶段 2/3 的旧测试因「默认管线多了阶段 4」失败（例如断言快照 `MaxSurfaceRadius` ≤ 某值），按「阶段 4 允许火山抬高」修断言，并在提交信息里列出改了哪条。
- [ ] **Step 2:** 启动编辑器（启动前若有 `Saved/Autosaves/PackageRestoreData.json` 先改名备份），打开 `GeoPlanetMap`，
  对 `Geo_Archipelago` 选 `GeoDebugSurfaceLayer = VolcanicType` 重建，截图；再切回 `Elevation` 截一张火山弧近景。
  执行 `StellarEra.Geo.ExportLayers 1024`，确认 `Saved/GeoDebug/*/Stage4_*` 生成。
- [ ] **Step 3:** 把各类火山数量、陆地占比变化、阶段 4 耗时（`Report.StageSeconds`）报告给用户，停下等验收，不进 4b。

