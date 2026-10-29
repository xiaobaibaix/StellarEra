# 体素 Invoker 视点门控 实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 只有当前被本地控制器 Possess 的 Pawn 的体素调用器参与 LOD 与碰撞，进星图后不再让飞船/角色的气泡一起算。

**Architecture:** 新增一个只含一个纯虚函数的原生接口 `IVoxelViewpointInvokers`，由三个持有调用器的 Pawn 实现；`AStellarEraPlayerController::OnPossess` 在每次换 Pawn 时遍历世界，把「是视点」这件事推给每个实现者；每个实现者用它与自身其它规则（登船）合成一个谓词，幂等地开关自己的调用器。`AlwaysLocal` 保留不动，作为回滚开关。

**Tech Stack:** Unreal Engine 5.8.3 / Win64 / C++20；模块 `StellarEraTerrain`、`StellarEraCharacter`、`StellarEraSpaceship`、`StellarEraUI`、`StellarEra`；VoxelFree 插件只读不写。

**Spec:** `docs/superpowers/specs/2026-10-29-voxel-invoker-viewpoint-design.md`

## Global Constraints

- **绝不修改** `Plugins/VoxelFree/` 与 `Plugins/VibeUE/`。自定义代码只进项目自己的模块。
- 构建**只能**用捆绑 dotnet 直调 UBT，cwd 在项目**外**，**构建前必须关闭编辑器**；不要用 `Build.bat`：
  ```
  "D:/Application/Epic Games/UE_5.8/Engine/Binaries/ThirdParty/DotNet/10.0/win-x64/dotnet.exe" \
    "D:/Application/Epic Games/UE_5.8/Engine/Binaries/DOTNET/UnrealBuildTool/UnrealBuildTool.dll" \
    StellarEraEditor Win64 Development \
    -Project="F:/workspace/ue/StellarEra/StellarEra.uproject" -WaitMutex -FromMsBuild
  ```
- **提交必须带 pathspec**（`git commit -m ... -- <paths>`）：索引里有常驻暂存文件，裸 `git commit` 会吞掉全部。新文件先 `git add`。
- **绝不提交** `Content/Saves/PlanetVoxel/UniverseMap/BP_VoxelPlanet3.uasset`。
- 含中文的 `.h` / `.cpp` 必须存成 **UTF-8 with BOM**；不要统一换行符（CRLF/LF 混排是仓库稳态）。
- 自动化测试名前缀 `StellarEra.`；本次新增的用 `StellarEra.Planet.InvokerViewpoint.*`。
- 不改任何 `LODToSet` / `LODRange` / `CollisionsRange` 的取值。
- 提交信息末尾加 `Co-Authored-By: Claude Code <noreply@anthropic.com>`。

## 关键既有事实（实施时不要重新推导）

| 事实 | 出处 |
|---|---|
| `EnableInvoker()` 在已启用时调 `FVoxelMessages::Error("Invoker already enabled")`，`DisableInvoker()` 同理 | `Plugins/VoxelFree/Source/Voxel/Private/VoxelComponents/VoxelInvokerComponent.cpp:41-70` |
| `FVoxelMessages::Error` 写的是 **Message Log（"PIE"）**，不走 `UE_LOG`，因此不会出现在自动化测试输出里，也不在 PIE 的 stdout 日志里 | `VoxelMessages.cpp:17-29` |
| 可用 `FVoxelMessages::LogMessageDelegate`（多播，两参数）截获这些消息 | `VoxelMessages.h:18` |
| `OnRegister()` 在 `bStartsEnabled`（默认 true）时自动 `EnableInvoker()` —— 早于 Possess | `VoxelInvokerComponent.cpp:81-88` |
| 组件在 `SpawnActor` 时就注册，**编辑器世界不会跑 BeginPlay** | `StarMapTests.cpp:381` 的既有写法 |
| `BoardShip` 是 `AttachToActor` → `SetBoarded(true)`；`LeaveShip` 是 `DetachFromActor` → `SetBoarded(false)` | `StellarEraPlayerController.cpp:831-850` |
| `APlanetCharacter::BeginPlay` 在「找不到行星」时**早退**，所以新代码必须放在早退之前 | `PlanetCharacter.cpp:323-328` |
| `Cast<IStarMapUIInputs>(UObject*)` 跨模块可用，接口用 `UINTERFACE(MinimalAPI, meta = (CannotImplementInterfaceInBlueprint))` 是本仓库既有写法 | `StellarEraHUD.cpp:95`、`StarMapUIInputs.h:14` |
| `Voxel` 是 `StellarEraTerrain` 的 **Public** 依赖，include 路径与链接都向下游传播，`StellarEra` 可直接 include `VoxelComponents/VoxelInvokerComponent.h` | `StellarEraTerrain.Build.cs:21` |
| `StellarEraCharacter` / `StellarEraSpaceship` / `StellarEraUI` 目前都只把 `StellarEraTerrain` 声明为 **Private** 依赖。它们的公开头一旦 include 新接口头，这个契约就写错了，必须先把 Terrain 提升为 Public | 三个 `*.Build.cs` 的 `PrivateDependencyModuleNames` |

---

### Task 1: 视点接口 + 三个 Pawn 的开关

