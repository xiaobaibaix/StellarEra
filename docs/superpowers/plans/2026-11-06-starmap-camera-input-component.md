# 星图相机输入抽成组件 — 实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 把 `AStellarEraPlayerController` 上那组「只有星图相机在用时才有意义」的鼠标 / 滚轮绑定搬进 `AStarMapCameraPawn` 自己的 `UStarMapCameraInputComponent`，与 `UPlanetCharacterInputComponent` / `USpaceshipInputComponent` 同构。

**Architecture:** 星图的键鼠输入目前绑在控制器上，`OnLook` / `OnZoom` 里再 `Cast<AStarMapCameraPawn>` 判断「现在是不是星图」。搬成组件后这条绑定只存在于相机 Pawn 上，`Cast` 整个消失。判视角模式要读的控制器状态（左键按住、编辑模式、连挖、矿物透视）通过一个新的原生 UINTERFACE `IStarMapLookContext` 取，形状照抄 `ICosmosBody`。

**Tech Stack:** Unreal Engine 5.8.3，Enhanced Input，原生 UINTERFACE（`CannotImplementInterfaceInBlueprint`）。

**Spec:** 无独立 spec 文档。设计要求由用户在 2026-11-06 直接提出（「星图相机的输入映射也应该做成和飞船角色一样的组件」），设计在会话中逐条确认；唯一的行为选择——灵敏度归属——用户选定「每个组件各自一份」。

## Global Constraints

- 构建（**编辑器必须关闭**）：
  ```bash
  cd /c && "D:/Application/Epic Games/UE_5.8/Engine/Binaries/ThirdParty/DotNet/10.0/win-x64/dotnet.exe" \
    "D:/Application/Epic Games/UE_5.8/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.dll" \
    StellarEraEditor Win64 Development \
    -Project="F:/workspace/ue/StellarEra/StellarEra.uproject" -WaitMutex -FromMsBuild
  ```
- 测试（编辑器开着也能跑）：
  ```bash
  cd /c && "D:/Application/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" \
    "F:/workspace/ue/StellarEra/StellarEra.uproject" \
    -ExecCmds="Automation RunTests StellarEra" \
    -TestExit="Automation Test Queue Empty" \
    -unattended -nopause -nullrhi -nosplash -stdout \
    -abslog="C:/Users/23080/AppData/Local/Temp/se_starmap_input.log" > /dev/null 2>&1
  ```
  基线：231 项 = 230 Success + 1 Fail（`StellarEra.Planet.AtmosphereMaterial.Structure` 是既有失败，允许）。
- 含中文的 `.h` / `.cpp` 必须存 **UTF-8 with BOM**。
- 不要统一换行符（仓库本来就是 CRLF/LF 混合）。
- 模块依赖只能收窄不能新增反向边：`StellarEraPawns` **不能** include 任何 `StellarEra/` 下的头（Game 依赖 Pawns，反向成环）。
- 提交必须带 pathspec：`git commit -m "..." -- <paths>`（索引里有常驻暂存文件）。
- 不改键位映射（`FStellarEraInputActions::Build` 与 `IMC_StellarEra` 一律不动）。
- 星图相机的**位置 / 缩放算法**一律不动（`OrbitLook` / `AimLook` / `AddZoom` 的函数体不改一个字符）。

---

### Task 1: 把 `EStarMapLookMode` 与 `ResolveStarMapLookMode` 搬进 Pawns

**为什么必须先做这一步：** 这两个符号现在声明在 `Source/StellarEra/Src/StellarEraPlayerController.h`（Game 模块）。Task 2 的组件住在 Pawns，要用它们就得反向依赖 Game —— 模块环。它们是纯枚举 + 纯函数，没有任何 UObject 依赖，搬到 Pawns 是零成本。

**Files:**
- Create: `Source/StellarEraPawns/Src/StarMap/StarMapLookMode.h`
- Create: `Source/StellarEraPawns/Src/StarMap/StarMapLookMode.cpp`
- Modify: `Source/StellarEra/Src/StellarEraPlayerController.h:22-37`（删枚举）、`:136-158`（删静态函数声明）
- Modify: `Source/StellarEra/Src/StellarEraPlayerController.cpp:291-326`（删函数体）
- Modify: `Source/StellarEra/Src/Tests/StarMapTests.cpp:700-707`

**Interfaces:**
- Produces: `EStarMapLookMode`（`None` / `Orbit` / `Aim`）与
  `EStarMapLookMode ResolveStarMapLookMode(bool bRightHeld, bool bLeftHeld, bool bMiddleHeld, bool bEditMode, bool bContinuousDigging, bool bActiveMineralView)`
  —— 自由函数，声明在 `StarMapLookMode.h`，定义在 `StarMapLookMode.cpp`，签名与现有静态成员**逐字一致**。
  Task 2 的 `UStarMapCameraInputComponent` 与 `StarMapTests.cpp` 都消费它。

- [ ] **Step 1: 先改测试，让它红**

把 `Source/StellarEra/Src/Tests/StarMapTests.cpp` 第 698–707 行改成直接调自由函数：

