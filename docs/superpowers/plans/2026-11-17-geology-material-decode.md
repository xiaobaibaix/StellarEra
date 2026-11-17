# 地质材质解码（MF_GeoDecode）Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development.

**Goal:** 让游戏画面显示岩性 / 矿种：生成器写 `U1` 表面遮罩，运行时生成 `256×3` 调色板 LUT，新增材质函数 `MF_GeoDecode` 与材质 `M_GeoPlanet`，地表保持生物群系、挖开处显示岩性。

**Architecture:** 编码通道已存在（`GeoMaterialCodec`），只补一个通道语义（`U1`）。材质侧全部由代码构建（照 `PlanetMineralMaterialBuilder` 的表达式拼装），不手改 uasset；颜色来自运行时 LUT，随矿物目录自动同步。

**Spec:** `docs/superpowers/specs/2026-11-17-geology-material-decode-design.md`

## Global Constraints

- 注释 WHY-only ≤3 行中文；中文源文件 UTF-8 with BOM。
- 本任务不加生成器参数。
- 提交 `git commit -m ... -- <paths>`；尾行 `Co-Authored-By: Claude Code <noreply@anthropic.com>`；Task 1–4 不碰 `Content/`，Task 5 只新增材质资产。
- 若 BUILD 报 Live Coding 或 `LNK1104`，立即 BLOCKED，不改名绕过。
- 迭代只跑 `StellarEra.Planet.Geo.Material`、`StellarEra.Planet.Geo.Generator`、`StellarEra.Planet.Geo.MaterialCodec`、`StellarEra.Planet.Geo.Debug`；全量 `StellarEra` 最后跑一次（基线 377）。
- 编辑器关闭才能构建；材质资产构建（Task 5）需要编辑器。

## 文件

| 文件 | 动作 |
|---|---|
| `Geo/Snapshot/GeoMaterialCodec.h` | `SurfaceMaskChannel` 常量与注释 |
| `Geo/Voxel/PlanetGeoGenerator.cpp` | 写 `U1` 表面遮罩 |
| `Geo/Snapshot/GeoPalette.{h,cpp}` | 新建：LUT 纯函数 |
| `VoxelPlanetActor.{h,cpp}` | 建 LUT 纹理、绑 `GeoPalette`、默认材质回退 |
| `Geo/Voxel/GeoMaterialBuilder.{h,cpp}` | 新建：`GeoMaterialBuild::BuildAll()` + 控制台命令 |
| `Tests/PlanetGeoMaterialDecodeTests.cpp` | 新建 |

---

### Task 1: `U1` 表面遮罩

**Files:** `GeoMaterialCodec.h`、`PlanetGeoGenerator.cpp`、`Tests/PlanetGeoMaterialDecodeTests.cpp`

**Interfaces:** Produces `U1 = round(255·clamp(1 − Depth/(8 + 2·(1<<LOD)), 0, 1))`；`V1` 恒 0。

- [ ] **Step 1: 失败测试** `StellarEra.Planet.Geo.Material.SurfaceMask`

```cpp
// 内置快照（与 PlanetGeoGeneratorTests 同法建一颗小星），沿一条半径采样 GetMaterialImpl：
// - 地表（D = Surface）U1 ≥ 250；深度 ≥ 8 + 2·(1<<LOD) 处 U1 == 0；中间段随深度单调不增。
// - DebugColor = Lithology 时同样成立（U1 不受调试着色影响）。
```

- [ ] **Step 2: RED。**
- [ ] **Step 3: 实现**
  - `GeoMaterialCodec.h` 加 `constexpr int32 SurfaceMaskChannel = 6;`（U1 在 8 通道里的序号）与一行注释。
  - `PlanetGeoGenerator.cpp` 的 `GetMaterialImpl`：
    ```cpp
    // 表面遮罩：地表用生物群系色，挖开处用岩性色。带宽随 LOD 放大，粗 LOD 采样点离表面更远。
    const double Band = 8.0 + 2.0 * double(1 << FMath::Clamp(LOD, 0, 20));
    const float Mask = float(FMath::Clamp(1.0 - FMath::Max(Surface - D, 0.0) / Band, 0.0, 1.0));
    P.U1 = uint8(FMath::RoundToInt(Mask * 255.f));
    ```
