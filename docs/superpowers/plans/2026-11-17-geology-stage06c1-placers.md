# 地质阶段 6c-1 砂矿与滨海重砂 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development.

**Goal:** 导出水系拓扑场 `FlowNext`，新增 `FGeoPlacerRule` 与 `UGeoPlacerScatterer`，按「上游原生矿 + 沿流追踪」撒砂金 / 砂锡 / 砂铂 / 砂金刚石，`bMouthOnly` 模式撒滨海重砂。

**Architecture:** 不加阶段、不改管线顺序。阶段 6a 多写一张 `FlowNext`（下游格编号 + 1）；撒矿规则挂在阶段号 7（6b 出露结算之后），遍历已有矿体、沿 `FlowNext` 往下游走、在河道格放 1 体素厚的贴地薄透镜，品位随流程指数衰减。矿种与参数全部由矿物目录与撒矿器资产驱动。

**Spec:** `docs/superpowers/specs/2026-11-17-geology-stage06c1-placers-design.md`

## Global Constraints

- 尺寸占半径比例；注释 WHY-only ≤3 行中文；中文源文件 UTF-8 with BOM。
- 参数进 `HashParams()`；提交 `git commit -m ... -- <paths>`；尾行 `Co-Authored-By: Claude Code <noreply@anthropic.com>`；除 Task 5 外不碰 `Content/`。
- 若 BUILD 报 Live Coding 或 `LNK1104`，立即 BLOCKED，不改名绕过。
- 迭代只跑 `StellarEra.Planet.Geo.Stage06c`、`StellarEra.Planet.Geo.Stage06`、`StellarEra.Planet.Geo.Recipe`、`StellarEra.Planet.Geo.Debug`；全量 `StellarEra` 只在最终提交前跑一次（基线 343）。
- 性能预算：阶段 6a 新增的写场不超过 0.05 s；单条砂矿规则 < 0.1 s；整管线集成测试 ≤ 15 s。

## 文件

| 文件 | 动作 |
|---|---|
| `Geo/Bake/PlanetBakeTypes.h` | `FGeoField2D FlowNext` |
| `Geo/Bake/Stages/Stage06Erosion.cpp` | 写 `FlowNext` |
| `Geo/Bake/GeoMineralRules.h` | `FGeoPlacerParams` + `FGeoPlacerRule` |
| `Geo/Bake/GeoMineralRules.cpp` | 规则实现 + 哈希 |
| `Geo/Assets/GeoMineralScatterer.h/.cpp` | `UGeoPlacerScatterer` |
| `Tests/PlanetGeoStage06cTests.cpp` | 新建 |

共用小工具（放在各自文件的**文件名前缀**匿名命名空间函数里，不得与其它测试文件重名——unity 构建会把同一 TU 的匿名命名空间合并）：

```cpp
// 方向 → 格子编号，与 FGeoField2D::SampleNearest 的取整逐位一致。
int32 Stage06cCellIndex(const FGeoField2D& Field, const FVector3f& Dir)
{
	int32 Face; float U, V;
	FGeoField2D::DirToFace(Dir, Face, U, V);
	const int32 R = Field.Res;
	const int32 X = FMath::Clamp(int32((U + 1.f) * 0.5f * float(R)), 0, R - 1);
	const int32 Y = FMath::Clamp(int32((V + 1.f) * 0.5f * float(R)), 0, R - 1);
	return (Face * R + Y) * R + X;
}
// 编号 → 面 / 面内坐标（写测试场与读 FlowNext 用）。
void Stage06cCellFaceXY(const FGeoField2D& Field, int32 Idx, int32& Face, int32& X, int32& Y)
{
	const int32 R = Field.Res;
	Face = Idx / (R * R);
	const int32 Rem = Idx - Face * R * R;
	X = Rem % R; Y = Rem / R;
}
```

---

### Task 1: 导出 `FlowNext` 与拓扑测试

**Files:**
- Modify: `Source/StellarEraPlanet/Src/Geo/Bake/PlanetBakeTypes.h`
- Modify: `Source/StellarEraPlanet/Src/Geo/Bake/Stages/Stage06Erosion.cpp`
- Create: `Source/StellarEraPlanet/Src/Tests/PlanetGeoStage06cTests.cpp`

**Interfaces:**
- Produces: `FPlanetBakeFields::FlowNext`（`FGeoField2D`，Res = `FlowAccum.Res`，值 = 下游格编号 + 1，0 = 无下游）。Task 2 的规则只读它。

