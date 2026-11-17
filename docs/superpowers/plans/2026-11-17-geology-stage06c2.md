# 地质阶段 6c-2 蒸发岩 / 锂盐 / 煤 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development.

**Goal:** 导出 `BasinId`，新增 `FGeoBasinDepositRule`（内流盆地按环带撒蒸发岩 / 锂盐）与 `FGeoCoalRule`（湿热低平近海撒煤），并配 6 个撒矿器与 6 条矿物。

**Architecture:** 沿用 6c-1：不加阶段，规则挂阶段号 7（6b 之后）。盆地规则按 `BasinId` 聚合内流盆地的格子求中心与半径，在归一化半径环带里放扁圆盘；煤规则用拒绝采样在气候 / 高程 / 坡度 / 大陆度四个条件下选址，放略埋的水平薄层。

**Spec:** `docs/superpowers/specs/2026-11-17-geology-stage06c2-evaporites-design.md`

## Global Constraints

- 尺寸占半径比例；注释 WHY-only ≤3 行中文；中文源文件 UTF-8 with BOM。
- 参数进 `HashParams()`；提交 `git commit -m ... -- <paths>`；尾行 `Co-Authored-By: Claude Code <noreply@anthropic.com>`；除 Task 6 外不碰 `Content/`。
- 若 BUILD 报 Live Coding 或 `LNK1104`，立即 BLOCKED，不改名绕过。
- 迭代只跑 `StellarEra.Planet.Geo.Stage06c2`、`StellarEra.Planet.Geo.Stage06c`、`StellarEra.Planet.Geo.Stage06`、`StellarEra.Planet.Geo.Debug`；全量 `StellarEra` 只在最后跑一次（基线 356）。
- 性能预算：单条沉积规则 < 0.1 s；整管线集成测试 ≤ 15 s。

## 文件

| 文件 | 动作 |
|---|---|
| `Geo/Bake/PlanetBakeTypes.h` | `FGeoField2D BasinId` |
| `Geo/Bake/Stages/Stage06Erosion.cpp` | 写 `BasinId` |
| `Geo/Bake/GeoMineralRules.h` | `FGeoBasinDepositParams/Rule`、`FGeoCoalParams/Rule` |
| `Geo/Bake/GeoMineralRules.cpp` | 两个规则实现 + 哈希 |
| `Geo/Assets/GeoMineralScatterer.h/.cpp` | `UGeoBasinDepositScatterer`、`UGeoCoalScatterer` |
| `Tests/PlanetGeoStage06c2Tests.cpp` | 新建 |

测试助手（前缀 `Stage06c2`，unity 会合并匿名命名空间，不得与 `Stage06c*` 重名）：

```cpp
int32 Stage06c2CellIndex(const FGeoField2D& Field, const FVector3f& Dir);   // 同 FlowNext 测试的取整
void Stage06c2CellFaceXY(const FGeoField2D& Field, int32 Idx, int32& Face, int32& X, int32& Y);
void Stage06c2Scatter(const FPlanetRecipeConfig& C, const FPlanetBakeFields& F,
	const IGeoMineralRule& Rule, FGeoScatterOutput& Out);
```

---

### Task 1: 导出 `BasinId`

**Files:** Modify `PlanetBakeTypes.h`, `Stage06Erosion.cpp`; Create `Tests/PlanetGeoStage06c2Tests.cpp`

**Interfaces:** Produces `FPlanetBakeFields::BasinId`（`FGeoField2D`，Res = `FlowAccum.Res`，-1 = 不在洼地）。

- [ ] **Step 1: 失败测试** `StellarEra.Planet.Geo.Stage06c2.BasinId`