- [ ] **Step 4: 绿；提交** `feat(地质): 材质 U1 写入离地表遮罩，供材质区分地表与挖开处`

---

### Task 2: 调色板 LUT 纯函数

**Files:** Create `Geo/Snapshot/GeoPalette.{h,cpp}`；tests

**Interfaces:** Produces `GeoPalette::BuildLUT(const TArray<FGeoMineralInfo>& Minerals) -> TArray<FColor>`（256×3 行优先）。

- [ ] **Step 1: 失败测试** `StellarEra.Planet.Geo.Material.Palette`
  - 长度 = 768；行 0 的 `[0, GeoLithology::Count)` 与 `GeoLithology::DebugColor(i)` 逐项相等，其余为黑；
  - 行 1 的 `[1, Minerals.Num()+1)` 与 `Minerals[i]` 的 color 逐项相等（`FLinearColor` 转 `FColor` 用 `ToFColorSRGB()`，与 `PlanetRecipe::ToConfig` 同口径），0 与越界黑；
  - 行 2 的 `[0, GeoClue::Count)` 与 `GeoClue::DebugColor(i)` 逐项相等，其余黑。
- [ ] **Step 2: RED → 实现**

```cpp
// 行 0 岩性 / 行 1 矿物 / 行 2 线索；材质按行中心采样，改动布局要同步 MF_GeoDecode。
namespace GeoPalette
{
	TArray<FColor> BuildLUT(const TArray<FGeoMineralInfo>& Minerals);
}
```

- [ ] **Step 3: 绿；提交** `feat(地质): 岩性/矿物/线索 256×3 调色板 LUT 纯函数`

---

### Task 3: Actor 绑定 LUT 与默认材质回退

**Files:** `VoxelPlanetActor.{h,cpp}`；tests

**Interfaces:** Consumes `GeoPalette::BuildLUT`；Produces MID 参数 `GeoPalette`（Texture2D）。

- [ ] **Step 1: 测试** `StellarEra.Planet.Geo.Material.ActorLUT`（编辑器上下文）
  - 造一颗带快照的 `AVoxelPlanetActor`（照 `PlanetGeoTestPlanetTests` 的最小装配），调 `InstallBiomeMaterial(true)`；
  - `BiomeMaterial->GetTextureParameterValue(FMaterialParameterInfo(TEXT("GeoPalette")), Tex)` 返回 true 且 `Tex->GetSizeX()==256 && GetSizeY()==3`；
  - 纹理 `Outer` 不是 Actor（瞬态包）。
- [ ] **Step 2: RED → 实现**
  - `AVoxelPlanetActor` 加 `UPROPERTY(Transient) TObjectPtr<UTexture2D> GeoPaletteTexture;`
  - 新静态函数 `static UTexture2D* CreatePaletteTexture(const TArray<FColor>& Pixels)`：`UTexture2D::CreateTransient(256, 3, PF_B8G8R8A8, TEXT("GeoPalette"))`，`SRGB=true`，写 Mip0 BulkData、`UpdateResource`，`Outer = GetTransientPackage()`。
  - `InstallBiomeMaterial` 在设置 `BiomeCube` 之后：若 `GeoPaletteTexture` 为空或快照调色板变化（用一个 `uint64 GeoPaletteHash` 记录），重建纹理并 `SetTextureParameterValue(TEXT("GeoPalette"), GeoPaletteTexture)`。
  - 构造函数默认材质：先找 `/Game/Code/Planet/Materials/M_GeoPlanet.M_GeoPlanet`；`!Succeeded()` 时退回 `M_PlanetVertexColor`。
  - 重建时把 `GeoPaletteTexture` 置空（瞬态，交给 GC）。
- [ ] **Step 3: 绿；提交** `feat(地质): 星球材质参数绑定调色板 LUT，默认材质切 M_GeoPlanet（缺失回退）`

---

### Task 4: 材质构建器（MF_GeoDecode + M_GeoPlanet）

**Files:** Create `Geo/Voxel/GeoMaterialBuilder.{h,cpp}`；tests

**Interfaces:** Produces `GeoMaterialBuild::BuildAll(FString& OutError)`（`WITH_EDITOR`）与命令 `StellarEra.BuildGeoMaterials`。