```cpp
bool FStellarEraStarMapLookDispatchTest::RunTest(const FString& Parameters)
{
	using EMode = EStarMapLookMode;

	// 参数顺序：(右键, 左键, 中键, 编辑模式, 连续挖, 矿物透视)
	auto Resolve = [](bool bRight, bool bLeft, bool bMiddle, bool bEdit, bool bDig, bool bMineral)
	{
		return ResolveStarMapLookMode(bRight, bLeft, bMiddle, bEdit, bDig, bMineral);
	};
```

同时把 `:700` 那行 `using FController = AStellarEraPlayerController;` 整行删掉。

- [ ] **Step 2: 跑测试确认失败**

```bash
cd /c && "D:/Application/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" \
  "F:/workspace/ue/StellarEra/StellarEra.uproject" \
  -ExecCmds="Automation RunTests StellarEra.StarMap.LookDispatch" \
  -TestExit="Automation Test Queue Empty" \
  -unattended -nopause -nullrhi -nosplash -stdout \
  -abslog="C:/Users/23080/AppData/Local/Temp/se_lookdispatch_red.log" > /dev/null 2>&1
```

Expected：编译失败，`ResolveStarMapLookMode` 未定义（`C3861`）。这就是红灯。

- [ ] **Step 3: 建 `StarMapLookMode.h`（UTF-8 with BOM）**

```cpp
// Copyright Voxel Plugin SAS. All Rights Reserved.
// 星图鼠标位移的分派规则：纯枚举 + 纯函数，不碰任何 UObject，因此能住在 Pawns 里。

#pragma once

#include "CoreMinimal.h"

/**
 * 星图里鼠标位移该拿去干什么。
 *
 * - `Orbit`：绕焦点转（相机位置跟着变），见 AStarMapCameraPawn::OrbitLook。
 * - `Aim`：只转视线朝向（相机位置不变），见 AStarMapCameraPawn::AimLook。
 * - `None`：正在编辑 / 连续挖，视角一律不动。
 *
 * 与「哪个键按着」分开建模：右键在普通模式与编辑模式里都归 Aim，左键在编辑
 * 模式里归挖掘（None）—— 一个 bool 表达不了这种三路分派。
 */
enum class EStarMapLookMode : uint8
{
	None,
	Orbit,
	Aim,
};

/**
 * 星图的鼠标位移该拿去哪。
 *
 * 星图的鼠标位移只在按住鼠标（CaptureDuringMouseDown）时才有值，所以
 * 「按的是哪个键」只能靠按住的标志位区分。分派规则（按优先级）：
 *   - 连续挖期间一律 None —— 挖掘点跟着光标走，视角一动就画歪（转头也算，
 *     视线一变，屏幕射线就变了）。
 *   - 右键 → Aim：只转头、不挪相机，任何模式都可用。
 *   - 中键 → Orbit：编辑 / 矿物透视下左键被借走时，绕行改由它承担。
 *   - 左键 → Orbit，但编辑模式（挖掘 / 填充）与矿物透视（图例）下左键已被
 *     借走，只能是 None。
 *   - 都没按 → None。
 *
 * 右键优先于其它键：转头不改变相机位置，同时按住时先满足它更符合直觉。
 * 只决定「拿去哪」；缩放走滚轮，任何状态下都能用。
 */
EStarMapLookMode ResolveStarMapLookMode(
	bool bRightHeld,
	bool bLeftHeld,
	bool bMiddleHeld,
	bool bEditMode,
	bool bContinuousDigging,
	bool bActiveMineralView);
```

- [ ] **Step 4: 建 `StarMapLookMode.cpp`（UTF-8 with BOM）**

函数体从 `StellarEraPlayerController.cpp:291-326` **原样搬过来**，只把定义前的类限定符去掉：

```cpp
// Copyright Voxel Plugin SAS. All Rights Reserved.

#include "StarMapLookMode.h"

EStarMapLookMode ResolveStarMapLookMode(
	const bool bRightHeld,
	const bool bLeftHeld,
	const bool bMiddleHeld,
	const bool bEditMode,
	const bool bContinuousDigging,
	const bool bActiveMineralView)
{
	// 连续挖期间一个键都不放行：挖掘点跟着光标走，视角一动路径就画歪了。
	// 转头也算 —— 视线一变，屏幕射线跟着变，挖掘点一样会漂。
	if (bContinuousDigging)
	{
		return EStarMapLookMode::None;
	}

	// 右键优先：它只转头、不挪相机，与同时按住的其它键不冲突。
	if (bRightHeld)
	{
		return EStarMapLookMode::Aim;
	}

	// 中键绕行不看模式：编辑 / 矿物透视下左键被借走时，它就是唯一的绕行键。
	if (bMiddleHeld)
	{
		return EStarMapLookMode::Orbit;
	}

	// 左键在编辑模式（挖掘 / 填充）与矿物透视（图例点击）里已经被借走，
	// 这两种状态下按下左键必须什么都不做。
	if (bLeftHeld && !bEditMode && !bActiveMineralView)
	{
		return EStarMapLookMode::Orbit;
	}

	return EStarMapLookMode::None;
}
```

- [ ] **Step 5: 从控制器删掉旧声明与旧定义**

`StellarEraPlayerController.h`：删掉 `:22-37` 的 `EStarMapLookMode` 枚举连同它上面的注释块；删掉 `:136-158` 的 `ResolveStarMapLookMode` 声明连同它的注释块。

`StellarEraPlayerController.cpp`：删掉 `:291-326` 整个函数定义。

