# 输入抽成组件 + 星图相机搬进 Pawns — 实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 把角色/飞船的输入回调抽成两个 `UActorComponent`，并通过抽 `ICosmosBody` 接口还清 `Cosmos → Pawns` 的依赖债，把 `AStarMapCameraPawn` 搬进 `StellarEraPawns`。

**Architecture:** 输入组件挂在 Pawn 上、回调绑到自己身上（`BindAction` 要求接收者是 UObject）；`ICosmosBody` 声明在 Cosmos、由 `ASpaceshipPawn` 实现，Cosmos 不再认识具体 Pawn 类型；相机随 Pawns 一起依赖 Cosmos。

**Tech Stack:** Unreal Engine 5.8.3 / C++ / Enhanced Input / UBT / 自动化测试

**Spec:** `docs/superpowers/specs/2026-11-06-input-components-and-starmap-camera-move-design.md`

## Global Constraints

- 构建：`dotnet UnrealBuildTool.dll StellarEraEditor Win64 Development -Project="F:/workspace/ue/StellarEra/StellarEra.uproject" -WaitMutex -FromMsBuild`，cwd 在项目外。**构建前必须关编辑器**。
- 含中文的 `.h`/`.cpp` 必须存成 **UTF-8 with BOM**；新文件写完用 `head -c 3 <file> | od -An -tx1` 确认输出 `ef bb bf`。
- 不改键位映射（`FStellarEraInputActions::Build` 一个字都不动）。
- `Cosmos → Pawns` 与 `Pawns → Cosmos` 必须在**同一个提交**里换向，中间态是模块环。
- 提交必须带 pathspec：`git commit -m "..." -- <paths>`（索引里有常驻暂存文件）。提交信息末尾附 `Co-Authored-By: Claude Code <noreply@anthropic.com>`。
- 每个 Task 结束都必须构建通过并留下提交。

---

### Task 1: 角色输入组件

**Files:**
- Create: `Source/StellarEraPawns/Src/Character/PlanetCharacterInputComponent.h`
- Create: `Source/StellarEraPawns/Src/Character/PlanetCharacterInputComponent.cpp`
- Create: `Source/StellarEraPawns/Src/Tests/InputComponentsTests.cpp`
- Modify: `Source/StellarEraPawns/Src/Character/PlanetCharacter.h`
- Modify: `Source/StellarEraPawns/Src/Character/PlanetCharacter.cpp`

**Interfaces:**
- Consumes: `FStellarEraInputActions` / `IStellarEraInputSource`（`StellarEraInputActions.h`，已在 Pawns）；`StellarEraInput::ClickMoveThresholdPx`；`APlanetCharacter::Move/Look/Jump/SetSprinting/AddZoom/GetPlanetEditComponent`（全部已是 public）。
- Produces: `UPlanetCharacterInputComponent::Bind(UInputComponent*)`；`APlanetCharacter::PlayerInput` 成员（`UPROPERTY(VisibleAnywhere)`）。Task 2 的飞船组件照抄这套结构。

- [ ] **Step 1: 先建一个空壳组件（只为让后续测试能编译）**

`PlanetCharacterInputComponent.h`：

```cpp
// Copyright Voxel Plugin SAS. All Rights Reserved.
// 角色的键鼠输入：绑在角色身上，代码不住在角色类里。

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "PlanetCharacterInputComponent.generated.h"

class APlanetCharacter;
class UInputComponent;
class UPlanetEditComponent;
struct FInputActionValue;

/**
 * APlanetCharacter 的全部输入：绑定、回调、只服务输入的状态。
 *
 * 为什么是组件而不是把代码留在角色里：这些回调既不是玩法也不是物理，却占着
 * 角色头的篇幅；拆出来之后角色类只剩「怎么走、怎么看」，输入改起来不用再翻它。
 *
 * 为什么是 UObject 组件而不是普通 C++ 类：`UEnhancedInputComponent::BindAction`
 * 的成员函数重载要求接收者是 UObject（换 lambda 会丢 GC 安全与反射签名检查）。
 * 由角色在构造函数里 CreateDefaultSubobject，生命周期天然跟着角色。
 */
UCLASS()
class STELLARERAPAWNS_API UPlanetCharacterInputComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	/**
	 * 把控制器给的动作表绑到本组件上。由 APlanetCharacter::SetupPlayerInputComponent
	 * 调用，引擎只在本地 Possess 这个 Pawn 时走到那里。
	 */
	void Bind(UInputComponent* PlayerInputComponent);

	/** 鼠标灵敏度（度/像素）。 */
	UPROPERTY(EditAnywhere, Category = "Input")
	float LookSensitivity = 1.2f;

	/** 上下视角反向。觉得鼠标上下颠倒时把它改成 true。 */
	UPROPERTY(EditAnywhere, Category = "Input")
	bool bInvertLookY = false;

private:
	/** 所属角色；组件只可能挂在角色上。 */
	APlanetCharacter* GetPawn() const;
};
```

`PlanetCharacterInputComponent.cpp`：

```cpp
// Copyright Voxel Plugin SAS. All Rights Reserved.

#include "PlanetCharacterInputComponent.h"

#include "PlanetCharacter.h"

APlanetCharacter* UPlanetCharacterInputComponent::GetPawn() const
{
	return Cast<APlanetCharacter>(GetOwner());
}

void UPlanetCharacterInputComponent::Bind(UInputComponent* PlayerInputComponent)
{
	(void)PlayerInputComponent;
}
```

- [ ] **Step 2: 写失败测试**

`Source/StellarEraPawns/Src/Tests/InputComponentsTests.cpp`：

```cpp
// 输入组件的接线回归测试：两个 Pawn 的 CDO 上都必须挂着各自的输入组件。
//
// 断言 CDO 而不是运行时实例：默认子对象在构造函数里创建，CDO 上就有，
// 不需要世界、不需要 PIE。组件被误删或被蓝图覆盖掉，这里立刻红。

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#include "PlanetCharacter.h"
#include "PlanetCharacterInputComponent.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraCharacterInputComponentTest,
	"StellarEra.Pawns.InputComponentWiring",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraCharacterInputComponentTest::RunTest(const FString& Parameters)
{
	const APlanetCharacter* CharacterCDO = GetDefault<APlanetCharacter>();
	if (!TestNotNull(TEXT("APlanetCharacter CDO"), CharacterCDO))
	{
		return false;
	}

	const UPlanetCharacterInputComponent* CharacterInput =
		CharacterCDO->FindComponentByClass<UPlanetCharacterInputComponent>();
	TestNotNull(TEXT("角色 CDO 上的 UPlanetCharacterInputComponent"), CharacterInput);
	if (CharacterInput)
	{
		TestEqual(TEXT("默认灵敏度"), CharacterInput->LookSensitivity, 1.2f);
		TestFalse(TEXT("默认不反 Y"), CharacterInput->bInvertLookY);
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
```

- [ ] **Step 3: 构建 + 跑测试确认它红**

先构建（命令见 Task 1 Step 6），再跑：

