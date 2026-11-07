# Source 目录导读

UE 要求所有模块平铺在 `Source/` 下，**物理上没有层次**。
这个文件是逻辑视图：按依赖层排列，告诉你每个模块是干什么的、想改东西该去哪。

依赖只能向下：上层可以 include 下层，下层不许 include 上层。

**5 个模块，5 个层号。** 一个模块内部按子目录分功能，不靠拆模块来分。

---

## 目录怎么排

每个模块长得一样，就四种东西：

```
StellarEraPlanet/
├── StellarEraPlanet.Build.cs      构建规则
├── StellarEraPlanet.h             模块入口头，只放 IMPLEMENT_MODULE 相关
├── StellarEraPlanet.cpp
└── Src/                           业务代码全在这里
    ├── VoxelPlanetActor.h         头与源**同目录并列**
    ├── VoxelPlanetActor.cpp
    ├── Terrain/                   按功能分子目录，里面也是 .h/.cpp 并列
    │   ├── VoxelPlanetGenerator.h
    │   ├── VoxelPlanetGenerator.cpp
    │   └── ...
    ├── Atmosphere/  Minerals/  Edit/
    └── Tests/                     自动化测试（*Tests.cpp 只放这里）
```

- **模块入口的两个文件跟 `Build.cs` 并排**，`Src/` 里只有业务代码，不会被入口文件搅混。
- **头文件与源文件同目录**，改一个功能时打开的目录里 `.h`/`.cpp` 都在，不用两边跳。
- `Src/` 及每个子目录都会自动进头文件搜索路径（见 `StellarEraPlanet.Build.cs`
  里的循环），所以 `#include "VoxelPlanetGenerator.h"` 这种裸文件名全项目可用；
  **新增子目录不需要改 Build.cs**。

---

## 按层看

### L1 世界 —— 一颗行星的全部

| 模块 | 行数 | 一句话 | 什么时候动它 |
|---|---:|---|---|
| `StellarEraPlanet` | 29293 | 地形、水文、水面、大气、矿物（含地质场与矿脉形态）、编辑、体素接线 | 见下面的子目录表 |

```
StellarEraPlanet/Src/
├── VoxelPlanetActor.h/.cpp    星球本体（对外门面，所以放在 Src 根）
├── Terrain/                   地形长什么样：模块基类、噪声、纯球、适配器、生物群系配色
├── Hydrology/                 水文：烘焙（填洼 / 汇流 / 下切）、水位场与河流湖泊特征
├── Water/                     水面：跟随行星的动态网格组件、水面几何与材质
├── Invokers/                  体素 LOD 调用器接线 + IVoxelViewpointInvokers（角色 / 飞船 / 相机共用）
├── Surface/                   星球这个球本身：解析射线、编辑经纬网格
├── Atmosphere/                大气组件与参数、星体组件与阴影遮罩
├── Sky/  Star/                天空材质、恒星材质（各一个编辑器期构建器）
├── Minerals/                  矿物域：Geology（含 Distributions）/ Surface / View
├── Edit/                      编辑组件、笔刷、涂抹、渐进编辑、网格几何、编辑按钮布局
└── Tests/                     30 个测试
```

矿物域的四层，从数据到交互：

```
Minerals/
├── Geology/                 元素表、地质场、UDataAsset 预设  有什么、有多少
│   └── Distributions/       基类 + 囊状/脉状/片层/浸染四变体  什么形状
├── Surface/                 矿物表面几何、材质构建          怎么显示
└── View/                    观测 Actor 与组件               怎么看、怎么选
```

改矿怎么分布 → `Minerals/Geology/PlanetGeologyField.cpp`；
改矿有多大、什么形状 → `Minerals/Geology/Distributions/` 里对应的变体。

`Terrain/` 里的东西只被 Planet 内部用；`Invokers/` 里的被 `StellarEraPawns`（角色 /
飞船 / 星图相机）与装配层控制器跨模块引用 —— 它们不属于「地形」，是 Voxel 插件 LOD
的接线层，当初只是恰好放在 Terrain 文件夹里。