`:343` 与 `:381` 的注释里仍提到 `ResolveStarMapLookMode` —— 这两处 Task 2 会把整个函数删掉，本 Task 先留着不动。

- [ ] **Step 6: 构建**

用 Global Constraints 里的构建命令。Expected：`Result: Succeeded`。

- [ ] **Step 7: 跑测试确认变绿**

```bash
cd /c && "D:/Application/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" \
  "F:/workspace/ue/StellarEra/StellarEra.uproject" \
  -ExecCmds="Automation RunTests StellarEra.StarMap.LookDispatch" \
  -TestExit="Automation Test Queue Empty" \
  -unattended -nopause -nullrhi -nosplash -stdout \
  -abslog="C:/Users/23080/AppData/Local/Temp/se_lookdispatch_green.log" > /dev/null 2>&1
grep -E "Name=\{LookDispatch\}" "C:/Users/23080/AppData/Local/Temp/se_lookdispatch_green.log"
```

Expected：`Test Completed. Result={Success} Name={LookDispatch} Path={StellarEra.StarMap.LookDispatch}`。

- [ ] **Step 8: 提交**

```bash
git add Source/StellarEraPawns/Src/StarMap/StarMapLookMode.h \
        Source/StellarEraPawns/Src/StarMap/StarMapLookMode.cpp
git commit -m "refactor(星图): 视角分派规则搬进 Pawns，Game 不再为它反向依赖" \
  -- Source/StellarEraPawns/Src/StarMap/StarMapLookMode.h \
     Source/StellarEraPawns/Src/StarMap/StarMapLookMode.cpp \
     Source/StellarEra/Src/StellarEraPlayerController.h \
     Source/StellarEra/Src/StellarEraPlayerController.cpp \
     Source/StellarEra/Src/Tests/StarMapTests.cpp
```

---

### Task 2: `IStarMapLookContext` + `UStarMapCameraInputComponent`，控制器摘掉星图视角输入

这三个改动必须在**同一个提交**里落地：只建组件不解绑，同一个按键会被绑两次（回调跑两遍，视角转速翻倍）；只解绑不建组件，星图的鼠标直接失效。

**Files:**
- Create: `Source/StellarEraPawns/Src/StarMap/StarMapLookContext.h`
- Create: `Source/StellarEraPawns/Src/StarMap/StarMapCameraInputComponent.h`
- Create: `Source/StellarEraPawns/Src/StarMap/StarMapCameraInputComponent.cpp`
- Modify: `Source/StellarEraPawns/Src/StarMap/StarMapCameraPawn.h`（加组件成员与 `SetupPlayerInputComponent`）
- Modify: `Source/StellarEraPawns/Src/StarMap/StarMapCameraPawn.cpp`（构造函数建子对象、实现转发）
- Modify: `Source/StellarEra/Src/StellarEraPlayerController.h`
- Modify: `Source/StellarEra/Src/StellarEraPlayerController.cpp`
- Create: `Source/StellarEraPawns/Src/Tests/StarMapInputComponentTests.cpp`

**Interfaces:**
- Consumes: Task 1 的 `EStarMapLookMode` / `ResolveStarMapLookMode`；项目既有的 `IStellarEraInputSource`（`StellarEraInputActions.h`）与 `FStellarEraInputActions`。
- Produces:
  - `IStarMapLookContext`（四个只读纯虚）：`bool IsStarMapSelectHeld() const`、`bool IsEditModeActive() const`、`bool IsContinuousDigging() const`、`bool IsMineralViewActive() const`。
    `AStellarEraPlayerController` 实现它。
  - `UStarMapCameraInputComponent::Bind(UInputComponent* PlayerInputComponent)` 与两个可编辑属性 `LookSensitivity`（默认 1.2f）、`bInvertLookY`（默认 false）。
  - `AStarMapCameraPawn::PlayerInput`（`TObjectPtr<UStarMapCameraInputComponent>`，`VisibleAnywhere`）。

- [ ] **Step 1: 建 `StarMapLookContext.h`（UTF-8 with BOM）**

```cpp
// Copyright Voxel Plugin SAS. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "StarMapLookContext.generated.h"

UINTERFACE(MinimalAPI, meta = (CannotImplementInterfaceInBlueprint))
class UStarMapLookContext : public UInterface
{
	GENERATED_BODY()
};

/**
 * 星图相机判「鼠标位移该拿去干什么」时要问控制器的那几个问题。
 *
 * 相机输入组件住在 StellarEraPawns，判模式要的却是控制器的状态（左键按住、
 * 编辑模式、连挖、矿物透视）。直接 include 控制器 = 反向依赖 Game 模块，
 * 所以照 ICosmosBody 的做法抽成接口：游戏模块单向依赖 Pawns，实现放在
 * AStellarEraPlayerController 上，组件只认这四句话。
 *
 * 纯状态查询，没有 setter —— 谁按住、谁在编辑模式都由写的那一方维护。
 */
class STELLARERAPAWNS_API IStarMapLookContext
{
	GENERATED_BODY()

public:
	/** 星图里左键是否按住（选择用的按下 / 拖拽判定）。 */
	virtual bool IsStarMapSelectHeld() const = 0;

	/** 当前 Pawn 的编辑组件是否处于编辑模式（挖掘 / 填充）。 */
	virtual bool IsEditModeActive() const = 0;

	/** 编辑组件是否处于连续挖。编辑模式没开时恒为 false。 */
	virtual bool IsContinuousDigging() const = 0;

	/** 矿物透视（图例）是否激活；激活时左键归图例，不归绕行。 */
	virtual bool IsMineralViewActive() const = 0;
};
```

