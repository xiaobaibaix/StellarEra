# StellarEra 框架改造：文件树 · 接口 · 通讯

> 配套 `ARCHITECTURE.md`（分层原则）。这份只讲**怎么落地**：目录怎么摆、接口长什么样、谁跟谁怎么说话。

---

## 0. 先修正一处

你的 UI 不是 UMG，是 **Canvas 立即模式**：

- `AStellarEraHUD : AHUD`，重写 `DrawHUD()`
- `UStarMapUISystem : UObject`，全是 `static` 绘制函数，**无状态**
- 数据靠 `IStarMapUIInputs` 每帧现问 Controller

所以 **MVVM 插件在这里用不上**，硬套只会给每帧绘制的代码加一层绑定开销。Canvas HUD 的正确模式是 **Pull（每帧拉一个 POD 结构）**，不是 Push。

MVVM 留给将来的 UMG 面板（工厂界面、物品栏）。

---

## 1. 三条通讯通道

先定规则，再写代码。所有跨模块交互必须归入其中一条，不许有第四条。

| 通道 | 方向 | 机制 | 用在 | 现有例子 |
|---|---|---|---|---|
| **Pull 查询** | UI → 领域 | `const` 方法返回 POD 结构 | 每帧要显示的数据 | `GetBodyVelocityGameUnits()` |
| **Command 命令** | 输入 → 领域 | 直接调 Subsystem 公开方法 | 玩家操作、状态切换 | `SetStarMapActive(true)` |
| **Event 事件** | 领域 → 多个监听者 | 消息总线（类型安全多播） | 一件事要多方知道 | 挖到矿物、登上飞船 |
| **Push 推送** | 领域 → UMG | FieldNotify / delegate | 将来的 UMG 面板 | 工厂库存变化 |

**选路规则**（照这个判断，不用讨论）：

1. 只是**显示**数据 → Pull。不要为显示做事件。
2. 玩家**按了键** → Command。Controller 只转发，不判断。
3. 一件事发生后**超过一个系统**要知道 → Event。
4. 只有**一个**消费者 → 直接调它的方法，别用事件。

第 4 条最容易被违反：一对一还发事件，是为了「解耦」而解耦，调试时找不到调用链。

---

## 2. 目标文件树

