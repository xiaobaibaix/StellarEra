# 星图相机输入抽成组件 — 实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 把 `AStellarEraPlayerController` 上那组星图鼠标 / 滚轮绑定搬进 `AStarMapCameraPawn` 自己的 `UStarMapCameraInputComponent`，与 `USpaceshipInputComponent` **完全同形**：一个组件、一个 `Bind()`、一组 `On*Input` 回调，不另拆类、不另建文件。

**Architecture:** 星图的键鼠输入现在绑在控制器上，`OnLook` / `OnZoom` 里再 `Cast<AStarMapCameraPawn>`。搬成组件后绑定只存在于相机 Pawn 上，`Cast` 消失；`OnLookInput` 里直接 if/else 分派，不引入额外的分派层。判分派要的两个控制器状态（左键是否按住、矿物透视是否开着）通过 `IStarMapLookContext` 的两句话取；编辑模式与连挖直接读 `AStarMapCameraPawn::GetPlanetEditComponent()` —— 相机 Pawn 自己就挂着那组件。

**Tech Stack:** Unreal Engine 5.8.3，Enhanced Input，原生 UINTERFACE（`CannotImplementInterfaceInBlueprint`，与 `ICosmosBody` 同写法）。

**Spec:** 无独立 spec。设计要求由用户在 2026-11-06 直接提出，并在随后的追问里明确「统一规范，不要搞特殊」——按 `USpaceshipInputComponent` 的形状来，不额外抽 `EStarMapLookMode` 之类的东西。

## Global Constraints

- 构建（**编辑器必须关闭**）：
  ```bash
  cd /c && "D:/Application/Epic Games/UE_5.8/Engine/Binaries/ThirdParty/DotNet/10.0/win-x64/dotnet.exe" \
    "D:/Application/Epic Games/UE_5.8/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.dll" \
    StellarEraEditor Win64 Development \
    -Project="F:/workspace/ue/StellarEra/StellarEra.uproject" -WaitMutex -FromMsBuild
  ```
  成功是 `Result: Succeeded`。注意 `Result: Failed` 也可能是「编译全过、只有 DLL 链接失败」—— 要看具体错误行。
- 测试（编辑器开着也能跑）：
  ```bash
  cd /c && "D:/Application/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" \
    "F:/workspace/ue/StellarEra/StellarEra.uproject" \
    -ExecCmds="Automation RunTests StellarEra" \
    -TestExit="Automation Test Queue Empty" \
    -unattended -nopause -nullrhi -nosplash -stdout \
    -abslog="C:/Users/23080/AppData/Local/Temp/se_starmap_input.log" > /dev/null 2>&1
  ```
  基线：231 项 = 230 Success + 1 Fail（`StellarEra.Planet.AtmosphereMaterial.Structure` 是既有失败，允许）。目标：232 项 = 231 Success + 1 Fail。
  **注意：`UnrealEditor-Cmd` 不做编译**，红灯只能靠 UBT 构建拿到。
- 含中文的 `.h` / `.cpp` 必须存 **UTF-8 with BOM**（`head -c 3` 应为 `ef bb bf`）。
- 不要统一换行符（仓库本来就是 CRLF/LF 混合）。
- 模块依赖只能收窄不能新增反向边：`StellarEraPawns` **不能** include 任何 `Source/StellarEra/` 下的头。
- 提交必须带 pathspec：`git commit -m "..." -- <paths>`。提交信息末行加 `Co-Authored-By: Claude Code <noreply@anthropic.com>`。
- 不改键位映射（`FStellarEraInputActions::Build` 与 `IMC_StellarEra` 一律不动）。
- 星图相机的位置 / 缩放算法一律不动：`OrbitLook` / `AimLook` / `AddZoom` 的函数体不改一个字符。

---

### Task 1: `UStarMapCameraInputComponent` + 控制器摘掉星图视角输入

