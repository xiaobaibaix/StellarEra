# 地质材质解码（MF_GeoDecode）设计

> 目标：让游戏画面里的岩性、矿种、蚀变线索真正可见。上位规格 `2026-11-12-geology-pipeline/11-material-field.md`；当前体素通道已经在写，缺的是「解」。

## 1. 目标与非目标

**目标**
- 新增材质函数 `MF_GeoDecode`：把顶点色 `RGBA` 与 `UV1` 解成「岩石色、矿物覆盖度、线索强度、表面遮罩」。
- 新增材质 `M_GeoPlanet`：未挖开的地表继续用 `BiomeCube`（生物群系），挖开 / 剖面 / 切面显示岩性与矿种。
- 调色板走运行时生成的 `256×3` LUT 纹理（岩性 / 矿物 / 线索），新增矿与岩性自动同步，不写死在材质里。
- 生成器在 `U1` 通道写「离地表深度」遮罩（现在恒 0）。
- 提供编辑器命令 `StellarEra.BuildGeoMaterials` 重建材质资产；星球默认材质切到 `M_GeoPlanet`，资产缺失时回退到 `M_PlanetVertexColor`。

**非目标**
- 不做 `Texture2DArray` 三平面岩性贴图（v1 只用纯色）。
- 不做次矿物 `U0/V0` 的混合（通道保留，材质 v1 不读）。
- 不做粗 LOD 的线索淡出（留给后续）。
- 不改烘焙、不改撒矿；只加「表面遮罩」一个生成器输出。

## 2. 现状与缺口

| 层 | 现状 |
|---|---|
| 编码 | `GeoMaterialCodec::Encode` 已写 `R=岩性 / G=矿物 / B=品位 / A=线索(高4位类型+低4位强度) / U0V0=次矿物` |
| 生成器 | `FPlanetGeoGeneratorInstance::GetMaterialImpl` 原样写这些通道；`U1/V1` 恒 0 |
| 材质 | 星球默认 `M_PlanetVertexColor`：表面按 `BiomeCube` + 顶点色混合，**不解码地质通道**；挖开看到的是索引当颜色 |

## 3. 通道语义扩展

`GeoMaterialCodec` 布局不变，补一条约定：

| 通道 | 语义 |
|---|---|
| `U1` | 表面遮罩：`255` = 贴近地表（用生物群系色），`0` = 深部（用岩性色），中间线性 |
| `V1` | 预留，恒 0 |

生成器规则：

```
Depth = SurfaceRadius(dir) − |P|            // 体素
Mask  = clamp(1 − Depth / SurfaceBandVoxels, 0, 1)
U1    = round(255 · Mask)
```

- `SurfaceBandVoxels` 固定 **8 体素**（生成器内常量 `MaterialSurfaceBandVoxels`）；v1 不做资产参数，避免动生成器参数哈希。
- 粗 LOD 下采样点可能落在表面下方一整格：带宽按 `LOD` 放大（`SurfaceBand + 2·(1<<LOD)`），与现有 `GeoNearBand` 同量级，避免远处整片变岩性色。
- 调试着色（`DebugColor != None`）时 `U1` 照写；调试只改 `RGB`，材质在调试模式下本来就不走解码分支。

## 4. 调色板 LUT

一张 `256×3` 的 `BGRA8` 纹理（`sRGB`），三行：

| 行 | 内容 | 来源 |
|---|---|---|
| 0 | 岩性色（x = 岩性编号） | `GeoLithology::DebugColor`，0–15 有效，16–255 黑 |
| 1 | 矿物色（x = 矿物编号） | 快照矿物调色板（编号 = 下标 + 1），0 与越界黑 |
| 2 | 线索色（x = 线索编号） | `GeoClue::DebugColor`，0–4 有效 |

- 纯函数 `GeoPalette::BuildLUT(const FPlanetSnapshot&)` 与 `BuildLUT(const TArray<FGeoMineralInfo>&)` 返回 `TArray<FColor>`（256×3 行优先），代码级可测。
- 运行时由 `AVoxelPlanetActor` 在 `InstallBiomeMaterial` 里创建瞬态 `UTexture2D`（`CreateTransient(256, 3, PF_B8G8R8A8)`，`SRGB=true`，直接写 Mip0 BulkData 后 `UpdateResource`），作为材质参数 `GeoPalette` 绑到 MID。
- 快照重建时重建纹理并重设参数；纹理与 MID 的 `Outer` 一律是瞬态包（防止切图泄漏，见 CLAUDE.md「已知坑」）。

## 5. 材质函数 `MF_GeoDecode`

输入：`VertexColor`（float4）、`UV1`（float2）、`GeoPalette`（Texture2D）。

输出结构：

| 输出 | 类型 | 含义 |
|---|---|---|
| `RockColor` | float3 | 岩性色叠矿物色再叠线索色 |
| `MineralCover` | float | 0–1，矿物覆盖度 |
| `SurfaceMask` | float | 0–1，来自 `UV1.x` |
| `Debug` | float3 | 岩性色（调试用） |

v1 只暴露 `Debug`（岩性色），暂不出 `Lithology` / `Mineral` / `Clue` / `ClueStrength` 四个整数输出；需要时后续再加。

解码公式：

```
Lith      = floor(VertexColor.r · 255 + 0.5)
Mineral   = floor(VertexColor.g · 255 + 0.5)
Grade     = VertexColor.b
A255      = floor(VertexColor.a · 255 + 0.5)
Clue      = floor(A255 / 16)
Strength  = (A255 − Clue·16) / 15

LithColor = GeoPalette(u = Lith/255,     v = 1/6)
MinColor  = GeoPalette(u = Mineral/255,  v = 3/6)
ClueColor = GeoPalette(u = Clue/255,     v = 5/6)

MineralCover = smoothstep(0.15, 0.75, Grade) · step(0.002, VertexColor.g)
RockColor    = lerp(LithColor, MinColor, MineralCover · 0.85)
RockColor    = lerp(RockColor, ClueColor, Strength · 0.35)
SurfaceMask  = UV1.x
```

