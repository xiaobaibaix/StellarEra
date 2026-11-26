# 星图相机 Pawn（复用 rig）Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 按 M 自动锁定最近天体，生成并 Possess 一台挂载 `UStarMapRigComponent` 的 `AStarMapCameraPawn`，退出时 Possess 回源 Pawn。

**Architecture:** 相机逻辑全部留在 rig；rig 新增 `SourcePawn`/`FocusBody`，所有「源 Pawn 的相机 / 编辑组件 / 调用器」查询走它。控制器负责生成、附加、Possess 与退出收尾；飞船与角色只保留 M 的绑定。

**Tech Stack:** UE 5.8.4 C++（Game / Pawns / Planet / UI 四模块）、Enhanced Input、VoxelFree 调用器。

**Spec:** `docs/superpowers/specs/2026-11-26-starmap-camera-pawn-design.md`

## Global Constraints

- 组件名与 UPROPERTY 变量名一律驼峰，面板显示名与变量名逐字一致（`meta=(DisplayName="Xxx")`）。
- 含中文的 `.h/.cpp` 存 UTF-8 with BOM。
- 注释只写「为什么」，单处 1–4 行。
- 构建必须先关编辑器；用捆绑 dotnet 直调 UBT。
- 完成后必须跑自动化测试并在 PIE 里看到实际效果。

---

### Task 1: rig 支持源 Pawn 与外部入场

**Files:**
- Modify: `Source/StellarEraPawns/Src/StarMap/StarMapRigComponent.h`
- Modify: `Source/StellarEraPawns/Src/StarMap/StarMapRigComponent.cpp`
- Test: `Source/StellarEra/Src/Tests/StarMapTests.cpp`

**Interfaces:**
- Produces:
  - `void SetSourcePawn(APawn* InSourcePawn);`
  - `APawn* GetSourcePawn() const;`
  - `void EnterFrom(APawn* InSourcePawn, AActor* Focus, const FVector& ViewLocation, const FRotator& ViewRotation);`
  - `void Exit();`（公开）
  - `bool IsExiting() const { return bExiting; }`
  - `static AActor* FindNearestBody(const FVector& FromLocation, const TArray<AActor*>& Bodies);`

- [ ] **Step 1: 头文件加成员与签名**

在 `UStarMapRigComponent` public 区加：
```cpp
void SetSourcePawn(APawn* InSourcePawn);
APawn* GetSourcePawn() const { return SourcePawn.Get(); }

/** 由控制器在 Possess 前后调用；ViewLocation/ViewRotation 是进图前的视点。 */
void EnterFrom(APawn* InSourcePawn, AActor* Focus,
	const FVector& ViewLocation, const FRotator& ViewRotation);

void Exit();
bool IsExiting() const { return bExiting; }

/** 球心距离最近的天体；Bodies 为空或全失效返回 nullptr。 */
static AActor* FindNearestBody(const FVector& FromLocation, const TArray<AActor*>& Bodies);
```
private 加：
```cpp
TWeakObjectPtr<APawn> SourcePawn;
TWeakObjectPtr<AActor> FocusBody;
void AttachOwnerToFocus(AActor* Focus);
```
`Toggle` / `Enter` / `ResolveFocusPlanet` 本任务**保留不动**（旧调用方还在用），到 Task 3 改完输入路由后再删。

- [ ] **Step 2: 写失败测试（最近天体）**

`StarMapTests.cpp` 末尾加：
```cpp
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraStarMapNearestBodyTest,
	"StellarEra.StarMap.NearestBody",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraStarMapNearestBodyTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Editor, false);
	if (!TestNotNull(TEXT("测试世界"), World)) { return false; }
	FWorldContext& Ctx = GEngine->CreateNewWorldContext(EWorldType::Editor);
	Ctx.SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());
	ON_SCOPE_EXIT { GEngine->DestroyWorldContext(World); World->DestroyWorld(false); };

	TArray<AActor*> Bodies;
	TestNull(TEXT("空列表"), UStarMapRigComponent::FindNearestBody(FVector::ZeroVector, Bodies));

	AStaticMeshActor* Near = World->SpawnActor<AStaticMeshActor>();
	AStaticMeshActor* Far = World->SpawnActor<AStaticMeshActor>();
	Near->SetActorLocation(FVector(100.0, 0.0, 0.0));
	Far->SetActorLocation(FVector(1000.0, 0.0, 0.0));
	Bodies = { Far, Near };
	TestSamePtr(TEXT("球心距离最近"), UStarMapRigComponent::FindNearestBody(FVector::ZeroVector, Bodies), Near);
	return true;
}
```
需要 `#include "Engine/StaticMeshActor.h"`。

