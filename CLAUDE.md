# StellarEra

体素行星 + N 体引力 + 球面行走 + 飞船的太空模拟项目。

> **架构权威文档：`docs/ARCHITECTURE.md`**（分层、依赖矩阵、目录规则、通讯机制、红线）。
> 本文只列 AI 助手最容易踩的几条，细节一律以那份为准。

## 技术栈
- Unreal Engine **5.8.3**（`EngineAssociation: "5.8"`），Windows / Win64
- C++ 为主；蓝图只承载资产配置（`Content/Code/BP_*`）
- 编辑器用 VS Code。仓库没有 `.sln` / `.vcxproj` 是正常的，别把 IDE 生成物入库

### IntelliSense：UBT 只管 `.cpp`，头文件要单独喂 includePath
VS Code 不解析 `.Build.cs`，C/C++ 扩展只读 `.vscode/compileCommands_StellarEra.json`。而 UBT 的 VSCode 生成器（`VSCodeProjectFileGenerator.WriteCompileCommands`）里的 `SourceFilesForClangCompilationDatabase` **只挑 `.cpp/.cc/.cxx/.c`，头文件永远不会入库**；cpptools 对库里查不到的文件回退到 `c_cpp_properties.json` 的 `includePath/forcedInclude`，而 UBT 生成的那份只有 `compileCommands` 一个字段 —— 于是**每个 `.h` 的第一个 `#include` 都报"无法打开源文件"**（`.cpp` 反而正常，这是最迷惑的地方）。

另外 UE5 的宏**不出现在命令行**上：全部塞进 `Intermediate/Build/<plat>/<arch>/UnrealEditor/<Config>/<Module>/Definitions.<Module>.h`，靠 `/FI` 强制包含。所以只补 `includePath` 不够，`forcedInclude` 也得给一份（只给一份，13 个模块的 Definitions 互相 `#undef` 会打架；任意一份即可，它内含的 `SharedDefinitions.*.h` 已经定义好全工程的 `*_API`）。

两步都靠 `Tools/vscode_intellisense.py` 自动完成，它从 rsp 里抽出 `/I` 与 `/FI` 回填到 `c_cpp_properties.json`：

```bash
# 1. 模块增删改名后先重生成工程文件（UBT 生成的 .ignore / *.code-workspace 可能带只读属性，先 attrib -R）
"<UE>/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.exe" -ProjectFiles \
  -Project="<项目>/StellarEra.uproject" -Game -Engine -Progress -VSCode
# 2. 再把 includePath / forcedInclude 补回去（UBT 会覆盖 c_cpp_properties.json）
python Tools/vscode_intellisense.py
```
最后在 VS Code 执行 `C/C++: Reset IntelliSense Database`（或重载窗口）—— cpptools 缓存编译命令，不重载不生效。