- [ ] **Step 1: 失败测试** `StellarEra.Planet.Geo.Stage06c.FlowNext`

```cpp
// 阶段 6c-1：水系拓扑导出与砂矿规则。
#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "GeoField2D.h"
#include "GeoMineralRule.h"
#include "GeoMineralRules.h"
#include "GeoPrimitive.h"
#include "GeoRng.h"
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
	FPlanetBakeFields Stage06cUpToHydro(const FPlanetRecipeConfig& C, FPlanetBakeReport& R)
	{
		FPlanetBakeFields F;
		FPlanetBakeContext C2{ C, F, FGeoRng::Derive(C.Seed, 2, 0), R }; FStage02Plates().Bake(C2);
		FPlanetBakeContext C3{ C, F, FGeoRng::Derive(C.Seed, 3, 0), R }; FStage03Terrain().Bake(C3);
		FPlanetBakeContext C4{ C, F, FGeoRng::Derive(C.Seed, 4, 0), R }; FStage04Magmatism().Bake(C4);
		FPlanetBakeContext C5{ C, F, FGeoRng::Derive(C.Seed, 5, 0), R }; FStage05Climate().Bake(C5);
		FPlanetBakeContext C6{ C, F, FGeoRng::Derive(C.Seed, 6, 0), R }; FStage06Erosion().Bake(C6);
		return F;
	}
	// 上面两个 CellIndex / CellFaceXY 小工具放这里。
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGeoStage06cFlowNextTest, "StellarEra.Planet.Geo.Stage06c.FlowNext",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGeoStage06cFlowNextTest::RunTest(const FString& Parameters)
{
	FPlanetRecipeConfig C; C.FaceRes = 256;
	FPlanetBakeReport R;
	const FPlanetBakeFields F = Stage06cUpToHydro(C, R);
	if (!TestFalse(FString::Printf(TEXT("无错误（%s）"), *FString::Join(R.Errors, TEXT(" | "))), R.HasErrors())) { return false; }
	if (!TestEqual(TEXT("FlowNext 与 FlowAccum 同分辨率"), F.FlowNext.Res, F.FlowAccum.Res)) { return false; }

	const int32 Res = F.FlowNext.Res, N = 6 * Res * Res;
	int32 NonZero = 0, BadIndex = 0, Cycles = 0, OceanWithDown = 0;
	for (int32 Idx = 0; Idx < N; ++Idx)
	{
		int32 Face, X, Y; Stage06cCellFaceXY(F.FlowNext, Idx, Face, X, Y);
		const int32 Next = int32(F.FlowNext.At(Face, X, Y)) - 1;
		const bool bLand = F.Elevation.Sample(F.FlowNext.TexelDir(Face, X, Y)) > 0.f;
		if (Next >= 0) { ++NonZero; }
		BadIndex += Next >= N ? 1 : 0;
		OceanWithDown += (!bLand && Next >= 0) ? 1 : 0;
		if (Next < 0 || Next >= N) { continue; }
		// 沿下游走，超过 N 步必成环（D8 不允许环）。
		int32 Cur = Next, Steps = 0;
		while (Cur >= 0 && Cur < N && Steps <= N)
		{
			int32 F2, X2, Y2; Stage06cCellFaceXY(F.FlowNext, Cur, F2, X2, Y2);
			Cur = int32(F.FlowNext.At(F2, X2, Y2)) - 1;
			++Steps;
		}
		Cycles += (Steps > N) ? 1 : 0;
	}
	AddInfo(FString::Printf(TEXT("格 %d，有下游 %d，海洋有下游 %d"), N, NonZero, OceanWithDown));
	TestEqual(TEXT("下游格编号在范围内"), BadIndex, 0);
	TestEqual(TEXT("顺流必在 N 步内终止（无环）"), Cycles, 0);
	TestTrue(TEXT("有河道格带下游"), NonZero > 0);
	return true;
}

#endif
```

- [ ] **Step 2: RED。**（编译失败：`FlowNext` 不存在）

