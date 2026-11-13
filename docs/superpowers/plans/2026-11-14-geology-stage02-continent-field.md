# 阶段 2 重做：整体大陆场 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 用「球谐框架 + warp 细节 + 立体角加权分位数 + 面积–高度曲线」的全球连续大陆场取代陆核模型，三种格局可由配方切换，并在 `GeoPlanetMap` 上三星并排验收。

**Architecture:** 新增无状态的 `GeoContinentField`（`Geo/Bake/`）负责框架、细节、加权 CDF、高程曲线四个纯函数；`FStage02Plates::Bake` 改为调用它写 `Elevation` 与派生宏观场，板块 Voronoi 那一半不动。快照与求值核接口不变，只把 `CrustSplitElevation` 改由阶段写入。

**Tech Stack:** UE 5.8.3 C++，`FGeoField2D` cubemap，`GeoNoise`/`FGeoRng` 确定性随机，UE Automation Test，VibeUE Python MCP。

**Spec:** `docs/superpowers/specs/2026-11-14-geology-stage02-continent-field-design.md`

## Global Constraints

- 只做大陆形状与高程；**不做矿、线索、岩性细分、山脉、水体**。
- 默认配方 R=40000、FaceRes=1024、Seed=1337；全尺寸烘焙 ≤ 10 s、快照 ≤ 60 MB（沿用阶段 2 验收口径）。
- 确定性：同配方两次、单线程与并行逐位一致；随机只来自 `FGeoRng`/`GeoHash`。
- 可调参数全部进 `FPlanetRecipeConfig::Hash()`。
- 测试阈值由实测定后写入，不手写猜数。
- 构建前编辑器必须关闭；构建命令只用 CLAUDE.md「构建与测试」一节那条 dotnet 直调。
- 含中文源文件存 UTF-8 with BOM；注释只写为什么，单处 ≤ 3 行。
- 提交带 pathspec：`git commit -- <paths>`；结尾 `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`。

---

## File Structure

| 文件 | 动作 | 职责 |
|---|---|---|
| `Source/StellarEraPlanet/Src/Geo/Bake/GeoContinentField.h/.cpp` | 新建 | 框架球谐、细节噪声、加权 CDF、高程曲线 |
| `Source/StellarEraPlanet/Src/Geo/Bake/PlanetRecipeConfig.h` | 改 | 新 `FPlanetContinentParams`；`FPlanetPlateParams` 删陆核/风格字段；`Hash()` |
| `Source/StellarEraPlanet/Src/Geo/Bake/GeoPlates.h/.cpp` | 改 | 删 `FGeoContinentCore`、`Cores`、`Style*` |
| `Source/StellarEraPlanet/Src/Geo/Bake/PlanetBakeTypes.h` | 改 | 删 `StyleHeight`；加 `CrustSplitElevation` |
| `Source/StellarEraPlanet/Src/Geo/Bake/Stages/Stage02Plates.cpp` | 重写大陆部分 | 调 `GeoContinentField` |
| `Source/StellarEraPlanet/Src/Geo/Bake/PlanetBakePipeline.cpp` | 改 | `CrustSplitElevation` 取自 Fields |
| `Source/StellarEraPlanet/Src/Geo/Assets/PlanetRecipe.h/.cpp` | 改 | `Stage2|Continents` 六项 |
| `Source/StellarEraPlanet/Src/Geo/Snapshot/PlanetDebugColor.h` | 新建 | 按高程分色 |
| `Source/StellarEraPlanet/Src/Geo/Voxel/PlanetGeoGenerator.h/.cpp` | 改 | `EPlanetGeoDebugColor` 取代 bool |
| `Source/StellarEraPlanet/Src/VoxelPlanetActor.h/.cpp` | 改 | `GeoDebugColor` 属性，推给生成器、进哈希 |
| `Source/StellarEraPlanet/Src/Tests/PlanetGeoContinentFieldTests.cpp` | 新建 | 纯函数 + 格局测试 |
| `Tests/PlanetGeoStage02Tests.cpp`、`…AcceptanceTests.cpp`、`…RecipeTests.cpp`、`…PlateFrameTests.cpp`、`…EvalStage02Tests.cpp` | 改 | 跟随删字段与新判据 |
| `Content/Code/Planet/DA_GeoRecipe_{Earthlike,Supercontinent,Archipelago}` | 新建资产 | 三套格局 |
| `Content/Code/Maps/GeoPlanetMap.umap` | 改 | 三星并排 |

## Interfaces（全部任务共用）

