# 行星角色与飞船 — 设计文档

- **日期**：2026-10-13
- **项目**：StellarEra（Unreal Engine 5.8）
- **状态**：设计已批准，待生成实现计划

---

## 1. 目标

在体素球体星球上实现《戴森球计划》伊卡洛斯式的玩法：一个角色能在球面行走、跳跃，走到飞船旁登船，飞船以街机手感在太空中飞行，落地后可离船继续行走。

## 2. 范围

### 本次做

- 角色在球面上的行走 / 跳跃 / 第三人称相机
- 飞船的街机式太空飞行
- 登船 / 离船
- Enhanced Input 绑定（C++ 运行时构造，不依赖手工建资产）
- 两个已下载模型的导入与基础设置

### 本次明确不做

- **角色骨骼动画** —— 模型有骨骼但零动画资源，角色以滑行呈现
- 多星球 / 星际跃迁 / 对接
- 战斗、建造、资源采集
- 第一人称视角
- 网络复制（单机）
- 存档

---

## 3. 关键前置发现

这两条决定了整个架构，先记录依据。

### 3.1 UE 5.8 原生支持自定义重力方向

`UCharacterMovementComponent::SetGravityDirection(const FVector& GravityDir)` 是一等 API
（`Engine/Source/Runtime/Engine/Classes/GameFramework/CharacterMovementComponent.h:1711`），
`BlueprintCallable`，且带网络校正（`bNetworkGravityDirectionChanged`，见 `CharacterMovementComponent.cpp:2336/2361`
的 `-GetGravityDirection() * GetGravityZ()`）。

配套 API：`GetGravityDirection()`、`HasCustomGravity()`、`GetWorldToGravityTransform()`、
`GetGravityToWorldTransform()`、`ProjectToGravityFloor()`、`GetGravitySpaceZ()`。

**结论：不需要手写球面物理。** CMC 的落地判定、斜坡滑动、台阶、跳跃在"重力空间"里自动成立，
我们只需每帧喂给它一个指向球心的重力方向。

### 3.2 碰撞需要角色自带 Invoker

- `AVoxelWorld::bEnableCollisions` 默认 `true`，`CollisionTraceFlag = CTF_UseComplexAsSimple`
  （`Plugins/VoxelFree/Source/Voxel/Public/VoxelWorld.h:457/467`），星球默认有碰撞。
- 但 `AVoxelPlanetActor` 的 LOD Invoker 显式设了 `bUseForCollisions = false`
  （`Source/StellarEra/Private/VoxelPlanetActor.cpp:47`），注释写明碰撞 Invoker 应由贴近表面的 Pawn 携带。

**结论：角色必须挂 `UVoxelInvokerComponent` 且 `bUseForCollisions = true`**，否则脚下不生成碰撞体。

---

## 4. 架构

三个新类，全部放在 `Source/StellarEra/`（遵守"自定义代码不进第三方插件目录"的既有约定）。

```
AStellarEraPlayerController ── 持有并切换 ──> APlanetCharacter
        │                                    ASpaceshipPawn
        │
        └─ 构造并注册 Enhanced Input (IMC / IA)

APlanetCharacter ── 查询 ──> AVoxelPlanetActor（球心与半径）
                 ── 自带 ──> UVoxelInvokerComponent（碰撞 + LOD）
                 ── 自带 ──> USpringArmComponent + UCameraComponent

ASpaceshipPawn   ── 自带 ──> UFloatingPawnMovement
                 ── 自带 ──> USphereComponent（登船触发区）
                 ── 自带 ──> USpringArmComponent + UCameraComponent
```

### 依赖方向

- 角色**只依赖 `AVoxelPlanetActor` 的位置**，不依赖其生成器内部实现。为将来多星球留的口子：
  角色持有一个 `TObjectPtr<AVoxelPlanetActor> PlanetActor`，为空时用
  `TActorIterator<AVoxelPlanetActor>` 找最近的并缓存。
- 飞船**不依赖星球**，零重力自由飞行。
- PlayerController 是唯一的编排者：谁被 Possess、何时切换、提示何时显示。

---

## 5. 详细设计