- [ ] **Step 3: 实现**
  - `PlanetBakeTypes.h`：`FGeoField2D RiverMask;` 之后加
    ```cpp
    /** 阶段 6 的水系拓扑：值 = 下游格编号 + 1，0 = 无下游（海洋 / 内流盆地出口 / 无流向）。 */
    FGeoField2D FlowNext;
    ```
  - `Stage06Erosion.cpp` 第 230 行附近：`F.FlowNext.Init(Res);` 与其它四张场并列；在写 `F.BasinMask` 的同一循环里（`const int32 D = Flow.Down[i];` 之后）加 `F.FlowNext.At(Face, X, Y) = float(D + 1);`

- [ ] **Step 4: 绿；提交** `feat(地质): 阶段 6a 导出水系拓扑 FlowNext 供阶段 6c 顺流追踪`

---

### Task 2: `FGeoPlacerRule`

**Files:**
- Modify: `Source/StellarEraPlanet/Src/Geo/Bake/GeoMineralRules.h`
- Modify: `Source/StellarEraPlanet/Src/Geo/Bake/GeoMineralRules.cpp`
- Modify: `Source/StellarEraPlanet/Src/Tests/PlanetGeoStage06cTests.cpp`

**Interfaces:**
- Consumes: `FPlanetBakeFields::FlowNext / FlowAccum / Elevation / Primitives`；`Config.Hydro.RiverThreshold`；`GeoMineralRules::TangentFrame`。
- Produces: `FGeoPlacerRule`（`GetStage() == 7`、`GetRuleName() == "Placer"`、公开参数 `P`），Task 3 的撒矿器会填 `P.SourceMineralIds`。

- [ ] **Step 1: 失败测试**（合成场，不跑真实烘焙）

```cpp
namespace
{
	// Res=16、面 0 的 Y=8 一行格子串成一条河：0..Land-1 陆上，其余海洋。
	struct FStage06cChain
	{
		FPlanetRecipeConfig C;
		FPlanetBakeFields F;
	};
	FStage06cChain Stage06cMakeChain(int32 LandCells)
	{
		FStage06cChain Out;
		Out.C.Radius = 1000.0;
		const int32 Res = 16, Y = 8;
		Out.F.FlowAccum.Init(Res);
		Out.F.FlowNext.Init(Res);
		Out.F.Elevation.Init(Res);
		const int32 Land = FMath::Clamp(LandCells, 1, Res - 1);
		for (int32 X = 0; X < Res; ++X)
		{
			const bool bLand = X < Land;
			Out.F.Elevation.At(0, X, Y) = bLand ? 100.f : -100.f;
			Out.F.FlowAccum.At(0, X, Y) = bLand ? 0.001f : 0.f;
			Out.F.FlowNext.At(0, X, Y) = (X + 1 < Res) ? float(((Y * Res) + (X + 1)) + 1) : 0.f;
		}
		for (int32 X = Land; X < Res; ++X)
		{
			for (int32 Y2 = 0; Y2 < Res; ++Y2)
			{
				Out.F.Elevation.At(0, X, Y2) = -100.f;
				Out.F.FlowAccum.At(0, X, Y2) = 0.f;
			}
		}
		// 源矿：上游第 3 个格子，出露满。
		const FVector3f Dir = Out.F.FlowAccum.TexelDir(0, 2, Y);
		FGeoPrimitive Src = FGeoPrimitive::MakeBlob(Dir * 1010.f, FVector3f(20.f, 20.f, 20.f), 0.1f);
		Src.MineralId = 5;
		Src.Exposure = 255;
		Out.F.Primitives.Add(Src);
		return Out;
	}

	void Stage06cScatter(const FPlanetRecipeConfig& C, const FPlanetBakeFields& F, const IGeoMineralRule& Rule,
		FGeoScatterOutput& Out)
	{
		TArray<FString> Errors;
		FGeoScatterContext SC{ C, F, FGeoRng::Derive(C.Seed, Rule.GetStage(), 0), Errors };
		Rule.Scatter(SC, Out);
	}
}
```

六个测试（全部同文件）：

```cpp
// NoSource：源矿列表为空 / 场上没有该矿 → 0 片。
// FollowFlow：源矿 5、StepStride=1 → 有片；每片中心所在格在面 0、Y=8 这条河上、Elev>0、FlowAccum ≥ RiverThreshold。
// GradeDecay：GradeMin=40、GradeMax=230；相邻片 Grade 单调不增；首片 ≥ 200；末片 < 首片且 ≥ 40。
// Mouth：同一条链（Land=12）下，非嘴模式片数 > 3，嘴模式 bMouthOnly + MouthCells=3 恰好 3 片，
//        且三片中心的 X 都 ≥ Land-3。
// Exposure：源矿 Exposure 改 0 且 MinSourceExposure=0.5 → 0 片；MinSourceExposure=0 时同一条链有片。
// Shape：每片 Kind==OrientedBlob 且 Radii.Z ≤ ThicknessVoxels/2 + 0.01。
```