- 取整必须用 `floor(x·255+0.5)`：顶点属性在三角形内插值，`5` 与 `25` 之间会出现中间值；取最近 + 硬切符合「侵入体与围岩是硬接触」。
- LUT 的 `v` 取行中心（1/6、3/6、5/6），避免跨行双线性串色。

## 6. 材质 `M_GeoPlanet`

`DefaultLit`，参数与 `M_PlanetVertexColor` 同名兼容（`BiomeCube`、`BiomeCubeBlend`），新增 `GeoPalette`。

```
Dir        = normalize(AbsoluteWorldPosition − ObjectPositionWS)
BiomeColor = TextureSampleCube(BiomeCube, Dir)
Decoded    = MF_GeoDecode(VertexColor, UV1, GeoPalette)
BaseColor  = lerp(Decoded.RockColor, BiomeColor, Decoded.SurfaceMask · BiomeCubeBlend)
Roughness  = lerp(0.85, 0.45, Decoded.MineralCover)
```

- `BiomeCubeBlend = 1`（默认）：地表保持生物群系外观；挖开 / 剖面 `SurfaceMask → 0`，显示岩性。
- `BiomeCubeBlend = 0`：整颗星按岩性上色（调试用）。
- 切面与剖面不依赖任何切除几何：只看 `U1`，因此「挖洞、切面、LOD」三条路径同一套逻辑。
- 粗 LOD：`U1` 带宽已随 LOD 放大；矿体在粗 LOD 不显示（`Grade` 太细，`MineralCover` 自然接近 0，不另做淡出）。

## 7. 资产构建器

编辑器命令 `StellarEra.BuildGeoMaterials`（`FAutoConsoleCommand`，`WITH_EDITOR`）：

1. 创建 / 覆盖 `/Game/Code/Planet/Materials/Functions/MF_GeoDecode`；
2. 创建 / 覆盖 `/Game/Code/Planet/Materials/M_GeoPlanet`；
3. 保存两个包。

实现照 `PlanetMineralMaterialBuilder.cpp` 的做法：`NewObject<UMaterialExpression*>` + `GetExpressionCollection().AddExpression`，不依赖 MaterialEditor 模块。构建器拆成纯代码入口 `GeoMaterialBuild::BuildAll()`，命令与自动化测试共用。

## 8. 接线

- `AVoxelPlanetActor` 构造函数的默认材质查找：先 `M_GeoPlanet`，找不到退回 `M_PlanetVertexColor`（两者都用 `ConstructorHelpers::FObjectFinder`，仅编辑器有效）。
- `InstallBiomeMaterial`：创建 / 更新 LUT 纹理，`SetTextureParameterValue(TEXT("GeoPalette"), LUT)`，并继续设 `BiomeCube` / `BiomeCubeBlend`。
- 生成器：`GetMaterialImpl` 写 `U1`（常量带宽，不进参数哈希）。

## 9. 测试与验收

**代码级**
1. `GeoMaterialCodec`：`Encode/Decode` 语义字段不变；补常量 `SurfaceMaskChannel` 与注释。
2. `StageMaterial.SurfaceMask`（生成器测试）：地表采样 `U1 ≈ 255`，深度 > 带宽的采样 `U1 = 0`，中间单调；`DebugColor != None` 时同样成立。
3. `GeoPalette`：行 0 与 `GeoLithology::DebugColor` 逐项相等；行 1 与传入的矿物调色板逐项相等且 0 为黑；行 2 与 `GeoClue::DebugColor` 逐项相等；越界索引为黑。
4. `GeoMaterialBuild.BuildAll()` 在自动化测试里跑一次，断言两个资产存在、`M_GeoPlanet` 的表达式集合里至少有一个 `UMaterialExpressionMaterialFunctionCall` 指向 `MF_GeoDecode`、且存在 `GeoPalette` 纹理参数。（重建幂等。）
5. 星球 Actor：`InstallBiomeMaterial` 后 MID 上有 `GeoPalette` 纹理参数（编辑器上下文测试）。

**目检（PIE / 编辑器视口）**
- 默认星球表面仍是生物群系颜色；
- 用切除（半剖 / 四分之一剖）或挖洞：剖面按岩性上色（花岗岩粉、辉长岩蓝灰、橄榄岩绿等），矿体按矿物色点缀；
- `BiomeCubeBlend = 0` 时整星显示岩性色；
- 远处 LOD 不出现「整片岩性色」或闪烁。

## 10. 风险

- **顶点属性插值**：`R` 在两大类岩石之间插出中间索引 → 解码取最近后可能落到第三种岩性。取整不能消除，只能把错误限制在「相邻索引」；v1 接受（`GeoLithology` 编号本身按深度 / 族排列）。
- **材质图不可单测**：只能靠「表达式集合有 MF 调用 + 参数存在」做冒烟；视觉验收是主判据。
- **瞬态纹理与 GC**：LUT 与 MID 必须挂在 Actor 的 `UPROPERTY` 上、`Outer` 用瞬态包，否则切图泄漏（CLAUDE.md 已知坑）。
- **粗 LOD 带宽**：`8 + 2·(1<<LOD)` 在 LOD 6 上为 136 体素，可能让远处「表层岩性色」变厚；按此值验收，必要时下调。
- **资产未提交**：新材质与 MF 属 `Content/`，与现有未提交资产一起由用户决定何时提交。
