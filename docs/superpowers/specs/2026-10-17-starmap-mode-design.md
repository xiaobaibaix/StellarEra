# 星图模式 — 设计

- **日期**：2026-10-17
- **项目**：StellarEra（Unreal Engine 5.8）
- **状态**：设计已确认，待实现
- **前置**：`2026-10-17-trajectory-system-design.md`
  （轨迹系统已实现并经 PIE 验收 —— 星图模式建立在它的渲染之上）

---

## 1. 目标

加一个**星图模式**：按 M 进入，鼠标点击选中一颗天体，鼠标拖拽改变视角绕着它看，
并能看到轨迹。轨迹**只在这个模式下显示**。

进入方式：M 键切换（再按一次退出）。退出后回到进入前的 Pawn（角色或飞船）。

## 2. 需求（用户原话）

> 需要你做一个星图模式，比如按下 M 键进入星图模式，然后我可以点击选中它，
> 然后鼠标可以改变视角看它，也可也看到轨迹，且我觉得估计就应该在在这个时候才显示出来

拆成四条：

| # | 需求 |
|---|---|
| 1 | 按 M 进入 / 退出星图模式 |
| 2 | 鼠标点击选中一颗天体 |
| 3 | 鼠标改变视角，绕着选中天体看 |
| 4 | 看得到轨迹，且轨迹**只在这个模式下显示** |

## 3. 已定的六个决策

| # | 问题 | 选择 |
|---|---|---|
| 1 | 相机模型 | **绕选中天体轨道** |
| 2 | 拖尾语义 | **只门控「画」** —— 拖尾是真实历史，采样始终在跑 |
| 3 | 轨迹范围 | **所有天体都画，选中的高亮** |
| 4 | 输入怎么接 | **用项目自己的 Enhanced Input 写法**（运行时 `NewObject` 构造 IA，见 §6） |
| 5 | 进入时相机在哪 | **原地不动、不选中**（保持玩家进入前看的方向） |
| 6 | 高亮怎么画 | **只改颜色**（向白 lerp），不加粗 —— 理由见 §5.5 |

> 决策 4 的过程值得记一笔：第一次问的时候我给的选项里写着「加 Enhanced Input 资产
> 需要动二进制资产」，**这是假的** —— 本项目根本没有任何输入资产文件，IMC 与 IA 全是
> `BuildInputAssets()` 里 `NewObject` 造的（`StellarEraPlayerController.cpp:21-26`、`:188`，
> 类头注释 `:16-24` 自己就写着这件事）。更正后用户改选了这一项。

## 4. 三个架构裁定

### 4.1 相机 = 新 Pawn，不是切视图目标

进入时 `Possess` 一个新建的 `AStarMapCameraPawn`，退出时 `Possess` 回原来那个。

**决定性理由**：`GetPawn()` 变成相机 Pawn 后，`AStellarEraPlayerController` 里
**全部现有输入回调自动失效** —— 它们无一例外是 `Cast<APlanetCharacter>(GetPawn())`
或 `Cast<ASpaceshipPawn>(GetPawn())`，两个 Cast 都会失败。**一行现有代码都不用改、
不用加任何门控。**

反过来，若用 `SetViewTargetWithBlend` 切视图而不换 Pawn，`GetPawn()` 仍是角色，
`OnMove` 照样推着角色跑，得逐个回调加 `if (!bStarMapActive)` —— 改动面大，
且漏掉一个就是「在星图里按 W，角色在星球上走」。

这也是本文件自己声明的架构（`StellarEraPlayerController.cpp:22-23`）：
「所有动作都绑定在本控制器上，再按『当前 Possess 的是哪个 Pawn』分派。」

### 4.2 拾取 = 屏幕空间解析，不是射线

不用 `GetHitResultUnderCursor`。理由：体素星球在星图距离（AU 级）的碰撞体未必存在、
未必够准，还会被别的东西挡住。

改成：遍历子系统里的天体，`ProjectWorldLocationToScreen` 把各自的世界坐标投到屏幕，
取离光标最近、且在容差内的那个。