```bash
cd /c && "D:/Application/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" \
  "F:/workspace/ue/StellarEra/StellarEra.uproject" \
  -ExecCmds="Automation RunTests StellarEra.Pawns.InputComponentWiring" \
  -TestExit="Automation Test Queue Empty" -unattended -nopause -nullrhi -nosplash -stdout \
  -abslog="C:/Users/23080/AppData/Local/Temp/se_t1_red.log" > /dev/null 2>&1; \
  grep -E "Result=\{" "C:/Users/23080/AppData/Local/Temp/se_t1_red.log" | tail -3
```

Expected: `Result={Fail}`，失败信息是「角色 CDO 上的 UPlanetCharacterInputComponent」为空（空壳组件还没挂到角色上）。

> 不先构建的话跑的是旧 DLL，测试根本不注册，那不算红。

- [ ] **Step 4: 把回调整体搬进组件**

`PlanetCharacterInputComponent.h` 的 private 区补上（放在 `GetPawn` 之后）：

```cpp
	void OnMoveInput(const FInputActionValue& Value);
	void OnLookInput(const FInputActionValue& Value);
	void OnLookRightStarted(const FInputActionValue& Value);
	void OnLookRightCompleted(const FInputActionValue& Value);
	void OnJumpInput(const FInputActionValue& Value);
	void OnSprintStarted(const FInputActionValue& Value);
	void OnSprintCompleted(const FInputActionValue& Value);
	void OnZoomInput(const FInputActionValue& Value);

	/** 当前打开的编辑组件；不在编辑模式、或蓝图没挂组件时为空。 */
	UPlanetEditComponent* GetEditComponentInEditMode() const;

	/** 右键是否按住。松开时视角不响应鼠标，走动时相机不会被拖走。 */
	bool bLookHeld = false;

	/** 编辑模式右键按下的屏幕位置。松开时用它区分「点击放置」与「拖拽转视角」。 */
	FVector2D EditRightPressPosition = FVector2D::ZeroVector;

	/** 右键按下时是否记下了编辑位置（不是编辑模式 / 没拿到鼠标时为 false）。 */
	bool bEditRightPressValid = false;
```

`PlanetCharacterInputComponent.cpp` 的 include 块改成：

```cpp
#include "PlanetCharacterInputComponent.h"

#include "EnhancedInputComponent.h"
#include "GameFramework/PlayerController.h"
#include "InputActionValue.h"
#include "InputCoreTypes.h"
#include "PlanetCharacter.h"
#include "PlanetEditComponent.h"
#include "StellarEraInputActions.h"
```

`Bind` 替换为（把 `PlanetCharacter.cpp` 里现有 `SetupPlayerInputComponent` 的内容原样搬来，接收者改成 `this`，Pawn 改用 `GetPawn()`）：

```cpp
void UPlanetCharacterInputComponent::Bind(UInputComponent* PlayerInputComponent)
{
	APlanetCharacter* Pawn = GetPawn();
	UEnhancedInputComponent* EnhancedInput = Cast<UEnhancedInputComponent>(PlayerInputComponent);
	IStellarEraInputSource* Source = Pawn ? Cast<IStellarEraInputSource>(Pawn->GetController()) : nullptr;
	if (!Pawn || !EnhancedInput || !Source)
	{
		// 静默解绑等于「角色对键鼠完全没反应」，是这里最难查的失败模式：留一条 Error。
		UE_LOG(LogTemp, Error, TEXT("[Input] APlanetCharacter 绑定失败：Pawn=%s EnhancedInput=%s InputSource=%s Controller=%s"),
			*GetNameSafe(Pawn),
			EnhancedInput ? TEXT("是") : TEXT("否"),
			Source ? TEXT("是") : TEXT("否"),
			*GetNameSafe(Pawn ? Pawn->GetController() : nullptr));
		return;
	}

	const FStellarEraInputActions& Input = Source->GetStellarEraInputActions();

	EnhancedInput->BindAction(Input.Move, ETriggerEvent::Triggered, this, &UPlanetCharacterInputComponent::OnMoveInput);
	EnhancedInput->BindAction(Input.Look, ETriggerEvent::Triggered, this, &UPlanetCharacterInputComponent::OnLookInput);
	EnhancedInput->BindAction(Input.LookActivate, ETriggerEvent::Started, this, &UPlanetCharacterInputComponent::OnLookRightStarted);
	EnhancedInput->BindAction(Input.LookActivate, ETriggerEvent::Completed, this, &UPlanetCharacterInputComponent::OnLookRightCompleted);
	EnhancedInput->BindAction(Input.Jump, ETriggerEvent::Started, this, &UPlanetCharacterInputComponent::OnJumpInput);
	EnhancedInput->BindAction(Input.Sprint, ETriggerEvent::Started, this, &UPlanetCharacterInputComponent::OnSprintStarted);
	EnhancedInput->BindAction(Input.Sprint, ETriggerEvent::Completed, this, &UPlanetCharacterInputComponent::OnSprintCompleted);

	// 只绑 Triggered：滚轮每格是一次独立冲量，不滚就不产生值，没有 Completed 可收。
	EnhancedInput->BindAction(Input.Zoom, ETriggerEvent::Triggered, this, &UPlanetCharacterInputComponent::OnZoomInput);

	// 绑定成功也给一条 Verbose：「没报错」不等于「绑上了」，无头跑时用它当正证据。
	UE_LOG(LogTemp, Verbose, TEXT("[Input] APlanetCharacter(%s) 已绑定角色输入"), *Pawn->GetName());
}
```

回调体从 `PlanetCharacter.cpp` 原样搬过来，只做三处替换：

1. `bLookHeld` / `LookSensitivity` / `bInvertLookY` / `EditRightPressPosition` / `bEditRightPressValid` 是本组件成员，去掉 `APlanetCharacter::` 前缀即可；
2. `Move(...)` → `Pawn->Move(...)`、`Look(...)` → `Pawn->Look(...)`、`Jump()` → `Pawn->Jump()`、`SetSprinting(...)` → `Pawn->SetSprinting(...)`、`AddZoom(...)` → `Pawn->AddZoom(...)`；
3. `GetController()` → `Pawn->GetController()`，并在每个回调开头取一次 `APlanetCharacter* Pawn = GetPawn(); if (!Pawn) { return; }`。

`GetEditComponentInEditMode` 实现：

```cpp
UPlanetEditComponent* UPlanetCharacterInputComponent::GetEditComponentInEditMode() const
{
	const APlanetCharacter* Pawn = GetPawn();
	UPlanetEditComponent* Edit = Pawn ? Pawn->GetPlanetEditComponent() : nullptr;
	return Edit && Edit->IsEditMode() ? Edit : nullptr;
}
```

- [ ] **Step 5: 角色类瘦身**

`PlanetCharacter.h`：

1. 顶部前向声明区加 `class UPlanetCharacterInputComponent;`
2. `virtual void SetupPlayerInputComponent(...) override;` 之后插入成员：

```cpp
	/** 键鼠输入。回调与输入状态都在组件里，本类只负责转发绑定。 */
	UPROPERTY(VisibleAnywhere, Category = "Input")
	TObjectPtr<UPlanetCharacterInputComponent> PlayerInput;
```

