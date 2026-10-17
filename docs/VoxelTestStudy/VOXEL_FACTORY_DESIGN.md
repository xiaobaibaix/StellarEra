# 体素工厂系统 —— 完整设计方案

版本：1.0
定位：最小可玩的五层工厂链条（采集 → 冶炼 → 零件 → 组装 → 物流）

---

## 一、设计目标

做一个**能跑通闭环**的工厂系统，玩家能在体素星球上：
挖矿 → 冶炼 → 造零件 → 组装行星发动机 → 用动力把它送上轨道。

**约束**：资源种类不超过 12 种，建造类不超过 4 个核心类。

### 为什么资源要少

玩家能记住的物品数量大约在 12±3。超过 15 种就会开始查阅 wiki，
而查阅 wiki 意味着游戏设计失败。DSP 的物品超过 50 种，但它用
「配方树 + 物流视图 + 大量 UI」来承载，复杂度是它的卖点也是它的门槛。

我们的定位是「精而不繁」，所以：

- **不做锻造**（后期再考虑）
- **不做流体**（第一版）
- **不做科技树的完整分级**（只区分「基础零件」与「设备」两个档次）

---

## 二、物品表（12 种）

分三类：**原料**（挖出来的）、**材料**（冶炼的）、**零件与成品**。

### 原料（6 种）—— 由挖掘机直接产出

| 枚举值 | 中文 | 来源 | 说明 |
|---|---|---|---|
| `EOreType::Iron` | 铁矿石 | 挖机 | 最常见 |
| `EOreType::Copper` | 铜矿石 | 挖机 | 导电 |
| `EOreType::Silicon` | 硅矿石 | 挖机 | 半导体 |
| `EOreType::Titanium` | 钛矿石 | 挖机 | 稀有，推进剂原料 |
| `EOreType::Uranium` | 铀矿石 | 挖机 | 最稀有，核能 |
| `EOreType::Water` | 水 | 挖机 | 冶炼与推进剂都要 |

### 材料（3 种）—— 冶炼产出

| 枚举值 | 中文 |
|---|---|
| `EMaterialType::IronIngot` | 铁锭 |
| `EMaterialType::CopperIngot` | 铜锭 |
| `EMaterialType::SiliconIngot` | 硅锭 |

> 钛和铀**不做锭**，直接用矿石。理由：减少物品数量，且钛锭的
> 冶炼流程在平衡上没带来额外乐趣。

### 零件与成品（4 种）

| 枚举值 | 中文 | 定位 |
|---|---|---|
| `EComponentType::Gear` | 齿轮 | 基础零件，机械通用品 |
| `EComponentType::Circuit` | 电路板 | 基础零件，电子通用品 |
| `EComponentType::Beam` | 结构梁 | 基础零件，承重通用品 |
| `EComponentType::Propellant` | 推进剂 | 消耗品，燃料 |

### 最终成品（不计物品数，是「建造物」）

行星发动机、发电机、轨道运输船——这些是**建造目标**，
不是可堆叠物品，而是「建造完成后变成一个可放置的 Actor」。

---

## 三、配方表

这是整个系统的核心数据结构。**所有生产行为都由配方驱动，没有第二套逻辑。**

### 3.1 冶炼（一进一出）

```
铁矿石 + 水×2  →  铁锭
铜矿石 + 水×2  →  铜锭
硅矿石 + 水×2  →  硅锭
```

> 水的作用是「消耗品」，给冶炼一个持续性成本。
> 初期也可以设成 0，让玩家专注主线。

### 3.2 零件（一对多，复杂度关键层）

```
铁锭×2                →  齿轮×1
硅锭×1 + 铜锭×1       →  电路板×1
铁锭×3 + 铜锭×1       →  结构梁×1
水×3 + 钛矿石×1       →  推进剂×1
```

### 3.3 组装（玩家目标）