五个目录共享同一份体素密度场，这是它们合在一个模块里的理由。
编辑内核直接写它、地形生成直接写它、矿物与大气按它取采样点 ——
拆成四个模块时，每次跨模块调用都得把内部实现提成 Public。

### L2 受控实体 —— 玩家能 possess 的东西

| 模块 | 行数 | 一句话 | 什么时候动它 |
|---|---:|---|---|
| `StellarEraPawns` | 9312 | 角色、飞船、星图相机与各自的输入组件 | 见下面的子目录表 |

```
StellarEraPawns/Src/
├── Character/                 球面角色、移动组件
│   ├── Animations/            动画实例
│   └── Input/                 输入组件 + 角色动作表
├── Spaceship/                 飞船 Pawn、着陆调试球
│   └── Input/                 输入组件 + 飞船动作表
├── StarMap/                   星图相机（不 Tick，位置在 CalcCamera 现算）、面板布局、屏幕拾取
│   └── Input/                 输入组件 + 相机动作表
├── StellarEraInputSource.h    三个 Pawn 向控制器取动作表的接口
├── SessionCommands.h          Pawn 请求切星图 / 上下船的接口
└── Tests/                     11 个测试
```

三者的共同点是「被 `AStellarEraPlayerController` 通过 Possess 切换」，
且都实现 `IVoxelViewpointInvokers`。放在一起，换视点的逻辑才有唯一归属地。

三个可操控对象各有自己的输入组件（角色 / 飞船 / 星图相机），Pawn 的
`SetupPlayerInputComponent` 只做转发；控制器**一条输入都不绑**（`SetupInputComponent`
已删除）。上下船（E）、星图开关（M）与选择（左键）在每张需要它的表里各有一条，
编辑（G）、笔刷尺寸（`[` / `]`）与矿物透视（H 与数字键）随各自 Pawn 绑 —— 星图的
矿物透视会话状态也住在相机 Pawn 上，控制器只在换 Pawn / 退出星图 / 选中变化时命令它收尾。
星图的分派规则
`EStarMapLookMode` 与 `ResolveLookMode` 都在
`StarMap/Input/StarMapCameraInputComponent.h`。

> **星图相机在 `Src/StarMap/`。** 它依赖 Cosmos，而 Cosmos 曾经反向
> `Cast<ASpaceshipPawn>`；抽出 `ICosmosBody` 之后方向才理顺。
> 相机曾住在 `StellarEraUI`，蓝图子类靠 `[CoreRedirects]` 的 `+ClassRedirects` 兜底。

### L3 天体

| 模块 | 行数 | 一句话 | 什么时候动它 |
|---|---:|---|---|
| `StellarEraCosmos` | 7739 | N 体求解器、天体类型与单位、轨道参数、预测轨迹 | 改积分算法、改模拟步长、改预测窗口 |

```
StellarEraCosmos/Src/
├── NBody/                         纯计算层，不碰 UObject
│   ├── NBodySimulator.h/.cpp      积分器、能量基线、碰撞语义
│   └── CosmosTypes.h              单位系统、天体类型、积分器枚举（UENUM）
├── CosmosSimulationSubsystem.h/.cpp  唯一驱动：每帧推进、下发状态
├── CosmosOrbitComponent.h/.cpp    天体在被驱动方一侧的轨道参数
├── CosmosTrajectoryComponent.h/.cpp  预测轨迹的可视化
└── Tests/
```

`NBodySimulator` 与 `CosmosTypes` 原在 `StellarEraCore`。那个模块总共 2687 行，
其中 2411 行就是这一个求解器 —— 为它单独维持一个模块（Build.cs、入口、Target 登记）
不划算，而它唯一的使用方就是本模块，于是并了进来。

### L4 界面

| 模块 | 行数 | 一句话 | 什么时候动它 |
|---|---:|---|---|
| `StellarEraUI` | 2856 | HUD 绘制、星图面板、矿物图例 | 改 HUD 显示、改星图样式 |