- 零碰撞依赖
- N 是个位数（本关卡天体数量），O(N) 无所谓
- **纯数学，所以能无头测**（§9）

拾取容差 = `max(固定像素容差, 该天体的屏幕投影半径)`。固定容差管远处只有几像素的天体；
投影半径管近处盘面有几百像素的情况 —— 否则「点在星球盘面上」会选不中。
投影半径取「中心」与「中心 + 视线右向 × 天体世界半径」两点的屏幕像素距离，**再乘以球体轮廓放大系数 `1/cos α`（`sin α = R/d`，见 `AStarMapCameraPawn::SilhouetteRadiusScale`）**
—— 切线决定的轮廓半径是 `f·R/√(d²−R²)`，直接投影「中心 + 右向 × R」量到的是 `f·R/d`，差一个 `cos α`；
最近距离（`d = 2R`）处差 13.4%，不乘就有一圈盘面点不中。
（体素星球用 `GetSurfaceRadiusCm()`，其它天体退回碰撞半径）。
**已知缺口**：拾取没有深度测试（见 §4.2 原设计），所以当近处天体的盘面覆盖了远处天体的
投影中心时，点那里会选中**中心更近**的那颗 —— 可能是被挡住的那颗。本次不修。

固定像素容差 `SelectionPixelRadius` 默认 `20.f`。

### 4.3 选中状态 = 放子系统

放 `UCosmosSimulationSubsystem`，与「星图模式是否激活」并列。

理由：它已经是天体的全局注册表（`Bodies`，`CosmosSimulationSubsystem.h:155`），
而且是**唯一**知道全部天体的地方 —— 那个数组现在是私有的、没有 getter，
无论如何都要加枚举接口。轨迹组件每帧**单向读**它决定高亮，没有回写、
没有生命周期问题。

备选（星图 Pawn 遍历所有天体、直接写每个轨迹组件的标志）要双向耦合、每帧写、
退出时还得清理，不做。

## 5. 组件设计

### 5.1 `AStarMapCameraPawn`（新建）

`APawn` 子类，一个 `UCameraComponent` 当根组件。不需要 SpringArm（太空里没有碰撞要避）。

**状态五样**：

| 成员 | 含义 |
|---|---|
| `FRotator OrbitRotation` | 从哪个方向看（yaw / pitch） |
| `double OrbitDistanceCm` | 相机到选中天体的距离 |
| `FVector CameraLocation` | 相机当前世界位置；**未选中时它不动，这就是「冻结」本身** |
| `TWeakObjectPtr<AActor> LastSeenSelected` | 上一帧看到的选中天体，用来检出「选中状态变了」 |
| 选中天体 | **不自己存**，每帧从子系统读（§4.3） |

**每帧**：

- **选中有效** → 位置 = 天体当前位置 − 视线方向 × 距离；朝向 = 视线方向。
  天体在动，相机跟着它动 —— 这是必须的，天体速度是 AU/年 量级。
- **未选中** → 位置**冻结在取消选中那一刻的位置**（`CameraLocation` 不更新），
  朝向仍由 `OrbitRotation` 决定。

两种语义**共用同一份 yaw/pitch 状态**，区别只是绕的中心（天体 vs 自己）。
比维护两套状态简单。

**相机不 tick，位置在 `CalcCamera` 里现算。** 这是**必需的**，不是风格选择 ——
`UWorld::Tick` 里的时序是：

| 顺序 | 发生在哪 |
|---|---|
| 1 | 各 Actor tick 组：`TG_PrePhysics` … `TG_PostPhysics` |
| 2 | `FTickableGameObject::TickObjects` ← **子系统在这里推进求解器、把位置写回 Actor** |
| 3 | `PlayerController->UpdateCameraManager` ← **`CalcCamera` 在这里被调用** |
| 4 | `TG_PostUpdateWork`（轨迹组件在这里画线） |
| 5 | `TG_LastDemotable` |

（出处：`Runtime/Engine/Private/LevelTick.cpp` 里这三处的先后。）