**Files:**
- Create: `Source/StellarEraPawns/Src/StarMap/StarMapCameraInputComponent.h`
- Create: `Source/StellarEraPawns/Src/StarMap/StarMapCameraInputComponent.cpp`
- Modify: `Source/StellarEraPawns/Src/StarMap/StarMapCameraPawn.h`
- Modify: `Source/StellarEraPawns/Src/StarMap/StarMapCameraPawn.cpp`
- Modify: `Source/StellarEra/Src/StellarEraPlayerController.h`
- Modify: `Source/StellarEra/Src/StellarEraPlayerController.cpp`
- Modify: `Source/StellarEra/Src/Tests/StarMapTests.cpp:700-707`
- Create: `Source/StellarEraPawns/Src/Tests/StarMapInputComponentTests.cpp`

**现状（必须先读，否则会改错）：**
- `Source/StellarEra/Src/StellarEraPlayerController.h:32-37` 有 `enum class EStarMapLookMode : uint8 { None, Orbit, Aim }`；
- `:152-158` 有 `static EStarMapLookMode ResolveStarMapLookMode(bool, bool, bool, bool, bool, bool)`；
- `:307-334` 有 `bLookHeld` / `LookSensitivity` / `bInvertLookY` / `bStarMapSelectHeld` / `bStarMapMiddleHeld`；
- `.cpp:291-326` 是 `ResolveStarMapLookMode` 的函数体；`.cpp:328-398` 是 `OnLook` / `OnLookActivate*` / `OnLookMiddle*` / `OnZoom`；`.cpp:106-113` 是这 6 处绑定。

**Interfaces:**
- Produces:
  - `EStarMapLookMode`（`None` / `Orbit` / `Aim`）—— 从控制器头搬进本组件头，**不单独建文件**。
  - `UStarMapCameraInputComponent::ResolveLookMode(bool bRightHeld, bool bLeftHeld, bool bMiddleHeld, bool bEditMode, bool bContinuousDigging, bool bActiveMineralView)` —— **静态成员**，函数体与现有自由函数逐字相同。`StarMapTests.cpp` 调它。
  - `IStarMapLookContext`（两个只读纯虚）：`bool IsStarMapSelectHeld() const`、`bool IsMineralViewActive() const`。`AStellarEraPlayerController` 实现。
  - `UStarMapCameraInputComponent::Bind(UInputComponent* PlayerInputComponent)`；可编辑属性 `LookSensitivity`（1.2f）、`bInvertLookY`（false）。
  - `AStarMapCameraPawn::PlayerInput`（`TObjectPtr<UStarMapCameraInputComponent>`，`VisibleAnywhere`）。

- [ ] **Step 1: 建 `StarMapCameraInputComponent.h`（UTF-8 with BOM）**

一个头里放三样：枚举、接口、组件。接口和组件同文件是为了不让这次改动多出一个「特殊」的头文件。

