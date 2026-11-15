# 地质阶段 4b 地下侵入体与蚀变 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 在阶段 4 里追加六类地下侵入体（岩基 / 岩株 / 岩墙 / 岩床 / 层状侵入体 / 金伯利岩管）与蚀变晕，切面按岩性上色。

**Architecture:** 岩体就是 `FGeoPrimitive`（新增 `ClueId/ClueStrength` 两个字节），进现有 BVH；材质求值在同一次 BVH 查询里按「矿 > 岩性覆盖 > 蚀变」累加。放置逻辑放新文件 `Stage04bIntrusions.{h,cpp}`，由 `FStage04Magmatism::Bake` 在火山地形之后调用。

**Tech Stack:** UE 5.8 C++，`StellarEraPlanet`，Automation Test。

**Spec:** `docs/superpowers/specs/2026-11-15-geology-stage04b-intrusions-design.md`

## Global Constraints

- 所有尺寸占半径比例；角度量用方向单位（单位球弦长），深度 = 比例 × `Cfg.Radius`（体素）。
- 每个配方参数带中文注释；`ToConfig` 透传；进 `FPlanetRecipeConfig::Hash()`。
- 注释只写「为什么」，单处 1–3 行。含中文的 `.h/.cpp` 存 UTF-8 with BOM（`head -c 3 f | xxd` → `ef bb bf`）。
- 随机流：`FGeoRng::Derive(Cfg.Seed, 4, GeoHash::Str("<用途>"))`，每类侵入体一条，关掉一类不影响其它类。
- 提交一律 `git commit -m "..." -- <paths>`；不碰 `Content/`。提交尾行 `Co-Authored-By: Claude Code <noreply@anthropic.com>`。
- 编辑器必须先关（Live Coding 锁 DLL）；构建 cwd 在项目外。

## 构建与测试

BUILD：
`cd F:/workspace/ue && "/d/Application/Epic Games/UE_5.8/Engine/Binaries/ThirdParty/DotNet/10.0/win-x64/dotnet.exe" "D:/Application/Epic Games/UE_5.8/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.dll" StellarEraEditor Win64 Development -Project="F:/workspace/ue/StellarEra/StellarEra.uproject" -WaitMutex -FromMsBuild 2>&1 | grep -E " error |Result:|LNK1104"`
`LNK1104` 时把 `Binaries/Win64/UnrealEditor-StellarEraPlanet*.{dll,pdb}` 改名 `*.locked_<HHMMSS>` 后重来。

TEST：
`"/d/Application/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "F:/workspace/ue/StellarEra/StellarEra.uproject" -ExecCmds="Automation RunTests <过滤>" -TestExit="Automation Test Queue Empty" -unattended -nopause -nullrhi -nosplash -stdout -abslog="F:/workspace/ue/StellarEra/Saved/Logs/<日志>"`
然后 `grep -a "Result={" <日志> | grep -av Success; grep -a "Expected\|tests performed" <日志>`

## 文件

| 文件 | 动作 | 职责 |
|---|---|---|
| `Geo/Core/GeoLithology.h` | 改 | 新增 9–14 六种火成岩 |
| `Geo/Core/GeoPrimitive.h` | 改 | `ClueId/ClueStrength`、`GeoClue`、`GeoPrimitiveDistance` |
| `Geo/Core/GeoPrimitiveIndex.cpp` | 改 | 哈希并入新字节 |
| `Geo/Snapshot/PlanetMaterialEval.h` | 改 | 岩性覆盖 + 蚀变（复用同一次查询） |
| `Geo/Bake/Stages/Stage01Sphere.cpp` | 改 | 岩性图层把 ≥ContinentalCrust 映射成 Crust |
| `Geo/Bake/PlanetRecipeConfig.h` | 改 | `FPlanetIntrusionParams` + 哈希 |
| `Geo/Bake/Stages/Stage04bIntrusions.{h,cpp}` | 新建 | 六类放置 + 蚀变晕 |
| `Geo/Bake/Stages/Stage04Magmatism.{h,cpp}` | 改 | `GetWrites` 加 Primitives；Bake 里调用；图层 |
| `Geo/Assets/PlanetRecipe.{h,cpp}` | 改 | `Stage4|Intrusion` 属性 + ToConfig |
| `Tests/PlanetGeoStage04bTests.cpp` | 新建 | 4b 测试 |
| `Tests/PlanetGeoRecipeTests.cpp` | 改 | 哈希探针 + 透传对比 |

公共测试辅助（写在 `PlanetGeoStage04bTests.cpp` 顶部匿名命名空间，前缀 `Stage04b` 防 unity 冲突）：

```cpp
FGeoField2D Stage04bTestBake(const FPlanetRecipeConfig& C, FPlanetBakeFields& F, FPlanetBakeReport& R)
{
	FPlanetBakeContext C2{ C, F, FGeoRng::Derive(C.Seed, 2, 0), R }; FStage02Plates().Bake(C2);
	FPlanetBakeContext C3{ C, F, FGeoRng::Derive(C.Seed, 3, 0), R }; FStage03Terrain().Bake(C3);
	const FGeoField2D Before = F.Elevation;
	FPlanetBakeContext C4{ C, F, FGeoRng::Derive(C.Seed, 4, 0), R }; FStage04Magmatism().Bake(C4);
	return Before;
}
int32 Stage04bCount(const TArray<FGeoPrimitive>& P, uint8 Lithology)
{
	int32 N = 0; for (const FGeoPrimitive& Pr : P) { N += Pr.LithologyId == Lithology ? 1 : 0; } return N;
}
int32 Stage04bClueCount(const TArray<FGeoPrimitive>& P)
{
	int32 N = 0; for (const FGeoPrimitive& Pr : P) { N += Pr.ClueId != 0 ? 1 : 0; } return N;
}
```