**任何 tick 组都不落在第 2 步和第 3 步之间。** 天体在第 2 步之后就不动了，而相机在
第 3 步就被取走 —— 排在第 1 步会读到天体上一帧的位置，排在第 4 步则相机已经算完了。
两条路都差一帧；`CalcCamera` 是唯一落在正确窗口里的钩子，零滞后。

`CalcCamera` 先调 `Super::CalcCamera` 拿到相机组件给的 FOV / 后处理，再把
`Location` / `Rotation` 覆盖掉。Pawn 自己的 Actor 变换**不影响相机** ——
它只是为了让这个 Pawn 在 PIE 里看得见、点得中。

**鼠标操作**：

| 输入 | 效果 |
|---|---|
| 左键按住 + 鼠标位移 | 改 `OrbitRotation` |
| 滚轮 | `OrbitDistanceCm` **按倍率**缩放：`Distance *= pow(ZoomStepFactor, -Value)` |

**这里和飞船的 `AddZoom` 不一样，是有意的。** 飞船是**线性**步进
（`SpaceshipPawn.cpp:479`：`TargetZoomOffset = Clamp(TargetZoomOffset - Value * ZoomStep, ...)`），
因为摇臂长度只在几十厘米的量级内变。星图的距离要跨若干数量级（从贴着一颗行星
到看整个星系），线性步进在 AU 尺度上等于没反应。所以用倍率。

但飞船那条注释里的经验要照搬过来 —— `SpaceshipPawn.cpp:469-471`：

> 夹的是**目标偏移**而不是最终臂长。只夹最终值的话，玩家滚过头会攒下一堆被 Clamp
> 吃掉的无效偏移，往回滚要先「还债」——前几格看起来像没反应。

星图同样**夹目标距离本身**（`OrbitDistanceCm`），不是最终算出来的相机位置。

### 5.2 选中 / 取消选中：相机位置不跳变，朝向重算

**点击选中一颗天体时，相机位置不变，但朝向会重算。** 实现：`OrbitRotation` 取
「当前相机位置 → 该天体」的方向，`OrbitDistanceCm` 取当前的实际距离。于是
`焦点 − 方向 × 距离` 恒等于原来的相机位置 —— **位置代数上严格不变**。
但**朝向是被整个替换掉的**：只有原本就正对着该天体时才不变，最明显的是进星图后的
第一次选中（从「进星图时的飞船视角」直接摆到对准被选天体）。
之后再拖拽才改变视角。

点另一颗天体时同样处理 —— 距离和方向都从当前相机位置重新推导，位置不变、朝向重算。

**取消选中**：点**已选中**的那颗天体。

**点空白**：不改变选中。理由：取消选中会让相机停止跟随、丢失目标，
把它绑在一个误点很容易触发的动作上不划算。要取消就明确地点回它自己。

### 5.3 `UCosmosSimulationSubsystem` 新增

```cpp
/** 选中天体。传 nullptr 取消选中。存弱引用。 */
void SetSelectedBody(AActor* Body);

/** 当前选中的天体。已被销毁时返回 nullptr。 */
AActor* GetSelectedBody() const;

/** 已注册进求解器的天体（各自的 Actor）。过滤掉失效的组件。拾取用。 */
void GetRegisteredBodies(TArray<AActor*>& Out) const;

/** 星图模式是否激活。轨迹组件的显示门控读它。 */
bool IsStarMapActive() const;
void SetStarMapActive(bool bActive);
```

`bStarMapActive` 由**控制器**在 `ToggleStarMap()` 的进入 / 退出分支里设与清，
**不是** Pawn 的 `BeginPlay` / `EndPlay` —— 星图 Pawn 是懒创建并复用的（§6.2），
`BeginPlay` 一辈子只跑一次，第二次进入时标志不会被重新设上。

放子系统而不是让轨迹组件每帧去世界里找 Pawn，是因为它已经是全局状态宿主。

