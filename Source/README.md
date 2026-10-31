# Source 目录导读

UE 要求所有模块平铺在 `Source/` 下，**物理上没有层次**。
这个文件是逻辑视图：按依赖层排列，告诉你每个模块是干什么的、想改东西该去哪。

依赖只能向下：上层可以 include 下层，下层不许 include 上层。

**7 个模块，6 个层号。** 一个模块内部按子目录分功能，不再靠拆模块来分。

---

## 按层看

### L0 底座 —— 零项目依赖

| 模块 | 行数 | 一句话 | 什么时候动它 |
|---|---:|---|---|
| `StellarEraCore` | 2694 | N 体求解器、物理单位、通用接口（`Public/Interfaces/`） | 改积分算法、加天体类型、加跨模块接口 |
| `StellarEraGeology` | 3193 | 元素表、地质场、矿物分布、`UDataAsset` 预设 | 加矿种、改分布形状 |

`StellarEraCore/Public/Interfaces/VoxelViewpointInvokers.h` 里是 `IVoxelViewpointInvokers`。
它住在这里而不是某个实现模块，是因为实现方（Pawns 的三个 Pawn）和使用方（控制器）
不该为这一个头互相依赖。

### L1 世界 —— 一颗行星的全部

| 模块 | 行数 | 一句话 | 什么时候动它 |
|---|---:|---|---|
| `StellarEraPlanet` | 14982 | 地形生成、大气、矿物、编辑 —— 四合一 | 见下面的子目录表 |

```
StellarEraPlanet/
├── Public/VoxelPlanetActor.h      星球本体（对外门面）
├── Public/Terrain/                生成器、地形模块、表面射线、网格、生物群系、调用器
├── Public/Atmosphere/             大气组件与参数
├── Public/Minerals/               矿物表面、观察组件、观察 Actor、材质
├── Public/Edit/                   编辑组件、笔刷、涂抹、渐进编辑、网格几何
└── Private/Tests/                 12 个测试
```

四个子目录共享同一份体素密度场，这是它们合在一个模块里的理由。
编辑内核直接写它、地形生成直接写它、矿物与大气按它取采样点 ——
拆成四个模块时，每次跨模块调用都得把内部实现提成 Public。

### L2 受控实体 —— 玩家能 possess 的东西

| 模块 | 行数 | 一句话 | 什么时候动它 |
|---|---:|---|---|
| `StellarEraPawns` | 5735 | 角色、飞船、星图相机 —— 三合一 | 见下面的子目录表 |

```
StellarEraPawns/
├── Public/Character/              球面角色、移动组件、动画实例
├── Public/Spaceship/              飞船 Pawn
├── Public/Camera/                 星图相机
└── Private/Tests/                 8 个测试
```

三者的共同点是「被 `AStellarEraPlayerController` 通过 Possess 切换」，
且都实现 `IVoxelViewpointInvokers`。放在一起，换视点的逻辑才有唯一归属地。

### L3 天体

| 模块 | 行数 | 一句话 | 什么时候动它 |
|---|---:|---|---|
| `StellarEraCosmos` | 4402 | 唯一 N 体驱动、轨道参数、预测轨迹 | 改模拟步长、改预测窗口 |

### L4 界面

| 模块 | 行数 | 一句话 | 什么时候动它 |
|---|---:|---|---|
| `StellarEraUI` | 3014 | HUD 绘制、星图面板、编辑 UI、`IStarMapUIInputs` | 改 HUD 显示、改星图样式 |

`StarMapUIInputs.h` 在这里而不是在 Game 模块，是正确的依赖倒置：
UI 声明自己需要什么，Game 去实现，于是 UI 不需要 include 控制器。

### L5 装配

| 模块 | 行数 | 一句话 | 什么时候动它 |
|---|---:|---|---|
| `StellarEra` | 2460 | GameMode、PlayerController（只转发输入） | 改输入绑定、改开局流程 |

---

## 常见任务 → 去哪

| 你想做的事 | 打开这个文件 |
|---|---|
| 改地形起伏 | `StellarEraPlanet/Private/Terrain/VoxelPlanetNoiseTerrain.cpp` |
| 改星球移动/缩放 | `StellarEraPlanet/Private/VoxelPlanetActor.cpp` |
| 改大气颜色/高度 | `StellarEraPlanet/Private/Atmosphere/PlanetAtmosphereMaterialBuilder.cpp` |
| 改矿物透视颜色 | `StellarEraPlanet/Private/Minerals/PlanetMineralMaterialBuilder.cpp` |
| 改挖掘笔刷范围 | `StellarEraPlanet/Private/Edit/PlanetEditBrush.cpp` |
| 改渐进编辑的速度 | `StellarEraPlanet/Private/Edit/PlanetProgressiveEdit.cpp` |
| 改星球上矿物怎么分布 | `StellarEraGeology/Private/PlanetGeologyField.cpp` |
| 改角色球面行走手感 | `StellarEraPawns/Private/Character/PlanetCharacterMovementComponent.cpp` |
| 改飞船推力/阻尼 | `StellarEraPawns/Private/Spaceship/SpaceshipPawn.cpp` |
| 改星图相机速度/缩放 | `StellarEraPawns/Private/Camera/StarMapCameraPawn.cpp` |
| 改 N 体积分稳定性 | `StellarEraCore/Private/NBodySimulator.cpp` |
| 改模拟步长/时间流速 | `StellarEraCosmos/Public/CosmosSimulationSubsystem.h` 的 Config 段 |
| 改 HUD 显示内容 | `StellarEraUI/Private/StellarEraHUD.cpp` |
| 改星图交互 | `StellarEraUI/Private/StarMapUISystem.cpp` |
| 改按键绑定 | `StellarEra/Private/StellarEraPlayerController.cpp` 的 `SetupInputComponent` |

---

## 三条纪律

1. **`*Tests.cpp` 只能出现在 `Private/Tests/`**，不许散在 `Private/` 根。
   测试要放哪个模块，看它 include 了谁 —— 必须是所引头文件的下游。
   跨模块的集成测试放装配层（`StellarEra/Private/Tests/`）。
2. **内部头放 `Private/`。** 同模块内编译边界是松的，靠人力约束；
   `PlanetProgressiveEditKernel.h` 这类纯实现头一旦被外部引用，以后就删不掉了。
3. **接口放 `Core/Public/Interfaces/`**，除非它像 `IStarMapUIInputs` 那样
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
