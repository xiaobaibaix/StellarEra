# 地质阶段 4c-1 火山竖向矿柱 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development.

**Goal:** 硫磺结壳 / 浅成低温金银脉 / 斑岩铜浸染体三条竖向矿柱，挂在阶段 4 的火山与岩株上。

**Architecture:** 三条 `IGeoMineralRule`（`GetStage() == 4`）读 `Fields.Volcanoes` 与 `Fields.Primitives`，产出矿体原语或地表覆层；管线自动补 `MineralId`、进快照与 BVH。配置面是三个 `UGeoMineralScatterer` 子类。

**Tech Stack:** UE 5.8 C++，`StellarEraPlanet`，Automation Test。

**Spec:** `docs/superpowers/specs/2026-11-15-geology-stage04c1-vertical-columns-design.md`

## Global Constraints

- 尺寸用占 R 的比例；深度换算 `ratio × Cfg.Radius`（体素）。
- 参数带中文注释，进 `HashParams()`。
- 注释 WHY-only ≤3 行；中文 `.h/.cpp` UTF-8 with BOM。
- 提交 `git commit -- <paths>`；尾行 `Co-Authored-By: Claude Code <noreply@anthropic.com>`。
- 编辑器必须先关；BUILD/TEST 命令同前几阶段（`UnrealEditor-Cmd -nullrhi`，过滤 `StellarEra`）。

## 文件

| 文件 | 动作 |
|---|---|
| `Geo/Bake/GeoMineralRules.{h,cpp}` | 新增三组 Params + Rule |
| `Geo/Assets/GeoMineralScatterer.{h,cpp}` | 新增三个 Scatterer 子类 |
| `Tests/PlanetGeoStage04cTests.cpp` | 新建，三条规则 + 竖向顺序测试 |
| `Content/Code/Planet/Geology/MineralData/DA_MineralCatalog.uasset` | 增三种矿（Task 3，编辑器 MCP） |

公共测试辅助（文件顶部匿名命名空间，前缀 `Stage04c`；`FGeoScatterContext` 的构造参数按 `GeoMineralRule.h` 实际签名调整）：

```cpp
FGeoField2D Stage04cTestBake(const FPlanetRecipeConfig& C, FPlanetBakeFields& F, FPlanetBakeReport& R)
{
	FPlanetBakeContext C2{ C, F, FGeoRng::Derive(C.Seed, 2, 0), R }; FStage02Plates().Bake(C2);
	FPlanetBakeContext C3{ C, F, FGeoRng::Derive(C.Seed, 3, 0), R }; FStage03Terrain().Bake(C3);
	FPlanetBakeContext C4{ C, F, FGeoRng::Derive(C.Seed, 4, 0), R }; FStage04Magmatism().Bake(C4);
	for (const TSharedPtr<const IGeoMineralRule>& Rule : C.MineralRules)
	{
		if (!Rule) { continue; }
		FGeoRng Rng = FGeoRng::Derive(C.Seed, Rule->GetStage(), Rule->Hash());
		FGeoScatterContext SC{ C, F, Rng, R.Errors };
		FGeoScatterOutput Out;
		Rule->Scatter(SC, Out);
		for (FGeoPrimitive& B : Out.Bodies) { B.MineralId = Rule->MineralId; F.Primitives.Add(B); }
		for (FGeoMineralOverlay& O : Out.Overlays) { O.MineralId = Rule->MineralId; F.MineralOverlays.Add(MoveTemp(O)); }
	}
	return F.Elevation;
}
```

---

### Task 1: 硫磺结壳

**Files:** `Geo/Bake/GeoMineralRules.{h,cpp}`、`Geo/Assets/GeoMineralScatterer.{h,cpp}`、`Tests/PlanetGeoStage04cTests.cpp`

- [ ] **Step 1: 写失败测试** `StellarEra.Planet.Geo.Stage04c.Sulfur`
  - 配置 FaceRes 512，`MineralRules = { MakeShared<FGeoSulfurRule>() }`，`MineralId = 1`。
  - 断言：有覆层且 `Mask.Res == max(128, FaceRes/2)`；遮罩非零的每个 texel 方向到最近 `bActive && CraterRadius>0` 火山的弦长 ≤ 1.2 × 其火口半径；每座这样的火山中心处 `Mask.SampleNearest > 0.5`；`DepthVoxels` 在 10–200 之间。
- [ ] **Step 2: BUILD + 跑测试看 RED。**
- [ ] **Step 3: 实现**