- [ ] **Step 3: 跑测试确认失败**

`UnrealEditor-Cmd.exe ... -ExecCmds="Automation RunTests StellarEra.StarMap.NearestBody" ...`
预期编译失败：`FindNearestBody` 未定义。

- [ ] **Step 4: 实现**

`StarMapRigComponent.cpp`：
```cpp
void UStarMapRigComponent::SetSourcePawn(APawn* InSourcePawn)
{
	SourcePawn = InSourcePawn;
}

AActor* UStarMapRigComponent::FindNearestBody(const FVector& FromLocation, const TArray<AActor*>& Bodies)
{
	AActor* Best = nullptr;
	double BestDistSq = TNumericLimits<double>::Max();
	for (AActor* Body : Bodies)
	{
		if (!IsValid(Body)) { continue; }
		const double DistSq = FVector::DistSquared(FromLocation, Body->GetActorLocation());
		if (DistSq < BestDistSq) { BestDistSq = DistSq; Best = Body; }
	}
	return Best;
}

void UStarMapRigComponent::EnterFrom(APawn* InSourcePawn, AActor* Focus,
	const FVector& ViewLocation, const FRotator& ViewRotation)
{
	UCosmosSimulationSubsystem* Subsystem = GetWorld() ? GetWorld()->GetSubsystem<UCosmosSimulationSubsystem>() : nullptr;
	if (!InSourcePawn || !Subsystem || !Focus) { return; }

	SourcePawn = InSourcePawn;
	FocusBody = Focus;

	if (UPlanetEditComponent* Edit = GetPlanetEditComponent())
	{
		if (Edit->IsEditMode()) { Edit->SetEditMode(false); }
	}

	CameraLocation = ViewLocation;
	OrbitRotation = ViewRotation;
	LastViewRotation = ViewRotation;
	AimOffset = FRotator::ZeroRotator;
	LastSeenSelected = nullptr;
	bSmoothFocusActive = false;
	bActive = true;
	bExiting = false;

	if (ASpaceshipPawn* Ship = Cast<ASpaceshipPawn>(InSourcePawn))
	{
		Ship->SetThrust(0.f); Ship->SetRoll(0.f); Ship->SetVertical(0.f); Ship->SetBoost(false);
	}

	Subsystem->SetSelectedBody(nullptr);
	Subsystem->SetStarMapActive(true);
	Subsystem->SetSelectedBody(Focus);

	FocusOn(Focus, PlanetFocusRadiusScale * StarMapPicking::GetBodyRadiusCm(Focus));

	ApplyStarMapEditSettings();
	if (UPlanetEditComponent* Edit = GetPlanetEditComponent())
	{
		if (APlanetCharacter* Character = Cast<APlanetCharacter>(InSourcePawn))
		{
			Edit->SetGridPlanet(nullptr);
			(void)Character;
		}
		Edit->SetTargetResolver([this](const FVector& RayOrigin, const FVector& RayDirection,
			const float MaxDistanceCm, FPlanetEditTarget& OutTarget)
		{
			return ResolveEditTargetByPlanets(RayOrigin, RayDirection, MaxDistanceCm, OutTarget);
		});
	}

	BeginInvokerFollow();
	AttachOwnerToFocus(Focus);
}
```
`AttachOwnerToFocus`：
```cpp
void UStarMapRigComponent::AttachOwnerToFocus(AActor* Focus)
{
	if (AActor* Owner = GetOwner(); Owner && IsValid(Focus))
	{
		Owner->AttachToActor(Focus, FAttachmentTransformRules::KeepWorldTransform);
	}
}
```

- [ ] **Step 5: 把既有查询改成读 SourcePawn**

- `GetPlanetEditComponent()`：`const APawn* Source = SourcePawn.Get(); const AActor* Owner = Source ? Source : GetOwner(); ...`
- `ResolveOwnerCameraLocation/Rotation()`：同上，取 `Source ? Source : GetOwner()` 的 `UCameraComponent`；没有时回退 `CameraLocation`/`LastViewRotation`。
- `BeginInvokerFollow()`：`AActor* Owner = SourcePawn.IsValid() ? SourcePawn.Get() : GetOwner();`
- `Exit()` 里 `Cast<APlanetCharacter>(GetOwner())` → `Cast<APlanetCharacter>(SourcePawn.Get())`。
- `EndPlay` 保持不变（`GetPlanetEditComponent` 已走源 Pawn）。

