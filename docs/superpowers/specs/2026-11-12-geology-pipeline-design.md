# 地质生成流水线重构：设计规格

> 日期：2026-11-12 ｜ 状态：**待审阅** ｜ 类型：架构级重构
> 细节附录：`2026-11-12-geology-pipeline/`（各阶段算法、预算、原语、规则、矿物文件）
> 上游调研：`docs/research/2026-11-11-geology-driven-terrain-analysis.md`
> 本规格与附录冲突时，以本规格为准。

## 1. 目标与非目标

**目标**
1. 矿物分布由地形成因决定（火山 → 硫磺、弧岩株 → 斑岩铜、河谷 → 砂金），不再随机撒点。
2. 地形按地质成因分阶段烘焙，全部阶段跑完才进入网格化。
3. 任何地方、任何深度都能挖，每个体素从地表到球心都有岩性。
4. 找矿线索全部表现在体素材质上，与地下矿体同源。
5. 烘焙 / 快照 / 求值三层分离，求值期是无虚函数、无分配的扁平核。

**非目标**
- 水体（海、湖、河、地下水）：单独设计。本方案只产出水留下的固体地貌。
- 流动的岩浆与外核：一律当作固态材质。
- 3A 沉积盖层、3B 构造变形与变质（第一版不做；矽卡岩、层控铅锌、煤、造山型金随之不做）。
- 非类地星球。
- 找矿仪器、产出换算、挖掘手感（另立子项目；本规格只保证模型能回答它们需要的查询）。
- 跨 CPU / 编译器的确定性。

## 2. 重构立场：平行新建，一次切换，整体删除

不在现有 `Terrain/` `Hydrology/` `Minerals/` 上叠加，也**不做**附录 `20-architecture.md` §9 的 `FLegacyStage`
（把旧逻辑原样搬进新外壳）：

1. 新系统在 `Source/StellarEraPlanet/Src/Geo/` 平行建起，挂在独立测试星球 `BP_GeoTestPlanet` 上验证，旧星球不受影响。
2. 新系统达到旧系统的地形能力后，在里程碑 M4 把 `BP_VoxelPlanet` 切到新生成器。
3. 同一里程碑内删除被取代的旧代码、旧资产、旧测试，不留开关、不留兼容层。

理由：旧系统的核心假设（矿是独立撒点的属性、地形是全域 fBm、矿物视图每元素一个体素世界）与新方案正面冲突，
迁移外壳会把这些假设一起带进新架构。旧测试里仍成立的契约（值域保守、最低地表半径、梯度法线、地表射线同源）
改写成针对新生成器的测试保留。

## 3. 采纳的决定（审阅时可推翻）

附录 `open-questions.md` 的建议按下表采纳。推翻任何一条，相关章节跟着改。

| # | 事项 | 采纳 |
|---|---|---|
| Q1 | 3A / 3B | 第一版不做 |
| Q2 | 星球类型 | 只做类地 |
| Q3 | 资源单位 | 按元素（Fe、Cu、Au…），矿物只是载体 |
| Q4 | 矿改不改形状 | 不改；删除 `SampleGeometryDelta` |
| Q5 | 圈层比例 | 地壳 0–25% R，地幔底界 55% R，地核开放为终极资源 |
| Q5′ | 星球半径 | **以 UniverseMap 里 `BP_VoxelPlanet` 的实际值为准：`R = 40000` 体素**（2026-11-12 用户确认；来源：运行日志「星球半径 40000 体素，体素世界边长 131072，RenderOctreeDepth 12」）。头文件默认 100 不作数。`VoxelSize = 50 cm`（M0 读关卡实例确认；BP 类默认是 100，实例覆盖为 50），故物理半径 ≈ 20 km |
| Q6 | 矿物透视（X 光） | 玩家侧、编辑器侧都删除；调试改用 GeoTools 场叠加 |
| Q7 | 线索可靠度 | 只有金伯利岩管不可靠，其余「看见就有」 |
| Q8 | 烘焙时机 | 编辑器预烘成资产 + 加载时校验哈希；缺缓存才后台现算 |
| Q9 | 单体素产出量级 | 留给玩法子项目；`FGeoSample` 带元素与品位即可 |
| Q10 | 确定性 | 同一构建、任意线程数字节一致 |
| Q11 | 快照字段 | 只留求值字段；调试字段走编辑器专用视图 |
| Q12 | 粗 LOD 细节噪声 | `LOD ≥ 4` 跳过 |
| — | 材质索引插值 | M0 原型二选一（§6） |

## 4. 架构骨架

细节见附录 `20-architecture.md`。