---

### Task 1: 数据模型与材质求值

**Files:**
- Modify: `Geo/Core/GeoLithology.h`、`Geo/Core/GeoPrimitive.h`、`Geo/Core/GeoPrimitiveIndex.cpp`、`Geo/Snapshot/PlanetSnapshot.{h,cpp}`、`Geo/Snapshot/PlanetMaterialEval.h`、`Geo/Bake/Stages/Stage01Sphere.cpp`
- Test: `Tests/PlanetGeoStage04bTests.cpp`（新建）

**Interfaces:**
- Produces: `GeoLithology::{Granite,Diorite,Gabbro,Peridotite,Kimberlite,LayeredMafic}`、`GeoClue::{Keratitic,Sericitic,Argillic,Propylitic}`、`FGeoPrimitive::{ClueId,ClueStrength}`、`GeoPrimitiveDistance(P, X)`、`FPlanetSnapshot::AlterBands[3]`。

- [ ] **Step 1: 岩性表**

`GeoLithology.h`：在 `OceanicCrust = 8` 后加六项，`Count` 改 15：
```cpp
	constexpr uint8 Granite = 9, Diorite = 10, Gabbro = 11, Peridotite = 12, Kimberlite = 13, LayeredMafic = 14;
	constexpr uint8 Count = 15;
```
`DisplayName` 的 `Names[Count]` 补：`花岗岩 / 闪长岩 / 辉长岩 / 橄榄岩 / 金伯利岩 / 层状镁铁岩`；
`DebugColor` 的 `Colors[Count]` 补：`(190,120,140) / (150,140,150) / (90,100,110) / (60,110,80) / (110,60,130) / (70,80,90)`。

- [ ] **Step 2: 原语线索字段与距离函数**

`GeoPrimitive.h`：`FGeoPrimitive` 里 `LithologyId` 后加
```cpp
	uint8 ClueId = 0;        // GeoClue，0 = 无
	uint8 ClueStrength = 0;  // 0..15，晕原语给满，材质按圈衰减
```
文件末尾（`GeoPrimitiveEvaluate` 旁）加：
```cpp
namespace GeoClue
{
	constexpr uint8 None = 0, Keratitic = 1, Sericitic = 2, Argillic = 3, Propylitic = 4, Count = 5;
	inline const TCHAR* DisplayName(uint8 Id)
	{
		static const TCHAR* Names[Count] = { TEXT("无"), TEXT("钾化"), TEXT("绢英岩化"), TEXT("泥化"), TEXT("青磐岩化") };
		return Id < Count ? Names[Id] : Names[0];
	}
	inline FColor DebugColor(uint8 Id)
	{
		static const FColor Colors[Count] = { FColor(255, 0, 255), FColor(230, 90, 90), FColor(230, 190, 70), FColor(120, 200, 120), FColor(90, 140, 210) };
		return Id < Count ? Colors[Id] : Colors[0];
	}
}

/** 归一化椭球距离：体内 < 1，中心 0。蚀变按它分圈，所以必须与 Evaluate 的分母同源。 */
FORCEINLINE float GeoPrimitiveDistance(const FGeoPrimitive& P, const FVector3f& X)
{
	switch (P.Kind)
	{
	case EGeoPrimitiveKind::Blob:         return ((X - P.Center) / P.Radii).Size();
	case EGeoPrimitiveKind::OrientedBlob: return (P.Rotation.UnrotateVector(X - P.Center) / P.Radii).Size();
	}
	return TNumericLimits<float>::Max();
}
```
`GeoPrimitiveEvaluate` 改成调 `GeoBlobFalloff(GeoPrimitiveDistance(P, X), P.Hard)`（行为不变，少一份重复）。

- [ ] **Step 3: 原语哈希**

`GeoPrimitiveIndex.cpp::Hash()` 的打包行后加
```cpp
		H = GeoHash::Combine(H, uint64(P.ClueId) | (uint64(P.ClueStrength) << 8));
```

- [ ] **Step 4: 快照的蚀变分圈阈值**

`PlanetSnapshot.h`（`CrustSplitElevation` 后）：
```cpp
	/** 蚀变四圈的归一化距离内边界：钾化 / 绢英岩化 / 泥化，第四圈到晕边界。 */
	float AlterBands[3] = { 0.3f, 0.55f, 0.8f };
```
`PlanetSnapshot.cpp::FinalizeHash()` 里 `CrustSplitElevation` 那行后加三个 `GeoHash::Combine(H, GeoHash::Float(AlterBands[i]))`。
烘焙期由阶段 4 从配方填（Task 3 做），默认值先保证独立快照也能用。

- [ ] **Step 5: 材质求值**

`PlanetMaterialEval.h` 里把矿体那段换成（同一次查询，优先级 矿 > 岩性 > 蚀变）：