- [ ] **Step 6: 选中变化时改挂**

`CalcView` 的 `Selected != LastSeenSelected.Get()` 分支末尾加：
```cpp
AttachOwnerToFocus(Selected);
```

- [ ] **Step 7: 构建 + 跑测试**

关编辑器 → UBT 构建 → `Automation RunTests StellarEra.StarMap`。预期 `NearestBody` PASS，其余不变。

---

### Task 2: 相机 Pawn 挂 rig 与输入

**Files:**
- Modify: `Source/StellarEraPawns/Src/StarMap/StarMapCameraPawn.h`
- Modify: `Source/StellarEraPawns/Src/StarMap/StarMapCameraPawn.cpp`

**Interfaces:**
- Consumes: `UStarMapRigComponent::CalcView/IsActive/IsExiting`、`UStarMapInputComponent::Bind`。
- Produces: `APawn* GetSourcePawn() const`、`UStarMapRigComponent* GetRig() const`、`SetSourcePawn(APawn*)`。

- [ ] **Step 1: 头文件**

```cpp
class UStarMapInputComponent;
class UStarMapRigComponent;

UCLASS()
class STELLARERAPAWNS_API AStarMapCameraPawn : public APawn
{
	GENERATED_BODY()
public:
	AStarMapCameraPawn();

	virtual void CalcCamera(float DeltaSeconds, FMinimalViewInfo& OutResult) override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;

	void SetSourcePawn(APawn* InSourcePawn);
	APawn* GetSourcePawn() const { return SourcePawn.Get(); }
	UStarMapRigComponent* GetRig() const { return StarMapRig; }

private:
	UPROPERTY(VisibleAnywhere, Category = "StarMap", meta = (DisplayName = "StarMapRoot"))
	TObjectPtr<USceneComponent> StarMapRoot;

	UPROPERTY(VisibleAnywhere, Category = "StarMap", meta = (DisplayName = "StarMapRig"))
	TObjectPtr<UStarMapRigComponent> StarMapRig;

	UPROPERTY(VisibleAnywhere, Category = "StarMap", meta = (DisplayName = "StarMapInput"))
	TObjectPtr<UStarMapInputComponent> StarMapInput;

	TWeakObjectPtr<APawn> SourcePawn;
};
```
`#include "StarMapRigComponent.h"` 只放 `.cpp`；头文件前向声明 + `TObjectPtr` 可用（`TObjectPtr` 对不完整类型 OK）。`FMinimalViewInfo` 用前向声明（已有用法参考 SpaceshipPawn.h）。

- [ ] **Step 2: cpp**

```cpp
AStarMapCameraPawn::AStarMapCameraPawn()
{
	StarMapRoot = CreateDefaultSubobject<USceneComponent>(TEXT("StarMapRoot"));
	SetRootComponent(StarMapRoot);

	StarMapRig = CreateDefaultSubobject<UStarMapRigComponent>(TEXT("StarMapRig"));
	StarMapInput = CreateDefaultSubobject<UStarMapInputComponent>(TEXT("StarMapInput"));
}

void AStarMapCameraPawn::SetSourcePawn(APawn* InSourcePawn)
{
	SourcePawn = InSourcePawn;
	if (StarMapRig) { StarMapRig->SetSourcePawn(InSourcePawn); }
}

void AStarMapCameraPawn::CalcCamera(float DeltaSeconds, FMinimalViewInfo& OutResult)
{
	Super::CalcCamera(DeltaSeconds, OutResult);
	if (StarMapRig && (StarMapRig->IsActive() || StarMapRig->IsExiting()))
	{
		StarMapRig->CalcView(DeltaSeconds, OutResult);
	}
}

void AStarMapCameraPawn::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);
	if (StarMapInput) { StarMapInput->Bind(PlayerInputComponent); }
}
```

- [ ] **Step 3: 构建**

关编辑器 → UBT → 预期 `Succeeded`。

---

### Task 3: 会话命令与控制器进出图

**Files:**
- Modify: `Source/StellarEraPawns/Src/SessionCommands.h`
- Modify: `Source/StellarEraPawns/Src/StarMap/Input/StarMapInputComponent.cpp`
- Modify: `Source/StellarEra/Src/StellarEraPlayerController.h`
- Modify: `Source/StellarEra/Src/StellarEraPlayerController.cpp`

