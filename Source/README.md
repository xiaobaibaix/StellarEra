# Source 目录导读

UE 要求所有模块平铺在 `Source/` 下，**物理上没有层次**。
这个文件是逻辑视图：按依赖层排列，告诉你每个模块是干什么的、想改东西该去哪。

依赖只能向下：上层可以 include 下层，下层不许 include 上层。

---

## 按层看

### L0 底座 —— 纯数据与计算，零项目依赖

| 模块 | 行数 | 一句话 | 什么时候动它 |
|---|---:|---|---|
| `StellarEraCore` | 1541 | N 体求解器、物理单位、AU/年/厘米换算 | 改积分算法、加天体类型 |
| `StellarEraGeology` | 2373 | 元素表、地质场、矿物分布、`UDataAsset` 预设 | 加矿种、改分布形状 |

### L1 契约 —— 接口住在这一层，谁都不依赖

| 模块 | 行数 | 一句话 | 什么时候动它 |
|---|---:|---|---|
| `StellarEraFramework` ★ | 新增 | 跨模块接口、事件总线、HUD/星图的 POD 结构 | 加一个新事件、加一个新接口 |

> 这一层存在的唯一理由：接口不该长在实现模块里。
> 原来 `IVoxelViewpointInvokers` 在 Terrain，逼得 UI 把 Terrain 当公开依赖。

### L2 领域 —— 世界怎么算的

| 模块 | 行数 | 一句话 | 什么时候动它 |
|---|---:|---|---|
| `StellarEraTerrain` | 4333 | 行星生成器、地形模块、地表射线、体素调用器 | 改地形形状、改采样精度 |
| `StellarEraCosmos` | 3230 | 唯一 N 体驱动、轨道参数、预测轨迹 | 改模拟步长、改预测窗口 |
| `StellarEraEconomy` ★ | 新增 | 物品/配方/矿藏储量/库存结算 | 加物品、改配方、改产出 |

### L3 表现 —— 玩家看得见的东西

| 模块 | 行数 | 一句话 | 什么时候动它 |
|---|---:|---|---|
| `StellarEraPlanet` ★ | 从 Terrain 拆出 | 体素行星 Actor、地表网格 | 改星球移动/缩放、改网格外观 |
| `StellarEraAtmosphere` | 978 | 大气壳、大气参数 | 改大气颜色、改高度 |
| `StellarEraMinerals` | 2211 | 矿物等值面、透视视图、矿体材质 | 改矿物透视效果 |
| `StellarEraPlanetEdit` | 2149 | 挖掘笔刷、渐进式编辑 | 改笔刷手感、加笔刷形状 |
| `StellarEraCharacter` | 1128 | 球面角色、移动组件、动画 | 改角色移动手感 |
| `StellarEraSpaceship` | 2146 | 飞船 Pawn、推力、碰撞 | 改飞船手感、改碰撞 |
| `StellarEraFactory` ★ | 新增 | 建筑 Actor、传送带、电网 | 加建筑类型 |

### L4 装配 —— 把上面的东西拼起来

| 模块 | 行数 | 一句话 | 什么时候动它 |
|---|---:|---|---|
| `StellarEraGameplay` ★ | 新增 | 星图/交互/登船/矿物视图/编辑模式的状态归属 | 改交互流程、加玩法规则 |
| `StellarEra` | 2143 | GameMode、PlayerController（只转发输入） | 改输入绑定、改开局流程 |

### L5 界面 —— 只读数据，不碰实现

| 模块 | 行数 | 一句话 | 什么时候动它 |
|---|---:|---|---|
| `StellarEraUI` | 2747 | HUD 绘制、星图面板、星图相机、编辑 UI | 改 HUD 显示、改星图样式 |

### 测试

| 模块 | 一句话 |
|---|---|
| `StellarEraTests` ★ | 30 个测试文件，按原模块分 10 个子目录 |

---

## 常见任务 → 去哪

| 你想做的事 | 打开这个文件 |
|---|---|
| 改地形起伏 | `StellarEraTerrain/Private/VoxelPlanetNoiseTerrain.cpp` |
| 改星球上矿物怎么分布 | `StellarEraGeology/Private/PlanetGeologyField.cpp` |
| 改矿物透视颜色 | `StellarEraMinerals/Private/PlanetMineralMaterialBuilder.cpp` |
| 改挖掘笔刷范围 | `StellarEraPlanetEdit/Private/PlanetEditBrush.cpp` |
| 改角色球面行走手感 | `StellarEraCharacter/Private/PlanetCharacterMovementComponent.cpp` |
| 改飞船推力/阻尼 | `StellarEraSpaceship/Private/SpaceshipPawn.cpp` |
| 改 N 体积分稳定性 | `StellarEraCore/Private/NBodySimulator.cpp` |
| 改模拟步长/时间流速 | `StellarEraCosmos/Public/CosmosSimulationSubsystem.h` 的 Config 段 |
| 改 HUD 显示内容 | `StellarEraUI/Private/StellarEraHUD.cpp` |
| 改星图交互 | `StellarEraUI/Private/StarMapUISystem.cpp` |
| 改按键绑定 | `StellarEra/Private/StellarEraPlayerController.cpp` 的 `SetupInputComponent` |
| 加一条生产配方 | `Content/Economy/DA_Recipe_*.uasset`（数据，不是代码） |

---

## 随时复查

```bash
# 架构体检（依赖环 / 超大文件 / 耦合广度 / 测试混杂）
python Tools/check_architecture.py

# 按层看模块（依赖深度自动计算）
python Tools/check_architecture.py --tree
```

新加的模块如果不在上面的表里，说明它还没归档——补一行进来。