**Files:**
- Create: `Source/StellarEraTerrain/Public/VoxelViewpointInvokers.h`
- Create: `Source/StellarEraTerrain/Private/VoxelViewpointInvokers.cpp`
- Modify: `Source/StellarEraCharacter/StellarEraCharacter.Build.cs`、`Source/StellarEraSpaceship/StellarEraSpaceship.Build.cs`、`Source/StellarEraUI/StellarEraUI.Build.cs`（Terrain 提升为公开依赖）
- Modify: `Source/StellarEraCharacter/Public/PlanetCharacter.h`（`class` 声明、public 段、`private:` 段）
- Modify: `Source/StellarEraCharacter/Private/PlanetCharacter.cpp`（`BeginPlay`、`SetBoarded`）
- Modify: `Source/StellarEraSpaceship/Public/SpaceshipPawn.h`、`Private/SpaceshipPawn.cpp`（`BeginPlay` 末尾）
- Modify: `Source/StellarEraUI/Public/StarMapCameraPawn.h`、`Private/StarMapCameraPawn.cpp`（`BeginPlay` 末尾）
- Test: `Source/StellarEra/Private/VoxelViewpointInvokerTests.cpp`

**Interfaces:**
- Consumes: 无（本任务是最底层）。
- Produces:
  - `IVoxelViewpointInvokers::SetViewpointInvokersActive(bool bActive)`（纯虚，原生接口）
  - `VoxelViewpointInvokers::SetEnabled(UVoxelInvokerComponentBase* Invoker, bool bEnabled)` —— 幂等开关，未注册的组件直接跳过
  - `APlanetCharacter` / `ASpaceshipPawn` / `AStarMapCameraPawn` 均实现该接口

- [ ] **Step 1: 先写失败的测试**

创建 `Source/StellarEra/Private/VoxelViewpointInvokerTests.cpp`：

