# 行星角色改为挂接到行星 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 把行星角色的位移来源从 `UCosmosSimulationSubsystem::CarryRiders` 的每帧推位移，改成「角色 `AttachToActor` 到行星，由附着关系带着走」。

**Architecture:** 角色的 `AttachParent` 从「只有在船上才非空」变成「要么是飞船、要么是行星」。因此「是否在船上」必须从附着父级推断改成显式的 `bBoarded` 成员——这是整个改动的前提，也是唯一真正的拦路点。搬运仍只有一处：附着关系。`UPlanetCharacterMovementComponent::UpdateBasedMovement` 继续空实现（引擎那套底座携带与附着叠加会把星球位移加两次），`bImpartBaseVelocity*` 继续关闭。

**Tech Stack:** UE 5.8.3 / C++20 / 5 个模块（StellarEra → Pawns/UI/Cosmos/Planet）。

**Spec:** `docs/superpowers/specs/2026-10-17-planet-rider-design.md`（现有搬运机制的设计记录；本计划改动其中「搬运交给 CarryRiders」这一条）

## Global Constraints

- 含中文的 `.h`/`.cpp` 存 **UTF-8 with BOM**。
- 不要统一换行符：仓库是 CRLF/LF 混合，`autocrlf=true` 下 CRLF 是稳定态。
- 提交必须带 pathspec（索引里有常驻暂存文件）：`git commit -- <paths>`。
- 构建必须用捆绑 dotnet 直调 UBT，cwd 在项目外；不要用 `Build.bat`。
- 本机 `Binaries` 必须是指向 `BinariesReal` 的目录联接，否则链接阶段必报 LNK1201。
- 测试名前缀 `StellarEra.`；测试文件只能放 `Src/Tests/`，且必须是所引头文件的下游模块。
- 新增测试放 `Source/StellarEra/Src/Tests/`（装配层，可同时看见 Pawns 与 Cosmos）。

---

## 执行前必读：本 worktree 无法构建

这个 worktree 会话里 `cmd` / `dotnet` / `bash 脚本` 全部被工作区隔离守卫拒绝，因此 **`Binaries` 联接建不了、UBT 跑不了、自动化测试跑不了**。

验证必须在主检出做：

```bash
# 1. 在 worktree 里改完并提交
# 2. ExitWorktree（keep）
# 3. 主检出切到同一分支
git switch worktree-planet-rider-attach
# 4. 构建 + 跑测试 + PIE，见 Task 5
```

如果嫌两处跑太啰嗦，可以放弃 worktree，直接在主检出开分支 `feat/planet-rider-attach`——隔离效果一样，工具链完整。

---

## File Structure

| 文件 | 职责 | 本计划改动 |
|---|---|---|
| `Source/StellarEraPawns/Src/Character/PlanetCharacter.h` | 角色公开接口与成员 | 新增 `IsBoarded()`、`SetRidePlanet()`、`bBoarded` |
| `Source/StellarEraPawns/Src/Character/PlanetCharacter.cpp` | 角色 Tick / 重力 / 附着 | 三处判据替换、`SetRidePlanet` 实现、构造函数加绝对缩放、`BeginPlay`/`SetBoarded`/`ReevaluatePlanet` 接线 |
| `Source/StellarEra/Src/StellarEraPlayerController.cpp` | 上下船流程 | `LeaveShip` 里重新挂回行星；注释更新 |
| `Source/StellarEra/Src/Tests/PlanetRiderAttachTests.cpp` | 新增自动化测试 | 新建 |
| `Source/StellarEraCosmos/Src/CosmosSimulationSubsystem.cpp` | `CarryRiders` | 只改注释：契约从「唯一搬运」变成「兜底」 |
| `Source/StellarEraPawns/Src/Character/PlanetCharacterMovementComponent.h` | 移动组件头注释 | 说明为什么仍然空实现 |
| `CLAUDE.md` | 架构约束 | 「骑乘搬运统一交给 `CarryRiders`」这条要改 |