**选中天体与星图模式是两个独立状态** —— 未选中时也要显示轨迹（决策 3：
所有天体都画，选中的高亮）。

### 5.4 `UCosmosTrajectoryComponent` 改动

两处，都是真行为改动（不是注释）。

**改动 A：拖尾采样前移到显示门控之前。**

用户选的是「只门控『画』」（决策 2）。所以采样无条件跑，只有绘制被门控。
`TickComponent` 的新顺序：

```
1. LineBatch 判空 → return
2. Subsystem 判空 → return              ← 保持现状（那条 parked 缺陷本轮不动）
3. Orbit 判空
4. ── 拖尾采样（无条件，仍按模拟时间门限 TrailInterval）──   ← 搬到这里
5. bInStarMapView = Subsystem->IsStarMapActive() || Orbit->bShowOutsideStarMap
6. 三个逐天体开关各自与它相与：bWantTrail / bWantPrediction / bWantArrow
7. 三个都为假 → 只清一次线（沿用 bDrewLastFrame 的写法）→ return
8. 预测（仅当 bWantPrediction）+ 画（选中的高亮）
9. Flush + DrawLines
```

第 4 步原在早退**之后**，所以现在关掉轨迹就不采样、拖尾不积累。
搬上来之后，**拖尾窗口仍然有界**：裁剪按模拟时间（`TrailStartTime = 模拟时间 − TrailSpan`），
保留点数 ≤ `TrailSamples + 1` = 513。所以「一直不显示」也不会无限增长。

> ⚠️ **`bDrawTrajectory` 的语义跟着变了，它的注释要一起改。**
> 现在的文档写着「关掉可省下它的全部开销」。
> 采样上移之后它只门控「画」，采样照跑 —— 这句话就假了。改成如实描述：
> 关掉省下的是**预测重算与画线**，拖尾采样仍在跑（成本有界，保留点数 ≤ 513）。
> 这条属于「注释声称的事实」，必须与代码同改，否则就是下一轮复审要抓的假注释。
>
> **后续（2026-10-18）：这条属性被拆成两个** —— `bDrawPrediction`（关掉时连预测重算
> 都跳过，比原来更强）与 `bDrawTrail`（只门控「画」，上面这段不变量原样保留）。
> 两者互相独立，谁也不否决谁；星图可见性那一关仍然同时管住三样。

**改动 B：`UCosmosOrbitComponent` 新增属性 `bShowOutsideStarMap`（默认 `false`）。**

放在 `UCosmosOrbitComponent` 上而不是轨迹组件上 —— 轨迹组件是**哑渲染器**，
它自己的头注释写着「它不存任何面向用户的参数，参数全从兄弟 `UCosmosOrbitComponent`
上读（那边在编辑器详情面板里能调、有取色器）」。这条属性是面向用户的，同理。

> ⚠️ **这会改变现有观感。** 现在普通模式下能看到轨迹；改完后**默认只在星图模式显示**。
> 想保留现在的观感就把 `bShowOutsideStarMap` 勾上。

### 5.5 高亮：只改颜色

**选中天体的预测线与拖尾线，颜色向白 lerp 0.45**；其余天体不变。

| | 原色 | lerp 0.45 后 |
|---|---|---|
| `PredictionColor` | `(90, 170, 255)` | `(164, 208, 255)` |
| `TrailColor` | `(20, 50, 110)` | `(126, 142, 175)` |

深蓝变浅蓝，区别很明显。

**为什么不加粗**：`CosmosTrajectoryComponent.cpp:18` 是 `constexpr float LineThickness = 0.0f;`，
而 `Thickness == 0` 在行批里是一个**真分支** —— `BatchedElements.cpp:167` 判 `Thickness == 0.0f`，
走 `LineVertices`（1 像素 GPU 线）；`:184` 的 `else` 走粗线（屏幕空间四边形）。

所以给选中的天体设非零厚度，会把它**切到另一条渲染路径**，观感与其余天体不同。
那条路径在无头环境验不了，只能人眼看。为了不让「高亮」引入一个只能靠肉眼发现的失败面，
本轮只改颜色。