```cpp
// 视点门控：只有当前视点 Pawn 的体素调用器参与 LOD。
//
// 这里验「开关本身」与「谓词合成」。BeginPlay 那一条验不了 —— 编辑器世界的
// SpawnActor 只走 OnRegister（自动启用），不走 BeginPlay。BeginPlay 的效果
// 只能在 PIE 里看日志，见计划任务 3。

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"

#include "VoxelMessages.h"
#include "VoxelComponents/VoxelInvokerComponent.h"
#include "VoxelViewpointInvokers.h"
#include "PlanetCharacter.h"
#include "SpaceshipPawn.h"
#include "StarMapCameraPawn.h"
#include "StellarEraPlayerController.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	/** 一次性编辑器世界，析构时销毁。照抄 StarMapTests.cpp 的写法。 */
	struct FScopedInvokerTestWorld
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

		~FScopedInvokerTestWorld()
		{
			if (World)
			{
				GEngine->DestroyWorldContext(World);
				World->DestroyWorld(false);
			}
		}
	};

	/** Owner 身上的全部体素调用器。 */
	TArray<UVoxelInvokerComponentBase*> CollectInvokers(const AActor* Owner)
	{
		TArray<UVoxelInvokerComponentBase*> Invokers;
		Owner->GetComponents<UVoxelInvokerComponentBase>(Invokers);
		return Invokers;
	}

	bool AllEnabled(const TArray<UVoxelInvokerComponentBase*>& Invokers)
	{
		for (const UVoxelInvokerComponentBase* Invoker : Invokers)
		{
			if (!Invoker->IsInvokerEnabled())
			{
				return false;
			}
		}
		return true;
	}

	bool AllDisabled(const TArray<UVoxelInvokerComponentBase*>& Invokers)
	{
		for (const UVoxelInvokerComponentBase* Invoker : Invokers)
		{
			if (Invoker->IsInvokerEnabled())
			{
				return false;
			}
		}
		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraInvokerViewpointShipTest,
	"StellarEra.Planet.InvokerViewpoint.Ship",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraInvokerViewpointShipTest::RunTest(const FString& Parameters)
{
	// 本测试的重点是「重复传同一个值不产生插件报错」：
	// EnableInvoker/DisableInvoker 在状态已对时会往 Message Log 写
	// "Invoker already enabled/disabled"，这里把消息截下来数一遍。
	FScopedInvokerTestWorld Scoped;
	if (!TestTrue(TEXT("测试世界"), Scoped.Create()))
	{
		return false;
	}

	ASpaceshipPawn* Ship = Scoped.World->SpawnActor<ASpaceshipPawn>(
		ASpaceshipPawn::StaticClass(), FTransform::Identity);
	if (!TestNotNull(TEXT("飞船"), Ship))
	{
		return false;
	}

	const TArray<UVoxelInvokerComponentBase*> Invokers = CollectInvokers(Ship);
	if (!TestEqual(TEXT("飞船的调用器个数"), Invokers.Num(), 3))
	{
		return false;
	}

	// OnRegister 自动启用：这是「生成后、首次 Possess 前」的既有行为。
	TestTrue(TEXT("生成后自动启用"), AllEnabled(Invokers));

	TArray<FString> Messages;
	const FDelegateHandle Handle = FVoxelMessages::LogMessageDelegate.AddLambda(
		[&Messages](const TSharedRef<FTokenizedMessage>& Message, EVoxelShowNotification)
		{
			Messages.Add(Message->ToText().ToString());
		});
	ON_SCOPE_EXIT
	{
		FVoxelMessages::LogMessageDelegate.Remove(Handle);
	};

	Ship->SetViewpointInvokersActive(false);
	TestTrue(TEXT("非视点 → 全部关闭"), AllDisabled(Invokers));

	Ship->SetViewpointInvokersActive(false);
	TestTrue(TEXT("重复关闭仍关闭"), AllDisabled(Invokers));

	Ship->SetViewpointInvokersActive(true);
	TestTrue(TEXT("视点 → 全部打开"), AllEnabled(Invokers));

	Ship->SetViewpointInvokersActive(true);
	TestTrue(TEXT("重复打开仍打开"), AllEnabled(Invokers));

	TestTrue(TEXT("重复开关不产生插件报错"),
		!Messages.ContainsByPredicate([](const FString& Message)
		{
			return Message.Contains(TEXT("already"));
		}));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraInvokerViewpointCharacterTest,
	"StellarEra.Planet.InvokerViewpoint.Character",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraInvokerViewpointCharacterTest::RunTest(const FString& Parameters)
{
	// 角色同时受「登船」与「视点」两条规则约束。谓词是「与」：
	// 登船中即使拿到视点也必须保持关闭，否则登船期间脚下 200 米的精细块
	// 会跟着飞船高速平移、每帧重网格化。
	FScopedInvokerTestWorld Scoped;
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

	const TArray<UVoxelInvokerComponentBase*> Invokers = CollectInvokers(Character);
	if (!TestEqual(TEXT("角色的调用器个数"), Invokers.Num(), 1))
	{
		return false;
	}

	// 不挂接 + 非视点 → 关
	Character->SetViewpointInvokersActive(false);
	TestTrue(TEXT("地面且非视点 → 关闭"), AllDisabled(Invokers));

	// 登船（与 BoardShip 同序：先挂后设）+ 拿到视点 → 仍关
	Character->AttachToActor(Ship, FAttachmentTransformRules::KeepWorldTransform);
	Character->SetViewpointInvokersActive(true);
	TestTrue(TEXT("登船中即使拿到视点也保持关闭"), AllDisabled(Invokers));

	// 离船（与 LeaveShip 同序：先摘后设）+ 仍是视点 → 开
	Character->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
	Character->SetViewpointInvokersActive(true);
	TestTrue(TEXT("离船且是视点 → 打开"), AllEnabled(Invokers));

	// SetBoarded 走同一个谓词：登船关、离船开。
	Character->AttachToActor(Ship, FAttachmentTransformRules::KeepWorldTransform);
	Character->SetBoarded(true);
	TestTrue(TEXT("登船 → 关闭"), AllDisabled(Invokers));

	Character->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
	Character->SetBoarded(false);
	TestTrue(TEXT("离船且是视点 → 重新打开"), AllEnabled(Invokers));

	// 视点被拿走（进星图），人还在地上 → 关
	Character->SetViewpointInvokersActive(false);
	TestTrue(TEXT("非视点 → 关闭"), AllDisabled(Invokers));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraInvokerViewpointStarMapTest,
	"StellarEra.Planet.InvokerViewpoint.StarMap",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraInvokerViewpointStarMapTest::RunTest(const FString& Parameters)
{
	FScopedInvokerTestWorld Scoped;
	if (!TestTrue(TEXT("测试世界"), Scoped.Create()))
	{
		return false;
	}

	AStarMapCameraPawn* Camera = Scoped.World->SpawnActor<AStarMapCameraPawn>(
		AStarMapCameraPawn::StaticClass(), FTransform::Identity);
	if (!TestNotNull(TEXT("星图相机"), Camera))
	{
		return false;
	}

	const TArray<UVoxelInvokerComponentBase*> Invokers = CollectInvokers(Camera);
	if (!TestEqual(TEXT("星图相机的调用器个数"), Invokers.Num(), 3))
	{
		return false;
	}

	Camera->SetViewpointInvokersActive(false);
	TestTrue(TEXT("非视点 → 三档一起关闭"), AllDisabled(Invokers));

	Camera->SetViewpointInvokersActive(true);
	TestTrue(TEXT("视点 → 三档一起打开"), AllEnabled(Invokers));

	return true;
}

#endif
```

- [ ] **Step 2: 确认测试是红的（编译失败）**

关闭编辑器后构建：

```
"D:/Application/Epic Games/UE_5.8/Engine/Binaries/ThirdParty/DotNet/10.0/win-x64/dotnet.exe" \
  "D:/Application/Epic Games/UE_5.8/Engine/Binaries/DOTNET/UnrealBuildTool/UnrealBuildTool.dll" \
  StellarEraEditor Win64 Development \
  -Project="F:/workspace/ue/StellarEra/StellarEra.uproject" -WaitMutex -FromMsBuild
```

预期：FAILED，报 `VoxelViewpointInvokers.h: No such file or directory` 与 `SetViewpointInvokersActive` 不是成员。这就是 C++ 的「红」。

- [ ] **Step 3: 写接口与幂等开关**

创建 `Source/StellarEraTerrain/Public/VoxelViewpointInvokers.h`：