## 模块结构（5 个运行时模块，依赖严格分层，无环；编辑器专用代码在 `Celestial/Planet/Src/Geo/Viewer`，`#if WITH_EDITOR` 隔离）
```
StellarEra          ─→ StellarEraPawns（Public）；UI / Celestial / Cosmos（Private）
StellarEraUI        ─→ Celestial / Pawns（Public）；Cosmos（Private）
StellarEraCosmos    ─→ StellarEraCelestial（Private）
StellarEraPawns     ─→ StellarEraCosmos（Public）, StellarEraCelestial（Public）, Voxel（Public）
StellarEraCelestial ─→ Voxel（Public），无其它项目依赖
```
```
Source/
├── StellarEra/            主游戏模块（primary game module）：GameMode / PlayerController
│                          Src/Input/ 聚合三张 Pawn 动作表（控制器自己一条输入都不绑）
├── StellarEraCelestial/   天体：按天体分身，共用件在 Common/
│                          Planet/Src/{Geo,Atmosphere,Edit,Interaction,Invokers}/
│                          Planet/Src 根下：VoxelPlanetActor / PlanetBodyChildComponent
│                          Planet/Src/Geo：Core（字段/岩性/图元）/ Bake（阶段+规则）/
│                                  Snapshot（快照+缓存+求值）/ Voxel（体素适配器+材质）/
│                                  Viewer（Slate 查看器, WITH_EDITOR）/ Debug / Assets（配方/目录）
│                          Star/Src：StarBodyComponent / StarBodyChildComponent / StarParamBridge
│                          Common：ChildActorParamBridge（两种天体共用的子 Actor 参数桥基类）
│                          Tests 跟天体走：Planet/Tests、Star/Tests
│                          Invokers 里还有 IVoxelViewpointInvokers（三个 Pawn 与控制器共用）
│                          Edit 里有 PlanetEditUI（编辑按钮布局，HUD 与两个 Pawn 共用）
│                          Interaction 是行星表面解析射线（球面求交，无 UObject）
├── StellarEraPawns/       受控实体：Src/{Character,Spaceship,StarMap}/
│                          各功能面再分 Input/（输入组件 + 动作表），Character 还有 Animations/
│                          StarMap 内：相机 Pawn + StarMapPanelLayout / StarMapPicking
├── StellarEraCosmos/      N 体求解器 + 单位类型（CosmosTypes）+ 天体接口（CosmosBody）
│                          + 编排（子系统 / 轨道组件）
│                          Trajectory/ 轨道线：轨迹组件 / 曲线滤波 / 预测（不属于 N 体受力模型）
└── StellarEraUI/          HUD、星图面板
Plugins/
├── VoxelFree/          第三方体素插件，勿改
└── VibeUE/             第三方编辑器 AI 工具，勿改
Content/
└── Code/               蓝图与资产，引擎路径 /Game/Code/...
    ├── Maps/           UniverseMap（唯一真实场景）/ NewMap / TestMap
    ├── Pawns/          BP_SpaceshipPawn / Character/BP_PlanetCharacter
    ├── Planet/         BP_VoxelPlanet / BP_Star / BP_*Proxy / Materials/ /
    │                   Data/DA_GeoRecipe_* / BakeCache/
    ├── Test/           测试代理蓝图
    └── Res/            GalaxyBackground / Sky / Star 背景资源
```

> `StellarEraCore` 已在 2026-10-31 拆掉，别按旧文档去找：2411 行 `FNBodySimulator`
> 并入唯一使用方 `Cosmos` 的 `Src/NBody/`，`CosmosTypes.h` 跟着走，
> `IVoxelViewpointInvokers` 落到 `Celestial/Planet/Src/Invokers/`。
> 项目自研代码**没有 L0**，最底层就是 `StellarEraCelestial`。
> 旧地形/矿物栈（`Terrain`/`Minerals`/`Sky`/`Star` 目录、`UPlanetGeologyDistribution`）
> 已随地质管线重写删除，矿物数据改由 `Geo/Assets` 的配方与矿物目录承载。

模块内部按子目录分功能，**不靠拆模块来分**。每个模块结构统一，头文件与源文件**同目录并列**：

```
StellarEraXxx/
├── StellarEraXxx.Build.cs
├── StellarEraXxx.h        模块入口，与 Build.cs 并排
├── StellarEraXxx.cpp
└── Src/
    ├── Foo.h  Foo.cpp     业务代码
    ├── <功能子目录>/       同样 .h/.cpp 并列
    └── Tests/             自动化测试
```

`StellarEraCelestial` 是例外：它按**天体**分 `Planet/` 与 `Star/`，各自带 `Src/` 与
`Tests/`，共用件在 `Common/`；Build.cs 把模块根及全部子目录递归加进 include 路径。

三条硬规则：

1. **`*Tests.cpp` 只能放 `Src/Tests/`。** 测试放哪个模块，看它 include 了谁 ——
   必须是所引头文件的下游。跨模块的集成测试放装配层（`StellarEra/Src/Tests/`）。
2. **Build.cs 用循环把 `Src/` 及全部子目录加进 `PublicIncludePaths`。** 每个 Build.cs
   都有这段，新模块照着抄。UBT 只认 `Public/` `Private/` `Classes/` `Internal` 四个名字
   且默认不递归（BuildSettings V7 关掉了 `bLegacyPublicIncludePaths`），必须自己展开；
   写成循环的好处是**新增功能子目录不用改 Build.cs**。
3. **模块入口的 `StellarEraXxx.h/.cpp` 不进 `Src/`**，它们与 `Build.cs` 并排，
   `Src/` 里只有业务代码。

2026-10 做过一次 11 → 7 的模块合并（Terrain/Atmosphere/Minerals/PlanetEdit → Planet，
Character/Spaceship → Pawns），历史类路径由 `Config/DefaultEngine.ini` 的
`[CoreRedirects]` 兜底，**那段配置不要删**。