### 5.1 APlanetCharacter —— 球面行走

**每帧（`Tick`）执行：**

1. `DirToCenter = (PlanetCenter - ActorLocation).GetSafeNormal()`
2. `CharacterMovement->SetGravityDirection(DirToCenter)`
3. **平滑对齐朝向**：目标旋转 = 让 `GetActorUpVector()` 对齐 `-DirToCenter`，同时保留当前的偏航。
   用 `FQuat::FindBetweenNormals(GetActorUpVector(), -DirToCenter)` 求出增量旋转，
   以 `RInterpTo` 的速率插值应用。**必须插值**——直接设置会在跨越地形时抽搐。
4. 移动输入走 `AddMovementInput(GetActorForwardVector(), X)` / `(GetActorRightVector(), Y)`。
   因为角色已被转正，这就是"贴着地面走"。

**关键参数（暴露为 UPROPERTY 便于调）**：`AlignSpeed`（朝向对齐速率）、`PlanetActor`。

**组件**：
- `UCapsuleComponent`（ACharacter 自带）
- `UCharacterMovementComponent` —— `bOrientRotationToMovement` 与 `bUseControllerRotationYaw`
  都会跟我们的朝向对齐打架，两者都要关掉，改为在 Tick 里手工对齐。
- `USpringArmComponent` + `UCameraComponent` —— 第三人称，臂长可调
- `UVoxelInvokerComponent` —— `bUseForCollisions = true`、`bUseForLOD = true`，
  其余用途（导航、事件、优先级）按 `VoxelPlanetActor.cpp:46-50` 的同样理由关掉

**为什么不用手写 Pawn**：手写要实现碰撞滑动、斜坡、台阶、落地检测，在体素地形上会明显不如 CMC。
原生 API 已经把这件事做完了，没有理由重造。

### 5.2 ASpaceshipPawn —— 街机式太空飞行

**移动由 `UFloatingPawnMovement` 负责**（白送碰撞扫掠、加速度、最大速度、阻尼），
街机手感写在 `Tick` 里：

| 输入 | 行为 |
|---|---|
| 鼠标 X/Y | 偏航 / 俯仰**角速度**（不是直接设朝向——角速度才有"开飞机"的惯性感） |
| `W` / `S` | 前向推力 / 制动（沿速度反向减速，不倒车） |
| `A` / `D` | 横滚 |
| `Space` / `Shift` | 升 / 降 |
| `E` | 离船（仅当已落地） |

**自动回正**：机身以可调速率缓慢对齐当前重力参考方向（太空中取世界 Z），
让飞船不会一直歪着。`AutoLevelSpeed` 可调，设为 0 即关闭。

**"已落地"判定**：速度低于 `LandingSpeedThreshold` 且向下射线命中星球碰撞体。
两个条件都满足才允许离船，避免空中下船摔死。

### 5.3 登船 / 离船

```
[角色] --走进 SphereComponent--> 显示"按 E 登船"
        --按 E--> 角色 SetActorHiddenInGame(true) + AttachToActor(飞船)
                   PlayerController->Possess(飞船)
                   HUD 提示消失

[飞船] --落地且按 E--> 角色 DetachFromActor + SetActorHiddenInGame(false)
                   角色位置 = 飞船旁（飞船位置 + 右向 * 偏移）
                   PlayerController->Possess(角色)
```

角色在船上的整个期间保持隐藏并附着，位置跟随飞船；离船时再摆到飞船旁。

### 5.4 输入（Enhanced Input）

**在 PlayerController 的 `BeginPlay` 里用 C++ 运行时构造** `UInputMappingContext` 与若干 `UInputAction`，
再 `AddMappingContext`。理由：零手工步骤，改完代码立刻能跑，不需要先在编辑器里点出一堆资产。

代价：键位暂时不能在编辑器里改。等键位定稿后，再转成 `.uasset` 资产是平滑的替换
（`UInputMappingContext` 的数据结构一样，只是来源从代码变成资产）。

