# StellarEra

体素行星 + N 体引力 + 球面行走 + 飞船的太空模拟项目。

## 技术栈
- Unreal Engine **5.8.3**（`EngineAssociation: "5.8"`），Windows / Win64
- C++ 为主；蓝图只承载资产配置（`Content/Code/BP_*`）
- 编辑器用 VS Code。仓库没有 `.sln` / `.vcxproj` 是正常的，别把 IDE 生成物入库

## 模块结构（依赖严格分层，无环）
```
StellarEra           ─→ StellarEraUI（Public）, StellarEraCosmos, StellarEraTerrain,
                         StellarEraMinerals, StellarEraPlanetEdit, StellarEraCharacter,
                         StellarEraSpaceship, StellarEraCore
StellarEraUI         ─→ StellarEraCosmos, StellarEraTerrain, StellarEraGeology,
                         StellarEraMinerals, StellarEraPlanetEdit, StellarEraCharacter,
                         StellarEraSpaceship, StellarEraCore
StellarEraCosmos     ─→ StellarEraCore（Public）, StellarEraSpaceship, StellarEraTerrain
StellarEraSpaceship  ─→ StellarEraCharacter, StellarEraTerrain, StellarEraAtmosphere
StellarEraCharacter  ─→ StellarEraPlanetEdit, StellarEraTerrain
StellarEraPlanetEdit ─→ StellarEraTerrain
StellarEraMinerals   ─→ StellarEraGeology（Public）, StellarEraTerrain, StellarEraAtmosphere
StellarEraAtmosphere ─→ StellarEraTerrain
StellarEraTerrain    ─→ StellarEraGeology（Public）
StellarEraGeology    ─→ 无项目依赖（Voxel 是 Public）
StellarEraCore       ─→ 无项目依赖
```
```
Source/
├── StellarEra/            主游戏模块（primary game module）：GameMode / PlayerController
├── StellarEraCore/        N 体求解器与 Cosmos 单位类型
├── StellarEraCosmos/      N 体编排（子系统 / 轨道组件 / 轨迹组件）
├── StellarEraGeology/     元素表、行星地质场、噪声采样、JSON 预设
├── StellarEraTerrain/     行星 Actor / 生成器 / 地形模块 / 行星面射线与网格
├── StellarEraAtmosphere/  大气壳与大气参数
├── StellarEraMinerals/    矿物等值面 / 观察体 / 矿体材质
├── StellarEraPlanetEdit/  行星编辑组件、笔刷几何
├── StellarEraCharacter/   球面角色与移动组件、动画实例
├── StellarEraSpaceship/   飞船 Pawn
└── StellarEraUI/          HUD、星图相机、星图面板、矿物图例、编辑按钮布局
Plugins/
├── VoxelFree/          第三方体素插件，勿改
└── VibeUE/             第三方编辑器 AI 工具，勿改
Content/
├── Maps/               UniverseMap（唯一真实场景）/ NewMap / TestMap
└── Code/               BP_SpaceshipPawn / BP_PlanetCharacter / BP_VoxelPlanet / DA_Terrain_*
```
自定义代码不进第三方插件目录，一律写进上面十一个模块并改对应 `Build.cs`。

**`StellarEraUI` 不依赖 `StellarEra`**（Game 依赖 UI，反向就成环）。星图 UI 需要控制器数据时走
`IStarMapUIInputs`（`Source/StellarEraUI/Public/StarMapUIInputs.h`），由 `AStellarEraPlayerController` 实现。

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
- `UCosmosOrbitComponent` **只存参数**（`MassEarth`、`InitialVelocityKmPerSecond`）；天体初态位置就是关卡里 Actor 的摆放位置。
- `ASpaceshipPawn` **不自己位移**：只暴露推力加速度与阻尼，位移由子系统送进求解器。
- `UPlanetCharacterMovementComponent` 关掉 CMC 自带的“底座移动我”，骑乘搬运统一交给 `CarryRiders`。
- 大气：球外看 `UPlanetAtmosphereComponent` 的壳；球内天空由子系统逐帧重定位引擎 `SkyAtmosphere`。
- `UCosmosTrajectoryComponent` 运行期只读求解器；编辑器态走离线重建，不启动模拟。

