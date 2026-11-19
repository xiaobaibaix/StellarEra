# 星图 rig 化实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 把 `AStarMapCameraPawn`（每颗行星挂一台、按 M 换 Possess）改成 `UStarMapRigComponent`（飞船/角色自带、接管自己的相机），并删掉整条行星挂相机链。

**Architecture:** 绕行/缩放/聚焦/拾取的数学与参数整体搬进组件；组件在 owner Pawn 的 `CalcCamera` 里覆盖视点，位置 = 焦点 − 轨道朝向 × 距离，逐帧现算，等价于「相机挂在行星下」但不换 Possess。键鼠输入与编辑组件跟着搬到 Pawn 上，用「两边 `GetPawn()` 互斥」实现输入隔离。

**Tech Stack:** UE 5.8.3（Win64）、C++（UHT/UCLASS 组件）、Enhanced Input、Unreal Automation Test、VibeUE MCP（资产脚本）。

**Spec:** `docs/superpowers/specs/2026-11-19-starmap-rig-camera-design.md`

## Global Constraints

- **构建前必须关闭编辑器**（Live Coding 锁 DLL）。构建命令（cwd 必须在项目外，例如 `C:/`）：
  ```bash
  "D:/Application/Epic Games/UE_5.8/Engine/Binaries/ThirdParty/DotNet/10.0/win-x64/dotnet.exe" \
    "D:/Application/Epic Games/UE_5.8/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.dll" \
    StellarEraEditor Win64 Development \
    -Project="F:/workspace/ue/StellarEra/StellarEra.uproject" -WaitMutex -FromMsBuild
  ```
  成功标志：日志末出现 `Total time in Unreal Build Tool: ...` 且无 `error C` / `error LNK`。
- 测试命令（编辑器开着也能跑；`nullrhi`）：
  ```bash
  "D:/Application/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" \
    "F:/workspace/ue/StellarEra/StellarEra.uproject" \
    -ExecCmds="Automation RunTests StellarEra.StarMap" \
    -TestExit="Automation Test Queue Empty" \
    -unattended -nopause -nullrhi -nosplash -stdout -abslog="C:/Temp/starmap-tests.log"
  ```
  结果看日志里的 `Automation Test Succeeded/Failed` 计数。
- **含中文的新 `.h` / `.cpp` 必须存 UTF-8 with BOM**。写完文件后执行：
  ```bash
  python -c "p=PATH;p_b=open(p,'rb').read();open(p,'wb').write(b'\xef\xbb\xbf'+p_b if not p_b.startswith(b'\xef\xbb\xbf') else p_b)"
  ```
- **提交必须带 pathspec**（索引里有常驻暂存文件）：`git add <paths>` 后 `git commit -m "..." -- <paths>`。
- 组件名字段一律驼峰、只含字母数字（`StarMapRig`、`StarMapInput`）。
- 注释只写「为什么」，单处 1–3 行。
- 不新增模块、不改任何 `Build.cs`（Pawns 已依赖 Cosmos / Planet / Voxel / EnhancedInput）。

---

### Task 1: 新建 `UStarMapRigComponent`（搬运 + 新外壳）

**Files:**
- Create: `Source/StellarEraPawns/Src/StarMap/StarMapRigComponent.h`
- Create: `Source/StellarEraPawns/Src/StarMap/StarMapRigComponent.cpp`
- Read (源材料): `Source/StellarEraPawns/Src/StarMap/StarMapCameraPawn.cpp`、`StarMapCameraPawn.h`

**Interfaces:**
- Produces（后续任务依赖的确切签名）:
  - `void UStarMapRigComponent::Toggle(APlayerController* PC)`
  - `bool UStarMapRigComponent::IsActive() const`
  - `void UStarMapRigComponent::CalcView(float DeltaSeconds, FMinimalViewInfo& OutResult)`
  - `void OrbitLook(float DeltaYaw, float DeltaPitch)` / `void AimLook(float, float)` / `void AddZoom(float)`
  - `float GetSelectionPixelRadius() const`
  - `UPlanetEditComponent* GetPlanetEditComponent() const`
  - static：`ClampAimOffset`、`OrbitLookScale`、`SilhouetteRadiusScale`、`ZoomAltitudeCm`、`RaySphereFirstHitCm`、`LimitDistanceByObstaclesCm`、`PickBodyAtScreenPosition`（签名与 `AStarMapCameraPawn` 完全一致）
  - `struct FStarMapScreenCandidate`、`struct FStarMapObstacle`（从相机头文件原样搬来）

- [ ] **Step 1: 写头文件**

`StarMapRigComponent.h`：