- [ ] **Step 2: 建 `StarMapCameraInputComponent.h`（UTF-8 with BOM）**

```cpp
// Copyright Voxel Plugin SAS. All Rights Reserved.
// 星图相机的键鼠输入：绑在相机身上，代码不住在相机类里。

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "StarMapCameraInputComponent.generated.h"

class AStarMapCameraPawn;
class IStarMapLookContext;
class UInputComponent;
struct FInputActionValue;

/**
 * AStarMapCameraPawn 的全部输入：绑定、回调、只服务输入的状态。
 *
 * 与 UPlanetCharacterInputComponent / USpaceshipInputComponent 同构。差别在
 * 「判视角去向」要问控制器（左键是不是在用、编辑模式开没开、连挖中不中、
 * 矿物透视有没有开），这部分走 IStarMapLookContext，不直接摸控制器。
 *
 * 三个 Pawn 各存一份 LookSensitivity：星图、飞船、角色的手感本来就可以不同，
 * 共用一个值反而要为了某一种模式去迁就另外两种。
 */
UCLASS()
class STELLARERAPAWNS_API UStarMapCameraInputComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	/** 把控制器给的动作表绑到本组件上。由 AStarMapCameraPawn::SetupPlayerInputComponent 调用。 */
	void Bind(UInputComponent* PlayerInputComponent);

	/** 鼠标灵敏度（度/像素）。星图与飞船、角色各存一份，见类注释。 */
	UPROPERTY(EditAnywhere, Category = "Input")
	float LookSensitivity = 1.2f;

	/** 上下视角反向。觉得鼠标上下颠倒时把它改成 true。 */
	UPROPERTY(EditAnywhere, Category = "Input")
	bool bInvertLookY = false;

private:
	/** 所属相机；组件只可能挂在星图相机上。 */
	AStarMapCameraPawn* GetPawn() const;

	/** 控制器的星图视角状态；拿不到时为空。 */
	const IStarMapLookContext* GetLookContext() const;

	void OnLookInput(const FInputActionValue& Value);
	void OnLookActivateStarted(const FInputActionValue& Value);
	void OnLookActivateCompleted(const FInputActionValue& Value);
	void OnLookMiddleStarted(const FInputActionValue& Value);
	void OnLookMiddleCompleted(const FInputActionValue& Value);
	void OnZoomInput(const FInputActionValue& Value);

	/** 右键是否按住（Aim 转头）。 */
	bool bLookHeld = false;

	/** 中键是否按住（编辑 / 矿物透视下借用左键的绕行）。 */
	bool bMiddleHeld = false;
};
```

- [ ] **Step 3: 建 `StarMapCameraInputComponent.cpp`（UTF-8 with BOM）**

