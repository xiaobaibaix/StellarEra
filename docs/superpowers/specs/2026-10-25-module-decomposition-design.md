# StellarEra 模块化重构设计（11 模块）

- 日期：2026-10-25
- 状态：待评审
- 关联：`docs/superpowers/specs/2026-10-24-planet-geology-design.md`、`docs/superpowers/specs/2026-10-24-mineral-cutaway-editor-design.md`

## 1. 背景

当前 4 个模块按「层」划分，不按领域划分：

| 现模块 | 文件数 | 实际包含的领域 |
|---|---:|---|
| `StellarEraCore` | 6 | 单位 + N 体求解器（较纯） |
| `StellarEraPlanet` | 42 | 地形/体素行星、大气、地质数据、矿物观察、编辑网格、Voxel 插件修补 |
| `StellarEraVehicle` | 23 | 角色+移动+动画、飞船、行星编辑组件/笔刷 |
| `StellarEra` | 29 | N 体编排、轨迹可视化、星图相机、星图 UI、HUD、GameMode、PlayerController |

典型症状：

- `AStellarEraPlayerController` 的 .cpp 有 1310 行，同时负责输入资产构造、Pawn 分派、登船/离船、星图模式、编辑模式、矿物透视、HUD 提示。
- `UCosmosTrajectoryComponent` 的 .cpp 有 1372 行，是纯可视化，却和求解器编排同模块。
- `AVoxelPlanetActor` 在构造函数里直接创建大气、矿物观察、编辑网格三个组件，把「行星」绑死在三个领域上。
- 行星编辑的代码散在三个模块：`PlanetEditGridGeometry`/`PlanetSurfaceGridComponent`（Planet）、`PlanetEditComponent`/`PlanetEditBrush`/`PlanetBrushApplicator`（Vehicle）、`PlanetEditUI`（StellarEra）。

## 2. 目标与非目标

### 2.1 目标

1. 按领域拆成 11 个运行期模块，依赖严格分层、无环。
2. 每个模块只有一个主职责，公开头只暴露该职责所需的类型。
3. 保留现有玩法、数值、资产内容与类名（模块名可变，类名不变）。
4. 拆分后 `UniverseMap` 能正常加载，现有 `StellarEra.*` 自动化测试全部通过。

### 2.2 非目标

- 不改玩法、数值、材质、关卡内容。
- 不新增网络、存档、音频、本地化模块。
- 不重写 `AStellarEraPlayerController` 的业务逻辑；只改它的 include、类型和依赖。
- 不把 `StellarEraCore` 改成完全无 Engine 依赖（后续可选清理）。
- 不新建编辑器专属模块；`WITH_EDITOR` 的材质构建器先留在各自运行期模块里。

## 3. 现状关键耦合（拆分前必须知道）

| 耦合 | 位置 | 影响 |
|---|---|---|
| 行星 Actor 构造时创建 LOD Invoker / 大气 / 矿物观察 / 编辑网格 | `Source/StellarEraPlanet/Private/VoxelPlanetActor.cpp:102-127` | 拆大气/矿物模块会成环 |
| 大气组件 `Cast<AVoxelPlanetActor>(GetOwner())` | `PlanetAtmosphereComponent.cpp:187,301` | 大气反向依赖地形 |
| 矿物观察组件 `Activate(AVoxelPlanetActor*)` | `PlanetMineralViewComponent.h:44` | 矿物反向依赖地形 |
| 角色构造时创建 `UPlanetEditComponent` | `PlanetCharacter.cpp:176` | 角色依赖行星编辑 |
| 飞船持有 `APlanetCharacter*` | `SpaceshipPawn.h:904` | 飞船依赖角色 |
| 仿真子系统 `Cast<ASpaceshipPawn>` | `CosmosSimulationSubsystem.cpp:145,216,274,539` | 引力编排依赖飞船 |
| 材质构建器是 `WITH_EDITOR` | `PlanetAtmosphereMaterialBuilder.cpp:3`、`PlanetMineralMaterialBuilder.cpp:24` | 运行期模块里含编辑器代码 |

另有一条已完成拆分的先例：`Config/DefaultEngine.ini:123` 起已有 `[CoreRedirects]` 兼容层，注释明确写了「这是永久兼容层，不要删」。本次继续沿用同一机制。

## 4. 目标模块（11 个）