```cpp
// Copyright Voxel Plugin SAS. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "StarMapRigComponent.generated.h"

class APlayerController;
class AVoxelPlanetActor;
class UPlanetEditComponent;
class USceneComponent;
class UVoxelSimpleInvokerComponent;
struct FMinimalViewInfo;
struct FPlanetEditTarget;

/** 屏幕空间候选：天体 Actor + 投影位置 + 盘面半径。用结构体而非平行数组，避免下标错位。 */
struct FStarMapScreenCandidate
{
	AActor* Actor = nullptr;
	FVector2D ScreenPosition = FVector2D::ZeroVector;

	/** 屏幕盘面半径（像素），0 = 未算出。拾取容差取 max(固定像素容差, 本值)。 */
	float ScreenRadiusPx = 0.f;
};

/** 相机拉远路径上的挡路球体（球心 + 地表半径）。纯数据，夹取逻辑可无头测。 */
struct FStarMapObstacle
{
	FVector Center = FVector::ZeroVector;
	double RadiusCm = 0.0;
};

/**
 * 星图 rig：挂在飞船 / 角色身上，进图后接管该 Pawn 的相机。
 * 位置在 CalcView 现算（子系统在 TickObjects 写回位置后才调 CalcCamera），所以不 Tick。
 */
UCLASS(ClassGroup = (StellarEra), meta = (BlueprintSpawnableComponent))
class STELLARERAPAWNS_API UStarMapRigComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UStarMapRigComponent();

	//~ Begin UActorComponent Interface
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	//~ End UActorComponent Interface

	/** M 键：未激活则进图，激活则退图；退图过渡期间重复调用忽略。 */
	void Toggle(APlayerController* PC);

	bool IsActive() const { return bActive; }

	/** owner Pawn 的 CalcCamera 在 IsActive() 时调用；OutResult 已由 Super 填好 FOV / 后期。 */
	void CalcView(float DeltaSeconds, FMinimalViewInfo& OutResult);

	void OrbitLook(float DeltaYaw, float DeltaPitch);
	void AimLook(float DeltaYaw, float DeltaPitch);
	void AddZoom(float Value);

	/** 左键拾取天体的像素容差。星图输入组件与 HUD 悬停查询共用。 */
	float GetSelectionPixelRadius() const { return SelectionPixelRadius; }

	/** owner 上的行星编辑组件；飞船 / 角色都可能是蓝图挂的。 */
	UPlanetEditComponent* GetPlanetEditComponent() const;

	// ── 纯函数（可无头测，签名与旧相机完全一致）──

	static FRotator ClampAimOffset(const FRotator& BaseRotation, const FRotator& AimOffset, double MaxOffsetDeg);
	static double OrbitLookScale(double DistanceCm, double FocusSurfaceRadiusCm, double MinScale, double MaxScale);
	static double SilhouetteRadiusScale(double CameraDistanceCm, double BodyRadiusCm);
	static double ZoomAltitudeCm(double AltitudeCm, double StepFactor, float Value, double MinAltitudeCm, double MaxAltitudeCm);
	static double RaySphereFirstHitCm(const FVector& RayOrigin, const FVector& UnitDir, const FVector& SphereCenter, double SphereRadiusCm);
	static double LimitDistanceByObstaclesCm(
		const FVector& FocusLocation, const FVector& RayDirection,
		TArrayView<const FStarMapObstacle> Obstacles,
		double MinAltitudeCm, double MinDistanceCm, double CurrentMaxCm);
	static AActor* PickBodyAtScreenPosition(
		TArrayView<const FStarMapScreenCandidate> Candidates,
		const FVector2D& ScreenPoint, float PixelRadius);

private:
	void Enter(APlayerController* PC);
	void Exit();

	/** 焦点行星：飞船取最近行星，角色取脚下那颗；都没有返回空（不进图）。 */
	AVoxelPlanetActor* ResolveFocusPlanet() const;

	/** 当前选中的天体（每帧从子系统读，不自己存）。 */
	AActor* ResolveSelectedBody() const;

	/** 焦点在「焦点 → 相机」方向的表面半径：体素行星查真实地表，非体素用碰撞半径。 */
	double ResolveFocusSurfaceRadiusCm(AActor* Focus) const;

	/** 当前视线下相机到焦点距离的合法区间；**每帧重算**（行星在动）。 */
	void ComputeOrbitDistanceLimitsCm(AActor* Focus, double& OutMinCm, double& OutMaxCm) const;

	/** 屏幕射线 → 最近被击中的体素行星表面点。 */
	bool ResolveEditTargetByPlanets(const FVector& RayOrigin, const FVector& RayDirection,
		float MaxDistanceCm, FPlanetEditTarget& OutTarget) const;

	FVector ResolveOwnerCameraLocation() const;
	FRotator ResolveOwnerCameraRotation() const;

	void BeginInvokerFollow();
	void UpdateInvokerFollow();
	void EndInvokerFollow();

	void ApplyStarMapEditSettings();
	void RestoreEditSettings();

	/** 焦点天体；进场与换选中都用它。 */
	void FocusOn(AActor* Body, double DistanceCm);

	bool bActive = false;
	bool bExiting = false;
	float ExitElapsedSeconds = 0.f;

	// ── 运行时状态（原相机字段，原样搬）──
	FRotator OrbitRotation = FRotator::ZeroRotator;
	FRotator AimOffset = FRotator::ZeroRotator;
	double OrbitDistanceCm = 0.0;
	bool bSmoothFocusActive = false;
	double SmoothFocusStartCm = 0.0;
	double SmoothFocusTargetCm = 0.0;
	float SmoothFocusElapsedSeconds = 0.f;
	FVector CameraLocation = FVector::ZeroVector;
	FRotator LastViewRotation = FRotator::ZeroRotator;
	TWeakObjectPtr<AActor> LastSeenSelected;

	// ── 退图过渡 ──
	FVector ExitStartLocation = FVector::ZeroVector;
	FRotator ExitStartRotation = FRotator::ZeroRotator;

	// ── 体素 invoker 跟随（只搬 bUseForCollisions == false 的）──
	struct FInvokerSnapshot
	{
		TObjectPtr<USceneComponent> Component = nullptr;
		FTransform RelativeTransform = FTransform::Identity;
	};
	TArray<FInvokerSnapshot> FollowedInvokers;

	// ── 编辑组件参数快照 ──
	struct FEditSettingsSnapshot
	{
		bool bValid = false;
		int32 MinSize = 1;
		int32 MaxSize = 8;
		int32 Size = 1;
		int32 Step = 1;
		float MaxDistanceCm = 1.0e5f;
		bool bMultiThreaded = false;
		FString PlaceHint;
	};
	FEditSettingsSnapshot SavedEditSettings;

	// ── 可调参数（原相机 UPROPERTY，原样搬）──
	UPROPERTY(EditAnywhere, Category = "StarMap")
	double ZoomStepFactor = 1.15;

	UPROPERTY(EditAnywhere, Category = "StarMap", meta = (ClampMin = "0.0"))
	double PlanetFocusRadiusScale = 3.0;

	UPROPERTY(EditAnywhere, Category = "StarMap", meta = (ClampMin = "0.0"))
	float SelectionPixelRadius = 20.f;

	UPROPERTY(EditAnywhere, Category = "StarMap", meta = (ClampMin = "0.0"))
	double MinOrbitLookScale = 0.05;

	UPROPERTY(EditAnywhere, Category = "StarMap", meta = (ClampMin = "1.0"))
	double MaxOrbitLookScale = 2.0;

	UPROPERTY(EditAnywhere, Category = "StarMap", meta = (ClampMin = "0.0", ClampMax = "180.0"))
	double MaxAimOffsetDeg = 90.0;

	UPROPERTY(EditAnywhere, Category = "StarMap")
	double MinSurfaceAltitudeCm = 5.0e3;

	UPROPERTY(EditAnywhere, Category = "StarMap")
	double MinFocusDistanceCm = 5.0e2;

	UPROPERTY(EditAnywhere, Category = "StarMap")
	double MaxOrbitDistanceCm = 1.0e9;

	UPROPERTY(EditAnywhere, Category = "StarMap", meta = (ClampMin = "0.01"))
	float SmoothFocusSeconds = 0.8f;

	UPROPERTY(EditAnywhere, Category = "StarMap", meta = (ClampMin = "0.0"))
	float ExitBlendSeconds = 0.5f;
};
```

- [ ] **Step 2: 写实现——先搬纯函数与数学**

`StarMapRigComponent.cpp` 上半部分：把下列函数**整段复制**到新文件，只把 `AStarMapCameraPawn::` 换成 `UStarMapRigComponent::`，注释一字不改：

| 目标函数 | 源文件行（`StarMapCameraPawn.cpp`） |
|---|---|
| `FocusOn` | 341–382 |
| `OrbitLook` | 383–401 |
| `AimLook` | 402–410 |
| `ClampAimOffset` | 411–462 |
| `OrbitLookScale` | 463–499 |
| `AddZoom` | 500–529 |
| `ZoomAltitudeCm` | 530–545 |
| `RaySphereFirstHitCm` | 546–555 |
| `LimitDistanceByObstaclesCm` | 556–581 |
| `ResolveFocusSurfaceRadiusCm` | 582–600 |
| `ComputeOrbitDistanceLimitsCm` | 601–670 |
| `ResolveEditTargetByPlanets` | 671–729 |
| `ResolveSelectedBody` | 730–738 |
| `PickBodyAtScreenPosition` | 832–868 |
| `SilhouetteRadiusScale` | 952–962 |

