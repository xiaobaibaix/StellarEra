# 地质阶段 4c-2 宿主型岩浆矿床 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development.

**Goal:** 碳酸岩岩性 + 六种宿主型矿（锡钨 / 伟晶岩 / 铜镍 / 铬铂 / 金刚石 / 稀土）。

**Architecture:** 碳酸岩由 4b 放置；六条 `IGeoMineralRule`（stage 4）遍历 `Fields.Primitives`，按 `LithologyId` 找宿主，在宿主局部空间（`Rotation`）里放矿体。配置面是六个 `UGeoMineralScatterer` 子类。

**Tech Stack:** UE 5.8 C++，`StellarEraPlanet`，Automation Test。

**Spec:** `docs/superpowers/specs/2026-11-15-geology-stage04c2-hosted-deposits-design.md`

## Global Constraints

- 尺寸用占 R 的比例；注释 WHY-only ≤3 行中文；中文源文件 UTF-8 with BOM。
- 参数进 `HashParams()`；提交 `git commit -- <paths>`；尾行 `Co-Authored-By: Claude Code <noreply@anthropic.com>`。
- 编辑器必须先关（Live Coding 锁 DLL）。BUILD/TEST 命令与 4c-1 相同（过滤 `StellarEra`，基线 311）。
- 宿主只认 `MineralId == 0` 的原语；已撒矿的矿体不得再当宿主。

## 文件

| 文件 | 动作 |
|---|---|
| `Geo/Core/GeoLithology.h` | 加 `Carbonatite = 15`、Count 16、名字与颜色 |
| `Geo/Bake/PlanetRecipeConfig.h` | `FPlanetIntrusionParams` 加 5 个碳酸岩参数与哈希 |
| `Geo/Bake/Stages/Stage04bIntrusions.cpp` | 碳酸岩放置段 |
| `Geo/Bake/Stages/Stage04Magmatism.cpp` | `IntrusionType` 紧凑编号/图例/`RayHits` 加碳酸岩 |
| `Geo/Bake/GeoMineralRules.{h,cpp}` | 六组 Params + Rule |
| `Geo/Assets/GeoMineralScatterer.{h,cpp}` | 六个 Scatterer 子类 |
| `Geo/Assets/PlanetRecipe.{h,cpp}` | 碳酸岩 5 个属性（撒矿器本身是列表项，不需要新 UPROPERTY） |
| `Tests/PlanetGeoStage04c2Tests.cpp` | 新建 |

测试辅助（顶部匿名命名空间，前缀 `Stage04c2`）：`Stage04c2TestBake(C, F, R)` 跑阶段 2→3→4，然后依次执行 `C.MineralRules`（与 4c-1 的辅助同构）。

宿主局部空间判定（测试里独立重写，与实现形成交叉验证）：

```cpp
FVector3f Stage04c2Local(const FGeoPrimitive& Host, const FVector3f& P)
{
	const FVector3f D = P - Host.Center;
	const FVector3f L = Host.Kind == EGeoPrimitiveKind::OrientedBlob ? Host.Rotation.UnrotateVector(D) : D;
	return L / Host.Radii;
}
```

---

### Task 1: 碳酸岩岩性与放置

- [ ] **Step 1: 失败测试** `Stage04c2.Carbonatite`：`GeoLithology::Count == 16`、`DisplayName(Carbonatite)` 非空；烘焙后存在 `LithologyId == Carbonatite` 的 Blob；每个都满足 `ReliefType==6`、`Elevation>0`；半径减半后数量相同。
- [ ] **Step 2: RED。**
- [ ] **Step 3: 实现**
  - `GeoLithology`：`constexpr uint8 Carbonatite = 15; constexpr uint8 Count = 16;`，名字「碳酸岩」，色 (200,180,210)。
  - `FPlanetIntrusionParams` 加：
    ```cpp
    bool bCarbonatites = true;              // 大陆裂谷的碳酸岩体（稀土的宿主）
    float CarbonatiteRadiusRatio = 0.006f;  // 半径
    float CarbonatiteTopDepthRatio = 0.01f; // 顶深
    float CarbonatiteChance = 0.35f;        // 每段裂谷出现概率
    float CarbonatiteSpacingRatio = 0.12f;  // 候选去重间距
    ```
    进 `Hash()`（bool 位打包 + 4 个 float）。
  - `Stage04bIntrusions.cpp`：在金伯利岩循环之后、蚀变晕之前，扫 `ReliefType==6 && Elevation>0` 的宏观 texel，`Stage04bFarFrom` 去重 `CarbonatiteSpacingRatio`，`Rng.NextFloat() < Chance` 时放 `Blob`（独立 RNG：`Derive(Cfg.Seed, 4, GeoHash::Str("carbonatite"))`）。
  - `Stage04Magmatism.cpp`：`Stage04CompactLitho` 加 `Carbonatite → 6`，图例加「碳酸岩」颜色；`RayHits` 的 `LithologyId >= Granite` 判据天然覆盖。