3. 删掉 `// --- 输入回调（SetupPlayerInputComponent 里绑定） ---` 到 `UPlanetEditComponent* GetEditComponentInEditMode() const;` 的全部声明（8 个 `On*` + `GetEditComponentInEditMode`）；
4. 删掉 `// --- 输入状态 ---` 起的五个成员（`LookSensitivity`、`bInvertLookY`、`bLookHeld`、`EditRightPressPosition`、`bEditRightPressValid`）。

`PlanetCharacter.cpp`：

1. include 区加 `#include "PlanetCharacterInputComponent.h"`；删掉只被搬走的代码用到的 include：`EnhancedInputComponent.h`、`InputActionValue.h`、`InputCoreTypes.h`、`StellarEraInputActions.h`（若构建报未定义再逐个加回，**不要**提前删 `GameFramework/PlayerController.h`，角色别处可能还在用）；
2. 构造函数末尾加：

```cpp
	PlayerInput = CreateDefaultSubobject<UPlanetCharacterInputComponent>(TEXT("PlayerInput"));
```

3. `SetupPlayerInputComponent` 整个函数体替换为：

```cpp
void APlanetCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	// 引擎只在**本地 Possess 这个 Pawn** 时调用这里，所以「谁被操控谁响应输入」
	// 由 Pawn 栈保证；真正的绑定与回调都在组件里。
	if (PlayerInput)
	{
		PlayerInput->Bind(PlayerInputComponent);
	}
}
```

4. 删掉 `OnMoveInput` / `OnLookInput` / `OnLookRightStarted` / `OnLookRightCompleted` / `OnJumpInput` / `OnSprintStarted` / `OnSprintCompleted` / `OnZoomInput` / `GetEditComponentInEditMode` 九个函数定义。`SetSprinting` **保留**（它是角色 API，不是输入）。

- [ ] **Step 6: 构建**

```bash
cd /c && "D:/Application/Epic Games/UE_5.8/Engine/Binaries/ThirdParty/DotNet/10.0/win-x64/dotnet.exe" \
  "D:/Application/Epic Games/UE_5.8/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.dll" \
  StellarEraEditor Win64 Development -Project="F:/workspace/ue/StellarEra/StellarEra.uproject" \
  -WaitMutex -FromMsBuild 2>&1 | grep -E "error|Result:|Total execution"
```

Expected: `Result: Succeeded`。若报 include 未定义，把对应 include 加回。

- [ ] **Step 7: 跑测试确认它绿**

```bash
cd /c && "D:/Application/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" \
  "F:/workspace/ue/StellarEra/StellarEra.uproject" \
  -ExecCmds="Automation RunTests StellarEra.Pawns.InputComponentWiring" \
  -TestExit="Automation Test Queue Empty" -unattended -nopause -nullrhi -nosplash -stdout \
  -abslog="C:/Users/23080/AppData/Local/Temp/se_t1_green.log" > /dev/null 2>&1; \
  grep -E "Result=\{" "C:/Users/23080/AppData/Local/Temp/se_t1_green.log" | tail -3
```

Expected: `Result={Success}`。

- [ ] **Step 8: 提交**

```bash
cd "F:/workspace/ue/StellarEra" && git add Source/StellarEraPawns && \
git commit -m "refactor(输入): 角色输入回调抽成 UPlanetCharacterInputComponent

Co-Authored-By: Claude Code <noreply@anthropic.com>" -- Source/StellarEraPawns
```

---

### Task 2: 飞船输入组件

**Files:**
- Create: `Source/StellarEraPawns/Src/Spaceship/SpaceshipInputComponent.h`
- Create: `Source/StellarEraPawns/Src/Spaceship/SpaceshipInputComponent.cpp`
- Modify: `Source/StellarEraPawns/Src/Tests/InputComponentsTests.cpp`
- Modify: `Source/StellarEraPawns/Src/Spaceship/SpaceshipPawn.h`
- Modify: `Source/StellarEraPawns/Src/Spaceship/SpaceshipPawn.cpp`

**Interfaces:**
- Consumes: Task 1 的组件结构；`ASpaceshipPawn::Look/SetThrust/SetRoll/SetVertical/SetBoost/TogglePreserveVelocity/IsPreserveVelocity/ToggleDebugViewMode/IsDebugViewMode/AddZoom`（全部 public）；`IStellarEraNoticeSink`。
- Produces: `USpaceshipInputComponent::Bind(UInputComponent*)`；`ASpaceshipPawn::PlayerInput` 成员。

- [ ] **Step 1: 建空壳组件**

`SpaceshipInputComponent.h`：

```cpp
// Copyright Voxel Plugin SAS. All Rights Reserved.
// 飞船的键鼠输入：绑在飞船身上，代码不住在飞船类里。

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "SpaceshipInputComponent.generated.h"

class ASpaceshipPawn;
class UInputComponent;
struct FInputActionValue;

/**
 * ASpaceshipPawn 的全部输入：绑定、回调、只服务输入的状态。
 *
 * 与 UPlanetCharacterInputComponent 同构，但飞船**不设「按住右键」的门** ——
 * 鼠标就是它的操纵杆，要按住右键才能转向等于飞不动。
 */
UCLASS()
class STELLARERAPAWNS_API USpaceshipInputComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	/** 把控制器给的动作表绑到本组件上。由 ASpaceshipPawn::SetupPlayerInputComponent 调用。 */
	void Bind(UInputComponent* PlayerInputComponent);

	/** 鼠标灵敏度（度/像素）。飞船与角色的手感可以不一样，所以各存一份。 */
	UPROPERTY(EditAnywhere, Category = "Input")
	float LookSensitivity = 1.2f;

	/** 上下视角反向。觉得鼠标上下颠倒时把它改成 true。 */
	UPROPERTY(EditAnywhere, Category = "Input")
	bool bInvertLookY = false;

private:
	/** 所属飞船；组件只可能挂在飞船上。 */
	ASpaceshipPawn* GetPawn() const;
};
```

`SpaceshipInputComponent.cpp`：

```cpp
// Copyright Voxel Plugin SAS. All Rights Reserved.

#include "SpaceshipInputComponent.h"

#include "SpaceshipPawn.h"

ASpaceshipPawn* USpaceshipInputComponent::GetPawn() const
{
	return Cast<ASpaceshipPawn>(GetOwner());
}

void USpaceshipInputComponent::Bind(UInputComponent* PlayerInputComponent)
{
	(void)PlayerInputComponent;
}
```

- [ ] **Step 2: 给测试补飞船断言（先红）**

在 `InputComponentsTests.cpp` 顶部 include 区加 `#include "SpaceshipInputComponent.h"` 与 `#include "SpaceshipPawn.h"`，在文件末尾（`#endif` 之前）追加：

```cpp
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraSpaceshipInputComponentTest,
	"StellarEra.Pawns.SpaceshipInputComponentWiring",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraSpaceshipInputComponentTest::RunTest(const FString& Parameters)
{
	const ASpaceshipPawn* ShipCDO = GetDefault<ASpaceshipPawn>();
	if (!TestNotNull(TEXT("ASpaceshipPawn CDO"), ShipCDO))
	{
		return false;
	}

	const USpaceshipInputComponent* ShipInput =
		ShipCDO->FindComponentByClass<USpaceshipInputComponent>();
	TestNotNull(TEXT("飞船 CDO 上的 USpaceshipInputComponent"), ShipInput);
	if (ShipInput)
	{
		TestEqual(TEXT("默认灵敏度"), ShipInput->LookSensitivity, 1.2f);
		TestFalse(TEXT("默认不反 Y"), ShipInput->bInvertLookY);
	}

	return true;
}
```