```cpp
// 视点门控：只有当前被本地控制器 Possess 的那个 Pawn 的体素调用器参与 LOD。
//
// 为什么需要它：本项目用 UAlwaysLocalVoxelInvokerComponent 覆写了 IsLocalInvoker
// （见 AlwaysLocalVoxelInvokerComponent.h），代价是所有调用器永久参与 —— 进星图后
// 行星 3 个 + 飞船 3 个 + 角色 1 个 + 星图 3 个同时算，而星图里根本看不见飞船和角色。
// 这里补一层显式开关：换视点时由 AStellarEraPlayerController::OnPossess 统一驱动。
//
// 行星自己的 UVoxelSimpleInvokerComponent 不实现本接口，因此永远不受影响。

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "VoxelViewpointInvokers.generated.h"

class UVoxelInvokerComponentBase;

UINTERFACE(MinimalAPI, meta = (CannotImplementInterfaceInBlueprint))
class UVoxelViewpointInvokers : public UInterface
{
	GENERATED_BODY()
};

/**
 * 由「持有体素调用器的 Pawn」实现。
 *
 * 契约：
 *   1. bActive = true 表示本 Pawn 是当前视点；
 *   2. 实现必须幂等 —— 重复传同一个值不改变状态，也不触发插件的
 *      "Invoker already enabled/disabled"（先用 VoxelViewpointInvokers::SetEnabled）；
 *   3. 实现必须记住最后一次的值，并在 BeginPlay 里按它重新应用 ——
 *      组件在 OnRegister 中自动启用（VoxelInvokerComponent.cpp:83），早于 Possess。
 */
class STELLARERATERRAIN_API IVoxelViewpointInvokers
{
	GENERATED_BODY()

public:
	virtual void SetViewpointInvokersActive(bool bActive) = 0;
};

namespace VoxelViewpointInvokers
{
	/**
	 * 幂等地把调用器切到 bEnabled。
	 *
	 * 插件的 EnableInvoker / DisableInvoker 在状态已经对时会往 Message Log 写
	 * "Invoker already enabled/disabled"（VoxelInvokerComponent.cpp:41 / :57）。
	 * 换视点是低频事件但会反复传同一个值，所以必须先比对再动手。
	 * 未注册的组件直接跳过 —— 注册状态由 OnRegister / OnUnregister 管。
	 */
	STELLARERATERRAIN_API void SetEnabled(UVoxelInvokerComponentBase* Invoker, bool bEnabled);
}
```

创建 `Source/StellarEraTerrain/Private/VoxelViewpointInvokers.cpp`：

```cpp
#include "VoxelViewpointInvokers.h"
#include "VoxelComponents/VoxelInvokerComponent.h"

void VoxelViewpointInvokers::SetEnabled(UVoxelInvokerComponentBase* const Invoker, const bool bEnabled)
{
	if (!Invoker || !Invoker->IsRegistered() || Invoker->IsInvokerEnabled() == bEnabled)
	{
		return;
	}

	if (bEnabled)
	{
		Invoker->EnableInvoker();
	}
	else
	{
		Invoker->DisableInvoker();
	}
}
```

- [ ] **Step 4: 给三个新文件加 UTF-8 BOM**

```bash
for f in Source/StellarEraTerrain/Public/VoxelViewpointInvokers.h \
         Source/StellarEraTerrain/Private/VoxelViewpointInvokers.cpp \
         Source/StellarEra/Private/VoxelViewpointInvokerTests.cpp; do
  if [ "$(head -c 3 "$f" | xxd -p)" != "efbbbf" ]; then
    printf '\xEF\xBB\xBF' | cat - "$f" > "$f.tmp" && mv "$f.tmp" "$f"
  fi
done
head -c 3 Source/StellarEraTerrain/Public/VoxelViewpointInvokers.h | xxd -p
```

预期最后一行输出 `efbbbf`。

- [ ] **Step 5: 把 StellarEraTerrain 提升为三个模块的公开依赖**

三个 Pawn 的**公开头**都要 include `VoxelViewpointInvokers.h`（继承接口需要完整类型），
而这个头属于 `StellarEraTerrain`。现在三个模块都只把 Terrain 写成私有依赖，
公开头 include 私有依赖的头是把模块契约写错 —— 下游 include `PlanetCharacter.h`
时能不能找到那个头，将取决于它碰巧有没有直接依赖 Terrain。

`Source/StellarEraCharacter/StellarEraCharacter.Build.cs` 改成：

```csharp
		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core", "CoreUObject", "Engine",
			// PlanetCharacter.h 公开继承 IVoxelViewpointInvokers，而
			// VoxelViewpointInvokers.h 在 StellarEraTerrain。公开头 include 的头
			// 所在模块必须是公开依赖，否则下游 include PlanetCharacter.h 时找不到它。
			"StellarEraTerrain",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"StellarEraPlanetEdit",
			"Voxel",
		});
```

`Source/StellarEraSpaceship/StellarEraSpaceship.Build.cs` 同样：把 `"StellarEraTerrain"`
从私有列表移到公开列表（注释换成 `SpaceshipPawn.h` 公开继承同一接口），私有列表剩下
`"StellarEraAtmosphere"`、`"StellarEraCharacter"`、`"Voxel"`。

`Source/StellarEraUI/StellarEraUI.Build.cs` 同样：把 `"StellarEraTerrain"` 那一行从私有列表
移到公开列表，私有列表里其余六项不动。

这三个改动不新增任何模块依赖边（三个模块本来就依赖 Terrain），只是把它们声明成公开，
不引入环：Terrain 不依赖 Character / Spaceship / UI。

- [ ] **Step 6: 角色实现接口**

修改 `Source/StellarEraCharacter/Public/PlanetCharacter.h`：

在 `#include "PlanetCharacter.generated.h"` 之前加：

```cpp
#include "VoxelViewpointInvokers.h"
```

类声明（当前 `PlanetCharacter.h:26`）改成：