**加粗 / 置顶（`SDPG_Foreground`）是可选加项**，想做再说 —— 它们的观感同样只能人眼验。

## 6. 输入

### 6.1 新增两个动作（复用现有的两个）

`BuildInputAssets()` 里加两个运行时构造的 `UInputAction`：

```cpp
IA_StarMapToggle = MakeAction(this, TEXT("IA_StarMapToggle"), EInputActionValueType::Boolean);
IA_StarMapSelect = MakeAction(this, TEXT("IA_StarMapSelect"), EInputActionValueType::Boolean);
MapKey(MappingContext, IA_StarMapToggle, EKeys::M);
MapKey(MappingContext, IA_StarMapSelect, EKeys::LeftMouseButton);
```

`IA_Look`（`Mouse2D`）与 `IA_Zoom`（`MouseWheelAxis`）**直接复用**，不新建 ——
它们在 `OnLook` / `OnZoom` 里本来就按 Pawn 类型分派，各加一个星图分支即可。

### 6.2 回调分派

| 动作 | 事件 | 处理 |
|---|---|---|
| `IA_StarMapToggle` | Started | `ToggleStarMap()` |
| `IA_StarMapSelect` | Started | 记下按下位置 |
| `IA_StarMapSelect` | Completed | 位移 < 5 px → 算**点击**（拾取）；否则算**拖拽结束** |
| `IA_Look` | Triggered | `OnLook` 加星图分支 → `OrbitLook(DeltaYaw, DeltaPitch)` |
| `IA_Zoom` | Triggered | `OnZoom` 加星图分支 → `AddZoom(...)` |

「同一个左键既选中又转视角」靠 5 px 阈值区分 —— 按下和松开位置几乎相同就是点击。

`ToggleStarMap()` 的方向由「当前 Pawn 是不是 `AStarMapCameraPawn`」判定，
**不用单独的状态标志** —— 幂等、不会不同步。

星图 Pawn 由控制器懒创建并复用（`TObjectPtr` 成员），不每次进出都 Spawn/Destroy。

### 6.3 `ApplyMouseModeForPawn` 加第三个分支

**这是必需的。** 现在星图 Pawn 会掉进 `else`（`StellarEraPlayerController.cpp:170-178`），
被当成飞船处理 → 光标隐藏 + 永久捕获 → **点不了任何东西**。

新增分支：

```cpp
bShowMouseCursor = true;
SetInputMode(FInputModeGameAndUI()
    .SetLockMouseToViewportBehavior(EMouseLockMode::LockOnCapture)
    .SetHideCursorDuringCapture(false));
ViewportClient->SetMouseCaptureMode(EMouseCaptureMode::CaptureDuringMouseDown);
```

> **最后一行是必需的，不是美化。** `SceneViewport.cpp:905` 算出 `bViewportHasCapture`，
> 而 `MouseDelta` 的累加（`:923-933`）整段包在 `if (bViewportHasCapture)` 里 ——
> **没有捕获就没有 `Mouse2D`，左键拖拽收不到任何位移**。
> 角色分支用的是 `CaptureDuringRightMouseDown`（右键版）；星图要的是任意键版，
> 正好是 `EngineBaseTypes.h:49` 那个枚举存在的理由（「Capture the mouse during a
> mouse down, releases on mouse up」）。

## 7. 退出与边界