- [ ] **Step 3: 构建 + 跑新测试确认红**

构建同 Task 1 Step 6，再：

```bash
cd /c && "D:/Application/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" \
  "F:/workspace/ue/StellarEra/StellarEra.uproject" \
  -ExecCmds="Automation RunTests StellarEra.Pawns.SpaceshipInputComponentWiring" \
  -TestExit="Automation Test Queue Empty" -unattended -nopause -nullrhi -nosplash -stdout \
  -abslog="C:/Users/23080/AppData/Local/Temp/se_t2_red.log" > /dev/null 2>&1; \
  grep -E "Result=\{" "C:/Users/23080/AppData/Local/Temp/se_t2_red.log" | tail -3
```

Expected: `Result={Fail}`（组件还没挂到飞船 CDO 上）。

- [ ] **Step 4: 搬回调**

`SpaceshipInputComponent.h` 的 private 区补上：

```cpp
	void OnLookInput(const FInputActionValue& Value);
	void OnThrustInput(const FInputActionValue& Value);
	void OnRollInput(const FInputActionValue& Value);
	void OnVerticalInput(const FInputActionValue& Value);
	void OnBoostStarted(const FInputActionValue& Value);
	void OnBoostCompleted(const FInputActionValue& Value);
	void OnCruiseToggle(const FInputActionValue& Value);
	void OnDebugViewToggle(const FInputActionValue& Value);
	void OnZoomInput(const FInputActionValue& Value);

	/** 通过 HUD 的 IStellarEraNoticeSink 弹一条提示；拿不到 HUD 时静默跳过。 */
	void ShowNotice(const FString& Text) const;
```

`SpaceshipInputComponent.cpp` 的 include 块：

```cpp
#include "SpaceshipInputComponent.h"

#include "EnhancedInputComponent.h"
#include "GameFramework/HUD.h"
#include "GameFramework/PlayerController.h"
#include "InputActionValue.h"
#include "SpaceshipPawn.h"
#include "StellarEraInputActions.h"
#include "StellarEraNoticeSink.h"
```

`Bind` 替换为（照 `SpaceshipPawn.cpp` 现有 `SetupPlayerInputComponent` 搬，接收者换成 `this`）：

```cpp
void USpaceshipInputComponent::Bind(UInputComponent* PlayerInputComponent)
{
	ASpaceshipPawn* Pawn = GetPawn();
	UEnhancedInputComponent* EnhancedInput = Cast<UEnhancedInputComponent>(PlayerInputComponent);
	IStellarEraInputSource* Source = Pawn ? Cast<IStellarEraInputSource>(Pawn->GetController()) : nullptr;
	if (!Pawn || !EnhancedInput || !Source)
	{
		// 静默解绑等于「飞船对键鼠完全没反应」——留一条 Error，别让它变成哑故障。
		UE_LOG(LogTemp, Error, TEXT("[Input] ASpaceshipPawn 绑定失败：Pawn=%s EnhancedInput=%s InputSource=%s Controller=%s"),
			*GetNameSafe(Pawn),
			EnhancedInput ? TEXT("是") : TEXT("否"),
			Source ? TEXT("是") : TEXT("否"),
			*GetNameSafe(Pawn ? Pawn->GetController() : nullptr));
		return;
	}

	const FStellarEraInputActions& Input = Source->GetStellarEraInputActions();

	EnhancedInput->BindAction(Input.Look, ETriggerEvent::Triggered, this, &USpaceshipInputComponent::OnLookInput);
	EnhancedInput->BindAction(Input.Thrust, ETriggerEvent::Triggered, this, &USpaceshipInputComponent::OnThrustInput);
	EnhancedInput->BindAction(Input.Thrust, ETriggerEvent::Completed, this, &USpaceshipInputComponent::OnThrustInput);
	EnhancedInput->BindAction(Input.Roll, ETriggerEvent::Triggered, this, &USpaceshipInputComponent::OnRollInput);
	EnhancedInput->BindAction(Input.Roll, ETriggerEvent::Completed, this, &USpaceshipInputComponent::OnRollInput);
	EnhancedInput->BindAction(Input.Vertical, ETriggerEvent::Triggered, this, &USpaceshipInputComponent::OnVerticalInput);
	EnhancedInput->BindAction(Input.Vertical, ETriggerEvent::Completed, this, &USpaceshipInputComponent::OnVerticalInput);
	EnhancedInput->BindAction(Input.Boost, ETriggerEvent::Started, this, &USpaceshipInputComponent::OnBoostStarted);
	EnhancedInput->BindAction(Input.Boost, ETriggerEvent::Completed, this, &USpaceshipInputComponent::OnBoostCompleted);

	// 只绑 Started：按住 Tab 只切一次，不会随按键重复翻转状态。B 同理。
	EnhancedInput->BindAction(Input.Cruise, ETriggerEvent::Started, this, &USpaceshipInputComponent::OnCruiseToggle);
	EnhancedInput->BindAction(Input.DebugViewToggle, ETriggerEvent::Started, this, &USpaceshipInputComponent::OnDebugViewToggle);

	// 只绑 Triggered：滚轮每格是一次独立冲量，不滚就不产生值，没有 Completed 可收。
	EnhancedInput->BindAction(Input.Zoom, ETriggerEvent::Triggered, this, &USpaceshipInputComponent::OnZoomInput);

	// 绑定成功也给一条 Verbose：「没报错」不等于「绑上了」，无头跑时用它当正证据。
	UE_LOG(LogTemp, Verbose, TEXT("[Input] ASpaceshipPawn(%s) 已绑定飞船输入"), *Pawn->GetName());
}
```

回调体原样搬来，做三处替换：`Look(...)` → `Pawn->Look(...)`、`SetThrust/SetRoll/SetVertical/SetBoost/Toggle*/AddZoom` → `Pawn->` 前缀、`GetController()` → `Pawn->GetController()`；每个回调开头取一次 `ASpaceshipPawn* Pawn = GetPawn(); if (!Pawn) { return; }`。

`ShowNotice` 搬进组件：

```cpp
void USpaceshipInputComponent::ShowNotice(const FString& Text) const
{
	// HUD 在 UI 模块（本模块的下游），只能走接口。没有 HUD 时静默跳过。
	const ASpaceshipPawn* Pawn = GetPawn();
	const APlayerController* PC = Pawn ? Cast<APlayerController>(Pawn->GetController()) : nullptr;
	IStellarEraNoticeSink* Sink = PC ? Cast<IStellarEraNoticeSink>(PC->GetHUD()) : nullptr;
	if (Sink)
	{
		Sink->ShowNotice(Text);
	}
}
```

- [ ] **Step 5: 飞船类瘦身**

`SpaceshipPawn.h`：