```cpp
// PlanetRecipeConfig.h
struct FPlanetContinentParams
{
	int32 FrameDegree = 4;            // 1–12
	float LandFrac = 0.30f;
	float CoastRoughness = 0.35f;     // 细节 std / 框架 std
	float ShelfFrac = 0.15f;          // 占海洋面积
	float LandHeightVoxels = 240.f;
	float OceanDepthVoxels = 480.f;
	// 以下不暴露，实测定
	float FrameSpectralDecay = 1.0f;  // 每阶功率 ∝ l^-decay
	float DetailFrequency = 3.0f;
	int32 DetailOctaves = 5;
	float DetailWarpAmp = 0.15f;
	float LandCurvePower = 1.6f;
	float ShelfDepthRatio = 0.1f;     // 陆架底 = 比例 × OceanDepth
	float SlopeFrac = 0.12f;          // 陆坡占海洋面积
	float AbyssRatio = 0.85f;         // 陆坡底 = 比例 × OceanDepth
};

// GeoContinentField.h
namespace GeoContinentField
{
	struct FFrame { int32 Degree = 0; TArray<double> Coeffs; };  // 下标 l*l + l + m
	FFrame BuildFrame(const FPlanetRecipeConfig& Config);
	float EvalFrame(const FFrame& Frame, const FVector3f& Dir);
	float EvalDetail(uint64 Seed, const FPlanetContinentParams& P, const FVector3f& Dir);
	double TexelWeight(const FGeoField2D& Field, int32 X, int32 Y);   // 立体角 ∝ (1+u²+v²)^-1.5
	struct FWeightedCdf
	{
		float Min = 0.f, Max = 0.f; TArray<double> Cum; double Total = 0.0;
		float Rank(float V) const;     // ∈ [0,1]
	};
	FWeightedCdf BuildCdf(const FGeoField2D& Field);
	float Elevation(float Rank, const FPlanetContinentParams& P);   // 体素，Rank=1−LandFrac 处为 0
}
```

`FPlanetBakeFields` 新增 `float CrustSplitElevation`，删 `StyleHeight`。`LandAreaFrac` 保留，改为加权 `Σw[E>0]/Σw`。

---

### Task 1: GeoContinentField 纯函数

**Files:** Create `Geo/Bake/GeoContinentField.h/.cpp`；Modify `PlanetRecipeConfig.h`（只加 `FPlanetContinentParams` 与成员 `Continents`，暂不删旧字段）；Test `Tests/PlanetGeoContinentFieldTests.cpp`（`StellarEra.Planet.Geo.ContinentField`）。

- [ ] 写测试：①`EvalFrame` 对 Degree=1 是线性函数（`F(d)+F(−d)≈0`，误差 < 1e-5）②同配方 `BuildFrame` 系数逐位一致、换种子不同 ③`Elevation` 在 `1−LandFrac` 处为 0、单调不减、两端分别为 `−OceanDepth` 与 `+LandHeight` ④`BuildCdf` 对线性场 `Rank` 单调、`Rank(Max)=1`、`Rank(Min)=0` ⑤`TexelWeight` 面中心 > 面角。
- [ ] 跑测试确认失败（未定义符号 → 编译失败即为失败）。
- [ ] 实现：实球谐用标准连带勒让德递推 + `K_lm=√((2l+1)/4π·(l−m)!/(l+m)!)`；系数 `a_lm = N(0,1)·√(l^-decay/(2l+1))`（Box–Muller，`FGeoRng::Derive(Seed,2,Str("continent-frame"))`）；细节 = 3 维 warp（2 八度、频率 `0.5·DetailFrequency`）后 `Fbm3(Freq, Octaves)`；CDF 用 65536 桶加权直方图，桶内线性插值；高程曲线按 spec §3 分段（陆地 `t^p`，陆架线性，陆坡 smoothstep，深海线性）。
- [ ] 跑测试通过。
- [ ] Commit。

### Task 2: 阶段 2 改用大陆场，删陆核与风格

**Files:** Modify `Stage02Plates.cpp`、`GeoPlates.h/.cpp`、`PlanetBakeTypes.h`、`PlanetRecipeConfig.h`（删旧字段、`Hash()` 改成 Plates 6 项 + Continents 14 项）、`PlanetBakePipeline.cpp`、`PlanetRecipe.h/.cpp`；Tests `Stage02`、`Acceptance`、`Recipe`、`PlateFrame`、`EvalStage02`、`Stage01`。

阶段流程：
1. `F_frame` 全分辨率 Fill（直接求球谐，不采样宏观图，避免立方体棱接缝）；`F_detail` 同理。
2. 串行求两者加权 std，`F = frame/σf + CoastRoughness·detail/σd`。
3. `Cdf = BuildCdf(F)`；`Elevation(Face,X,Y) = Elevation(Cdf.Rank(F))`。
4. `LandAreaFrac` = 加权 `E>0` 占比；`CrustSplitElevation = −ShelfDepthRatio·OceanDepth`。
5. 宏观场循环：`M = smoothstep(qc−0.05, qc+0.05, Rank)`（宏观 texel 处采样 F 后求 Rank）；`ContinentalGrad` 用原 GradFull 的差分写法作用于宏观 M；`CrustType = E>0 ? 1 : (E ≥ CrustSplit ? 2 : 0)`；其余板块场原样。

