# 阶段 2 重做：整体大陆场（取代陆核模型）

- **日期**：2026-11-14
- **状态**：已实施（2026-11-14，commit 5919280…103fc92），待用户在 GeoPlanetMap 上验收形状
- **取代**：`2026-11-12-geology-pipeline/02-plates.md` 里「陆核 + 高斯团 + α 二分」的大陆部分；板块（Voronoi）部分保留
- **范围**：只做大陆形状与高程。**不做矿、不做线索、不改岩性**；地下仍只有阶段 1 圈层

## 1. 为什么重做

在 `GeoPlanetMap`（用户的 `BP_VoxelPlanet`，R=40000）上实拍，陆块是平顶圆盘、陡崖、盘心有圆洞：

| 现象 | 根因 |
|---|---|
| 盘心圆洞 | `exp(-D²/2σ²)` 以 `D = |dir−c| − r` 为自变量，峰值在核边缘，核心处 M≈0.19<0.5 → 环形陆地 |
| 海岸是正圆 | 大陆遮罩不经 warp、不叠噪声，高斯团叠加只有圆弧 |
| 平顶陡崖 | 落差 720 体素压在 240 体素宽的陆坡里，坡度约 70° |

更根本的问题是**大陆被拆成若干独立区域分别构造**，形状由「核」决定。改为全球一张连续场整体描述，大陆与海洋的个数由场自然长出。

## 2. 大陆场

方向 `d` 上的标量场：

```
F(d) = Frame(d) + CoastRoughness · Detail(Warp(d))
```

- **Frame**：实球谐 `Σ_{l=1..FrameDegree} Σ_m a_lm · Y_lm(d)`，`a_lm ~ N(0, σ_l²)`，系数由种子确定。`σ_l` 随阶数衰减，低阶决定大陆布局。`l=0` 不参与（只是平移，阈值会吸收）。球谐天然全球连续，无立方体接缝。
- **Detail**：域扭曲后的 3D fBm，只负责海岸线破碎、半岛、海湾、近海岛屿，不改变总体布局。
- **陆地判定**：对 FaceRes 全张场**按 texel 立体角加权**求分位数 `τ`，使加权陆地占比 = `LandFrac`。不加权会因立方体贴图面积不均系统性偏离。

## 3. 高程

1. 每个 texel 求其场值的**全球加权排名** `q ∈ [0,1]`（低 = 深海）。
2. 用分段平滑的**面积–高度曲线** `H(q)` 映射到高程（体素，相对 R0），从低到高：深洋盆 → 陆坡 → 陆架浅海 → 海岸（`q = 1−LandFrac` 处 H=0）→ 平原 → 高地。
3. 曲线各段之间用 smoothstep 衔接，配合低频场，相邻 texel 坡度自然有界，不出陡崖。

`MinElevation` / `MaxElevation` 仍在全部写完后串行扫描得到，供快照的保守值域用。

## 4. 可调参数（`UPlanetRecipe`，`Category = "Stage2|Continents"`）

| 参数 | 作用 | 默认 |
|---|---|---|
| `FrameDegree` | 大陆数量与尺度 | 4（类地球）；1–2 超大陆；≥6 配高 `CoastRoughness` 出群岛 |
| `LandFrac` | 陆地面积占比 | 0.30 |
| `CoastRoughness` | 细节层相对框架层的幅度 | 0.35 |
| `ShelfFrac` | 陆架占海洋面积比例 | 0.15 |
| `LandHeightVoxels` | 高地顶部高程 | 240 |
| `OceanDepthVoxels` | 深洋盆底部深度 | 480 |

全部进 `FPlanetRecipeConfig::Hash()`。默认值上的具体数（σ_l 衰减指数、fBm 八度、warp 幅度）由实施阶段实测定，写进配置并进哈希。

## 5. 删 / 留 / 改

- **删**：`FGeoContinentCore`、`Frame.Cores`、`ContinentalMaskAt`、`LandFracAt`、α 二分、`SigmaRatio` `CoreRadiusRatio` `CoreMinSpacingRatio` `AlphaMin/Max` `Continental/Mixed/MicroCore*`、`StyleHeight` 与 `StyleAmp/StyleNoiseWavelengths`、`SlopeWidthVoxels`。
- **留**：Voronoi 板块与 `PlateId` `BoundaryType` `BoundaryDistance` `CrustAge`，阶段 3 造山用；与大陆形状暂不关联。
- **改**：`ContinentalMask = smoothstep` 后的 `F−τ`；`ContinentalGrad` 从新场求；`CrustType` 由高程派生（>0 陆壳 / 陆架 / 洋壳）；`CrustSplitElevation` 取陆架底部高程，与 `CrustType` 分界同源。
- 旧资产 `DA_GeoRecipe_Test` 上已删字段丢弃即可（阶段 2 未发布，不做迁移）。

## 6. 调试着色

`UPlanetGeoGenerator` 增加按高程分色模式，与现有 `bDebugLithologyColor` 并列（枚举二选一）：深海深蓝 / 浅海浅蓝 / 海岸沙 / 平原绿 / 高地棕。只在地表附近体素有意义，地下仍按岩性。

## 7. 验收关卡

`GeoPlanetMap` 改为**三颗** `BP_VoxelPlanet` 并排（N 体注册关、星图相机 ChildActor 清空，同现状），各挂一份配方：

| 资产 | 格局 | 关键参数 |
|---|---|---|
| `DA_GeoRecipe_Earthlike` | 类地球 | FrameDegree 4 |
| `DA_GeoRecipe_Supercontinent` | 超大陆 | FrameDegree 1–2 |
| `DA_GeoRecipe_Archipelago` | 群岛 | FrameDegree ≥6、CoastRoughness 高 |

三颗星之间距离足够大，互不进入对方 LOD invoker 范围；体素存档在 geo 模式下已被抑制。

## 8. 测试（阈值由实测定，不手写数字）

- 加权陆地占比与 `LandFrac` 差 ≤ 0.01。
- 立方体棱两侧高程连续（沿棱采样，跨棱差与面内相邻差同量级）。
- 相邻 texel 最大坡度有上界（无陡崖）。
- 格局可调：`FrameDegree=1` 时最大连通陆块占总陆地 ≥80%；`FrameDegree=6` 时连通陆块数明显多于 `FrameDegree=4`。
- 环形陆地回归：每个连通陆块的质心方向上不得是海（旧模型的盘心洞正好落在这里）；若个别细长陆块误报，降级为人工验收项。
- 同种子烘焙两次哈希一致；单线程与并行逐位一致。
- 删字段后 `Hash()` 覆盖测试同步更新。

## 9. 不做

矿物、线索、岩性细分、山脉（阶段 3）、水体。顺序固定：**形状经用户验收 → 撒矿经用户验收 → 进阶段 3**。