- [ ] **Step 2: RED。**

- [ ] **Step 3: 实现**

`GeoMineralRules.h` 追加（`FGeoRareEarthRule` 之后、`namespace GeoMineralRules` 之前）：

```cpp
/** 砂矿：上游原生矿沿流磨碎、在河谷里沉积的 1 体素薄层。运行于阶段 6b 之后（阶段号 7）。 */
struct FGeoPlacerParams
{
	TArray<uint8> SourceMineralIds;   // 源矿编号；空 = 不撒（「无源不砂矿」）
	float MinSourceExposure = 0.f;    // 源矿最低出露比例（0–1）
	int32 MaxSteps = 96;              // 单条流路最多走多少宏观格
	int32 StepStride = 2;             // 每几个有效沉积格放一片
	int32 MaxBodiesPerSource = 24;    // 单个源矿最多几片
	float MinFlowAccumRatio = 1.f;    // 沉积格最低汇流量 = 该倍数 × Hydro.RiverThreshold
	float HalfWidthRatio = 0.0015f;   // 横跨半宽（占 R），随汇流开方放大
	float ThicknessVoxels = 1.f;      // 薄层厚度（体素）
	float DecayPerVoxel = 0.00008f;   // 品位沿流程的指数衰减系数
	bool bMouthOnly = false;          // 只在入海前最后 MouthCells 格沉积（滨海重砂）
	int32 MouthCells = 3;             // bMouthOnly 时沉积几格
	float Hard = 0.3f;                // 薄层软边宽度
};

class STELLARERAPLANET_API FGeoPlacerRule final : public IGeoMineralRule
{
public:
	FGeoPlacerParams P;
	virtual uint32 GetStage() const override { return 7; }
	virtual const TCHAR* GetRuleName() const override { return TEXT("Placer"); }
	virtual void Scatter(FGeoScatterContext& Ctx, FGeoScatterOutput& Out) const override;
	virtual uint64 HashParams() const override;
};
```

`GeoMineralRules.cpp` 追加实现。走路与发片：