- [ ] 改测试：Stage02 删「陆核非空」「风格场分辨率」「海岸 h_base」，加：加权陆地占比 `|·−0.30| ≤ 0.01`、`CrustType` 与 `E` 一致、记录值域一致、确定性三条保留；Acceptance 删「8 大圆穿越 ≤12」只留 ≥2，其余保留；Recipe 改六项；PlateFrame 删两条核断言；EvalStage02 `TargetLandFrac` → `Continents.LandFrac`。
- [ ] 构建（编辑器关）→ 跑 `StellarEra.Planet.Geo` 确认新断言失败的那几条按预期失败（或编译失败）。
- [ ] 实现上述流程，删 `FGeoContinentCore` / `Cores` / `Style*` / `StyleHeight`。
- [ ] 构建 + 全部 `StellarEra.Planet.Geo` 通过；记录全尺寸烘焙秒数、MB。
- [ ] Commit。

### Task 3: 格局与形状测试（阈值实测）

**Files:** Modify `Tests/PlanetGeoContinentFieldTests.cpp`，新增 `StellarEra.Planet.Geo.ContinentField.Pattern`。

工具（文件名前缀 `ContinentField`，避免 unity 合并重名）：在 128 分辨率网格上按 `Elevation.Sample(TexelDir)>0` 得陆地，邻接通过 `FaceToDir(U±du)`→`DirToFace` 跨面；BFS 求连通块，按 `TexelWeight` 计面积。「主陆块」= 面积 ≥ 1% 星球。

- [ ] 断言：Degree=1 最大陆块 ≥ 80% 陆地；Degree=6 主陆块数 > Degree=4 主陆块数；棱两侧 ±0.5 texel 处 |ΔE| ≤ 3×面内同间距最大 |ΔE|+1；相邻 texel 最大坡度（|ΔE|/弧长体素）≤ 实测上界；半数以上主陆块的质心方向为陆地（环形陆地回归）。先用 `AddInfo` 打出实测，再定阈值。
- [ ] 通过后 Commit。

### Task 4: 按高程调试着色

**Files:** Create `Geo/Snapshot/PlanetDebugColor.h`；Modify `PlanetGeoGenerator.h/.cpp`、`GeoTestPlanetActor.cpp`（若引用 bool）、`VoxelPlanetActor.h/.cpp`；Test 加进 `ContinentField` 测试（颜色函数：深海、陆架、海岸、平原、高地五档各取一点断言颜色不同且海蓝分量 > 红）。

- [ ] `UENUM enum class EPlanetGeoDebugColor : uint8 { None, Lithology, Elevation }`；生成器 `VisibleAnywhere` 成员 `DebugColor = Lithology`；`AVoxelPlanetActor::GeoDebugColor`（EditAnywhere，默认 Elevation）在 `RebuildGeoPlanet` 推给生成器并进 `ComputeParamsHash`。
- [ ] 地表以下 > 16 体素仍用岩性色。
- [ ] 构建 + 测试 + Commit。

### Task 5: 三套配方与验收关卡

**Files:** 资产 `DA_GeoRecipe_Earthlike/Supercontinent/Archipelago`（`/Game/Code/Planet/`）；`GeoPlanetMap.umap`。

- [ ] 启动编辑器（后台进程），Python 建三份配方：Earthlike（默认）、Supercontinent（FrameDegree 2，LandFrac 0.30）、Archipelago（FrameDegree 8，CoastRoughness 0.8，LandFrac 0.25）。
- [ ] 关卡三星：沿 X 轴 −8e6 / 0 / +8e6 厘米；每颗 `registers_with_simulation=False`、`child_actor=None`、`geo_debug_color=Elevation`、各挂一份配方；大气与云保持开；PlayerStart 与灯照旧。保存后**重载读回**确认。
- [ ] PIE 实拍三颗星（等 chunk 建完再请求截图，再 end PIE 出帧），查看画面：无圆盘、无圆洞、海岸不规则、无陡崖、三种格局可区分。不达标回到 Task 1 调不暴露参数。
- [ ] Commit 资产 + 关卡。

### Task 6: 收尾

- [ ] 跑整包 `StellarEra` 自动化，0 失败。
- [ ] 更新 `.superpowers/sdd/…/progress.md` 与 spec 状态行。
- [ ] 报告实测数与截图路径，**停下等用户验收形状**，不撒矿。