```
Source/
├── StellarEraCore/                  L0  纯计算，零项目依赖
│   ├── Public/
│   │   ├── CosmosTypes.h                 （已有）
│   │   ├── NBodySimulator.h              （已有）
│   │   └── StellarEraUnits.h         ★ 从 CosmosTypes 拆出：Au/Year/Cm 换算
│   └── Private/NBodySimulator.cpp
│
├── StellarEraGeology/               L0  纯数据（已有，不动）
│   ├── Public/
│   │   ├── PlanetGeologyField.h
│   │   ├── PlanetGeologyDistribution.h
│   │   ├── PlanetGeologyPreset.h
│   │   └── GeologyElements.h
│   └── Private/
│
├── StellarEraFramework/             L1  ★ 全新：跨层契约，零项目依赖
│   ├── Public/
│   │   ├── Interfaces/
│   │   │   ├── StarMapUIInputs.h          ← 从 StellarEraUI 搬入
│   │   │   ├── VoxelViewpointInvokers.h   ← 从 StellarEraTerrain 搬入（只留接口）
│   │   │   └── PlanetSurfaceQuery.h   ★ 新：地形射线的抽象
│   │   ├── Messages/
│   │   │   ├── StellarEraMessageBus.h     ★ 新：类型安全事件总线
│   │   │   └── StellarEraMessageTypes.h   ★ 新：所有事件的 delegate 声明
│   │   ├── ViewModels/
│   │   │   ├── HUDState.h                 ★ 新：FHUDState POD
│   │   │   └── StarMapState.h             ★ 新：FStarMapState POD
│   │   └── StellarEraFramework.h
│   └── Private/
│       ├── StellarEraMessageBus.cpp
│       └── StellarEraFramework.cpp
│
├── StellarEraTerrain/               L2  领域：生成/数据/射线（Actor 移出）
│   ├── Public/
│   │   ├── VoxelPlanetGenerator.h
│   │   ├── VoxelPlanetTerrainModule.h
│   │   ├── VoxelPlanetNoiseTerrain.h
│   │   ├── VoxelPlanetSphereTerrain.h
│   │   ├── PlanetSurfaceRay.h
│   │   ├── PlanetSurfaceGridComponent.h
│   │   ├── PlanetBiomeCubemap.h
│   │   ├── PlanetEditGridGeometry.h
│   │   ├── AlwaysLocalVoxelInvokerComponent.h
│   │   └── VoxelViewpointInvokers.h        → 只留 SetEnabled 工具函数
│   └── Private/
│
├── StellarEraCosmos/                L2  模拟
│   ├── Public/
│   │   ├── CosmosSimulationSubsystem.h    （已有，作为 Subsystem 样板）
│   │   └── CosmosOrbitComponent.h
│   └── Private/
│       └── CosmosTrajectoryComponent.cpp  → ⚠ 拆分见 §5
│
├── StellarEraEconomy/               L2  ★ 全新：玩法数据，无 Actor
│   ├── Public/
│   │   ├── EconomyTypes.h                 EOreType/EMaterialType/FItemStack
│   │   ├── RecipeAsset.h                  URecipeAsset : UPrimaryDataAsset
│   │   ├── OreDepositComponent.h          矿藏储量（纯数据组件）
│   │   ├── EconomySubsystem.h             全局库存/物流结算
│   │   └── EconomyQueries.h               只读查询接口（给 UI 和 Factory 用）
│   └── Private/
│
├── StellarEraPlanet/                L3  ★ 全新：星球表现（从 Terrain 拆出）
│   ├── Public/
│   │   ├── VoxelPlanetActor.h             ← 从 Terrain 搬入
│   │   └── PlanetSurfaceGridVisual.h
│   └── Private/VoxelPlanetActor.cpp
│
├── StellarEraAtmosphere/            L3  （已有，不动）
├── StellarEraMinerals/              L3  （已有，不动）
├── StellarEraPlanetEdit/            L3  （已有，不动）
├── StellarEraCharacter/             L3  （已有，不动）
├── StellarEraSpaceship/             L3  （已有，不动）
│
├── StellarEraFactory/               L3  ★ 全新：建筑表现
│   ├── Public/
│   │   ├── FactoryBuilding.h              建筑基类（挖机/冶炼/组装）
│   │   ├── ConveyorBelt.h                 传送带
│   │   └── PowerGridComponent.h
│   └── Private/
│
├── StellarEraGameplay/              L4  ★ 全新：规则编排
│   ├── Public/
│   │   ├── ProgressionSubsystem.h         进度、解锁
│   │   ├── BoardingSubsystem.h            ← 从 PlayerController 拆出
│   │   └── InteractionSubsystem.h         ← 从 PlayerController 拆出
│   └── Private/
│
├── StellarEraUI/                    L5  只读 POD，不 include 实现
│   ├── Public/
│   │   ├── StellarEraHUD.h
│   │   ├── StarMapUISystem.h
│   │   ├── StarMapCameraPawn.h
│   │   └── StarMapUIInputs.h              → 移到 Framework（留转发头会更好）
│   └── Private/
│
├── StellarEra/                      L4  装配
│   ├── Public/
│   │   ├── StellarEraGameMode.h
│   │   └── StellarEraPlayerController.h   ← 383 行 → 目标 <150
│   └── Private/
│
└── StellarEraTests/                 ★ 全新：30 个测试文件归位
    ├── Public/StellarEraTests.h
    └── Private/
        ├── Core/NBodySimulatorTests.cpp
        ├── Core/CosmosTypesTests.cpp
        ├── Terrain/PlanetSurfaceRayTests.cpp
        ├── Terrain/VoxelPlanetBiomeMaterialTests.cpp
        ├── Geology/PlanetGeologyDistributionTests.cpp
        ├── Cosmos/CosmosTrajectoryComponentTests.cpp
        ├── Spaceship/SpaceshipThrustDirectionTests.cpp
        ├── UI/StarMapUISystemTests.cpp
        └── ...（按原模块建子目录，共 30 个文件）
```

> `StellarEraTests` 的 `Build.cs` 里 `Type = "Developer"`，`bBuildDeveloperTools = true`，
> 不参与 Shipping 打包。

---

## 3. 关键接口

### 3.1 事件总线 —— `Framework/Public/Messages/StellarEraMessageBus.h`

用 **WorldSubsystem + 类型安全多播**，不做 `FName` + `void*` 的类型擦除。

理由：类型擦除版（像 Lyra 的 `GameplayMessageSubsystem`）需要 `GameplayTag` 资产、需要 `FInstancedStruct`，为单人项目引入两套插件不值得；而直接持有 delegate 成员，调试时能一眼看到谁订了、改一个事件只需改一处声明。

