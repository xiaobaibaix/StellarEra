# 输入抽成组件 + 星图相机搬进 Pawns — 设计文档

- **日期**：2026-11-06
- **项目**：StellarEra（Unreal Engine 5.8）
- **状态**：设计已批准，待生成实现计划
- **前置文档**：`docs/ARCHITECTURE.md`、`Source/README.md`

---

## 1. 背景

上一轮把输入绑定从 `AStellarEraPlayerController` 下沉到了 Pawn（引擎的
`SetupPlayerInputComponent` 天然做到「谁被 Possess 谁响应」）。那次之后还留两个尾巴：

1. **回调挤在 Pawn 里**。`APlanetCharacter` / `ASpaceshipPawn` 各自多了七八个
   `On*Input` 回调，加上只服务输入的状态（`LookSensitivity`、`bInvertLookY`、
   `bLookHeld`、右键按下位置）。这些既不是角色玩法也不是飞船物理，却占着两个
   大类头的篇幅。
2. **星图相机仍在 `StellarEraUI`**。它只依赖 Planet 与 Cosmos，一行 UI 的头都没碰，
   放在 UI 纯粹是历史原因。搬不动的唯一障碍是 `StellarEraCosmos → StellarEraPawns`
   这笔债：`CosmosSimulationSubsystem.cpp` 4 处、`CosmosTrajectoryComponent.cpp` 2 处
   `Cast<ASpaceshipPawn>`。搬过去就是 `Pawns → Cosmos → Pawns`。

### 1.1 代码侧现状（2026-11-06 核对）

| 事实 | 值 |
|---|---|
| Cosmos 里的 `Cast<ASpaceshipPawn>` | 6 处（子系统的 145 / 216 / 395 / 660 行，轨迹组件的 302 / 1419 行） |
| Cosmos 用到的飞船成员 | 8 个，**全部已是公开 API**（见 §4.2） |
| 相机行数 | `StarMapCameraPawn.h` 575 + `.cpp` 793 |
| 相机对 UI 的符号依赖 | 0（HUD 反向 include 相机，方向已正确） |
| 相机对 Pawns 的符号依赖 | 0 |
| 输入回调对 Pawn 私有成员的直接访问 | 0（全部走 `Move`/`Look`/`Jump`/`SetSprinting`/`SetThrust`/`SetRoll`/`SetVertical`/`SetBoost`/`Toggle*`/`AddZoom`/`ShowNotice`） |

---

## 2. 目标

- 角色 / 飞船的输入回调与输入状态搬出 Pawn，各自成为一个可独立阅读的类。
- `StellarEraCosmos` 不再认识任何具体 Pawn 类型；`AStarMapCameraPawn` 搬进
  `StellarEraPawns`。
- 运行行为不变：按键、灵敏度默认值、编辑点放阈值、巡航/调试视角提示全部照旧。

## 3. 非目标

- 不改键位映射（`FStellarEraInputActions::Build` 与 `IMC_StellarEra` 不动）。
- 不把输入类做成蓝图玩法部件（不进 SCS、不加蓝图可调的新开关）。
- 不动星图相机的位置/缩放算法，只搬模块。
- 不给相机新增输入绑定：星图鼠标仍绑在控制器上（星图是控制器的模式，不是 Pawn 的行为）。
- 不改 `StellarEraUI → StellarEraPawns` 这条既有依赖。

---

## 4. 设计

### 4.1 输入组件

```
StellarEraPawns/Src/Character/PlanetCharacterInputComponent.h/.cpp
StellarEraPawns/Src/Spaceship/SpaceshipInputComponent.h/.cpp
```

`UActorComponent` 子类，由 Pawn 在构造函数里 `CreateDefaultSubobject` 建立，
随 Pawn 生灭。职责：

- 持有该类 Pawn 的全部 `On*Input` 回调；
- 持有只服务输入的状态（灵敏度、反 Y、鼠标按住标志、右键按下位置）；
- `Bind(UInputComponent* PlayerInputComponent)` 完成全部绑定。