```cpp
		if (!S.Primitives.IsEmpty())
		{
			const FVector3f Pf(P);
			float BestMineral = 0.5f, BestLitho = TNumericLimits<float>::Max();
			uint8 LithoId = 0, ClueId = 0, ClueStr = 0;
			const TArray<FGeoPrimitive>& Prims = S.Primitives.GetPrimitives();
			S.Primitives.QueryPoint(Pf, [&](int32 i)
			{
				const FGeoPrimitive& Pr = Prims[i];
				const float D = GeoPrimitiveDistance(Pr, Pf);
				if (D >= 1.f) { return; }
				if (Pr.MineralId != 0)
				{
					const float V = GeoBlobFalloff(D, Pr.Hard);
					if (V > BestMineral) { BestMineral = V; Out.Mineral = Pr.MineralId; Out.Grade = Pr.Grade; }
				}
				if (Pr.LithologyId != 0 && D < BestLitho) { BestLitho = D; LithoId = Pr.LithologyId; }
				if (Pr.ClueId != 0)
				{
					// 晕原语只带「有一圈蚀变」，具体是哪一圈由归一化距离决定，强度按圈递减。
					const int32 Band = D < S.AlterBands[0] ? 0 : D < S.AlterBands[1] ? 1 : D < S.AlterBands[2] ? 2 : 3;
					static const uint8 BandId[4] = { GeoClue::Keratitic, GeoClue::Sericitic, GeoClue::Argillic, GeoClue::Propylitic };
					static const uint8 BandStr[4] = { 15, 11, 7, 3 };
					const uint8 Str = uint8(FMath::Max(1, FMath::RoundToInt(float(BandStr[Band]) * float(Pr.ClueStrength) / 15.f)));
					if (Str > ClueStr) { ClueStr = Str; ClueId = BandId[Band]; }
				}
			});
			if (LithoId != 0) { Out.Lithology = LithoId; }
			Out.Clue = ClueId;
			Out.ClueStrength = ClueStr;
		}
```

- [ ] **Step 6: 阶段 1 岩性图层**

`Stage01Sphere.cpp` 的 Sample 里把
`Id == GeoLithology::ContinentalCrust || Id == GeoLithology::OceanicCrust ? GeoLithology::Crust : Id`
改成 `Id >= GeoLithology::ContinentalCrust ? GeoLithology::Crust : Id`（新增火成岩不污染地壳切面配色）。

- [ ] **Step 7: 测试（合成原语，不跑烘焙）**

```cpp
#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "GeoLithology.h"
#include "GeoPrimitive.h"
#include "PlanetMaterialEval.h"
#include "PlanetSnapshot.h"

namespace
{
	TSharedPtr<FPlanetSnapshot> Stage04bSnap(const TArray<FGeoPrimitive>& Prims)
	{
		TSharedPtr<FPlanetSnapshot> S = MakeShared<FPlanetSnapshot>();
		S->R0 = 40000.0;
		S->Radial.Layers = { { 1.f, GeoLithology::Crust, 0.f } };
		S->Primitives.Build(Prims);
		return S;
	}
	FVector3f Stage04bAt(const TSharedPtr<FPlanetSnapshot>& S, double DepthRatio)
	{
		return FVector3f(FVector3d(FVector3f::UpVector) * (S->R0 * (1.0 - DepthRatio)));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGeoStage04bMaterialTest, "StellarEra.Planet.Geo.Stage04b.Material",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGeoStage04bMaterialTest::RunTest(const FString& Parameters)
{
	const FVector3f Center(0.f, 0.f, float(40000.0 * 0.95));
	FGeoPrimitive Rock = FGeoPrimitive::MakeBlob(Center, FVector3f(500.f), 0.02f);
	Rock.LithologyId = GeoLithology::Granite;
	const TSharedPtr<FPlanetSnapshot> S = Stage04bSnap({ Rock });
	TestEqual(TEXT("岩体内岩性覆盖为花岗岩"), int32(PlanetMaterialEval::Sample(*S, FVector3d(Center)).Lithology), int32(GeoLithology::Granite));
	TestEqual(TEXT("岩体外恢复地壳"), int32(PlanetMaterialEval::Sample(*S, FVector3d(Center + FVector3f(900.f, 0.f, 0.f))).Lithology), int32(GeoLithology::Crust));

	// 蚀变四圈：同一晕上按归一化距离取 4 点。
	FGeoPrimitive Halo = FGeoPrimitive::MakeBlob(Center, FVector3f(1000.f), 0.02f);
	Halo.ClueId = 1; Halo.ClueStrength = 15;
	const TSharedPtr<FPlanetSnapshot> H = Stage04bSnap({ Halo });
	const float Fract[4] = { 0.15f, 0.42f, 0.68f, 0.9f };
	const uint8 Want[4] = { GeoClue::Keratitic, GeoClue::Sericitic, GeoClue::Argillic, GeoClue::Propylitic };
	int32 Order = 0, Mono = 0; uint8 Prev = 255;
	for (int32 k = 0; k < 4; ++k)
	{
		const FGeoSample Sm = PlanetMaterialEval::Sample(*H, FVector3d(Center + FVector3f(Fract[k] * 1000.f, 0.f, 0.f)));
		Order += Sm.Clue == Want[k] ? 1 : 0;
		Mono += Sm.ClueStrength < Prev ? 1 : 0;
		Prev = Sm.ClueStrength;
	}
	TestEqual(TEXT("四圈顺序：钾化→绢英岩化→泥化→青磐岩化"), Order, 4);
	TestEqual(TEXT("强度由内到外单调递减"), Mono, 4);

	// 矿优先：矿体与岩体重叠时两者都写。
	FGeoPrimitive Ore = FGeoPrimitive::MakeBlob(Center, FVector3f(300.f), 0.02f);
	Ore.MineralId = 7; Ore.Grade = 200;
	const TSharedPtr<FPlanetSnapshot> M = Stage04bSnap({ Rock, Ore });
	const FGeoSample Ms = PlanetMaterialEval::Sample(*M, FVector3d(Center));
	TestEqual(TEXT("矿仍被写出"), int32(Ms.Mineral), 7);
	TestEqual(TEXT("岩性同时被覆盖"), int32(Ms.Lithology), int32(GeoLithology::Granite));
	return true;
}
```

- [ ] **Step 8: BUILD + `TEST StellarEra.Planet.Geo.Stage04b s4b_t1.log`** — 期望 Material 通过；若 `Radial.Layers` 初始化语法不对，按 `GeoProfile.h` 的字段顺序改。
- [ ] **Step 9: 提交**（pathspec：`Source/StellarEraPlanet/Src/Geo Source/StellarEraPlanet/Src/Tests`）。