| # | 模块 | 类型 | 主职责 | Public 依赖 | Private 依赖 |
|---:|---|---|---|---|---|
| 1 | `StellarEraCore` | Runtime | 单位（`CosmosUnits`）、N 体求解器、纯数学 | Core, CoreUObject, Engine | — |
| 2 | `StellarEraGeology` | Runtime | 元素表、`FPlanetGeologyField`、JSON 预设、噪声采样 | Core, CoreUObject | Voxel, Json |
| 3 | `StellarEraTerrain` | Runtime | 体素行星 Actor、生成器、地形模块、表面射线、编辑网格、Voxel invoker 修补 | Core, CoreUObject, Engine, Voxel, StellarEraGeology | StellarEraCore, AssetRegistry |
| 4 | `StellarEraAtmosphere` | Runtime | 大气壳组件、参数推导、大气材质 | Core, CoreUObject, Engine | StellarEraTerrain |
| 5 | `StellarEraMinerals` | Runtime | 等值面构建、矿物观察 Actor/组件、矿物材质 | Core, CoreUObject, Engine, StellarEraGeology | StellarEraTerrain, StellarEraAtmosphere, GeometryCore, GeometryFramework |
| 6 | `StellarEraPlanetEdit` | Runtime | 编辑组件、笔刷形状、笔刷应用器 | Core, CoreUObject, Engine | StellarEraTerrain, Voxel |
| 7 | `StellarEraCharacter` | Runtime | 行星角色、球面移动组件、动画实例 | Core, CoreUObject, Engine | StellarEraTerrain, StellarEraPlanetEdit, Voxel |
| 8 | `StellarEraSpaceship` | Runtime | 飞船 Pawn、推力/阻尼、登船挂接 | Core, CoreUObject, Engine | StellarEraTerrain, StellarEraAtmosphere, StellarEraCharacter, Voxel |
| 9 | `StellarEraCosmos` | Runtime | 轨道组件、仿真子系统、轨迹/速度/引力可视化 | Core, CoreUObject, Engine, StellarEraCore | StellarEraTerrain, StellarEraSpaceship |
| 10 | `StellarEraUI` | Runtime | HUD、星图相机、星图面板、矿物图例、提示、编辑按钮布局 | Core, CoreUObject, Engine | StellarEraCore, StellarEraCosmos, StellarEraTerrain, StellarEraMinerals, StellarEraPlanetEdit, StellarEraCharacter, StellarEraSpaceship, Voxel |
| 11 | `StellarEra` | Runtime（primary） | GameMode、PlayerController、输入资产构造、登船/模式切换 | Core, CoreUObject, Engine | 以上全部, Voxel, InputCore, EnhancedInput |

> `StellarEraPlanet` 重命名为 `StellarEraTerrain`；`StellarEraVehicle` 拆空后删除。
> `StellarEra` 保持原名，`IMPLEMENT_PRIMARY_GAME_MODULE` 不动。

## 5. 依赖规则

```
L4  StellarEra (Game)
      │
L3  StellarEraUI ─→ StellarEraCosmos ─→ StellarEraSpaceship ─→ StellarEraCharacter
      │                  │                    │                    │
      │                  └────────────────────┴──→ StellarEraAtmosphere
      ├─→ StellarEraMinerals ─→ StellarEraTerrain ─→ StellarEraGeology
      └─→ StellarEraPlanetEdit ─→ StellarEraTerrain
                                 StellarEraCore（所有模块的底座）
```

强制规则：

1. 箭头只能向下；任何模块不得依赖 `StellarEra`（primary game module）。
2. 只有 `StellarEraUI` 可以同时依赖多个功能模块；功能模块不得反向依赖 UI。
3. 只有 `StellarEra` 可以依赖全部模块；其他模块不得依赖 `StellarEra`。
4. 公开头只 include 同模块或 Public 依赖的头；跨模块类型优先前置声明 + `TObjectPtr<>`。
5. `StellarEraTerrain` 的公开头包含 `VoxelWorld.h`，所以 `Voxel` 必须是它的 Public 依赖；其他模块只有在 `.cpp` 里直接 include Voxel 头时才需要把 `Voxel` 放进 Private。
6. 每个模块的 API 宏必须与模块名一致：`STELLARERATERRAIN_API`、`STELLARERAGEOLOGY_API`、`STELLARERAATMOSPHERE_API`、`STELLARERAMINERALS_API`、`STELLARERAPLANETEDIT_API`、`STELLARERACHARACTER_API`、`STELLARERASPACESHIP_API`、`STELLARERACOSMOS_API`、`STELLARERAUI_API`。搬类时必须同步改宏，否则链接期报 `unresolved external` 或 DLL 导入不匹配。