**不要搬**：`EnterStarMap`、`ExitStarMap`、`FinishStarMapExit`、`EnterFrom`、`ComputeSurfaceAnchorLocation`、`ComputeEntryAnchorLocation`、`ResetToAttachOrigin`、`HasStarMapCamera`、`ResolveReferencePlanet`、`FindChildStarMapCamera`、`FindAttachedStarMapCamera`、`ResolvePlanetStarMapCamera`、`SetViewpointInvokersActive`、`ApplyViewpointInvokerState`。

需要的 include：`Camera/CameraComponent.h`、`CosmosSimulationSubsystem.h`、`Engine/World.h`、`GameFramework/PlayerController.h`、`PlanetSurfaceRay.h`、`PlanetEditComponent.h`、`StarMapPicking.h`、`StarMapRigComponent.h`、`VoxelPlanetActor.h`、`VoxelComponents/VoxelInvokerComponent.h`（Task 4 之后输入头文件叫 `StarMapInputComponent.h`）。

- [ ] **Step 3: 写实现——新外壳**

```cpp
UStarMapRigComponent::UStarMapRigComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UStarMapRigComponent::BeginPlay()
{
	Super::BeginPlay();

	// 飞船没有自己的解析器（角色在 BeginPlay 里装自己的）；进图时会被星图版覆盖，这里只兜底。
	if (UPlanetEditComponent* Edit = GetPlanetEditComponent())
	{
		Edit->SetTargetResolver(
			[this](const FVector& RayOrigin, const FVector& RayDirection,
				const float MaxDistanceCm, FPlanetEditTarget& OutTarget)
			{
				return ResolveEditTargetByPlanets(RayOrigin, RayDirection, MaxDistanceCm, OutTarget);
			});
	}
}

void UStarMapRigComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UPlanetEditComponent* Edit = GetPlanetEditComponent())
	{
		Edit->SetTargetResolver(nullptr);
	}
	EndInvokerFollow();
	Super::EndPlay(EndPlayReason);
}

UPlanetEditComponent* UStarMapRigComponent::GetPlanetEditComponent() const
{
	const AActor* Owner = GetOwner();
	return Owner ? Owner->FindComponentByClass<UPlanetEditComponent>() : nullptr;
}

FVector UStarMapRigComponent::ResolveOwnerCameraLocation() const
{
	const AActor* Owner = GetOwner();
	const UCameraComponent* Camera = Owner ? Owner->FindComponentByClass<UCameraComponent>() : nullptr;
	return Camera ? Camera->GetComponentLocation() : CameraLocation;
}

FRotator UStarMapRigComponent::ResolveOwnerCameraRotation() const
{
	const AActor* Owner = GetOwner();
	const UCameraComponent* Camera = Owner ? Owner->FindComponentByClass<UCameraComponent>() : nullptr;
	return Camera ? Camera->GetComponentRotation() : LastViewRotation;
}

AVoxelPlanetActor* UStarMapRigComponent::ResolveFocusPlanet() const
{
	const APawn* OwnerPawn = Cast<APawn>(GetOwner());
	if (!OwnerPawn)
	{
		return nullptr;
	}
	// 两个 Pawn 都逐帧维护「最近行星」，别在这里重算距离。
	if (const ASpaceshipPawn* Ship = Cast<ASpaceshipPawn>(OwnerPawn))
	{
		return Ship->GetPlanetActor();
	}
	if (const APlanetCharacter* Character = Cast<APlanetCharacter>(OwnerPawn))
	{
		return Character->PlanetActor;
	}
	return nullptr;
}

void UStarMapRigComponent::Toggle(APlayerController* PC)
{
	if (bExiting)
	{
		return;   // 退图过渡期间按 M 忽略，否则会把整段流程重跑一遍
	}
	if (bActive)
	{
		Exit();
	}
	else
	{
		Enter(PC);
	}
}

void UStarMapRigComponent::Enter(APlayerController* PC)
{
	UCosmosSimulationSubsystem* Subsystem =
		GetWorld() ? GetWorld()->GetSubsystem<UCosmosSimulationSubsystem>() : nullptr;
	AVoxelPlanetActor* Focus = ResolveFocusPlanet();
	if (!PC || !Subsystem || !Focus)
	{
		return;
	}

	FVector ViewLocation;
	FRotator ViewRotation;
	PC->GetPlayerViewPoint(ViewLocation, ViewRotation);

	// 星图里左键归拾取：先退出编辑模式。
	if (UPlanetEditComponent* Edit = GetPlanetEditComponent())
	{
		if (Edit->IsEditMode())
		{
			Edit->SetEditMode(false);
		}
	}

	CameraLocation = ViewLocation;
	OrbitRotation = ViewRotation;
	LastViewRotation = ViewRotation;
	AimOffset = FRotator::ZeroRotator;
	LastSeenSelected = nullptr;
	bSmoothFocusActive = false;
	bActive = true;
	bExiting = false;

	Subsystem->SetSelectedBody(nullptr);
	Subsystem->SetStarMapActive(true);
	Subsystem->SetSelectedBody(Focus);

	FocusOn(Focus, PlanetFocusRadiusScale * StarMapPicking::GetBodyRadiusCm(Focus));

	ApplyStarMapEditSettings();
	if (UPlanetEditComponent* Edit = GetPlanetEditComponent())
	{
		Edit->SetTargetResolver(
			[this](const FVector& RayOrigin, const FVector& RayDirection,
				const float MaxDistanceCm, FPlanetEditTarget& OutTarget)
			{
				return ResolveEditTargetByPlanets(RayOrigin, RayDirection, MaxDistanceCm, OutTarget);
			});
	}

	BeginInvokerFollow();
}

void UStarMapRigComponent::Exit()
{
	if (!bActive || bExiting)
	{
		return;
	}

	if (UCosmosSimulationSubsystem* Subsystem =
		GetWorld() ? GetWorld()->GetSubsystem<UCosmosSimulationSubsystem>() : nullptr)
	{
		Subsystem->SetSelectedBody(nullptr);
		Subsystem->SetStarMapActive(false);
	}

	if (UPlanetEditComponent* Edit = GetPlanetEditComponent())
	{
		if (Edit->IsEditMode())
		{
			Edit->SetEditMode(false);
		}
	}
	// 角色的解析器按「脚下这颗」取样，退图必须还回去；飞船只在星图里编辑，留着星图版即可。
	if (APlanetCharacter* Character = Cast<APlanetCharacter>(GetOwner()))
	{
		Character->InstallEditTargetResolver();
	}
	RestoreEditSettings();
	EndInvokerFollow();

	ExitStartLocation = CameraLocation;
	ExitStartRotation = LastViewRotation;
	ExitElapsedSeconds = 0.f;
	bExiting = true;
}

void UStarMapRigComponent::CalcView(float DeltaSeconds, FMinimalViewInfo& OutResult)
{
	if (bExiting)
	{
		ExitElapsedSeconds += DeltaSeconds;
		const double Alpha = FMath::Clamp(
			double(ExitElapsedSeconds) / double(FMath::Max(ExitBlendSeconds, 0.01f)), 0.0, 1.0);
		const double Eased = Alpha * Alpha * (3.0 - 2.0 * Alpha);

		OutResult.Location = FMath::Lerp(ExitStartLocation, ResolveOwnerCameraLocation(), Eased);
		OutResult.Rotation = FQuat::Slerp(
			ExitStartRotation.Quaternion(),
			ResolveOwnerCameraRotation().Quaternion(), Eased).Rotator();

		if (Alpha >= 1.0)
		{
			bExiting = false;
			bActive = false;
		}
		return;
	}

	// ↓ 原 AStarMapCameraPawn::CalcCamera 主体（源文件 739–830），三处改动：
	//   1. 删掉开头的 Super::CalcCamera（owner 已调过）；
	//   2. 删掉「SetActorLocation(CameraLocation)」整块（invoker 改为显式搬）；
	//   3. 末尾补 UpdateInvokerFollow() 与 LastViewRotation = OutResult.Rotation。
	...
	UpdateInvokerFollow();
	LastViewRotation = OutResult.Rotation;
}
```