```cpp
class STELLARERACHARACTER_API APlanetCharacter : public ACharacter, public IVoxelViewpointInvokers
```

紧跟 `virtual void BeginPlay() override;` 之后加：

```cpp
	//~ Begin IVoxelViewpointInvokers Interface
	virtual void SetViewpointInvokersActive(bool bActive) override;
	//~ End IVoxelViewpointInvokers Interface
```

在 `private:` 段（当前 `PlanetCharacter.h:203`）里加：

```cpp
	/**
	 * 按「是当前视点 ∧ 没在船上」开关脚下的体素调用器。
	 *
	 * 两条规则必须合成一个谓词：分开写会互相拆台 —— SetBoarded(false) 会把
	 * 非当前视点的角色重新打开，而登船期间角色跟着飞船高速平移，
	 * LODToSet = 0 的 200 米盒子每帧都要重网格化。
	 *
	 * @param bBoarded 调用方当时知道的登船状态。SetBoarded 传它自己的入参，
	 *                 其余两处（视点切换 / BeginPlay）现查 GetAttachParentActor()。
	 *                 不存成员：登船状态的唯一口径是挂接关系，存一份会漂移。
	 */
	void ApplyViewpointInvokerState(bool bBoarded);

	/** 本 Pawn 是否当前视点。初值 false —— OnRegister 会自动启用，BeginPlay 要按它关掉。 */
	bool bIsViewpoint = false;
```

- [ ] **Step 7: 角色的实现**

修改 `Source/StellarEraCharacter/Private/PlanetCharacter.cpp`。加 `#include "VoxelViewpointInvokers.h"`（跟随现有的 include 块）。

`BeginPlay` 里，紧跟解析器那一段之后、`PlanetActor = FindNearestPlanet();` **之前**（当前 `:317`）插入：

```cpp
	// 调用器在 OnRegister 里自动启用（VoxelInvokerComponent.cpp:83），那时还没有
	// 任何人告诉过我们是不是视点。这里按当前已知状态重新应用一次。
	// 必须在下面「找不到行星」的早退之前 —— 早退之后就没有第二次机会，
	// 角色会在没被 Possess 的情况下一直开着调用器。
	ApplyViewpointInvokerState(GetAttachParentActor() != nullptr);
```

`SetBoarded` 里，把现有的整段调用器开关（当前 `:603-616`，从 `if (VoxelInvoker && VoxelInvoker->IsRegistered())` 到它自己的右花括号）替换为：

```cpp
	// 登船后角色只是被隐藏/禁撞，Actor 和调用器仍然挂在飞船上，会跟着飞船
	// 一起高速平移：LODToSet = 0 + 200 米的盒子每帧都要重网格化，是飞船自己
	// 那三档之外的一块大头。这里连同「是不是当前视点」一起交给同一个谓词。
	ApplyViewpointInvokerState(bBoarded);
```

在 `SetBoarded` 的实现之后加这两个函数：

```cpp
void APlanetCharacter::SetViewpointInvokersActive(const bool bActive)
{
	bIsViewpoint = bActive;
	ApplyViewpointInvokerState(GetAttachParentActor() != nullptr);
}

void APlanetCharacter::ApplyViewpointInvokerState(const bool bBoarded)
{
	// 「参与」= 是当前视点 ∧ 没在船上。
	VoxelViewpointInvokers::SetEnabled(VoxelInvoker, bIsViewpoint && !bBoarded);
}
```

- [ ] **Step 8: 飞船实现接口**

修改 `Source/StellarEraSpaceship/Public/SpaceshipPawn.h`：

在 `#include "SpaceshipPawn.generated.h"` 之前加 `#include "VoxelViewpointInvokers.h"`。

类声明（当前 `:107`）改成：

```cpp
class STELLARERASPACESHIP_API ASpaceshipPawn : public APawn, public IVoxelViewpointInvokers
```

紧跟 `virtual void BeginPlay() override;` 之后加：

```cpp
	//~ Begin IVoxelViewpointInvokers Interface
	virtual void SetViewpointInvokersActive(bool bActive) override;
	//~ End IVoxelViewpointInvokers Interface
```

在 `private:` 段（当前 `:773`，紧跟 `void SetVoxelInvokerCollisionEnabled(bool bEnabled);` 之后）加：

```cpp
	/** 三档调用器一起开关 —— 只留高档近处会糊，只留低档就没有碰撞气泡。 */
	void ApplyViewpointInvokerState();

	/** 本 Pawn 是否当前视点。初值 false —— OnRegister 会自动启用，BeginPlay 要按它关掉。 */
	bool bIsViewpoint = false;
```

- [ ] **Step 9: 飞船的实现**

修改 `Source/StellarEraSpaceship/Private/SpaceshipPawn.cpp`。加 `#include "VoxelViewpointInvokers.h"`（跟在 `#include "AlwaysLocalVoxelInvokerComponent.h"` 之后）。

`BeginPlay` 末尾（当前 `:309`，`SpringArmBaseRotation` 赋值那一行之后、函数右花括号之前）加：

```cpp
	// 调用器在 OnRegister 里已经自动启用了，这里按「还不是视点」关掉。
	// 飞船在 PIE 开局就生成、由玩家登上后才 Possess，这个窗口里它不该参与 LOD。
	ApplyViewpointInvokerState();
```

在文件里 `SetVoxelInvokerCollisionEnabled` 的实现之后加：