```cpp
// 阶段 6c-2：盆地沉积与煤。
#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "GeoField2D.h"
#include "GeoMineralRule.h"
#include "GeoMineralRules.h"
#include "GeoPrimitive.h"
#include "GeoRng.h"
#include "GeoVolcano.h"
#include "PlanetBakeTypes.h"
#include "PlanetRecipeConfig.h"
#include "Stage02Plates.h"
#include "Stage03Terrain.h"
#include "Stage04Magmatism.h"
#include "Stage05Climate.h"
#include "Stage06Erosion.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	FPlanetBakeFields Stage06c2UpToHydro(const FPlanetRecipeConfig& C, FPlanetBakeReport& R)
	{
		FPlanetBakeFields F;
		FPlanetBakeContext C2{ C, F, FGeoRng::Derive(C.Seed, 2, 0), R }; FStage02Plates().Bake(C2);
		FPlanetBakeContext C3{ C, F, FGeoRng::Derive(C.Seed, 3, 0), R }; FStage03Terrain().Bake(C3);
		FPlanetBakeContext C4{ C, F, FGeoRng::Derive(C.Seed, 4, 0), R }; FStage04Magmatism().Bake(C4);
		FPlanetBakeContext C5{ C, F, FGeoRng::Derive(C.Seed, 5, 0), R }; FStage05Climate().Bake(C5);
		FPlanetBakeContext C6{ C, F, FGeoRng::Derive(C.Seed, 6, 0), R }; FStage06Erosion().Bake(C6);
		return F;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGeoStage06c2BasinIdTest, "StellarEra.Planet.Geo.Stage06c2.BasinId",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGeoStage06c2BasinIdTest::RunTest(const FString& Parameters)
{
	FPlanetRecipeConfig C; C.FaceRes = 256;
	FPlanetBakeReport R;
	const FPlanetBakeFields F = Stage06c2UpToHydro(C, R);
	if (!TestFalse(FString::Printf(TEXT("无错误（%s）"), *FString::Join(R.Errors, TEXT(" | "))), R.HasErrors())) { return false; }
	if (!TestEqual(TEXT("BasinId 与 BasinMask 同分辨率"), F.BasinId.Res, F.BasinMask.Res)) { return false; }
	const int32 Res = F.BasinId.Res;
	int32 BadMaskWithoutId = 0, BadIdOutside = 0, Endorheic = 0;
	for (int32 Face = 0; Face < 6; ++Face)
	for (int32 Y = 0; Y < Res; ++Y)
	for (int32 X = 0; X < Res; ++X)
	{
		const float M = F.BasinMask.At(Face, X, Y);
		const float Id = F.BasinId.At(Face, X, Y);
		if (M > 0.f) { ++Endorheic; BadMaskWithoutId += Id < 0.f ? 1 : 0; }   // 湖盆也有编号
		else { BadIdOutside += Id >= 0.f ? 1 : 0; }
	}
	AddInfo(FString::Printf(TEXT("洼地格（湖盆+内流）%d"), Endorheic));
	TestTrue(TEXT("有洼地格（否则断言空转）"), Endorheic > 0);
	TestEqual(TEXT("洼地格子都有编号"), BadMaskWithoutId, 0);
	TestEqual(TEXT("非洼地格编号为 -1"), BadIdOutside, 0);
	return true;
}

#endif
```

- [ ] **Step 2: RED。**
- [ ] **Step 3: 实现**：`PlanetBakeTypes.h` 在 `FlowNext` 后加带注释的 `FGeoField2D BasinId;`；`Stage06Erosion.cpp` 初始化并写 `F.BasinId.At(Face, X, Y) = float(Flow.BasinId[i]);`（与 `BasinMask` 同一循环、所有格都写）。
- [ ] **Step 4: 绿；提交** `feat(地质): 阶段 6a 导出洼地编号 BasinId 供 6c-2 盆地沉积`

---

### Task 2: `FGeoBasinDepositRule`

**Files:** `GeoMineralRules.h/.cpp`; append tests to `PlanetGeoStage06c2Tests.cpp`

**Interfaces:** Produces `FGeoBasinDepositRule`（stage 7，名字 `"BasinDeposit"`，公开 `P`）。

- [ ] **Step 1: 失败测试**（合成盆地场）

```cpp
namespace
{
	// Res=8、面 0 上以 (4,4) 为中心、半径 1 格的内流盆地；降水整体可调。
	struct FStage06c2Basin
	{
		FPlanetRecipeConfig C;
		FPlanetBakeFields F;
		FVector3f Center;
	};
	FStage06c2Basin Stage06c2MakeBasin(float Precip)
	{
		FStage06c2Basin Out;
		Out.C.Radius = 1000.0;
		const int32 Res = 8;
		Out.F.BasinMask.Init(Res); Out.F.BasinId.Init(Res);
		Out.F.Precipitation.Init(Res); Out.F.Elevation.Init(Res);
		Out.F.ContinentalMask.Init(Res);
		for (int32 Face = 0; Face < 6; ++Face)
		for (int32 Y = 0; Y < Res; ++Y)
		for (int32 X = 0; X < Res; ++X)
		{
			const bool bIn = Face == 0 && FMath::Abs(X - 4) <= 1 && FMath::Abs(Y - 4) <= 1;
			Out.F.BasinMask.At(Face, X, Y) = bIn ? 2.f : 0.f;
			Out.F.BasinId.At(Face, X, Y) = bIn ? 0.f : -1.f;
			Out.F.Precipitation.At(Face, X, Y) = Precip;
			Out.F.Elevation.At(Face, X, Y) = 50.f;
			Out.F.ContinentalMask.At(Face, X, Y) = 0.9f;
		}
		Out.Center = Out.F.BasinMask.TexelDir(0, 4, 4);
		return Out;
	}
}
```

