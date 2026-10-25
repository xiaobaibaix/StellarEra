# StellarEra

体素行星 + N 体引力 + 球面行走 + 飞船的太空模拟项目，基于 Unreal Engine 5.8。

## 环境要求

- **Unreal Engine 5.8.3**（`.uproject` 的 `EngineAssociation` 为 `5.8`）
- Windows 10/11 + VS Code（仓库没有 `.sln` / `.vcxproj` 是正常的，不要提交 IDE 生成物）
- Git LFS（工程内资产以 LFS 存储）
- 插件已在 `StellarEra.uproject` 中启用：`VoxelFree`、`ModelContextProtocol`、`AllToolsets`、`VibeUE`

## 快速开始

```bash
git lfs install
git lfs pull
```

1. 用 UE 5.8 打开 `StellarEra.uproject`
2. 打开 `Content/Maps/UniverseMap` —— 唯一包含体素行星、球面重力与 N 体场景的关卡
3. `Content/Maps/NewMap` 是平地测试图，**没有** `AVoxelPlanetActor`，球面行走相关验证要在 `UniverseMap` 做

## 构建

构建前必须关闭编辑器（Live Coding 会锁 DLL）。推荐用引擎自带 dotnet 直接调用 UBT：

```bash
"D:/Application/Epic Games/UE_5.8/Engine/Binaries/ThirdParty/DotNet/10.0/win-x64/dotnet.exe" \
  "D:/Application/Epic Games/UE_5.8/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.dll" \
  StellarEraEditor Win64 Development \
  -Project="<仓库路径>/StellarEra.uproject" -WaitMutex -FromMsBuild
```

不要使用 `Engine/Build/BatchFiles/Build.bat`，也不要使用 VibeUE 的 `BuildAndLaunchGame.ps1`（它最终同样调用 `Build.bat`）——两者都会因为锁机制报“找不到 .uproject”。

## 自动化测试

编辑器开着也能跑；测试只覆盖 C++ 逻辑，不启动模拟：

```bash
"D:/Application/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" \
  "<仓库路径>/StellarEra.uproject" \
  -ExecCmds="Automation RunTests StellarEra" \
  -TestExit="Automation Test Queue Empty" \
  -unattended -nopause -nullrhi -nosplash -stdout \
  -abslog="<可写日志路径>"
```

- 测试名前缀为 `StellarEra.`：`StellarEra.Core.*`、`StellarEra.Planet.AtmosphereParams.*`、`StellarEra.StarMap.*`
- 这条命令跑在 **editor world，不会执行 BeginPlay**；要验证 BeginPlay 用 `-game -nullrhi + UniverseMap`，并且该进程不会自动退出

## 目录与模块

```
Source/
├── StellarEra/            主游戏模块：GameMode / PlayerController
├── StellarEraCore/        N 体求解器与 Cosmos 单位类型
├── StellarEraCosmos/      N 体编排（子系统 / 轨道组件 / 轨迹组件）
├── StellarEraGeology/     元素表、行星地质场、噪声采样、JSON 预设
├── StellarEraTerrain/     行星 Actor / 生成器 / 地形模块 / 行星面射线与网格
├── StellarEraAtmosphere/  大气壳与大气参数
├── StellarEraMinerals/    矿物等值面 / 观察体 / 矿体材质
├── StellarEraPlanetEdit/  行星编辑组件、笔刷几何
├── StellarEraCharacter/   球面角色与移动组件、动画实例
├── StellarEraSpaceship/   飞船 Pawn
└── StellarEraUI/          HUD、星图相机与面板、矿物图例、编辑按钮布局
Plugins/
├── VoxelFree/          第三方体素插件（勿改）
└── VibeUE/             编辑器 AI 工具（第三方，勿改）
Content/
├── Maps/               UniverseMap / NewMap / TestMap
└── Code/               BP_ / DA_ 资产
docs/superpowers/       设计规格（specs/）与实施计划（plans/）
```

模块依赖方向是严格无环的：`StellarEra → StellarEraUI → StellarEraCosmos → StellarEraSpaceship → StellarEraCharacter → StellarEraPlanetEdit → StellarEraTerrain → StellarEraGeology`，
`StellarEraCosmos → StellarEraCore`。**`StellarEraUI` 不能依赖 `StellarEra`**（反向就成环）。

## 编辑器 AI 工具（MCP）

- 编辑器启动时自动开启 MCP：`http://127.0.0.1:8000/mcp`
- 项目根 `.mcp.json` 已配置 `unreal-mcp`；Claude Code 首次连接需要批准一次
- 官方工具 + VibeUE 扩展（Blueprint / Material / Actor / Landscape / Niagara / UMG / Performance 等 37 个服务），批量操作优先用 `execute_python_code`
- 配置位置：`Config/DefaultEditorPerProjectUserSettings.ini`
- 该端点无鉴权、只监听本机回环，等价于编辑器内任意代码执行，不要暴露到局域网

## 文档

- 项目上下文与已知坑：[`CLAUDE.md`](CLAUDE.md)
- 设计规格：`docs/superpowers/specs/`
- 实施计划：`docs/superpowers/plans/`
