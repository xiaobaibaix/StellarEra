# StellarEra 架构方案

> ⚠️ **本文已作废，仅作历史记录保留。**
>
> 它提的是 **17 模块方案**：新增 `StellarEraFramework`、`StellarEraTests`、
> `StellarEraEconomy`、`StellarEraFactory`、`StellarEraGameplay` 等模块。
> 该方案在 2026-10-31 当天被反转为 **11 → 7 合并**，理由见 `docs/TREE_AFTER.md`。
>
> **当前架构以 `docs/ARCHITECTURE.md` 为准。**
> 本文提到的 `StellarEraFramework` / `StellarEraMessageBus` / `HUDState` /
> `PlanetSurfaceQuery` 在代码里**都不存在**，不要照着实现。

> 基于 2026-10-31 的全量代码体检（11 模块 / 142 文件 / 36054 行）。
> 复现方式：`python Tools/check_architecture.py`

---

## 摘要

项目**不是没有框架**，模块划分已经做对了大半——11 个模块、依赖无环。真正的问题是三个缺失：

1. **缺状态归属层**：全项目只有 1 个 Subsystem（Cosmos），其余跨帧状态只能堆进 `AStellarEraPlayerController`，把它养成 1449 行、跨 7 模块的上帝类。这是「改代码麻烦」的直接来源。
2. **缺跨层契约模块**：UI 需要的接口被放在实现模块（`VoxelViewpointInvokers.h` 在 Terrain），逼得 UI 反向依赖 Terrain 并把它提为 Public 依赖。UI 现在直接 include 了 `CosmosSimulationSubsystem`、`VoxelPlanetActor`。
3. **缺玩法层**：`docs/VoxelTestStudy/VOXEL_FACTORY_DESIGN.md` 写了 460 行工厂设计，`VoxelFactory` 插件里有 8 个原型类，但**没有任何一行进了 `Source/`**。技术层已经饱和，玩法层是空的。

所以方案不是「引入 MVC 或 MVVM」，而是**补齐这三层，再把已有的东西归位**。

---

## 一、体检结果

### 1.1 规模与依赖

| 模块 | 业务行 | 测试行 | 依赖数 |
|---|---:|---:|---:|
| StellarEraTerrain | 4333 | 2231 | 1 |
| StellarEraCosmos | 3230 | 475 | 3 |
| StellarEraUI | 2747 | 590 | **8** |
| StellarEraGeology | 2373 | 820 | 0 |
| StellarEraMinerals | 2211 | 2018 | 3 |
| StellarEraPlanetEdit | 2149 | 528 | 1 |
| StellarEraSpaceship | 2146 | 1136 | 3 |
| **StellarEra**（主模块） | 2143 | 1948 | **8** |
| StellarEraCore | 1541 | 1109 | 0 |
| StellarEraCharacter | 1128 | 79 | 2 |
| StellarEraAtmosphere | 978 | 141 | 1 |

**测试代码 11075 行，占 31%，全部混在 10 个业务模块的 `Private/` 里。**

### 1.2 五个具体症状

| # | 症状 | 证据 | 代价 |
|---|---|---|---|
| S1 | 上帝类 | `StellarEraPlayerController.cpp` 1449 行，是**全项目唯一**跨 7 模块 include 的文件 | 改任何一个交互都要动它；且它与 Cosmos/UI 双向牵扯 |
| S2 | 测试淹没业务 | 30 个 `*Tests.cpp` 在业务模块内，Terrain 32 个文件里 6 个是测试 | 打开模块先看到一半测试，可读性直接崩 |
| S3 | 头文件爆炸 | `SpaceshipPawn.h` 942 行 | 头文件比大多数类的实现还长，改一行触发全模块重编 |
| S4 | 分层倒挂 | Terrain 被 7 个模块依赖，但 Public 里有 `AVoxelPlanetActor`（表现）+ `PlanetSurfaceRay`（纯计算） | 底层不稳定 → 上层全重编；想复用纯计算必须先拉进 Actor |
| S5 | 玩法层空白 | `Source/` 里 0 个玩法模块；工厂设计在 docs、原型在 docs 里的独立插件 | 设计无法落地；再拖下去技术层会继续膨胀 |