```cpp
void ASpaceshipPawn::SetViewpointInvokersActive(const bool bActive)
{
	bIsViewpoint = bActive;
	ApplyViewpointInvokerState();
}

void ASpaceshipPawn::ApplyViewpointInvokerState()
{
	// 高速门控只改 bUseForCollisions，与这里的整体启停正交，不参与合并。
	VoxelViewpointInvokers::SetEnabled(VoxelInvoker, bIsViewpoint);
	VoxelViewpointInvokers::SetEnabled(VoxelInvokerLOD3, bIsViewpoint);
	VoxelViewpointInvokers::SetEnabled(VoxelInvokerLOD5, bIsViewpoint);
}
```

- [ ] **Step 10: 星图相机实现接口**

修改 `Source/StellarEraUI/Public/StarMapCameraPawn.h`：

在 `#include "StarMapCameraPawn.generated.h"` 之前加 `#include "VoxelViewpointInvokers.h"`。

类声明（当前 `:66`）改成：

```cpp
class STELLARERAUI_API AStarMapCameraPawn : public APawn, public IVoxelViewpointInvokers
```

紧跟 `virtual void BeginPlay() override;` 之后加：

```cpp
	//~ Begin IVoxelViewpointInvokers Interface
	virtual void SetViewpointInvokersActive(bool bActive) override;
	//~ End IVoxelViewpointInvokers Interface
```

在私有段（当前 `:248` 三个调用器成员之后）加：

```cpp
	/** 三档调用器同进同出。 */
	void ApplyViewpointInvokerState();

	/** 本 Pawn 是否当前视点。初值 false —— OnRegister 会自动启用，BeginPlay 要按它关掉。 */
	bool bIsViewpoint = false;
```

- [ ] **Step 11: 星图相机的实现**

修改 `Source/StellarEraUI/Private/StarMapCameraPawn.cpp`。加 `#include "VoxelViewpointInvokers.h"`（跟在 `#include "AlwaysLocalVoxelInvokerComponent.h"` 之后）。

`BeginPlay` 末尾（当前 `:107`，`SetTargetResolver` 那个 `if` 之后）加：

```cpp
	// 调用器在 OnRegister 里已经自动启用了。星图相机只在被 Possess（进星图）
	// 期间才是视点，此前一律关掉。
	ApplyViewpointInvokerState();
```

在 `BeginPlay` 之后加：

```cpp
void AStarMapCameraPawn::SetViewpointInvokersActive(const bool bActive)
{
	bIsViewpoint = bActive;
	ApplyViewpointInvokerState();
}

void AStarMapCameraPawn::ApplyViewpointInvokerState()
{
	VoxelViewpointInvokers::SetEnabled(VoxelInvoker, bIsViewpoint);
	VoxelViewpointInvokers::SetEnabled(VoxelInvokerLOD3, bIsViewpoint);
	VoxelViewpointInvokers::SetEnabled(VoxelInvokerLOD5, bIsViewpoint);
}
```

- [ ] **Step 12: 构建**

编辑器关闭状态下跑构建命令（见 Global Constraints）。预期：`Result: Succeeded`。

- [ ] **Step 13: 跑测试**

```bash
"D:/Application/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" \
  "F:/workspace/ue/StellarEra/StellarEra.uproject" \
  -ExecCmds="Automation RunTests StellarEra.Planet.InvokerViewpoint" \
  -TestExit="Automation Test Queue Empty" \
  -unattended -nopause -nullrhi -nosplash -stdout -abslog="$TEMP/invoker_viewpoint.log"
grep -E "Test Completed|Fail|Error" "$TEMP/invoker_viewpoint.log" | head -40
```

预期：3 个测试全部 `Result={Success}`，`0 Fail`。

- [ ] **Step 14: 提交**

```bash
git add Source/StellarEraTerrain/Public/VoxelViewpointInvokers.h \
        Source/StellarEraTerrain/Private/VoxelViewpointInvokers.cpp \
        Source/StellarEra/Private/VoxelViewpointInvokerTests.cpp
git commit -m "feat(体素): 视点接口与三个 Pawn 的调用器开关

Co-Authored-By: Claude Code <noreply@anthropic.com>" -- \
  Source/StellarEraTerrain/Public/VoxelViewpointInvokers.h \
  Source/StellarEraTerrain/Private/VoxelViewpointInvokers.cpp \
  Source/StellarEraCharacter/StellarEraCharacter.Build.cs \
  Source/StellarEraSpaceship/StellarEraSpaceship.Build.cs \
  Source/StellarEraUI/StellarEraUI.Build.cs \
  Source/StellarEraCharacter/Public/PlanetCharacter.h \
  Source/StellarEraCharacter/Private/PlanetCharacter.cpp \
  Source/StellarEraSpaceship/Public/SpaceshipPawn.h \
  Source/StellarEraSpaceship/Private/SpaceshipPawn.cpp \
  Source/StellarEraUI/Public/StarMapCameraPawn.h \
  Source/StellarEraUI/Private/StarMapCameraPawn.cpp \
  Source/StellarEra/Private/VoxelViewpointInvokerTests.cpp
```

提交前 `git status -- <这些路径>`。注意 `Source/StellarEraUI/Private/StarMapCameraPawn.cpp` 等文件在本次会话开始前**已有无关的未提交改动**（见会话起始的 git status）—— pathspec 提交会把这些改动一并带上。先 `git diff -- <该文件>` 看清楚，若确实混着别人的改动，停下来问用户。

