# 工厂系统使用说明

## 模块结构

```
VoxelFactory/
├── Public/
│   ├── FactoryTypes.h        EOreType / FPlanetAnchor（已有）
│   ├── FactoryRecipe.h       EItemType / FFactoryRecipe / URecipeLibrary ★
│   ├── PlanetSurfaceProbe.h  表面探测（已有）
│   ├── MiningRig.h           挖掘机（已有）
│   ├── ProcessingBuilding.h  通用加工机 ★
│   └── ConveyorBelt.h        传送带 ★
└── Private/  对应 6 个 .cpp
```

依赖：`VoxelPlanet` + `VoxelCosmos`

---

## 矿脉系统

### 浓度 = 星球丰富度 × 深度剖面 × 空间噪声

```cpp
Richness = GlobalAbundance × DepthFactor × Spatial;
if (Richness < Threshold) Richness = 0;   // 阈值以下是空白
```

**① 深度剖面**（高斯分布）

```
PeakDepth 处浓度最高，向上向下按 LayerThickness 衰减。
铁在浅层、钛在中层、铀在深层 —— 挖穿表层就没铁了。
```

**② 空间噪声** —— 让矿脉成块而不是均匀撒

```cpp
// 在「单位球面 + 深度」上采样 3D 噪声
SamplePoint = (N.X, N.Y, N.Z + Depth/LayerThickness × 0.3);
Spatial = Lerp(1.0, Noise01, SpatialVariance);
```

`SpatialVariance = 0` 完全均匀，`= 1` 完全由噪声决定。

**③ 阈值** —— 这是「矿脉」而非「整球都有一点」的关键

低于阈值的区域完全没有矿。玩家必须**找矿**。

### 开采难度与产量

```cpp
GetDigDifficulty = Hardness × (1 + Depth×0.0008 + Depth²×0.0000035)
GetYield = BaseYield × (0.5 + Richness×0.5)
```

深度系数增长很快，**挖深层本身就是目标**。

### 7 种星球预设

```cpp
FPlanetPresets::Apply(EPlanetType::Volcanic, Seed, Radius,
    Continents, Mountains, Detail, Ravine, Sea, Land, OreConfig);
```

| 预设 | 地形 | 矿脉 |
|---|---|---|
| Rocky 岩石 | 温和，有海 | 铁铜硅均衡 |
| Metallic 金属 | 平坦，山脊尖锐 | 铁 0.95 钛 0.60，缺水 |
| Frozen 冰冻 | 平坦，白 | **水 0.95**，金属稀少 |
| Volcanic 火山 | 剧烈，有峡谷 | 钛 0.55 **铀 0.40** |
| Silica 硅地 | 荒原 | **硅 0.90** |
| Barren 贫瘠 | 几乎全平 | 只有水和一点点铁 |
| Rich 富饶 | 中等 | 全都多，适合测试 |

**预设同时配地形和矿脉** —— 分开配会出现「火山星球但矿脉是水」的矛盾。

### 挖掘机的自动选矿

```cpp
Rig->bAutoSelectOre = true;   // 默认开
```

每次探测时自动挑当前点最富的矿，**一台挖掘机不会在富矿星球上闲置**。
关掉则固定挖 `PrimaryOre`。

### 单独用矿脉系统

```cpp
FOreSystem Sampler;
Sampler.Init(OreConfig);

float Richness;
Sampler.SampleRichness(FVector(500, 300, 800), EOreType::Uranium, Richness);

EOreType Best;
float BestRichness;
Sampler.GetDominantOre(FVector(500, 300, 800), Best, BestRichness);
```

---

## 三个核心类

| 类 | 作用 | 关键点 |
|---|---|---|
| `AMiningRig` | 挖体素产矿 | 已实现。`TakeOre()` 供传送带取货 |
| `AProcessingBuilding` | **通用加工机** | 冶炼/零件/组装全靠配方区分 |
| `AConveyorBelt` | 传送带 | 自动连接，首尾相接 |
| `APowerPlant` | 发电站 | 供电范围内设备共享功率 |

---

## 最小的闭环：挖 → 炼

### 1. 放一个挖掘机

```
关卡里拖 AMiningRig
Settings：
  PrimaryOre = Iron
  PlanetOreAbundance = 0.5     丰富度，影响产量
  Config.DigsPerSecond = 2
```

### 2. 运行时锚定到星球

```cpp
Rig->AnchorToPlanet(BodyActor, VoxelPlanetActor, Direction);
Rig->SetWorking(true);
```

锚定后它会贴在星球地表，`GetProducedAmount()` 可以读库存。

### 3. 放一个加工机并设置配方

```
关卡里拖 AProcessingBuilding
Settings → Production → Recipe：
  Id       = SmeltIron
  DisplayName = 铁锭
  Inputs   = [IronOre ×1, Water ×2]
  Outputs  = [IronIngot ×1]
  CraftTime = 2.0
  PowerDraw = 10.0
  Category = Smelter
```

### 4. 放传送带连起来

```
拖 AConveyorBelt 放在挖掘机和加工机之间
```

**传送带会自动连接**——`AutoConnect()` 每帧扫描周围：