星图 UI 需要的鼠标坐标、屏幕拾取、矿物透视都不是「只有控制器才知道」的信息：
前两个是对 `APlayerController::GetMousePosition` 与 `StarMapPicking` 的一行转发，
第三个是 `Cast<AStarMapCameraPawn>(Pawn)`。UI 本来就 Public 依赖 Pawns，手里也有
玩家控制器，所以直接调，**不设接口** —— 曾经有过一个 `IStarMapUIInputs`，四个
方法全是这种转发，删掉后 Game 的公开头不再 include 任何 UI 头，
`StellarEraUI` 也随之从公开依赖降为私有依赖。

### L5 装配

| 模块 | 行数 | 一句话 | 什么时候动它 |
|---|---:|---|---|
| `StellarEra` | 3290 | GameMode、PlayerController；`Src/Input/` 聚合三张 Pawn 动作表（控制器一条输入都不绑） | 改动作表的聚合构造、改开局流程 |

---

## 常见任务 → 去哪

| 你想做的事 | 打开这个文件 |
|---|---|
| 改地形起伏 | `StellarEraPlanet/Src/Terrain/VoxelPlanetNoiseTerrain.cpp` |
| 改星球移动/缩放 | `StellarEraPlanet/Src/VoxelPlanetActor.cpp` |
| 改大气颜色/高度 | `StellarEraPlanet/Src/Atmosphere/PlanetAtmosphereMaterialBuilder.cpp` |
| 改水文（河谷 / 湖泊） | `StellarEraPlanet/Src/Hydrology/PlanetHydrologyBake.cpp` |
| 改水面几何/材质 | `StellarEraPlanet/Src/Water/PlanetWaterComponent.cpp`（几何）/ `PlanetWaterMaterialBuilder.cpp`（材质） |
| 改矿物透视颜色 | `StellarEraPlanet/Src/Minerals/Surface/PlanetMineralMaterialBuilder.cpp` |
| 改挖掘笔刷范围 | `StellarEraPlanet/Src/Edit/PlanetEditBrush.cpp` |
| 改渐进编辑的速度 | `StellarEraPlanet/Src/Edit/PlanetProgressiveEdit.cpp` |
| 改星球上矿物怎么分布 | `StellarEraPlanet/Src/Minerals/Geology/PlanetGeologyField.cpp` |
| 改矿脉形状/大小/深度 | `StellarEraPlanet/Src/Minerals/Geology/Distributions/` 里对应变体 |
| 加一种新的矿脉形态 | 继承 `UPlanetGeologyDistribution`，实现 `BuildRuntime` |
| 改角色球面行走手感 | `StellarEraPawns/Src/Character/PlanetCharacterMovementComponent.cpp` |
| 改飞船推力/阻尼 | `StellarEraPawns/Src/Spaceship/SpaceshipPawn.cpp` |
| 改星图相机速度/缩放 | `StellarEraPawns/Src/StarMap/StarMapCameraPawn.cpp`（速度曲线）/ `Src/StarMap/Input/StarMapCameraInputComponent.cpp`（缩放绑定） |
| 改星图面板布局 / 拾取容差 | `StellarEraPawns/Src/StarMap/StarMapPanelLayout.cpp` / `StarMapPicking.cpp`（绘制与命中共用） |
| 改 N 体积分稳定性 | `StellarEraCosmos/Src/NBodySimulator.cpp` |
| 改模拟步长/时间流速 | `StellarEraCosmos/Src/CosmosSimulationSubsystem.h` 的 Config 段 |
| 加一种天体类型/单位制 | `StellarEraCosmos/Src/CosmosTypes.h` |
| 改 HUD 显示内容 | `StellarEraUI/Src/StellarEraHUD.cpp` |
| 改星图交互 | `StellarEraUI/Src/StarMapUISystem.cpp` |
| 改按键映射（哪个键是哪个动作） | 按归属找三张表各自的 `Build`：角色 `StellarEraPawns/Src/Character/Input/PlanetCharacterInputActions.cpp` / 飞船 `StellarEraPawns/Src/Spaceship/Input/SpaceshipInputActions.cpp` / 星图相机 `StellarEraPawns/Src/StarMap/Input/StarMapCameraInputActions.cpp`。E / M / 左键在需要它的表里各有一条（E：角色登船、飞船离船）；三张表由 `StellarEra/Src/Input/StellarEraInputActions.cpp` 的 `FStellarEraInputActions` 聚合构造 |
| 改角色 / 飞船 / 星图相机对输入的响应 | `StellarEraPawns/Src/Character/Input/PlanetCharacterInputComponent.cpp` / `Src/Spaceship/Input/SpaceshipInputComponent.cpp` / `Src/StarMap/Input/StarMapCameraInputComponent.cpp`（三个输入组件都在 Pawn 的构造函数里 `CreateDefaultSubobject`，`SetupPlayerInputComponent` 只调 `Bind`） |
| 改 E / M / 左键的绑定 | 控制器**不绑任何输入**（`SetupInputComponent` 已删除）；三个 Pawn 各绑自己表里的动作：角色 `Src/Character/Input/PlanetCharacterInputComponent.cpp`、飞船 `Src/Spaceship/Input/SpaceshipInputComponent.cpp`、星图相机 `Src/StarMap/Input/StarMapCameraInputComponent.cpp`。动作由控制器造，Pawn 经 `IStellarEraInputSource`（`StellarEraPawns/Src/StellarEraInputSource.h`）只取自己那张表；跨 Pawn 的 M（切星图）与 E（上下船）都由 Pawn 自己执行 —— 先经 `IStellarEraSessionCommands`（`Src/SessionCommands.h`）向控制器查询目标（M：`FindStarMapCameraFor`；E：`FindBoardableShipFrom` / `GetOrSpawnCrewCharacter`），机械动作在 `AStarMapCameraPawn::EnterStarMap` / `ExitStarMap` 与 `ASpaceshipPawn::Board` / `DisembarkCrew`，`Possess` 仍由控制器完成。退星图不问控制器：相机自己记着是谁把它叫出来的 |

