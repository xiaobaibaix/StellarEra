# 星图改回「独立相机 Pawn + 复用 rig」，按 M 自动锁最近天体

日期：2026-11-26
状态：已评审（用户 2026-11-26 确认「相机载体（复用 rig）」并补充自动寻找最近天体）

## 目标

- 恢复 `AStarMapCameraPawn` 作为**被 Possess 的相机载体**：内部挂现有的
  `UStarMapRigComponent` + `UStarMapInputComponent`，相机算法一行不重写。
- 按 M 进图：控制器自动找**离源 Pawn 最近的天体**，生成相机 Pawn、平滑聚焦到它、
  把相机 Pawn 附加到该天体，再 `Possess`。
- 按 M 退图：rig 平滑混合回源 Pawn 的相机，控制器 `Possess` 回源 Pawn 并销毁相机 Pawn。
- 焦点天体与编辑组件从源 Pawn 传入相机 Pawn；飞船/角色自己不再挂 rig。

## 非目标

- 不恢复「每颗行星挂一台星图相机」的 CAC 模型；关卡里没有任何此类实例可删/可加。
- 不改绕行、缩放、拾取、编辑的算法与手感参数。
- 不做星图内 FOV 过渡。

## 背景（为什么推翻 2026-11-19 的决定）

`docs/superpowers/specs/2026-11-19-starmap-rig-camera-design.md` 当时去掉相机 Pawn，
好处是不换 Possess。本次按用户要求换回「相机载体」，保留该设计搬进 rig 的全部逻辑，
只把「谁拥有 rig」从飞船/角色改成相机 Pawn。已知风险用下面的门控与既有 AlwaysLocal
调用器规避，并在 PIE 验证。

## 设计

### 1. 组件归属

| 类 | 变化 |
|---|---|
| `AStarMapCameraPawn` | 根 `StarMapRoot`；CDO 里 `UStarMapRigComponent` + `UStarMapInputComponent`；`CalcCamera` 在 rig 激活/退场混合时调 `Rig->CalcView`，否则 `Super`；`SetupPlayerInputComponent` 调 `StarMapInput->Bind`；持 `TWeakObjectPtr<APawn> SourcePawn` 与访问器 |
| `ASpaceshipPawn` / `APlanetCharacter` | 删除 `StarMapRig` 成员、CDO 创建、`CalcCamera` 的星图分支与 include；保留 `UStarMapInputComponent`（只用来绑 M）；`IsStarMapActive()` 改为查 `UCosmosSimulationSubsystem` |
| `UStarMapRigComponent` | 增 `SourcePawn`；`Enter` 改 `EnterFrom(Source, Focus, ViewLocation, ViewRotation)`；`Exit`/`IsExiting` 公开；删 `Toggle`；焦点、编辑组件、退场相机位姿、invoker 搬运全部改读 `SourcePawn`；新增静态纯函数 `FindNearestBody(Location, Bodies)` |
| `UStarMapInputComponent` | `Bind` 不再强制要求 owner 上有 rig（飞船/角色只绑 M）；`OnStarMapToggle` 改为调 `IStellarEraSessionCommands::ToggleStarMap(GetOwner())` |

### 2. 进入流程（M）

`IStellarEraSessionCommands` 新增 `ToggleStarMap(APawn* From)`，实现在
`AStellarEraPlayerController`：

1. `From` 是相机 Pawn → `Rig->Exit()`，返回。
2. 否则要求 `From == GetPawn()`；从 `UCosmosSimulationSubsystem::GetRegisteredBodies` 里
   按球心距离取最近天体。没有天体 → HUD 提示并返回。
3. `GetPlayerViewPoint` 抓当前视点（Possess 前）。
4. 生成 `AStarMapCameraPawn`（`TSubclassOf` 控制器属性，默认原生类），`AlwaysSpawn`。
5. `SetSourcePawn(From)`；`Rig->EnterFrom(From, Focus, View, ViewRotation)`：
   设星图态、`SetSelectedBody(焦点)`、`FocusOn` 对数平滑拉远、套编辑参数、
   缓存源 Pawn 的非碰撞调用器并逐帧搬到相机。
6. `AttachToActor(焦点, KeepWorldTransform)`；`Possess(相机 Pawn)`。
7. 之后左键改选别的天体时，rig 在 `CalcView` 的选中变化分支里把相机 Pawn 改挂到新选中天体。

### 3. 退出流程（M）

1. 相机 Pawn 上的 M → `ToggleStarMap` → `Rig->Exit()`：清选中、星图态置 false、
   还原编辑参数与调用器、按 `ExitBlendSeconds` 向**源 Pawn 相机**缓动。
2. 控制器 `PlayerTick`：当前 Pawn 是相机 Pawn 且 `!Rig->IsActive() && !Rig->IsExiting()`
   → `Possess(SourcePawn)`、`DetachFromActor`、`Destroy` 相机 Pawn。
3. 源 Pawn 已失效：记 warning，保持相机 Pawn，不崩。

### 4. 编辑与 HUD

- rig 的 `GetPlanetEditComponent()` 优先取 `SourcePawn` 上的编辑组件；进图配置、退图还原不变。
- `AStellarEraHUD::GetViewedEditComponent()` 增加回退：当前 Pawn 是相机 Pawn 时取源 Pawn 的编辑组件。

### 5. 体素调用器

- `ApplyViewpointInvokerGate`：被 Possess 的是相机 Pawn 时，把 `SourcePawn` 当作视点，
  源 Pawn 的调用器保持开启（不翻 `bUseForLOD`，避免旧 ensure）。
- rig 的 `BeginInvokerFollow` 收集**源 Pawn**上 `bUseForCollisions == false` 的调用器，
  逐帧搬到相机世界位置；碰撞环留在源 Pawn。与 `VoxelViewpointInvokers.h` 的既有契约一致。

### 6. 测试

- 新增 `StellarEra.StarMap.NearestBody`：`UStarMapRigComponent::FindNearestBody` 的
  距离选择、空列表、失效 Actor。
- 现有 `StellarEra.StarMap.*` 纯函数用例与 `StarMap.InputWiring` 不变。
- PIE 实测进出、平滑、点击换选中、编辑、鼠标模式。

## 风险

- 重新引入 Possess 切换：鼠标模式、输入重绑、HUD 查询都要按「当前 Pawn 是相机 Pawn」重新走一遍。
- 飞船/角色蓝图里若改过星图 rig 参数，迁移后失效；参数以相机 Pawn 的原生默认值为准。
- 退出混合期间星图态已关，HUD 面板先消失，属既有行为。