- [ ] **Step 4: 写实现——invoker 跟随与编辑参数快照**

```cpp
void UStarMapRigComponent::BeginInvokerFollow()
{
	FollowedInvokers.Reset();

	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return;
	}

	// 碰撞气泡必须留在原地：角色靠它站在地上，飞船靠它判落地。
	TArray<UVoxelSimpleInvokerComponent*> Invokers;
	Owner->GetComponents<UVoxelSimpleInvokerComponent>(Invokers);
	for (UVoxelSimpleInvokerComponent* Invoker : Invokers)
	{
		if (!Invoker || Invoker->bUseForCollisions)
		{
			continue;
		}
		FInvokerSnapshot& Snapshot = FollowedInvokers.AddDefaulted_GetRef();
		Snapshot.Component = Invoker;
		Snapshot.RelativeTransform = Invoker->GetRelativeTransform();
	}
}

void UStarMapRigComponent::UpdateInvokerFollow()
{
	for (const FInvokerSnapshot& Snapshot : FollowedInvokers)
	{
		if (Snapshot.Component)
		{
			Snapshot.Component->SetWorldLocation(CameraLocation);
		}
	}
}

void UStarMapRigComponent::EndInvokerFollow()
{
	for (const FInvokerSnapshot& Snapshot : FollowedInvokers)
	{
		if (Snapshot.Component)
		{
			Snapshot.Component->SetRelativeTransform(Snapshot.RelativeTransform);
		}
	}
	FollowedInvokers.Reset();
}

void UStarMapRigComponent::ApplyStarMapEditSettings()
{
	UPlanetEditComponent* Edit = GetPlanetEditComponent();
	if (!Edit)
	{
		return;
	}

	SavedEditSettings.bValid = true;
	SavedEditSettings.MinSize = Edit->MinBrushSizeVoxels;
	SavedEditSettings.MaxSize = Edit->MaxBrushSizeVoxels;
	SavedEditSettings.Size = Edit->GetBrushSizeVoxels();
	SavedEditSettings.Step = Edit->BrushSizeStepVoxels;
	SavedEditSettings.MaxDistanceCm = Edit->MaxEditDistanceCm;
	SavedEditSettings.bMultiThreaded = Edit->bMultiThreadedEdits;
	SavedEditSettings.PlaceHint = Edit->PlaceActionHint;

	Edit->ConfigureBrushRange(8, 1280, 64);
	Edit->BrushSizeStepVoxels = 10;
	Edit->MaxEditDistanceCm = 5.0e7f;
	Edit->bMultiThreadedEdits = true;
	Edit->PlaceActionHint = TEXT("Shift+左键按住连续挖 · Ctrl+左键填");
}

void UStarMapRigComponent::RestoreEditSettings()
{
	UPlanetEditComponent* Edit = GetPlanetEditComponent();
	if (!Edit || !SavedEditSettings.bValid)
	{
		return;
	}

	Edit->ConfigureBrushRange(
		SavedEditSettings.MinSize, SavedEditSettings.MaxSize, SavedEditSettings.Size);
	Edit->BrushSizeStepVoxels = SavedEditSettings.Step;
	Edit->MaxEditDistanceCm = SavedEditSettings.MaxDistanceCm;
	Edit->bMultiThreadedEdits = SavedEditSettings.bMultiThreaded;
	Edit->PlaceActionHint = SavedEditSettings.PlaceHint;
	SavedEditSettings = FEditSettingsSnapshot();
}
```

- [ ] **Step 5: 加 BOM 并构建**

对两个新文件跑 Global Constraints 的 BOM 脚本，然后执行构建命令。预期：编译通过（旧相机与测试全部未动，只是多了两个新文件）。

- [ ] **Step 6: 提交**

```bash
git add Source/StellarEraPawns/Src/StarMap/StarMapRigComponent.h Source/StellarEraPawns/Src/StarMap/StarMapRigComponent.cpp
git commit -m "feat(星图): 新增 UStarMapRigComponent（搬自星图相机）

Co-Authored-By: Claude Code <noreply@anthropic.com>" -- Source/StellarEraPawns/Src/StarMap/StarMapRigComponent.h Source/StellarEraPawns/Src/StarMap/StarMapRigComponent.cpp
```

---

### Task 2: 测试迁移到 rig

**Files:**
- Modify: `Source/StellarEra/Src/Tests/StarMapTests.cpp`
- Modify: `Source/StellarEra/Src/Tests/VoxelViewpointInvokerTests.cpp`

**Interfaces:**
- Consumes: Task 1 的 `UStarMapRigComponent` 全部 static。

- [ ] **Step 1: 批量替换静态函数宿主**

```bash
sed -i 's/AStarMapCameraPawn::/UStarMapRigComponent::/g' Source/StellarEra/Src/Tests/StarMapTests.cpp
```

include 增加 `StarMapRigComponent.h`。这些是纯函数，替换后行为不变。

- [ ] **Step 2: 删除被移除功能的用例**

删掉整段自动化测试（从其 `IMPLEMENT_SIMPLE_AUTOMATION_TEST` 到下一个 `IMPLEMENT_SIMPLE_AUTOMATION_TEST` 之前）：

1. `FStellarEraStarMapFocusTargetTest`（`StellarEra.StarMap.FocusTarget`）——只测 `HasStarMapCamera`。
2. `FStellarEraStarMapPlanetCameraTest`（`StellarEra.StarMap.PlanetCamera`）——CAC / 附着解析。
3. `FStellarEraStarMapEntryAnchorTest`（`StellarEra.StarMap.EntryAnchor`）——`ComputeSurfaceAnchorLocation` 已不搬。

`VoxelViewpointInvokerTests.cpp`：删掉 `FStellarEraInvokerViewpointStarMapTest` 里星图相机 spawn 与「星图相机的调用器个数」断言；第二个用例里 `Camera` spawn、`CameraInvokers` 与相关断言一并删掉，保留飞船 / 角色 / 普通 Pawn 三条。

- [ ] **Step 3: 修剩余的蓝图路径**