## 6. 关键设计决策

### D1：11 个运行期模块

已确认（2026-10-25）。细粒度方案，`Atmosphere`、`PlanetEdit`、`Spaceship` 都独立。

### D2：大气与矿物观察组件挪到蓝图

`AVoxelPlanetActor` 不再在构造函数里创建 `UPlanetAtmosphereComponent` 和 `UPlanetMineralViewComponent`：

- `BP_VoxelPlanet` 增加这两个组件，挂到根组件下。
- `AVoxelPlanetActor` 删除这两个成员、getter 和构造代码。
- 消费方改为 `FindComponentByClass<UPlanetAtmosphereComponent>()` / `FindComponentByClass<UPlanetMineralViewComponent>()`。
- 受影响调用点：
  - `SpaceshipPawn.cpp:960,990`
  - `StellarEraPlayerController.cpp:852`
  - `PlanetMineralViewComponent.cpp:145,178`
- `EditGrid`（`UPlanetSurfaceGridComponent`）**留在 Terrain**：它是对体素表面的可视化，不是编辑逻辑。

代价：直接摆放原生 `AVoxelPlanetActor` 的关卡不再自带大气和矿物观察组件。本项目使用 `BP_VoxelPlanet`，可接受。

### D3：不引入新接口

不新增 `IPlanetSurfaceProvider` 之类的接口。`Atmosphere`、`Minerals` 继续直接 `Cast<AVoxelPlanetActor>` / 接收 `AVoxelPlanetActor*`，因此它们单向依赖 `StellarEraTerrain`；`StellarEraTerrain` 反过来不认识它们。

### D4：`PlanetEditUI` 归 UI

`PlanetEditUI` 是纯布局与命中函数，不碰 Canvas、不碰体素。放 `StellarEraUI`，不放 `StellarEraPlanetEdit`。`PlanetEditComponent` 本身不依赖它。

### D5：测试跟随模块，集成测试放上层

- 纯单元测试跟随被测类所在模块。
- 需要同时实例化角色 + 编辑 + 行星的 `PlanetEditTests.cpp` 放 `StellarEra`（Game），因为 Game 依赖全部模块。
- 星图相关的集成测试放 `StellarEraUI`（UI 依赖 Cosmos/Terrain/PlanetEdit/Spaceship/Character）。

### D6：模块改名与兼容层

- `StellarEraPlanet` → `StellarEraTerrain`。
- `StellarEraVehicle` 删除；类分别进 `Character`、`Spaceship`、`PlanetEdit`。
- `StellarEraCore`、`StellarEra` 名字不变。
- 所有搬走的 UCLASS 都要在 `Config/DefaultEngine.ini` 的 `[CoreRedirects]` 里加 `+ClassRedirects`；`EPlanetBrushShape` 加 `+EnumRedirects`。
- 现有 `StellarEra.VoxelPlanetActor → StellarEraPlanet.VoxelPlanetActor` 这类链式重定向，**直接把 `NewName` 改到最终模块**，不要叠成两级链。
- 重定向是永久兼容层，不删。

### D7：primary module 不动

`Source/StellarEra/StellarEra.cpp` 里的 `IMPLEMENT_PRIMARY_GAME_MODULE(FDefaultGameModuleImpl, StellarEra, "StellarEra")` 保持原样。

## 7. 类 → 模块映射

### 7.1 来自 `StellarEra`（29 个文件）

| 目标模块 | 文件 |
|---|---|
| `StellarEraCosmos` | `CosmosOrbitComponent.h/.cpp`、`CosmosOrbitComponentTests.cpp`、`CosmosSimulationSubsystem.h/.cpp`、`CosmosTrajectoryComponent.h/.cpp`、`CosmosTrajectoryComponentTests.cpp` |
| `StellarEraUI` | `StarMapCameraPawn.h/.cpp`、`StarMapCameraInvokerTests.cpp`、`StarMapUISystem.h/.cpp`、`StarMapUISystemTests.cpp`、`StarMapTests.cpp`、`StellarEraHUD.h/.cpp`、`StellarEraHUDTests.cpp`、`MineralLegendTests.cpp`、`PlanetEditUI.h/.cpp` |
| `StellarEra` | `StellarEraGameMode.h/.cpp`、`StellarEraPlayerController.h/.cpp`、`PlanetEditTests.cpp`、`StellarEra.cpp/.h`、`StellarEra.Build.cs` |