```
UPlanetRecipe（DataAsset）──游戏线程拷一次──► FPlanetRecipeConfig（POD）
   │ 后台任务
   ▼
FPlanetBakePipeline：阶段 1 → 2 → 3 → 4 → 5 → 6 → 7，每阶段 Bake 后执行挂在本阶段的撒矿规则
   │ 阶段间用类型化结构体 FPlanetBakeFields 传数据；读写用位掩码声明，烘焙前检查「读了没人写」
   ▼ 冻结
FPlanetSnapshot（不可变、无 UObject、TSharedPtr<const> 共享）
   │ 四张球面场 + 径向剖面 + 基元 BVH + 值域包络
   ▼ 体素线程
FPlanetShapeEval / FPlanetMaterialEval（inline，无虚函数 / 分配 / 锁）
   ├─ UPlanetGeoGenerator（VoxelFree 适配器）
   ├─ FPlanetGeoQuery（对外契约：地表半径、海平面、单点取样）
   └─ GeoTools（编辑器调试叠加）
```

硬约束：
1. 后阶段只读前阶段（阶段 4 不读阶段 6，出露由阶段 6 结算）。
2. 撒矿规则只在烘焙期执行；求值期只查 BVH 与球面场。
3. 地表三个入口（体素求值、地表射线、最低地表半径）全部转发同一个 `FPlanetShapeEval`。
4. 材质编解码只有 `FGeoMaterialCodec` 一处；shader 侧常量由它生成。

## 5. 代码组织

仍在 `StellarEraPlanet` 模块内（拆模块的触发条件见附录 `20-architecture.md` §6.3）：

```
Src/Geo/
  Core/      GeoRng、GeoNoise、GeoField2D（6 面 + 接缝 + 3 级 mip）、GeoProfile、
             GeoPrimitive（扁平结构体 + switch）、GeoPrimitiveIndex（BVH）
  Bake/      PlanetRecipeConfig、PlanetBakeFields、PlanetBakePipeline、PlanetStage、Stages/、Rules/
  Snapshot/  PlanetSnapshot、PlanetShapeEval、PlanetMaterialEval、GeoMaterialCodec、PlanetGeoQuery
  Assets/    UPlanetRecipe、ULithologyCatalog、UMineralCatalog
  Voxel/     UPlanetGeoGenerator
  Debug/     GeoTools（WITH_EDITOR）
```

这是全项目第二处三级目录（`Geo/Bake/Stages`），`Source/README.md` 的判据要同步更新。

依赖规则（一条自动化测试扫 include 守住）：

| 目录 | 允许 | 禁止 |
|---|---|---|
| `Core` `Snapshot` | Core、Math | Engine、CoreUObject、Voxel |
| `Bake` | 上面 + `Core` `Snapshot` | World、Actor、Component、Voxel |
| `Assets` | CoreUObject | Voxel |
| `Voxel` `Debug` | 全部 | 自写地质数学（必须转发 `Snapshot`） |

测试放 `Src/Tests/`，前缀 `StellarEra.Planet.Geo.*`。

## 6. 材质承载：M0 原型二选一

附录原推荐的「相近岩性编相邻索引 + shader 取整」已否决：顶点值在三角形内连续插值，取整挡不住跨类；A 通道按位打包，插值后失义。

| 方案 | 做法 | 代价 |
|---|---|---|
| A. `MultiIndex` | 插件负责 4 索引混合 | `R G B` 变权重、`A` 变湿度；品位、线索强度、次矿物要另找承载（线索改读烘焙 cubemap，品位不上材质） |
| B. 三顶点原值 | 保留 RGB 布局，为每个三角形写入三个顶点的原始索引，像素按重心坐标混合解码结果 | 需在插件网格生成后插入顶点属性处理，先确认有可用回调 |

**判据**（全满足才选定）：跨大类岩性边界只出现两端之一；线索类型在边界不出现第三类；1 km 视距无锯齿闪烁；网格化额外耗时 ≤ 10%。
选定后写回本节，附录 `11-material-field.md` 同步。

## 7. 里程碑

每个里程碑结束时全部自动化测试绿、构建通过，才进下一个。M0–M3 期间旧星球完全不动。