Pawn 侧只剩转发：

```cpp
void APlanetCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
    Super::SetupPlayerInputComponent(PlayerInputComponent);
    PlayerInput->Bind(PlayerInputComponent);   // UPlanetCharacterInputComponent*
}
```

绑定目标从 Pawn 换成组件自身：

```cpp
EnhancedInput->BindAction(Actions.Move, ETriggerEvent::Triggered,
    this, &UPlanetCharacterInputComponent::OnMoveInput);
```

**为什么是 UObject 组件而不是普通 C++ 类**：`BindAction` 的成员函数重载要求接收者是
UObject（换成 lambda 会丢掉 GC 安全与反射签名检查）；组件的生命周期与所属 Pawn 天然
一致，灵敏度也还能按实例在细节面板里调，与现状等价。

**为什么不是自定义 `UInputComponent` 子类**：那需要覆写 `APawn::CreatePlayerInputComponent`，
把 Pawn 和输入再缠回一起；本方案的组件可以脱离 Pawn 单独测试（CDO 上就能断言绑定表）。

### 4.2 `ICosmosBody`：把 Cosmos 与具体 Pawn 解耦

新文件 `Source/StellarEraCosmos/Src/CosmosBody.h`（命名跟 `VoxelViewpointInvokers.h`、
`StarMapUIInputs.h` 一致：文件名不带 `I` 前缀，声明 `UCosmosBody` + `ICosmosBody`）。

八个纯虚方法，签名与 `ASpaceshipPawn` 现有公开方法**逐字对应**：

| 方法 | 用途（Cosmos 侧调用点） |
|---|---|
| `float GetContactRadiusCm() const` | 兜底接触半径（子系统 145 行、轨迹组件 1419 行） |
| `const TArray<FVector>& GetContactProbesLocalCm() const` | 船体凸包探针（子系统 216 行起） |
| `AVoxelPlanetActor* GetPlanetActor() const` | 找参考行星的速度（子系统 395 行起） |
| `void SetReferencePlanetVelocity(const FVector&)` | 大气阻力按相对速度算 |
| `FVector GetThrustAccelerationWorldCmPerS2(float DeltaSeconds) const` | 把玩家的推力转成求解器的外力 |
| `float GetDampingTime() const` | 阻尼项 |
| `void SetCosmosVelocity(const FVector&)` | 求解器速度回写（子系统 660 行） |
| `bool IsDebugViewMode() const` | 合力箭头的门控（轨迹组件 302 行） |

`IsLocallyControlled()` 不进接口 —— 它是 `APawn` 的，调用方 `Cast<APawn>` 即可。

`ASpaceshipPawn` 继承 `ICosmosBody`，现有方法声明加 `override`，函数体一行不改。

Cosmos 侧：6 处 `Cast<ASpaceshipPawn>` → `Cast<ICosmosBody>`，
`#include "SpaceshipPawn.h"` → `#include "CosmosBody.h"`。

### 4.3 相机搬家

```
Source/StellarEraUI/Src/StarMapCameraPawn.h/.cpp
  → Source/StellarEraPawns/Src/StarMap/StarMapCameraPawn.h/.cpp

Source/StellarEraUI/Src/Tests/StarMapCameraInvokerTests.cpp
  → Source/StellarEraPawns/Src/Tests/StarMapCameraInvokerTests.cpp
```

`Src/StarMap/` 是新功能子目录（README 的规矩：新增功能加子目录，`.h`/`.cpp` 并列）。
测试跟着被测类走，放 Pawns 的 `Src/Tests/`。

### 4.4 依赖矩阵变化

```
改前：Cosmos → Pawns, Planet        （Pawns → Planet, Voxel）
改后：Cosmos → Planet
      Pawns  → Cosmos, Planet, Voxel
```

两条边必须在**同一个改动**里落地：只加不下、只下不加都不行 —— 前者是模块环，
UBT 直接报错。