`StarMapTests.cpp` 里 `TEXT("/Game/Code/Pawns/BP_StarMapCameraPawn.BP_StarMapCameraPawn")` → `TEXT("/Game/Code/Pawns/BP_SpaceshipPawn.BP_SpaceshipPawn")`（`BP_SpaceshipPawn` 的编辑组件在 Task 7 才加）。同时把「笔刷区间 8..1280 / 64」这几条硬编码断言改成不钉具体数字的版本（只断言 `Size` 在区间内、`MaxEditDistanceCm > 1e7`、`bMultiThreadedEdits`），因为星图参数现在由 rig 在进图时注入，蓝图组件本身用 C++ 默认值。

- [ ] **Step 4: 构建并跑测试**

先构建（Global Constraints），再跑测试命令。预期：
- `FocusTarget` / `PlanetCamera` / `EntryAnchor` 不再出现在日志。
- 其余 `StellarEra.StarMap.*` 全绿。
- `EditComponentDefaults` / `BlueprintPawn` 会失败（飞船蓝图还没有编辑组件）——预期，Task 7 转绿，提交信息注明。

- [ ] **Step 5: 提交**

```bash
git add Source/StellarEra/Src/Tests/StarMapTests.cpp Source/StellarEra/Src/Tests/VoxelViewpointInvokerTests.cpp
git commit -m "test(星图): 纯函数断言改挂 UStarMapRigComponent

Co-Authored-By: Claude Code <noreply@anthropic.com>" -- Source/StellarEra/Src/Tests/StarMapTests.cpp Source/StellarEra/Src/Tests/VoxelViewpointInvokerTests.cpp
```

---

### Task 3: Pawn 接线（rig / 输入组件成员、CalcCamera、M 收口、输入互斥）

**Files:**
- Modify: `Source/StellarEraPawns/Src/Spaceship/SpaceshipPawn.h`、`SpaceshipPawn.cpp`
- Modify: `Source/StellarEraPawns/Src/Character/PlanetCharacter.h`、`PlanetCharacter.cpp`
- Modify: `Source/StellarEraPawns/Src/Spaceship/Input/SpaceshipInputComponent.cpp`
- Modify: `Source/StellarEraPawns/Src/Character/Input/PlanetCharacterInputComponent.cpp`

**Interfaces:**
- Consumes: `UStarMapRigComponent::IsActive()` / `Toggle(PC)` / `CalcView` / `GetPlanetEditComponent`（Task 1）。
- Produces:
  - `ASpaceshipPawn::IsStarMapActive() const`、`APlanetCharacter::IsStarMapActive() const`
  - `APlanetCharacter::InstallEditTargetResolver()`
  - 两个 Pawn 的 `OnStarMapToggle` 与其 M 绑定被删除（M 只在星图输入组件绑一次）

- [ ] **Step 1: 两个 Pawn 加组件成员与 IsStarMapActive**

`SpaceshipPawn.h`（放在 Camera 相关成员旁）：

```cpp
	/** 星图 rig：进图后接管本 Pawn 的相机。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "StarMap")
	TObjectPtr<UStarMapRigComponent> StarMapRig;

	/** 星图的键鼠输入；未激活时组件自身返回空 Pawn，全部回调空转。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "StarMap")
	TObjectPtr<UStarMapCameraInputComponent> StarMapInput;

	/** 星图 rig 是否正在接管视点（含退图过渡）。 */
	bool IsStarMapActive() const;
```

`PlanetCharacter.h` 同样三行（加在 `Camera` 成员后）。两个头文件都加前置声明 `class UStarMapRigComponent; class UStarMapCameraInputComponent;`。

两个 `.cpp` 构造函数里：

```cpp
	StarMapRig = CreateDefaultSubobject<UStarMapRigComponent>(TEXT("StarMapRig"));
	StarMapInput = CreateDefaultSubobject<UStarMapCameraInputComponent>(TEXT("StarMapInput"));
```

`IsStarMapActive`：

```cpp
bool ASpaceshipPawn::IsStarMapActive() const
{
	return StarMapRig && StarMapRig->IsActive();
}
```

（`APlanetCharacter` 同样。）

- [ ] **Step 2: CalcCamera 分支**

`ASpaceshipPawn::CalcCamera`：在 `Super::CalcCamera(DeltaSeconds, OutResult);` 之后立刻插入：

```cpp
	if (IsStarMapActive())
	{
		StarMapRig->CalcView(DeltaSeconds, OutResult);
		return;   // 星图期间不做机身横滚滞后
	}
```

`APlanetCharacter` 新增 override（`.h` 加 `virtual void CalcCamera(float DeltaSeconds, FMinimalViewInfo& OutResult) override;`）：

```cpp
void APlanetCharacter::CalcCamera(float DeltaSeconds, FMinimalViewInfo& OutResult)
{
	Super::CalcCamera(DeltaSeconds, OutResult);

	if (IsStarMapActive())
	{
		StarMapRig->CalcView(DeltaSeconds, OutResult);
	}
}
```

- [ ] **Step 3: SetupPlayerInputComponent 多绑一个输入组件**

两个 Pawn 的 `SetupPlayerInputComponent` 里，`PlayerInput->Bind(...)` 之后追加：

```cpp
	if (StarMapInput)
	{
		StarMapInput->Bind(PlayerInputComponent);
	}
```

- [ ] **Step 4: 输入互斥 + 删掉 Pawn 自己的 M 绑定**

`USpaceshipInputComponent::GetPawn()`：

```cpp
ASpaceshipPawn* USpaceshipInputComponent::GetPawn() const
{
	ASpaceshipPawn* OwnerPawn = Cast<ASpaceshipPawn>(GetOwner());
	// 星图期间飞船输入全停（M 由星图输入组件接），与旧版 Possess 换人等价。
	return (OwnerPawn && !OwnerPawn->IsStarMapActive()) ? OwnerPawn : nullptr;
}
```

`UPlanetCharacterInputComponent::GetPawn()` 同样处理。

删掉两个组件里 `OnStarMapToggle` 的实现与 `Bind` 里的 `StarMapToggle` 绑定行；若 `#include "SessionCommands.h"` 因此不再被使用则一并删除（`IStellarEraSessionCommands` 的其它方法仍可能被用，先编译再定）。

- [ ] **Step 5: 角色暴露解析器安装方法**

`PlanetCharacter.cpp` 的 `BeginPlay` 里那段 `Edit->SetTargetResolver([this](...){ TraceTargetForCharacter(...); })` 提成 public 方法：

```cpp
void APlanetCharacter::InstallEditTargetResolver()
{
	if (UPlanetEditComponent* Edit = GetPlanetEditComponent())
	{
		Edit->SetTargetResolver(
			[this](const FVector& RayOrigin, const FVector& RayDirection,
				const float MaxDistanceCm, FPlanetEditTarget& OutTarget)
			{
				return TraceTargetForCharacter(
					*this, PlanetActor.Get(), RayOrigin, RayDirection, MaxDistanceCm, OutTarget);
			});
	}
}
```

`BeginPlay` 改为调用它（保留原注释的时序理由）。

- [ ] **Step 6: 构建**

执行构建命令。预期通过：`UStarMapCameraInputComponent` 仍是旧实现，两个 Pawn 只是持有它。