1. 前向声明区加 `class USpaceshipInputComponent;`
2. `virtual void SetupPlayerInputComponent(...) override;` 之后插入：

```cpp
	/** 键鼠输入。回调与输入状态都在组件里，本类只负责转发绑定。 */
	UPROPERTY(VisibleAnywhere, Category = "Input")
	TObjectPtr<USpaceshipInputComponent> PlayerInput;
```

3. 删掉 `// --- 输入回调（SetupPlayerInputComponent 里绑定） ---` 到 `void ShowNotice(const FString& Text) const;` 的全部声明（9 个 `On*` + `ShowNotice`）；
4. 删掉 `LookSensitivity` / `bInvertLookY` 两个成员（含它们的 UPROPERTY 与注释）。

`SpaceshipPawn.cpp`：

1. include 区加 `#include "SpaceshipInputComponent.h"`；删掉 `EnhancedInputComponent.h`、`InputActionValue.h`、`InputCoreTypes.h`、`StellarEraInputActions.h`、`StellarEraNoticeSink.h`、`GameFramework/HUD.h`（构建报未定义再加回）；
2. 构造函数里（`Hull` 建好之后任意位置）加：

```cpp
	PlayerInput = CreateDefaultSubobject<USpaceshipInputComponent>(TEXT("PlayerInput"));
```

3. `SetupPlayerInputComponent` 函数体替换为：

```cpp
void ASpaceshipPawn::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	if (PlayerInput)
	{
		PlayerInput->Bind(PlayerInputComponent);
	}
}
```

4. 删掉九个 `On*` 回调定义与 `ShowNotice` 定义。

- [ ] **Step 6: 构建 + 跑测试确认绿**

构建同 Task 1 Step 6。再跑：

```bash
cd /c && "D:/Application/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" \
  "F:/workspace/ue/StellarEra/StellarEra.uproject" \
  -ExecCmds="Automation RunTests StellarEra.Pawns" \
  -TestExit="Automation Test Queue Empty" -unattended -nopause -nullrhi -nosplash -stdout \
  -abslog="C:/Users/23080/AppData/Local/Temp/se_t2_green.log" > /dev/null 2>&1; \
  grep -cE "Result=\{Success\}" "C:/Users/23080/AppData/Local/Temp/se_t2_green.log"; \
  grep -E "Result=\{Fail" "C:/Users/23080/AppData/Local/Temp/se_t2_green.log"
```

Expected: 成功计数 ≥ 2（两个输入组件测试），`Result={Fail` 无输出。

- [ ] **Step 7: 提交**

```bash
cd "F:/workspace/ue/StellarEra" && git add Source/StellarEraPawns && \
git commit -m "refactor(输入): 飞船输入回调抽成 USpaceshipInputComponent

Co-Authored-By: Claude Code <noreply@anthropic.com>" -- Source/StellarEraPawns
```

---

### Task 3: `ICosmosBody` + 依赖换向

**Files:**
- Create: `Source/StellarEraCosmos/Src/CosmosBody.h`
- Modify: `Source/StellarEraCosmos/Src/CosmosSimulationSubsystem.cpp`（4 处 Cast + include）
- Modify: `Source/StellarEraCosmos/Src/CosmosSimulationSubsystem.h`（注释）
- Modify: `Source/StellarEraCosmos/Src/CosmosTrajectoryComponent.cpp`（2 处 Cast + include）
- Modify: `Source/StellarEraCosmos/StellarEraCosmos.Build.cs`
- Modify: `Source/StellarEraPawns/StellarEraPawns.Build.cs`
- Modify: `Source/StellarEraPawns/Src/Spaceship/SpaceshipPawn.h`
- Modify: `Source/StellarEraPawns/Src/Tests/InputComponentsTests.cpp`

**Interfaces:**
- Consumes: `ASpaceshipPawn` 的 8 个公开方法（签名不改）。
- Produces: `ICosmosBody`；Task 4 的相机搬进 Pawns 后才有资格 include Cosmos。

- [ ] **Step 1: 建接口头**

`Source/StellarEraCosmos/Src/CosmosBody.h`：

```cpp
// Copyright Voxel Plugin SAS. All Rights Reserved.
// 「被 N 体求解器驱动的一个天体」—— Cosmos 对受控实体的全部要求。

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "CosmosBody.generated.h"

class AVoxelPlanetActor;

UINTERFACE(MinimalAPI, meta = (CannotImplementInterfaceInBlueprint))
class UCosmosBody : public UInterface
{
	GENERATED_BODY()
};

/**
 * 受控天体（目前只有飞船）对模拟层暴露的接口。
 *
 * 为什么需要它：`UCosmosSimulationSubsystem` 与 `UCosmosTrajectoryComponent` 各有
 * 几处 `Cast<ASpaceshipPawn>`，把 Cosmos 钉死在 Pawns 上。那笔依赖反过来挡住了
 * 星图相机搬家（相机要 Cosmos，Pawns 里一放就成环）。接口抽出来后 Cosmos 只认
 * 本接口，Pawns 反而可以放心依赖 Cosmos。
 *
 * 声明在 Cosmos（消费方）、实现在 Pawns（`ASpaceshipPawn`）——依赖方向与
 * `IStarMapUIInputs`、`IStellarEraNoticeSink` 相反，因为这次消费方在低层。
 *
 * 全部方法与 `ASpaceshipPawn` 的现有公开方法逐字对应，实现方只需加 `override`。
 */
class STELLARERACOSMOS_API ICosmosBody
{
	GENERATED_BODY()

public:
	// ── 接触 ──

	/** 凸包探针不可用时的兜底接触半径（厘米）。 */
	virtual float GetContactRadiusCm() const = 0;

	/** 船体局部空间的接触探针；为空时求解器退回上面的球。 */
	virtual const TArray<FVector>& GetContactProbesLocalCm() const = 0;

	// ── 受力 ──

	/** 大气阻力要按「相对参考行星」的速度算，参考行星由这里给。 */
	virtual AVoxelPlanetActor* GetPlanetActor() const = 0;

	/** 子系统每帧推进来的参考行星速度（世界空间，厘米/秒）。 */
	virtual void SetReferencePlanetVelocity(const FVector& InVelocity) = 0;

	/**
	 * 玩家输入产生的推力加速度（世界空间，厘米/秒²）。
	 *
	 * 带 DeltaSeconds：飞船的制动要按「本帧满推力能改变多少速度」收敛。
	 */
	virtual FVector GetThrustAccelerationWorldCmPerS2(float DeltaSeconds) const = 0;

	/** 阻尼时间常数（秒）；<= 0 表示不加阻尼。 */
	virtual float GetDampingTime() const = 0;

	// ── 状态回写 ──

	/** 求解器把本帧速度写回来（世界空间，厘米/秒）。 */
	virtual void SetCosmosVelocity(const FVector& InVelocity) = 0;

	/** 调试视角是否打开（B 键）。合力箭头只在它打开时画。 */
	virtual bool IsDebugViewMode() const = 0;
};
```

- [ ] **Step 2: 飞船实现接口**

`SpaceshipPawn.h`：include 区加 `#include "CosmosBody.h"`；类声明改为