### 1.3 值得保留的部分

不要推倒重来，这几点做得对：

- **依赖无环**，且 `StellarEraCore` / `StellarEraGeology` 是干净的零依赖底座。
- **`UCosmosSimulationSubsystem` 是全项目的正确样板**：唯一的 N 体驱动、单向喂数据、注释写清了「为什么是 Subsystem 不是 GameMode」。方案就是把这个模式推广到其余系统。
- **`UPlanetGeologyDistribution` 的数据驱动多态**（基类 + 意染/片层/囊状/脉状子类 + `UPlanetGeologyPreset : UDataAsset`）是玩法层该抄的模板。
- 注释密度极高，思路都留下了——这是重构最大的本钱。

---

## 二、目标架构

### 2.1 六层，单向依赖

```
L0  底座      StellarEraCore        StellarEraGeology
                 ↓                      ↓
L1  契约      StellarEraFramework    ← 新增：接口 / ViewModel / 消息 Tag
                 ↓
L2  领域      StellarEraTerrain     StellarEraCosmos     StellarEraEconomy ← 新增
                 ↓
L3  表现      StellarEraPlanet ←新   Atmosphere  Minerals  PlanetEdit
              Character  Spaceship   StellarEraFactory ← 新增
                 ↓
L4  装配      StellarEraGameplay ← 新增    StellarEra（GameMode / PlayerController）
                 ↓
L5  界面      StellarEraUI（只依赖 L1 的契约 + L2 的只读查询）
```

规则只有一条：**箭头只能向下**。上层知道下层，下层不知道上层。

### 2.2 两个新基础模块

**`StellarEraFramework`（L1，必做）**
存放所有「跨层契约」，只依赖 Core/CoreUObject/Engine：

- `IStarMapUIInputs`（从 UI 搬入）、`IVoxelViewpointInvokers`（**从 Terrain 搬出**）
- 所有 ViewModel 基类与接口
- `GameplayMessage` 的 Tag 常量（`StellarEra.Message.Ore.Mined` 等）
- 通用枚举/结构体（跨模块共享但不是物理计算的）

> 这一个模块能同时解决 S4 和 UI 的反向依赖：接口不再长在实现模块里，Terrain 也就不必当 UI 的 Public 依赖。

**`StellarEraTests`（可选但强烈建议）**
`Type = "Developer"`，依赖全部业务模块，装载所有 `*Tests.cpp`。解决 S2，且测试代码不进 Shipping 包。

---

## 三、三条铁律

写下来、能自动检查、违反就拦住。这是「框架」真正起作用的地方——不靠自觉。

### 铁律一：跨帧状态必须有唯一归属

> 任何跨帧存在的状态，必须由一个 `U*Subsystem` 持有。
> Actor / Pawn / Widget / PlayerController 只能是它的一面镜子。

判定：如果一个字段需要被两个不同类读写，那它就不该待在这两个类里。

### 铁律二：接口住在 Framework，不住在实现模块

> 一个接口只要被两个以上模块使用，就必须放进 `StellarEraFramework`。

判定：`Source/*/Public/*.h` 里出现 `class UINTERFACE` 且被其他模块 include → 不合格。

### 铁律三：UI 只吃数据，不碰实现

> `StellarEraUI` 允许 include 的只有：Framework 的接口/VM、Core 的类型。
> 不允许 include 任何 Actor、Component、Subsystem。

判定：`grep "#include" Source/StellarEraUI | grep -E "Actor|Pawn|Component|Subsystem"` 应为空。
现在这条**全线违规**——这是 UI 模块最大的技术债。

---

## 四、改造清单

按「风险 / 收益」排序，每项独立可交付、可回滚。

### P0 零风险，先做（不改一行逻辑）

| 项 | 动作 | 收益 |
|---|---|---|
| P0-1 | 建 `StellarEraTests` 模块，搬 30 个 `*Tests.cpp` | 业务模块立刻瘦 31%，可读性回正 |
| P0-2 | 建 `StellarEraFramework`，先把 `IVoxelViewpointInvokers` 从 Terrain 搬入 | 解除 UI→Terrain 的 Public 依赖枷锁 |
| P0-3 | 把 `Tools/check_architecture.py` 挂到提交前 | 防止退化 |