```cpp
// StellarEraMessageBus.h
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "StellarEraMessageTypes.h"
#include "StellarEraMessageBus.generated.h"

/**
 * 跨系统事件总线。每个 UWorld 一份，PIE 多开时互不串扰。
 *
 * 只放「一件事需要多个系统知道」的通知。
 * 一对一的通知直接调对方方法，不要走这里 —— 否则调用链会断在总线里。
 */
UCLASS()
class STELLARERAFRAMEWORK_API UStellarEraMessageBus : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	/** 便捷取用。WorldContext 传任意 UObject（Actor/Component/Widget 都行）。 */
	static UStellarEraMessageBus* Get(const UObject* WorldContext);

	// ---- 事件声明：加新事件就在这里加一行 ----

	/** 玩家挖掉了一块地形（行星, 位置, 挖掉的体素体积 cm³）。 */
	FOnTerrainDug OnTerrainDug;

	/** 飞船状态切换（新状态）。 */
	FOnVehicleChanged OnVehicleChanged;

	/** 星图开/关（是否开启）。 */
	FOnStarMapToggled OnStarMapToggled;

	/** 玩家库存变化（物品, 新数量）。经济系统内部用，UI 不听这个。 */
	FOnInventoryChanged OnInventoryChanged;

	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
};
```

配套 `StellarEraMessageTypes.h`：

```cpp
#pragma once

#include "CoreMinimal.h"
#include "StellarEraMessageTypes.generated.h"

class AActor;

DECLARE_MULTICAST_DELEGATE_ThreeParams(FOnTerrainDug, AActor* /*Planet*/, const FVector& /*WorldLocation*/, float /*VolumeCm3*/);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnVehicleChanged, AActor* /*NewVehicle*/);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnStarMapToggled, bool /*bActive*/);
DECLARE_MULTICAST_DELEGATE_TwoParams(FOnInventoryChanged, uint8 /*ItemId*/, int32 /*NewCount*/);
```

使用时：

```cpp
// 发布（挖掘组件里）
if (auto* Bus = UStellarEraMessageBus::Get(this))
{
    Bus->OnTerrainDug.Broadcast(GetOwner(), HitLocation, RemovedVolume);
}

// 订阅（矿物系统里）
Bus->OnTerrainDug.AddUObject(this, &UPlanetMineralViewComponent::HandleTerrainDug);
```

> **为什么不用 `AddLambda`**：`AddUObject` 在对象销毁时自动解绑，`AddLambda` 不会，
> 忘了 `RemoveAll` 就是悬空调用。除了一次性测试，一律 `AddUObject`。

### 3.2 接口搬家 —— `Framework/Public/Interfaces/VoxelViewpointInvokers.h`

原文件拆成两半，**接口进 Framework，工具函数留 Terrain**：

```cpp
// Framework/Public/Interfaces/VoxelViewpointInvokers.h
#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "VoxelViewpointInvokers.generated.h"

UINTERFACE(MinimalAPI, meta = (CannotImplementInterfaceInBlueprint))
class UVoxelViewpointInvokers : public UInterface
{
	GENERATED_BODY()
};

/**
 * 由「持有体素调用器的 Pawn」实现。契约见原注释（幂等、记住最后一次值）。
 *
 * 放在 Framework 而不是 Terrain：UI 的 AStarMapCameraPawn 实现了它，
 * 放在 Terrain 会逼 UI 把 Terrain 提成 Public 依赖（见 StellarEraUI.Build.cs 的注释）。
 * 本接口不引用任何 Voxel 类型，因此 Framework 不需要依赖 Voxel 插件。
 */
class STELLARERAFRAMEWORK_API IVoxelViewpointInvokers
{
	GENERATED_BODY()

public:
	virtual void SetViewpointInvokersActive(bool bActive) = 0;
};
```

```cpp
// Terrain/Public/VoxelViewpointInvokers.h   —— 只剩工具函数
#pragma once

#include "CoreMinimal.h"
// 这里才需要 Voxel 类型，所以留在 Terrain
class UVoxelInvokerComponentBase;

namespace VoxelViewpointInvokers
{
	/** 幂等地把调用器切到 bEnabled。原实现不变。 */
	STELLARERATERRAIN_API void SetEnabled(UVoxelInvokerComponentBase* Invoker, bool bEnabled);
}
```

**收益**：`StellarEraUI` 的 `PublicDependencyModuleNames` 里可以删掉 `StellarEraTerrain`。

### 3.3 HUD 数据 —— `Framework/Public/ViewModels/HUDState.h`

Canvas HUD 每帧拉的就是这个。纯 POD，不含指针以外的东西，方便堆栈上构造。