```cpp
class STELLARERAPAWNS_API ASpaceshipPawn : public APawn, public IVoxelViewpointInvokers, public ICosmosBody
```

给这 8 个已达公开的声明加 `override`（函数体不动，内联体原样保留）：

```cpp
	float GetContactRadiusCm() const override;
	const TArray<FVector>& GetContactProbesLocalCm() const override { return ContactProbesLocalCm; }
	AVoxelPlanetActor* GetPlanetActor() const override { return PlanetActor; }
	void SetReferencePlanetVelocity(const FVector& InVelocity) override { ReferencePlanetVelocity = InVelocity; }
	FVector GetThrustAccelerationWorldCmPerS2(float DeltaSeconds) const override;
	float GetDampingTime() const override;
	void SetCosmosVelocity(const FVector& InVelocity) override { CosmosVelocity = InVelocity; }
	bool IsDebugViewMode() const override { return bDebugViewMode; }
```

若某个内联体的成员名与上面不完全一致（如 `ContactProbesLocalCm` 实际叫别的），以文件里的真实成员名为准，只补 `override`。

- [ ] **Step 3: Cosmos 改用接口**

`CosmosSimulationSubsystem.cpp`：include `"SpaceshipPawn.h"` → `"CosmosBody.h"`；四处替换：

| 位置 | 改前 | 改后 |
|---|---|---|
| 145 行 | `else if (const ASpaceshipPawn* Ship = Cast<ASpaceshipPawn>(BodyActor))` | `else if (const ICosmosBody* Ship = Cast<ICosmosBody>(BodyActor))` |
| 216 行 | `ASpaceshipPawn* Ship = Component ? Cast<ASpaceshipPawn>(Component->GetOwner()) : nullptr;` | `ICosmosBody* Ship = Component ? Cast<ICosmosBody>(Component->GetOwner()) : nullptr;` |
| 395 行 | `ASpaceshipPawn* Ship = Cast<ASpaceshipPawn>(Component->GetOwner());` | `ICosmosBody* Ship = Cast<ICosmosBody>(Component->GetOwner());` |
| 660 行 | `if (ASpaceshipPawn* Ship = Cast<ASpaceshipPawn>(BodyActor))` | `if (ICosmosBody* Ship = Cast<ICosmosBody>(BodyActor))` |

（行号是 2026-11-06 的快照，改动后可能偏移；用 `grep -n "Cast<ASpaceshipPawn>"` 定位全部 6 处。）

`CosmosTrajectoryComponent.cpp`：include 同样换掉；302 行附近改为

```cpp
	const APawn* ControlledPawn = Cast<APawn>(OwnerActor);
	const ICosmosBody* ControlledBody = Cast<ICosmosBody>(OwnerActor);
	const bool bWantsNetForceArrow = ShouldShowNetForceArrow(
		bDrawNetForceArrow,
		ControlledPawn != nullptr && ControlledPawn->IsLocallyControlled(),
		Subsystem->IsStarMapActive(),
		ControlledBody != nullptr && ControlledBody->IsDebugViewMode());
```

1419 行附近：`else if (const ASpaceshipPawn* Ship = Cast<ASpaceshipPawn>(OwnerActor))` → `else if (const ICosmosBody* Ship = Cast<ICosmosBody>(OwnerActor))`。

`CosmosSimulationSubsystem.h` 里描述「只认 ASpaceshipPawn」「见 ASpaceshipPawn::...」的注释，把类型名改成 `ICosmosBody`，语义说明保留。

- [ ] **Step 4: 依赖换向（同一个提交）**

`StellarEraCosmos.Build.cs`：从 `PublicDependencyModuleNames` 里删掉 `"StellarEraPawns"`，并把注释段改成：

```csharp
		// 本模块只依赖 Planet（行星 Actor）。受控实体通过 CosmosBody.h 里的
		// ICosmosBody 接口反向接入 —— 原先的 StellarEraCosmos → StellarEraPawns
		// 那笔债（4+2 处 Cast<ASpaceshipPawn>）已随接口抽离还清，现在
		// Pawns → Cosmos 才是正确方向。
```

`StellarEraPawns.Build.cs`：`PublicDependencyModuleNames` 里 `"StellarEraPlanet"` 之后加

```csharp
			// 飞船在公开头里实现 Cosmos 的 ICosmosBody，星图相机（Src/StarMap/）
			// 也要调 UCosmosSimulationSubsystem —— 下游 include 这些公开头时必须
			// 找得到 Cosmos，所以是 Public 依赖。
			"StellarEraCosmos",
```

同时把 Build.cs 顶部注释里「星图相机**不在**这里……」那一段（第 14–15 行）改成：

```csharp
// 星图相机在 Src/StarMap/：它依赖 Cosmos，而 Cosmos 曾经反向 Cast<ASpaceshipPawn>。
// 抽出 ICosmosBody 之后那笔债还清，Pawns → Cosmos 才立得住。
```

- [ ] **Step 5: 补接口回归测试**

`InputComponentsTests.cpp` 顶部 include 区加 `#include "CosmosBody.h"`，文件末尾（`#endif` 之前）追加：

```cpp
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraShipImplementsCosmosBodyTest,
	"StellarEra.Pawns.ShipImplementsCosmosBody",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraShipImplementsCosmosBodyTest::RunTest(const FString& Parameters)
{
	const ASpaceshipPawn* ShipCDO = GetDefault<ASpaceshipPawn>();
	if (!TestNotNull(TEXT("ASpaceshipPawn CDO"), ShipCDO))
	{
		return false;
	}

	// Cosmos 靠这个转型驱动飞船；接口没实现的话飞船在求解器里是颗无接触点质量。
	TestNotNull(TEXT("飞船实现 ICosmosBody"), Cast<ICosmosBody>(ShipCDO));
	return true;
}
```

- [ ] **Step 6: 构建（依赖换向的最终判据）**

构建同 Task 1 Step 6。Expected: `Result: Succeeded`，且 UBT 不报 `Circular dependency`。若报环，说明 `StellarEraCosmos.Build.cs` 里的 `"StellarEraPawns"` 没删干净。

- [ ] **Step 7: 跑测试**

```bash
cd /c && "D:/Application/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" \
  "F:/workspace/ue/StellarEra/StellarEra.uproject" \
  -ExecCmds="Automation RunTests StellarEra.Cosmos" \
  -TestExit="Automation Test Queue Empty" -unattended -nopause -nullrhi -nosplash -stdout \
  -abslog="C:/Users/23080/AppData/Local/Temp/se_t3.log" > /dev/null 2>&1; \
  grep -cE "Result=\{Success\}" "C:/Users/23080/AppData/Local/Temp/se_t3.log"; \
  grep -E "Result=\{Fail" "C:/Users/23080/AppData/Local/Temp/se_t3.log"
```

Expected: 成功计数 > 0，无 `Result={Fail`。

- [ ] **Step 8: 提交**

```bash
cd "F:/workspace/ue/StellarEra" && git add Source/StellarEraCosmos Source/StellarEraPawns && \
git commit -m "refactor(cosmos): 抽 ICosmosBody 接口，Cosmos 不再依赖 Pawns

Co-Authored-By: Claude Code <noreply@anthropic.com>" -- Source/StellarEraCosmos Source/StellarEraPawns
```