---

### Task 2: 六类侵入体放置

**Files:**
- Create: `Geo/Bake/Stages/Stage04bIntrusions.{h,cpp}`
- Modify: `Geo/Bake/PlanetRecipeConfig.h`、`Geo/Bake/Stages/Stage04Magmatism.{h,cpp}`
- Test: `Tests/PlanetGeoStage04bTests.cpp`

**Interfaces:**
- Consumes: Task 1 的岩性与原语字段；`F.Volcanoes`（4a）、`F.ReliefType/CrustAge/ContinentalMask/MountainMask/Elevation`、`Cfg.Volcano`。
- Produces: `Stage04b::PlaceIntrusions(const FPlanetRecipeConfig&, const FPlanetBakeFields&, TArray<FGeoPrimitive>& Out)`；`FPlanetIntrusionParams` 与 `Cfg.Intrusion`。

- [ ] **Step 1: 参数结构**（`PlanetRecipeConfig.h`，`FPlanetVolcanoParams` 之后）

字段与默认值逐条照规格 §6：
```cpp
struct FPlanetIntrusionParams
{
	bool bBatholiths = true;              // 岩基：弧火山段下方的大花岗岩体
	bool bStocks = true;                  // 岩株：每座弧火山正下方的小侵入体（4c 斑岩铜宿主）
	bool bDikes = true;                   // 岩墙：裂谷处近垂直的薄板
	bool bSills = true;                   // 岩床：溢流玄武岩台地与克拉通下的近水平薄板
	bool bLayeredIntrusions = true;       // 层状侵入体：古老克拉通的叠层（4c 铬铂宿主）
	bool bKimberlites = true;             // 金伯利岩管（4c 金刚石宿主）
	bool bAlteration = true;              // 岩体周围的蚀变晕（只写材质线索）
	float BatholithLongRatio = 0.08f;     // 岩基长半径（占 R）
	float BatholithShortRatio = 0.04f;    // 岩基短半径
	float BatholithThickRatio = 0.03f;    // 岩基竖直半径
	float BatholithTopDepthRatio = 0.05f; // 岩基顶深
	int32 BatholithMinVolcanoes = 3;      // 段内至少几座弧火山才长岩基
	float StockRadiusRatio = 0.008f;      // 岩株半径
	float StockTopDepthRatio = 0.03f;     // 岩株顶深
	int32 DikePerRift = 2;                // 每条裂谷的岩墙数
	float DikeThicknessRatio = 0.002f;    // 岩墙厚（半径）
	float DikeLengthRatio = 0.02f;        // 岩墙半高
	float DikeTopDepthRatio = 0.02f;      // 岩墙顶深
	float SillRadiusRatio = 0.05f;        // 岩床半径
	float SillThicknessRatio = 0.002f;    // 岩床厚（半）
	float SillTopDepthRatio = 0.02f;      // 岩床顶深
	float CratonSillDensity = 0.4f;       // 克拉通内岩床撒布密度
	int32 LayeredCount = 4;               // 层状侵入体每组的层数
	float LayeredRadiusRatio = 0.03f;     // 单层半径
	float LayeredThicknessRatio = 0.0025f;// 单层半厚
	float LayeredGapRatio = 0.004f;       // 层间距
	float LayeredTopDepthRatio = 0.06f;   // 整组顶深
	float KimberliteRadiusRatio = 0.002f; // 岩管半径
	float KimberliteHalfHeightRatio = 0.01f; // 岩管半高
	float KimberliteTopDepthRatio = 0.004f;  // 岩管顶深
	int32 KimberliteMax = 8;              // 全局最多几根
	float AlterHaloScale = 2.2f;          // 岩株晕半径倍数（岩基固定 1.25）
	float AlterKeratiticBand = 0.3f;      // 钾化外边界（归一化距离）
	float AlterSericiticBand = 0.55f;     // 绢英岩化外边界
	float AlterArgillicBand = 0.8f;       // 泥化外边界
};
```
`FPlanetRecipeConfig` 加 `FPlanetIntrusionParams Intrusion;`；`Hash()` 里把 7 个 bool 位打包、3 个 int、其余 float 逐个混入（与 `Volcano` 同样式）。

- [ ] **Step 2: 新文件 `Stage04bIntrusions.{h,cpp}`**