```cpp
namespace
{
	int32 GeoPlacerCellIndex(const FGeoField2D& Field, const FVector3f& Dir) { /* 与测试同式 */ }
	void GeoPlacerCellFaceXY(const FGeoField2D& Field, int32 Idx, int32& Face, int32& X, int32& Y) { /* 同上 */ }
	float GeoPlacerReadAt(const FGeoField2D& Field, int32 Idx)
	{
		int32 Face, X, Y; GeoPlacerCellFaceXY(Field, Idx, Face, X, Y); return Field.At(Face, X, Y);
	}
}

void FGeoPlacerRule::Scatter(FGeoScatterContext& Ctx, FGeoScatterOutput& Out) const
{
	if (P.SourceMineralIds.IsEmpty()) { return; }
	const FPlanetBakeFields& F = Ctx.Fields;
	const int32 Res = F.FlowAccum.Res;
	if (Res <= 0 || F.FlowNext.Res != Res)
	{
		Ctx.Errors.Add(TEXT("[地质] 撒矿器「Placer」需要阶段 6 的 FlowAccum 与 FlowNext"));
		return;
	}
	const double R = Ctx.Config.Radius;
	const double Thr = FMath::Max(double(Ctx.Config.Hydro.RiverThreshold), 1e-9);
	const double MinAcc = double(P.MinFlowAccumRatio) * Thr;
	const double CellArc = 1.5707963 * R / double(Res);   // 一个宏观格的弧长
	const float HalfThick = FMath::Max(0.5f * P.ThicknessVoxels, 0.5f);
	const int32 MaxSteps = FMath::Clamp(P.MaxSteps, 1, 4096);
	const int32 Stride = FMath::Clamp(P.StepStride, 1, 64);
	const int32 MaxBodies = FMath::Clamp(P.MaxBodiesPerSource, 1, 512);
	const int32 MouthCells = FMath::Clamp(P.MouthCells, 1, MaxBodies);

	// 一片薄透镜：中心贴地表，X 轴顺流向，Z 轴沿法线。
	const auto EmitOne = [&](int32 Cell, const FVector3f& FlowDir, double Dist)
	{
		int32 Face, X, Y; GeoPlacerCellFaceXY(F.FlowAccum, Cell, Face, X, Y);
		const FVector3f Dir = F.FlowAccum.TexelDir(Face, X, Y);
		const float Elev = F.Elevation.Sample(Dir);
		const float Acc = F.FlowAccum.At(Face, X, Y);
		FVector3f T = FlowDir - Dir * FVector3f::DotProduct(Dir, FlowDir);
		if (T.SizeSquared() < 1e-12f) { T = GeoMineralRules::TangentFrame(Dir, 0.f).RotateVector(FVector3f::XAxisVector); }
		const FVector3f XAxis = T.GetSafeNormal();
		const FVector3f YAxis = FVector3f::CrossProduct(Dir, XAxis);
		const FQuat4f Rot(FMatrix44f(XAxis, YAxis, Dir, FVector3f::ZeroVector));
		const float Across = FMath::Clamp(float(P.HalfWidthRatio * R) * FMath::Sqrt(Acc / float(Thr)),
			1.f, float(4.0 * CellArc));
		const FVector3f Radii(float(0.75 * Stride * CellArc), Across, HalfThick);
		const FVector3f Center = Dir * (float(R) + Elev - 0.5f * P.ThicknessVoxels);
		FGeoPrimitive Body = FGeoPrimitive::MakeOrientedBlob(Center, Radii, Rot, P.Hard);
		Body.Grade = uint8(FMath::Clamp(FMath::RoundToInt(double(GradeMax) * FMath::Exp(-double(P.DecayPerVoxel) * Dist)),
			GradeMin, GradeMax));
		Out.Bodies.Add(Body);
	};

	for (const FGeoPrimitive& Src : F.Primitives)
	{
		if (Src.MineralId == 0 || !P.SourceMineralIds.Contains(Src.MineralId)) { continue; }
		if (float(Src.Exposure) / 255.f < P.MinSourceExposure) { continue; }
		int32 Cell = int32(GeoPlacerReadAt(F.FlowNext, GeoPlacerCellIndex(F.FlowAccum, Src.Center.GetSafeNormal()))) - 1;
		if (Cell < 0) { continue; }

		int32 Emitted = 0, ValidRun = 0, Steps = 0, PrevCell = -1;
		double Dist = 0.0;
		FVector3f PrevDir = FVector3f::ZeroVector;
		TArray<TPair<int32, double>> MouthBuf;   // (格, 到该格的流程距离)
		while (Cell >= 0 && Steps < MaxSteps && Emitted < MaxBodies)
		{
			++Steps;
			int32 Face, X, Y; GeoPlacerCellFaceXY(F.FlowAccum, Cell, Face, X, Y);
			const FVector3f Dir = F.FlowAccum.TexelDir(Face, X, Y);
			const float Elev = F.Elevation.Sample(Dir);
			const float Acc = F.FlowAccum.At(Face, X, Y);
			if (PrevCell >= 0) { Dist += R * double((Dir - PrevDir).Size()); }
			const int32 Next = int32(F.FlowNext.At(Face, X, Y)) - 1;
			FVector3f NextDir = Dir;
			if (Next >= 0)
			{
				int32 NF, NX, NY; GeoPlacerCellFaceXY(F.FlowAccum, Next, NF, NX, NY);
				NextDir = F.FlowAccum.TexelDir(NF, NX, NY);
			}
			PrevDir = Dir; PrevCell = Cell;
			if (Elev <= 0.f)   // 入海：嘴模式结算缓冲区；普通模式直接停
			{
				if (P.bMouthOnly) { for (const TPair<int32, double>& M : MouthBuf) { EmitOne(M.Key, NextDir, M.Value); } }
				break;
			}
			if (double(Acc) >= MinAcc)
			{
				if (P.bMouthOnly)
				{
					MouthBuf.Add({ Cell, Dist });
					if (MouthBuf.Num() > MouthCells) { MouthBuf.RemoveAt(0); }
				}
				else if (++ValidRun >= Stride) { ValidRun = 0; ++Emitted; EmitOne(Cell, NextDir, Dist); }
			}
			if (Next < 0) { break; }   // 内流盆地 / 无下游：嘴模式不结算（不是海）
			Cell = Next;
		}
	}
}
```

`HashParams`：