```cpp
// Copyright Voxel Plugin SAS. All Rights Reserved.

#include "StarMapCameraInputComponent.h"

#include "EnhancedInputComponent.h"
#include "GameFramework/Controller.h"
#include "InputActionValue.h"
#include "StarMapCameraPawn.h"
#include "StarMapLookContext.h"
#include "StarMapLookMode.h"
#include "StellarEraInputActions.h"

AStarMapCameraPawn* UStarMapCameraInputComponent::GetPawn() const
{
	return Cast<AStarMapCameraPawn>(GetOwner());
}

const IStarMapLookContext* UStarMapCameraInputComponent::GetLookContext() const
{
	const AStarMapCameraPawn* Pawn = GetPawn();
	const AController* Controller = Pawn ? Pawn->GetController() : nullptr;
	return Cast<IStarMapLookContext>(Controller);
}

void UStarMapCameraInputComponent::Bind(UInputComponent* PlayerInputComponent)
{
	AStarMapCameraPawn* Pawn = GetPawn();
	UEnhancedInputComponent* EnhancedInput = Cast<UEnhancedInputComponent>(PlayerInputComponent);
	IStellarEraInputSource* Source = Pawn ? Cast<IStellarEraInputSource>(Pawn->GetController()) : nullptr;
	if (!Pawn || !EnhancedInput || !Source)
	{
		// 静默解绑等于「星图对键鼠完全没反应」——留一条 Error，别让它变成哑故障。
		UE_LOG(LogTemp, Error, TEXT("[Input] AStarMapCameraPawn 绑定失败：Pawn=%s EnhancedInput=%s InputSource=%s Controller=%s"),
			*GetNameSafe(Pawn),
			EnhancedInput ? TEXT("是") : TEXT("否"),
			Source ? TEXT("是") : TEXT("否"),
			*GetNameSafe(Pawn ? Pawn->GetController() : nullptr));
		return;
	}

	// 每次 Possess 都会重绑。按着鼠标切进 / 切出星图时，松开事件会送到别的 Pawn
	// 身上，标志就永远停在 true —— 一进星图没按键也在转视角。在这里清一次，
	// 比在控制器里按 Pawn 类型分支更稳：新增 / 删除 Pawn 时不用回来改。
	bLookHeld = false;
	bMiddleHeld = false;

	const FStellarEraInputActions& Input = Source->GetStellarEraInputActions();

	EnhancedInput->BindAction(Input.Look, ETriggerEvent::Triggered, this, &UStarMapCameraInputComponent::OnLookInput);
	EnhancedInput->BindAction(Input.LookActivate, ETriggerEvent::Started, this, &UStarMapCameraInputComponent::OnLookActivateStarted);
	EnhancedInput->BindAction(Input.LookActivate, ETriggerEvent::Completed, this, &UStarMapCameraInputComponent::OnLookActivateCompleted);
	EnhancedInput->BindAction(Input.LookMiddle, ETriggerEvent::Started, this, &UStarMapCameraInputComponent::OnLookMiddleStarted);
	EnhancedInput->BindAction(Input.LookMiddle, ETriggerEvent::Completed, this, &UStarMapCameraInputComponent::OnLookMiddleCompleted);

	// 只绑 Triggered：滚轮每格是一次独立冲量，不滚就不产生值，没有 Completed 可收。
	EnhancedInput->BindAction(Input.Zoom, ETriggerEvent::Triggered, this, &UStarMapCameraInputComponent::OnZoomInput);

	// 绑定成功也给一条 Verbose：「没报错」不等于「绑上了」，无头跑时用它当正证据。
	UE_LOG(LogTemp, Verbose, TEXT("[Input] AStarMapCameraPawn(%s) 已绑定星图相机输入"), *Pawn->GetName());
}

void UStarMapCameraInputComponent::OnLookInput(const FInputActionValue& Value)
{
	AStarMapCameraPawn* Pawn = GetPawn();
	const IStarMapLookContext* Context = GetLookContext();
	if (!Pawn || !Context)
	{
		return;
	}

	const FVector2D Axis = Value.Get<FVector2D>();
	const float DeltaYaw = Axis.X * LookSensitivity;
	const float DeltaPitch = (bInvertLookY ? -Axis.Y : Axis.Y) * LookSensitivity;

	// 星图的鼠标位移只在按住鼠标（CaptureDuringMouseDown）时才有值，「现在按的是
	// 哪个键」靠各键的按住标志区分；拿去哪由 ResolveStarMapLookMode 决定。
	switch (ResolveStarMapLookMode(
		bLookHeld,
		Context->IsStarMapSelectHeld(),
		bMiddleHeld,
		Context->IsEditModeActive(),
		Context->IsContinuousDigging(),
		Context->IsMineralViewActive()))
	{
	case EStarMapLookMode::Orbit:
		Pawn->OrbitLook(DeltaYaw, DeltaPitch);
		break;
	case EStarMapLookMode::Aim:
		Pawn->AimLook(DeltaYaw, DeltaPitch);
		break;
	case EStarMapLookMode::None:
	default:
		break;
	}
}

void UStarMapCameraInputComponent::OnLookActivateStarted(const FInputActionValue& Value)
{
	// 「按住右键 = 转头」。角色的右键（视角开关 + 编辑点放）由
	// UPlanetCharacterInputComponent 自己绑同一个动作、自己处理。
	bLookHeld = true;
}

void UStarMapCameraInputComponent::OnLookActivateCompleted(const FInputActionValue& Value)
{
	bLookHeld = false;
}

void UStarMapCameraInputComponent::OnLookMiddleStarted(const FInputActionValue& Value)
{
	// 编辑模式下左键被借走时的绕行键，见 ResolveStarMapLookMode。
	bMiddleHeld = true;
}

void UStarMapCameraInputComponent::OnLookMiddleCompleted(const FInputActionValue& Value)
{
	bMiddleHeld = false;
}

void UStarMapCameraInputComponent::OnZoomInput(const FInputActionValue& Value)
{
	if (AStarMapCameraPawn* Pawn = GetPawn())
	{
		Pawn->AddZoom(Value.Get<float>());
	}
}
```

> `Cast<IStarMapLookContext>(Controller)` 要求 `StarMapLookContext.h` 可见；`IStellarEraInputSource`、`FStellarEraInputActions` 来自 `StellarEraInputActions.h`（已在 Pawns）。

- [ ] **Step 4: 相机 Pawn 接线**

`StarMapCameraPawn.h`：

在 include 段下方的 forward declaration 区（`class UVoxelSimpleInvokerComponent;` 之后）加一行：

```cpp
class UStarMapCameraInputComponent;
```

在 `public:` 段（构造函数声明 `AStarMapCameraPawn();` 之后）加：

```cpp
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;
```

在 `protected:` 段里、第一个 `UPROPERTY` 组件成员之前（`Camera` 那一段附近）加：

```cpp
	/** 键鼠输入。回调与输入状态都在组件里，本类只负责转发绑定。 */
	UPROPERTY(VisibleAnywhere, Category = "Input")
	TObjectPtr<UStarMapCameraInputComponent> PlayerInput;
```

`StarMapCameraPawn.cpp`：

include 段加：

```cpp
#include "StarMapCameraInputComponent.h"
```

构造函数里，`Camera` 建完之后（`SetRootComponent(Camera);` 与 `Camera->bUsePawnControlRotation = false;` 之后）加：