头文件：
```cpp
// 阶段 4b：地下侵入体与蚀变。只产出原语，几何与材质由快照与材质求值消费。
#pragma once
#include "CoreMinimal.h"
#include "PlanetBakeTypes.h"

namespace Stage04b
{
	/** 读 4a 的火山点表与阶段 2/3 的场，把六类岩体与蚀变晕追加到 Out。 */
	STELLARERAPLANET_API void PlaceIntrusions(const FPlanetRecipeConfig& Cfg, const FPlanetBakeFields& F, TArray<FGeoPrimitive>& Out);
}
```
实现要点（完整写出关键函数，其余按同样风格补全）：
```cpp
namespace
{
	/** 顶深 + 竖直半厚的中心：岩体沿 dir 摆，跟着球面走。 */
	FVector3f Stage04bCenter(const FPlanetRecipeConfig& C, const FPlanetBakeFields& F, const FVector3f& Dir, float TopDepth, float HalfHeight)
	{
		const double S = C.Radius + double(F.Elevation.Sample(Dir));
		const double Rc = S - double((TopDepth + HalfHeight) * float(C.Radius));
		return FVector3f(FVector3d(Dir) * Rc);
	}
	/** 局部 X 沿 Along、Z 沿 Up：椭圆长轴贴走向，短轴贴球面切向。 */
	FQuat4f Stage04bFrame(const FVector3f& Up, const FVector3f& Along)
	{
		FVector3f X = (Along - Up * FVector3f::DotProduct(Along, Up)).GetSafeNormal();
		if (X.IsNearlyZero())
		{
			const FVector3f Ref = FMath::Abs(Up.Z) < 0.9f ? FVector3f::UpVector : FVector3f::ForwardVector;
			X = FVector3f::CrossProduct(Ref, Up).GetSafeNormal();
		}
		const FVector3f Y = FVector3f::CrossProduct(Up, X);
		return FQuat4f(FMatrix44f(X, Y, Up, FVector3f::ZeroVector)).GetNormalized();
	}
	FGeoPrimitive Stage04bBody(const FVector3f& Center, const FVector3f& Radii, const FQuat4f& Rot, uint8 Litho, bool bOriented)
	{
		FGeoPrimitive P = bOriented ? FGeoPrimitive::MakeOrientedBlob(Center, Radii, Rot, 0.02f)
			: FGeoPrimitive::MakeBlob(Center, Radii, 0.02f);
		P.LithologyId = Litho;
		return P;
	}
	/** 宏观 texel 候选去重：与已收点保持 MinChord 以上，避免一片裂谷撒满。 */
	bool Stage04bFarFrom(const TArray<FVector3f>& L, const FVector3f& D, float MinChord)
	{
		for (const FVector3f& Q : L) { if ((Q - D).Size() < MinChord) { return false; } }
		return true;
	}
}
```
`PlaceIntrusions` 主体按下面顺序（每段都带中文 why 注释）：

1. **弧火山分组**：取 `OtherPlate >= 0` 的火山，按 `(Plate, OtherPlate)` 分桶；桶内按数组顺序贪心串段（与段内任一座弦长 < `2 × ArcSpacingRatio` 就并入），得到 `TArray<TArray<int32>>`。
2. **岩基**：每段 `Num >= BatholithMinVolcanoes` 放一个。质心方向 = 段内方向求和归一；长轴 = 首座→末座方向的切向投影；`Radii = (Long, Short, Thick) × R`；`Top = BatholithTopDepthRatio`，竖直半径 = Thick。
3. **岩株**：每座弧火山一个，`Blob` 半径 `StockRadiusRatio × R × Rng.Range(0.7,1.3)`，顶深 `StockTopDepthRatio`。
4. **岩墙**：扫宏观 `ReliefType == 6` 的 texel，按 `0.1` 弦长去重成裂谷段；每段放 `DikePerRift` 条，沿段切向偏移 `±0.01`，`OrientedBlob` 局部 Z 朝上、`Radii = (Thick, Thick, Length) × R`，顶深 `DikeTopDepthRatio`。
5. **岩床**：① 每座 `Kind == FloodBasalt` 的火山下方 1 片；② 扫宏观 texel `CrustAge > 0.85 && ContinentalMask > 0.6 && Elevation > 0`，按 `0.12` 弦长去重，`Rng.NextFloat() < CratonSillDensity` 时放 1 片。`OrientedBlob`，局部 Z 朝上，`Radii = (Radius, Radius, Thickness) × R`。
6. **层状侵入体**：同一批克拉通候选（另用一条随机流），按 `0.2` 弦长去重、`Rng < 0.15` 时放一组 `LayeredCount` 片，自 `LayeredTopDepthRatio` 起每片下移 `LayeredGapRatio`。
7. **金伯利岩管**：`CrustAge > 0.85 && ContinentalMask > 0.6 && MountainMask < 0.2 && Elevation > 0`，按 `0.05` 弦长去重，`Rng < 0.25` 时放，全局最多 `KimberliteMax` 根；`OrientedBlob` 竖直，`Radii = (R, R, HalfHeight) × R`。
8. 所有原语 `Out.Add(...)`；函数不改 `F`。

- [ ] **Step 3: 接进阶段 4**

`Stage04Magmatism.h`：`GetWrites` 改成 `EPlanetField::Elevation | EPlanetField::Volcanoes | EPlanetField::Primitives`。
`.cpp`：`#include "Stage04bIntrusions.h"`；`Bake` 末尾（宏观场之后）加
```cpp
	Stage04b::PlaceIntrusions(Ctx.Config, F, F.Primitives);
	F.AlterBands[0] = Ctx.Config.Intrusion.AlterKeratiticBand; // 快照用的分圈阈值
	F.AlterBands[1] = Ctx.Config.Intrusion.AlterSericiticBand;
	F.AlterBands[2] = Ctx.Config.Intrusion.AlterArgillicBand;
```
同时补快照通路：`FPlanetBakeFields` 加 `float AlterBands[3] = { 0.3f, 0.55f, 0.8f };`；
`PlanetBakePipeline.cpp` 在 `Snap->Minerals = Config.Minerals;` 后加三行把 `Fields.AlterBands[i]` 拷进 `Snap->AlterBands[i]`
（必须在 `FinalizeHash()` 之前）。`bAlteration == false` 时阈值仍写，无晕场景不受影响。

- [ ] **Step 4: 放置测试**