- [ ] **Step 4: 绿；提交。**

---

### Task 2: 锡钨 + 伟晶岩

- [ ] **Step 1: 失败测试** `Stage04c2.TinTungsten`、`Stage04c2.Pegmatite`：
  - 每个锡钨体到最近 `Granite` 宿主：局部 `|z| ∈ [0.5, 1.02]`、`|x|,|y| ≤ 0.6`；数量 = 岩基数 × `PerBatholith`（±5%）。
  - 每个伟晶岩体到最近 `Granite`：局部半径 ∈ [0.8, 1.05]。
  - 无宿主的矿体数 = 0。
- [ ] **Step 2: RED。**
- [ ] **Step 3: 实现**
  ```cpp
  struct FGeoTinTungstenParams { int32 PerBatholith=4; float LengthRatio=0.02f, RadiusRatio=0.004f, ThicknessRatio=0.0015f, TopMinRatio=0.6f, TopMaxRatio=0.95f; };
  struct FGeoPegmatiteParams   { int32 PerBatholith=6; float RadiusRatio=0.003f, ShellMin=0.88f, ShellMax=1.f; };
  ```
  锡钨：对每个 Granite 宿主，`Rot = Host.Rotation`，位置 = `Center + Rot*(X*Rx*u, Y*Ry*v, Z*Rz*zr)`（u,v ∈ [-0.5,0.5]，zr ∈ [TopMin,TopMax]），朝向 `Rot`，`Radii = (LengthRatio, RadiusRatio, ThicknessRatio)*R`（局部 X 长、Z 厚）。
  伟晶岩：球面随机方向 `u`，半径比例 `s ∈ [ShellMin, ShellMax]`，位置 = `Center + Rot*(u * Radii * s)`，`Blob`，半径 `RadiusRatio*R`。
  两个 Scatterer 子类，`Mineral = TEXT("TinTungsten") / TEXT("PegmatiteLi")`。
- [ ] **Step 4: 绿；提交。**

---

### Task 3: 铜镍 + 铬铂

- [ ] **Step 1: 失败测试**：
  - `Stage04c2.CopperNickel`：每个矿体到最近 `Gabbro` 宿主，局部 `z ∈ [-1.02,-0.4]`、`|x|,|y| ≤ 1`。
  - `Stage04c2.ChromiumPGE`：每个矿体到最近 `LayeredMafic` 宿主，中心距离 ≤ 0.05R、朝向点积 ≥ 0.99、半厚 ≤ 0.5 宿主 `Rz`。
- [ ] **Step 2: RED。**
- [ ] **Step 3: 实现**
  ```cpp
  struct FGeoCopperNickelParams { int32 PerHost=1; float WidthScale=0.7f, ThicknessScale=0.3f; };
  struct FGeoChromiumPGEParams { int32 PerSlab=1; float ThicknessScale=0.4f; };
  ```
  铜镍：位置 = `Center + Rot*(0,0,-Rz*Lerp(0.5,0.9,rng))`，`OrientedBlob` 与宿主同朝向，`Radii = (Rx*WidthScale, Ry*WidthScale, Rz*ThicknessScale)`。
  铬铂：与宿主同中心同朝向，`Radii = (Rx, Ry, Rz*ThicknessScale)`，`Hard = 0.05`。
  Scatterer：`Mineral = TEXT("CopperNickel") / TEXT("ChromiumPGE")`。
- [ ] **Step 4: 绿；提交。**

---

### Task 4: 金刚石 + 稀土（controller 收尾）

- [ ] **Step 1: 失败测试** `Stage04c2.Diamond`、`Stage04c2.RareEarth`：
  - 金刚石：到最近 `Kimberlite` 宿主局部半径 ≤ 1.05；含矿管 / 总管 ∈ [0.05, 0.3]。
  - 稀土：到最近 `Carbonatite` 宿主局部半径 ≤ 1.05。
  - 开关：去掉对应撒矿器后为 0；两次烘焙哈希一致。
- [ ] **Step 2: RED / 实现 / GREEN**（`FGeoDiamondParams { PerPipe=1; RadiusRatio=0.0012f; FertileChance=0.15f; }`、`FGeoRareEarthParams { PerBody=1; RadiusScale=0.6f; }`）。
- [ ] **Step 3（controller）**：目录追加 6 项（TinTungsten / PegmatiteLi / CopperNickel / ChromiumPGE / Diamond / RareEarth），配方挂 6 个撒矿器；`modify()+save_asset` 保存并字节验收。
- [ ] **Step 4（controller）**：全量 `StellarEra` 回归；重烘并导出矿图，报告六种矿数量。