| 里程碑 | 内容 | 验收 |
|---|---|---|
| **M0 原型与测量** | 读资产确认 `Radius=40000` 与 `VoxelSize`，按 §12 重算附录预算；`Geo/Core` 的 RNG、噪声、`GeoField2D`、`GeoPrimitive` + BVH；`FGeoMaterialCodec` 两个候选；材质方案 A/B 对比原型 | §6 判据定出方案；BVH 1000 点与暴力遍历一致；`R` 实测值写回 §3 |
| **M1 骨架 + 阶段 1** | `FPlanetBakePipeline`、读写位掩码检查、`FPlanetSnapshot`、`FPlanetShapeEval`、`UPlanetGeoGenerator`、`BP_GeoTestPlanet`；阶段 1 圈层与背景岩性 | 测试星球能在 PIE 里生成、挖到球心看到逐层岩性；同 seed 1 线程 vs 8 线程快照哈希一致；依赖扫描测试绿 |
| **M2 竖切 S1：火山与硫磺** | 规则引擎、矿物目录、岩性目录；阶段 4 火山点表**手填**（不等板块）；硫磺、浅成金银、斑岩铜三条规则；蚀变线索；`MF_GeoDecode` | 硫磺只在活火山火口 4 格内；从火山顶竖直采样顺序为硫磺 → 金银 → 斑岩铜；PIE 里肉眼可见黄色结壳并能挖到 |
| **M3 地形骨架：阶段 3 + 4** | 阶段 2（板块 / 陆块 / 宏观高程）已提前完成并作为用户验收点，M3 只剩阶段 3（宏观地形）+ 阶段 4 由汇聚边界自动生成弧火山；板块、陆块、`ElevationBase`、山带遮蔽、海沟、热力侵蚀 | 附录 `02` `03` 的验收（穿越次数、陆地占比、三峰直方图、无凹坑无假脊）；带宽 ≤ 15% R；单点 `GetValue` ≤ 1.5 µs |
| **M4 切换与删除** | `BP_VoxelPlanet` 换 `UPlanetGeoGenerator`；对外契约接入（§9）；删除旧系统（§8）；旧存档提示丢弃 | 旧代码、旧测试、旧资产全部删除后构建与测试绿；UniverseMap 里角色行走、飞船着陆、大气地平线正常 |
| **M5 阶段 5 + 6** | 气候、流水侵蚀、出露结算；砂金、蒸发岩、锂盐、滨海重砂 | 附录 `05` `06` 验收；砂金「无源为零」测试 |
| **M6 阶段 7** | 风化壳；铝土、红土镍、次生铜、铁帽 | 附录 `07` 验收；铁帽只在硫化物出露处 |

M2 放在 M3 之前：火山点表手填即可，先拿到「看到线索 → 挖到矿」的完整闭环，再投入回归面最大的地形骨架。

## 8. M4 删除清单

| 删除 | 内容 |
|---|---|
| `Src/Minerals/` 整个目录 | Geology（场、预设、4 种分布）、Surface、View（矿物透视、每元素体素世界） |
| `Src/Hydrology/` 整个目录 | 汇流算法在 `Geo/Bake/Stages/Stage06Erosion` 重写，不复用文件 |
| `Src/Terrain/` 中 | `VoxelPlanetNoiseTerrain`、`VoxelPlanetSphereTerrain`、`VoxelPlanetTerrainModule`、`VoxelPlanetGenerator` |
| 测试 | `PlanetGeology*`、`PlanetMineral*`、`PlanetHydrology*`、`PlanetTerrain*`（契约改写后进 `StellarEra.Planet.Geo.*`） |
| 资产 | `Content/Code/Planet/Geology/MineralData/DA_Geology_*`、矿物视图材质、`DA_Terrain_*` 地形预设（由 `UPlanetRecipe` 资产取代） |
| 跨模块调用点 | `StellarEraPlayerController`、`StarMapCameraPawn`、`StarMapCameraInputComponent`、`StarMapUISystem` 里的矿物透视开关与图例 |

`PlanetBiomeCubemap` 保留到 M5，届时改读阶段 5 的气候场。`[CoreRedirects]` 不为被删的类加重定向（类已不存在）。

## 9. 对外契约

| 使用方 | 现在读 | M4 之后读 |
|---|---|---|
| 大气 `PlanetAtmosphereComponent` | `GetMinSurfaceRadiusCm` | 同名函数，转发 `FPlanetGeoQuery::GetMinSurfaceRadius`（快照包络的下界） |
| 地表射线、着陆、角色 | `GetSurfaceRadiusCm(Direction)` | 同名函数，转发 `FPlanetShapeEval` |
| 水面 `PlanetWaterComponent` | `FPlanetHydrologyField` | **M4 起关闭**（`bEnableWater=false`），等水系统单独设计；新设计读 `FPlanetGeoQuery` 的海平面 `R₀`、河谷与内流盆地 |
| 生物群系材质 | `PlanetBiomeCubemap` | M5 前不变 |
| 体素存档 | 无版本戳 | 存档写入快照哈希；不匹配时提示并丢弃 |

**`R₀` 与 `R_max` 分开**：海平面 `R₀` 不再等于「地表」，凡是把基准半径当地表用的地方（大气地面半径、相机 Far、LOD 边界）改用 `R_max`。

## 10. 测试策略