测试：
```cpp
// RingBand：P.RingInner=0.3、RingOuter=0.6、BodiesPerBasin=32 → 有片；
//   每片与 Center 的夹角 / 盆地最大角半径 ∈ [0.25, 0.65]。
// RingPrecip：Precip=0.5（> MaxPrecip 0.2）→ 0 片；Precip=0.1 → 有片。
// RingVolcano：P.bRequireVolcano=true 且 Volcanoes 空 → 0 片；加 FGeoVolcano{Dir=Center,bActive=true} → 有片。
// RingShape：每片 Kind==OrientedBlob、Radii.Z == max(0.5*Thickness,0.5)、中心半径 ≈ R+Elev-0.5*Thickness。
// Hash：逐个参数改一项，HashParams 必变（10 项）。
```

- [ ] **Step 2: RED → 实现 → 绿；提交** `feat(地质): 盆地沉积规则——内流盆地环带撒蒸发岩/锂盐`

实现要点：

```cpp
struct FGeoBasinStats { FVector3f SumDir = FVector3f::ZeroVector; int32 Count = 0; };

void FGeoBasinDepositRule::Scatter(FGeoScatterContext& Ctx, FGeoScatterOutput& Out) const
{
	const FPlanetBakeFields& F = Ctx.Fields;
	const int32 Res = F.BasinMask.Res;
	if (Res <= 0 || F.BasinId.Res != Res || F.Precipitation.Res <= 0 || F.Elevation.Res <= 0)
	{
		Ctx.Errors.Add(TEXT("[地质] 撒矿器「BasinDeposit」需要阶段 6 的 BasinMask/BasinId 与降水/高程场"));
		return;
	}
	if (P.BodiesPerBasin <= 0) { return; }
	const double R = Ctx.Config.Radius;
	const float Inner = FMath::Min(P.RingInner, P.RingOuter), Outer = FMath::Max(P.RingInner, P.RingOuter);
	const float HalfThick = FMath::Max(0.5f * P.ThicknessVoxels, 0.5f);

	// 1) 单遍聚合：同一 BasinId 的方向和与格子数。
	TMap<int32, FGeoBasinStats> Bins;
	for (int32 Face = 0; Face < 6; ++Face)
	for (int32 Y = 0; Y < Res; ++Y)
	for (int32 X = 0; X < Res; ++X)
	{
		if (F.BasinMask.At(Face, X, Y) != 2.f) { continue; }
		const int32 Id = int32(F.BasinId.At(Face, X, Y));
		if (Id < 0) { continue; }
		FGeoBasinStats& S = Bins.FindOrAdd(Id);
		S.SumDir += F.BasinMask.TexelDir(Face, X, Y);
		++S.Count;
	}
	TArray<int32> Ids; Bins.GetKeys(Ids); Ids.Sort();   // 键排序：确定性
	for (const int32 Id : Ids)
	{
		const FGeoBasinStats& S = Bins[Id];
		const FVector3f Center = S.SumDir.GetSafeNormal();
		if (Center.IsNearlyZero() || S.Count < P.MinBasinCells) { continue; }
		if (F.Precipitation.Sample(Center) > P.MaxPrecip) { continue; }
		if (P.bRequireVolcano && !GeoBasinHasVolcanoNear(F.Volcanoes, Center, float(P.VolcanoRadiusRatio * R))) { continue; }

		// 2) 盆地角半径：同编号格子里离中心最远的那个。
		float MinDot = 1.f;
		for (int32 Face = 0; Face < 6; ++Face)
		for (int32 Y = 0; Y < Res; ++Y)
		for (int32 X = 0; X < Res; ++X)
		{
			if (F.BasinMask.At(Face, X, Y) != 2.f || int32(F.BasinId.At(Face, X, Y)) != Id) { continue; }
			MinDot = FMath::Min(MinDot, FVector3f::DotProduct(F.BasinMask.TexelDir(Face, X, Y), Center));
		}
		const float Rad = FMath::Acos(FMath::Clamp(MinDot, -1.f, 1.f));
		if (Rad <= 1e-5f) { continue; }

		for (int32 k = 0; k < P.BodiesPerBasin; ++k)
		{
			const float U = Ctx.Rng.NextFloat();
			const float Angle = Rad * FMath::Lerp(Inner, Outer, FMath::Sqrt(U));
			const FQuat4f Frame = GeoMineralRules::TangentFrame(Center, Ctx.Rng.Range(0.f, 2.f * PI));
			const FVector3f Tangent = Frame.RotateVector(FVector3f::XAxisVector);
			const FVector3f Dir = (Center * FMath::Cos(Angle) + Tangent * FMath::Sin(Angle)).GetSafeNormal();
			const float Elev = F.Elevation.Sample(Dir);
			const FVector3f Radii(float(P.RadiusRatio * R), float(P.RadiusRatio * R), HalfThick);
			const FVector3f BodyCenter = Dir * (float(R) + Elev - 0.5f * P.ThicknessVoxels);
			FGeoPrimitive Body = FGeoPrimitive::MakeOrientedBlob(BodyCenter, Radii, Frame, P.Hard);
			Body.Grade = RandomGrade(Ctx.Rng);
			Out.Bodies.Add(Body);
		}
	}
}
```

