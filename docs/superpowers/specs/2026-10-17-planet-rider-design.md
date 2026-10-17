# 行星携带角色（Planet Rider）设计

日期：2026-10-17
分支：`feat/planet-character-spaceship`
目标（用户原话）：「先不管尺度，只要不掉出去」

## 1. 现象

角色降落到行星表面后**一走就飞出去**。实测（PIE，编辑器 MCP）：

把角色传送到行星 B 表面上方，逐秒采样：

| 时刻 | 到 B 中心距离 | MovementMode | 备注 |
|---|---|---|---|
| 0.0 s | 124,110 cm | MOVE_Falling | |
| 4.0 s | 112,222 cm | MOVE_Falling | |
| 5.0 s | 101,166 cm | **MOVE_Walking** | `Velocity = (−1687510, 859275, 0)`，合 **1,893,700 cm/s** |
| 6.0 s | 1,122,905 cm | MOVE_Falling | |
| 7.0 s | 4,241,178 cm | MOVE_Falling | |
| 8.0 s | 7,582,971 cm | MOVE_Falling | 被抛到约 76 km |
| 9.0 s | 1,257,891 cm | MOVE_Falling | 回落到 12.6 km |
| 10–19 s | 1.2×10⁶ – 1.9×10⁶ cm | MOVE_Falling | 反复振荡，再未落地 |

即：**落地的同一瞬间角色速度从约 14 m/s 跳到 1.89×10⁶ cm/s，随后被抛离行星。**

基线状态（不传送）下角色**从不进入 MOVE_Walking**，与 B 的距离在 22,608–95,794 cm 之间振荡，而 B 的表面半径是 75,000 cm。

## 2. 证据状态

本文档区分三类陈述，实施时不要把第三类当第一类用：

| 类别 | 含义 |
|---|---|
| **【读码】** | 直接读 UE 5.8 引擎源码确认，附文件:行号 |
| **【实测】** | 本次会话中通过编辑器 MCP 在活体 PIE 里读到 |
| **【推断】** | 由前两类推出，**尚未直接测量** |

