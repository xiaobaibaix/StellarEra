# 模块合并方案：11 → 7（已实施）

> 本文取代上一版 TREE_AFTER.md 的 17 模块方案。
> 依据是 2026-10-31 的实际文件清单与 Build.cs 依赖实测，不含推测。
>
> **状态：已完成**，分支 `refactor/module-merge`，worktree `F:/workspace/ue/StellarEra-refactor`。
> 实施结果与本文有两处偏差，已就地修正并标注（§3 Pawns 与 §5.5）。
>
> **⚠️ 目录布局后来又改过一次**：各模块不再分 `Public/` 与 `Private/`，
> 统一为「入口两件套与 `Build.cs` 并排 + 业务代码全在 `Src/` 内、`.h` 与 `.cpp` 同目录」。
> **以 `Source/README.md` 为准**，本文 §3 里的 `Public/` `Private/` 树是那一版的历史记录。

---

## 一、结论：你的三条判断，三条都成立

| 你的提议 | 判断 | 依据 |
|---|---|---|
| 测试留在各模块的 `Tests/` 目录 | **成立** | 30 个测试文件已全部带 `WITH_DEV_AUTOMATION_TESTS` 宏，Shipping 不打包。拆独立模块的唯一收益（不进发布包）已经通过宏达成了 |
| 地形 + 大气 + 矿物 合为一个模块 | **成立**，建议连 PlanetEdit 一起 | 编辑内核 `PlanetProgressiveEditKernel` 直接写体素密度场，和地形生成是同一份数据的两面。拆在两个模块里，反而要把内核提成 Public |
| 角色 + 飞船 + 星图相机 合为一个模块 | **成立** | 共同点是 `APawn` + 被 Controller possess。`StarMapCameraPawn` 现在寄生在 UI 模块里，是它导致 UI 必须公开依赖 Terrain |

我上一版把模块加到 17 个，是拿团队项目的划分标准套在单人项目上，方向错了。单人项目里模块化的收益（并行开发、二进制复用、流式加载）接近零，成本（Build.cs 维护、循环依赖排查、跨模块 include 的隐性约束、导航时要在 17 个目录里找）每天都要付。

---

## 二、最终结构：7 个模块

```
Source/
├── L0  底座
│   ├── StellarEraCore        2.6k   纯计算 + 通用契约（接口）
│   └── StellarEraGeology     3.2k   地质数据（零依赖，不动）
│
├── L1  世界
│   └── StellarEraPlanet     15.0k   ★ 地形 / 大气 / 矿物 / 编辑
│
├── L2  受控实体
│   └── StellarEraPawns       5.1k   ★ 角色 / 飞船 / 星图相机
│
├── L3  天体
│   └── StellarEraCosmos      3.7k   轨道模拟（不动）
│
├── L4  界面
│   └── StellarEraUI          2.7k   HUD / 星图面板 / 编辑 UI
│
└── L5  装配
    └── StellarEra            4.1k   GameMode / PlayerController
```

7 个模块，6 个层号（L0–L5）。原来是 11 个模块、8 个层号（L0–L7）。

**没有新增模块，只有 2 个合并出来的模块和 0 个空骨架。**

---

## 三、完整文件树

### StellarEraCore（+2 个接口，从别处搬入）

```
StellarEraCore/
├── StellarEraCore.Build.cs
├── Public/
│   ├── CosmosTypes.h
│   ├── NBodySimulator.h
│   └── Interfaces/                          ★ 新增目录
│       ├── StarMapUIInputs.h                ← 从 StellarEraUI/Public 搬
│       └── VoxelViewpointInvokers.h         ← 从 StellarEraTerrain/Public 搬（只留接口）
├── Private/
│   ├── NBodySimulator.cpp
│   └── Tests/
│       └── NBodySimulatorTests.cpp          ← 从 Private/ 搬
└── StellarEraCore.cpp / .h
```

Core 的定位从「纯计算」扩成「纯计算 + 通用契约」。这是不建 Framework 模块的代价，换来模块数少 1。

