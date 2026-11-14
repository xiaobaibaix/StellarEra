# 地质可视化（图层注册 + 星球叠加 / 剖切 + 2D 查看器）Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: superpowers:executing-plans。用户已确认方案 A，并要求「做好计划，直接开始，不再询问，直到做好、测试好」。

**Goal:** 让每个地质阶段的数据都能直观地看：星球表面叠加、星球剖切、编辑器 2D 查看器（展开地图 + 剖面 + 读数 + 图例 + PNG 导出），以后的阶段只注册图层，不改查看器。

**Architecture:** 图层表由各阶段自己声明（`IPlanetStage::DescribeDebugLayers`），查看器只消费表。编辑器烘焙额外保留一份调试场（`FPlanetSnapshot::Debug`，不进哈希、不进运行时）。三种显示共用同一套取值与配色函数（`Geo/Debug/`）。查看器面板放在新的编辑器模块 `StellarEraPlanetEditor`。

## 设计要点

| 组件 | 位置 | 职责 |
|---|---|---|
| `FGeoDebugLayer` / `PlanetGeoDebug::Layers()` | `Planet/Src/Geo/Debug/PlanetGeoDebugLayer.*` | 图层描述（编号、中文名、阶段、地表/地下、分类/连续、取值、配色、图例）；从默认管线收集 |
| `IPlanetStage::DescribeDebugLayers` / `ExportDebugFields` | `Geo/Bake/PlanetStage.h` | 阶段声明图层、把要看的中间场交给调试快照 |
| `FPlanetDebugFields` | `Geo/Snapshot/PlanetSnapshot.h` | 只在 `bKeepDebugFields` 时存在；不进 ContentHash |
| `PlanetGeoDebugRender` | `Geo/Debug/PlanetGeoDebugRender.*` | 展开地图、过球心剖面、PNG、瞬态立方体贴图 |
| `StellarEra.Geo.ExportLayers` | `Geo/Debug/PlanetGeoDebugCommands.cpp` | 无界面导出全部图层 PNG 到 `Saved/GeoDebug/` |
| 星球叠加 | `AVoxelPlanetActor`（`GeoDebugSurfaceLayer` / `GeoDebugOverlayOpacity`） | 图层烘成立方体贴图喂 `M_PlanetVertexColor.BiomeCube`；不重建网格 |
| 星球剖切 | `UPlanetGeoGenerator`（`Cutaway` / `VolumeLayer`）+ 星球 `GeoCutaway` / `GeoDebugVolumeLayer` | 密度与保留半空间求交；切面按地下图层着色；进参数哈希（要重建网格） |
| 地质查看器 | `StellarEraPlanetEditor/Src/SGeoViewerPanel.*` | 菜单「窗口 → 地质查看器」；选星球、选图层、悬停读数、图例、导出 |

## Tasks

1. 图层表 + 阶段接口 + 调试场（阶段 1：岩性、莫霍面起伏；阶段 2：高程、板块、边界类型、大陆度、地壳类型/年龄/厚度）。
2. 渲染器 + 导出命令。
3. 生成器剖切 + 星球属性（叠加、剖切）。
4. 编辑器模块与查看器面板；`.uproject`、Editor Target、`ARCHITECTURE.md`、`CLAUDE.md` 同步。
5. 测试 `StellarEra.Planet.Geo.Debug.*`：注册完整性与编号唯一；调试场不改哈希；剖面圈层分界与配方深度一致；地图加权陆地占比 = LandFrac；立方体贴图 Outer 为瞬态包；剖切密度与切面颜色。
6. 编辑器实机验证：叠加切换、剖切、查看器、导出命令；整包测试 0 失败。