- [ ] **Step 7: 提交**

```bash
git add Source/StellarEraPawns/Src/Spaceship Source/StellarEraPawns/Src/Character
git commit -m "feat(星图): 飞船与角色挂 rig 与星图输入

Co-Authored-By: Claude Code <noreply@anthropic.com>" -- Source/StellarEraPawns/Src/Spaceship Source/StellarEraPawns/Src/Character
```

---

### Task 4: 星图输入组件改挂 rig 并改名

**Files:**
- Rename: `Source/StellarEraPawns/Src/StarMap/Input/StarMapCameraInputComponent.h` → `StarMapInputComponent.h`
- Rename: `.../StarMapCameraInputComponent.cpp` → `StarMapInputComponent.cpp`
- Modify: `SpaceshipPawn.h/.cpp`、`PlanetCharacter.h/.cpp`（成员类型与 include）
- Modify: `Source/StellarEra/Src/Tests/StarMapTests.cpp`（`UStarMapCameraInputComponent::ResolveLookMode` → `UStarMapInputComponent::`）

**Interfaces:**
- Produces: `class UStarMapInputComponent`；`APawn* GetPawn() const`（未激活时为 null）；`UStarMapRigComponent* GetRig() const`。

- [ ] **Step 1: 重命名文件与类**

```bash
git mv Source/StellarEraPawns/Src/StarMap/Input/StarMapCameraInputComponent.h Source/StellarEraPawns/Src/StarMap/Input/StarMapInputComponent.h
git mv Source/StellarEraPawns/Src/StarMap/Input/StarMapCameraInputComponent.cpp Source/StellarEraPawns/Src/StarMap/Input/StarMapInputComponent.cpp
```

文件内 `UStarMapCameraInputComponent` → `UStarMapInputComponent`，`#include "...generated.h"` 改名。

- [ ] **Step 2: 换 Pawn 取用方式**

头文件：

```cpp
class UStarMapRigComponent;

	/** 所属 Pawn；只在星图激活时返回，否则全部回调空转。 */
	APawn* GetPawn() const;

	/** owner 上的星图 rig。 */
	UStarMapRigComponent* GetRig() const;
```

实现：

```cpp
APawn* UStarMapInputComponent::GetPawn() const
{
	APawn* OwnerPawn = Cast<APawn>(GetOwner());
	const UStarMapRigComponent* Rig = GetRig();
	return (OwnerPawn && Rig && Rig->IsActive()) ? OwnerPawn : nullptr;
}

UStarMapRigComponent* UStarMapInputComponent::GetRig() const
{
	const AActor* Owner = GetOwner();
	return Owner ? Owner->FindComponentByClass<UStarMapRigComponent>() : nullptr;
}

APlayerController* UStarMapInputComponent::GetPlayerController() const
{
	const APawn* OwnerPawn = Cast<APawn>(GetOwner());
	return OwnerPawn ? Cast<APlayerController>(OwnerPawn->GetController()) : nullptr;
}
```

`Bind` 的守卫：

```cpp
	APawn* OwnerPawn = Cast<APawn>(GetOwner());
	UStarMapRigComponent* Rig = GetRig();
	UEnhancedInputComponent* EnhancedInput = Cast<UEnhancedInputComponent>(PlayerInputComponent);
	IStellarEraInputSource* Source =
		OwnerPawn ? Cast<IStellarEraInputSource>(OwnerPawn->GetController()) : nullptr;
	if (!OwnerPawn || !Rig || !EnhancedInput || !Source)
	{
		UE_LOG(LogTemp, Error, TEXT("[Input] 星图输入绑定失败：Pawn=%s Rig=%s EnhancedInput=%s InputSource=%s"),
			*GetNameSafe(OwnerPawn), Rig ? TEXT("是") : TEXT("否"),
			EnhancedInput ? TEXT("是") : TEXT("否"), Source ? TEXT("是") : TEXT("否"));
		return;
	}
```

- [ ] **Step 3: 回调改调 rig**

逐个替换：

| 原 | 新 |
|---|---|
| `Pawn->OrbitLook(...)` | `Rig->OrbitLook(...)` |
| `Pawn->AimLook(...)` | `Rig->AimLook(...)` |
| `Pawn->AddZoom(v)` | `Rig->AddZoom(v)` |
| `Pawn->GetSelectionPixelRadius()` | `Rig->GetSelectionPixelRadius()` |
| `Pawn->GetPlanetEditComponent()` | `Rig->GetPlanetEditComponent()` |
| `Pawn->GetController()`（Ctrl 粗调判定） | `OwnerPawn->GetController()` |

各回调开头的守卫统一为：

```cpp
	UStarMapRigComponent* Rig = GetRig();
	if (!GetPawn() || !Rig)
	{
		return;
	}
```

`OnStarMapSelectCompleted` 的编辑分支仍最先执行，保持不变。

`GetEditComponentInEditMode()`：

```cpp
UPlanetEditComponent* UStarMapInputComponent::GetEditComponentInEditMode() const
{
	const UStarMapRigComponent* Rig = GetRig();
	UPlanetEditComponent* Edit = Rig ? Rig->GetPlanetEditComponent() : nullptr;
	return Edit && Edit->IsEditMode() ? Edit : nullptr;
}
```

- [ ] **Step 4: 进出图改用 Toggle**

```cpp
void UStarMapInputComponent::OnStarMapToggle(const FInputActionValue& Value)
{
	// 进退都走这一个入口：M 只在本组件绑了一次（飞船 / 角色自己的那份已删）。
	if (UStarMapRigComponent* Rig = GetRig())
	{
		Rig->Toggle(GetPlayerController());
	}
}
```

- [ ] **Step 5: 修 include 与测试**

`SpaceshipPawn.h/.cpp`、`PlanetCharacter.h/.cpp` 的 include 与成员类型改名；`StarMapTests.cpp` 的 `ResolveLookMode` 宿主改名，include 同步。

- [ ] **Step 6: 构建 + 跑星图测试**

预期：构建通过；`StellarEra.StarMap.LookDispatch` 等纯函数用例全绿。此时 `AStarMapCameraPawn` 已无人调用 `EnterStarMap`/`ExitStarMap`，但类仍存在，不报错。

- [ ] **Step 7: 提交**

```bash
git add Source/StellarEraPawns/Src/StarMap/Input Source/StellarEraPawns/Src/Spaceship Source/StellarEraPawns/Src/Character Source/StellarEra/Src/Tests/StarMapTests.cpp
git commit -m "refactor(星图): 输入组件改挂 rig 并改名 UStarMapInputComponent

Co-Authored-By: Claude Code <noreply@anthropic.com>" -- Source/StellarEraPawns/Src/StarMap/Input Source/StellarEraPawns/Src/Spaceship Source/StellarEraPawns/Src/Character Source/StellarEra/Src/Tests/StarMapTests.cpp
```

---

### Task 5: HUD / 星图 UI / 控制器的判据换成 `IsStarMapActive()`