`VoxelViewpointInvokers.h` 拆成两半：接口留 Core，`VoxelViewpointInvokers::SetEnabled()` 这个需要 Voxel 类型的工具函数留在 Planet。这是删掉 UI 对 Terrain 公开依赖的关键动作。

### StellarEraPlanet（原 Terrain + Atmosphere + Minerals + PlanetEdit）

> 这一节已于 2026-10-31 按实际磁盘结构重写。原先的 `Public/` `Private/` 双树
> 是取消分层之前的记录，已删除 —— 保留只会误导。

```
StellarEraPlanet/
├── StellarEraPlanet.Build.cs
├── StellarEraPlanet.h / .cpp              模块入口（与 Build.cs 并排）
└── Src/
    ├── VoxelPlanetActor.h/.cpp         星球本体（模块门面，放 Src 根）
    ├── Atmosphere/
    │   ├── PlanetAtmosphereComponent.h/cpp
    │   ├── PlanetAtmosphereMaterialBuilder.cpp
    │   └── PlanetAtmosphereParams.h/cpp
    ├── Edit/
    │   ├── PlanetBrushApplicator.h/cpp
    │   ├── PlanetEditBrush.h/cpp
    │   ├── PlanetEditComponent.h/cpp
    │   ├── PlanetEditGridGeometry.h/cpp
    │   ├── PlanetProgressiveEdit.h/cpp
    │   └── PlanetProgressiveEditKernel.h/cpp
    ├── Invokers/
    │   ├── AlwaysLocalVoxelInvokerComponent.h/cpp
    │   ├── VoxelViewpointInvokers.cpp
    │   └── VoxelViewpointInvokerUtils.h
    ├── Minerals/
    │   ├── PlanetMineralMaterialBuilder.h/cpp
    │   ├── PlanetMineralSurface.h/cpp
    │   ├── PlanetMineralViewActor.h/cpp
    │   └── PlanetMineralViewComponent.h/cpp
    ├── Surface/
    │   ├── PlanetSurfaceGridComponent.h/cpp
    │   └── PlanetSurfaceRay.h/cpp
    ├── Terrain/
    │   ├── PlanetBiomeCubemap.h/cpp
    │   ├── VoxelPlanetGenerator.h/cpp
    │   ├── VoxelPlanetNoiseTerrain.h/cpp
    │   ├── VoxelPlanetSphereTerrain.h/cpp
    │   └── VoxelPlanetTerrainModule.h/cpp
    └── Tests/
        ├── PlanetAtmosphereParamsTests.cpp
        ├── PlanetBiomeCubemapTests.cpp
        ├── PlanetGeologyPodTests.cpp
        ├── PlanetGeologyPresetTests.cpp
        ├── PlanetLODTransitionTests.cpp
        ├── PlanetMineralSurfaceTests.cpp
        ├── PlanetMineralViewTests.cpp
        ├── PlanetProgressiveEditTests.cpp
        ├── PlanetSurfaceRayTests.cpp
        ├── PlanetTerrainEarlyOutTests.cpp
        ├── PlanetTerrainGradientNormalTests.cpp
        └── VoxelPlanetBiomeMaterialTests.cpp
```

**`Invokers/` 不在 `Terrain/` 里的理由**：那 4 个文件被 `StellarEraPawns`
（角色、飞船）和 `StellarEraUI`（星图相机）跨模块 include，它们是 Voxel 插件
LOD 的接线层，不是地形本身。判据看 include 关系，不看文件名前缀。

`VoxelPlanetTerrainModule.h` 名字里带 Module 容易和模块入口混淆，但它是 `UDataAsset`
基类，**不改名**——改文件名要动一堆 include，收益不抵成本。

### StellarEraPawns（原 Character + Spaceship）