**输入资产清单**：`IA_Move`(Axis2D)、`IA_Look`(Axis2D)、`IA_Jump`(Bool)、`IA_Sprint`(Bool)、
`IA_Thrust`(Axis1D)、`IA_Roll`(Axis1D)、`IA_Vertical`(Axis1D)、`IA_Interact`(Bool)。

全部 IA 注册在同一个 IMC 上，每个 Pawn 只绑定自己用得到的那几个：角色用
`IA_Move` / `IA_Jump` / `IA_Sprint`，飞船用 `IA_Thrust` / `IA_Roll` / `IA_Vertical`，
两者共用 `IA_Look` 与 `IA_Interact`。

### 5.5 提示 UI

不引入 UMG 资产。用最简单的屏幕文字（`GEngine->AddOnScreenDebugMessage` 或一个极简的 `AHUD` 子类）。
目的是让"按 E 登船"可见即可，美术表现不在本次范围。

---

## 6. 资产导入

两个模型在 Fab 的**临时**目录：
`C:\Users\23080\AppData\Local\Temp\FabLibrary\<guid>\{pomni_3d_model.glb, 12b5_atmospherhuttle002.glb}`

**这是 `Temp` 目录，会被系统清理删掉。** 导入前先把原始文件复制到工程外的
`F:\workspace\ue\StellarEra\ArtSource\`（该目录不纳入 git）。

| 模型 | 结构 | 导入要点 |
|---|---|---|
| `pomni_3d_model.glb` | 25 网格 / 16 skins / 319 节点 / 16 材质 / **0 动画** | 带骨骼；Sketchfab 导出常见 100 倍缩放与 Z-up 朝向问题，需手工校正缩放与朝向 |
| `12b5_atmospherhuttle002.glb` | 2 网格 / 0 skins / 0 动画 / 1 材质 / **149 MB** | 静态网格；体量很大，需确认面数与贴图尺寸，必要时减面 |

导入后放置到 `Content/Characters/` 与 `Content/Vehicles/`。

---

## 7. 风险

| # | 风险 | 影响 | 应对 |
|---|---|---|---|
| 1 | **碰撞 LOD 与渲染 LOD 不一致** | 角色踩的碰撞网格在轨道 LOD 下很粗，可能看起来浮在或陷进球面 | 先实测；明显的话给角色附近区块强制更细 LOD |
| 2 | 飞船 149 MB | 内存与加载时间 | 确认面数/贴图，必要时减面 |
| 3 | Pomni 缩放/朝向异常 | 角色比例或朝向错误 | 导入后手工校正 |
| 4 | 朝向对齐抖动 | 角色在球面行走时抽搐 | 对齐速率可调 + 插值；必要时加死区 |
| 5 | 角色滑行（无动画） | 观感廉价 | 已明确接受，动画后续单独做 |
| 6 | 自动回正与手动转向打架 | 飞船手感拧巴 | `AutoLevelSpeed` 可调、可关 |

---

## 8. 验收标准

1. 角色出生在球面上，**不会掉穿地面**，能在球面任意方向行走；走到"另一面"时角色仍直立（脚朝球心）。
2. 按 Space 能跳跃并落回地面，不穿模。
3. 相机跟随角色，第三人称，不穿地。
4. 走近飞船出现"按 E 登船"提示，按 E 后 Possess 飞船，角色消失。
5. 飞船能起飞、在太空中自由飞行，鼠标转向有角速度惯性，松手会衰减，机身会自动回正。
6. 飞船降落到星球表面并减速后出现"按 E 离船"，按 E 后角色出现在飞船旁并可继续行走。
7. 全程无崩溃、无 ensure、无 `LogVoxel` 错误。

---

## 9. 测试方式

- **构建**：用既有的 UBT 直调脚本 `%TEMP%\ue_mcp\ue_build.ps1`（绕过 Build.bat 的锁问题），
  构建前需关闭编辑器。
- **运行验证**：打开编辑器 → 加载 `UniverseMap` → PIE。
- **自动化**：本项目尚无测试框架，本次不引入；验收靠 PIE 手工过一遍第 8 节清单。
- **回归**：改动不得破坏既有的 `AVoxelPlanetActor` 行为（纯球体生成、材质、LOD Invoker）。