之后模块内部按功能分子目录，不再拆模块；`Celestial/Planet/Src/Geo/` 是唯一到三级的域。

最多三级（`Src/功能域/子目录/文件`），现存三级都在 `Geo/` 下；
Pawns 的 `Character|Spaceship|StarMap/Input/` 与 `Character/Animations/` 是二级。
判据写在 `Source/README.md` 的「什么时候该建子目录」。

`AStarMapCameraPawn`（`StellarEraPawns/Src/StarMap/`）是**被 Possess 的相机载体**：
CDO 里挂 `UStarMapRigComponent` + `UStarMapInputComponent`，`CalcCamera` 转给 rig。
按 M 时 `AStellarEraPlayerController::ToggleStarMap` 找离源 Pawn 最近的天体、生成相机 Pawn、
附加到该天体并 `Possess`；退图等 rig 混合完再 `Possess` 回源 Pawn 并销毁相机 Pawn。
焦点、编辑组件、退场相机位姿都从源 Pawn（飞船/角色）取，飞船与角色自己不再挂 rig。
行星上没有星图相机实例；`Config/DefaultEngine.ini` 里两条指向旧类的 redirect 保留（目标类已回）。

自定义代码不进第三方插件目录，一律写进上面五个模块并改对应 `Build.cs`。

**`StellarEraUI` 不依赖 `StellarEra`**（Game 依赖 UI，反向就成环）。星图 UI 需要玩家控制器时
直接吃 `APlayerController`：鼠标坐标、屏幕拾取、当前 Pawn 的 rig / 编辑组件分别来自
`GetMousePosition`、Pawns 的 `StarMapPicking`、`Cast<AStarMapCameraPawn>(Pawn)` 的回退链，
星图开关看 `UCosmosSimulationSubsystem::IsStarMapActive()`。
`StellarEra` 侧因此也把 `StellarEraUI` 降为**私有**依赖。

## 技能与 Agent 路由（强制）

下列技能/Agent 已随仓库入库，每个会话都会加载。命中场景时先调用、再动手：

- UE C++ 编写/修改/排错 → `unreal-engine-cpp-pro`
- 新建工程、工程配置、生成/更新 CLAUDE.md → `ue5-project-setup`
- 新功能或跨模块改动 → `superpowers:brainstorming` → `superpowers:writing-plans` → `superpowers:executing-plans`
- 修 bug → `superpowers:systematic-debugging`（先复现，再改）
- 声称完成或提交前 → `superpowers:verification-before-completion`（必须给出证据）
- 架构/性能/跨模块问题可委派对应 agent（小改动不必）：
  - C++/Blueprint 边界、性能、GAS → `unreal-systems-engineer`
  - World Partition / Landscape / HLOD / 流送 → `unreal-world-builder`
  - 材质 / Niagara / PCG → `unreal-technical-artist`
  - 网络复制 / 多人 → `unreal-multiplayer-architect`

用户明确指定技能或 Agent 时，以用户指定为准。任务完成后说明实际用了哪个技能/Agent。

## 关键架构约束
- **N 体只有一个驱动**：`UCosmosSimulationSubsystem`（TickableWorldSubsystem）每帧推进求解器、把位置/速度写回 Actor、再搬运骑乘者。天体不要各自 Tick。
- **天体的运动由自己的物理体承担**：子系统不再创建质量球，只绑定 Actor **根组件**上的物理体（代理天体的根就是 MassBody 球），读它的位置/速度、把求解器合成的合力 `AddForce` 上去，Transform 归 Chaos。根组件不模拟物理（如体素世界根组件）时退回求解器积分 + 每帧写回。初速以物理体为准，`InitialVelocityKmPerSecond` 只对退回路径生效。引擎默认线性阻尼 0.01/s 要显式清零。
- `UCosmosOrbitComponent` **只存参数**（`MassEarth`、`InitialVelocityKmPerSecond`）；天体初态位置就是关卡里 Actor 的摆放位置。
- `ASpaceshipPawn` **不自己位移**：只暴露推力加速度与阻尼，位移由子系统送进求解器。
- `UPlanetCharacterMovementComponent` 关掉 CMC 自带的“底座移动我”。角色**挂在行星下**（`SetRidePlanet` → `AttachToActor`），
  行星平移由附着层级带着走；`CarryRiders` 只是兜底，负责没有附着父级的 Pawn。
  登船判据用 `IsBoarded()`，**不要**用 `GetAttachParentActor() != nullptr`（骑乘时父级就是行星）。