```
StellarEraPawns/
├── StellarEraPawns.Build.cs
├── Public/
│   ├── Character/
│   │   ├── PlanetCharacter.h
│   │   ├── PlanetCharacterAnimInstance.h
│   │   └── PlanetCharacterMovementComponent.h
│   └── Spaceship/
│       └── SpaceshipPawn.h
├── Private/
│   ├── StellarEraPawns.cpp                             ★ 新模块入口
│   ├── Character/
│   │   ├── PlanetCharacter.cpp
│   │   ├── PlanetCharacterAnimInstance.cpp
│   │   └── PlanetCharacterMovementComponent.cpp
│   ├── Spaceship/
│   │   └── SpaceshipPawn.cpp
│   └── Tests/                                          7 个
│       ├── PlanetCharacterAnimTests.cpp
│       ├── SpaceshipAtmosphereDragTests.cpp
│       ├── SpaceshipCollisionTests.cpp
│       ├── SpaceshipFreeLookTests.cpp
│       ├── SpaceshipLODInvokerTests.cpp
│       ├── SpaceshipSpeedLimitTests.cpp
│       └── SpaceshipThrustDirectionTests.cpp
```

**`StarMapCameraPawn` 没能搬进来**（原计划搬入，实测会成环）：
它调用 `UCosmosSimulationSubsystem`，而 `StellarEraCosmos` 又有 4 处
`Cast<ASpaceshipPawn>` 依赖 Pawns。相机进 Pawns 就是
`Pawns → Cosmos → Pawns`。等 `ICosmosBody` 那笔债还清后再搬。

### StellarEraUI（4 个类 + 星图相机）

```
StellarEraUI/
├── StellarEraUI.Build.cs                               依赖从 8 个降到 5 个
├── Public/
│   ├── StellarEraHUD.h
│   ├── StarMapUISystem.h
│   ├── PlanetEditUI.h
│   ├── StarMapUIInputs.h                              IStarMapUIInputs（依赖倒置，留在这里）
│   └── StarMapCameraPawn.h                            暂留：搬去 Pawns 会成环，见 §3
├── Private/
│   ├── StellarEraHUD.cpp
│   ├── StarMapUISystem.cpp
│   ├── PlanetEditUI.cpp
│   ├── StarMapCameraPawn.cpp
│   └── Tests/                                          5 个
│       ├── StellarEraHUDTests.cpp
│       ├── StarMapUISystemTests.cpp
│       ├── MineralLegendTests.cpp
│       ├── StarMapCameraInvokerTests.cpp
│       └── PlanetEditTests.cpp                         ← 从 StellarEra/Private 搬
└── StellarEraUI.cpp / .h
```

### 不动的三个

```
StellarEraGeology/    18 文件  3.2k 行   零依赖，只把测试挪进 Private/Tests/
StellarEraCosmos/     10 文件  3.7k 行   测试进 Private/Tests/，收下 StarMapTests
StellarEra/            9 文件  4.1k 行   测试进 Private/Tests/，只留集成测试
```

---

## 四、依赖与层

### 合并后

```
StellarEraCore      ← 无
StellarEraGeology   ← 无
StellarEraPlanet    ← Core, Geology
StellarEraPawns     ← Core, Planet
StellarEraCosmos    ← Core, Planet, Pawns
StellarEraUI        ← Core, Planet, Pawns, Cosmos, Geology
StellarEra          ← 全部 + UI
```

### 变化

| 指标 | 现在 | 合并后 |
|---|---:|---:|
| 模块数 | 11 | **7** |
| 依赖最深层 | 8（L0–L7） | **6（L0–L5）** |
| `StellarEraUI` 依赖数 | 8 | **6** |
| `StellarEraUI` Public 依赖数 | 2（含被迫加的 Terrain） | **1**（只剩 Core） |
| 业务模块里的 `*Tests.cpp` | 分散 10 个 `Private/` | 各自 `Private/Tests/` |
| 改动要碰的 Build.cs | 11 个 | **7 个** |

### Build.cs 内容