### 7.2 来自 `StellarEraPlanet`（42 个文件）

| 目标模块 | 文件 |
|---|---|
| `StellarEraTerrain` | `VoxelPlanetActor.h/.cpp`、`VoxelPlanetGenerator.h/.cpp`、`VoxelPlanetTerrainModule.h/.cpp`、`VoxelPlanetNoiseTerrain.h/.cpp`、`VoxelPlanetSphereTerrain.h/.cpp`、`PlanetSurfaceRay.h/.cpp`、`PlanetSurfaceRayTests.cpp`、`PlanetSurfaceGridComponent.h/.cpp`、`PlanetEditGridGeometry.h/.cpp`、`AlwaysLocalVoxelInvokerComponent.h/.cpp`、`PlanetLODTransitionTests.cpp`、`PlanetTerrainGradientNormalTests.cpp` |
| `StellarEraGeology` | `PlanetGeologyField.h/.cpp`、`PlanetGeologyFieldTests.cpp`、`PlanetGeologyTypes.h` |
| `StellarEraAtmosphere` | `PlanetAtmosphereComponent.h/.cpp`、`PlanetAtmosphereParams.h/.cpp`、`PlanetAtmosphereParamsTests.cpp`、`PlanetAtmosphereMaterialBuilder.h/.cpp` |
| `StellarEraMinerals` | `PlanetMineralSurface.h/.cpp`、`PlanetMineralSurfaceTests.cpp`、`PlanetMineralViewActor.h/.cpp`、`PlanetMineralViewComponent.h/.cpp`、`PlanetMineralViewTests.cpp`、`PlanetMineralMaterialBuilder.h/.cpp` |

### 7.3 来自 `StellarEraVehicle`（23 个文件）

| 目标模块 | 文件 |
|---|---|
| `StellarEraCharacter` | `PlanetCharacter.h/.cpp`、`PlanetCharacterMovementComponent.h/.cpp`、`PlanetCharacterAnimInstance.h/.cpp`、`PlanetCharacterAnimTests.cpp` |
| `StellarEraSpaceship` | `SpaceshipPawn.h/.cpp`、`SpaceshipAtmosphereDragTests.cpp`、`SpaceshipCollisionTests.cpp`、`SpaceshipFreeLookTests.cpp`、`SpaceshipLODInvokerTests.cpp`、`SpaceshipSpeedLimitTests.cpp`、`SpaceshipThrustDirectionTests.cpp` |
| `StellarEraPlanetEdit` | `PlanetEditComponent.h/.cpp`、`PlanetEditBrush.h/.cpp`、`PlanetBrushApplicator.h/.cpp` |

### 7.4 `StellarEraCore`（6 个文件）

不动：`CosmosTypes.h`、`NBodySimulator.h/.cpp`、`NBodySimulatorTests.cpp`、模块文件。

## 8. 迁移阶段

每个阶段都必须独立构建、加载 `UniverseMap`、跑 `StellarEra.*` 测试，通过后再进下一阶段。

### Phase 0：基线

1. 提交当前工作区（含 Forgeworks 资产与 `BP_VoxelPlanet3.uasset` 等）。
2. 确认构建通过、`UniverseMap` 能打开、`StellarEra.*` 全绿。
3. 记录基线 commit hash。

### Phase 1：拆 `StellarEraCosmos`

- 新建模块目录、`.Build.cs`、`.h/.cpp`（`IMPLEMENT_MODULE`）。
- 移动 7.1 中 Cosmos 的文件。
- 把 `UCosmosOrbitComponent` / `UCosmosSimulationSubsystem` / `UCosmosTrajectoryComponent` 的 API 宏从 `STELLARERA_API` 改成 `STELLARERACOSMOS_API`。
- 更新 `StellarEra.uproject`、两个 `Target.cs`。
- `StellarEra.Build.cs` 增加 `StellarEraCosmos` 私有依赖。
- 添加 3 条 `ClassRedirects`；更新 `DefaultEngine.ini` 里「Cosmos 留在 StellarEra」的注释。
- 如果 `DefaultGame.ini` 出现 `[/Script/StellarEra.CosmosSimulationSubsystem]` 段，改成 `[/Script/StellarEraCosmos.CosmosSimulationSubsystem]`（当前无此段）。