---

### Task 1: 用显式 `bBoarded` 取代「附着父级 = 在船上」判据

这是整个改动的前提。做完这一步角色行为**完全不变**（此时还没有任何代码把角色挂到行星下），但判据已经从附着关系解耦，后面才敢改附着。

**Files:**
- Modify: `Source/StellarEraPawns/Src/Character/PlanetCharacter.h:63` 附近
- Modify: `Source/StellarEraPawns/Src/Character/PlanetCharacter.cpp:368`、`:428-437`、`:646-675`
- Test: `Source/StellarEra/Src/Tests/PlanetRiderAttachTests.cpp`（新建）

**Interfaces:**
- Produces: `bool APlanetCharacter::IsBoarded() const`、私有成员 `bool bBoarded`

- [ ] **Step 1: 写失败的测试**

新建 `Source/StellarEra/Src/Tests/PlanetRiderAttachTests.cpp`（UTF-8 with BOM）：

```cpp
// 骑乘/登船的状态判据。
//
// 背景：登船期间角色的 AttachParent 是飞船，「有没有附着父级」曾经等价于
// 「在不在船上」。改成骑乘之后角色的 AttachParent 平时就是行星，那个等价
// 关系不成立了，判据必须改成显式的 bBoarded。本文件是那条判据的守门人。

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "Engine/World.h"

#include "PlanetCharacter.h"
#include "SpaceshipPawn.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	/** 一次性编辑器世界，析构时销毁。照抄 VoxelViewpointInvokerTests.cpp 的写法。 */
	struct FScopedRiderTestWorld
	{
		UWorld* World = nullptr;

		bool Create()
		{
			World = UWorld::CreateWorld(EWorldType::Editor, false);
			if (!World)
			{
				return false;
			}
			FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Editor);
			Context.SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			return true;
		}

		~FScopedRiderTestWorld()
		{
			if (World)
			{
				GEngine->DestroyWorldContext(World);
				World->DestroyWorld(false);
			}
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraPlanetBoardedFlagTest,
	"StellarEra.Planet.BoardedFlag",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraPlanetBoardedFlagTest::RunTest(const FString& Parameters)
{
	FScopedRiderTestWorld Scoped;
	if (!TestTrue(TEXT("测试世界"), Scoped.Create()))
	{
		return false;
	}

	ASpaceshipPawn* Ship = Scoped.World->SpawnActor<ASpaceshipPawn>(
		ASpaceshipPawn::StaticClass(), FTransform::Identity);
	APlanetCharacter* Character = Scoped.World->SpawnActor<APlanetCharacter>(
		APlanetCharacter::StaticClass(), FTransform::Identity);
	if (!TestNotNull(TEXT("飞船"), Ship) || !TestNotNull(TEXT("角色"), Character))
	{
		return false;
	}

	TestFalse(TEXT("出生时不登船"), Character->IsBoarded());

	// 与 BoardShip 同序：先挂后设。
	Character->AttachToActor(Ship, FAttachmentTransformRules::KeepWorldTransform);
	Character->SetBoarded(true);
	TestTrue(TEXT("登船后 IsBoarded 为真"), Character->IsBoarded());

	// 与 LeaveShip 同序：先摘后设。
	Character->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
	Character->SetBoarded(false);
	TestFalse(TEXT("离船后 IsBoarded 为假"), Character->IsBoarded());

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
```

- [ ] **Step 2: 跑测试确认失败**

（构建与测试只能在主检出做，见「执行前必读」。）

```bash
"D:/Application/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" \
  "<项目>/StellarEra.uproject" \
  -ExecCmds="Automation RunTests StellarEra.Planet.BoardedFlag" \
  -TestExit="Automation Test Queue Empty" \
  -unattended -nopause -nullrhi -nosplash -stdout -abslog="<临时日志>"
```

Expected: FAIL —— 编译错误 `APlanetCharacter has no member named 'IsBoarded'`。