- 大气：球外看 `UPlanetAtmosphereComponent` 的壳；球内天空由子系统逐帧重定位引擎 `SkyAtmosphere`。
- `UCosmosTrajectoryComponent` 运行期只读求解器；编辑器态走离线重建，不启动模拟。

## 关键类
| 类 | 模块 | 职责 |
|---|---|---|
| `FNBodySimulator` | Cosmos | O(N²) 引力，Velocity Verlet；AU / AU每年 / M☉，双精度 |
| `UCosmosSimulationSubsystem` | Cosmos | 唯一 N 体驱动：注册、推进、写回、诊断、轨迹预测 |
| `UCosmosOrbitComponent` | Cosmos | 天体参数（质量、初速） |
| `UCosmosTrajectoryComponent` | Cosmos | 预测轨迹、拖尾、速度/引力箭头 |
| `AStellarEraGameMode` | StellarEra | 出生点、飞船生成与登船 |
| `AStellarEraPlayerController` | StellarEra | 运行时构造输入资产、上下船、星图模式 |
| `AStarMapCameraPawn` | Pawns | 被 Possess 的相机载体；内部 rig 现算相机，退图时销毁 |
| `ICosmosBody` | Cosmos | 受控天体对模拟层的契约（13 个纯虚），由 `ASpaceshipPawn` 实现 |
| `UPlanetGeoGenerator` | Planet/Geo | 体素适配器：只把体素坐标转给快照求值核，不含地质数学 |
| `AVoxelPlanetActor` | Planet | 体素行星根 Actor；gizmo 移动 / 等比缩放即改行星 |
| `APlanetCharacter` / `UPlanetCharacterMovementComponent` | Character | 球面行走；重力方向喂给 CMC |
| `ASpaceshipPawn` | Spaceship | 街机飞船；推力交给 N 体求解器 |
| `UPlanetAtmosphereComponent` | Atmosphere | 大气壳（球外可见、球内被背面剔除） |

## 地质调试可视化（2026-11-14）
- 图层由阶段自己声明：`IPlanetStage::DescribeDebugLayers`（中间场经 `ExportDebugFields` 进快照 `DebugFields`，只在编辑器烘焙时保留，不进哈希）。**新阶段只注册图层，查看器不用改**；测试 `StellarEra.Planet.Geo.Debug.Registry` 会抓「阶段没登记图层」。
- 三种看法：星球细节面板 `Planet|GeoDebug`（地表叠加 / 剖切 + 切面图层）、地质查看器面板（展开地图 + 剖面 + 悬停读数 + 图例）、`StellarEra.Geo.ExportLayers [宽度]` 导出 PNG 到 `Saved/GeoDebug/`。
- 共用代码在 `Celestial/Planet/Src/Geo/Debug/`（`PlanetDebugLayer.*` 也在这里）。

## 关卡
- `UniverseMap`：**唯一有体素行星、球面重力、N 体与飞船的关卡**，PIE 验证都在这里。
- `NewMap`：平地测试图，没有 `AVoxelPlanetActor`；球面重力/绕球行走在这里验不了。
- `TestMap`：简单测试图。

## 构建与测试
构建前**必须关闭编辑器**（Live Coding 会锁 DLL）。用捆绑 dotnet 直调 UBT，cwd 在项目外：
```bash
"D:/Application/Epic Games/UE_5.8/Engine/Binaries/ThirdParty/DotNet/10.0/win-x64/dotnet.exe" \
  "D:/Application/Epic Games/UE_5.8/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.dll" \
  StellarEraEditor Win64 Development \
  -Project="F:/workspace/ue/StellarEra/StellarEra.uproject" -WaitMutex -FromMsBuild
```
不要用 `Build.bat`：它的锁机制会让 UBT 报“找不到 .uproject”。
VibeUE 自带的 `Plugins/VibeUE/BuildAndLaunchGame.ps1` 最终也是调 `Build.bat`，同样不用；构建一律以本节命令为准。