### Phase 2：拆 `StellarEraUI`

- 移动 7.1 中 UI 的文件。
- API 宏改为 `STELLARERAUI_API`。
- `StellarEra.Build.cs` 增加 `StellarEraUI` 私有依赖。
- 添加 `StellarEraHUD`、`StarMapUISystem`、`StarMapCameraPawn` 3 条 `ClassRedirects`。
- `PlanetEditUI` 是纯 C++ 命名空间，无重定向。

### Phase 3：拆 `StellarEraCharacter` / `StellarEraSpaceship` / `StellarEraPlanetEdit`

- 新建 3 个模块，移动 7.3 的文件。
- API 宏分别改为 `STELLARERACHARACTER_API`、`STELLARERASPACESHIP_API`、`STELLARERAPLANETEDIT_API`。
- 删除 `StellarEraVehicle`（模块文件 + `.uproject` + `Target.cs` 条目）。
- 添加 `ClassRedirects`：`PlanetCharacter`、`PlanetCharacterAnimInstance`、`PlanetCharacterMovementComponent`、`SpaceshipPawn`、`PlanetEditComponent`；`+EnumRedirects`：`EPlanetBrushShape`。
- `StellarEra.Build.cs` 更新依赖。
- `PlanetEditTests.cpp` 留在 `StellarEra`。

### Phase 4：拆 `StellarEraGeology` / `StellarEraAtmosphere` / `StellarEraMinerals`

- 新建 3 个模块，移动 7.2 的文件。
- API 宏分别改为对应模块宏。
- `StellarEraPlanet` 重命名为 `StellarEraTerrain`：
  - 改目录名、`.Build.cs`、`.h/.cpp`、`IMPLEMENT_MODULE`。
  - 全模块 `STELLARERAPLANET_API` → `STELLARERATERRAIN_API`。
  - 更新 `.uproject`、`Target.cs`、引用它的 `Build.cs`。
- 添加 `ClassRedirects`：
  - `VoxelPlanetActor`、`VoxelPlanetGenerator`、`VoxelPlanetTerrainModule`、`VoxelPlanetNoiseTerrain`、`VoxelPlanetSphereTerrain`、`PlanetSurfaceGridComponent`、`AlwaysLocalVoxelInvokerComponent`、`AlwaysLocalVoxelInvokerWithPredictionComponent` → `StellarEraTerrain.*`
  - `PlanetAtmosphereComponent` → `StellarEraAtmosphere.*`
  - `PlanetMineralViewActor`、`PlanetMineralViewComponent` → `StellarEraMinerals.*`
- 把现有 `StellarEra.VoxelPlanetActor → StellarEraPlanet.VoxelPlanetActor` 等 5 条的 `NewName` 直接改到 `StellarEraTerrain.*`。

### Phase 5：组件挪到 BP

1. 从 `AVoxelPlanetActor` 删除 `AtmosphereComponent`、`MineralViewComponent` 成员、getter、`CreateDefaultSubobject` 与对应 include。
2. 先在编辑器里列出 `UniverseMap` 中所有 `AVoxelPlanetActor` 实例的**实际类**，逐个改 —— 不要假定只有一张 BP。已知行星 BP 在 `Content/Saves/PlanetVoxel/UniverseMap/` 下有 `BP_VoxelPlanet.uasset`、`BP_VoxelPlanet2.uasset`、`BP_VoxelPlanet3.uasset` 三份（关卡是 OFPA，`UniverseMap.umap` 本身只有约 24 KB，不含 Actor 数据），`Content/Code/BP_VoxelPlanet.uasset` 是另一份。
3. 在步骤 2 列出的每张行星 BP 上添加 `UPlanetAtmosphereComponent` 和 `UPlanetMineralViewComponent`，挂到根组件下。
4. 迁移前先导出这两个组件在 BP 上的属性覆盖值，迁移后重新填写。
5. 更新调用点：
   - `SpaceshipPawn.cpp` 两处 → `FindComponentByClass<UPlanetAtmosphereComponent>()`
   - `StellarEraPlayerController.cpp` → `FindComponentByClass<UPlanetMineralViewComponent>()`
   - `PlanetMineralViewComponent.cpp` 两处 → `FindComponentByClass<UPlanetAtmosphereComponent>()`