```cpp
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGeoStage04bPlacementTest, "StellarEra.Planet.Geo.Stage04b.Placement",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGeoStage04bPlacementTest::RunTest(const FString& Parameters)
{
	FPlanetRecipeConfig C; C.FaceRes = 512;
	FPlanetBakeFields F; FPlanetBakeReport R;
	Stage04bTestBake(C, F, R);
	TestFalse(TEXT("无错误"), R.HasErrors());
	const int32 NB = Stage04bCount(F.Primitives, GeoLithology::Granite);
	const int32 NS = Stage04bCount(F.Primitives, GeoLithology::Diorite);
	const int32 NG = Stage04bCount(F.Primitives, GeoLithology::Gabbro);
	const int32 NL = Stage04bCount(F.Primitives, GeoLithology::LayeredMafic);
	const int32 NK = Stage04bCount(F.Primitives, GeoLithology::Kimberlite);
	AddInfo(FString::Printf(TEXT("岩基 %d，岩株 %d，辉长岩（岩墙+岩床）%d，层状 %d，金伯利 %d"), NB, NS, NG, NL, NK));
	TestTrue(TEXT("有岩基"), NB > 0);
	TestTrue(TEXT("有岩株"), NS > 0);
	TestTrue(TEXT("有岩墙 / 岩床"), NG > 0);
	TestTrue(TEXT("有金伯利岩"), NK > 0 && NK <= C.Intrusion.KimberliteMax);

	const int32 Arc = F.Volcanoes.FilterByPredicate([](const FGeoVolcano& V) { return V.OtherPlate >= 0; }).Num();
	TestEqual(TEXT("岩株数 = 弧火山数"), NS, Arc);

	// 深度：每个岩体顶都在地表下 ≥ 0.01R（顶深参数最小的金伯利岩除外，它按自己的 0.004 检）。
	int32 TooHigh = 0;
	for (const FGeoPrimitive& P : F.Primitives)
	{
		if (P.LithologyId < GeoLithology::Granite) { continue; }
		const float TopDepth = float((C.Radius + double(F.Elevation.Sample(FVector3f(P.Center.GetSafeNormal()))) - double(P.Bounds.Max.GetMax())) / C.Radius);
		TooHigh += TopDepth >= 0.003f ? 0 : 1;
	}
	TestEqual(TEXT("没有侵入体露头"), TooHigh, 0);

	// 确定性
	FPlanetBakeFields F2; FPlanetBakeReport R2;
	Stage04bTestBake(C, F2, R2);
	TestEqual(TEXT("原语哈希可复现"), F.Primitives.Hash(), F2.Primitives.Hash());

	// 尺度不变
	FPlanetRecipeConfig Half = C; Half.Radius = C.Radius * 0.5;
	FPlanetBakeFields Fh; FPlanetBakeReport Rh;
	Stage04bTestBake(Half, Fh, Rh);
	TestEqual(TEXT("半径减半各类数量不变（岩基）"), Stage04bCount(Fh.Primitives, GeoLithology::Granite), NB);
	TestEqual(TEXT("半径减半各类数量不变（岩株）"), Stage04bCount(Fh.Primitives, GeoLithology::Diorite), NS);
	TestTrue(TEXT("半径减半单个原语半轴减半"), Fh.Primitives.Num() > 0 && F.Primitives.Num() > 0
		&& FMath::IsNearlyEqual(Fh.Primitives.Last().Bounds.GetExtent().GetMax(), 0.5f * F.Primitives.Last().Bounds.GetExtent().GetMax(), 1e-3f));

	// 开关
	const auto Off = [&](TFunctionRef<void(FPlanetIntrusionParams&)> Set, uint8 Litho, const TCHAR* Name)
	{
		FPlanetRecipeConfig X = C; Set(X.Intrusion);
		FPlanetBakeFields Fx; FPlanetBakeReport Rx; Stage04bTestBake(X, Fx, Rx);
		TestEqual(FString::Printf(TEXT("关闭%s"), Name), Stage04bCount(Fx.Primitives, Litho), 0);
	};
	Off([](FPlanetIntrusionParams& P) { P.bBatholiths = false; }, GeoLithology::Granite, TEXT("岩基"));
	Off([](FPlanetIntrusionParams& P) { P.bStocks = false; }, GeoLithology::Diorite, TEXT("岩株"));
	Off([](FPlanetIntrusionParams& P) { P.bLayeredIntrusions = false; }, GeoLithology::LayeredMafic, TEXT("层状侵入体"));
	Off([](FPlanetIntrusionParams& P) { P.bKimberlites = false; }, GeoLithology::Kimberlite, TEXT("金伯利岩"));
	return true;
}
```

- [ ] **Step 5: BUILD + `TEST StellarEra.Planet.Geo.Stage04b s4b_t2.log`** — 失败先看 `AddInfo` 计数：某类为 0 时检查候选场（裂谷只有 `ReliefType==6`，克拉通要求 `CrustAge>0.85`，可能本种子太少 → 调候选阈值前先在报告里说明）。
- [ ] **Step 6: 提交**。

---

### Task 3: 蚀变晕

**Files:**
- Modify: `Geo/Bake/Stages/Stage04bIntrusions.cpp`
- Test: `Tests/PlanetGeoStage04bTests.cpp`

- [ ] **Step 1: 写失败测试**