---

### Task 2: OnPossess 驱动

**Files:**
- Modify: `Source/StellarEra/Public/StellarEraPlayerController.h`（`public` 段）
- Modify: `Source/StellarEra/Private/StellarEraPlayerController.cpp`（`OnPossess`、新函数）
- Test: `Source/StellarEra/Private/VoxelViewpointInvokerTests.cpp`（追加一个测试）

**Interfaces:**
- Consumes: `IVoxelViewpointInvokers::SetViewpointInvokersActive(bool)`，三个 Pawn 的实现（Task 1）。
- Produces: `static void AStellarEraPlayerController::ApplyViewpointInvokerGate(UWorld* World, const APawn* InPawn);` —— 公开静态，测试可直接调（与 `ResolveStarMapFocusShip` 同风格）。

- [ ] **Step 1: 追加失败的测试**

在 `Source/StellarEra/Private/VoxelViewpointInvokerTests.cpp` 里、`#endif` 之前追加：

```cpp
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraInvokerViewpointGateTest,
	"StellarEra.Planet.InvokerViewpoint.Gate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraInvokerViewpointGateTest::RunTest(const FString& Parameters)
{
	// 驱动逻辑本身：遍历世界，只把 InPawn 当成视点；InPawn 为空时全关。
	// 不能直接调 OnPossess —— 它是 protected，而且需要本地玩家与 HUD。
	FScopedInvokerTestWorld Scoped;
	if (!TestTrue(TEXT("测试世界"), Scoped.Create()))
	{
		return false;
	}

	ASpaceshipPawn* Ship = Scoped.World->SpawnActor<ASpaceshipPawn>(
		ASpaceshipPawn::StaticClass(), FTransform::Identity);
	APlanetCharacter* Character = Scoped.World->SpawnActor<APlanetCharacter>(
		APlanetCharacter::StaticClass(), FTransform::Identity);
	AStarMapCameraPawn* Camera = Scoped.World->SpawnActor<AStarMapCameraPawn>(
		AStarMapCameraPawn::StaticClass(), FTransform::Identity);
	APawn* Plain = Scoped.World->SpawnActor<APawn>(
		APawn::StaticClass(), FTransform::Identity);

	if (!TestNotNull(TEXT("飞船"), Ship) || !TestNotNull(TEXT("角色"), Character) ||
		!TestNotNull(TEXT("星图相机"), Camera) || !TestNotNull(TEXT("普通 Pawn"), Plain))
	{
		return false;
	}

	const TArray<UVoxelInvokerComponentBase*> ShipInvokers = CollectInvokers(Ship);
	const TArray<UVoxelInvokerComponentBase*> CharacterInvokers = CollectInvokers(Character);
	const TArray<UVoxelInvokerComponentBase*> CameraInvokers = CollectInvokers(Camera);

	// 视点 = 飞船
	AStellarEraPlayerController::ApplyViewpointInvokerGate(Scoped.World, Ship);
	TestTrue(TEXT("视点飞船 → 打开"), AllEnabled(ShipInvokers));
	TestTrue(TEXT("角色 → 关闭"), AllDisabled(CharacterInvokers));
	TestTrue(TEXT("星图相机 → 关闭"), AllDisabled(CameraInvokers));

	// 视点 = 角色（下船）
	AStellarEraPlayerController::ApplyViewpointInvokerGate(Scoped.World, Character);
	TestTrue(TEXT("视点角色 → 打开"), AllEnabled(CharacterInvokers));
	TestTrue(TEXT("飞船 → 关闭"), AllDisabled(ShipInvokers));

	// 视点 = 星图相机（进星图）
	AStellarEraPlayerController::ApplyViewpointInvokerGate(Scoped.World, Camera);
	TestTrue(TEXT("视点星图 → 打开"), AllEnabled(CameraInvokers));
	TestTrue(TEXT("角色 → 关闭"), AllDisabled(CharacterInvokers));
	TestTrue(TEXT("飞船 → 关闭"), AllDisabled(ShipInvokers));

	// 没有视点（OnUnPossess 路径）
	AStellarEraPlayerController::ApplyViewpointInvokerGate(Scoped.World, nullptr);
	TestTrue(TEXT("无视点 → 星图关闭"), AllDisabled(CameraInvokers));

	return true;
}
```

- [ ] **Step 2: 确认是红的**

跑构建命令。预期：FAILED，报 `ApplyViewpointInvokerGate` 不是 `AStellarEraPlayerController` 的成员。

- [ ] **Step 3: 加声明**

修改 `Source/StellarEra/Public/StellarEraPlayerController.h`，在 `public` 段紧接 `static ASpaceshipPawn* ResolveStarMapFocusShip(APawn* EnteringPawn);`（当前 `:69`）之后加：

```cpp
	/**
	 * 换视点的唯一驱动：把 World 里所有实现 IVoxelViewpointInvokers 的 Pawn
	 * 按 InPawn 设成视点，只有 InPawn 拿到 true。InPawn 为空（OnUnPossess
	 * 路径）时全部关掉。
	 *
	 * 静态是为了让测试能在没有控制器实例的情况下验它 —— 与
	 * ResolveStarMapFocusShip 同理。不实现本接口的 Pawn 不受影响。
	 */
	static void ApplyViewpointInvokerGate(UWorld* World, const APawn* InPawn);
```