```cpp
	// 星图的键鼠输入：与角色 / 飞船同构，回调住在组件里。
	PlayerInput = CreateDefaultSubobject<UStarMapCameraInputComponent>(TEXT("PlayerInput"));
```

构造函数之后（`AStarMapCameraPawn::AStarMapCameraPawn()` 那个函数体的右花括号之后）加：

```cpp
void AStarMapCameraPawn::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	if (PlayerInput)
	{
		PlayerInput->Bind(PlayerInputComponent);
	}
}
```

- [ ] **Step 5: 控制器摘掉星图视角输入**

`StellarEraPlayerController.h`：

1. 类声明改成三个接口（`IStarMapUIInputs`、`IStellarEraInputSource` 之后再挂一个）：

```cpp
class AStellarEraPlayerController : public APlayerController, public IStarMapUIInputs,
	public IStellarEraInputSource, public IStarMapLookContext
```

2. include 段加 `#include "StarMapLookContext.h"`。

3. 在 `protected:` 或 `private:` 里加实现声明（放在 `GetEditComponentInEditMode` 旁边即可）：

```cpp
	//~ Begin IStarMapLookContext Interface
	virtual bool IsStarMapSelectHeld() const override;
	virtual bool IsEditModeActive() const override;
	virtual bool IsContinuousDigging() const override;
	virtual bool IsMineralViewActive() const override;
	//~ End IStarMapLookContext Interface
```

4. 删掉这些成员：`bLookHeld`、`bStarMapMiddleHeld`、`LookSensitivity`、`bInvertLookY`，以及它们上面的注释块。
   **`bStarMapSelectHeld` 保留** —— 选择逻辑还在本类里，接口第 1 条要读它。

5. 删掉这些函数声明：`OnLook`、`OnLookActivateStarted`、`OnLookActivateCompleted`、`OnLookMiddleStarted`、`OnLookMiddleCompleted`、`OnZoom`。

6. 类注释 `:46-51` 那段「输入绑定按谁被操控谁绑分」里，「本类只绑控制器级的东西……以及**星图模式**下的鼠标」要改成「以及星图模式下的**选择**（左键按下 / 拖拽 / 连挖收尾）；星图的视角与缩放已归相机自己的 `UStarMapCameraInputComponent`」。

`StellarEraPlayerController.cpp`：

1. `SetupInputComponent` 里删掉这 6 行绑定与它们的注释：

```cpp
	EnhancedInput->BindAction(Input.Look, ETriggerEvent::Triggered, this, &AStellarEraPlayerController::OnLook);
	EnhancedInput->BindAction(Input.LookActivate, ETriggerEvent::Started, this, &AStellarEraPlayerController::OnLookActivateStarted);
	EnhancedInput->BindAction(Input.LookActivate, ETriggerEvent::Completed, this, &AStellarEraPlayerController::OnLookActivateCompleted);
	EnhancedInput->BindAction(Input.LookMiddle, ETriggerEvent::Started, this, &AStellarEraPlayerController::OnLookMiddleStarted);
	EnhancedInput->BindAction(Input.LookMiddle, ETriggerEvent::Completed, this, &AStellarEraPlayerController::OnLookMiddleCompleted);
	EnhancedInput->BindAction(Input.Zoom, ETriggerEvent::Triggered, this, &AStellarEraPlayerController::OnZoom);
```

留下 `StarMapToggle` / `StarMapSelect` / `EditModeToggle` / `BrushSize` / `Interact` / 矿物那几条。原注释「这里只留『只有星图相机在用时才有意义』的鼠标与滚轮」改成：

```cpp
	// 角色 / 飞船 / 星图相机的移动、视角、推力由各自的 Pawn 在自己的
	// SetupPlayerInputComponent 里绑定；这里只留控制器自己的那几条 ——
	// 上下船、星图开关与**选择**、编辑、矿物透视。
```

2. 删掉 `OnLook`、`OnLookActivateStarted`、`OnLookActivateCompleted`、`OnLookMiddleStarted`、`OnLookMiddleCompleted`、`OnZoom` 六个函数定义。

3. `ApplyMouseModeForPawn` 里三行清零改成一行：

```cpp
	// 换 Pawn 时把「左键按住」标志清零：按着键登船 / 离船 / 进星图的话，松开事件
	// 会送到新 Pawn 身上，标志就永远停在 true，星图里没按左键也在转视角。
	// 视角那两个标志（右键 / 中键）现在归 UStarMapCameraInputComponent，
	// 它在每次 Bind 时自己清。
	bStarMapSelectHeld = false;
```

4. 加四个接口实现（放在 `GetEditComponentInEditMode` 之后）：

```cpp
bool AStellarEraPlayerController::IsStarMapSelectHeld() const
{
	return bStarMapSelectHeld;
}

bool AStellarEraPlayerController::IsEditModeActive() const
{
	return GetEditComponentInEditMode() != nullptr;
}

bool AStellarEraPlayerController::IsContinuousDigging() const
{
	const UPlanetEditComponent* Edit = GetEditComponentInEditMode();
	return Edit && Edit->IsContinuousDigging();
}

bool AStellarEraPlayerController::IsMineralViewActive() const
{
	return ActiveMineralView != nullptr;
}
```

- [ ] **Step 6: 建 `StarMapInputComponentTests.cpp`（UTF-8 with BOM）**