6. 保存改过的行星 BP 与 `UniverseMap`。

### Phase 6：清理与验证

- 删除空的旧模块目录、`.uproject` 条目、`Target.cs` 条目。
- 全量构建 Editor target。
- 跑 `StellarEra.*` 自动化测试。
- PIE 验证：进 `UniverseMap`、看到大气壳、进星图、进矿物透视、进编辑模式、登船/离船。
- 检查日志里的 `LogLinker` / `LogClass` 重定向告警。

## 9. 验证清单

### 构建

```bash
"D:/Application/Epic Games/UE_5.8/Engine/Binaries/ThirdParty/DotNet/10.0/win-x64/dotnet.exe" \
  "D:/Application/Epic Games/UE_5.8/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.dll" \
  StellarEraEditor Win64 Development \
  -Project="F:/workspace/ue/StellarEra/StellarEra.uproject" -WaitMutex -FromMsBuild
```

构建前必须关闭编辑器。

### 自动化测试

```bash
"D:/Application/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" \
  "F:/workspace/ue/StellarEra/StellarEra.uproject" \
  -ExecCmds="Automation RunTests StellarEra" \
  -TestExit="Automation Test Queue Empty" \
  -unattended -nopause -nullrhi -nosplash -stdout -abslog="<临时日志>"
```

### 资产加载

每个阶段至少确认这些资产无「unknown class / missing parent」：

- `Content/Code/BP_VoxelPlanet`
- `Content/Saves/PlanetVoxel/UniverseMap/BP_VoxelPlanet`、`BP_VoxelPlanet2`、`BP_VoxelPlanet3`（关卡里实际摆放的行星）
- `Content/Code/BP_PlanetCharacter`
- `Content/Code/BP_SpaceshipPawn`
- `Content/Code/DA_Terrain_Default`、`DA_Terrain_Earth`、`DA_Terrain_Jupiter`、`DA_Terrain_Mars`、`DA_Terrain_Venus`
- `Content/Maps/UniverseMap`

### 运行时

- 开局能看到大气壳。
- 按 M 进星图，相机、拾取、面板正常。
- 按 H 进矿物透视，图例与矿体正常。
- 按 G 进编辑模式，笔刷、网格、右键点放正常。
- 登船/离船正常。

## 10. 风险与缓解

| 风险 | 缓解 |
|---|---|
| 重定向遗漏导致 BP 丢失父类 | 每个阶段只搬一组类，搬完立即加载 `UniverseMap` 与四个 BP/DA 检查 |
| 移动类忘记改 API 宏导致链接失败 | 每个模块搬完后全模块搜 `STELLARERA[A-Z]+_API`，逐个核对 |
| BP 上大气/矿物组件的属性覆盖丢失 | 迁移前导出属性，迁移后逐项核对 |
| 循环依赖 | 每个阶段构建时看 UBT 的循环依赖报错；依赖表已在 §4 固化 |
| 构建时间变化 | 模块化后增量构建更快，全量构建可能略慢；不做量化承诺，以实测为准 |
| 未提交工作混进拆分 diff | Phase 0 先提交基线，拆分 commit 只含模块移动 |

## 11. 验收标准

1. `Source/` 下恰好 11 个运行期模块，依赖方向符合 §5。
2. `StellarEraEditor Win64 Development` 构建成功。
3. `StellarEra.*` 自动化测试全部通过。
4. `UniverseMap` 加载无 unknown class / missing parent。
5. PIE 五项运行时检查全部通过。
6. `Config/DefaultEngine.ini` 的 `[CoreRedirects]` 覆盖所有搬走的 UCLASS 与 `EPlanetBrushShape`。

## 12. 后续（不在本次范围）

- 把 `StellarEraCore` 改成完全无 Engine 依赖。
- 新增 `StellarEraEditor` 模块，收纳 `WITH_EDITOR` 的材质构建器。
- 新增 `StellarEraSave` 模块，把 `VoxelPlanetActor` 里的存档逻辑抽出来。
- 给 `AStellarEraPlayerController` 瘦身：输入资产构造抽成独立类，模式切换抽成小控制器。
- 星图相机独立成第 12 个模块（如果以后星图逻辑继续变大）。