---

### Task 4: 相机搬进 Pawns

**Files:**
- Move: `Source/StellarEraUI/Src/StarMapCameraPawn.h/.cpp` → `Source/StellarEraPawns/Src/StarMap/`
- Move: `Source/StellarEraUI/Src/Tests/StarMapCameraInvokerTests.cpp` → `Source/StellarEraPawns/Src/Tests/`
- Modify: `Source/StellarEraUI/StellarEraUI.Build.cs`（注释段）
- Modify: `Config/DefaultEngine.ini`（ClassRedirects）

**Interfaces:**
- Consumes: Task 3 的 `Pawns → Cosmos` 边。
- Produces: `/Script/StellarEraPawns.StarMapCameraPawn`。

- [ ] **Step 1: 移动文件（保留 git 历史）**

```bash
cd "F:/workspace/ue/StellarEra" && mkdir -p Source/StellarEraPawns/Src/StarMap && \
git mv Source/StellarEraUI/Src/StarMapCameraPawn.h Source/StellarEraPawns/Src/StarMap/StarMapCameraPawn.h && \
git mv Source/StellarEraUI/Src/StarMapCameraPawn.cpp Source/StellarEraPawns/Src/StarMap/StarMapCameraPawn.cpp && \
git mv Source/StellarEraUI/Src/Tests/StarMapCameraInvokerTests.cpp Source/StellarEraPawns/Src/Tests/StarMapCameraInvokerTests.cpp
```

- [ ] **Step 2: 确认 include 不用改**

相机 `.cpp` 与搬走的测试都是裸文件名 include（`CosmosSimulationSubsystem.h`、`PlanetEditComponent.h` 等），Pawns 的 include 路径循环已覆盖新子目录，**不需要改**。构建若报 C1083 才逐个修。

- [ ] **Step 3: 更新 UI 模块注释**

`StellarEraUI.Build.cs`：

1. 文件头描述里 `// 界面层：HUD、星图面板、矿物图例、编辑按钮布局、星图相机。` → 去掉末尾的 `、星图相机`。
2. 「关于星图相机为何仍在本模块」那一整段（从 `// ── 关于星图相机为何仍在本模块 ──` 到 `// 等抽出 ICosmosBody 接口、Cosmos 不再认识具体 Pawn 类型后再搬。`）替换为：

```csharp
// ── 星图相机已经搬走 ────────────────────────────────────────────────
// AStarMapCameraPawn 现在在 StellarEraPawns/Src/StarMap/。它依赖 Cosmos 的
// ICosmosBody（飞船实现的接口）之后，Pawns → Cosmos 这条边才成立 ——
// 之前搬不动是因为 Cosmos 内部 Cast<ASpaceshipPawn>，即 Cosmos → Pawns。
// 本模块仍然依赖 Pawns：HUD 要 include 相机头来认「现在是不是星图视角」。
```

3. `PrivateDependencyModuleNames` 里 `"StellarEraPawns"` 那行的尾注改为 `// 角色 / 飞船 / 星图相机`。

- [ ] **Step 4: 加资产重定向**

`Config/DefaultEngine.ini` 的 `[CoreRedirects]` 段内（行星侧那组注释之前）插入：

```ini
; ── 星图相机搬进 Pawns ─────────────────────────────────────────────
; AStarMapCameraPawn 从 StellarEraUI 移到 StellarEraPawns（2026-11-06）。
; Content/Code/BP_StarMapCameraPawn.uasset 是它的蓝图子类，存着旧脚本路径；
; BP_VoxelPlanet 的 Child Actor 组件也用着这个类。
+ClassRedirects=(OldName="/Script/StellarEraUI.StarMapCameraPawn",NewName="/Script/StellarEraPawns.StarMapCameraPawn")
```

- [ ] **Step 5: 构建**

构建同 Task 1 Step 6。Expected: `Result: Succeeded`。

- [ ] **Step 6: 测试**

```bash
cd /c && "D:/Application/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" \
  "F:/workspace/ue/StellarEra/StellarEra.uproject" \
  -ExecCmds="Automation RunTests StellarEra" \
  -TestExit="Automation Test Queue Empty" -unattended -nopause -nullrhi -nosplash -stdout \
  -abslog="C:/Users/23080/AppData/Local/Temp/se_t4.log" > /dev/null 2>&1; \
  grep -cE "Result=\{Success\}" "C:/Users/23080/AppData/Local/Temp/se_t4.log"; \
  grep -E "Result=\{Fail" "C:/Users/23080/AppData/Local/Temp/se_t4.log"
```

Expected: 成功数 ≥ 230（原 228 + 本次新增的 3 项测试中的 2 项已在前序 Task 落地，星图 12 项含搬过来的 1 项），唯一失败只允许既有的 `StellarEra.Planet.AtmosphereMaterial.Structure`。

- [ ] **Step 7: 重定向生效检查**

```bash
cd /c && "D:/Application/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" \
  "F:/workspace/ue/StellarEra/StellarEra.uproject" \
  -run=pythonscript -script="C:/Users/23080/AppData/Local/Temp/se_redirect_check.py" \
  -unattended -nopause -nullrhi -nosplash -stdout \
  -abslog="C:/Users/23080/AppData/Local/Temp/se_t4_redirect.log" > /dev/null 2>&1
```

`se_redirect_check.py` 内容（磁盘上有两份同名蓝图：`Content/Code/` 与 `Content/Code/Pawns/`，两份都要查）：

```python
import unreal

for pkg in ("/Game/Code/BP_StarMapCameraPawn.BP_StarMapCameraPawn_C",
            "/Game/Code/Pawns/BP_StarMapCameraPawn.BP_StarMapCameraPawn_C"):
    cls = unreal.load_class(None, pkg)
    if cls is None:
        print("REDIRECT-CHECK: SKIP %s（资产不在这里）" % pkg)
        continue
    path = cls.static_class().get_path_name()
    ok = path.endswith("/Script/StellarEraPawns.StarMapCameraPawn")
    print("REDIRECT-CHECK: %s %s -> %s" % ("OK" if ok else "FAIL", pkg, path))
```

Expected: 每个真实存在的资产都打印 `REDIRECT-CHECK: OK ... -> /Script/StellarEraPawns.StarMapCameraPawn`；`FAIL` 出现即为重定向未生效。脚本本身 `SKIP` 不算失败（路径写错另说，先按磁盘上的实际路径核）。

- [ ] **Step 8: 提交**

```bash
cd "F:/workspace/ue/StellarEra" && git add -A Source/StellarEraUI Source/StellarEraPawns Config/DefaultEngine.ini && \
git commit -m "refactor(星图): 相机搬进 StellarEraPawns/Src/StarMap

Co-Authored-By: Claude Code <noreply@anthropic.com>" -- Source/StellarEraUI Source/StellarEraPawns Config/DefaultEngine.ini
```

---

### Task 5: 文档同步

**Files:**
- Modify: `CLAUDE.md`
- Modify: `Source/README.md`
- Modify: `docs/ARCHITECTURE.md`

**Interfaces:** 无代码接口变化；只改文档里的模块归属与依赖矩阵。