- [ ] **Step 3: 加成员与访问器**

`PlanetCharacter.h`，在 `void SetBoarded(bool bBoarded);`（约 :63）之后加：

```cpp
	/** 是否在船上。**不要**再用 `GetAttachParentActor() != nullptr` 判断：
	 *  骑乘时角色的附着父级是行星，那条式子恒为真。 */
	bool IsBoarded() const { return bBoarded; }
```

在私有段（`bSpawnedAtPlayerStart` 附近，约 :274）加：

```cpp
	/** 登船状态。SetBoarded 是唯一写入口。 */
	bool bBoarded = false;
```

- [ ] **Step 4: 把三处判据换成成员**

`PlanetCharacter.cpp:646` 的 `SetBoarded` 开头加一行赋值。**注意参数名冲突**：成员与入参同名会导致自赋值，所以签名同时改名：

```cpp
void APlanetCharacter::SetBoarded(bool bInBoarded)
{
	bBoarded = bInBoarded;
```

（头文件里的声明同步改成 `void SetBoarded(bool bInBoarded);`。）

`PlanetCharacter.cpp:368`（`BeginPlay`）：

```cpp
	ApplyViewpointInvokerState(GetAttachParentActor() != nullptr);
```
改为

```cpp
	ApplyViewpointInvokerState(bBoarded);
```

`PlanetCharacter.cpp:428`（`Tick`）：

```cpp
	// 登船期间不重评：角色此刻挂在飞船下跟着飞船跨星球移动，重评会把角色从
	// 船里瞬移到新星的地表上。本项目只有一处 AttachToActor（登船，见
	// StellarEraPlayerController.cpp），所以附着关系就是「在船上」。
	const bool bBoarded = GetAttachParentActor() != nullptr;
```
改为

```cpp
	// 登船期间不重评：角色此刻挂在飞船下跟着飞船跨星球移动，重评会把角色从
	// 船里瞬移到新星的地表上。
	//
	// 判据是 bBoarded，**不是** GetAttachParentActor()：骑乘之后角色的附着
	// 父级平时就是行星，用父级判断会把「在地上走路」当成「在船上」。
```

（局部变量删掉，下面 `if (!bBoarded && ...)` 直接读成员，名字不变。）

`PlanetCharacter.cpp:674`（`SetViewpointInvokersActive`）：

```cpp
	ApplyViewpointInvokerState(GetAttachParentActor() != nullptr);
```
改为

```cpp
	ApplyViewpointInvokerState(bBoarded);
```

- [ ] **Step 5: 跑测试确认通过**

Run: `Automation RunTests StellarEra.Planet.BoardedFlag`
Expected: PASS。

同时跑回归（这几条测试走的是同一套「附着 + SetBoarded」次序，判据一换就会暴露不匹配）：

Run: `Automation RunTests StellarEra.Planet.InvokerViewpoint`
Expected: PASS（`Character` 与 `StarMap` 两条）。

- [ ] **Step 6: 提交**

```bash
git commit -- Source/StellarEraPawns/Src/Character/PlanetCharacter.h \
              Source/StellarEraPawns/Src/Character/PlanetCharacter.cpp \
              Source/StellarEra/Src/Tests/PlanetRiderAttachTests.cpp \
  -m "refactor(角色): 登船判据从附着父级改为显式 bBoarded"
```

（本仓库提交必须带 pathspec；`git add` 之后裸 `git commit` 会吞掉索引里常驻的暂存文件。）

---

### Task 2: 新增 `SetRidePlanet`，附着关系只有这一个写入口

**Files:**
- Modify: `Source/StellarEraPawns/Src/Character/PlanetCharacter.h`
- Modify: `Source/StellarEraPawns/Src/Character/PlanetCharacter.cpp:572-596`（`ReevaluatePlanet`）
- Test: `Source/StellarEra/Src/Tests/PlanetRiderAttachTests.cpp`