```csharp
// StellarEraPlanet —— 原四模块依赖的并集，去掉互相引用
PublicDependencyModuleNames:  Core, CoreUObject, Engine, AssetRegistry,
                              Voxel, GeometryCore, GeometryFramework,
                              StellarEraCore, StellarEraGeology

// StellarEraPawns
PublicDependencyModuleNames:  Core, CoreUObject, Engine, Voxel,
                              StellarEraCore, StellarEraPlanet

// StellarEraUI
PublicDependencyModuleNames:  Core, CoreUObject, Engine, StellarEraCore
PrivateDependencyModuleNames: StellarEraPlanet, StellarEraPawns,
                              StellarEraCosmos, StellarEraGeology
```

`StellarEraUI` 的 `Public` 里那行 `"StellarEraTerrain"` 和它上方那段解释（「公开头 StarMapCameraPawn.h 公开继承 IVoxelViewpointInvokers，而 VoxelViewpointInvokers.h 在 StellarEraTerrain」）可以整段删掉——那个类搬走了，接口也搬到 Core 了。

---

## 五、迁移步骤

### 5.1 这是唯一需要动脑的地方：类路径变了

UE 的原生类路径是 `/Script/<模块名>.<类名>`。模块合并 = 所有相关类的路径全变，四个蓝图会断父类：

```
Content/Code/BP_VoxelPlanet.uasset          → VoxelPlanetActor
Content/Code/BP_PlanetCharacter.uasset      → PlanetCharacter
Content/Code/BP_SpaceshipPawn.uasset        → SpaceshipPawn
Content/Code/BP_StarMapCameraPawn.uasset    → StarMapCameraPawn
```

你的 `Config/DefaultEngine.ini` 里已经有一套 `[CoreRedirects]`，处理过上次拆模块时的同类问题。这次要做两件事：

**一、删掉这些条目**——新模块就叫 `StellarEraPlanet`，这些会变成自映射或反链：

```ini
; 删除（OldName 与新模块同名，留着会互相打架）
+ClassRedirects=(OldName="/Script/StellarEraPlanet.VoxelPlanetActor",...)
+ClassRedirects=(OldName="/Script/StellarEraPlanet.VoxelPlanetNoiseTerrain",...)
+ClassRedirects=(OldName="/Script/StellarEraPlanet.VoxelPlanetGenerator",...)
+ClassRedirects=(OldName="/Script/StellarEraPlanet.VoxelPlanetSphereTerrain",...)
+ClassRedirects=(OldName="/Script/StellarEraPlanet.VoxelPlanetTerrainModule",...)
+ClassRedirects=(OldName="/Script/StellarEraPlanet.PlanetSurfaceGridComponent",...)
+ClassRedirects=(OldName="/Script/StellarEraPlanet.AlwaysLocalVoxelInvokerComponent",...)
+ClassRedirects=(OldName="/Script/StellarEraPlanet.AlwaysLocalVoxelInvokerWithPredictionComponent",...)
+ClassRedirects=(OldName="/Script/StellarEraPlanet.PlanetMineralViewActor",...)
+ClassRedirects=(OldName="/Script/StellarEraPlanet.PlanetMineralViewComponent",...)
+ClassRedirects=(OldName="/Script/StellarEraPlanet.PlanetAtmosphereComponent",...)
```

**二、把所有 `NewName` 改成最终模块名**：

