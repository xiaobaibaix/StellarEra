# 体素 Invoker 视点门控设计

**日期**：2026-10-29
**状态**：待评审

## 背景

体素 LOD 由「调用器（invoker）」驱动：每个 invoker 声明一个位置和一个 LOD 范围，
八叉树对与之相交的区块细分到 `LODToSet` 为止。项目里有四类持有者：

| 持有者 | 数量 | 类型 | 用途 |
|---|---|---|---|
| `AVoxelPlanetActor` | 每星 1 个 | `UVoxelSimpleInvokerComponent` | 行星基础 LOD，覆盖全球 |
| `APlanetCharacter` | 1 个 | `UAlwaysLocalVoxelInvokerComponent` | 脚下 200 m 精修 + 碰撞 |
| `ASpaceshipPawn` | 3 个（LOD1/3/5） | `UAlwaysLocalVoxelInvokerWithPredictionComponent` | 飞船周围三档 |
| `AStarMapCameraPawn` | 3 个（LOD1/3/5） | `UAlwaysLocalVoxelInvokerComponent` | 星图相机周围三档 |

插件本身有一个正确的默认语义（`VoxelInvokerComponent.cpp:21`）：

```cpp
auto* Owner = Cast<APawn>(GetOwner());
return !Owner || Owner->IsLocallyControlled();
```

即「owner 不是 Pawn 就恒为真（行星），是 Pawn 就只在被本地控制器 Possess 时才算」——
**恰好就是「只有当前视点生效」**。

本项目用 `UAlwaysLocalVoxelInvokerComponent` 把它覆写成恒真，理由是：进星图等于
`Possess(星图相机)`，原版会让飞船与角色的 `bUseForLOD` 在同一帧翻转，
`VoxelDefaultLODManager.cpp:235` 判定设置变化后触发整树 LOD 重建，
此时若行星还在生成就会踩中 VoxelFree 的 chunk 簿记 ensure。

代价是**所有 invoker 永久参与**。2026-10-29 的 PIE 日志实测：

```
PIE 启动：3× PlanetLODInvoker + 3× 飞船 + 1× 角色 = 7 个 enabled
12:00:51  角色 VoxelInvoker disabled      ← 上船，全场唯一一条 disabled
12:01:02  星图 3 个 enabled               ← 进星图后 9 个同时参与
```

星图里看不见飞船和角色，但它们脚下的精细块仍在生成。

## 目标

只有**当前被本地控制器 Possess 的 Pawn** 的 invoker 参与 LOD 与碰撞。
行星自己的 invoker 不受影响。

## 非目标

- 不改任何 `LODToSet` / `LODRange` / `CollisionsRange` 的取值。
- 不删除 `AlwaysLocalVoxelInvokerComponent`。它保留为回滚开关：把门控关掉即回到今天的状态。
- 不解决 VoxelFree 的 chunk 簿记 ensure。若它在本改动后重新出现，按**回滚**处理（见「风险与回滚」）。

## 方案

保留 `AlwaysLocal`（它保证「被启用即参与」，不引入第二套语义），
在**换视点**这一处显式开关 invoker。

### 新接口

`Source/StellarEraTerrain/Public/VoxelViewpointInvokers.h`：

```cpp
UINTERFACE(MinimalAPI)
class UVoxelViewpointInvokers : public UInterface
{
	GENERATED_BODY()
};

/**
 * 由「持有体素 invoker 的 Pawn」实现。
 *
 * 契约：bActive = true 表示本 Pawn 是当前视点。实现必须
 *   1. 幂等 —— 重复调用同一值不产生副作用，也不触发插件的
 *      FVoxelMessages::Error("Invoker already enabled/disabled")；
 *   2. 记住最后一次的值，BeginPlay 时按它重新应用（组件在 OnRegister
 *      里自动启用，早于 Possess，见 VoxelInvokerComponent.cpp:83）。
 */
class STELLARERATERRAIN_API IVoxelViewpointInvokers
{
	GENERATED_BODY()

public:
	virtual void SetViewpointInvokersActive(bool bActive) = 0;
};
```

放在 `StellarEraTerrain`：模块依赖表中 Character / Spaceship / UI / 主模块都依赖它，
且 `AlwaysLocalVoxelInvokerComponent` 已经在这里。

### 各 Pawn 的实现

| Pawn | 目标状态 | 备注 |
|---|---|---|
| `APlanetCharacter` | `GetAttachParentActor() == nullptr && bIsViewpoint` | 与现有 `SetBoarded` 的开关合并成**一个**谓词。登船状态没有成员变量，唯一口径是挂接关系（`PlanetCharacter.cpp:375`），不要新增 `bBoarded` 成员 |
| `ASpaceshipPawn` | `bIsViewpoint` | 高速门控只改 `bUseForCollisions`，与整体启停正交，不参与合并 |
| `AStarMapCameraPawn` | `bIsViewpoint` | 三个 invoker 同进同出 |

每个 Pawn 增加：