**Interfaces:**
- Consumes: `AStarMapCameraPawn::GetRig/GetSourcePawn/SetSourcePawn`、`UStarMapRigComponent::EnterFrom/Exit/IsActive/IsExiting/FindNearestBody`、`UCosmosSimulationSubsystem::GetRegisteredBodies`。
- Produces: `IStellarEraSessionCommands::ToggleStarMap(APawn* From)`；`AStellarEraPlayerController::ToggleStarMap`。

- [ ] **Step 1: 接口加方法**

`SessionCommands.h`：
```cpp
/**
 * M：进/出星图。实现方负责生成、Possess 相机 Pawn 与退图收尾。
 * From 是相机 Pawn 时表示退出。
 */
virtual void ToggleStarMap(APawn* From) = 0;
```

- [ ] **Step 2: 输入组件改路由**

`StarMapInputComponent.cpp` `Bind` 的保护条件去掉 `Rig`：
```cpp
if (!OwnerPawn || !EnhancedInput || !Source)
```
`OnStarMapToggle`：
```cpp
void UStarMapInputComponent::OnStarMapToggle(const FInputActionValue& Value)
{
	bSelectHeld = false;
	APawn* OwnerPawn = Cast<APawn>(GetOwner());
	IStellarEraSessionCommands* Session =
		OwnerPawn ? Cast<IStellarEraSessionCommands>(OwnerPawn->GetController()) : nullptr;
	if (Session && OwnerPawn) { Session->ToggleStarMap(OwnerPawn); }
}
```
`#include "SessionCommands.h"`。

随后删除 `UStarMapRigComponent::Toggle`（头文件声明与实现），以及只在旧 `Enter` 里用的 `ResolveFocusPlanet`；`Enter` 也删。确认全项目 `grep -rn "Rig->Toggle\|\.Toggle(" Source/` 无命中。

- [ ] **Step 3: 控制器头文件**

`AStellarEraPlayerController` public 加：
```cpp
virtual void ToggleStarMap(APawn* From) override;

/** 调试/无头验证：等价于按 M。 */
UFUNCTION(Exec)
void DebugToggleStarMap();
```
private 加：
```cpp
UPROPERTY(EditDefaultsOnly, Category = "StellarEra")
TSubclassOf<AStarMapCameraPawn> StarMapCameraPawnClass;
```
前置声明 `class AStarMapCameraPawn;`。

- [ ] **Step 4: 控制器实现**

```cpp
void AStellarEraPlayerController::ToggleStarMap(APawn* From)
{
	if (AStarMapCameraPawn* CameraPawn = Cast<AStarMapCameraPawn>(From))
	{
		if (UStarMapRigComponent* Rig = CameraPawn->GetRig(); Rig && Rig->IsActive())
		{
			Rig->Exit();
		}
		return;
	}

	if (!From || From != GetPawn()) { return; }
	UCosmosSimulationSubsystem* Subsystem =
		GetWorld() ? GetWorld()->GetSubsystem<UCosmosSimulationSubsystem>() : nullptr;
	if (!Subsystem) { return; }

	TArray<AActor*> Bodies;
	Subsystem->GetRegisteredBodies(Bodies);
	AActor* Focus = UStarMapRigComponent::FindNearestBody(From->GetActorLocation(), Bodies);
	if (!Focus) { ShowPrompt(TEXT("附近没有天体")); return; }

	FVector ViewLocation;
	FRotator ViewRotation;
	GetPlayerViewPoint(ViewLocation, ViewRotation);

	FActorSpawnParameters Params;
	Params.Owner = this;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	const TSubclassOf<AStarMapCameraPawn> Class =
		StarMapCameraPawnClass ? StarMapCameraPawnClass : TSubclassOf<AStarMapCameraPawn>(AStarMapCameraPawn::StaticClass());
	AStarMapCameraPawn* CameraPawn = GetWorld()->SpawnActor<AStarMapCameraPawn>(
		Class, ViewLocation, ViewRotation, Params);
	if (!CameraPawn) { return; }

	CameraPawn->SetSourcePawn(From);
	if (UStarMapRigComponent* Rig = CameraPawn->GetRig())
	{
		Rig->EnterFrom(From, Focus, ViewLocation, ViewRotation);
	}
	Possess(CameraPawn);
}

void AStellarEraPlayerController::DebugToggleStarMap()
{
	ToggleStarMap(GetPawn());
}
```
构造里 `StarMapCameraPawnClass = AStarMapCameraPawn::StaticClass();`（若已有构造函数）。
include `StarMapCameraPawn.h`。