```cpp
struct FGeoSulfurParams
{
	float RadiusRatio = 0.008f;   // 结壳半径下限（占 R）：火口比它小时用它
	float DepthVoxels = 40.f;     // 结壳厚度
};
class STELLARERAPLANET_API FGeoSulfurRule final : public IGeoMineralRule
{
public:
	FGeoSulfurParams P;
	virtual uint32 GetStage() const override { return 4; }
	virtual const TCHAR* GetRuleName() const override { return TEXT("Sulfur"); }
	virtual uint64 HashParams() const override;
	virtual void Scatter(FGeoScatterContext& Ctx, FGeoScatterOutput& Out) const override;
};
```
`Scatter`：遍历 `Ctx.Fields.Volcanoes` 收集 `bActive && CraterRadius > 0` 的 `(Dir, Max(CraterRadius, RadiusRatio))`；
`O.Mask.Init(FMath::Max(128, Ctx.Fields.Elevation.Res / 2))`；`O.Mask.Fill` 对每个方向取到最近火山的弦长，返回 `1 - SmoothStep(R*0.8, R*1.2, Chord)`；
`O.DepthVoxels = FMath::Clamp(P.DepthVoxels, 2.f, 400.f)`；`O.Grade = RandomGrade(Ctx.Rng)`；`Out.Overlays.Add(MoveTemp(O))`。

`UGeoSulfurScatterer`（`Mineral = TEXT("Sulfur")`，两个 UPROPERTY 中文注释，`MakeRule()` 拷贝参数）。
- [ ] **Step 4: BUILD + 测试转绿；提交。**

---

### Task 2: 浅成金银 + 斑岩铜 + 竖向顺序

- [ ] **Step 1: 写失败测试** `Stage04c.Epithermal`、`Stage04c.Porphyry`、`Stage04c.Column`
  - Epithermal：每个 `bActive` 火山正下方 0.004–0.012R 内至少 1 次命中该矿；0.002R 与 0.02R 处不命中。
  - Porphyry：每个原语中心到最近闪长岩原语 ≤ 1.5 × 岩株半径；顶面低于地表。
  - Column：每座活跃层状火山沿 `dir` 从地表向下每 0.001R 采样，`PlanetMaterialEval::Sample(...).Mineral` 压成连续段，段序列必须是 `[硫磺, 金银, 斑岩铜]`，三段非空。
- [ ] **Step 2: RED。**
- [ ] **Step 3: 实现**

```cpp
struct FGeoEpithermalParams
{
	float DepthMinRatio = 0.004f;  // 脉体深度下限（占 R）
	float DepthMaxRatio = 0.012f;  // 脉体深度上限
	int32 VeinCount = 3;           // 每座火山的脉体条数
	float VeinRadiusRatio = 0.004f;// 单条脉半径
	float VeinThicknessRatio = 0.0015f; // 单条脉半厚（局部 Z）
	float SpreadRatio = 2.0f;      // 水平散布 = 该倍数 × 火口半径
};
struct FGeoPorphyryParams
{
	float DepthMinRatio = 0.012f;  // 浸染体深度下限
	float DepthMaxRatio = 0.03f;   // 上限
	float RadiusRatio = 0.01f;     // 浸染体半径
	int32 PerStock = 1;            // 每座岩株几个
};
```
Epithermal：`Frame = GeoMineralRules::TangentFrame(Dir, rng)`，`Along = Frame.RotateVector(XAxis)`，第 k 条中心 = `(Dir + Along * (k - (N-1)/2) * Radius * 1.2)` 归一化后按 `S - Lerp(Min,Max,rng)*R` 定位；`OrientedBlob`，`Radii = (Radius, Radius, Thickness) × R`，`Hard = 0.06`。
Porphyry：对每个 `LithologyId == GeoLithology::Diorite` 的原语，`Dir = Center.GetSafeNormal()`，中心半径 = `R0 + Elevation(Dir) - Mid*R`，`Blob`，`Radii = RadiusRatio*R` 各轴，`Hard = 0.25`。
三个 `UGeoMineralScatterer` 子类，中文注释，`Mineral = EPITHERMAL/TEXT("EpithermalAuAg")`、`TEXT("PorphyryCu")`。
- [ ] **Step 4: 绿；提交。**

---

### Task 3: 目录、配方与目检（controller）

- [ ] 编辑器 MCP 往 `DA_MineralCatalog` 追加三项：Sulfur（亮黄 240,220,40）、EpithermalAuAg（淡金 230,190,90）、PorphyryCu（铜绿 60,180,150）。
- [ ] 全量 `StellarEra` 测试（基线 304 + 新增 4 条）。
- [ ] 给 `DA_GeoRecipe_Archipelago` 添加三个撒矿器实例，重烘 `Geo_Archipelago`，导出 `Stage0_Mineral_*`，确认火口黄色结壳与下方金银、斑岩铜；报告数量与截图路径。