> P0 全部是「搬文件 + 改 include 路径」，编译通过即完成，无运行时风险。

### P1 中期（动结构，不动行为）

| 项 | 动作 | 收益 |
|---|---|---|
| P1-1 | 按 §5 拆 `AStellarEraPlayerController` | 最大痛点，单文件 1449 → ~250 |
| P1-2 | 把 `AVoxelPlanetActor` 从 Terrain 拆到新建 `StellarEraPlanet` | 底层稳定，纯计算可独立复用 |
| P1-3 | UI 改为只依赖 Framework 的 VM | 满足铁律三 |

### P2 长期（玩法落地）

| 项 | 动作 |
|---|---|
| P2-1 | 建 `StellarEraEconomy`：物品/配方/矿藏储量/物流（纯数据 + Subsystem） |
| P2-2 | 建 `StellarEraFactory`：建筑 Actor + 生产表现 |
| P2-3 | 建 `StellarEraGameplay`：进度、解锁、建造规则 |

---

## 五、样板：PlayerController 怎么拆

现状：一个类干 8 件事——输入、相机、拾取、上下船、星图、矿物视图、编辑模式、HUD 提示。
目标：**Controller 只做转发，判断全部下沉**。

| 新类 | 类型 | 职责 | 预估 |
|---|---|---|---:|
| `AStellarEraPlayerController` | APlayerController | 输入绑定 → 转发；Pawn 切换；模式门控 | ~250 |
| `UStellarEraInteractionSubsystem` | UTickableWorldSubsystem | 屏幕拾取、交互目标解析、距离/朝向判定 | ~200 |
| `UStarMapSubsystem` | UWorldSubsystem | 星图激活、选中天体、天体屏幕投影 | ~250 |
| `UBoardingSubsystem` | UWorldSubsystem | 上下船状态机、相机/碰撞体切换 | ~180 |
| `UPlanetEditModeSubsystem` | UWorldSubsystem | 编辑模式开关、笔刷参数 | ~150 |
| `UMineralViewSubsystem` | UWorldSubsystem | 矿物视图开关、元素过滤 | ~120 |
| `UStellarEraHUDViewModel` | UObject | 提示文本、准星状态、HUD 数值 | ~150 |

**输入为什么留在 Controller**：Enhanced Input 的 `InputAction` 天生绑在 Controller/Pawn 上，抽出去只会多一层转发。Controller 保留「接收输入」的权利，但**不保留「响应输入」的逻辑**——只写一行 `GetSubsystem<UStarMapSubsystem>()->Toggle()`。

**关键判断标准**：改星图逻辑时，是否还需要打开 `StellarEraPlayerController.cpp`？拆对了，答案是「不用」。

### 拆解顺序（每一步都能编译、能进 PIE）

1. 先抽 `UStarMapSubsystem`（内聚性最强、与 Cosmos 已有交集）
2. 再抽 `UStellarEraInteractionSubsystem`（拾取逻辑能整体搬走）
3. 然后 `UBoardingSubsystem`（状态机清晰）
4. 最后 `UMineralView` + `UPlanetEditMode`（两个小的，可以一起做）

每步完成后跑 `Tools/check_architecture.py`，看 `StellarEraPlayerController.cpp` 的跨模块 include 数是否在下降。**从 7 降到 2 以下即达标**。

---

## 六、玩法层骨架（对接工厂设计文档）

`VOXEL_FACTORY_DESIGN.md` 已经定义得很清楚：12 种物品、五层链条、配方驱动。架构只需要给它两个落点。

### 6.1 `StellarEraEconomy`（L2，纯数据 + 模拟，无 Actor）

```
Public/
├── EconomyTypes.h          ← EOreType/EMaterialType/EComponentType、FItemStack
├── RecipeAsset.h           ← URecipeAsset : UPrimaryDataAsset（配方）
├── OreDepositComponent.h   ← 星球上的矿藏储量（纯数据，不渲染）
└── EconomySubsystem.h      ← UWorldSubsystem：全局库存、物流结算
```