```cpp
// Copyright Voxel Plugin SAS. All Rights Reserved.
// 星图相机的键鼠输入：绑在相机身上，代码不住在相机类里。

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "UObject/Interface.h"
#include "StarMapCameraInputComponent.generated.h"

class AStarMapCameraPawn;
class UInputComponent;
class UPlanetEditComponent;
struct FInputActionValue;

/**
 * 星图里鼠标位移该拿去干什么。
 *
 * - `Orbit`：绕焦点转（相机位置跟着变），见 AStarMapCameraPawn::OrbitLook。
 * - `Aim`：只转视线朝向（相机位置不变），见 AStarMapCameraPawn::AimLook。
 * - `None`：正在编辑 / 连续挖，视角一律不动。
 */
enum class EStarMapLookMode : uint8
{
	None,
	Orbit,
	Aim,
};

UINTERFACE(MinimalAPI, meta = (CannotImplementInterfaceInBlueprint))
class UStarMapLookContext : public UInterface
{
	GENERATED_BODY()
};

/**
 * 星图相机判「鼠标位移该拿去干什么」时要问控制器的那两句话。
 *
 * 左键按住与矿物透视都是控制器的状态（选择逻辑、图例面板都在那边），
 * 组件住 StellarEraPawns、控制器住 StellarEra，直接 include 会反向依赖
 * Game 模块。做法照 ICosmosBody：游戏模块单向依赖 Pawns，实现放控制器上。
 *
 * 只有这两个：编辑模式与连续挖直接从相机 Pawn 自己的
 * GetPlanetEditComponent() 读，不绕控制器。
 */
class STELLARERAPAWNS_API IStarMapLookContext
{
	GENERATED_BODY()

public:
	/** 星图里左键是否按住（选择用的按下 / 拖拽判定）。 */
	virtual bool IsStarMapSelectHeld() const = 0;

	/** 矿物透视（图例）是否激活；激活时左键归图例，不归绕行。 */
	virtual bool IsMineralViewActive() const = 0;
};

/**
 * AStarMapCameraPawn 的全部输入：绑定、回调、只服务输入的状态。
 *
 * 与 USpaceshipInputComponent 同形：一个 Bind()、一组 On*Input，回调里直接
 * 判着走，不另抽分派层。三个 Pawn 各存一份 LookSensitivity —— 星图、飞船、
 * 角色的手感本来就可以不同。
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

	/**
	 * 星图的鼠标位移该拿去哪。纯函数，OnLookInput 调它，测试直接调它。
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
	 */
	static EStarMapLookMode ResolveLookMode(
		bool bRightHeld,
		bool bLeftHeld,
		bool bMiddleHeld,
		bool bEditMode,
		bool bContinuousDigging,
		bool bActiveMineralView);

private:
	/** 所属相机；组件只可能挂在星图相机上。 */
	AStarMapCameraPawn* GetPawn() const;

	/** 控制器的星图视角状态；拿不到时为空。 */
	const IStarMapLookContext* GetLookContext() const;

	/** 相机 Pawn 自己的编辑组件，且只在编辑模式下才返回。 */
	const UPlanetEditComponent* GetEditComponentInEditMode() const;

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

- [ ] **Step 2: 建 `StarMapCameraInputComponent.cpp`（UTF-8 with BOM）**

`ResolveLookMode` 的函数体从 `StellarEraPlayerController.cpp:291-326` **原样搬过来**，只改名字与限定符。

```cpp
// Copyright Voxel Plugin SAS. All Rights Reserved.

#include "StarMapCameraInputComponent.h"

#include "EnhancedInputComponent.h"
#include "GameFramework/Controller.h"
#include "InputActionValue.h"
#include "PlanetEditComponent.h"
#include "StarMapCameraPawn.h"
#include "StellarEraInputActions.h"

AStarMapCameraPawn* UStarMapCameraInputComponent::GetPawn() const
{
	return Cast<AStarMapCameraPawn>(GetOwner());
}

const IStarMapLookContext* UStarMapCameraInputComponent::GetLookContext() const
{
	// 用非 const 的 AController* 做 Cast，避开「const UObject* 能否 Cast 到接口」这个坑；
	// 接口方法全是 const，返回值再收窄成 const 指针。
	AController* Controller = GetPawn() ? GetPawn()->GetController() : nullptr;
	return Cast<IStarMapLookContext>(Controller);
}

const UPlanetEditComponent* UStarMapCameraInputComponent::GetEditComponentInEditMode() const
{
	const AStarMapCameraPawn* Pawn = GetPawn();
	const UPlanetEditComponent* Edit = Pawn ? Pawn->GetPlanetEditComponent() : nullptr;
	return Edit && Edit->IsEditMode() ? Edit : nullptr;
}