```ini
; ── 2026-10 模块合并：Terrain/Atmosphere/Minerals/PlanetEdit → StellarEraPlanet
;    Character/Spaceship/StarMapCameraPawn → StellarEraPawns
+ClassRedirects=(OldName="/Script/StellarEraTerrain.VoxelPlanetActor",NewName="/Script/StellarEraPlanet.VoxelPlanetActor")
+ClassRedirects=(OldName="/Script/StellarEraTerrain.VoxelPlanetNoiseTerrain",NewName="/Script/StellarEraPlanet.VoxelPlanetNoiseTerrain")
+ClassRedirects=(OldName="/Script/StellarEraTerrain.VoxelPlanetSphereTerrain",NewName="/Script/StellarEraPlanet.VoxelPlanetSphereTerrain")
+ClassRedirects=(OldName="/Script/StellarEraTerrain.VoxelPlanetTerrainModule",NewName="/Script/StellarEraPlanet.VoxelPlanetTerrainModule")
+ClassRedirects=(OldName="/Script/StellarEraTerrain.VoxelPlanetGenerator",NewName="/Script/StellarEraPlanet.VoxelPlanetGenerator")
+ClassRedirects=(OldName="/Script/StellarEraTerrain.PlanetSurfaceGridComponent",NewName="/Script/StellarEraPlanet.PlanetSurfaceGridComponent")
+ClassRedirects=(OldName="/Script/StellarEraTerrain.AlwaysLocalVoxelInvokerComponent",NewName="/Script/StellarEraPlanet.AlwaysLocalVoxelInvokerComponent")
+ClassRedirects=(OldName="/Script/StellarEraTerrain.AlwaysLocalVoxelInvokerWithPredictionComponent",NewName="/Script/StellarEraPlanet.AlwaysLocalVoxelInvokerWithPredictionComponent")
+ClassRedirects=(OldName="/Script/StellarEraAtmosphere.PlanetAtmosphereComponent",NewName="/Script/StellarEraPlanet.PlanetAtmosphereComponent")
+ClassRedirects=(OldName="/Script/StellarEraMinerals.PlanetMineralViewActor",NewName="/Script/StellarEraPlanet.PlanetMineralViewActor")
+ClassRedirects=(OldName="/Script/StellarEraMinerals.PlanetMineralViewComponent",NewName="/Script/StellarEraPlanet.PlanetMineralViewComponent")
+ClassRedirects=(OldName="/Script/StellarEraPlanetEdit.PlanetEditComponent",NewName="/Script/StellarEraPlanet.PlanetEditComponent")
+EnumRedirects=(OldName="/Script/StellarEraPlanetEdit.EPlanetBrushShape",NewName="/Script/StellarEraPlanet.EPlanetBrushShape")

+ClassRedirects=(OldName="/Script/StellarEraCharacter.PlanetCharacter",NewName="/Script/StellarEraPawns.PlanetCharacter")
+ClassRedirects=(OldName="/Script/StellarEraCharacter.PlanetCharacterAnimInstance",NewName="/Script/StellarEraPawns.PlanetCharacterAnimInstance")
+ClassRedirects=(OldName="/Script/StellarEraCharacter.PlanetCharacterMovementComponent",NewName="/Script/StellarEraPawns.PlanetCharacterMovementComponent")
+ClassRedirects=(OldName="/Script/StellarEraSpaceship.SpaceshipPawn",NewName="/Script/StellarEraPawns.SpaceshipPawn")
+ClassRedirects=(OldName="/Script/StellarEraUI.StarMapCameraPawn",NewName="/Script/StellarEraPawns.StarMapCameraPawn")
```

最老的 `/Script/StellarEra.*` 和 `/Script/StellarEraVehicle.*` 条目保留，只改 `NewName` 指到最终位置，不要留成两级链（A→B→C 的解析行为在 UE 里不保证）。

### 5.2 机械替换清单

| 动作 | 命令 |
|---|---|
| 建目录 | `mkdir -p Source/StellarEraPlanet/{Public,Private}/{Terrain,Atmosphere,Minerals,Edit,Tests}` |
| 移文件 | `git mv`，保留历史 |
| 改 API 宏 | `sed -i 's/STELLARERATERRAIN_API/STELLARERAPLANET_API/g'`，另三个宏同理 |
| 改模块入口 | 删 4 个 `StellarEra{Terrain,Atmosphere,Minerals,PlanetEdit}.{h,cpp}`，新建 `StellarEraPlanet.{h,cpp}` 里的 `IMPLEMENT_MODULE(FDefaultModuleImpl, StellarEraPlanet)` |
| 改 include | **不用改**（裸文件名继续有效）。但 **Build.cs 必须显式声明子目录**：`PublicIncludePaths.Add(Path.Combine(ModuleDirectory, "Public", "Terrain"))`，四个子目录各一行。UBT **只加 `Public/` 本身，不递归** —— 这一条与初稿写的相反，实测漏写直接 C1083 |
| 改 Build.cs | 7 个模块的依赖名重写 |
| 改 uproject | `Modules` 数组 11 项 → 7 项 |
| 加 redirect | 见 5.1 |

