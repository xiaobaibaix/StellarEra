# 术语表

项目自造词与同名异义速查。路径都相对 `Source/`。

## 宇宙与 N 体

| 术语 | 含义 | 位置 |
|---|---|---|
| N 体 / 求解器 | O(N²) 引力 + Velocity Verlet，单位 AU / AU每年 / M☉，双精度 | `StellarEraCosmos/Src/NBody/` |
| 子系统 | 全项目唯一跨帧状态归属：`UCosmosSimulationSubsystem`，每帧推进求解器并写回 Actor | `StellarEraCosmos/Src/` |
| 天体 | 参与 N 体的 Actor。行星、恒星、飞船都算 | 各 Actor |
| 代理天体 | 用「预制体 + 子 Actor」拼出来的天体：`BP_PlanetProxy` / `BP_StarProxy` 等，根组件挂质量体或子 Actor | `Content/Code/Planet/` |
| 质量体（MassBody） | 代理天体根上的无碰撞球体刚体，Chaos 积分；求解器只往里 `AddForce` | `BP_MassProxy` |
| 参数桥 | `UChildActorParamBridge` 多态管道：组件把参数写进子 Actor，不关心子类型 | `StellarEraCelestial/Common/ChildActorParamBridge.h` |
| 轨道组件 | `UCosmosOrbitComponent` 只存参数（质量、初速），不 Tick | `StellarEraCosmos/Src/` |
| 轨迹组件 | `UCosmosTrajectoryComponent` 预测线 / 拖尾 / 速度与引力箭头；运行期只读求解器 | `StellarEraCosmos/Src/Trajectory/` |

## 行星与地质

| 术语 | 含义 | 位置 |
|---|---|---|
| 体素 | VoxelFree 插件的稀疏体素世界；行星地形、碰撞、存档都在这一层 | 插件 + `StellarEraCelestial/Planet/Src/Geo/Voxel/` |
| 体积 / 体素单位 | 世界层用厘米；体素值 `FVoxelValue` 是 **int16**（±32767），不是 8 位 | — |
| 烘焙（Bake） | 把配方在后台线程算成行星数据；结果可缓存 | `Geo/Bake/` |
| 阶段（Stage） | 烘焙管线的一步：`IPlanetStage` 子类，有唯一阶段号，声明读写的字段 | `Geo/Bake/PlanetStage.h` |
| 快照（Snapshot） | 烘焙产物：形状 / 材质 / 诊断字段的只读数据，体素生成器只对它求值 | `Geo/Snapshot/` |
| 烘焙缓存 / BakeCache | 快照的序列化缓存，`PC_<关卡>_<Actor>`，走 LFS；格式版本在 `GeoBakeCacheFormat::Version` | `Geo/Snapshot/`、`Content/Code/Planet/BakeCache/` |
| 配方（Recipe） | `UPlanetRecipe`：大陆、板块、气候、矿物规则的参数集合，资产前缀 `DA_GeoRecipe_*` | `Geo/Assets/` |
| 岩性（Lithology） | 岩石类型枚举，决定体素颜色与矿物规则 | `Geo/Core/GeoLithology.h` |
| 矿物目录 | `UGeoMineralCatalog`：矿物定义与分布规则 | `Geo/Assets/` |
| 调试图层 | 阶段声明的可查看图层（地表叠加 / 剖面），查看器与导出都读注册表 | `Geo/Debug/`、`IPlanetStage::DescribeDebugLayers` |
| 剖切（Cutaway） | 生成器把行星切掉半/四分之一，露出切面着色；改它要重建网格 | `Geo/Voxel/PlanetGeoGenerator.h` |
| 调用器（Invoker） | 体素插件的 LOD 驱动组件；`IVoxelViewpointInvokers` 是三个 Pawn 与控制器共用的接口 | `StellarEraCelestial/Planet/Src/Invokers/` |
| AlwaysLocal | `AlwaysLocalVoxelInvokerComponent`：星图相机期间绕开全量重建的调用器子类 | `Src/Invokers/` |
| 大气壳 | `UPlanetAtmosphereComponent`：球外可见的 StaticMesh 壳，球内背面剔除 | `Atmosphere/` |
| SunVis | 大气材质里每颗太阳的可见度因子；多重太阳各产生一条本影线 | `Atmosphere/Sky/PlanetSkyModel.h` |
| 天光 | 引擎 `SkyLight` 的球内替代：`UPlanetSkyLightComponent`，暗面补光 | `Atmosphere/Sky/` |

## 玩法与界面

| 术语 | 含义 | 位置 |
|---|---|---|
| 星图 | 绕天体观察的相机模式，按 M 进出 | `StellarEraPawns/Src/StarMap/` |
| rig | `UStarMapRigComponent`：现算相机位姿，挂在星图相机 Pawn 内 | `StarMap/StarMapRigComponent.h` |
| 相机 Pawn | `AStarMapCameraPawn`：进星图时生成并被 Possess，退图销毁 | `StarMap/StarMapCameraPawn.h` |
| 编辑组件 | `UPlanetEditComponent`：屏幕射线 → 行星表面 → 笔刷应用；角色与星图共用 | `StellarEraCelestial/Planet/Src/Edit/` |
| 渐进编辑（Progressive） | 大半径笔刷分帧应用，避免一次性重建卡顿 | `Edit/Progressive/` |
| 骑乘（Ride） | 角色挂到行星下随其平移：`SetRidePlanet` → `AttachToActor`；`CarryRiders` 只兜底无父级 Pawn | `Character/`、`CosmosSimulationSubsystem` |
| 登船（Boarded） | 判据是 `IsBoarded()`，**不是** `GetAttachParentActor() != nullptr`（骑乘时父级就是行星） | `Character/PlanetCharacter.h` |

## 工程约定

| 术语 | 含义 |
|---|---|
| primary game module | `StellarEra`：承载 `IMPLEMENT_PRIMARY_GAME_MODULE`，`/Script/StellarEra.*` 路径的归属 |
| CoreRedirects | `Config/DefaultEngine.ini` 里 53 条历史类路径映射，模块合并后靠它兜底，**不要删** |
| LFS | `.uasset` / `.umap` 走 Git LFS；`Package is unloadable` 先查指针是否未 smudge |
| 源文件口径 | 统计与架构文档只数各模块 `Src/` 下的 `.h`/`.cpp`（含 `Tests/`），不含模块入口与 `Build.cs` |