```cpp
#pragma once

#include "CoreMinimal.h"
#include "HUDState.generated.h"

/** 准星状态。 */
UENUM()
enum class EHUDCrosshair : uint8
{
	None,
	Interact,      // 可交互（登船、采集）
	Edit,          // 编辑模式
};

/**
 * 一帧的 HUD 快照。
 *
 * 为什么是 POD 而不是让 HUD 挨个问 Subsystem：
 *   - 一帧内多次问，各个系统可能在不同 Tick 阶段更新，会出现半新半旧的画面；
 *   - 一次拉齐，HUD 只依赖这一个结构体，不依赖任何 Subsystem 头。
 *
 * 由 UStellarEraHUDSubsystem::BuildHUDState() 每帧填。
 */
USTRUCT()
struct STELLARERAFRAMEWORK_API FHUDState
{
	GENERATED_BODY()

	/** 底部提示文本（"按 E 登船"）。空则不画。 */
	FString PromptText;

	/** 提示剩余显示时间（秒）。<= 0 表示不显示。 */
	float PromptRemaining = 0.f;

	/** 准星状态。 */
	EHUDCrosshair Crosshair = EHUDCrosshair::None;

	/** 当前速度（世界单位 厘米/秒），HUD 显示用。 */
	float SpeedCmPerSecond = 0.f;

	/** 是否在飞船里。 */
	bool bInVehicle = false;

	/** 星图是否开启。 */
	bool bStarMapActive = false;
};
```

### 3.4 Pull 查询接口 —— `Framework/Public/Interfaces/PlanetSurfaceQuery.h`

UI 目前直接 include `PlanetSurfaceRay.h` / `VoxelPlanetActor.h` 做地形采样。抽成接口后，UI 只认这一个。

```cpp
#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "PlanetSurfaceQuery.generated.h"

UINTERFACE(MinimalAPI, meta = (CannotImplementInterfaceInBlueprint))
class UPlanetSurfaceQuery : public UInterface
{
	GENERATED_BODY()
};

/**
 * 地表查询。由 AVoxelPlanetActor（StellarEraPlanet）实现。
 *
 * UI 只需要「这个方向的地表在哪、法线朝哪」，不需要知道体素八叉树怎么组织。
 * 抽出来后 UI 可以不依赖 StellarEraPlanet / VoxelFree。
 */
class STELLARERAFRAMEWORK_API IPlanetSurfaceQuery
{
	GENERATED_BODY()

public:
	/** 从球心沿 Direction 找地表交点。失败返回 false。 */
	virtual bool QuerySurfaceAlongDirection(const FVector& Direction, FVector& OutWorldPoint, FVector& OutNormal) const = 0;

	/** 当前朝向的地表半径（厘米）。 */
	virtual float GetSurfaceRadiusAlong(const FVector& Direction) const = 0;

	/** 星球中心的世界坐标。 */
	virtual FVector GetPlanetCenter() const = 0;
};
```

### 3.5 Subsystem 样板 —— 从 PlayerController 拆出来的形态

用 `UStarMapSubsystem` 做示范，其余五个照抄结构。

```cpp
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "StarMapState.h"
#include "StarMapSubsystem.generated.h"

class AActor;

/**
 * 星图模式的状态归属地。
 *
 * 状态（是否开启、选中哪颗、鼠标位置）从这里读，不要再放 PlayerController 上。
 * Controller 只负责「按下 Tab → 调 Toggle()」。
 */
UCLASS()
class STELLARERAGAMEPLAY_API UStarMapSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	//~ Command 通道：输入 → 这里
	void Toggle();
	void SetActive(bool bActive);

	//~ Pull 通道：UI 每帧读
	const FStarMapState& GetState() const { return State; }

	/** 屏幕点下的天体。原 Controller::PickBodyUnderScreenPosition 搬到这里。 */
	AActor* PickBodyUnderScreenPosition(const FVector2D& ScreenPosition) const;

	/** 天体 → 屏幕投影。原 Controller::ProjectBodyToScreen 搬到这里。 */
	bool ProjectBodyToScreen(AActor* Body, FVector2D& OutCenter, float& OutRadiusPx) const;

private:
	FStarMapState State;
};
```

`FStarMapState`（在 Framework 的 `ViewModels/StarMapState.h`）：

```cpp
USTRUCT()
struct STELLARERAFRAMEWORK_API FStarMapState
{
	GENERATED_BODY()

	bool bActive = false;
	TWeakObjectPtr<AActor> SelectedBody;
	FVector2D MousePositionPx = FVector2D::ZeroVector;
	bool bMouseValid = false;
	float SelectionPixelRadius = 20.f;
};
```

---

## 4. 改造后的依赖矩阵

每行只列**直接**依赖。空 = 零项目依赖。