### 5.3 测试归位的硬规则

测试文件不是「看名字搬」，而是**看它 include 了谁**：测试能放的模块，必须是它所引用全部头文件的下游。

主模块 `StellarEra/Private/` 里那 3 个测试，实测 include 后归属如下：

| 测试 | 引用 | 归属 |
|---|---|---|
| `PlanetEditTests.cpp` 933 行 | `PlanetEditBrush.h` `PlanetEditGridGeometry.h`（Planet）+ `PlanetEditUI.h`（UI） | **UI/Tests**。只有 UI 同时看得见两边，放 Planet 会形成 `Planet → UI` 环 |
| `StarMapTests.cpp` 696 行 | `CosmosOrbitComponent.h` `CosmosSimulationSubsystem.h` `CosmosTrajectoryComponent.h`，另有 `PlanetCharacter.h` `SpaceshipPawn.h` `StarMapCameraPawn.h` `StellarEraPlayerController.h` | **留在主模块/Tests**。初稿以为它只引用 Cosmos，实测还拉控制器（主模块），放 Cosmos 会成环 |
| `VoxelViewpointInvokerTests.cpp` 316 行 | `PlanetCharacter.h` `SpaceshipPawn.h` `StarMapCameraPawn.h` `StellarEraPlayerController.h` | **留在主模块/Tests**。跨 Pawns + Controller 的集成测试，只有装配层看得见全部 |

这就是分界线：**单元测试跟着被测模块走，集成测试留在装配层**。

### 5.4 验证

1. `Tools/check_architecture.py --tree` → 层数应从 8 掉到 5
2. 重新生成工程文件 → 编译
3. 编辑器打开 `UniverseMap`，确认 4 个蓝图的父类没丢
4. 进 PIE，跑一遍：上船 / 下船 / 切星图 / 挖地形 / 开矿物视图

---

## 六、合并会变差的地方

说清楚代价，免得改完发现新的问题：

**1. Public 边界失效。** 合并后 Planet 的 20 个 Public 头对 Pawns / Cosmos / UI 全部可见，编译器不再帮你拦「UI 引用了 `PlanetProgressiveEditKernel` 这种内部实现」。缓解手段只有目录约定 + 体检脚本。`Private/` 仍然是真的私有，所以内部头放 Private 这条纪律要比现在更严格。

**2. 同模块内没有编译期防环。** 原来 `A → B → A` 会被 Build.cs 拒绝。合并后 A 和 B 在同一个模块里可以互相 include，环就藏起来了，只能靠脚本按子目录检查。

**3. 改 Public 头要重编整个 15k 行模块。** 但对比一下：现在改 `VoxelPlanetActor.h`，Terrain + 7 个依赖模块全部重编；合并后是 Planet + 4 个依赖模块。**总编译量反而略降。** 日常改 `Private/*.cpp` 只编单个文件，不受影响。这一条不是真代价。

**4. `Cosmos → Pawns` 依然存在。** `CosmosSimulationSubsystem.cpp` 里有 4 处 `Cast<ASpaceshipPawn>`，你代码注释里已经写明「只认 ASpaceshipPawn，现在只有一艘船」。合并后这条依赖从 `Cosmos → Spaceship` 变成 `Cosmos → Pawns`，性质没变。真正的解法是定义一个 `ICosmosBody` 接口（放 Core），让模拟层只认接口不认具体 Pawn。这属于另一件事，不混进这次合并。

---

## 七、做与不做

**这轮做**：合并模块 + 测试归位 + 两个接口搬 Core。全部是移动和改名，零逻辑改动。

**这轮不做**：`StellarEraPlayerController` 拆解（1449 行）、`AVoxelPlanetActor` 拆解（1470 行）、`CosmosTrajectoryComponent` 拆分、工厂玩法层。这些是内容重构，跟模块边界无关，单独开轮次。