- **纯计算单测**（不需要 World）：RNG 派生、噪声确定性、`GeoField2D` 接缝连续、BVH 与暴力一致、原语值域 ⊇ 采样、编解码往返、每阶段的附录验收。
- **架构测试**：依赖扫描、快照无 UObject、三入口同源（10⁴ 方向两两差 < 0.01 体素）、确定性（两次烘焙 / 1 vs 8 线程哈希一致）。
- **黄金星球**：`R=500, Seed=1337` 的 10⁵ 点 `GetValue` 哈希写进测试常量；地形有意改动时显式更新基线。
- **性能护栏**：micro benchmark 测单点 `GetValue` ≤ 1.5 µs、`GetMaterial` ≤ 2 µs；烘焙全流程 ≤ 10 s；快照 ≤ 60 MB。
  附录里「节点图 3–8 µs」「现状 0.4–0.6 µs」「烘焙 3–15 s」都是估计，M0 / M1 实测后替换。
- **PIE 验证**：只在 `UniverseMap`（切换后）与 `BP_GeoTestPlanet` 所在关卡做；编辑器在前台测性能。

## 11. 风险

| 风险 | 后果 | 应对 |
|---|---|---|
| **附录预算按 R=100–1000 设计，实际 R=40000** | `clamp` 上限全部封顶：火山锥 48 格、造山带 32 格，在 40 km 星球上显得过小；`faceRes` 封顶 1024 时一个 cell ≈ πR/2N ≈ 61 格，海岸线、陆坡、河谷全部分辨不出；按 1.5 格分辨率直接烘焙需每面 ≈ 4.2 万²，不可行 | 见 §12，M0 重算预算并改为「粗烘焙 + 求值期细节」两级方案 |
| 材质方案 A、B 都不过判据 | 线索无法可靠表现 | 退路：线索全部改由烘焙 cubemap 在 shader 采样，体素材质只存岩性 |
| 水面在 M4 到水系统落地之间关闭 | UniverseMap 暂时没有海 | 已在 §9 明确；若不可接受，M4 推迟到水系统设计完成 |
| 删旧系统后体素存档失效 | 现有挖掘进度丢失 | 存档写快照哈希、不匹配时提示；仓库里 `Content/Saves/PlanetVoxel/**` 在 M4 一并清空 |
| 阶段 3 回归面大 | 角色、着陆、大气同时出问题 | 三入口同源测试 + M4 的 UniverseMap 手动验收清单 |

## 12. 实际半径 R = 40000 带来的修订（M0 必须先完成）

附录的分辨率预算（`00-pipeline.md` §3）、带宽表（§4.1）、快照内存（`20-architecture.md` §3.3）都按 `R = 100–1000` 推导，
在 `R = 40000` 下结论翻转：

| 项 | 附录结论 | R = 40000 时 |
|---|---|---|
| 带宽准入线 15% R | R=100 不合格 | 累计带宽 ~90 格 ≈ 0.2% R，**远低于准入线，不再是约束** |
| `clamp(k·R, floor, cap)` | 小星球被 floor 托住 | 全部被 **cap** 封住，尺度被压成地球的「小一号」，需按大星球重定 cap 或改为纯比例 |
| `faceRes ≤ 1024` | cell 1.5 格 | cell ≈ 61 格（≈ 61 m）；按 1.5 格直接烘焙每面约 4.2 万²，6 面 ≈ 10⁹ texel，不可行 |
| 3A 门槛「地壳 ≥ 16 格」 | R≥80 | 地壳 25% R = 1 万格，不是约束 |
| 一次挖掘 ≈ 2×10⁹ 次 `GetValue` | 现状注释 | 不变，单点预算仍是 1.5 µs |

由此定下的架构修订（细节在 M0 写回附录）：

1. **两级地形**：球面场只承载宏观量（板块、陆块、山带遮蔽、气候），按 `faceRes = 1024–2048` 烘焙；
   百米级以下的地貌（山脊、河谷、海沟断面、火山锥）在求值期由解析剖面 + 噪声按需生成，不进烘焙图。
2. **尺度改用物理尺寸表达**：各特征写成「米」而不是「占 R 百分比」或固定格数，再换算成体素；cap 取消或按 R=40000 重定。
3. **局部特征走基元**：火山、侵入体、矿体、河谷段这类空间有界的东西全部是基元 + BVH，与球面场分辨率无关。
4. **河谷与砂矿的流路**不能靠全球 D8 栅格（分辨率不够），改为在粗网格上求干流拓扑，再在求值期按中心线生成谷形；M5 前在附录 `06-erosion.md` 重写。

M0 的第一项因此改为：读 `BP_VoxelPlanet` 资产确认 `Radius` 与 `VoxelSize`，按上表重算附录 §3、§4 并更新 `20-architecture.md` §3.3 的内存估算。