- 私有 `bool bIsViewpoint = false;`
- 私有 `void ApplyViewpointInvokerState();` —— 按上面的谓词，对各自的
  invoker 逐个 `IsInvokerEnabled()` 判断后调用 `EnableInvoker()` / `DisableInvoker()`
- `BeginPlay()` 末尾调一次 `ApplyViewpointInvokerState()`（覆盖「生成后、
  首次 Possess 前」的窗口 —— 飞船就是在这个窗口里一直开着的）

`APlanetCharacter::SetBoarded` 改为设置 `bBoarded` 后调 `ApplyViewpointInvokerState()`，
不再自己判断开关。

### 驱动力

`AStellarEraPlayerController::OnPossess` 是所有换 Pawn 的唯一收口
（登船 / 离船 / 进出星图 / `DebugPossessShip`）。在 `Super::OnPossess` 之后、
`ApplyMouseModeForPawn` 之前：

```cpp
for (TActorIterator<APawn> It(GetWorld()); It; ++It)
{
	APawn* Pawn = *It;
	if (Pawn->Implements<UVoxelViewpointInvokers>())
	{
		IVoxelViewpointInvokers::Execute_SetViewpointInvokersActive(Pawn, Pawn == InPawn);
	}
}
```

`OnPossess` 是低频事件，遍历全部 Pawn 的开销可忽略。

## 数据流

```
RestartPlayer / 登船 / 进出星图 / DebugPossess
    → AStellarEraPlayerController::OnPossess(InPawn)
        → Super::OnPossess（控制器已指派）
        → ExitMineralViewIfActive
        → 【新】遍历 Pawn，只把 InPawn 置为视点
             → 各 Pawn::SetViewpointInvokersActive(bool)
                 → 记录 bIsViewpoint，ApplyViewpointInvokerState()
                     → EnableInvoker / DisableInvoker（幂等）
        → ApplyMouseModeForPawn
```

invoker 集合变化后，`FVoxelDefaultLODManager` 会置 `bLODUpdateQueued`，
在下一次 `Task->IsDone()` 且超过 `MinDelayBetweenLODUpdates` 时执行
（`VoxelDefaultLODManager.cpp:286`）—— 不是同步卡死。

## 边界情况

1. **重复调用**：`EnableInvoker` 在已启用时会调
   `FVoxelMessages::Error`。实现必须先 `IsInvokerEnabled()` 判断。
2. **组件自动启用**：`OnRegister` 里 `bStartsEnabled` 会启用组件，早于 Possess。
   所以 `BeginPlay` 必须按 `bIsViewpoint` 重新应用一次，否则新生成的飞船会一直开着。
3. **角色同时受登船与视点两条规则约束**：合成单一谓词
   `GetAttachParentActor() == nullptr && bIsViewpoint`，避免 `SetBoarded(false)`
   把非当前视点的角色重新打开。登船状态没有成员变量，必须现查挂接关系 ——
   新增 `bBoarded` 成员会与真实挂接状态漂移（`SetBoarded` 只应用效果，不改挂接）。
4. **`InPawn` 为空**（`OnUnPossess` 路径）：循环仍会执行，全部置 false —— 正确。
5. **行星 invoker**：不实现接口，永远不受影响。

## 验证

**自动化**（`StellarEra.Planet.InvokerViewpoint.*`）：

- 生成一个未 Possess 的 `ASpaceshipPawn`，BeginPlay 后其 3 个 invoker 均未启用。
- `SetViewpointInvokersActive(true)` 后均启用；再调一次 true 不报错且仍启用。
- `SetViewpointInvokersActive(false)` 后均未启用；再调一次 false 不报错。
- 角色：把角色挂到另一个 Actor 上（模拟登船）后 `SetViewpointInvokersActive(true)`
  → 仍未启用（谓词是「与」）；取消挂接后再调一次 → 启用。

**PIE**（`UniverseMap`，编辑器置前台）：

- 日志里 `Voxel Invoker enabled/disabled` 的行数应与视点切换次数吻合，而不是只有角色一条。
- 进星图前后对比 HUD 的 `Total tasks remaining` 与 FPS。
- **grep 日志确认 ensure 条数没有增加**。

## 风险与回滚

| 风险 | 应对 |
|---|---|
| VoxelFree 的 chunk 簿记 ensure 重新出现 | 立即回滚本改动（门控逻辑集中在一个接口 + 一个循环，回滚 = 删掉 `OnPossess` 里那几行） |
| 切回视点时出现可见卡顿 | 用户已接受。若卡顿过大，退化为「只停飞船与星图，角色常开」 |
| 新生成的 Pawn 未被门控 | `BeginPlay` 重新应用 + 自动化测试覆盖 |

## 不做的事

- 不动行星 invoker 的配置。
- 不合并 `AlwaysLocal` 与手动开关（保留两层，注释写明「参与 = 被启用 ∧ AlwaysLocal」）。
- 不在本次改动里处理星图相机的 LOD3/LOD5 档取值。