**Interfaces:**
- Consumes: `bool IsBoarded() const`（Task 1）
- Produces: `void APlanetCharacter::SetRidePlanet(AVoxelPlanetActor* NewPlanet)`

- [ ] **Step 1: 写失败的测试**

在 `PlanetRiderAttachTests.cpp` 末尾追加：

```cpp
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraPlanetRidePlanetBookkeepingTest,
	"StellarEra.Planet.RidePlanetBookkeeping",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraPlanetRidePlanetBookkeepingTest::RunTest(const FString& Parameters)
{
	// 只验账本，不验「星球带着角色走」——后者需要真的体素行星，只能在 PIE 里看。
	// 这里锁住两条最容易写错的规则：
	//   1. 登船期间 SetRidePlanet 不许抢父级（父级归飞船）；
	//   2. 传 nullptr 会把附着摘掉。
	FScopedRiderTestWorld Scoped;
	if (!TestTrue(TEXT("测试世界"), Scoped.Create()))
	{
		return false;
	}

	ASpaceshipPawn* Ship = Scoped.World->SpawnActor<ASpaceshipPawn>(
		ASpaceshipPawn::StaticClass(), FTransform::Identity);
	APlanetCharacter* Character = Scoped.World->SpawnActor<APlanetCharacter>(
		APlanetCharacter::StaticClass(), FTransform::Identity);
	if (!TestNotNull(TEXT("飞船"), Ship) || !TestNotNull(TEXT("角色"), Character))
	{
		return false;
	}

	// 登船期间不抢父级
	Character->AttachToActor(Ship, FAttachmentTransformRules::KeepWorldTransform);
	Character->SetBoarded(true);
	Character->SetRidePlanet(nullptr);
	TestEqual(TEXT("登船期间父级仍是飞船"), Character->GetAttachParentActor(), Ship);

	// 离船后 nullptr 会把附着摘干净
	Character->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
	Character->SetBoarded(false);
	Character->SetRidePlanet(nullptr);
	TestNull(TEXT("离船且没有行星 → 没有附着父级"), Character->GetAttachParentActor());

	return true;
}
```

- [ ] **Step 2: 跑测试确认失败**

Expected: 编译错误 `no member named 'SetRidePlanet'`。

- [ ] **Step 3: 声明并实现**

`PlanetCharacter.h`，`IsBoarded()` 之后加：

```cpp
	/**
	 * 指定角色骑在哪颗行星上。这是**唯一**写本 Actor 附着关系的地方。
	 *
	 * 非登船状态下把角色 AttachToActor 到行星，此后行星平移多少、角色就跟着
	 * 平移多少 —— 由引擎的附着层级负责，不再依赖
	 * UCosmosSimulationSubsystem::CarryRiders 每帧推位移。
	 *
	 * 登船期间父级归飞船，本函数只更新「脚下是哪颗星」。
	 */
	void SetRidePlanet(AVoxelPlanetActor* NewPlanet);
```

`PlanetCharacter.cpp`，放在 `ReevaluatePlanet` 之前：

```cpp
void APlanetCharacter::SetRidePlanet(AVoxelPlanetActor* NewPlanet)
{
	// 登船期间父级是飞船，不抢。只更新「脚下是哪颗星」——
	// 登船时保留旧值本来就是这个类的既有行为（见 Tick 里的重评守卫）。
	if (bBoarded)
	{
		PlanetActor = NewPlanet;
		return;
	}

	AActor* CurrentParent = GetAttachParentActor();
	if (CurrentParent != NewPlanet)
	{
		if (CurrentParent)
		{
			DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
		}
		if (NewPlanet)
		{
			AttachToActor(NewPlanet, FAttachmentTransformRules::KeepWorldTransform);
		}
	}
	PlanetActor = NewPlanet;
}
```

- [ ] **Step 4: 让 `ReevaluatePlanet` 走它**

`PlanetCharacter.cpp:572-596` 整体替换为：