```
行星发动机 = 齿轮×8 + 电路板×4 + 结构梁×6 + 推进剂×12
发电机     = 齿轮×4 + 电路板×2 + 铜锭×6
轨道运输船 = 结构梁×10 + 推进剂×8 + 电路板×2
```

### 3.4 配方设计的三个意图

**① 行星发动机需要全部 4 种零件**
强迫玩家把四条产线都建起来，不能只专精一条。

**② 结构梁需求量最大（6 个）**
结构梁消耗铁锭最多（3 个），这在数学上自然导向「多建冶炼炉」，
而不需要额外的机制引导。

**③ 推进剂直接吃钛矿石**
钛是稀有种（丰富度低），这迫使玩家去探矿，
而不是在自家门口无限刷。

---

## 四、类设计

### 4.1 类总览（4 个核心类）

```
AMiningRig           挖掘机          ✅ 已实现
AProcessingBuilding  加工机（通用）  ★ 本方案的核心
ALogisticsBuilding   物流节点         传送带 / 管道
APowerGrid           电力网络        供电与分配
```

### 4.2 为什么需要一个通用的加工机类

冶炼炉、零件厂、组装厂**共用 `AProcessingBuilding`**，行为差异完全由配方决定。

```cpp
class AProcessingBuilding : public AActor
{
    // 配方资产决定它是谁
    UPROPERTY(EditAnywhere) TObjectPtr<UFactoryRecipe> Recipe;

    // 三个槽：进料 ×2、输出 ×1
    UPROPERTY() TMap<EItemType, int32> InputBuffer;
    UPROPERTY() TMap<EItemType, int32> OutputBuffer;

    // 通电状态
    UPROPERTY() bool bPowered = false;
};
```

这样做的收益：

| 收益 | 说明 |
|---|---|
| 代码量 | 一个类覆盖全部 11 种配方 |
| 存档 | 只存「配方 ID + 槽内容」，不需要为每个建筑写序列化 |
| 扩展 | 加新配方 = 加数据，不改代码 |
| 平衡 | 改配方数值不动任何代码 |

代价是「玩家看不出这是冶炼炉还是组装厂」——**但可以通过网格贴图解决**，
或者用一个小的 `EBuildingCategory` 枚举决定外观。

### 4.3 统一物品类型

原料、材料、零件需要放在同一个槽里，所以需要一个统一的物品枚举：

```cpp
UENUM(BlueprintType)
enum class EItemType : uint8
{
    None = 0,

    // 原料
    IronOre, CopperOre, SiliconOre, TitaniumOre, UraniumOre, Water,

    // 材料
    IronIngot, CopperIngot, SiliconIngot,

    // 零件
    Gear, Circuit, Beam, Propellant,

    // 成品（作为建造目标的标志）
    PlanetaryEngine, Generator, OrbitalTransport,

    Count
};
```

`EOreType` 保留用于「矿石丰富度分布」，`EItemType` 用于实际槽位。
两者通过 `EOreTypeToItem()` 转换。

---

## 五、物流设计

### 5.1 第一版只做传送带

卡车、飞机的复杂度在**寻路与调度**，而这些在链没跑通前不值得投入。

第一版只有一种物流：

```
ALogisticsBuilding
  · 有输入端与输出端
  · 连接关系用「端口对端口」表示
  · 物品沿连接方向流动
```

### 5.2 三种物流模式（预留，不第一版实现）

| 模式 | 特点 | 适用距离 |
|---|---|---|
| 传送带 | 固定路径，容量无限 | 短距离、产线内 |
| 管道 | 只走流体 | 短距离、高吞吐 |
| 卡车/轨道船 | 需要寻路，可跨地形 | 长距离、跨星球 |

**关键：三者接口必须一致。**

```cpp
// 物流设备的统一接口
virtual void RequestPickup(FItemStack& OutStack) = 0;
virtual void Deliver(const FItemStack& Stack) = 0;
```

这样上层「加工机」完全不关心货物怎么来的。加卡车只是多一个
`ALogisticsVehicle`，加工机一行代码都不用改。