```cpp
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGeoStage04bAlterationTest, "StellarEra.Planet.Geo.Stage04b.Alteration",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGeoStage04bAlterationTest::RunTest(const FString& Parameters)
{
	FPlanetRecipeConfig C; C.FaceRes = 512;
	FPlanetBakeFields F; FPlanetBakeReport R;
	Stage04bTestBake(C, F, R);
	const int32 Halos = Stage04bClueCount(F.Primitives);
	AddInfo(FString::Printf(TEXT("蚀变晕原语 %d 个"), Halos));
	TestTrue(TEXT("有蚀变晕"), Halos > 0);
	// 每个岩株 / 岩基都挂一个：晕数 = 闪长岩 + 花岗岩 数。
	TestEqual(TEXT("岩株与岩基各挂一个晕"), Halos,
		Stage04bCount(F.Primitives, GeoLithology::Granite) + Stage04bCount(F.Primitives, GeoLithology::Diorite));

	// 四圈：取一个岩株，沿其半径向外采样，ClueId 顺序正确、强度递减。
	const FGeoPrimitive* Stock = F.Primitives.FindByPredicate([](const FGeoPrimitive& P) { return P.LithologyId == GeoLithology::Diorite; });
	TestNotNull(TEXT("找得到岩株"), Stock);
	if (Stock)
	{
		const FGeoPrimitive* Halo = nullptr;
		for (const FGeoPrimitive& P : F.Primitives)
		{
			if (P.ClueId != 0 && (P.Center - Stock->Center).Size() < 1.f) { Halo = &P; break; }
		}
		TestNotNull(TEXT("找得到对应的晕"), Halo);
		if (Halo)
		{
			const uint8 Want[4] = { GeoClue::Keratitic, GeoClue::Sericitic, GeoClue::Argillic, GeoClue::Propylitic };
			const float Fract[4] = { 0.15f, 0.42f, 0.68f, 0.9f };
			TSharedPtr<FPlanetSnapshot> S = MakeShared<FPlanetSnapshot>();
			S->R0 = C.Radius;
			S->Radial.Layers = { { 1.f, GeoLithology::Crust, 0.f } };
			S->AlterBands[0] = C.Intrusion.AlterKeratiticBand;
			S->AlterBands[1] = C.Intrusion.AlterSericiticBand;
			S->AlterBands[2] = C.Intrusion.AlterArgillicBand;
			S->Primitives.Build(F.Primitives);
			int32 Order = 0, Mono = 0; uint8 Prev = 255;
			for (int32 k = 0; k < 4; ++k)
			{
				const FVector3f P = Halo->Center + FVector3f(FVector3f::UpVector) * (Fract[k] * Halo->Radii.Z);
				// 用 Z 轴采样会落到椭球外：换成沿晕的任一轴即可，这里取最长轴方向。
				const FVector3f Q = Halo->Center + FVector3f(Halo->Radii.X * Fract[k], 0.f, 0.f);
				const FGeoSample Sm = PlanetMaterialEval::Sample(*S, FVector3d(Q));
				Order += Sm.Clue == Want[k] ? 1 : 0;
				Mono += Sm.ClueStrength < Prev ? 1 : 0;
				Prev = Sm.ClueStrength;
			}
			TestEqual(TEXT("四圈顺序正确"), Order, 4);
			TestEqual(TEXT("强度递减"), Mono, 4);
		}
	}

	// 关掉蚀变 → 晕为 0，岩体不受影响。
	FPlanetRecipeConfig Off = C; Off.Intrusion.bAlteration = false;
	FPlanetBakeFields Fo; FPlanetBakeReport Ro;
	Stage04bTestBake(Off, Fo, Ro);
	TestEqual(TEXT("关掉蚀变后没有晕"), Stage04bClueCount(Fo.Primitives), 0);
	TestEqual(TEXT("关掉蚀变不影响岩基"), Stage04bCount(Fo.Primitives, GeoLithology::Granite), Stage04bCount(F.Primitives, GeoLithology::Granite));
	return true;
}
```

- [ ] **Step 2: BUILD + `TEST StellarEra.Planet.Geo.Stage04b s4b_t3a.log`** — 期望 Alteration FAIL（无晕）。
- [ ] **Step 3: 实现**：`PlaceIntrusions` 末尾，`bAlteration` 为真时遍历 `Out` 的副本，
  对 `LithologyId == Granite`（岩基，倍数 1.25）与 `Diorite`（岩株，倍数 `AlterHaloScale`）各追加一个晕：
  ```cpp
  	const int32 Bodies = Out.Num();
  	for (int32 i = 0; i < Bodies; ++i)
  	{
  		const FGeoPrimitive& B = Out[i];
  		const bool bBatho = B.LithologyId == GeoLithology::Granite;
  		if (!bBatho && B.LithologyId != GeoLithology::Diorite) { continue; }
  		FGeoPrimitive Halo = B;              // 同形状同朝向，按倍数放大
  		Halo.MineralId = 0; Halo.Grade = 0;
  		Halo.LithologyId = 0;                // 晕不是岩石，只带线索
  		Halo.ClueId = 1; Halo.ClueStrength = 15;
  		Halo.Radii *= (bBatho ? 1.25f : Cfg.Intrusion.AlterHaloScale);
  		Halo.Bounds = B.Kind == EGeoPrimitiveKind::Blob
  			? FBox3f(Halo.Center - Halo.Radii, Halo.Center + Halo.Radii)
  			: FGeoPrimitive::MakeOrientedBlob(Halo.Center, Halo.Radii, Halo.Rotation, Halo.Hard).Bounds;
  		Out.Add(Halo);
  	}
  ```
  注意：测试里那段「Z 轴采样」的注释说明保留（说明为什么用 X 轴）。
- [ ] **Step 4: BUILD + `TEST StellarEra.Planet.Geo.Stage04b s4b_t3b.log`** — 期望全过。
- [ ] **Step 5: 提交**。

---

### Task 4: 调试图层与配方参数

**Files:**
- Modify: `Geo/Bake/Stages/Stage04Magmatism.cpp`、`Geo/Assets/PlanetRecipe.{h,cpp}`、`Tests/PlanetGeoRecipeTests.cpp`

- [ ] **Step 1: 三个图层**（`DescribeDebugLayers` / `ExportDebugFields`）

