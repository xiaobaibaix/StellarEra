# 地质管线状态（2026-11-17，compact 交接）

> 用途：跨会话接手。实现细节看各阶段规格/计划；这里是「现在到哪了、下一步做什么」。

## 仓库状态

- 分支 `geo-pipeline`，HEAD `d057fcc`（阶段 6c-1 砂矿与滨海重砂）。
- 全量自动化测试 **356 条全过**（`Automation RunTests StellarEra`，约 10 分钟）。
- 未提交（工作区）：`Content/Code/Maps/GeoPlanetMap.umap`、`GeoTestMap.umap`、
  `DA_GeoRecipe_Archipelago.uasset`、`DA_MineralCatalog.uasset`、`Content/Code/Maps/Saves/PlanetVoxel/GeoPlanetMap/`。
  **这两个资产是 4c 与 6c-1 的挂载（现为 18 种矿、18 个撒矿器），回退会丢**；配方资产里混着用户早前的未提交改动，所以一直没代提交。

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

阶段号：1 球、2 板块、3 地形、4 岩浆（含 4a/4b/4c）、5 气候、6 侵蚀、7 出露（6b 占 7，风化壳将来用 8）。

## 下一步：6c-2 蒸发岩 / 锂盐 / 煤

上位规格 `docs/superpowers/specs/2026-11-12-geology-pipeline/06-erosion.md` §撒矿（砂矿与滨海重砂已在 6c-1 完成）：

| 矿 | 条件 | 形态 |
|---|---|---|
| 蒸发岩 | `BasinMask == 内流` 且 `Precipitation < 0.2` | 同心环：碳酸盐→石膏→岩盐→钾盐 |
| 锂盐 | 同上 + 附近有火山 | 盆心固态层 |
| 煤 | `Precipitation > 0.6` + 低平三角洲 | 水平薄层 |

数据都已就位：`BasinMask`、`BasinId`（6c-2 需要从 6a 再导出一张，与 `FlowNext` 同法）、`Precipitation`、`Volcanoes`、`Primitives`。
6c-1 的 `FGeoPlacerRule` 可作模板：一个规则类 + 一个撒矿器类，矿种与参数全在资产里。

之后：阶段 7 风化壳（铝土 / 红土镍 / 次生铜 / 铁帽），需要 `ErosionDepth` + 气候。

## 一直挂着的两件事

1. **材质解码 `MF_GeoDecode` 没做**：游戏画面里切面不按岩性 / 矿种上色，只有调试图层与查看器可见。
   规格见 `11-material-field.md`（含「顶点索引插值」那条被否决的方案）。
2. **矽卡岩**没做：缺沉积岩（石灰岩）岩性，上位规格写明「需 3A 否则跳过」。

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