### 5.3 传送带在球面上的额外考虑

我们的星球是**球形**，这带来两个问题：

**① 方向问题**：传送带必须知道自己朝哪边「上」，否则会插进地里。
用 `GetUpVector()`（已有接口）。

**② 距离问题**：同样直线距离，跨赤道比高纬度长。
可以做成设计特性（物流成本随纬度变化），也可以忽略。

第一版建议**忽略球面特殊性**，直接用直线距离。

---

## 六、能源系统

### 6.1 为什么必须有

没有能源，工厂游戏就是「堆数字」——放 100 个冶炼炉和放 10 个没有区别。
能源系统的价值是**制造取舍**。

### 6.2 最小实现

```cpp
class APowerGrid
{
    float TotalSupply = 0;    // 所有发电机供给之和
    float TotalDemand = 0;    // 所有设备需求之和

    void Distribute();  // 按比例分配
};

class AProcessingBuilding
{
    float PowerSatisfaction = 1.0f;  // 0~1

    void Tick()
    {
        // 供电不足时降速
        const float Rate = PowerSatisfaction;
        Progress += Recipe.CraftTime * Rate * DeltaTime;
    }
};
```

### 6.3 建筑耗能

| 建筑 | 耗能 |
|---|---|
| 挖掘机 | 中（挖得越快越费电） |
| 加工机 | 按配方设定（冶炼低、组装高） |
| 传送带 | 极低（但数量多，总量大） |
| 发电机 | 供给 |

### 6.4 取舍如何产生

- **集中布局**：短距离传送带少，电力线短，但地形受限
- **分散布局**：地形自由，但电力线长、传送带多

**只靠「供电范围有限」就能产生这个取舍**，不需要复杂的电网拓扑。

---

## 七、存档设计

### 7.1 三类数据

| 数据 | 存储方式 | 备注 |
|---|---|---|
| 建筑位置与类型 | Actor 本身 | UE 自动序列化 |
| 槽位物品 | `AProcessingBuilding` 的 UPROPERTY | 随 Actor 序列化 |
| 矿石丰富度变化 | `AVoxelPlanetActor` 的存档对象 | **体素世界自带的 Save Object** |

### 7.2 锚点的持久化

这是最容易出错的地方。

**不能存世界坐标** —— 星球会自转公转，世界坐标不断变化。
必须存 `FPlanetAnchor`（已有）：

```cpp
struct FPlanetAnchor
{
    TWeakObjectPtr<ACosmosBodyActor> Planet;  // 哪颗星球
    FVector LocalDirection;                  // 单位球面方向
    double LocalHeight;                       // 相对地表高度
    FRotator LocalRotation;
};
```

**读档时的正确流程**：
1. 找到 `Planet` 指向的星球 Actor
2. 用 `LocalDirection + LocalHeight` 重新算出世界变换
3. `SetActorTransform()`
4. 如果星球 Actor 不存在（存档丢失），则标记为「孤儿建筑」等待回收

---

## 八、实现顺序

严格按这个顺序，每步都能独立验证。

### 第 1 步：物品与配方数据层（不涉及任何 Actor）

**产出**：
```cpp
enum class EItemType : uint8 { ... };
USTRUCT() struct FRecipeInput { EItemType Item; int32 Count; };
USTRUCT() struct FFactoryRecipe {
    FName Id;
    FText DisplayName;
    TArray<FRecipeInput> Inputs;
    TArray<FRecipeInput> Outputs;
    float CraftTime = 1.0f;
    float PowerDraw = 5.0f;
    EBuildingCategory Category;
};
class URecipeLibrary : public UDataAsset { TArray<FFactoryRecipe> Recipes; };
```

**为什么先做这个**：配方表是整个系统的骨架。
先把它定下来，后面的类都是围绕它设计的。
而且它可以完全脱离 UE 测试（纯数据结构）。