```cpp
uint64 FGeoPlacerRule::HashParams() const
{
	TArray<uint8> Sorted = P.SourceMineralIds;
	Sorted.Sort();
	uint64 H = GeoHash::Combine(GeoHash::Str("placer"),
		uint64(uint32(P.MaxSteps)) | (uint64(uint32(P.StepStride)) << 32));
	H = GeoHash::Combine(H, uint64(uint32(P.MaxBodiesPerSource)) | (uint64(uint32(P.MouthCells)) << 32)
		| (uint64(P.bMouthOnly ? 1 : 0) << 33));
	H = GeoMineralRulesHashFloats(H, { P.MinSourceExposure, P.MinFlowAccumRatio, P.HalfWidthRatio,
		P.ThicknessVoxels, P.DecayPerVoxel, P.Hard });
	for (const uint8 Id : Sorted) { H = GeoHash::Combine(H, uint64(Id)); }
	return H;
}
```

- [ ] **Step 4: 绿；提交** `feat(地质): 砂矿规则——沿 FlowNext 顺流追踪、1 体素薄层与品位衰减`

---

### Task 3: `UGeoPlacerScatterer` 与源矿名解析

**Files:**
- Modify: `Source/StellarEraPlanet/Src/Geo/Assets/GeoMineralScatterer.h`
- Modify: `Source/StellarEraPlanet/Src/Geo/Assets/GeoMineralScatterer.cpp`
- Modify: `Source/StellarEraPlanet/Src/Tests/PlanetGeoStage06cTests.cpp`

**Interfaces:**
- Consumes: `UPlanetRecipe::MineralCatalog`（`GetOuter()` 拿配方）、`UGeoMineralCatalog::FindIndex`。
- Produces: `UGeoPlacerScatterer`，属性与 `FGeoPlacerParams` 一一对应；`MakeRule()` 解析 `TArray<FName> Sources`。

- [ ] **Step 1: 失败测试** `StellarEra.Planet.Geo.Stage06c.Recipe`

```cpp
UGeoMineralCatalog* Cat = NewObject<UGeoMineralCatalog>();
for (const TCHAR* Id : { TEXT("EpithermalAuAg"), TEXT("PorphyryCu") })
{
	FGeoMineralDef D; D.Id = FName(Id); D.DisplayName = Id; Cat->Minerals.Add(D);
}
UPlanetRecipe* Recipe = NewObject<UPlanetRecipe>();
Recipe->MineralCatalog = Cat;
UGeoPlacerScatterer* S = NewObject<UGeoPlacerScatterer>(Recipe);
S->Sources = { TEXT("EpithermalAuAg"), TEXT("PorphyryCu"), TEXT("Missing") };
const TSharedPtr<IGeoMineralRule> Rule = S->MakeRule();
TestNotNull(...); 
const FGeoPlacerRule* Placer = static_cast<const FGeoPlacerRule*>(Rule.Get());
TestEqual(TEXT("两个名字解析成功、缺的跳过"), Placer->P.SourceMineralIds.Num(), 2);
TestEqual(TEXT("编号 1"), int32(Placer->P.SourceMineralIds[0]), 1);
TestEqual(TEXT("编号 2"), int32(Placer->P.SourceMineralIds[1]), 2);
// 哈希探针：每个标量参数各改一项，HashParams 必变（12 条断言）。
```

- [ ] **Step 2: RED。**

- [ ] **Step 3: 实现**

`GeoMineralScatterer.h` 末尾追加：

