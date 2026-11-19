# 地质管线状态（2026-11-17，compact 交接）

> 用途：跨会话接手。实现细节看各阶段规格/计划；这里是「现在到哪了、下一步做什么」。

## 仓库状态

- 分支 `geo-pipeline`，HEAD `c773b0a`（旧地形栈、旧矿物栈都已删，行星统一走 geo 管线）。
- 全量自动化测试 **299 条全过**（`Automation RunTests StellarEra`，约 6 分钟）；删旧矿物栈带走了 73 条。
- 未提交（工作区）：`Content/Code/Maps/GeoPlanetMap.umap`、`GeoTestMap.umap`、
  `DA_GeoRecipe_Archipelago.uasset`、`DA_MineralCatalog.uasset`、`Content/Code/Maps/Saves/PlanetVoxel/GeoPlanetMap/`。
  **这两个资产是 4c 与 6c 的挂载（现为 24 种矿、24 个撒矿器），回退会丢**；配方资产里混着用户早前的未提交改动，所以一直没代提交。

## 已完成的阶段

| 阶段 | 类 | 内容 |
|---|---|---|
| 1 | `FStage01Sphere` | 球体与圈层 |
| 2 | `FStage02Plates` | 板块（加权 Voronoi + 欧拉极运动）、大陆场 |
| 3 | `FStage03Terrain` | 山带 / 海沟 / 平原 / 洋中脊 / 裂谷 / 古老造山带 / 热力侵蚀 |
| 4a | `FStage04Magmatism` | 五类火山（层状 / 盾状 / 溢流 / 破火山口 / 渣锥）+ 四图层 |
| 4b | 同上（`Stage04bIntrusions`） | 七类岩体（岩基 / 岩株 / 岩墙 / 岩床 / 层状 / 金伯利 / 碳酸岩）+ 四圈蚀变 + 三图层 |
| 4c-1 | 撒矿器 | 硫磺结壳、浅成金银脉、斑岩铜 |
| 4c-2 | 撒矿器 | 锡钨、伟晶岩、铜镍、铬铂、金刚石、稀土 |
| 5 | `FStage05Climate` | 温度 / 降水（四段纬度 + 离海衰减 + 雨影回溯）/ 风带 / 8 类生物群系 + 四图层 |
| 6a | `FStage06Erosion` | priority-flood 填洼、D8 流向、内流盆地、汇流累积、U 形河谷刻进高程、剥蚀深度 + 四图层 |
| 6b | `FStage06bExposure` | 按剥蚀深度把岩体 / 矿体 / 蚀变晕径向上抬（跟随者复制宿主位移向量）、夹到地表、写 `Exposure` / `RawDepthBefore` + 两图层 |
| 6c-1 | `FGeoPlacerRule` + `UGeoPlacerScatterer` | 阶段 6a 导出 `FlowNext`；砂矿按「上游原生矿 + 沿流追踪」撒 1 体素薄层，品位沿流程指数衰减；`bMouthOnly` 撒滨海重砂。目录 18 种矿、配方 18 个撒矿器 |
| 6c-2 | `FGeoBasinDepositRule` + `FGeoCoalRule` | 阶段 6a 导出 `BasinId`；内流盆地按归一化半径环带撒蒸发岩四环（各自一个矿物），可选火山门槛撒锂盐；煤在湿热、低平、近海的陆地撒略埋薄层。目录 24 种矿、配方 24 个撒矿器 |
| 迁移 | `UniverseMap` + 旧地形删除 | `UniverseMap` 行星改挂 `DA_GeoRecipe_Earthlike`；删除旧噪声/球体地形与旧生成器 12 个文件、`InstallBiomeMaterial` 全路径 |
| 材质 | `MF_GeoDecode` + `M_GeoPlanet` | 生成器写 `U1` 表面遮罩；运行时 256×3 调色板 LUT（岩性/矿物/线索）；材质解码岩性色、矿物覆盖、线索染色；`StellarEra.BuildGeoMaterials` 代码重建材质资产 |

阶段号：1 球、2 板块、3 地形、4 岩浆（含 4a/4b/4c）、5 气候、6 侵蚀（6a/6b/6c-1/6c-2）、7 出露（6b 占 7，风化壳将来用 8）。

## 本轮新增：阶段开关（调试）

分类名全中文（`阶段1|圈层` … `阶段6b|出露`），开关分两层：

- **总开关** `bRunStage1..7` 直接放在阶段标题正下方（`Category = "阶段N"`），关掉即整段跳过，
  该段全部参数带 `EditCondition` 自动置灰。