**验收标准**：能用代码遍历所有配方，检查有没有循环依赖。

### 第 2 步：`AProcessingBuilding`

实现输入槽、输出槽、进度条、通电判定。

**验收标准**：给一个矿机和一台冶炼炉，中间连一条带子，矿能自动变成锭。

### 第 3 步：`ALogisticsBuilding`（传送带）

**验收标准**：完整的「挖 → 炼 → 造齿轮」链路跑通。

### 第 4 步：能源系统

**验收标准**：切断供电，设备降速；建发电机后恢复。

### 第 5 步：组装与建造

行星发动机的建造流程。

**验收标准**：能造出一台发动机并放到场景里。

### 第 6 步（可选）：卡车 / 轨道船

---

## 九、需要立刻做的决策

以下几件事会影响后续所有代码，建议先定。

### 决策 1：槽位容量

**建议**：每种建筑固定 `N` 个槽位，不做 UI 自定义。
理由：可自定义槽位需要一套拖拽 UI，成本高且对玩法无实质增益。

- 冶炼炉：1 进 1 出
- 零件厂：2 进 1 出
- 组装机：4 进 1 出

### 决策 2：配方是否允许中间产物

**建议**：允许，但要设「中间产物可堆叠上限」防止无限囤积。
否则玩家会把冶炼炉的输出槽填满然后停产。

### 决策 3：要不要做流体

**建议**：第一版不做。推进剂用水做就够了，流体的管网逻辑
（压力、分流、泄漏）会让整个系统复杂度翻倍。

### 决策 4：玩家如何放置建筑

这是个未解决的大问题。当前只有 `AnchorToPlanet()` 代码调用，
没有编辑器交互。

**三个选项**：

| 方案 | 做法 | 成本 |
|---|---|---|
| A. 蓝图工具 Actor | 拖一个「建造预览」Actor，跟随鼠标，点击落地 | 中 |
| B. 建筑本身就是可拖 Actor | 从调色板拖建筑到场景，自动吸附地表 | 低，但限制多 |
| C. 菜单 + 幽灵预览 | 按快捷键打开建造菜单，选类型后放置 | 高，但最像正式游戏 |

**建议先做 B**，验证玩法后再考虑 C。

---

## 十、与现有代码的接口

### 10.1 已完成

| 类 | 文件 | 作用 |
|---|---|---|
| `AMiningRig` | `MiningRig.h/cpp` | 挖体素产矿 |
| `FPlanetSurfaceProbe` | `PlanetSurfaceProbe.h/cpp` | 表面探测 + 矿脉分布 |
| `FPlanetAnchor` | `FactoryTypes.h` | 星球局部坐标锚点 |

### 10.2 待实现

| 类 | 依赖 | 说明 |
|---|---|---|
| `EItemType` | — | 统一物品枚举 |
| `FFactoryRecipe` | — | 配方结构 |
| `URecipeLibrary` | — | 配方数据资产 |
| `AProcessingBuilding` | 配方 + 能源 | ★ 核心 |
| `ALogisticsBuilding` | 物品 | 传送带 |
| `APowerGrid` | — | 电网 |

### 10.3 依赖链

```
VoxelFactory
  ├── VoxelPlanet   （AVoxelPlanetActor, AVoxelWorld）
  └── VoxelCosmos   （ACosmosBodyActor，用于锚定）
```

---

## 十一、已知的开放问题

1. **球形工厂的传送带如何铺设？**
   平面传送带可以直接沿地面贴，球形需要沿径向吸附。
   第一版建议用「直线 + 端点吸附地表」的简化做法。

2. **玩家降落后体素世界才加载，那建造是否受限于此？**
   建议：只有玩家在星球附近时才允许建造。远处的星球不能建。

3. **废弃建筑怎么回收？**
   锚点机制天然支持——只要找到星球引用就能定位。
   但需要一套「已探索星球」的持久化列表。

4. **多玩家时体素编辑冲突**
   当前是单机设计，不考虑。