**为什么矿藏是 Component 不是 Actor**：储量是行星的属性，跟着 `AVoxelPlanetActor` 走，但计算完全不需要 Actor 参与。这样存档 = 序列化组件，不用遍历场景。

### 6.2 `StellarEraFactory`（L3，表现 + 局部逻辑）

```
Public/
├── FactoryBuilding.h       ← 建筑基类（挖机/冶炼炉/组装台）
├── ConveyorBelt.h          ← 传送带（只在建筑之间搬 FItemStack）
└── PowerGridComponent.h    ← 电网
```

建筑 = `StellarEraEconomy` 数据的**视觉代理**：销毁 Actor 不丢库存，库存归 Subsystem。这与 Cosmic 的「飞船不自己位移、位移归求解器」是同一条原则。

### 6.3 从原型迁移

`docs/VoxelTestStudy/VoxelFactory/` 里的 8 个类（`OreSystem` / `FactoryRecipe` / `MiningRig` / `ProcessingBuilding` / `ConveyorBelt` / `PowerGrid` / `PlanetPresets` / `PlanetSurfaceProbe`）按职责对半分：

| 原型类 | 去处 |
|---|---|
| `OreSystem` `FactoryRecipe` `FactoryTypes` | → `StellarEraEconomy`（纯数据） |
| `MiningRig` `ProcessingBuilding` `ConveyorBelt` `PowerGrid` | → `StellarEraFactory`（表现） |
| `PlanetSurfaceProbe` | → `StellarEraTerrain`（这是地形查询，本来就该在底层） |

---

## 七、日常开发流程（防止再退化）

### 7.1 新功能落位决策树

写新东西时按顺序问：

1. 它是纯计算 / 纯数据，且不碰 UWorld？→ **L0**
2. 它要被两个以上模块用，且是接口或契约？→ **L1 Framework**
3. 它持有跨帧状态，且不渲染？→ **L2，做成 Subsystem**
4. 它要显示/被看见？→ **L3，做成 Actor/Component**
5. 它在编排玩法规则（谁解锁什么、什么条件触发什么）？→ **L4 Gameplay**
6. 它只是像素？→ **L5 UI，且只能读 VM**

### 7.2 每次提交前

```bash
python Tools/check_architecture.py
```

关注三个数字是否在变小：
- `跨模块 include ≥6` 的文件数（现在是 1）
- 业务模块里的测试文件数（现在是 30）
- `超大头文件` 数（现在是 8）

### 7.3 代码规模红线

| 对象 | 红线 | 超了怎么办 |
|---|---|---|
| 单个 `.h` | 300 行 | 拆内部结构体到独立头文件 |
| 单个 `.cpp` | 600 行 | 按职责拆多个文件，用 `Private/` 内的子目录分组 |
| 单个类的方法数 | 25 个 | 大概率有第二个职责没分离 |

---

## 八、不建议做的事

| 别做 | 原因 |
|---|---|
| 全项目套 MVC/MVP | UE 的 Actor/Component 已经是「数据+行为」的容器，再套一层 Controller 只是把 1449 行搬成 3 个 500 行的类，没有净收益 |
| 现在就上 Lyra 的 Experience + GameFeature | 单人开发、没有 DLC 需求，收益为零，成本是每个功能都要写 Asset |
| 全面引入 GAS | 工厂游戏的「能力」是建筑生产，不是角色技能；GAS 的复杂度在这里是负资产。等有战斗/角色成长再做 |
| 一次重构到底 | 3.6 万行、无环的代码库是资产，不是包袱。按 P0 → P1 → P2 走，每步可停 |

---

## 九、如果只做一件事

做 **P0-1：把 30 个测试文件拆进独立模块**。

理由：这是唯一一个「零逻辑风险、当天完成、立刻改善可读性」的动作，而且它会让业务模块从 36054 行降到 25000 行左右，之后每看一个文件都清爽。它不改任何行为，不需要回归测试。

做完之后再决定要不要继续走 P1。