```cpp
// Copyright Voxel Plugin SAS. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "StarMapCameraInputComponent.h"
#include "StarMapCameraPawn.h"

#if WITH_DEV_AUTOMATION_TESTS

// 断言 CDO，验的是「组件真的建在类上、默认值没被人改跑」。
// 「真的绑上了」的证据在无头运行日志里（[Input] AStarMapCameraPawn(...) 已绑定星图相机输入）。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraStarMapInputComponentWiringTest,
	"StellarEra.Pawns.StarMapInputComponentWiring",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraStarMapInputComponentWiringTest::RunTest(const FString& Parameters)
{
	const AStarMapCameraPawn* CameraCDO = GetDefault<AStarMapCameraPawn>();
	if (!TestNotNull(TEXT("星图相机 CDO"), CameraCDO))
	{
		return false;
	}

	const UStarMapCameraInputComponent* Input = CameraCDO->FindComponentByClass<UStarMapCameraInputComponent>();
	if (!TestNotNull(TEXT("相机 CDO 上有 UStarMapCameraInputComponent"), Input))
	{
		return false;
	}

	TestEqual(TEXT("默认灵敏度"), Input->LookSensitivity, 1.2f);
	TestFalse(TEXT("默认不反 Y"), Input->bInvertLookY);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
```

> `LookSensitivity` / `bInvertLookY` 是 `public`，CDO 上直接读。若 `PlayerInput` 成员是 `protected`，`FindComponentByClass` 依然可用（它走组件列表，不走成员）。

- [ ] **Step 7: 构建**

用 Global Constraints 里的构建命令。Expected：`Result: Succeeded`。
特别注意：`StarMapCameraInputComponent.cpp` **不能** include `StellarEraPlayerController.h`（模块环，UBT 会报循环依赖或 C1083）。

- [ ] **Step 8: 跑全量测试**

用 Global Constraints 里的测试命令。Expected：232 项 = 231 Success + 1 Fail（既有 `AtmosphereMaterial.Structure`）；`StellarEra.Pawns.StarMapInputComponentWiring` 与 `StellarEra.StarMap.LookDispatch` 都 Success。

- [ ] **Step 9: 无头验证（星图输入真的绑上了）**

`-game` 模式下星图相机默认**不**被 Possess，要进星图才有。用控制台的 `-ExecCmds` 调控制器的 `ToggleStarMap`：

```bash
cd /c && UE="D:/Application/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor.exe"; \
MSYS2_ARG_CONV_EXCL='*' timeout 150 "$UE" "F:/workspace/ue/StellarEra/StellarEra.uproject" \
  /Game/Code/Maps/UniverseMap -game -nullrhi -nosplash -unattended -stdout \
  -LogCmds="LogTemp Verbose" -ExecCmds="DebugDisembark,ToggleStarMap" \
  -abslog="C:/Users/23080/AppData/Local/Temp/se_starmap_input.log" > /dev/null 2>&1; \
grep -nE "\[Input\]|绑定失败|Failed to enter" "C:/Users/23080/AppData/Local/Temp/se_starmap_input.log" | head
```

Expected：出现 `[Input] AStarMapCameraPawn(...) 已绑定星图相机输入`；`绑定失败` 0 条；`Failed to enter` 0 条。

> 若 `ToggleStarMap` 在 `-ExecCmds` 里跑得太早（控制器还没 Possess 飞船），日志里不会出现相机那条。此时改成两步：先 `DebugDisembark`，再 `ToggleStarMap`，仍然拿不到就记进报告 —— **不要**据此声称验证通过。

- [ ] **Step 10: 提交**

```bash
git add Source/StellarEraPawns/Src/StarMap/StarMapLookContext.h \
        Source/StellarEraPawns/Src/StarMap/StarMapCameraInputComponent.h \
        Source/StellarEraPawns/Src/StarMap/StarMapCameraInputComponent.cpp \
        Source/StellarEraPawns/Src/Tests/StarMapInputComponentTests.cpp
git commit -m "refactor(星图): 视角与缩放输入抽成 UStarMapCameraInputComponent" \
  -- Source/StellarEraPawns/Src/StarMap/StarMapLookContext.h \
     Source/StellarEraPawns/Src/StarMap/StarMapCameraInputComponent.h \
     Source/StellarEraPawns/Src/StarMap/StarMapCameraInputComponent.cpp \
     Source/StellarEraPawns/Src/StarMap/StarMapCameraPawn.h \
     Source/StellarEraPawns/Src/StarMap/StarMapCameraPawn.cpp \
     Source/StellarEraPawns/Src/Tests/StarMapInputComponentTests.cpp \
     Source/StellarEra/Src/StellarEraPlayerController.h \
     Source/StellarEra/Src/StellarEraPlayerController.cpp
```

---

### Task 3: 文档同步 + 全量验证

**Files:**
- Modify: `docs/ARCHITECTURE.md`
- Modify: `Source/README.md`
- Modify: `CLAUDE.md`（只在「输入按谁被操控谁绑」那一类描述里有一句需要改时才动）

- [ ] **Step 1: 找出文档里所有描述「星图鼠标绑在控制器上」的句子**

```bash
cd "F:/workspace/ue/StellarEra"
grep -rn "星图.*鼠标\|鼠标.*星图\|StarMapCamera\|输入组件\|InputComponent" docs/ARCHITECTURE.md Source/README.md CLAUDE.md
```