自动化测试（编辑器开着也能跑）：
```bash
"D:/Application/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" \
  "F:/workspace/ue/StellarEra/StellarEra.uproject" \
  -ExecCmds="Automation RunTests StellarEra" \
  -TestExit="Automation Test Queue Empty" \
  -unattended -nopause -nullrhi -nosplash -stdout -abslog="<临时日志>"
```
- 测试名前缀为 `StellarEra.`，主要分组：`Cosmos.*`（N 体/轨道）、`Planet.*`、`Geo.*`、`StarMap.*`、`UI.*`。
- 这条跑的是 **editor world，不会执行 BeginPlay**；要验 BeginPlay 用 `-game -nullrhi + 地图`，并且它不会自退。

## 编辑器 MCP
- Epic 官方 MCP + VibeUE 扩展都在 `http://127.0.0.1:8000/mcp`；项目根 `.mcp.json` 已配为 `unreal-mcp`。
- 自动启动在 `Config/DefaultEditorPerProjectUserSettings.ini`。
- 优先用 `execute_python_code` 批量操作编辑器；结构化工具在 OFPA 保存、渲染刷新上有已知限制。
- 该端点无鉴权、仅本机回环，等价于编辑器内任意代码执行。

## 单位约定（2026-10-19）

N 体数据分四层，**编辑器里只填人类单位**：

| 层 | 质量 | 速度 | 距离 / 时间 | 出现在哪 |
|---|---|---|---|---|
| 设计层（手填） | 地球质量 M⊕ | km/s（物理速度） | AU / 年 | `UCosmosOrbitComponent`、`ASpaceshipPawn` |
| 求解器层 | M☉ | AU/年 | AU / 年 | `FNBodySimulator`、`FCosmosBodyState` |
| 世界层 | — | 厘米/秒 | 厘米 / 秒 | Actor 位置、碰撞、体素、HUD 换算 |
| 换算层 | — | — | — | 只允许 `CosmosConstants::` / `CosmosMass::` / `CosmosSpeed::` / `CosmosWorld::`（`CosmosTypes.h`）；Pawns 已 Public 依赖 Cosmos（`ICosmosBody`），但飞船本地仍保留 `1e5` 一项（`CentimetersPerKilometer`） |

- `MassEarth` 单位 M⊕；`InitialVelocityKmPerSecond` 单位 km/s；`ThrustAccelerationKmPerS2` 单位 km/s²。
- **物理速度 ≠ 世界速度**：1 AU/年 = 4.74047 km/s = 3330 厘米/秒（默认标定）。
  编辑器初速是物理速度，HUD 显示的是世界速度（`GetVelocity().Size() / 1e5`）。
- 调用点禁止手写 `× CentimetersPerAu × TimeScale`：速度用
  `GameVelocityToWorldCmPerSecond` / `WorldCmPerSecondToGameVelocity`，
  加速度用 `GameAccelerationToWorldCmPerS2` / `WorldCmPerS2ToGameAcceleration`。
- `UCosmosOrbitComponent` **不 Tick**，只存参数。星图需要的位置/速度由
  `StarMapUISystem::BuildBodyInfo` 按需从 Actor 位置与求解器换算，不再有逐帧回显字段。
- 旧字段 `MassSolar` / `InitialVelocity` 只保留序列化与 `PostLoad` 迁移，代码不再消费。

## 编码约定
- Epic 命名：`A`/`U`/`F`/`E`/`I`/`T` 前缀，布尔 `bXxx`，UObject 指针 `TObjectPtr<>`。
- **组件名一律驼峰**：`CreateDefaultSubobject` 的字符串名、组件 UPROPERTY 变量名、蓝图组件变量名都只含字母数字（`PlanetSkyLight`、`LODInvoker`），不写空格、下划线、括号等符号。蓝图组件树渲染成 `变量名 (对象名)`，名字里再带符号只会叠出噪声。
- 每个 UObject 类写 `GENERATED_BODY()`，头文件用 `#pragma once`。
- 含中文的 `.h`/`.cpp` 存 **UTF-8 with BOM**，否则 MSVC 按 GBK 读会产生假 IntelliSense 错误。
- 不要统一换行符：仓库本来就是 CRLF/LF 混合，`autocrlf=true` 下 CRLF 是稳定态。
- 资产用 Allar 风格前缀：`BP_`/`DA_`/`M_`/`MI_`/`T_`/`SM_`/`NS_`。
- 二进制资产走 Git LFS；遇到 `Package is unloadable` 先查 LFS 指针是否未 smudge（`git lfs checkout`）。
- 提交时 pathspec 挂在 commit 上（`git commit -- <paths>`）：索引里有常驻暂存文件，裸 `git commit` 会吞掉全部。

