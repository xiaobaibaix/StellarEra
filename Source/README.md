# Source 目录导读

UE 要求所有模块平铺在 `Source/` 下，**物理上没有层次**。
这个文件是逻辑视图：按依赖层排列，告诉你每个模块是干什么的、想改东西该去哪。

依赖只能向下：上层可以 include 下层，下层不许 include 上层。

**7 个模块，6 个层号。** 一个模块内部按子目录分功能，不靠拆模块来分。

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

### L0 底座 —— 零项目依赖

| 模块 | 行数 | 一句话 | 什么时候动它 |
|---|---:|---|---|
| `StellarEraCore` | 2694 | N 体求解器、物理单位、通用接口 | 改积分算法、加天体类型、加跨模块接口 |
| `StellarEraGeology` | 3193 | 元素表、地质场、矿物分布、`UDataAsset` 预设 | 加矿种、改分布形状 |

`StellarEraCore/Src/Interfaces/VoxelViewpointInvokers.h` 里是 `IVoxelViewpointInvokers`。
它住在这里而不是某个实现模块，是因为实现方（Pawns 的两个 Pawn）和使用方（控制器）
不该为这一个头互相依赖。

### L1 世界 —— 一颗行星的全部

| 模块 | 行数 | 一句话 | 什么时候动它 |
|---|---:|---|---|
| `StellarEraPlanet` | 14982 | 地形生成、大气、矿物、编辑 —— 四合一 | 见下面的子目录表 |

```
StellarEraPlanet/Src/
├── VoxelPlanetActor.h/.cpp    星球本体（对外门面，所以放在 Src 根）
├── Terrain/                   生成器、地形模块、表面射线、网格、生物群系、调用器
├── Atmosphere/                大气组件与参数
├── Minerals/                  矿物表面、观察组件、观察 Actor、材质
├── Edit/                      编辑组件、笔刷、涂抹、渐进编辑、网格几何
└── Tests/                     12 个测试
```

四个子目录共享同一份体素密度场，这是它们合在一个模块里的理由。
编辑内核直接写它、地形生成直接写它、矿物与大气按它取采样点 ——
拆成四个模块时，每次跨模块调用都得把内部实现提成 Public。

### L2 受控实体 —— 玩家能 possess 的东西

| 模块 | 行数 | 一句话 | 什么时候动它 |
|---|---:|---|---|
| `StellarEraPawns` | 4477 | 角色、飞船 —— 二合一 | 见下面的子目录表 |

```
StellarEraPawns/Src/
├── Character/                 球面角色、移动组件、动画实例
├── Spaceship/                 飞船 Pawn
└── Tests/                     7 个测试
```

两者的共同点是「被 `AStellarEraPlayerController` 通过 Possess 切换」，
且都实现 `IVoxelViewpointInvokers`。放在一起，换视点的逻辑才有唯一归属地。

> **星图相机不在这个模块**，它在 `StellarEraUI`。它确实是个被 possess 的 Pawn，
> 但它调 `UCosmosSimulationSubsystem`，而 Cosmos 里有 4 处 `Cast<ASpaceshipPawn>`，
> 搬过来就是 `Pawns → Cosmos → Pawns`。等抽出 `ICosmosBody` 后再搬。

### L3 天体

| 模块 | 行数 | 一句话 | 什么时候动它 |
|---|---:|---|---|
| `StellarEraCosmos` | 3705 | 唯一 N 体驱动、轨道参数、预测轨迹 | 改模拟步长、改预测窗口 |

### L4 界面

| 模块 | 行数 | 一句话 | 什么时候动它 |
|---|---:|---|---|
| `StellarEraUI` | 4272 | HUD 绘制、星图面板、编辑 UI、星图相机、`IStarMapUIInputs` | 改 HUD 显示、改星图样式 |

`StarMapUIInputs.h` 在这里而不是在 Game 模块，是正确的依赖倒置：
UI 声明自己需要什么，Game 去实现，于是 UI 不需要 include 控制器。

### L5 装配

| 模块 | 行数 | 一句话 | 什么时候动它 |
|---|---:|---|---|
| `StellarEra` | 3157 | GameMode、PlayerController（只转发输入） | 改输入绑定、改开局流程 |

---

## 常见任务 → 去哪

| 你想做的事 | 打开这个文件 |
|---|---|
| 改地形起伏 | `StellarEraPlanet/Src/Terrain/VoxelPlanetNoiseTerrain.cpp` |
| 改星球移动/缩放 | `StellarEraPlanet/Src/VoxelPlanetActor.cpp` |
| 改大气颜色/高度 | `StellarEraPlanet/Src/Atmosphere/PlanetAtmosphereMaterialBuilder.cpp` |
| 改矿物透视颜色 | `StellarEraPlanet/Src/Minerals/PlanetMineralMaterialBuilder.cpp` |
| 改挖掘笔刷范围 | `StellarEraPlanet/Src/Edit/PlanetEditBrush.cpp` |
| 改渐进编辑的速度 | `StellarEraPlanet/Src/Edit/PlanetProgressiveEdit.cpp` |
| 改星球上矿物怎么分布 | `StellarEraGeology/Src/PlanetGeologyField.cpp` |
| 改角色球面行走手感 | `StellarEraPawns/Src/Character/PlanetCharacterMovementComponent.cpp` |
| 改飞船推力/阻尼 | `StellarEraPawns/Src/Spaceship/SpaceshipPawn.cpp` |
| 改星图相机速度/缩放 | `StellarEraUI/Src/StarMapCameraPawn.cpp` |
| 改 N 体积分稳定性 | `StellarEraCore/Src/NBodySimulator.cpp` |
| 改模拟步长/时间流速 | `StellarEraCosmos/Src/CosmosSimulationSubsystem.h` 的 Config 段 |
| 改 HUD 显示内容 | `StellarEraUI/Src/StellarEraHUD.cpp` |
| 改星图交互 | `StellarEraUI/Src/StarMapUISystem.cpp` |
| 改按键绑定 | `StellarEra/Src/StellarEraPlayerController.cpp` 的 `SetupInputComponent` |

---

## 三条纪律

1. **`*Tests.cpp` 只能出现在 `Src/Tests/`**，不许散在 `Src/` 根。
   测试要放哪个模块，看它 include 了谁 —— 必须是所引头文件的下游。
   跨模块的集成测试放装配层（`StellarEra/Src/Tests/`）。
2. **新增功能就加一个子目录，`.h` 与 `.cpp` 一起放进去。** 不要往 `Src/` 根堆文件，
   那一层只留模块门面（`VoxelPlanetActor.h` 这类被外部当入口用的）。
3. **接口放 `StellarEraCore/Src/Interfaces/`**，除非它像 `IStarMapUIInputs` 那样
   明确属于某个使用方（那时由使用方定义，上层实现）。

---

## 随时复查

```bash
# 架构体检（依赖环 / 超大文件 / 耦合广度 / 测试是否散落）
python Tools/check_architecture.py

# 按层看模块（依赖深度自动计算）
python Tools/check_architecture.py --tree
```

新加的模块如果不在上面的表里，说明它还没归档 —— 补一行进来。