| 情况 | 行为 |
|---|---|
| 进入 | 记 `PreviousPawn = GetPawn()`；`Possess(StarMapPawn)`；`SetStarMapActive(true)` |
| 退出 | `Possess(PreviousPawn)`；清 `SelectedBody`；`SetStarMapActive(false)` |
| `PreviousPawn` 已失效 | **不 Possess**（留在相机上，不崩）。**但不是「M 还能再进」** —— `GetPawn()` 仍是星图 Pawn，下次按 M 会再次命中退出分支（星图已关，等于空转），进不去；脱身只能靠 `DebugPossessShip`。**这条路径可达**：引擎自己会销毁 Pawn（`AActor::CheckStillInWorld` 在 Z < `KillZ` 时调 `FellOutOfWorld`，后者函数体调 `Destroy()`；`AKillZVolume` 同理）—— 但**整条链有授权门控**，两处都要求 `HasAuthority() \|\| GetLocalRole() == ROLE_None`，PIE/单机成立、**纯客户端不成立**（结论不变，只是发生在有授权的一侧）。蓝图/关卡/PIE 手动删 Actor 也都在 `Source/` 之外；进入分支那句 `PreviousPawn = GetPawn()` 又没做空指针保护。**修法三条缺一不可**：①进入时只记非星图 Pawn；②判据换 `IsStarMapActive()`；③**退出分支兜底**（前两条只让状态可检出、不可恢复，`PreviousPawn` 失效时仍无对象可 Possess，需重新拿默认 Pawn） |
| 角色在星图模式下 | 站着不动（没输入）。它在星球表面的话，子系统照常携带它 —— `CarryRiders` 与 Possess 无关 |
| `NewMap` 没有天体 | 拾取遍历空列表 → 点不中 → 相机原地。不崩 |
| 进入前 Possess 的是飞船 | 退出后 `ApplyMouseModeForPawn` 恢复飞船的永久捕获 + 隐藏光标 |
| 重复按 M | 由「当前 Pawn 是不是星图 Pawn」判定方向，幂等 |

## 8. 提示

`PlayerTick` 加星图分支，复用现成的 `ShowPrompt`（固定 key、duration 0、每帧刷新）：

| 状态 | 文案 |
|---|---|
| 未选中 | `按 M 退出星图　左键拖拽转视角　滚轮缩放` |
| 已选中 | `<天体名>　左键拖拽绕行　滚轮缩放　M 退出` |

## 9. 测试

**能无头测的（纯数学）**：

1. **拾取函数** —— 设计成纯函数，投影留在调用方。候选是**一个结构体数组**，不是
   两个平行数组（平行数组靠下标约定对齐，结构体靠类型对齐）：
   ```cpp
   struct FStarMapScreenCandidate
   {
       AActor* Actor = nullptr;
       FVector2D ScreenPosition = FVector2D::ZeroVector;
       float ScreenRadiusPx = 0.f;   // 该天体的屏幕投影半径，见 §4.2
   };

   static AActor* PickBodyAtScreenPosition(
       TArrayView<const FStarMapScreenCandidate> Candidates,
       const FVector2D& ScreenPoint,
       float PixelRadius);
   ```
   测：命中最近者；超出容差返回 `nullptr`；空列表返回 `nullptr`；`Actor` 为空的候选
   被跳过；**恰好等于半径时算命中**（边界含）；两个候选等距时取**先出现**的那个 ——
   把输入顺序换过来结果跟着换，以证明它认的是顺序而不是某个特定对象。
   另有 `StellarEra.StarMap.PickingScreenRadius` 专测「容差取 max(固定容差, 投影半径)」：
   投影半径放大容差、半径为 0 时退回固定容差、边界含、中心更近者赢、负半径不放大容差。
   `StellarEra.StarMap.SilhouetteRadiusScale` 测球体轮廓系数这个纯函数：`d=2R` 得 `2/√3`、
   `d=10R` 得 `10/√99`（都用 1e-9 浮点容差），以及 `d=R`、相机在球内、半径非正三种情形返回 0。

2. **子系统的新增查询** —— 用 `NewObject` 出来的裸实例即可，**不需要世界**：
   `SetSelectedBody` / `GetSelectedBody` 往返；`GetRegisteredBodies` 过滤掉没有 Owner 的组件。

   > **弱引用失效那条不测。** 要验「天体被销毁后 `GetSelectedBody()` 返回 `nullptr`」，
   > 得先真的把对象 GC 掉（`MarkAsGarbage` + `CollectGarbage`）—— 在自动化测试里既慢
   > 又易碎，而它依赖的 `TWeakObjectPtr::Get()` 行为是引擎保证的。花这个代价换不来什么。