- **子分组开关** 放在对应折叠的第一项：`bRunStage1Moho`（莫霍面）、`bRunStage4Volcano`（火山）、
  `bRunStage4Intrusion`（侵入）；该段参数是链式条件 `bRunStage4 && bRunStage4Volcano`。
  只给这三段加，因为其余子分组是后面阶段的地基（`板块`/`大陆`）或本来就是单分组。
- `bScatterStage2/3/4/7` 仍在撒矿所在段的收尾，只管撒矿器。

子开关落到 POD：`Sphere.bMoho`（关掉=莫霍面填 0）、`Volcano.bVolcanoes`（只跳过放置，高程照写）、
`Intrusion.bIntrusions`（只跳过岩体，蚀变阈值照写），三者都进 `Hash()`。

行星上新增 `Geo Recipe Override`（Instanced），可在行星细节里直接改全部参数与开关，不经过资产。关掉前面的阶段后，后面阶段读不到它的场会在校验里报错——这是有意的。

## 已完成：阶段 7 / 3A / 3B（2026-11-19）

| 阶段 | 类 | 内容 |
|---|---|---|
| 7 | `FStage07Weathering`（阶段号 8） | 风化壳厚度 = 温湿 × (1 − 坡度)，土壤分红土 / 高岭 / 温带 / 干旱；`WeatheringDepth` `SoilType` |
| 3A | `FStage03aSediments`（9，顺序 3.5） | 低地盖砂岩 / 页岩 / 石灰岩，只写厚度与类型；材质求值在地表这层改岩性 |
| 3B | `FStage03bTectonics`（10，顺序 3.6） | 断层带（剪切 / 挤压边界）、褶皱噪声、变质级；石灰岩过门槛变大理岩 |

插队靠新加的 `IPlanetStage::GetOrder()`（默认等于阶段号）：3A/3B 要跑在 3 与 4 之间，
但阶段号必须唯一（随机流与禁用位都用它）。

成矿规律只新增三个类：`FGeoBlanketRule`（毯层，按盖层岩性 / 土壤 / 变质级 / 断层 / 大理岩挂）、
`FGeoSupergeneRule`（次生富集，宿主矿体上方）、`FGeoGossanRule`（铁帽，宿主出露才有）。
矿物目录 24 → 34 种，配方各加对应撒矿器（8 毯层 + 1 次生 + 1 铁帽）。

矽卡岩已补：`FGeoSkarnRule` 挂在阶段 4 之后，酸性/中酸性侵入体与石灰岩接触带才出。

仍未做：
- **土壤颜色**：沉积岩与大理岩走既有岩性调色板，颜色已经生效；红土 / 干旱壳只做了
  `SoilType` 场与调试图层，材质没读——要加第 4 行调色板与 V1 通道，改 `MF_GeoDecode`。
- 元素映射 / 资源层。

迁移后的已知缺口：
- 水面组件（`UPlanetWaterComponent`）已按用户要求从行星 Actor 摘除（`e11999a`）：海面不再显示，`Water/` 组件类与其测试保留、未实例化。`Terrain` 字段与 `UVoxelPlanetTerrainModule::PostEditChangeProperty` 同时删除。
- 旧矿物栈与星图矿物透视已整体删除（`6e19623`）；`DA_Geology_*` 与 `DA_Terrain_*`、
  `M_MineralBlock` 共 8 个无引用资产已在 `bc6e60e` 删除。

材质解码 v1 的已知限制：
- 地表「生物群系」混合尚未生效：实机里行星表面直接显示岩性色（`U1` 遮罩在材质侧读出 0，疑为体素网格未把 UV1 传给材质）。挖开 / 剖面显示岩性色已成立。
- 打包版（`bKeepDebugFields=false`）没有 Biome 底图，按设计回退旧材质，不解码。

6c 的两个规则类可作模板：一个规则类 + 一个撒矿器类，矿种与参数全在资产里。

## 一直挂着的两件事

1. **矽卡岩**没做：缺沉积岩（石灰岩）岩性，上位规格写明「需 3A 否则跳过」。
2. **材质解码的表面遮罩**：`U1` 已写入但材质读不到，地表用不上生物群系色；下一步查体素网格 UV1 传递或改用其他通道。

## 常用命令

