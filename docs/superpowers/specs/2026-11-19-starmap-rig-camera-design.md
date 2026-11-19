# 星图相机改为 Pawn 自带 rig（去掉独立星图相机 Pawn）

日期：2026-11-19
状态：已评审（用户 2026-11-19 确认三项取舍）

## 目标

星图不再靠「每颗行星挂一台 `AStarMapCameraPawn` + Possess 换人」。改为：**当前操控的 Pawn（飞船 / 角色）身上挂一个 `UStarMapRigComponent`，进星图时它接管该 Pawn 的相机**——位置 = 焦点行星 − 轨道视线 × 距离，逐帧现算，等价于「相机挂在行星下」，但飞船/角色的物理、附着、Possess 一概不变。

## 非目标

- 不改绕行/缩放/聚焦/拾取的算法与手感参数（整体搬家，数值不动）。
- 不恢复「星图里也有最细一档 LOD」——碰撞气泡留在原地，见取舍。
- 不做星图内的 FOV 过渡（星图不改 FOV）。

## 现状

- `AStarMapCameraPawn`（`Pawns/Src/StarMap/`）由行星的 CAC / 附着提供，按 M 时 `Possess`；退图用 `SetViewTargetWithBlend` + 延迟 Possess。
- Actor 不 Tick，位置在 `CalcCamera` 现算，同时把 Actor 拽到渲染位置——只为带着 4 档体素 invoker 走。
- 键鼠输入（`UStarMapCameraInputComponent`）、行星编辑组件（`UPlanetEditComponent`）、4 档 invoker 都挂在这台相机上。
- `StarMapPicking` 只把「带星图相机的天体」当可点候选；HUD / `StarMapUISystem` 用 `Cast<AStarMapCameraPawn>(GetPawn())` 判断在不在星图。

## 设计

### 1. `UStarMapRigComponent`（新，`Source/StellarEraPawns/Src/StarMap/`）

从 `AStarMapCameraPawn` 原样搬入：

- 状态：`OrbitRotation`、`AimOffset`、`OrbitDistanceCm`、`CameraLocation`、`LastSeenSelected`、平滑拉远字段。
- 方法：`CalcCamera` 主体（改名 `CalcView`）、`FocusOn`、`OrbitLook`、`AimLook`、`AddZoom`、`ResolveFocusSurfaceRadiusCm`、`ComputeOrbitDistanceLimitsCm`、`ResolveEditTargetByPlanets`。
- static 纯函数：`ClampAimOffset`、`OrbitLookScale`、`SilhouetteRadiusScale`、`ZoomAltitudeCm`、`RaySphereFirstHitCm`、`LimitDistanceByObstaclesCm`、`ComputeSurfaceAnchorLocation`、`PickBodyAtScreenPosition`。
- 全部 `EditAnywhere` 参数不变。

新增外壳：

| 成员 | 行为 |
|---|---|
| `Toggle(APlayerController*)` | 激活则 `Exit`，否则 `Enter` |
| `Enter(PC)` | `PC->GetPlayerViewPoint` 取起点；焦点 = 当前 Pawn 最近的行星（`AVoxelPlanetActor`，复用 Pawn 已有的最近行星搜索）；`SetSelectedBody(焦点)` + `SetStarMapActive(true)`；`FocusOn(焦点, PlanetFocusRadiusScale × 地表半径)` 沿原视线对数平滑拉远；缓存 owner 上 `bUseForCollisions == false` 的 invoker 相对变换；套星图笔刷参数 |
| `Exit()` | 视点按缓动插回 owner 相机组件的当前位姿（`ExitBlendSeconds`）；完成时 `SetStarMapActive(false)`、清选中、还原 invoker 与笔刷参数 |
| `CalcView(Delta, OutResult)` | owner 的 `CalcCamera` 调用；激活期间覆盖位置 / 朝向 |
| `IsActive()` | 进图到退图插值结束之间为真 |

每帧在 `CalcView` 里把缓存的 invoker 组件搬到相机世界位置（只搬 `bUseForCollisions == false` 的那些）。

### 2. Pawn 侧

- `ASpaceshipPawn::CalcCamera`（已有 override）与 `APlanetCharacter::CalcCamera`（新增 override）：rig 激活时先 `Super::CalcCamera` 取 FOV / 后期，再由 rig 覆盖位置与朝向；否则走原逻辑。
- `SetupPlayerInputComponent`：多绑一个 `UStarMapInputComponent`。
- 输入互斥：
  - `USpaceshipInputComponent::GetPawn()` / `UPlanetCharacterInputComponent::GetPawn()` 在 rig 激活时返回 `nullptr`（推力、视角、E 全停）。
  - `UStarMapInputComponent::GetPawn()` 在 rig 未激活时返回 `nullptr`。
  - 两侧的 `OnStarMapToggle` 都直接用 `GetOwner()`，保证进退都能触发。