- [ ] **Step 4: 加实现并接进 OnPossess**

修改 `Source/StellarEra/Private/StellarEraPlayerController.cpp`：

加 `#include "VoxelViewpointInvokers.h"`；`TActorIterator` 需要 `#include "EngineUtils.h"`（若文件里已有就跳过）。

`OnPossess`（当前 `:193`）改成：

```cpp
void AStellarEraPlayerController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);

	// 换 Pawn（登船 / 离船 / 进出星图 / DebugPossessShip）一律退出矿物透视。
	ExitMineralViewIfActive();

	// 换视点：只有新 Pawn 的体素调用器参与 LOD 与碰撞。这是所有换 Pawn 路径的
	// 唯一收口，所以门控挂在这里，而不是登船 / 离船 / 星图三处各写一遍。
	ApplyViewpointInvokerGate(GetWorld(), InPawn);

	ApplyMouseModeForPawn(InPawn);
}
```

在 `OnPossess` 之后加：

```cpp
void AStellarEraPlayerController::ApplyViewpointInvokerGate(
	UWorld* const World, const APawn* const InPawn)
{
	if (!World)
	{
		return;
	}

	// OnPossess 是低频事件，遍历全部 Pawn 的开销可忽略。
	for (TActorIterator<APawn> It(World); It; ++It)
	{
		APawn* const Pawn = *It;
		IVoxelViewpointInvokers* const Viewpoint = Cast<IVoxelViewpointInvokers>(Pawn);
		if (!Viewpoint)
		{
			continue;
		}

		Viewpoint->SetViewpointInvokersActive(Pawn == InPawn);
	}
}
```

- [ ] **Step 5: 构建**

跑构建命令。预期：`Result: Succeeded`。

- [ ] **Step 6: 跑测试**

```bash
"D:/Application/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" \
  "F:/workspace/ue/StellarEra/StellarEra.uproject" \
  -ExecCmds="Automation RunTests StellarEra.Planet.InvokerViewpoint" \
  -TestExit="Automation Test Queue Empty" \
  -unattended -nopause -nullrhi -nosplash -stdout -abslog="$TEMP/invoker_viewpoint.log"
grep -E "Test Completed|Fail|Error" "$TEMP/invoker_viewpoint.log" | head -40
```

预期：4 个测试全部 `Result={Success}`，`0 Fail`。

- [ ] **Step 7: 提交**

```bash
git commit -m "feat(体素): OnPossess 驱动视点门控

Co-Authored-By: Claude Code <noreply@anthropic.com>" -- \
  Source/StellarEra/Public/StellarEraPlayerController.h \
  Source/StellarEra/Private/StellarEraPlayerController.cpp \
  Source/StellarEra/Private/VoxelViewpointInvokerTests.cpp
```

`Source/StellarEra/Private/StellarEraPlayerController.cpp` 在本次会话开始前已是修改状态，先 `git diff` 看有没有无关改动，有就问用户。

---

### Task 3: 全量验证（自动化 + PIE）

**Files:**
- 无代码改动。本任务只产出证据；任何失败都退回对应任务修。

**Interfaces:**
- Consumes: Task 1、Task 2 的全部产物。
- Produces: 无。

- [ ] **Step 1: 跑全量自动化**

```bash
"D:/Application/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" \
  "F:/workspace/ue/StellarEra/StellarEra.uproject" \
  -ExecCmds="Automation RunTests StellarEra" \
  -TestExit="Automation Test Queue Empty" \
  -unattended -nopause -nullrhi -nosplash -stdout -abslog="$TEMP/full_suite.log"
grep -cE "Test Completed\. Result=\{Success\}" "$TEMP/full_suite.log"
grep -cE "Test Completed\. Result=\{Fail" "$TEMP/full_suite.log"
```

基线：上一次全量是 **142 个跑、141 过**。本次新增 4 个测试，预期 **146 跑 / 145 过**，失败数不高于基线。

- [ ] **Step 2: PIE 验证（编辑器必须在前台，否则整进程被限 3 FPS）**

打开 `UniverseMap` 后逐条核：

1. PIE 启动后 `grep "Voxel Invoker" <PIE 日志>`：飞船的 3 个应出现 `disabled`（BeginPlay 关掉），角色应 `enabled`。
2. 按 M 进星图：角色 1 个 + 飞船 3 个 `disabled`，星图相机 3 个 `enabled`。
3. 退出星图：反向。
4. 登船：角色 `disabled`；离船：角色 `enabled`。
5. 日志里 `ensure` 的条数必须与改动前一致（**0 条新增**）。
6. 编辑器 Message Log 里不得出现 `Invoker already enabled` / `Invoker already disabled`。

任一条不符：立刻按 spec 的「风险与回滚」回滚 —— 门控逻辑集中在一个接口加一个循环，回滚 = 删掉 `OnPossess` 里 `ApplyViewpointInvokerGate` 那一行，再删掉三个 Pawn 的 `BeginPlay` 调用。

- [ ] **Step 3: 记录结论**

把上面 6 条的实测结果写进本文档末尾的「验证记录」小节，然后提交：

```bash
git commit -m "docs(体素): 视点门控验证记录

Co-Authored-By: Claude Code <noreply@anthropic.com>" -- \
  docs/superpowers/plans/2026-10-29-voxel-invoker-viewpoint.md
```

## 验证记录

（Task 3 完成后填写）