火山判定放文件内 helper：`V.bActive && (V.Dir - Center).Size() * R <= MaxChord`。

- [ ] **Step 3: 哈希**：`MaxPrecip`、`MinBasinCells`、`RingInner`、`RingOuter`、`BodiesPerBasin`、`RadiusRatio`、`ThicknessVoxels`、`bRequireVolcano`、`VolcanoRadiusRatio`、`Hard` 全进 `HashParams()`（`bRequireVolcano` 单独 `Combine`，避免位重叠——6c-1 踩过）。

---

### Task 3: `UGeoBasinDepositScatterer`

**Files:** `GeoMineralScatterer.h/.cpp`；测试追加 `Stage06c2.Recipe`

- [ ] 属性与 §3.1 的 10 个字段一一对应逐字段拷贝；`MakeRule()` 不碰目录。
- [ ] 测试：全字段非默认值透传 + 10 条哈希探针 + `GetStage()==7`、`GetRuleName()=="BasinDeposit"`。
- [ ] 提交 `feat(地质): 盆地沉积撒矿器`

---

### Task 4: `FGeoCoalRule` + `UGeoCoalScatterer`

**Files:** 同上两个源文件 + 测试

- [ ] **Step 1: 失败测试**

合成场：Res=8；`Elevation` 大部分 50，面 0 的一片低地带写 5（高程 5 体素 = 0.005R）；`Precipitation` 全 0.8；`ContinentalMask` 全 0.2。
- `CoalWetLow`：默认参数 → 有片，且每片方向高程 ≤ `MaxElevRatio·R`。
- `CoalDry`：`Precipitation = 0.3` → 0 片。
- `CoalHigh`：低地高程改 100 → 0 片。
- `CoalSteep`：低地内相邻格 -50/+50 → 0 片。
- `CoalDepth`：中心半径 ≈ `R + Elev − DepthVoxels − Thickness/2`（±0.6）。
- 哈希探针 9 项。

- [ ] **Step 2: 实现**

```cpp
struct FGeoCoalParams
{
	float MinPrecip = 0.6f;
	float MaxElevRatio = 0.004f;
	float MaxSlope = 0.02f;
	float MaxContinental = 0.5f;
	int32 PatchCount = 300;
	float RadiusRatio = 0.004f;
	float ThicknessVoxels = 2.f;
	float DepthVoxels = 1.f;
	float Hard = 0.2f;
};
class STELLARERAPLANET_API FGeoCoalRule final : public IGeoMineralRule
{
public:
	FGeoCoalParams P;
	virtual uint32 GetStage() const override { return 7; }
	virtual const TCHAR* GetRuleName() const override { return TEXT("Coal"); }
	virtual void Scatter(FGeoScatterContext& Ctx, FGeoScatterOutput& Out) const override;
	virtual uint64 HashParams() const override;
};
```