### 注释：只写「为什么」，不写「是什么」

- **单处 1–3 行，最多 4 行。** 超过就删。
- 留：反直觉选择的理由、踩过的坑、非显然的单位/物理约束、「别这样改」的警告。
- 删：复述代码在做什么、逐段讲解引擎源码、比喻与多段论证、历史沿革（「以前是 X」）。
- 头文件里的类/函数 doc 用 1–3 行说清契约；实现细节留给代码。
- 需要长篇解释的，写进 `docs/` 或 `CLAUDE.md`，不要塞进源码。

## 地质烘焙缓存（2026-11-22）
- 烘焙结果存 Content 资产 `/Game/Code/Planet/BakeCache/PC_<关卡>_<Actor>`（`UGeoBakeCache`，约 160 MB，走 LFS），编辑器启动/重烘时命中就跳过烘焙。
- **改了任何阶段的产出逻辑，必须把 `PlanetSnapshotSerialize.h` 的 `GeoBakeCacheFormat::Version` +1**，否则会静默读到旧缓存。
- 手动：`StellarEra.Geo.SaveBakeCache` 立刻保存脏缓存；行星细节面板「立即烘焙」强制忽略缓存；关编辑器自动保存。

## 已知坑
- 编辑器不在前台会整进程限 3 FPS，体素生成 0.16 s → 179 s；测性能先让编辑器在前台。
- `UniverseMap` 三体初态在 t≈0.84 模拟年近距离交会后能量漂移爆掉；先看日志“能量漂移”行，别先查渲染。
- 飞船会不会被行星剥走，看“飞船轨道半径 / 行星近拱点 Hill 半径”：≈0.61 被剥走，≈0.36 能活；飞船轨道面必须与行星轨道面共面。
- 体素八叉树的碰撞细分路径不读 `LODToSet`（锁死 Height 0），是爆块主因。
- **切图崩 `World Memory Leaks: N leaks objects and packages`** → 别猜，看日志里泄漏对象的 Outer 链。旧世界的 UObject 只要还被 GC root 握着就收不掉，`UEditorEngine` 在关卡加载后直接 Fatal。已踩过一次：`BiomeMaterialMID` 的 Outer 曾是行星 Actor，而编辑器态插件把这张 MID 原样登记进全局 `FVoxelMaterialInterfaceManager`（GC root）—— 现在统一走 `AVoxelPlanetActor::CreateBiomeMaterialMID`（瞬态包当 Outer），回归测试 `StellarEra.Planet.BiomeMaterial.TransientOuter`。**给体素相关物建 MID / 纹理，Outer 一律给瞬态包。**
- **World Partition 关卡里删掉 `WorldDataLayers` 再保存** → `Assertion failed: CanResolveDataLayers()`（`DataLayerManager.cpp`），进程当场死、关卡只写一半。脚本批量清空 Actor 时最容易踩（`bCanResolveDataLayers` 只在 `Initialize()` 且无 Level Instance 父级时才置 true）。要模板关卡就**新建空关卡**，别「复制后清空」。
- 体素行星存档有两种格式，**存与读的 `voxel.data.StoreSpecialValueForGeneratorValuesInSaves` 必须一致**。当前 ini 是 0（新格式）；仓库里的 `Content/Saves/PlanetVoxel/**` 是 **1（旧格式）** 的，因为 145 MB 的 LFS 资产没跟着提交。新拉下来的仓库若直接进 `UniverseMap`，会读出一颗坏星。迁移三步：把 ini 改回 1 → 进关卡等它加载完（会慢约 36 s）→ 执行 `StellarEra.Planet.ResaveAll 0`（它在写之前把 CVar 切成 0，读的时候还得是 1，所以不能提前到命令行）→ 把 ini 改回 0。

更细的设计见 `docs/superpowers/specs/`，实施记录见 `docs/superpowers/plans/`，
历史踩坑与项目记忆见 `docs/memory/`（索引 `docs/memory/MEMORY.md`）。

<!-- BEGIN VibeUE (v5.0) — generated by VibeUE.GenerateAgentConfig; re-run to refresh -->
@F:/workspace/ue/StellarEra/Plugins/VibeUE/Content/samples/AGENTS.md.sample
<!-- END VibeUE -->