- [ ] **Step 2: 逐条改成搬家后的事实**

必须成立的三句话：
- 三个可操控对象（角色、飞船、星图相机）各有自己的输入组件，Pawn 只转发 `SetupPlayerInputComponent`；
- 控制器只绑控制器级输入（上下船、星图开关与选择、编辑、矿物透视）；
- `EStarMapLookMode` / `ResolveStarMapLookMode` 住在 `StellarEraPawns/Src/StarMap/`。

依赖矩阵**不变**（本次没有新增或删除模块边），如果文档里那张表本来就正确，不要动它。

- [ ] **Step 3: 重算文档里的模块规模统计**

只改 `StellarEraPawns` 那一行（新增 5 个文件：`StarMapLookMode.h/.cpp`、`StarMapLookContext.h`、`StarMapCameraInputComponent.h/.cpp`；新增 1 个测试文件）。口径与 `docs/ARCHITECTURE.md:13-17` 里写的一致：

```bash
cd "F:/workspace/ue/StellarEra"
for m in StellarEra StellarEraPlanet StellarEraPawns StellarEraCosmos StellarEraUI; do
  f=$(find "Source/$m/Src" -type f \( -name '*.h' -o -name '*.cpp' \) | wc -l)
  l=$(find "Source/$m/Src" -type f \( -name '*.h' -o -name '*.cpp' \) -exec wc -l {} + | tail -1 | awk '{print $1}')
  t=$(find "Source/$m/Src/Tests" -maxdepth 1 -type f -name '*Tests.cpp' 2>/dev/null | wc -l)
  echo "$m 文件=$f 行=$l 测试=$t"
done
```

把结果填进 `docs/ARCHITECTURE.md` 的模块表与汇总句、`Source/README.md` 的对应行。**五列都要重算**，不要只改 Pawns —— 上一轮就是「只改一处」连修四轮。

- [ ] **Step 4: 架构体检**

```bash
cd "F:/workspace/ue/StellarEra" && PYTHONIOENCODING=utf-8 python Tools/check_architecture.py 2>&1 | head -20
```

Expected：`── 依赖环 ──` 下为「无」；`StellarEraPawns` 的依赖列仍是 `Cosmos, Planet`（没有新增 `StellarEra`）。

- [ ] **Step 5: 全量测试**

用 Global Constraints 里的测试命令，Expected：231 Success + 1 Fail（既有）。

- [ ] **Step 6: 提交**

```bash
git commit -m "docs(架构): 同步星图相机输入组件" -- docs/ARCHITECTURE.md Source/README.md
```

（若 `CLAUDE.md` 也改了，把它的路径一并加进 pathspec。）

---

## Self-Review

**1. 覆盖度**

| 用户要求 | 任务 |
|---|---|
| 星图的输入映射做成和飞船角色一样的组件 | Task 2 |
| 星图相机在 Pawns 模块内落地 | Task 1、Task 2（都在 `Src/StarMap/`） |
| 灵敏度归属：每组件各自一份（用户选定） | Task 2 Step 2、Step 3 |
| 模块不出现环 | Task 1（先把纯函数搬到低层）、Task 2 Step 7 的构建检查、Task 3 Step 4 |
| 运行行为不变 | Task 2 Step 5 只搬不改；`OrbitLook` / `AimLook` / `AddZoom` 一行不动 |

**2. 占位符扫描**：无 TBD / TODO；每个代码步骤都给了可直接粘贴的完整文件或逐行替换。

**3. 类型一致性**

- `ResolveStarMapLookMode` 六参数顺序 `(右, 左, 中, 编辑, 连挖, 矿物)` 在 Task 1 的定义、Task 2 的调用、`StarMapTests.cpp` 的 lambda 三处一致。
- `IStarMapLookContext` 四个方法与 Task 2 Step 5 的四段实现逐一对应：`IsStarMapSelectHeld`→`bStarMapSelectHeld`、`IsEditModeActive`→`GetEditComponentInEditMode() != nullptr`、`IsContinuousDigging`→`Edit->IsContinuousDigging()`、`IsMineralViewActive`→`ActiveMineralView != nullptr`。
- 组件成员名 `PlayerInput` 与 `APlanetCharacter` / `ASpaceshipPawn` 的 `PlayerInput` 一致。
- 日志串 `[Input] AStarMapCameraPawn(...) 已绑定星图相机输入` 在 Task 2 Step 3 与 Step 9 的 grep 里一致。

**4. 已知风险**

| 风险 | 处置 |
|---|---|
| `ToggleStarMap` 在无头 `-ExecCmds` 里时机不对，拿不到相机那条日志 | Task 2 Step 9 已写明：拿不到就如实记进报告，不声称通过 |
| 相机 Pawn 是 `BP_VoxelPlanet` 的 Child Actor，蓝图里可能也挂了同名组件 | 构建 + 跑测试时会暴露重复组件；`StarMapInputComponentWiring` 断言 CDO 上找得到组件即可，若蓝图重复则由 Task 3 的验证复核 |
| 删掉控制器那 6 行绑定后，若相机 Pawn 在某些路径上没走 `SetupPlayerInputComponent`，星图会静默没反应 | 无头验证 Task 2 Step 9 的 Verbose 日志就是为此设的正证据 |