| 模块 | 依赖 |
|---|---|
| StellarEraCore | — |
| StellarEraGeology | — |
| **StellarEraFramework** | — |
| StellarEraTerrain | Core, Framework, Geology |
| StellarEraCosmos | Core, Framework, Terrain |
| StellarEraEconomy | Core, Framework, Geology |
| StellarEraPlanet | Core, Framework, Terrain, Geology |
| StellarEraAtmosphere | Core, Framework, Terrain |
| StellarEraMinerals | Core, Framework, Terrain, Geology, Atmosphere |
| StellarEraPlanetEdit | Core, Framework, Terrain |
| StellarEraCharacter | Core, Framework, Terrain, PlanetEdit |
| StellarEraSpaceship | Core, Framework, Terrain, Atmosphere, Character |
| StellarEraFactory | Core, Framework, Economy, Planet |
| StellarEraGameplay | Core, Framework, Economy, Planet |
| StellarEraUI | Core, Framework |
| StellarEra | Framework, Gameplay, UI（其余按需） |

**关键变化**：

- `UI` 从 8 个依赖降到 **2 个**
- `Terrain` 不再被 UI 依赖，也不再含 Actor
- 没有任何模块依赖 `StellarEraUI`，也没有任何模块依赖 `StellarEra`（除装配）
- `Framework` 被 13 个模块依赖，但它不依赖任何人——**这就是它的价值**

---

## 5. 分批改造步骤

每批都能独立编译、能进 PIE 验证、能单独回滚。

### 第 1 批：只搬文件，不改一行逻辑

```bash
# 1. 建空模块（.uproject 加两行，两个 Build.cs）
Source/StellarEraFramework/     Type=Runtime
Source/StellarEraTests/         Type=Developer

# 2. 搬 30 个测试文件（git mv 保留历史）
git mv Source/StellarEraCore/Private/NBodySimulatorTests.cpp  Source/StellarEraTests/Private/Core/
git mv Source/StellarEraTerrain/Private/PlanetSurfaceRayTests.cpp Source/StellarEraTests/Private/Terrain/
... 逐个搬完

# 3. 两个接口搬家
git mv Source/StellarEraUI/Public/StarMapUIInputs.h           Source/StellarEraFramework/Public/Interfaces/
git mv Source/StellarEraTerrain/Public/VoxelViewpointInvokers.h Source/StellarEraFramework/Public/Interfaces/

# 4. 改各模块 Build.cs 的 include 路径
# 5. 从 StellarEraUI.Build.cs 删掉 "StellarEraTerrain"
```

验证：`python Tools/check_architecture.py` → 业务模块测试文件数归零，`超大头文件` 少一个。

### 第 2 批：加通讯机制（新增为主，不删旧代码）

1. 建 `UStellarEraMessageBus` + `StellarEraMessageTypes.h`
2. 先只接三个事件：`OnTerrainDug` / `OnVehicleChanged` / `OnStarMapToggled`
3. **旧代码路径不动**，新事件和旧调用并存，跑通了再删旧的

验证：在 `OnTerrainDug` 里打个日志，挖一块地形看有没有输出。

### 第 3 批：拆 PlayerController（一个一个来）

顺序：`UStarMapSubsystem` → `UInteractionSubsystem` → `UBoardingSubsystem` → `UMineralViewSubsystem` → `UPlanetEditModeSubsystem`。

每个的拆法固定三步：

1. 把状态字段从 Controller 头文件**剪**到新 Subsystem
2. 把处理方法从 `StellarEraPlayerController.cpp` **剪**到新 Subsystem 的 cpp
3. Controller 里留一个转发：`GetSubsystem<UStarMapSubsystem>()->Toggle();`

每拆一个，重跑体检脚本看 `StellarEraPlayerController.cpp` 的跨模块 include 数：
**7 → 5 → 4 → 3 → 2** 就是进度条。

### 第 4 批：拆 Terrain 与 Cosmos 的表现层

- `AVoxelPlanetActor`（1470 行）→ `StellarEraPlanet`
- `UCosmosTrajectoryComponent`（1373 行）→ 拆成「轨迹预测（留 Cosmos）」+「轨迹绘制（进 UI 或 Planet）」

### 第 5 批：玩法层

`StellarEraEconomy` 先做纯数据（物品/配方/矿藏），能被单元测试覆盖之后再上 `StellarEraFactory` 的 Actor。

---

## 6. 一张图记住

```
输入 ──Command──▶ Subsystem ──Pull(POD)──▶ HUD/Widget
                       │
                       └──Event──▶ 其他 Subsystem ──Push──▶ UMG 面板
```

**输入不直接碰 UI，UI 不直接碰实现。** 中间那层 Subsystem 是唯一的状态持有者。