```cpp
void APlanetCharacter::ReevaluatePlanet()
{
	AVoxelPlanetActor* Nearest = FindNearestPlanet();

	// 星球没换、附着也没丢（比如刚下船需要重新挂回去）时，SetRidePlanet 自己
	// 会把附着补齐，这里不必再重摆位置。
	const bool bPlanetChanged = (Nearest != PlanetActor);

	SetRidePlanet(Nearest);

	if (!bPlanetChanged)
	{
		return;
	}

	// 换星：旧星已经不在脚下了（或者本来就没找到）。把「已落地」清掉 ——
	// 新星的地表还没流送出来，得让 Tick 末尾的兜底逻辑重新把角色按到球面上。
	bHasLanded = false;

	if (PlanetActor)
	{
		// 内部会把 LastSurfaceUp 清零，相机基座下一帧重建。
		SnapToPlanetSurface();
	}

	// 编辑网格跟着角色走：组件内部会先把旧星的网格收掉再点亮新星。
	if (UPlanetEditComponent* Edit = GetPlanetEditComponent())
	{
		Edit->SetGridPlanet(PlanetActor);
	}
}
```

- [ ] **Step 5: 跑测试确认通过**

Run: `Automation RunTests StellarEra.Planet.RidePlanetBookkeeping`
Expected: PASS。

- [ ] **Step 6: 提交**

```bash
git commit -- Source/StellarEraPawns/Src/Character/PlanetCharacter.h \
              Source/StellarEraPawns/Src/Character/PlanetCharacter.cpp \
              Source/StellarEra/Src/Tests/PlanetRiderAttachTests.cpp \
  -m "feat(角色): 新增 SetRidePlanet，附着关系收敛到一个写入口"
```

---

### Task 3: 接线（出生、下船、根组件忽略父级缩放）

**Files:**
- Modify: `Source/StellarEraPawns/Src/Character/PlanetCharacter.cpp`（构造函数、`BeginPlay`、`SetBoarded`）
- Modify: `Source/StellarEra/Src/StellarEraPlayerController.cpp:910-939`（`LeaveShip`）

**Interfaces:**
- Consumes: `SetRidePlanet`（Task 2）、`IsBoarded`（Task 1）

- [ ] **Step 1: 根组件忽略父级缩放**

`PlanetCharacter.cpp` 构造函数里，`GetCapsuleComponent()->InitCapsuleSize(42.f, 88.f);` 之后加：

```cpp
	// 角色会被 AttachToActor 到行星下。行星的大小是靠 Actor 缩放做的
	// （AVoxelPlanetActor::GetScaledRadiusVoxels 读 GetActorScale3D()），
	// 子物体默认继承父级缩放 —— 一旦有人缩放星球，胶囊、网格、相机臂、四圈
	// 调用器的厘米量会跟着一起打折。让根组件忽略父级缩放，世界缩放恒为 1。
	//
	// 本关卡三颗星球 S = 1，所以这条今天看不出效果；它是为了不让「缩放行星」
	// 这个既有工作流在将来踩雷。
	GetCapsuleComponent()->SetAbsolute(/*Location=*/false, /*Rotation=*/false, /*Scale=*/true);
```

- [ ] **Step 2: 出生时挂到初始行星**

`BeginPlay` 里把

```cpp
	PlanetActor = FindNearestPlanet();
```
改为

```cpp
	SetRidePlanet(FindNearestPlanet());
```

（`SetRidePlanet` 内部会写 `PlanetActor`，后面所有 `if (!PlanetActor)` 判断不受影响。）

- [ ] **Step 3: 下船后重新挂回行星**

`SetBoarded` 末尾、`if (!bBoarded)` 块里，在 `PlanetReevaluateTimer = 0.f;` 之后加：

```cpp
		// 从飞船下摘下来之后，位移重新交给行星附着关系。这里先挂回「上一次
		// 已知的那颗星」，LeaveShip 紧接着的 ReevaluatePlanet 会把它纠正成
		// 真正最近的一颗。
		SetRidePlanet(PlanetActor);
```