**Files:**
- Modify: `Source/StellarEraUI/Src/StellarEraHUD.cpp`
- Modify: `Source/StellarEraUI/Src/StarMapUISystem.cpp`
- Modify: `Source/StellarEra/Src/StellarEraPlayerController.cpp`、`.h`
- Modify: `Source/StellarEra/Src/StellarEraPlayerControllerBase.h/.cpp`
- Modify: `Source/StellarEraPawns/Src/SessionCommands.h`

**Interfaces:**
- Consumes: `UCosmosSimulationSubsystem::IsStarMapActive()`、`UStarMapRigComponent::GetSelectionPixelRadius()`。
- Produces: `IStellarEraSessionCommands` 不再有 `FindStarMapCameraFor`。

- [ ] **Step 1: HUD 判据**

`StellarEraHUD.cpp` 里「当前 Pawn 是星图相机」那段：

```cpp
	// 星图模式的信息面板：只看子系统标志，不再认 Pawn 类型。
	if (APlayerController* PlayerController = GetOwningPlayerController())
	{
		const UWorld* World = GetWorld();
		const UCosmosSimulationSubsystem* Cosmos =
			World ? World->GetSubsystem<UCosmosSimulationSubsystem>() : nullptr;
		if (Cosmos && Cosmos->IsStarMapActive())
		{
			if (!StarMapUI)
			{
				StarMapUI = NewObject<UStarMapUISystem>(this);
			}
			StarMapUI->Draw(*this, *PlayerController);
		}
	}
```

`GetViewedEditComponent` 改为：

```cpp
	const UPlanetEditComponent* Edit =
		ViewedPawn ? ViewedPawn->FindComponentByClass<UPlanetEditComponent>() : nullptr;
	return Edit && Edit->IsEditMode() ? Edit : nullptr;
```

删掉 `#include "StarMapCameraPawn.h"`。

- [ ] **Step 2: 星图 UI 的容差**

`StarMapUISystem.cpp`：

```cpp
	// 拾取容差住在星图 rig 上；不在星图 / 没挂 rig 时退回默认值。
	const APawn* ViewedPawn = PlayerController.GetPawn();
	const UStarMapRigComponent* Rig =
		ViewedPawn ? ViewedPawn->FindComponentByClass<UStarMapRigComponent>() : nullptr;
```

`Camera ? Camera->GetSelectionPixelRadius() : 20.f` → `Rig ? Rig->GetSelectionPixelRadius() : 20.f`；include 换成 `StarMapRigComponent.h`。

- [ ] **Step 3: 控制器鼠标模式与编辑组件查询**

`StellarEraPlayerController.cpp`：

```cpp
	const UStarMapRigComponent* Rig = InPawn ? InPawn->FindComponentByClass<UStarMapRigComponent>() : nullptr;
	const bool bStarMap = Rig && Rig->IsActive();
```

```cpp
UPlanetEditComponent* GetEditComponentForPawn(APawn* InPawn)
{
	// 角色与飞船都用组件查：谁是当前 Pawn 就查谁。
	return InPawn ? InPawn->FindComponentByClass<UPlanetEditComponent>() : nullptr;
}
```

- [ ] **Step 4: PlayerTick 跟随星图状态**

头文件加成员 `bool bAppliedStarMapActive = false;`；`PlayerTick` 开头（在角色 / 飞船分支之前）：

```cpp
	// 星图进出不再走 Possess，鼠标模式只能在这里跟上；只在状态变化那一帧重设。
	APawn* CurrentPawn = GetPawn();
	const UStarMapRigComponent* Rig = CurrentPawn
		? CurrentPawn->FindComponentByClass<UStarMapRigComponent>() : nullptr;
	const bool bStarMapActive = Rig && Rig->IsActive();
	if (bStarMapActive != bAppliedStarMapActive)
	{
		bAppliedStarMapActive = bStarMapActive;
		ApplyMouseModeForPawn(CurrentPawn);
	}
```

- [ ] **Step 5: 删除 `FindStarMapCameraFor`**

- `SessionCommands.h`：删方法声明与 `class AStarMapCameraPawn;`。
- `StellarEraPlayerControllerBase.h`：删 override 与前置声明。
- `StellarEraPlayerControllerBase.cpp`：删实现与 `#include "StarMapCameraPawn.h"`。

- [ ] **Step 6: 构建 + 跑星图测试**

- [ ] **Step 7: 提交**

```bash
git add Source/StellarEraUI/Src Source/StellarEra/Src Source/StellarEraPawns/Src/SessionCommands.h
git commit -m "refactor(星图): UI/HUD/控制器判据改用 IsStarMapActive

Co-Authored-By: Claude Code <noreply@anthropic.com>" -- Source/StellarEraUI/Src Source/StellarEra/Src Source/StellarEraPawns/Src/SessionCommands.h
```

---

### Task 6: 删 `AStarMapCameraPawn` 与整条解析链

**Files:**
- Delete: `Source/StellarEraPawns/Src/StarMap/StarMapCameraPawn.h/.cpp`
- Modify: `Source/StellarEraPawns/Src/StarMap/StarMapPicking.cpp`
- Modify: `Source/StellarEra/Src/Tests/StarMapInputWiringTests.cpp`、`StarMapTests.cpp`
- Modify（仅注释）: `Source/StellarEraCosmos/Src/CosmosSimulationSubsystem.h`、`Source/StellarEraPawns/Src/StarMap/Input/StarMapCameraInputActions.h`、`Source/StellarEraPlanet/Src/Invokers/VoxelViewpointInvokers.h`、`Source/StellarEraPlanet/Src/Edit/PlanetEditComponent.h`

- [ ] **Step 1: `StarMapPicking` 去掉相机过滤**

```cpp
#include "StarMapRigComponent.h"   // 原 StarMapCameraPawn.h

	// 原先只挑「带星图相机」的天体；相机不再挂在天体上，全部注册天体都可点。
	for (AActor* Body : Bodies)
	{
		FStarMapScreenCandidate Candidate;
		if (MakeScreenCandidate(PC, Body, Candidate))
		{
			Candidates.Add(Candidate);
		}
	}

	return UStarMapRigComponent::PickBodyAtScreenPosition(
		Candidates, ScreenPosition, PixelTolerance);
```

`MakeScreenCandidate` 里 `AStarMapCameraPawn::SilhouetteRadiusScale` → `UStarMapRigComponent::SilhouetteRadiusScale`。

- [ ] **Step 2: 删类文件**

```bash
git rm Source/StellarEraPawns/Src/StarMap/StarMapCameraPawn.h Source/StellarEraPawns/Src/StarMap/StarMapCameraPawn.cpp
```

- [ ] **Step 3: 修测试残留**

`StarMapInputWiringTests.cpp`：把「Spawn 星图相机 + `Camera->SetupPlayerInputComponent`」改成「Spawn `ASpaceshipPawn` + 它的 `SetupPlayerInputComponent`」；总数断言先用失败信息里的实际值回填（飞船输入 + 星图输入之和），星图 8 个动作的逐项计数断言保持不变。include 换 `SpaceshipPawn.h`。

`StarMapTests.cpp`：include 换 `StarMapRigComponent.h`；`grep -rn AStarMapCameraPawn Source/` 必须为空。