```cpp
/** 砂矿：上游原生矿沿流磨碎、沉积在河谷里的薄层。运行于阶段 6b 之后。 */
UCLASS(meta = (DisplayName = "砂矿 / 滨海重砂（阶段 6）"))
class STELLARERAPLANET_API UGeoPlacerScatterer : public UGeoMineralScatterer
{
	GENERATED_BODY()
public:
	UGeoPlacerScatterer() { Mineral = TEXT("AuPlacer"); }

	/** 源矿：上游必须有它才撒砂矿；从矿物目录多选。 */
	UPROPERTY(EditAnywhere, Category = "Rule", meta = (GetOptions = "GetMineralOptions"))
	TArray<FName> Sources;

	/** 源矿最低出露比例；0 = 只要有矿体。 */
	UPROPERTY(EditAnywhere, Category = "Rule", meta = (ClampMin = "0", ClampMax = "1"))
	float MinSourceExposure = 0.f;
	/** 单条流路最多追踪多少宏观格。 */
	UPROPERTY(EditAnywhere, Category = "Rule", meta = (ClampMin = "1", ClampMax = "4096"))
	int32 MaxSteps = 96;
	/** 每几个有效沉积格放一片。 */
	UPROPERTY(EditAnywhere, Category = "Rule", meta = (ClampMin = "1", ClampMax = "64"))
	int32 StepStride = 2;
	/** 单个源矿最多几片。 */
	UPROPERTY(EditAnywhere, Category = "Rule", meta = (ClampMin = "1", ClampMax = "512"))
	int32 MaxBodiesPerSource = 24;
	/** 沉积格最低汇流量 = 该倍数 × 配方里的河道阈值。 */
	UPROPERTY(EditAnywhere, Category = "Rule", meta = (ClampMin = "0", ClampMax = "20"))
	float MinFlowAccumRatio = 1.f;
	/** 横跨河谷的半宽（占半径），随汇流开方放大。 */
	UPROPERTY(EditAnywhere, Category = "Rule", meta = (ClampMin = "0.0001", ClampMax = "0.05"))
	float HalfWidthRatio = 0.0015f;
	/** 薄层厚度（体素）；1 = 规格要求的单格砂层。 */
	UPROPERTY(EditAnywhere, Category = "Rule", meta = (ClampMin = "0.5", ClampMax = "16"))
	float ThicknessVoxels = 1.f;
	/** 品位沿流程的指数衰减系数（1/体素）。 */
	UPROPERTY(EditAnywhere, Category = "Rule", meta = (ClampMin = "0", ClampMax = "0.01"))
	float DecayPerVoxel = 0.00008f;
	/** 只在入海前最后几格沉积：滨海重砂用。 */
	UPROPERTY(EditAnywhere, Category = "Rule")
	bool bMouthOnly = false;
	/** 入海前沉积几格。 */
	UPROPERTY(EditAnywhere, Category = "Rule", meta = (ClampMin = "1", ClampMax = "64", EditCondition = "bMouthOnly"))
	int32 MouthCells = 3;
	/** 薄层软边宽度（占半径）。 */
	UPROPERTY(EditAnywhere, Category = "Rule", meta = (ClampMin = "0.02", ClampMax = "1"))
	float Hard = 0.3f;

	virtual TSharedPtr<IGeoMineralRule> MakeRule() const override;
};
```

`GeoMineralScatterer.cpp` 末尾：

```cpp
TSharedPtr<IGeoMineralRule> UGeoPlacerScatterer::MakeRule() const
{
	TSharedPtr<FGeoPlacerRule> R = MakeShared<FGeoPlacerRule>();
	R->P.MinSourceExposure = MinSourceExposure;
	R->P.MaxSteps = MaxSteps;
	R->P.StepStride = StepStride;
	R->P.MaxBodiesPerSource = MaxBodiesPerSource;
	R->P.MinFlowAccumRatio = MinFlowAccumRatio;
	R->P.HalfWidthRatio = HalfWidthRatio;
	R->P.ThicknessVoxels = ThicknessVoxels;
	R->P.DecayPerVoxel = DecayPerVoxel;
	R->P.bMouthOnly = bMouthOnly;
	R->P.MouthCells = MouthCells;
	R->P.Hard = Hard;
	// 源矿按名字引用目录：找不到只跳过该名字，不能让整条烘焙失败。
	const UPlanetRecipe* Recipe = Cast<UPlanetRecipe>(GetOuter());
	const UGeoMineralCatalog* Cat = Recipe ? Recipe->MineralCatalog.Get() : nullptr;
	if (!Cat)
	{
		UE_LOG(LogTemp, Warning, TEXT("[地质] 撒矿器 %s 没有矿物目录，源矿列表为空"), *GetClass()->GetName());
		return R;
	}
	for (const FName& Name : Sources)
	{
		const int32 Index = Cat->FindIndex(Name);
		if (Index == INDEX_NONE || Index >= 255)
		{
			UE_LOG(LogTemp, Warning, TEXT("[地质] 撒矿器 %s 的源矿「%s」不在矿物目录里，跳过"), *GetClass()->GetName(), *Name.ToString());
			continue;
		}
		R->P.SourceMineralIds.Add(uint8(Index + 1));
	}
	return R;
}
```

- [ ] **Step 4: 绿；提交** `feat(地质): 砂矿撒矿器，源矿按目录名解析、参数全部可调`