- [ ] **Step 1: CLAUDE.md**

1. 模块结构树里 `└── StellarEraUI/          HUD、星图相机、星图面板、矿物图例、编辑按钮布局` → 去掉「星图相机、」；对应 `StellarEraPawns/` 那行补上「星图相机」。
2. 删掉「`AStarMapCameraPawn` 仍在 `StellarEraUI` 而不是 `StellarEraPawns`：……」整段（含末尾「等 `Cosmos → Pawns` 这笔债……才能搬进 Pawns。」），替换为：

```markdown
`AStarMapCameraPawn` 现在在 `StellarEraPawns/Src/StarMap/`：Cosmos 抽出
`ICosmosBody`（`Source/StellarEraCosmos/Src/CosmosBody.h`）之后不再认识
`ASpaceshipPawn`，`Pawns → Cosmos` 这条边才立得住。蓝图子类靠
`Config/DefaultEngine.ini` 的 `ClassRedirects` 兜底。
```

3. 关键类表的 `| AStarMapCameraPawn | UI |` 一行改成 `| AStarMapCameraPawn | Pawns |`。
4. 附带修正既有漂移：`Content/` 目录那段写的 `Maps/` 实际是 `Code/Maps/`（`/Game/Code/Maps/UniverseMap`），改成实际路径。

- [ ] **Step 2: Source/README.md**

1. L2 表 `StellarEraPawns` 的「一句话」补上星图相机；子目录树加一行 `├── StarMap/                   星图相机（不 Tick，位置在 CalcCamera 现算）`，`└── Tests/` 改成 `├──` 让它在最后。
2. 删掉「> **星图相机不在这个模块**……」那段引用块（第 97–99 行），替换为：

```markdown
> **星图相机在 `Src/StarMap/`。** 它依赖 Cosmos，而 Cosmos 曾经反向
> `Cast<ASpaceshipPawn>`；抽出 `ICosmosBody` 之后方向才理顺。
```

3. L4 表 `StellarEraUI` 的「一句话」去掉「星图相机、」。
4. 「常见任务」表 `改星图相机速度/缩放` 的路径改成 `StellarEraPawns/Src/StarMap/StarMapCameraPawn.cpp`。
5. 「三条纪律」第 3 条补一句：`ICosmosBody` 声明在 Cosmos、由 Pawns 实现 —— 消费方在低层时，接口就放低层。

- [ ] **Step 3: docs/ARCHITECTURE.md**

1. L4 依赖表 `StellarEraUI` 一行的「干什么」去掉星图相机。
2. 「为什么不能搬」那张表里 `AStarMapCameraPawn` 那一行改写成已解决：

```markdown
| `AStarMapCameraPawn` → Pawns | ~~`Pawns → Cosmos → Pawns`~~（已还清） | 2026-11-06 抽出 `ICosmosBody` 后相机已搬进 `Pawns/Src/StarMap/` |
```

3. 提到「UI 的星图相机」的句子改成「Pawns 的星图相机」。

- [ ] **Step 4: 提交**

```bash
cd "F:/workspace/ue/StellarEra" && git add CLAUDE.md Source/README.md docs/ARCHITECTURE.md && \
git commit -m "docs(架构): 同步输入组件与星图相机搬家后的模块归属

Co-Authored-By: Claude Code <noreply@anthropic.com>" -- CLAUDE.md Source/README.md docs/ARCHITECTURE.md
```

---

### Task 6: 全量验证

**Files:** 无（只跑验证并记录证据）

- [ ] **Step 1: 架构体检**

```bash
cd "F:/workspace/ue/StellarEra" && python Tools/check_architecture.py 2>&1 | tail -20
```

Expected: 无依赖环、无新增超大文件警告。

- [ ] **Step 2: 全量测试**

同 Task 4 Step 6。Expected: 成功 ≥ 230，唯一失败 `StellarEra.Planet.AtmosphereMaterial.Structure`。

- [ ] **Step 3: 无头启动验证（飞船路径）**

```bash
cd /c && UE="D:/Application/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor.exe"; \
MSYS2_ARG_CONV_EXCL='*' "$UE" "F:/workspace/ue/StellarEra/StellarEra.uproject" /Game/Code/Maps/UniverseMap \
  -game -nullrhi -nosplash -unattended -stdout -LogCmds="LogTemp Verbose" \
  -abslog="C:/Users/23080/AppData/Local/Temp/se_final_ship.log" \
  > /dev/null 2>&1 & PID=$!; sleep 150; kill $PID 2>/dev/null; \
grep -nE "\[Input\]|初始化完成|绑定失败|Failed to enter|StarMapCamera" \
  "C:/Users/23080/AppData/Local/Temp/se_final_ship.log" | head -10
```

Expected：出现 `[Input] ASpaceshipPawn(...) 已绑定飞船输入`、`[Cosmos] 初始化完成：5 个天体`；`绑定失败` 与 `Failed to enter` 各 0 条；与 `StarMapCamera` 相关的缺类报错也是 0 条。

- [ ] **Step 4: 无头启动验证（角色路径）**

同 Step 3，命令行再加 `-ExecCmds="DebugDisembark"`，日志另存 `se_final_character.log`。Expected：除飞船那条外，还出现 `[Input] APlanetCharacter(...) 已绑定角色输入`。

- [ ] **Step 5: 汇总证据并报告**

把四组证据（架构体检、测试计数、两份无头日志的关键行、重定向检查）整理成表交给用户；**没有证据的项不要声称通过**。

---

## Self-Review

**1. Spec coverage**

| Spec 段落 | 任务 |
|---|---|
| §4.1 输入组件（两个类、Bind、Pawn 转发） | Task 1、Task 2 |
| §4.2 `ICosmosBody` 八方法、6 处 Cast、依赖换向 | Task 3 |
| §4.3 相机与测试搬家、`Src/StarMap/` | Task 4 |
| §4.4 依赖矩阵 | Task 3（边）、Task 4（Build.cs 注释） |
| §4.5 ClassRedirect | Task 4 Step 4 / Step 7 |
| §5 影响面里的文档三项 | Task 5 |
| §6 验证五条 | Task 1/2/3/4 的分步验证 + Task 6 |

**2. Placeholder scan**：无 TBD/TODO；每个代码步骤都给了可直接粘贴的代码或明确的逐行替换表。

**3. Type consistency**

- 两个组件的 `Bind(UInputComponent*)` 签名一致，Pawn 侧调用一致。
- `ICosmosBody` 八个方法与 `ASpaceshipPawn` 现有签名逐字核对过：`float GetContactRadiusCm() const`、`const TArray<FVector>& GetContactProbesLocalCm() const`、`AVoxelPlanetActor* GetPlanetActor() const`、`void SetReferencePlanetVelocity(const FVector&)`、`FVector GetThrustAccelerationWorldCmPerS2(float) const`、`float GetDampingTime() const`、`void SetCosmosVelocity(const FVector&)`、`bool IsDebugViewMode() const`。
- 日志文本 `[Input] APlanetCharacter(...) 已绑定角色输入` / `[Input] ASpaceshipPawn(...) 已绑定飞船输入` 与验证步骤的 grep 串一致。