## 关键类
| 类 | 模块 | 职责 |
|---|---|---|
| `FNBodySimulator` | Core | O(N²) 引力，Velocity Verlet；AU / AU每年 / M☉，双精度 |
| `UCosmosSimulationSubsystem` | Cosmos | 唯一 N 体驱动：注册、推进、写回、诊断、轨迹预测 |
| `UCosmosOrbitComponent` | Cosmos | 天体参数（质量、初速） |
| `UCosmosTrajectoryComponent` | Cosmos | 预测轨迹、拖尾、速度/引力箭头 |
| `AStellarEraGameMode` | StellarEra | 出生点、飞船生成与登船 |
| `AStellarEraPlayerController` | StellarEra | 运行时构造输入资产、上下船、星图模式 |
| `AStarMapCameraPawn` | UI | 星图相机；不 Tick，位置在 `CalcCamera` 现算 |
| `AVoxelPlanetActor` | Terrain | 体素行星根 Actor；gizmo 移动 / 等比缩放即改行星 |
| `UVoxelPlanetGenerator` | Terrain | 行星与共享地形模块 `UVoxelPlanetTerrainModule` 的适配器 |
| `APlanetCharacter` / `UPlanetCharacterMovementComponent` | Character | 球面行走；重力方向喂给 CMC |
| `ASpaceshipPawn` | Spaceship | 街机飞船；推力交给 N 体求解器 |
| `UPlanetAtmosphereComponent` | Atmosphere | 大气壳（球外可见、球内被背面剔除） |

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
- 测试名前缀为 `StellarEra.`：`StellarEra.Core.*`（N 体）、`StellarEra.Planet.AtmosphereParams.*`、`StellarEra.StarMap.*`。
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
| 换算层 | — | — | — | 只允许 `CosmosUnits::`（`CosmosTypes.h`）；`StellarEraVehicle` 不能依赖 Core，本地保留 `1e5` 一项 |

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
- 每个 UObject 类写 `GENERATED_BODY()`，头文件用 `#pragma once`。
- 含中文的 `.h`/`.cpp` 存 **UTF-8 with BOM**，否则 MSVC 按 GBK 读会产生假 IntelliSense 错误。
- 不要统一换行符：仓库本来就是 CRLF/LF 混合，`autocrlf=true` 下 CRLF 是稳定态。
- 资产用 Allar 风格前缀：`BP_`/`DA_`/`M_`/`MI_`/`T_`/`SM_`/`NS_`。
- 二进制资产走 Git LFS；遇到 `Package is unloadable` 先查 LFS 指针是否未 smudge（`git lfs checkout`）。
- 提交时 pathspec 挂在 commit 上（`git commit -- <paths>`）：索引里有常驻暂存文件，裸 `git commit` 会吞掉全部。

## 已知坑
- 编辑器不在前台会整进程限 3 FPS，体素生成 0.16 s → 179 s；测性能先让编辑器在前台。
- `UniverseMap` 三体初态在 t≈0.84 模拟年近距离交会后能量漂移爆掉；先看日志“能量漂移”行，别先查渲染。
- 飞船会不会被行星剥走，看“飞船轨道半径 / 行星近拱点 Hill 半径”：≈0.61 被剥走，≈0.36 能活；飞船轨道面必须与行星轨道面共面。
- 体素八叉树的碰撞细分路径不读 `LODToSet`（锁死 Height 0），是爆块主因。

更细的设计见 `docs/superpowers/specs/`，实施记录见 `docs/superpowers/plans/`。

<!-- BEGIN VibeUE (v5.0) — generated by VibeUE.GenerateAgentConfig; re-run to refresh -->
@F:/workspace/ue/StellarEra/Plugins/VibeUE/Content/samples/AGENTS.md.sample
<!-- END VibeUE -->