**验不了、必须人看的**：相机观感、高亮够不够明显、点击手感。
按本项目惯例，这些归验收步骤的用户，**不派子代理** —— 它要么报「验不了」（白跑），
要么编一个结论（更糟）。

## 10. 已知取舍

| # | 取舍 | 说明 |
|---|---|---|
| 1 | 进入时相机原地不动、不自动框住全部天体 | 更简单、更可预测，且对上「进去之后点选」的流程。若玩家在星球表面，其它行星可能在背后 —— 未选中时左键拖拽也能转视角，转过去就能看到 |
| 2 | 点空白不取消选中 | 取消选中会让相机停止跟随、丢失目标；绑在误点很容易触发的动作上不划算 |
| 3 | 高亮只改颜色，不加粗 | 加粗会切渲染路径（§5.5），而那条路径只能人眼验 |
| 4 | 星图模式下时间照常推进 | 天体继续运动，拖尾继续积累。不暂停 —— 暂停会引入一套与子系统无关的状态 |
| 5 | 拖尾采样在普通模式下也一直跑 | 决策 2 的直接后果。成本有界（保留点数 ≤ 513），且这正是「进入星图立刻看到完整拖尾」的前提 |

## 11. 涉及文件

全部进 `Source/StellarEra/`（框架模块）—— 符合插件纯净偏好。
`StellarEra.Build.cs` **不用动**（只用已有的 Core / CoreUObject / Engine）。

| 文件 | 动作 |
|---|---|
| `Source/StellarEra/Public/StarMapCameraPawn.h` | 新建 |
| `Source/StellarEra/Private/StarMapCameraPawn.cpp` | 新建 |
| `Source/StellarEra/Public/CosmosSimulationSubsystem.h` | 加 §5.3 的五个方法 |
| `Source/StellarEra/Private/CosmosSimulationSubsystem.cpp` | 同上 |
| `Source/StellarEra/Public/StellarEraPlayerController.h` | 加两个 IA 成员、星图回调、`ToggleStarMap` |
| `Source/StellarEra/Private/StellarEraPlayerController.cpp` | §6 全部 |
| `Source/StellarEra/Public/CosmosTrajectoryComponent.h` | 高亮用的颜色函数 |
| `Source/StellarEra/Private/CosmosTrajectoryComponent.cpp` | §5.4 的 A / B 两处改动 + §5.5 高亮 |
| `Source/StellarEra/Public/CosmosOrbitComponent.h` | 加 `bShowOutsideStarMap` 属性（与其它轨迹参数同处） |

**需要重新构建**：§5.4 是对已复审的 T6 代码的**真行为改动**（不是注释），
之前记录的「DLL 行为与 T6 相同」随之作废。

## 12. 引擎事实（本设计依赖，均已核对源码）

| 事实 | 出处 |
|---|---|
| `bool ProjectWorldLocationToScreen(FVector WorldLocation, FVector2D& ScreenLocation, bool bPlayerViewportRelative = false) const` | `Runtime/Engine/Classes/GameFramework/PlayerController.h:742` |
| `EMouseCaptureMode::CaptureDuringMouseDown` = 「Capture the mouse during a mouse down, releases on mouse up」 | `Runtime/Engine/Classes/Engine/EngineBaseTypes.h:49` |
| `MouseDelta` 只在视口握着捕获时累加 | `Runtime/Engine/Private/Slate/SceneViewport.cpp:905`（判定）、`:923-933`（累加） |
| `Thickness == 0` 是行批里的真分支（1 像素线 vs 粗线） | `Runtime/Engine/Private/BatchedElements.cpp:167` / `:184` |
| `FBatchedLine` 构造参数序 `(Start, End, Color, LifeTime, Thickness, DepthPriority, BatchID)` | `Runtime/Engine/Classes/Components/LineBatchComponent.h:15-57` |
| `void DrawLines(TArrayView<FBatchedLine> InLines)` | `Runtime/Engine/Classes/Components/LineBatchComponent.h:146` |