---

## 三条纪律

1. **`*Tests.cpp` 只能出现在 `Src/Tests/`**，不许散在 `Src/` 根。
   测试要放哪个模块，看它 include 了谁 —— 必须是所引头文件的下游。
   跨模块的集成测试放装配层（`StellarEra/Src/Tests/`）。
2. **新增功能就加一个子目录，`.h` 与 `.cpp` 一起放进去。** 不要往 `Src/` 根堆文件，
   那一层只留模块门面（`VoxelPlanetActor.h` 这类被外部当入口用的）。
3. **接口放到「已经被所有实现方与使用方依赖的最低层」**，而且只在**消费方够不到
   实现**时才建。`IVoxelViewpointInvokers` 在 `Planet/Src/Invokers/`（Pawns 与 UI
   都已 Public 依赖 Planet）；`ICosmosBody` 声明在 Cosmos（消费方）、由 Pawns 的
   飞船实现 —— 消费方在低层时，接口就放低层。反例是刚删掉的 `IStarMapUIInputs`：
   消费方（UI）本就依赖实现方所在的 Pawns、手里还有 `APlayerController`，
   接口只是给四个一行转发多起了一套名字。

### 什么时候该建子目录

| 目录内文件数 | 怎么做 |
|---|---|
| ≤ 12 | 别拆。命名前缀（`PlanetMineral*`）已经够用 |
| 13–15 | 先问「这里面有没有两类职责」，有才拆 |
| ≥ 16 | 基本是已经过载了，拆 |

拆的方式：**按「它回答什么问题」分，不按类名分**。拆完每族至少 4 个文件才值得
单独开一个目录 —— 一个目录里只放两个文件，是把分类做成了负担。

判据要看 include 关系而不是文件名：被别的模块引用的东西，往往根本不属于当前这个
功能面（`Invokers/` 就是这么从 `Terrain/` 里分出来的）。

---

## 随时复查

```bash
# 架构体检（依赖环 / 超大文件 / 耦合广度 / 测试是否散落）
python Tools/check_architecture.py

# 按层看模块（依赖深度自动计算）
python Tools/check_architecture.py --tree
```

新加的模块如果不在上面的表里，说明它还没归档 —— 补一行进来。

分层依据、依赖矩阵、目录判据、通讯机制的完整说明在 **`docs/ARCHITECTURE.md`**。