---

### Task 4: 整管线集成与确定性（controller 跑全量）

**Files:**
- Modify: `Source/StellarEraPlanet/Src/Tests/PlanetGeoStage06cTests.cpp`

- [ ] **Step 1: 测试** `StellarEra.Planet.Geo.Stage06c.Pipeline`

```cpp
FPlanetRecipeConfig C; C.FaceRes = 512; C.bSingleThreadBake = true;
TSharedPtr<FGeoEpithermalRule> Gold = MakeShared<FGeoEpithermalRule>(); Gold->MineralId = 6;
TSharedPtr<FGeoPlacerRule> Placer = MakeShared<FGeoPlacerRule>();
Placer->MineralId = 20;
Placer->P.SourceMineralIds = { 6 };
Placer->P.StepStride = 1;
C.MineralRules = { Gold, Placer };

FPlanetBakeReport R;
const TSharedPtr<const FPlanetSnapshot> Snap = FPlanetBakePipeline::MakeDefault().Bake(C, R);
TestFalse(无错误); TestNotNull(快照);
int32 Placers = 0, Above = 0, Below = 0;
for (const FGeoPrimitive& B : Snap->Primitives.GetPrimitives())
{
	if (B.MineralId != 20) { continue; }
	++Placers;
	const FVector3f Dir = B.Center.GetSafeNormal();
	const double Surf = double(Snap->R0) + double(Snap->Elevation.Sample(Dir));
	Above += double(B.Center.Size()) > Surf + 1.0 ? 1 : 0;
	Below += double(B.Center.Size()) < Surf - 4.0 ? 1 : 0;
}
AddInfo(...);
TestTrue(TEXT("有砂金产出"), Placers > 0);
TestEqual(TEXT("砂金不高于地表 1 体素"), Above, 0);
TestEqual(TEXT("砂金不埋深 4 体素以上"), Below, 0);
TestTrue(TEXT("沉积规则阶段号是 7"), Placer->GetStage() == 7);
TestTrue(TEXT("默认管线含阶段 7"), FPlanetBakePipeline::MakeDefault().Sorted().ContainsByPredicate(
	[](const IPlanetStage* S) { return S->GetStageIndex() == 7; }));
```

再补：
- `Stage06c.PlacerOnRivers`：手动跑 2→3→4→5→6→7（`FStage06bExposure`）再按管线方式撒矿，保留 `FlowAccum`；断言片中心都在 `FlowAccum ≥ RiverThreshold` 的陆上格。
- `Stage06c.Determinism`：同配方两次烘焙 `Snap->Primitives.Hash()` 一致；`bSingleThreadBake` 开/关一致。

- [ ] **Step 2: 迭代跑** `StellarEra.Planet.Geo.Stage06c` + `StellarEra.Planet.Geo.Stage06` + `StellarEra.Planet.Geo.Debug`。

- [ ] **Step 3: 全量 `StellarEra`**（基线 343；有回归先修）。

- [ ] **Step 4: 提交** `test(地质): 阶段 6c 整管线、河道位置与确定性`

---

### Task 5: 资产与编辑器目检（controller，MCP）

- [ ] `DA_MineralCatalog` 追加 5 条：`AuPlacer 砂金`（金黄）、`SnPlacer 砂锡`（灰白）、`PtPlacer 砂铂`（银灰）、`DiamondPlacer 砂金刚石`（冰蓝）、`HeavySand 滨海重砂`（深褐）。
- [ ] `DA_GeoRecipe_Archipelago` 追加 5 个 `UGeoPlacerScatterer`：

  | 矿物 | Sources（默认） | 备注 |
  |---|---|---|
  | `AuPlacer` | `EpithermalAuAg`、`PorphyryCu` | 斑岩铜伴生金 |
  | `SnPlacer` | `TinTungsten` | |
  | `PtPlacer` | `ChromiumPGE` | |
  | `DiamondPlacer` | `Diamond` | |
  | `HeavySand` | 上述四种源矿全部 | `bMouthOnly = true` |
- [ ] 保存后**从磁盘字节复核**（矿物 Id 字符串、撒矿器实例数），不拿 `save_asset` 返回值当证据。
- [ ] 编辑器重烘 `GeoPlanetMap`：导出图层，切面看谷底 / 海岸的砂矿条带；报告每种砂矿片数与阶段 7 耗时。
- [ ] 停下等用户验收。