无环校验：`UI → Pawns, Cosmos, Planet`；`Game → UI, Pawns, Cosmos, Planet`；
`Cosmos → Planet`；`Pawns → Cosmos, Planet`；`Planet → Voxel`。不存在反向边。

### 4.5 资产重定向

`Content` 里有 `BP_StarMapCameraPawn`（`BP_VoxelPlanet` 的 Child Actor 组件类），
它存的是旧脚本路径。`Config/DefaultEngine.ini` 的 `[CoreRedirects]` 追加：

```ini
+ClassRedirects=(OldName="/Script/StellarEraUI.StarMapCameraPawn",NewName="/Script/StellarEraPawns.StarMapCameraPawn")
```

## 5. 影响面

| 文件 | 改动 |
|---|---|
| `StellarEraCosmos/Src/CosmosBody.h` | 新增（接口） |
| `StellarEraCosmos/Src/CosmosSimulationSubsystem.{h,cpp}` | 4 处 Cast、include、注释 |
| `StellarEraCosmos/Src/CosmosTrajectoryComponent.cpp` | 2 处 Cast、include |
| `StellarEraCosmos/StellarEraCosmos.Build.cs` | 删 `StellarEraPawns` |
| `StellarEraPawns/StellarEraPawns.Build.cs` | 加 `StellarEraCosmos`（Public） |
| `StellarEraPawns/Src/Spaceship/SpaceshipPawn.{h,cpp}` | 继承接口；输入回调搬走 |
| `StellarEraPawns/Src/Character/PlanetCharacter.{h,cpp}` | 输入回调搬走 |
| `StellarEraPawns/Src/{Character,Spaceship}/*InputComponent.{h,cpp}` | 新增 |
| `StellarEraPawns/Src/StarMap/StarMapCameraPawn.{h,cpp}` | 从 UI 移入 |
| `StellarEraPawns/Src/Tests/StarMapCameraInvokerTests.cpp` | 从 UI 移入 |
| `StellarEraUI/StellarEraUI.Build.cs` | 删相机的说明段，保留 Pawns 依赖 |
| `Config/DefaultEngine.ini` | ClassRedirect |
| `Source/README.md`、`docs/ARCHITECTURE.md`、`CLAUDE.md` | 更新依赖矩阵与「相机为何在 UI」的说明 |

## 6. 验证

1. 构建 `StellarEraEditor Win64 Development` → `Result: Succeeded`。
2. `Automation RunTests StellarEra` → 228 项，除既有 `StellarEra.Planet.AtmosphereMaterial.Structure`
   外全过（含星图 12 项、相机调用器回归 1 项）。
3. 无头 `-game -nullrhi /Game/Code/Maps/UniverseMap`：
   - `[Input] ASpaceshipPawn(...) 已绑定飞船输入`
   - `[Cosmos] 初始化完成：5 个天体`
   - 无 `绑定失败`、无 `Failed to enter`
4. `-ExecCmds="DebugDisembark"` 再跑一次：`[Input] APlanetCharacter(...) 已绑定角色输入`。
5. 编辑器打开 `UniverseMap`：`BP_StarMapCameraPawn` 能正常加载（重定向生效），
   按 M 进星图不报缺类。

## 7. 风险

| 风险 | 处置 |
|---|---|
| 相机搬迁漏改某个 include，只在 UI 里编译过 | 构建即暴露（C1083 / LNK2019） |
| `BindAction` 的接收者从 Pawn 换成组件后，回调里取 Controller 的路径变了 | 组件通过 `GetOwner()` 拿 Pawn 再取 Controller，与原来同一对象 |
| 蓝图资产仍存旧类路径 | ClassRedirect；验证第 5 步专门看它 |
| 组件构造函数里建默认子对象，蓝图子类继承后出现重复 | 只在 C++ 构造函数 `CreateDefaultSubobject`，蓝图不另挂 |