引擎源码根目录：`D:\Application\Epic Games\UE_5.8\Engine\Source\Runtime\Engine\`（下文 `CMC.cpp` 指 `Private/Components/CharacterMovementComponent.cpp`，`CMC.h` 指 `Classes/GameFramework/CharacterMovementComponent.h`）。

## 3. 根因

角色一旦「站在行星上」，CMC 就把行星登记为 **movement base**。引擎的 base 机制里有两条独立的行为，在「base 是一个距原点约 8×10⁵ cm、以 km/s 移动的行星」这个前提下都会造成灾难。

### 通路 1 —— 离开底座时的速度注入

【读码】

```
CMC.cpp:1442   if (MovementMode == MOVE_Walking) { ... }        // 进入 Walking 的分支
CMC.cpp:1459   if (MovementMode == MOVE_Falling)
CMC.cpp:1461       if (bStayBasedInAir == false)
CMC.cpp:1463           ApplyImpartedMovementBaseVelocity();
CMC.cpp:2744   void UCharacterMovementComponent::ApplyImpartedMovementBaseVelocity()
CMC.cpp:2746       DecayingFormerBaseVelocity = GetImpartedMovementBaseVelocity();
CMC.cpp:2747       Velocity += DecayingFormerBaseVelocity;
CMC.cpp:1193   FVector UCharacterMovementComponent::GetImpartedMovementBaseVelocity() const
CMC.cpp:1200       if (MovementBaseUtility::IsDynamicBase(MovementBaseInterfaceData))
CMC.cpp:1202           FVector BaseVelocity = MovementBaseUtility::GetMovementBaseVelocity(...);
CMC.cpp:1211-1221      // 按 bImpartBaseVelocityX/Y/Z 三个开关逐轴取用
```

- `bStayBasedInAir` 默认 **false**（`CMC.h:1445`），所以 :1463 那一支**默认就是活的**。
- `bImpartBaseVelocityX` / `Y` / `Z` / `bImpartBaseAngularVelocity` 四个开关默认**全开**（`CMC.h:937-954`），即 `GetImpartedMovementBaseVelocity()` 会原样返回**底座自己的速度**。
- `FormerBaseVelocityDecayHalfLife` 默认 **0.f**（`CMC.h:1092`）。`CMC.cpp:2753-2756` 在 `HalfLife == 0` 时立刻把 `DecayingFormerBaseVelocity` 清零 —— 也就是说**注入进来的速度不衰减，直接变成角色自己的 `Velocity`**。

**结论：角色每次从 Walking 切到 Falling，都会把行星的速度加进自己的 `Velocity`。**

量化核对（【推断】）：行星 B 的轨道速率【实测】在 1.0×10⁵ – 2.6×10⁵ cm/s 之间。而实测的落地速度是 1.89×10⁶ cm/s，是行星速度的 7–19 倍。**所以「只触发一次」不足以解释 1.89×10⁶** —— 要么角色在 Falling↔Walking 之间反复抖动、每次叠加一次（进入 Walking 时 `CMC.cpp:1445` 只做平面投影，切向速度被保留，不会清零），要么还有第二个贡献者。这一点**必须在实施前用测量确认**（见 §6 第 1 步）。

### 通路 2 —— 首次贴上底座时的瞬移

【读码】

```
CMC.cpp:2616   FVector NewWorldPos;
CMC.cpp:2617   if (HasCustomGravity())
CMC.cpp:2621       const FVector LinearDelta = NewBaseLocation - OldBaseLocation;
CMC.cpp:2622       NewWorldPos = ConstrainLocationToPlane(... + RotationDelta + LinearDelta);
CMC.cpp:2635   DeltaPosition = ConstrainDirectionToPlane(NewWorldPos - UpdatedComponent->GetComponentLocation());
CMC.cpp:2669   MoveUpdatedComponent(DeltaPosition, FinalQuat, true, &MoveOnBaseHit);
```

- `OldBaseLocation` 初值是**零向量**（`CMC.cpp:832`）。
- 它只在底座有效时才被写入：`CMC.cpp:3201 SaveBaseLocation()` → `:3209 if (MovementBaseInterfaceData && MovementBaseInterfaceData->IsValid())` → `:3212` 写入 `OldBaseLocation`。

**结论：底座第一次变有效的那一帧，`LinearDelta` 等于底座的绝对世界坐标。** 对本关卡的行星 B（世界坐标约 7.4×10⁵, 3.1×10⁵）就是单帧约 8×10⁵ cm 的位移。

本项目的 `APlanetCharacter` 有自定义重力（每帧调 `SetGravityDirection`），所以走的是 `HasCustomGravity()` 这一支（`CMC.cpp:2617-2622`）。

### 触发条件

两条通路由**同一个事件**触发：角色成为「站在行星上」。`UpdateBasedMovement` 在 `PostPhysicsTickComponent`（`CMC.cpp:1834`）里调用，跑在物理之后。

**注：此前我（助手）曾声称「`PerformMovement` 以 `Velocity = (Loc−OldLoc)/dt` 结束，底座携带的位移被折叠进速度」。这是错的** —— 那一行（`CMC.cpp:4485`）属于 `PhysFlying`（起于 `CMC.cpp:4429`）。`UpdateBasedMovement` 全程**不写 `Velocity`**，唯一的 Velocity 写入就是 :2747 的 `ApplyImpartedMovementBaseVelocity`。

### 附带：为什么基线里角色从不落地

【读码】`CMC.cpp:1468-1472`：`if (!IsFalling() || bStayBasedInAir == false) { SetBase(&EmptyMovementBaseData); }`。`bStayBasedInAir` 默认 false，所以**离开 Walking 时底座被清掉**。角色在空中只剩我们的 `UCosmosSimulationSubsystem::CarryRiders` 携带，而它受 `RideRadius = GetSurfaceRadiusCm() * RideRadiusScale` 限制，一旦漂出判定球就掉队。

## 4. 设计

保留用户方案的概念（进入引力球即与行星绑定），**换掉实现层**：绑定不再由引擎的 movement base 机制承担。

分工：

- **引擎的 movement base 只留「我脚下是什么」这层语义** —— `MOVE_Walking`、地板判定、摩擦、台阶、跳跃都靠它，这些都不动。
- **它的两条运动通路全部关掉**（通路 1 靠配置，通路 2 靠重写虚函数）。
- **`CarryRiders` 成为唯一的搬运者**，且因为通路 2 已死，不会再有「引擎搬一次 + 我们搬一次」的双重搬运。

### 改动 1 —— 关掉通路 1（配置）

在 `APlanetCharacter` 构造函数里，对 `GetCharacterMovement()` 设：

```cpp
Movement->bImpartBaseVelocityX = false;
Movement->bImpartBaseVelocityY = false;
Movement->bImpartBaseVelocityZ = false;
Movement->bImpartBaseAngularVelocity = false;
```

效果：`GetImpartedMovementBaseVelocity()`（`CMC.cpp:1195` `Result = FVector::ZeroVector` 起步，:1211-1221 三个开关全 false 则一次都不赋值）恒返回零向量 → `CMC.cpp:2747` 加的是零 → **起跳/掉落时再也不会被灌速度**。走路、摩擦、地板判定不受影响。

这四个是 `UPROPERTY(EditAnywhere, BlueprintReadWrite)`（`CMC.h:937-954`）且位于 `public:` 段（`CMC.h:928`），因此既能从 C++ 直接赋值，**也可以零重编译在 PIE 里直接改来验证**（见 §6 第 1 步）。

### 改动 2 —— 关掉通路 2（CMC 子类）

新增 `UPlanetCharacterMovementComponent : UCharacterMovementComponent`，重写 `UpdateBasedMovement(float)` 为空实现。

`UpdateBasedMovement` 是 `ENGINE_API virtual`（`CMC.h:1421`），可重写。重写成空实现后，引擎不再替底座移动胶囊；底座照旧被 `SetBase` 设置，所以 `MOVE_Walking` 与地板语义全部保留。

在 `APlanetCharacter` 里换掉 CMC 类。**注意**：`SetDefaultSubobjectClass` 不能在构造函数体内调用（`UObjectGlobals.h:1729` 明确 assert），必须写在初始化列表里，在 `Super(...)` 之前：

```cpp
APlanetCharacter::APlanetCharacter(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.SetDefaultSubobjectClass<UPlanetCharacterMovementComponent>(
		ACharacter::CharacterMovementComponentName))
{
    ...
}
```

已核实的 API 事实：
- `ACharacter::CharacterMovementComponentName` 是 `static ENGINE_API FName`（`Character.h:501`）。
- `ACharacter(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get())`（`Character.h:343`）。
- `SetDefaultSubobjectClass(FName, const UClass*)` 返回 `const FObjectInitializer&`（`UObjectGlobals.h:1459`），可直接喂给 `Super`。

因此 `APlanetCharacter` 的构造函数签名要从 `APlanetCharacter()` 改成 `APlanetCharacter(const FObjectInitializer&)`，头文件同步。

**不再需要动 `bStayBasedInAir`。** 改动 1 让 :1463 和 :2705 两处调用都变成空操作，保持默认 false 即可。

### 改动 3 —— 不动

`CarryRiders` 当前逻辑（`CosmosSimulationSubsystem.cpp:365-466`）保持不变：跳过已附着的 Pawn、判定球门控、最近携带者独占、`AddActorWorldOffset(Delta, false)`。

理由：改动 2 之后引擎不再搬运，`CarryRiders` 就是唯一搬运者，不会重复。角色站在地表时到球心距离 = 表面半径，远小于 `RideRadius = 表面半径 × RideRadiusScale`，不会掉出判定球。

已知但**本次不处理**的问题：飞船只复现行星位移的约 90%，会缓慢漂出判定球被甩下。那是独立的「粘性绑定」课题（用户方案里「进入引力球就绑定」的完整形态），见 §7。

## 5. 改动清单

| 文件 | 改动 |
|---|---|
| `Source/StellarEraVehicle/Public/PlanetCharacterMovementComponent.h` | 新增。`UPlanetCharacterMovementComponent` 类声明，`STELLARERAVEHICLE_API`，重写 `UpdateBasedMovement` |
| `Source/StellarEraVehicle/Private/PlanetCharacterMovementComponent.cpp` | 新增。空实现 |
| `Source/StellarEraVehicle/Public/PlanetCharacter.h` | 构造函数签名改为 `APlanetCharacter(const FObjectInitializer&)` |
| `Source/StellarEraVehicle/Private/PlanetCharacter.cpp` | 构造函数签名 + 初始化列表换 CMC 类 + 四个 `bImpartBase*` 开关 |

不需要改 `Build.cs`：`UCharacterMovementComponent` 属于 `Engine` 模块，`StellarEraVehicle` 已依赖。

不需要改任何第三方插件目录。

## 6. 验证

**前置：需要先启动编辑器**（上一会话结束时编辑器进程已不在运行，8000 端口无监听）。

1. **先零重编译验通路 1。** 起 PIE，用 MCP 在角色的 CMC 实例上把这四个 `bImpartBase*` 设成 false，再跑一次传送测试。这能**单独判定通路 1 是否就是 1.89×10⁶ 的来源**：
   - 若落地后 `Velocity` 回到 10³ 量级且能停在 `MOVE_Walking` → 通路 1 是主因。
   - 若仍被抛飞 → 通路 1 只是部分原因，`LinearDelta`（通路 2）也在贡献，改动 2 必需；此时要额外确认 :2621 的 `OldBaseLocation` 是否为零向量。

2. **重编译后复测。** 改动 1+2 都进 C++ 构造函数，必须重编译（且重编译前要关编辑器 —— Live Coding 对构造函数默认值不生效）。

3. **通过判据**（同 probe15 的传送测试口径，采样 ≥20 s）：
   - `MovementMode` 能**稳定停在** `MOVE_Walking`；
   - `Velocity` 模长始终在 10³ cm/s 量级，不出现 10⁵ 以上；
   - 到行星 B 中心的距离**稳定在** 75,000 + 胶囊半高（88）= 75,088 cm 附近；
   - 按住移动键行走时角色贴在球面上，不被甩出。

4. **回归**：从行星上跳一下，应落回地面而不是被发射。

## 7. 非目标 / 未决

- **整体尺度问题**（行星轨道速率 km/s 与飞船速度、TimeScale 的关系）用户明确推迟，本文档不涉及。
- **粘性绑定**：`CarryRiders` 目前每帧重算「最近携带者」，飞船会缓慢漂出判定球被甩下。完整形态是「进入引力球才获取，获取后保持到离开球体」。属独立课题。
- **角色如何到达地表**：关卡里存在 PlayerStart，于是 `bSpawnedAtPlayerStart = true`，`SnapToPlanetSurface()` 与 Tick 里的兜底回贴**都被跳过**；而那个 PlayerStart 距行星 B 约 18 km。这是与本文档独立的一件事，需单独决定（出生即贴球面 vs 把出生点挪进引力球）。
- **`LookSensitivity`**（当前 1.2，约快 7 倍）此前提出过，与本文档无关。