```cpp
	// 侵入体类型：直接读快照原语，切面与 3D 视图都能看到岩体形状。
	{
		FGeoDebugLayer L;
		L.Id = TEXT("IntrusionType"); L.DisplayName = TEXT("侵入体类型"); L.Stage = 4;
		L.Kind = EGeoDebugKind::Categorical;
		L.Legend = { { TEXT("无"), FColor(70, 70, 70) }, { TEXT("花岗岩（岩基）"), GeoLithology::DebugColor(GeoLithology::Granite) },
			{ TEXT("闪长岩（岩株）"), GeoLithology::DebugColor(GeoLithology::Diorite) },
			{ TEXT("辉长岩（岩墙 / 岩床）"), GeoLithology::DebugColor(GeoLithology::Gabbro) },
			{ TEXT("金伯利岩"), GeoLithology::DebugColor(GeoLithology::Kimberlite) },
			{ TEXT("层状镁铁岩"), GeoLithology::DebugColor(GeoLithology::LayeredMafic) } };
		L.Sample = [](const FPlanetSnapshot& S, const FVector3d& P)
		{
			const FVector3f Pf(P);
			float Best = TNumericLimits<float>::Max(); uint8 Id = 0;
			if (!S.Primitives.IsEmpty())
			{
				const TArray<FGeoPrimitive>& Prims = S.Primitives.GetPrimitives();
				S.Primitives.QueryPoint(Pf, [&](int32 i)
				{
					const FGeoPrimitive& Pr = Prims[i];
					const float D = GeoPrimitiveDistance(Pr, Pf);
					if (Pr.LithologyId >= GeoLithology::Granite && D < Best) { Best = D; Id = Pr.LithologyId; }
				});
			}
			// 图例只要 4 个非零项：把辉长岩与层状分开靠数值区分，图例按 Id 顺序排。
			return float(Id);
		};
		Out.Add(MoveTemp(L));
	}
```
（图例与返回值按 Id 对齐：0 无、9 花岗岩、10 闪长岩、11 辉长岩、13 金伯利岩、14 层状。`EGeoDebugKind::Categorical` 的颜色表按 Legend 下标取，所以要给 Id 9..14 之间的空位补「无」——实现时用 `DynamicLegend` 或把 Legend 按 0..14 填满，选简单的一种并在报告里说明。）

其余两层：
- `AlterationClue`：分类，图例 `GeoClue::DisplayName/DebugColor`（0..4），Sample 用与材质求值相同的规则（最近晕的 D 分圈）返回 ClueId。
- `AlterationStrength`：连续，Return `ClueStrength`，Range `[0,15]`。

`ExportDebugFields` 这两个层没有宏观场可导（它们由快照原语算），所以只在 `DescribeDebugLayers` 注册，不加 `ExportDebugFields` 条目——查看器的 3D / 剖面路径直接调 `Sample`。

- [ ] **Step 2: 配方属性**

`PlanetRecipe.h` 在 Stage4|Volcano 块之后加 `Stage4|Intrusion` 的 34 项（7 bool + 4 int + 23 float，名字与 `FPlanetIntrusionParams` 完全一致），每项带中文注释；`PlanetRecipe.cpp::ToConfig` 逐项 `C.Intrusion.X = X;`。

- [ ] **Step 3: 测试**

`PlanetGeoRecipeTests.cpp` 哈希探针加：
```cpp
	Probe = C2; Probe.Intrusion.BatholithLongRatio = 0.081f;  TestTrue(TEXT("岩基长轴进哈希"), Probe.Hash() != Base);
	Probe = C2; Probe.Intrusion.KimberliteMax = 9;            TestTrue(TEXT("岩管上限进哈希"), Probe.Hash() != Base);
	Probe = C2; Probe.Intrusion.bAlteration = false;          TestTrue(TEXT("蚀变开关进哈希"), Probe.Hash() != Base);
```
并在既有「配方 → 配置透传」块末尾加一条 All-Fields 对比（仿 Stage3 Continents 的默认值比较）：
```cpp
	{
		UPlanetRecipe* Rp = NewObject<UPlanetRecipe>();
		const FPlanetIntrusionParams D{};
		const FPlanetIntrusionParams T = Rp->ToConfig(40000.0).Intrusion;
		TestTrue(TEXT("Intrusion 透传全字段"), T.bBatholiths == D.bBatholiths && T.bStocks == D.bStocks && T.bDikes == D.bDikes
			&& T.bSills == D.bSills && T.bLayeredIntrusions == D.bLayeredIntrusions && T.bKimberlites == D.bKimberlites
			&& T.bAlteration == D.bAlteration && T.BatholithMinVolcanoes == D.BatholithMinVolcanoes && T.DikePerRift == D.DikePerRift
			&& T.LayeredCount == D.LayeredCount && T.KimberliteMax == D.KimberliteMax
			&& FMath::IsNearlyEqual(T.BatholithLongRatio, D.BatholithLongRatio, 1e-6f)
			&& FMath::IsNearlyEqual(T.AlterHaloScale, D.AlterHaloScale, 1e-6f)
			&& FMath::IsNearlyEqual(T.KimberliteTopDepthRatio, D.KimberliteTopDepthRatio, 1e-6f));
	}
```
（其余 float 靠哈希探针与构造时同名字段覆盖；实现时若愿意逐项比完更好。）

- [ ] **Step 4: BUILD + `TEST StellarEra s4b_t4.log`** — 期望全绿（含 `Debug.Registry`）。
- [ ] **Step 5: 提交**。

---

### Task 5: 全量回归 + 编辑器目检（controller 执行）

- [ ] `TEST StellarEra all_4b.log` — 期望 0 失败（基线 299 + 4b 新增 4 条）。
- [ ] 关编辑器 → 打开 `GeoPlanetMap` → 对 `Geo_Archipelago` 选 `GeoDebugSurfaceLayer = IntrusionType` 重建并截图；再开地质查看器看剖面。
  `StellarEra.Geo.ExportLayers 1024`，确认 `Saved/GeoDebug/*/Stage4_IntrusionType_*`、`Stage4_Alteration*` 生成。
- [ ] 把各类侵入体数量、原语总数、快照大小与烘焙耗时报告给用户，停下等验收，不进 4c。