EStarMapLookMode UStarMapCameraInputComponent::ResolveLookMode(
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
	// 身上，标志就永远停在 true —— 一进星图没按键也在转视角。在这里清一次。
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
	if (!Pawn)
	{
		return;
	}

	const FVector2D Axis = Value.Get<FVector2D>();
	const float DeltaYaw = Axis.X * LookSensitivity;
	const float DeltaPitch = (bInvertLookY ? -Axis.Y : Axis.Y) * LookSensitivity;

	const UPlanetEditComponent* Edit = GetEditComponentInEditMode();
	const IStarMapLookContext* Context = GetLookContext();

	switch (ResolveLookMode(
		bLookHeld,
		Context && Context->IsStarMapSelectHeld(),
		bMiddleHeld,
		Edit != nullptr,
		Edit && Edit->IsContinuousDigging(),
		Context && Context->IsMineralViewActive()))
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
	// 编辑模式下左键被借走时的绕行键，见 ResolveLookMode。
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

> `ResolveLookMode` 是 `STELLARERAPAWNS_API` 类上的静态成员，跨 DLL 调用由类的导出宏覆盖，不需要额外标注。
> `StarMapCameraInputComponent.cpp` **绝不能** include `StellarEraPlayerController.h`（模块环）。

- [ ] **Step 3: 相机 Pawn 接线**

`StarMapCameraPawn.h`：

1. 前向声明区（`class UVoxelSimpleInvokerComponent;` 之后）加：
   ```cpp
   class UStarMapCameraInputComponent;
   ```
2. `public:` 段（构造函数声明 `AStarMapCameraPawn();` 之后）加：
   ```cpp
   	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;
   ```
3. 组件成员区（`Camera` 那一带）加：
   ```cpp
   	/** 键鼠输入。回调与输入状态都在组件里，本类只负责转发绑定。 */
   	UPROPERTY(VisibleAnywhere, Category = "Input")
   	TObjectPtr<UStarMapCameraInputComponent> PlayerInput;
   ```

`StarMapCameraPawn.cpp`：

1. include 段加 `#include "StarMapCameraInputComponent.h"`。
2. 构造函数里，`Camera->bUsePawnControlRotation = false;` 之后加：
   ```cpp
   	// 星图的键鼠输入：与角色 / 飞船同构，回调住在组件里。
   	PlayerInput = CreateDefaultSubobject<UStarMapCameraInputComponent>(TEXT("PlayerInput"));
   ```
3. 构造函数之后加：
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

- [ ] **Step 4: 控制器摘掉星图视角输入**

`StellarEraPlayerController.h`：

1. 删掉 `:22-37` 的 `EStarMapLookMode` 枚举（连同注释）与 `:136-158` 的 `ResolveStarMapLookMode` 声明（连同注释）。
2. 类声明挂上第三个接口：
   ```cpp
   class AStellarEraPlayerController : public APlayerController, public IStarMapUIInputs,
   	public IStellarEraInputSource, public IStarMapLookContext
   ```
3. include 段加 `#include "StarMapCameraInputComponent.h"`。
4. `GetEditComponentInEditMode` 旁边加：
   ```cpp
   	//~ Begin IStarMapLookContext Interface
   	virtual bool IsStarMapSelectHeld() const override;
   	virtual bool IsMineralViewActive() const override;
   	//~ End IStarMapLookContext Interface
   ```
5. 删掉这四个成员及其注释：`bLookHeld`、`bStarMapMiddleHeld`、`LookSensitivity`、`bInvertLookY`。
   **`bStarMapSelectHeld` 保留**（选择逻辑还在本类，接口第 1 条读它）。
6. 删掉这六个函数声明：`OnLook`、`OnLookActivateStarted`、`OnLookActivateCompleted`、`OnLookMiddleStarted`、`OnLookMiddleCompleted`、`OnZoom`。
7. 类注释里「本类只绑控制器级的东西……以及**星图模式**下的鼠标」改成「以及星图模式下的**选择**（左键按下 / 拖拽 / 连挖收尾）；星图的视角与缩放归相机自己的 `UStarMapCameraInputComponent`」。

`StellarEraPlayerController.cpp`：

1. `SetupInputComponent` 删掉这 6 行绑定：
   ```cpp
   	EnhancedInput->BindAction(Input.Look, ETriggerEvent::Triggered, this, &AStellarEraPlayerController::OnLook);
   	EnhancedInput->BindAction(Input.LookActivate, ETriggerEvent::Started, this, &AStellarEraPlayerController::OnLookActivateStarted);
   	EnhancedInput->BindAction(Input.LookActivate, ETriggerEvent::Completed, this, &AStellarEraPlayerController::OnLookActivateCompleted);
   	EnhancedInput->BindAction(Input.LookMiddle, ETriggerEvent::Started, this, &AStellarEraPlayerController::OnLookMiddleStarted);
   	EnhancedInput->BindAction(Input.LookMiddle, ETriggerEvent::Completed, this, &AStellarEraPlayerController::OnLookMiddleCompleted);
   	EnhancedInput->BindAction(Input.Zoom, ETriggerEvent::Triggered, this, &AStellarEraPlayerController::OnZoom);
   ```
   并把那段注释改成：
   ```cpp
   	// 角色 / 飞船 / 星图相机的移动、视角、推力由各自的 Pawn 在自己的
   	// SetupPlayerInputComponent 里绑定；这里只留控制器自己的那几条 ——
   	// 上下船、星图开关与**选择**、编辑、矿物透视。
   ```
2. 删掉 `ResolveStarMapLookMode` 函数体（`:291-326`）与六个回调定义（`OnLook` … `OnZoom`）。
3. `ApplyMouseModeForPawn` 的三行清零改成一行：
   ```cpp
   	// 换 Pawn 时把「左键按住」标志清零：按着键登船 / 离船 / 进星图的话，松开事件
   	// 会送到新 Pawn 身上，标志就永远停在 true，星图里没按左键也在转视角。
   	// 视角那两个标志（右键 / 中键）归 UStarMapCameraInputComponent，它在 Bind 时自己清。
   	bStarMapSelectHeld = false;
   ```
4. 在 `GetEditComponentInEditMode` 实现之后加：
   ```cpp
   bool AStellarEraPlayerController::IsStarMapSelectHeld() const
   {
   	return bStarMapSelectHeld;
   }

   bool AStellarEraPlayerController::IsMineralViewActive() const
   {
   	return ActiveMineralView != nullptr;
   }
   ```

- [ ] **Step 5: 把 `StarMapTests.cpp` 的调用点改指向组件**

`Source/StellarEra/Src/Tests/StarMapTests.cpp:698-707` 现在是：

```cpp
bool FStellarEraStarMapLookDispatchTest::RunTest(const FString& Parameters)
{
	using FController = AStellarEraPlayerController;
	using EMode = EStarMapLookMode;

	// 参数顺序：(右键, 左键, 中键, 编辑模式, 连续挖, 矿物透视)
	auto Resolve = [](bool bRight, bool bLeft, bool bMiddle, bool bEdit, bool bDig, bool bMineral)
	{
		return FController::ResolveStarMapLookMode(bRight, bLeft, bMiddle, bEdit, bDig, bMineral);
	};
```

改成：

```cpp
bool FStellarEraStarMapLookDispatchTest::RunTest(const FString& Parameters)
{
	using EMode = EStarMapLookMode;

	// 参数顺序：(右键, 左键, 中键, 编辑模式, 连续挖, 矿物透视)
	auto Resolve = [](bool bRight, bool bLeft, bool bMiddle, bool bEdit, bool bDig, bool bMineral)
	{
		return UStarMapCameraInputComponent::ResolveLookMode(bRight, bLeft, bMiddle, bEdit, bDig, bMineral);
	};
```

并在该文件 include 段加 `#include "StarMapCameraInputComponent.h"`。

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

- [ ] **Step 7: 构建（红灯在这一步）**

先用 Global Constraints 的构建命令跑一次，预期成功。若失败，按错误行修，不要靠猜。

- [ ] **Step 8: 跑受影响的两个测试**

```bash
cd /c && "D:/Application/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" \
  "F:/workspace/ue/StellarEra/StellarEra.uproject" \
  -ExecCmds="Automation RunTests StellarEra.Pawns.StarMapInputComponentWiring+StellarEra.StarMap.LookDispatch" \
  -TestExit="Automation Test Queue Empty" \
  -unattended -nopause -nullrhi -nosplash -stdout \
  -abslog="C:/Users/23080/AppData/Local/Temp/se_starmap_unit.log" > /dev/null 2>&1
grep -E "Result=\{(Success|Fail)\}" "C:/Users/23080/AppData/Local/Temp/se_starmap_unit.log"
```

Expected：两条都是 `Result={Success}`。

- [ ] **Step 9: 跑全量测试**

用 Global Constraints 里的全量测试命令。Expected：232 项 = 231 Success + 1 Fail（既有 `AtmosphereMaterial.Structure`）。

- [ ] **Step 10: 提交**

```bash
git add Source/StellarEraPawns/Src/StarMap/StarMapCameraInputComponent.h \
        Source/StellarEraPawns/Src/StarMap/StarMapCameraInputComponent.cpp \
        Source/StellarEraPawns/Src/Tests/StarMapInputComponentTests.cpp
git commit -m "refactor(星图): 视角与缩放输入抽成 UStarMapCameraInputComponent

与 USpaceshipInputComponent 同形：一个 Bind()、一组 On*Input，
回调里直接分派，不另抽分派层。

Co-Authored-By: Claude Code <noreply@anthropic.com>" \
  -- Source/StellarEraPawns/Src/StarMap/StarMapCameraInputComponent.h \
     Source/StellarEraPawns/Src/StarMap/StarMapCameraInputComponent.cpp \
     Source/StellarEraPawns/Src/StarMap/StarMapCameraPawn.h \
     Source/StellarEraPawns/Src/StarMap/StarMapCameraPawn.cpp \
     Source/StellarEraPawns/Src/Tests/StarMapInputComponentTests.cpp \
     Source/StellarEra/Src/StellarEraPlayerController.h \
     Source/StellarEra/Src/StellarEraPlayerController.cpp \
     Source/StellarEra/Src/Tests/StarMapTests.cpp
```

---

### Task 2: 文档同步 + 全量验证

**Files:**
- Modify: `docs/ARCHITECTURE.md`
- Modify: `Source/README.md`
- Modify: `Source/StellarEra/StellarEra.Build.cs`（整支复审 Important 1：`:19-23` 的依赖注释写的还是改前的 `Cosmos ─Private→ Pawns`）
- Modify: `Source/StellarEraPawns/Src/Spaceship/SpaceshipPawn.h:160`（复审 Minor 3：注释指向已搬走的绑定表）

- [ ] **Step 1: 修 `StellarEra.Build.cs` 的依赖注释**

`Source/StellarEra/StellarEra.Build.cs:19-23` 现在写的是：

```
//   L3 StellarEraCosmos   ─Private→ Planet / Pawns
//   L2 StellarEraPawns    ─Public→ Planet
```

改成与 `docs/ARCHITECTURE.md` 一致的现状：

```
//   L3 StellarEraCosmos   ─Private→ Planet
//   L2 StellarEraPawns    ─Public→ Cosmos / Planet / Voxel
```

同一段里「两个 Public 依赖都是被 `IVoxelViewpointInvokers` 逼出来的：UI 的星图相机与……」这类理由也要同步 —— 星图相机已不在 UI。

- [ ] **Step 2: 修 `SpaceshipPawn.h:160` 的注释指向**

`// 输入约定见 ASpaceshipPawn::SetupPlayerInputComponent` 改成 `USpaceshipInputComponent::Bind`。

- [ ] **Step 3: 找出文档里所有描述「星图鼠标绑在控制器上」的句子**

```bash
cd "F:/workspace/ue/StellarEra"
grep -rn "星图.*鼠标\|鼠标.*星图\|StarMapCamera\|输入组件\|InputComponent" docs/ARCHITECTURE.md Source/README.md CLAUDE.md
```

- [ ] **Step 4: 逐条改成搬家后的事实**

必须成立的三句话：
- 三个可操控对象（角色、飞船、星图相机）各有自己的输入组件，Pawn 只转发 `SetupPlayerInputComponent`；
- 控制器只绑控制器级输入（上下船、星图开关与选择、编辑、矿物透视）；
- `StarMapCameraInputComponent.h` 里同时放着 `EStarMapLookMode` / `UStarMapCameraInputComponent::ResolveLookMode` / `IStarMapLookContext`。

依赖矩阵**不变**（本次没有新增或删除模块边），那张表本来就对就别动。

- [ ] **Step 5: 重算文档里的模块规模统计**

```bash
cd "F:/workspace/ue/StellarEra"
for m in StellarEra StellarEraPlanet StellarEraPawns StellarEraCosmos StellarEraUI; do
  f=$(find "Source/$m/Src" -type f \( -name '*.h' -o -name '*.cpp' \) | wc -l)
  l=$(find "Source/$m/Src" -type f \( -name '*.h' -o -name '*.cpp' \) -exec wc -l {} + | tail -1 | awk '{print $1}')
  t=$(find "Source/$m/Src/Tests" -maxdepth 1 -type f -name '*Tests.cpp' 2>/dev/null | wc -l)
  echo "$m 文件=$f 行=$l 测试=$t"
done
```

把结果同时填进 `docs/ARCHITECTURE.md` 的模块表 + 汇总句、`Source/README.md` 的对应行。**一份都不能漏** —— 上一轮就是只改一处连修四轮。

- [ ] **Step 6: 架构体检**

```bash
cd "F:/workspace/ue/StellarEra" && PYTHONIOENCODING=utf-8 python Tools/check_architecture.py 2>&1 | head -20
```

Expected：依赖环「无」；`StellarEraPawns` 依赖列仍是 `Cosmos, Planet`（没有新增 `StellarEra`）。

- [ ] **Step 7: 无头验证（星图输入真的绑上了）**

```bash
cd /c && UE="D:/Application/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor.exe"; \
MSYS2_ARG_CONV_EXCL='*' timeout 150 "$UE" "F:/workspace/ue/StellarEra/StellarEra.uproject" \
  /Game/Code/Maps/UniverseMap -game -nullrhi -nosplash -unattended -stdout \
  -LogCmds="LogTemp Verbose" -ExecCmds="DebugDisembark,ToggleStarMap" \
  -abslog="C:/Users/23080/AppData/Local/Temp/se_starmap_input.log" > /dev/null 2>&1; \
grep -nE "\[Input\]|绑定失败|Failed to enter" "C:/Users/23080/AppData/Local/Temp/se_starmap_input.log" | head
```

Expected：出现 `[Input] AStarMapCameraPawn(...) 已绑定星图相机输入`；`绑定失败` 0 条；`Failed to enter` 0 条。

> `-ExecCmds` 里的 `ToggleStarMap` 可能跑在控制器 Possess 之前。拿不到那条日志就如实记进报告，**不要**据此声称验证通过。

- [ ] **Step 8: 提交**

```bash
git commit -m "docs(架构): 同步星图相机输入组件与依赖注释

Co-Authored-By: Claude Code <noreply@anthropic.com>" \
  -- docs/ARCHITECTURE.md Source/README.md Source/StellarEra/StellarEra.Build.cs \
     Source/StellarEraPawns/Src/Spaceship/SpaceshipPawn.h
```

---

## Self-Review

**1. 覆盖度**

| 用户要求 | 任务 |
|---|---|
| 星图输入做成和飞船角色一样的组件 | Task 1（一个类、一个 `Bind()`、一组 `On*Input`） |
| 「统一规范，不要搞特殊」 | 不新增独立的分派文件；`EStarMapLookMode` 与 `ResolveLookMode` 就放在组件头里；接口只用两句话，编辑模式直接读 Pawn |
| 灵敏度每组件各自一份 | Task 1 Step 1 |
| 模块不出现环 | Task 1 Step 2 的 include 禁令 + Step 7 构建 + Task 2 Step 6 |
| 运行行为不变 | 分派规则函数体原样搬迁；`OrbitLook` / `AimLook` / `AddZoom` 一行不动 |

**2. 占位符扫描**：无 TBD / TODO；每个代码步骤都是可直接粘贴的完整文件或逐行替换。

**3. 类型一致性**

- `ResolveLookMode` 六参数顺序 `(右, 左, 中, 编辑, 连挖, 矿物)` 在定义、`OnLookInput` 调用、`StarMapTests.cpp` 的 lambda 三处一致。
- `IStarMapLookContext` 两方法 ↔ 控制器两段实现逐条对应。
- 组件成员名 `PlayerInput` 与 `APlanetCharacter` / `ASpaceshipPawn` 一致。
- 日志串 `[Input] AStarMapCameraPawn(...) 已绑定星图相机输入` 在 Step 2 与 Task 2 Step 7 一致。

**4. 已知风险**

| 风险 | 处置 |
|---|---|
| `ResolveLookMode` 跨 DLL 调用需要导出 | 它是 `STELLARERAPAWNS_API` 类上的**静态成员**，导出由类宏覆盖（上一轮 Task 1 用自由函数时曾撞 `LNK2019`，改成静态成员即避开） |
| `ToggleStarMap` 在无头 `-ExecCmds` 里时机不对 | Task 2 Step 7 已写明：拿不到就如实记录，不声称通过 |
| 相机 Pawn 是 `BP_VoxelPlanet` 的 Child Actor，蓝图可能也挂了同名组件 | 构建与测试会暴露重复组件；`StarMapInputComponentWiring` 只断言 CDO 上找得到 |