`ApplyViewpointInvokerGate` 循环前：
```cpp
const APawn* ViewpointPawn = InPawn;
if (const AStarMapCameraPawn* CameraPawn = Cast<AStarMapCameraPawn>(InPawn))
{
	ViewpointPawn = CameraPawn->GetSourcePawn();
}
```
循环里 `Pawn == InPawn` → `Pawn == ViewpointPawn`。

`PlayerTick` 顶部加退出收尾：
```cpp
if (AStarMapCameraPawn* CameraPawn = Cast<AStarMapCameraPawn>(GetPawn()))
{
	UStarMapRigComponent* Rig = CameraPawn->GetRig();
	if (Rig && !Rig->IsActive() && !Rig->IsExiting())
	{
		if (APawn* Source = CameraPawn->GetSourcePawn())
		{
			Possess(Source);
			CameraPawn->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
			CameraPawn->Destroy();
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("星图退出：源 Pawn 已失效，保留相机 Pawn"));
		}
	}
}
```

- [ ] **Step 5: 构建**

关编辑器 → UBT → `Succeeded`。此时飞船/角色仍挂自己的 rig，但不再被 Toggle；下一步删。

---

### Task 4: 飞船/角色移除 rig

**Files:**
- Modify: `Source/StellarEraPawns/Src/Spaceship/SpaceshipPawn.h`
- Modify: `Source/StellarEraPawns/Src/Spaceship/SpaceshipPawn.cpp`
- Modify: `Source/StellarEraPawns/Src/Character/PlanetCharacter.h`
- Modify: `Source/StellarEraPawns/Src/Character/PlanetCharacter.cpp`

**Interfaces:**
- Consumes: `UCosmosSimulationSubsystem::IsStarMapActive()`。

- [ ] **Step 1: 删 rig**

两个类都：
- 删 `StarMapRig` UPROPERTY、`CreateDefaultSubobject`、`#include "StarMapRigComponent.h"`、`CalcCamera` 里的 `if (IsStarMapActive()) { StarMapRig->CalcView(...); return; }`。
- `IsStarMapActive()` 改为：
```cpp
bool ASpaceshipPawn::IsStarMapActive() const
{
	const UCosmosSimulationSubsystem* Subsystem =
		GetWorld() ? GetWorld()->GetSubsystem<UCosmosSimulationSubsystem>() : nullptr;
	return Subsystem && Subsystem->IsStarMapActive();
}
```
角色同理。需要 `#include "CosmosSimulationSubsystem.h"`。

- [ ] **Step 2: 构建 + 自动化**

关编辑器 → UBT → `Automation RunTests StellarEra`。预期全绿。

---

### Task 5: HUD 编辑面板回退

**Files:**
- Modify: `Source/StellarEraUI/Src/StellarEraHUD.cpp`

- [ ] **Step 1: 回退到源 Pawn**

```cpp
const APlayerController* PlayerController = GetOwningPlayerController();
APawn* ViewedPawn = PlayerController ? PlayerController->GetPawn() : nullptr;
const UPlanetEditComponent* Edit =
	ViewedPawn ? ViewedPawn->FindComponentByClass<UPlanetEditComponent>() : nullptr;
if (!Edit)
{
	if (const AStarMapCameraPawn* CameraPawn = Cast<AStarMapCameraPawn>(ViewedPawn))
	{
		const APawn* Source = CameraPawn->GetSourcePawn();
		Edit = Source ? Source->FindComponentByClass<UPlanetEditComponent>() : nullptr;
	}
}
```
`#include "StarMapCameraPawn.h"`。

- [ ] **Step 2: 构建**

UBT → `Succeeded`。

---

### Task 6: 端到端验证

- [ ] **Step 1: 全量测试**

`Automation RunTests StellarEra`，记录结果。

- [ ] **Step 2: PIE 实测**

编辑器前台打开 `UniverseMap` → 开始 PIE（飞船或角色）→ 控制台 `DebugToggleStarMap`：
- 相机平滑聚焦最近天体（截图）；
- 滚轮缩放、右键转头、左键点选换天体、相机 Pawn 改挂新天体；
- `DebugToggleStarMap` 退出，0.5 s 后回到原 Pawn（截图 + 日志无 ensure/LNK/泄漏）。

- [ ] **Step 3: 清理**

关闭 PIE；确认 `git status` 只含预期的源码与 spec/plan 文件。