- [ ] **Step 4: 清理注释里的旧名字**

- `CosmosSimulationSubsystem.h:111`：「由 AStarMapCameraPawn::EnterStarMap / ExitStarMap 调用」→「由 UStarMapRigComponent::Toggle 调用」。
- `StarMapCameraInputActions.h:2`：删掉「AStarMapCameraPawn 专属」。
- `VoxelViewpointInvokers.h`：「实现者是 Pawns 的三个 Pawn 与 UI 的星图相机」→「实现者是飞船与角色」。
- `PlanetEditComponent.h:3`：「角色与星图相机共用」→「角色与飞船的星图 rig 共用」。

- [ ] **Step 5: 构建 + 全量测试**

`Automation RunTests StellarEra`。预期：构建通过；除 Task 2 已知的两条蓝图用例（Task 7 补资产）外全绿。

- [ ] **Step 6: 提交**

```bash
git add Source/StellarEraPawns/Src/StarMap Source/StellarEra/Src/Tests Source/StellarEraCosmos/Src/CosmosSimulationSubsystem.h Source/StellarEraPlanet/Src/Invokers/VoxelViewpointInvokers.h Source/StellarEraPlanet/Src/Edit/PlanetEditComponent.h
git commit -m "refactor(星图): 删除 AStarMapCameraPawn 与行星挂相机解析链

Co-Authored-By: Claude Code <noreply@anthropic.com>" -- Source/StellarEraPawns/Src/StarMap Source/StellarEra/Src/Tests Source/StellarEraCosmos/Src/CosmosSimulationSubsystem.h Source/StellarEraPlanet/Src/Invokers/VoxelViewpointInvokers.h Source/StellarEraPlanet/Src/Edit/PlanetEditComponent.h
```

---

### Task 7: 资产清理（蓝图 / 地图 / 删相机 BP）

**Files:**
- Modify: `Content/Code/Pawns/BP_SpaceshipPawn.uasset`（加 `PlanetEditComponent`）
- Modify: `Content/Code/Planet/BP_VoxelPlanet.uasset`（若有 CAC 则删，先确认）
- Modify: `Content/Code/Maps/UniverseMap.umap`、`GeoPlanetMap.umap`、`GeoTestMap.umap`（删 CAC）
- Delete: `Content/Code/Pawns/BP_StarMapCameraPawn.uasset`

**Interfaces:**
- Consumes: 编辑器 MCP `execute_python_code`（端点 `http://127.0.0.1:8000/mcp`）。

- [ ] **Step 1: 删三张地图上的 CAC**

逐张 load → 删 → save（**不要**清空 Actor，更不要碰 `WorldDataLayers`）：

```python
import unreal

actors_subsys = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)

def strip_cacs():
    removed = 0
    for actor in actors_subsys.get_all_level_actors():
        for comp in actor.get_components_by_class(unreal.ChildActorComponent):
            cls = comp.get_editor_property("child_actor_class")
            name = cls.get_name() if cls else ""
            if "StarMapCameraPawn" in name:
                comp.destroy_component(actor)
                removed += 1
    print("removed", removed)

les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
for path in ["/Game/Code/Maps/UniverseMap", "/Game/Code/Maps/GeoPlanetMap", "/Game/Code/Maps/GeoTestMap"]:
    les.load_level(path)
    strip_cacs()
    les.save_current_level()
```

`BP_VoxelPlanet`：先只读确认它的 CDO / 实例上是否真有指向相机类的 CAC（`get_editor_property("child_actor_class")` 读到空类是类已删的正常表现，名字仍可读）；有则同样用 SCS 编辑删除，没有就跳过。

- [ ] **Step 2: 飞船蓝图加 PlanetEditComponent**

```python
import unreal
bp = unreal.load_asset("/Game/Code/Pawns/BP_SpaceshipPawn")
subsys = unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem)
root = subsys.k2_gather_subobject_data_for_blueprint(bp)[0]
params = unreal.AddNewSubobjectParams(parent_handle=root,
    new_class=unreal.PlanetEditComponent.static_class(), blueprint_context=bp)
handle, fail = subsys.add_new_subobject(params)
subsys.rename_subobject(handle, "PlanetEdit")
unreal.BlueprintEditorLibrary.compile_blueprint(bp)
unreal.EditorAssetLibrary.save_loaded_asset(bp)
```

- [ ] **Step 3: 删旧相机蓝图资产**

```python
import unreal
print(unreal.EditorAssetLibrary.delete_asset("/Game/Code/Pawns/BP_StarMapCameraPawn"))
```

- [ ] **Step 4: 验证**

- 三张地图 `get_all_level_actors()` 里没有 child 类名含 `StarMapCameraPawn` 的 CAC。
- `BP_SpaceshipPawn` CDO 上 `get_components_by_class(unreal.PlanetEditComponent)` 数为 1。
- 打开地图日志无 `World Memory Leaks`。

- [ ] **Step 5: 跑两条蓝图断言测试**

```bash
"D:/Application/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" \
  "F:/workspace/ue/StellarEra/StellarEra.uproject" \
  -ExecCmds="Automation RunTests StellarEra.StarMap.EditComponentDefaults+StellarEra.StarMap.BlueprintPawn" \
  -TestExit="Automation Test Queue Empty" \
  -unattended -nopause -nullrhi -nosplash -stdout -abslog="C:/Temp/starmap-bp.log"
```

预期两条全绿。

- [ ] **Step 6: 提交**

```bash
git add Content/Code/Pawns Content/Code/Planet/BP_VoxelPlanet.uasset Content/Code/Maps
git commit -m "chore(星图): 飞船蓝图带编辑组件、删行星上的星图相机 CAC 与 BP

Co-Authored-By: Claude Code <noreply@anthropic.com>" -- Content/Code/Pawns Content/Code/Planet/BP_VoxelPlanet.uasset Content/Code/Maps
```

---

### Task 8: 全量验证

**Files:** 无（只验证）。

- [ ] **Step 1: 构建**（关编辑器）

- [ ] **Step 2: 全量自动化测试**

`Automation RunTests StellarEra`，预期 0 失败。

- [ ] **Step 3: PIE @ UniverseMap（飞船）**

1. 按 M：相机从船尾平滑拉到行星 3R，无 Possess 闪烁；HUD 速度 / 位置连续。
2. 鼠标左键拖拽绕行、滚轮缩放、点选另一颗行星（相机飞过去）。
3. G 进编辑：笔刷面板出现、左键挖、Shift 连挖、`[`/`]` 调尺寸、Ctrl 粗调。
4. 按 M 退出：相机平滑回船尾；W 推力正常；E 离船正常。

- [ ] **Step 4: PIE @ UniverseMap（角色）**

下船后重复 Step 3 的 1/2/4；G 编辑时笔刷区间为 8..1280（进图注入），退出后回到 1..8。

- [ ] **Step 5: 日志检查**

PIE 日志无 `[Input] ... 绑定失败`、无 `VoxelDefaultRenderer` ensure、无 `World Memory Leaks`。

- [ ] **Step 6: 收尾**

`git status --short`，只提交本计划范围内的文件。