```bash
# 构建（编辑器必须先关；不要用 Build.bat）
cd F:/workspace/ue && "/d/Application/Epic Games/UE_5.8/Engine/Binaries/ThirdParty/DotNet/10.0/win-x64/dotnet.exe" \
  "D:/Application/Epic Games/UE_5.8/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.dll" \
  StellarEraEditor Win64 Development -Project="F:/workspace/ue/StellarEra/StellarEra.uproject" -WaitMutex -FromMsBuild

# 全量测试（编辑器可开）
"D:/Application/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "F:/workspace/ue/StellarEra/StellarEra.uproject" \
  -ExecCmds="Automation RunTests StellarEra" -TestExit="Automation Test Queue Empty" \
  -unattended -nopause -nullrhi -nosplash -stdout -abslog="<日志>"
```

提交一律 `git commit -- <paths>`（索引里有常驻暂存文件）。

## 编辑器侧

- 图层下拉已中文化：「中文名（Id）」，读取时解析回 Id，老数据兼容（`AVoxelPlanetActor::ParseGeoDebugLayerLabel`）。
- 查看器：工具 → 地质查看器；图层下拉自带「阶段 N · 中文名」。
- 导出图层：控制台 `StellarEra.Geo.ExportLayers 1024` → `Saved/GeoDebug/<星球>_Editor/`。
- 体积图层（`IntrusionType` / `Exposure` 等）在地表图上看不到，要看剖面 / 3D 视图 / 切面。
- 已知：重开编辑器偶尔在 Slate 层崩一次（崩溃栈无本模块帧），重开即好，未复现。
- **改配方后重烘要用「重建行星（ReloadGeology）」**，不是 `RebuildPlanet`：后者只在没有快照时才烘，旧快照不会因配方改动失效。

## 本轮关键裁定（避免重复讨论）

1. 尺寸一律占半径比例；注释只写「为什么」。
2. 河谷刻进 2D 高程场，不做体素差集。
3. 剥蚀深度只是一张场（阶段 3 的高程已是侵蚀后地表），只用于 6b 抬升。
4. 6b 的跟随者复制宿主整条位移向量（不是只抄长度）；蚀变晕也算跟随者。
5. `Stage02Acceptance` 总预算 30 s（墙钟受外部负载影响），另设阶段 6 ≤1.5 s、6b ≤0.5 s 单独上限。
6. 6a 湖盆按「切断前」累积量判定，属已知简化。
7. 配方的气候 / 水文 / 出露参数都在资产里；资产未提交见上文。
8. 沉积矿不写新阶段：6c 一律是撒矿器（规则挂阶段号 7，即 6b 之后运行），矿种必须由目录驱动。
9. 6a 多导出的 `FlowNext`（下游格 + 1）与后续 6c-2 的 `BasinId` 都属 `Hydro` 组：只活在烘焙期，不进快照 / 哈希 / 调试导出。
10. 砂矿源矿默认不要求出露（`MinSourceExposure = 0`），要收紧在资产里调；「无源不砂矿」是硬规则。
11. 砂矿体的 `Exposure` 恒 0（规则跑在 6b 之后，本来就没经过出露结算）：出露图层里它们显示「未出露」，看砂矿请用「矿物 / 地表矿化」图层。
12. 全量套件由 controller 统一跑；实施子代理只跑聚焦过滤，避免每次 10 分钟。
13. 6c-2 的 `BasinId`：`>= 0` 表示「在某洼地」，湖盆（Mask 1）与内流盆地（Mask 2）各有编号；规则只聚合 Mask==2。
14. 蒸发岩用四个独立矿物、四个撒矿器实例共用同一规则类；环带边界首尾相接不留重叠（材质取强度最大的矿，重叠归属不确定）。
15. 煤的默认门槛：`MaxContinental = 0.65`（0.5 恰好是大陆度场的海岸线，写 0.5 会把煤闸死）、`MaxSlope = 0.04`；默认星球约 18 片煤层、744 片蒸发岩。
16. 沉积规则的 `BodiesPerBasin` 是每盆地上限而非全球配额；极端调大需自行控制总量。
17. 材质调色板是运行时 `256×3` LUT（行 0 岩性 / 行 1 矿物 / 行 2 线索），由目录自动生成，不写死在材质里。
18. 材质资产由代码构建（`StellarEra.BuildGeoMaterials`），GUID 固定、重复构建字节稳定；改材质图必须开真 RHI 编辑器验证编译（`-nullrhi` 测试抓不到）。
19. UE 5.8 的 `VertexColor` 表达式是两路输出（RGB float3 + Alpha float），接 Vector4 前必须 `AppendVector` 拼回；这是本轮的编译错误根因。
20. 默认 `GeoDebugColor` 改为 `None`（地质编码）、默认剖切图层置空；调试色与调试图层仍可在下拉里选。