- [ ] **Step 4: 更新 `LeaveShip` 的注释**

`StellarEraPlayerController.cpp`，`DetachFromActor` 那一行上方补一句：

```cpp
	// 摘掉飞船之后角色是「无父级」状态，由 SetBoarded(false) 重新挂回行星；
	// 下面的 ReevaluatePlanet 再把父级纠正成真正最近的一颗。
	PawnCharacter->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
```

- [ ] **Step 5: 全量回归**

Run: `Automation RunTests StellarEra`
Expected: 全绿。重点看 `StellarEra.Planet.InvokerViewpoint.Character`（登船关调用器、离船开调用器）与 `StellarEra.Planet.BoardedFlag`。

- [ ] **Step 6: 提交**

```bash
git commit -- Source/StellarEraPawns/Src/Character/PlanetCharacter.cpp \
              Source/StellarEra/Src/StellarEraPlayerController.cpp \
  -m "feat(角色): 出生与下船时挂接到行星，根组件忽略父级缩放"
```

---

### Task 4: 把「搬运只有一处」这个契约写回文档和注释

代码改完之后，仓库里至少四处注释会变成假话，必须一起改，否则下一个人会按旧契约推理。

**Files:**
- Modify: `Source/StellarEraCosmos/Src/CosmosSimulationSubsystem.cpp:870-880`
- Modify: `Source/StellarEraPawns/Src/Character/PlanetCharacterMovementComponent.h:12-29`
- Modify: `CLAUDE.md`（「关键架构约束」小节）

- [ ] **Step 1: 改 `CarryRiders` 的去重注释**

`CosmosSimulationSubsystem.cpp:870-880`，把那句「（本项目当前只有一处 AttachToActor：登船时角色挂到飞船下。）」改为：

```cpp
		// （本项目有两处 AttachToActor：登船时角色挂到飞船下；平时角色挂在
		// 行星下，由附着关系搬运。本函数因此只是**兜底**，负责那些没有附着父级、
		// 也还够得着判定球的 Pawn。）
```

- [ ] **Step 2: 改移动组件头注释**

`PlanetCharacterMovementComponent.h:27` 的

```
 * 两条都关掉之后，角色的搬运全部交给 UCosmosSimulationSubsystem::CarryRiders，恰好一处。
```
改为

```cpp
 * 两条都关掉之后，角色的搬运交给**行星附着关系**（APlanetCharacter::SetRidePlanet）：
 * 角色是行星的子 Actor，行星平移多少、引擎的变换层级就把它带走多少。
 *
 * 为什么不是「恢复本函数」：UpdateBasedMovement 无条件把底座位移
 * MoveUpdatedComponent 一次（CharacterMovementComponent.cpp:2670），而附着层级
 * 已经带过一次。两者并存就是 2× 位移，所以本函数必须保持空实现。
 *
 * UCosmosSimulationSubsystem::CarryRiders 降级为兜底，只负责没有附着父级的 Pawn。
```

- [ ] **Step 3: 改 `CLAUDE.md`**

「关键架构约束」里这一条：

```
- `UPlanetCharacterMovementComponent` 关掉 CMC 自带的“底座移动我”，骑乘搬运统一交给 `CarryRiders`。
```
改为

```
- `UPlanetCharacterMovementComponent` 关掉 CMC 自带的“底座移动我”。角色**挂在行星下**（`SetRidePlanet` → `AttachToActor`），
  行星平移由附着关系带着走；`CarryRiders` 只是兜底，负责没有附着父级的 Pawn。
  登船判据用 `IsBoarded()`，**不要**用 `GetAttachParentActor() != nullptr`（骑乘时父级就是行星）。
```

- [ ] **Step 4: 提交**

```bash
git commit -- Source/StellarEraCosmos/Src/CosmosSimulationSubsystem.cpp \
              Source/StellarEraPawns/Src/Character/PlanetCharacterMovementComponent.h \
              CLAUDE.md \
  -m "docs(角色): 搬运契约改为附着关系，CarryRiders 降为兜底"
```