- [ ] **Step 1: 冒烟测试** `StellarEra.Planet.Geo.Material.Assets`（编辑器上下文）
  - 先 `BuildAll`，再 `LoadObject<UMaterial>(/Game/.../M_GeoPlanet)` 与 `LoadObject<UMaterialFunction>(/Game/.../Functions/MF_GeoDecode)`；
  - 断言 `M_GeoPlanet` 的表达式集合里至少一个 `UMaterialExpressionMaterialFunctionCall` 的 `MaterialFunction == MF`；
  - 断言存在纹理参数 `GeoPalette`；标量参数 `BiomeCubeBlend`；纹理参数 `BiomeCube`；
  - 再跑一次 `BuildAll` 断言幂等（表达式数量不翻倍）。
- [ ] **Step 2: 实现**

构建器骨架（照 `PlanetMineralMaterialBuilder.cpp`：`FindOrCreateMaterial` + `NewObject<UMaterialExpression*>` + `GetExpressionCollection().AddExpression` + `SaveMaterial`）：

`MF_GeoDecode`（`UMaterialFunction`）：
1. `UMaterialExpressionFunctionInput` ×3：`VertexColor`(Float4)、`UV1`(Float2)、`GeoPalette`(Texture2D)；
2. 表达式链按 spec §5：`Multiply 255 → Add 0.5 → Floor` 解 Lith / Mineral / A；`Divide 16 → Floor` 解 Clue，`A255 − Clue·16 → /15` 解 Strength；
3. 三次 `TextureSample`（`MipValueMode = MipLevel 0`，`SamplerType = SAMPLERTYPE_Color`，UV 由 `Constant2Vector` 组合 `(id/255, rowCenter)`，`rowCenter` 取 `1/6, 3/6, 5/6`）；
4. `SmoothStep(0.15,0.75,Grade)` × `Step(0.002, G)` → `MineralCover`；
5. `Lerp(Lith, Min, MineralCover·0.85)` → `Lerp(., Clue, Strength·0.35)` → `RockColor`；
6. `UMaterialExpressionFunctionOutput` ×4：`RockColor`(Float3)、`MineralCover`、`SurfaceMask`（来自 `UV1` 的 ComponentMask X）、`Debug`(Float3 = LithColor)；
7. `PreEditChange/PostEditChange` + 保存到 `/Game/Code/Planet/Materials/Functions/MF_GeoDecode`。

`M_GeoPlanet`（`UMaterial`，`DefaultLit`）：
1. 纹理参数 `GeoPalette`、`BiomeCube`；标量参数 `BiomeCubeBlend`（默认 1）；
2. `VertexColor`、`TextureCoordinate(1)`（UV1）各一个节点，接 `UMaterialExpressionMaterialFunctionCall`（`SetMaterialFunction(MF)` 后按 `FunctionInputs[i].InputName` 连）；
3. 生物群系方向：`AbsoluteWorldPosition − ObjectPositionWS` → `Normalize` → `TextureSampleCube(BiomeCube)`；
4. `Lerp(RockColor, BiomeColor, SurfaceMask · BiomeCubeBlend)` → `BaseColor`；
5. `Roughness = Lerp(0.85, 0.45, MineralCover)`；
6. 保存到 `/Game/Code/Planet/Materials/M_GeoPlanet`。

命令：`FAutoConsoleCommandWithWorld` 调 `BuildAll` 并打印成败（`WITH_EDITOR`）。头文件导出 `namespace GeoMaterialBuild { bool BuildAll(FString& OutError); }`。

- [ ] **Step 3: 绿；提交** `feat(地质): 代码构建 MF_GeoDecode 与 M_GeoPlanet 材质`

---

### Task 5: 资产、目检与文档（controller）

- [ ] 编辑器内执行 `StellarEra.BuildGeoMaterials`（MCP `execute_console_command`），确认两个资产落盘（字节大小与 mtime）。
- [ ] PIE / 编辑器视口目检：表面保持生物群系；半剖 / 四分之一剖与挖洞处显示岩性色，矿体按矿物色点缀；`BiomeCubeBlend=0` 时整星岩性色；远处 LOD 无整片岩性色 / 闪烁。
- [ ] 截图存档；全量 `StellarEra`（基线 377 + 新增）。
- [ ] STATE 文档更新（材质解码完成、遗留项）。