### 3. 输入组件改名 `UStarMapInputComponent`

- `GetPawn()` → 返回 `APawn*`，内部查 rig。
- 编辑组件改为 `GetOwner()->FindComponentByClass<UPlanetEditComponent>()`。
- 进图时套星图笔刷参数（`ConfigureBrushRange(8, 1280, 64)`、`MaxEditDistanceCm = 5e7`、提示串），退图还原原值（min / max / size / step / 编辑距离共 5 项）。

### 4. 删除链

- `AStarMapCameraPawn`、`BP_StarMapCameraPawn`、三张关卡上的 CAC 实例。
- `IStellarEraSessionCommands::FindStarMapCameraFor` 及其实现与两处调用。
- `HasStarMapCamera`、`FindChildStarMapCamera`、`FindAttachedStarMapCamera`、`ResolvePlanetStarMapCamera`、`ResetToAttachOrigin`、`StarMapEnterer`、`StarMapExitTimerHandle`、`FinishStarMapExit`。
- `StarMapPicking` 的「有没有星图相机」候选过滤 → 所有注册天体可选。
- HUD / `StarMapUISystem` / PC 的 `Cast<AStarMapCameraPawn>` 判据 → `UCosmosSimulationSubsystem::IsStarMapActive()`。
- PC 鼠标模式：`ApplyMouseModeForPawn` 的星图分支改读子系统标志；`PlayerTick` 里与 free look 一样按「激活状态变化」刷新。

### 5. 资产

- `BP_SpaceshipPawn`：加 `StarMapRig`、`StarMapInput`、`PlanetEditComponent`（笔刷参数照旧相机那份）。
- `BP_PlanetCharacter`：加 `StarMapRig`、`StarMapInput`。
- `UniverseMap` / `GeoPlanetMap` / `GeoTestMap`：删 CAC（脚本批量；不要碰 `WorldDataLayers`）。

### 6. 测试迁移

| 原 | 新 |
|---|---|
| `AStarMapCameraPawn::<纯函数>` | `UStarMapRigComponent::<同函数>` |
| `AStarMapCameraPawn::StaticClass`（输入接线 / 视点门控） | 用 `ASpaceshipPawn` 或带 rig 的测试 Pawn |
| `GetDefault<AStarMapCameraPawn>()` 参数断言 | `GetDefault<UStarMapRigComponent>()` |
| `HasStarMapCamera` / `FindChild*` / `FindAttached*` / `ResolvePlanetStarMapCamera` 用例 | 删除 |
| `BP_StarMapCameraPawn` 的编辑组件断言 | 改指 `BP_SpaceshipPawn` |
| `UStarMapCameraInputComponent::ResolveLookMode` | `UStarMapInputComponent::ResolveLookMode` |

## 取舍与风险

- **星图期间少一层近景细节**：碰撞气泡不跟相机走（角色靠它站在地上、飞船靠它判落地）。要补就在两个 BP 各加一个 `bUseForCollisions=false` 的 LOD1 invoker，本设计不做。
- **退出过渡**是位置 / 旋转缓动，不是引擎 ViewTargetBlend；星图不改 FOV，实际无差别。
- **不再换 Possess**：附带好处是进出星图不会再触发体素八叉树全量重建（旧 ensure 的根因消失）。
- 相机不再是一个 Actor，`StarMapUISystem` 若用到相机上的取值（拾取像素容差）改为从当前 Pawn 的 rig 取，缺省回退到类默认值。
- 角色自己的编辑解析器按「脚下这颗行星」取样。星图期间 rig 会把它换成「所有行星里最近命中」，退图需还原成角色原解析器（`APlanetCharacter` 暴露安装方法，rig 退出时回装）。

## 验证

1. `Build` 通过（关编辑器）。
2. 自动化测试全绿（`Automation RunTests StellarEra`），重点 `StellarEra.StarMap.*`。
3. PIE @ `UniverseMap`：飞船 / 角色各按 M 进出；星图里转视角、滚轮缩放、点选换星、G 编辑、退出平滑；飞船物理坐标在星图前后连续（无 Possess、无重摆位）。
4. 关卡打开无 CAC 残留、无 `World Memory Leaks`。