- 上游只认**生产设备或矿机**（它们是货物来源）
- 下游只认**没有上游的传送带**

所以摆成一排就能串成通路，不需要手动连线。

### 5. 跑起来

加工机会自动循环：

```
挖机产出铁矿石 → 传送带 → 加工机输入槽 → 变成铁锭 → 输出槽 → 下一条带子
```

---

## 三种物流接口必须一致

这是扩展性的关键。加卡车/飞机时，**加工机和矿机一行代码都不用改**：

```cpp
// 上游
int32 TakeOre(EItemType Item, int32 Count);              // AMiningRig
int32 TakeOutput(EItemType Item, int32 Count);           // AProcessingBuilding

// 下游
int32 TryAddInput(EItemType Item, int32 Count);           // AProcessingBuilding
int32 Receive(EItemType Item, int32 Count);              // AConveyorBelt
```

---

## 加工机的状态反馈

指示灯颜色：

| 颜色 | 含义 |
|---|---|
| 绿 | 正在加工 |
| 黄 | 缺料或输出槽满 |
| 灰 | 空闲 |

进度条在机器上方（压扁的球体，用缩放表现进度）。

---

## 电力系统

### 发电站（APowerPlant）

```
关卡里拖 APowerPlant
Settings → Power：
  MaxOutput = 100          额定输出
  SupplyRadius = 3000      供电半径（厘米）
  bOnline = true           开机
  bRequiresFuel = false    需要燃料吗
```

**自动为供电半径内的所有 `AProcessingBuilding` 供电**，不需要玩家铺电线。

### 分配逻辑：统一比例，不用优先级

```cpp
Satisfaction = Clamp(ActualOutput / TotalDemand, 0, 1);
// 所有设备共享这个满足率
Building->PowerSatisfaction = Satisfaction;
```

**刻意不做优先级**——优先级会让玩家要算电力配额，多一套 UI 和决策成本。统一比例更直观：**电不够就大家一起变慢。**

**电力不足时是变慢不是停机**，玩家能看见信号：

```cpp
CraftProgress += DeltaSeconds * PowerSatisfaction;
```

### 没接电会怎样

默认 `StandalonePowerRatio = 0`，所以**忘接电的机器完全不动**。

这是刻意的——明显的错误提示比默默满速运行骗玩家好。
调试时可以直接把 `StandalonePowerRatio` 调成 `1.0` 跳过电力系统。

### 需要燃料的电厂

`bRequiresFuel = true` + `FuelItem = UraniumOre`：

- 燃料耗尽 → **完全停机**（不是降额）
- 每秒消耗 `FuelPerSecond`
- 燃料要靠传送带送进去：`Plant->FuelBuffer.Add(UraniumOre, 100)`

### 视觉反馈

| 颜色 | 含义 |
|---|---|
| 白 | 正常 |
| 红 | **超载**（需求 > 输出） |
| 灰 | 离线 |

查询接口：

```cpp
Plant->GetActualOutput();      // 实际输出
Plant->GetConnectedCount();    // 接入设备数
Plant->GetLoadFactor();        // 负载率，> 1 就是超载
```

---

## 组装产物会真的生成 Actor

配方里勾选 `bProduces Actor` 并指定 `Produced Actor` 类，
完成后会：

```
1. 在加工机旁边生成那个 Actor
2. 打日志确认
```

**当前放置位置是「加工机旁边 +200cm」**，等建造交互做好后改成玩家指定位置。

`FPlanetAnchor` 的设置留了 TODO——等星球锚定系统接过来，
新建筑也要锚定到星球表面而不是悬在加工机旁边。

---

## 验证配方表（不依赖 UE）

```bash
clang++ -std=c++17 Tests/FactoryRecipeTest.cpp -o /tmp/test && /tmp/test
```

四组测试：默认配方合法 / 循环依赖检出 / 设计意图验证 / 成品溯源。

改配方表后跑一下，能在进 UE 之前发现问题。

---

## 调试建议

开这些日志：

```cpp
// 看加工状态
UE_LOG(LogTemp, Log, TEXT("[Factory] %s 加工中 %.0f%%"),
    *GetName(), GetProgress() * 100.f);

// 看传送带货物流
UE_LOG(LogTemp, Verbose, TEXT("[Belt] %s 载货 %d/%d"),
    *GetName(), GetTotalCargo(), Capacity);
```

用 `LogTemp` 的 Verbose 级别看挖矿细节：

```
LogTemp Verbose
```

---

## 完成状态

| 步骤 | 状态 |
|---|---|
| ① 物品与配方数据层 | ✅ |
| ② AProcessingBuilding | ✅ |
| ③ AConveyorBelt | ✅ |
| ④ 电力系统 | ✅ |
| ⑤ 组装落地 | ⚠️ 能生成 Actor，但放置位置与锚定待做 |
| ⑥ 卡车 / 轨道船 | ❌ 可选 |

**额外完成：**
- ✅ 矿脉系统（深度分层 + 空间噪声 + 阈值）
- ✅ 7 种星球预设（同时配地形与矿脉）

**剩下最大的缺口是建造交互** —— 玩家目前只能代码调用，
没有放置预览、合法性检测、UI。