```cpp
void FGeoCoalRule::Scatter(FGeoScatterContext& Ctx, FGeoScatterOutput& Out) const
{
	const FPlanetBakeFields& F = Ctx.Fields;
	if (F.Precipitation.Res <= 0 || F.ContinentalMask.Res <= 0 || F.Elevation.Res <= 0)
	{
		Ctx.Errors.Add(TEXT("[地质] 撒矿器「Coal」需要降水、大陆度与高程场"));
		return;
	}
	const double R = Ctx.Config.Radius;
	const float HalfThick = FMath::Max(0.5f * P.ThicknessVoxels, 0.5f);
	const int32 Target = FMath::Clamp(P.PatchCount, 0, 4096);
	int32 Placed = 0;
	for (int32 Try = 0; Try < Target * 400 && Placed < Target; ++Try)
	{
		const FVector3f Dir = GeoMineralRulesRandomDir(Ctx.Rng);
		const float Elev = F.Elevation.Sample(Dir);
		if (Elev <= 0.f || double(Elev) > double(P.MaxElevRatio) * R) { continue; }
		if (F.Precipitation.Sample(Dir) < P.MinPrecip) { continue; }
		if (F.ContinentalMask.Sample(Dir) > P.MaxContinental) { continue; }
		const FQuat4f Frame = GeoMineralRules::TangentFrame(Dir, Ctx.Rng.Range(0.f, 2.f * PI));
		const FVector3f X = Frame.RotateVector(FVector3f::XAxisVector);
		const FVector3f Y = Frame.RotateVector(FVector3f::YAxisVector);
		const float Probe = 0.002f * float(R);
		const FVector3f P0 = Dir * float(R);
		const float E1 = F.Elevation.Sample((P0 + X * Probe).GetSafeNormal());
		const float E2 = F.Elevation.Sample((P0 - X * Probe).GetSafeNormal());
		const float E3 = F.Elevation.Sample((P0 + Y * Probe).GetSafeNormal());
		const float E4 = F.Elevation.Sample((P0 - Y * Probe).GetSafeNormal());
		const float Slope = FMath::Max(FMath::Max(FMath::Abs(E1 - Elev), FMath::Abs(E2 - Elev)),
			FMath::Max(FMath::Abs(E3 - Elev), FMath::Abs(E4 - Elev))) / Probe;
		if (Slope > P.MaxSlope) { continue; }
		const FVector3f Radii(float(P.RadiusRatio * R), float(P.RadiusRatio * R), HalfThick);
		const FVector3f Center = Dir * (float(R) + Elev - P.DepthVoxels - 0.5f * P.ThicknessVoxels);
		FGeoPrimitive Body = FGeoPrimitive::MakeOrientedBlob(Center, Radii, Frame, P.Hard);
		Body.Grade = RandomGrade(Ctx.Rng);
		Out.Bodies.Add(Body);
		++Placed;
	}
}
```

- [ ] **Step 3: 撒矿器** `UGeoCoalScatterer`（9 属性 + MakeRule）。
- [ ] **Step 4: 绿；提交** `feat(地质): 煤规则与撒矿器——湿热低平近海的水平薄层`

---

### Task 5: 整管线集成（controller 跑全量）

- [ ] `Stage06c2.Pipeline`：默认管线 + 碳酸盐环规则 + 煤规则；打印内流盆地数、蒸发岩片数、煤片数；断言煤 > 0；若内流盆地数 > 0 则蒸发岩 > 0（否则打印提示不判失败）。
- [ ] `Stage06c2.Determinism`：两次烘焙 `Primitives.Hash()` 一致；单/多线程一致。
- [ ] 全量 `StellarEra`（基线 356）。
- [ ] 提交 `test(地质): 阶段 6c-2 整管线、盆地环带与确定性`

---

### Task 6: 资产与编辑器目检（controller，MCP）

- [ ] `DA_MineralCatalog` 追加 6 条：`EvaporiteCarbonate 蒸发岩·碳酸盐`（0.55,0.85,0.85）、`EvaporiteGypsum 蒸发岩·石膏`（0.92,0.90,0.82）、`EvaporiteHalite 蒸发岩·岩盐`（0.95,0.80,0.85）、`EvaporiteSylvite 蒸发岩·钾盐`（0.95,0.45,0.25）、`LithiumSalt 锂盐`（0.70,0.45,0.95）、`Coal 煤`（0.10,0.10,0.10）。
- [ ] `DA_GeoRecipe_Archipelago` 追加 6 个撒矿器（4 环 + 锂盐 + 煤），环带边界按 spec §5。
- [ ] 磁盘字节复核 + 编辑器读回；重烘 `Geo_Archipelago`，报告烘焙耗时与快照体积；确认无「烘焙失败」。
- [ ] 停下等用户验收。