---

### Task 5: PIE 验证（唯一能证明「真的被星球带着走」的一步）

自动化测试跑在 editor world，不执行 `BeginPlay`，也没有体素行星——**附着搬运这条只能在这里验**。

**Files:** 无（纯验证）

- [ ] **Step 1: 让编辑器在前台**

后台运行时整个进程被限到 3 FPS，体素生成会从 0.16 s 变成上百秒，看起来像卡死。

- [ ] **Step 2: 打开 `UniverseMap`，PIE，等行星加载完**

- [ ] **Step 3: 在 World Outliner 里选中角色的 Actor，确认挂接**

Expected: 角色的 `Attach Parent` 显示为对应的 `BP_VoxelPlanet*`。这条是整个改动是否生效的直接证据。

- [ ] **Step 4: 在球面上走动、跳跃、跑到另一颗行星的判定球里**

Expected:
- 走动、落地、台阶、跳跃手感与改动前**一致**（这几项走 CMC，不该有变化）；
- 行星高速平移时角色**不再被甩下**（旧机制在判定球边界会掉队）；
- 换星时角色被摆到新星地表，没有飞出去。

- [ ] **Step 5: 上船 → 起飞 → 降落 → 下船**

Expected:
- 上船后角色 Attach Parent 变成飞船，调用器关闭；
- 下船后 Attach Parent 重新变回行星，被摆到飞船旁边并贴回球面；
- 下船瞬间镜头不甩（`CameraPivot` 重算）。

- [ ] **Step 6: 在编辑器里临时把某颗星球缩放成 2 倍，确认角色不被放大**

Expected: 角色、胶囊、相机臂尺寸不变。验完把缩放改回 1。

> 注意：`GetScaledRadiusVoxels` 在 S ≠ 1 时本身就有一条已知不一致（见该函数注释），星球网格与角色位置会错开。本条只验「角色没有被放大」，不验位置。

---

### Task 6: 收尾

- [ ] **Step 1: 全量自动化**

Run: `Automation RunTests StellarEra`
Expected: 全绿（改动前基线 + 新增 2 条）。

- [ ] **Step 2: 回主检出合并**

```bash
git switch master
git merge --no-ff worktree-planet-rider-attach
```

---

## Self-Review

**1. Spec coverage**

| 要求 | 落点 |
|---|---|
| 角色与星球建立父子关系 | Task 2 `SetRidePlanet` + Task 3 接线 |
| 保留 movement base 的地板语义 | 不动 `UPlanetCharacterMovementComponent` 的空实现；`bImpartBaseVelocity*` 不动 |
| 不叠加两次位移 | Task 4 Step 2 把「为什么必须保持空实现」写进头注释；`UpdateBasedMovement` 不动 |
| 登船/下船仍正确 | Task 1 判据替换 + Task 3 Step 3/4 |
| 缩放不再传染 | Task 3 Step 1 `SetAbsolute(..., Scale=true)` |
| 掉落兜底 | `CarryRiders` 不删，降级为兜底（Task 4 Step 1） |

**2. Placeholder scan** —— 无 TBD / TODO；每个代码步骤都给完整代码。

**3. Type consistency** —— `SetBoarded(bool bInBoarded)`（Task 1 Step 4 改名）与 Task 2/3 中的 `SetBoarded(true/false)` 调用一致；`SetRidePlanet(AVoxelPlanetActor*)` 在 Task 2 声明、Task 2/3 调用，签名一致；`IsBoarded() const` 在 Task 1 声明、Task 2 测试使用，一致。

**已知缺口（故意留白）**：没有任何自动化测试验证「角色真的跟着行星走」——worktree 里跑不了构建，editor world 里也没有体素行星。这条只能靠 Task 5 的 PIE 清单。若要补一条真正的集成测试，需要先做出「在测试世界里生成一颗可用体素行星」的公共 helper，那是独立任务。
