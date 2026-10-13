# 行星角色与飞船 实现计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 在体素球体行星上实现《戴森球计划》伊卡洛斯式玩法：角色在球面行走、跳跃，走到飞船旁登船，飞船以街机手感在太空飞行，落地后离船继续行走。

**Architecture:** 五个 C++ 类，全部放在 `Source/StellarEra/`。球面行走不写任何球面物理——每帧把指向球心的方向喂给 `UCharacterMovementComponent::SetGravityDirection`，引擎自带的落地判定/斜坡/台阶/跳跃在"重力空间"里自动成立。太空飞行用 `UFloatingPawnMovement` 负责位移，街机手感（鼠标角速度 + 惯性衰减）写在 `Tick` 里。`AStellarEraPlayerController` 是唯一的编排者：构造 Enhanced Input 资产、按当前 Possess 的 Pawn 类型分派输入、管理登船/离船。

**Tech Stack:** Unreal Engine 5.8、Voxel Plugin Free、Enhanced Input、UMG（不用）

**Spec:** `docs/superpowers/specs/2026-10-13-planet-character-and-spaceship-design.md`

---

## Global Constraints

以下约束适用于本计划的**每一个任务**，不再逐条重复。

- **语言**：所有新增/修改的源文件注释用中文，与现有 `VoxelPlanetActor.h/.cpp` 风格一致。
- **UTF-8 BOM**：本项目没有 `Directory.Build.props`（已确认根目录不存在），无 BOM 的 UTF-8 中文源文件会被 VS IntelliSense 按 GBK 误读并报假错。**每个新建的 `.h` / `.cpp` 写完必须补 BOM**，命令见各任务的"补 BOM"步骤。现有 4 个源文件均以 `ef bb bf` 开头，是唯一的正确范例。
- **第三方插件目录只读**：任何自定义代码都不得写进 `Plugins/VoxelFree/`。只允许在 `Source/StellarEra/` 下新增，并在需要时往 `StellarEra.Build.cs` 加模块依赖。
- **构建方式**：编辑器**必须关闭**（Live Coding 会拒绝 UObject 布局变更），然后：
  ```bash
  powershell -NoProfile -File "C:/Users/23080/AppData/Local/Temp/ue_mcp/ue_build.ps1"
  ```
  该脚本用引擎自带的 dotnet 直调 `UnrealBuildTool.dll`，绕过 `Build.bat` 的锁问题。期望末尾出现 `Build succeeded` 且退出码为 0。
- **验证方式**：本项目没有测试框架，spec §9 已明确"本次不引入"。因此**每个任务的验证 = 构建通过 + PIE 里逐条肉眼核对**，每条验证都写成可证伪的观察点（"看到 X" / "日志里不出现 Y"），不写无法执行的假测试。
- **命名**：新类不加 `StellarEra` 前缀（除 GameMode/PlayerController，它们要与项目名区分）；文件名与类名一致，去掉前缀字母（`APlanetCharacter` → `PlanetCharacter.h`）。
- **提交信息**：中文，格式 `<type>: <描述>`，末尾附
  ```
  Co-Authored-By: Claude Code <noreply@anthropic.com>
  ```

---

## 关键引擎事实（写代码时不要推翻它们）

这些是读引擎源码确认过的，计划里的每一处设计都建立在它们之上。

1. **`UCharacterMovementComponent::SetGravityDirection(const FVector&)`** 是一等 API
   （`CharacterMovementComponent.h:1711`），幂等（`CharacterMovementComponent.cpp:4864` 相同值直接 return），可以每帧调用。
   它内部算 `WorldToGravityTransform = FQuat::FindBetweenNormals(FVector::UpVector, -NewGravityDir)`（`:4868`）。

2. **朝向对齐由引擎完成，不要手写。** `PhysicsRotation` 在 `bOrientRotationToMovement` 为真时，
   会把目标旋转换到重力空间、在那里清零俯仰与横滚，再换回世界空间
   （`CharacterMovementComponent.cpp:6673-6680`），随后用 `RotationRate` 插值（`:6711-6728`）。
   自己再 `RInterpTo` 一次朝向只会和它打架。**所以 `bOrientRotationToMovement = true`。**

3. **`ControlRotation` 不感知重力。** `APawn::GetViewRotation()` 直接返回 `Controller->GetControlRotation()`（`Pawn.cpp:360-363`），
   `AddControllerYawInput` 绕的是**世界 Z 轴**。在球体背面绕世界 Z 转会让画面翻滚。
   **所以不能用 `bUsePawnControlRotation` 的弹簧臂，必须自己做相机枢轴。**

4. **碰撞 Invoker**：`AVoxelPlanetActor` 的 Invoker 显式设了 `bUseForCollisions = false`
   （`VoxelPlanetActor.cpp:47`），所以目前整颗行星**没有任何碰撞体**。
   角色必须自带一个 `UVoxelSimpleInvokerComponent` 且 `bUseForCollisions = true`，脚下才会生成碰撞。
   （注意类名是 `UVoxelSimpleInvokerComponent`，不存在 `UVoxelInvokerComponent`。）
   体素碰撞体走 `FBodyInstance` 默认的 `BlockAll` 档案（`VoxelWorld.cpp:774` 载入档案数据），
   所有通道都被 Block，射线检测可用。

5. **`DefaultInputComponentClass` 在 `APawn` 上，不在 `APlayerController` 上。**
   `APlayerController::InitInputSystem` 用的是 `UInputSettings::GetDefaultInputComponentClass()`
   （`PlayerController.cpp:2654`），它读 `DefaultInput.ini`。本项目没有该文件，必须新建，
   否则 `Cast<UEnhancedInputComponent>(InputComponent)` 会失败。

---

## 文件结构

| 文件 | 职责 |
|---|---|
| `Source/StellarEra/Public/VoxelPlanetActor.h` | 改：加一个公开的球面半径查询 |
| `Source/StellarEra/Private/VoxelPlanetActor.cpp` | 改：同上 |
| `Source/StellarEra/Public/StellarEraGameMode.h` | 新：决定生成哪个 Pawn / PlayerController；补生成飞船 |
| `Source/StellarEra/Private/StellarEraGameMode.cpp` | 新：同上 |
| `Source/StellarEra/Public/PlanetCharacter.h` | 新：球面行走角色（重力、相机枢轴、贴合球面、登船接口） |
| `Source/StellarEra/Private/PlanetCharacter.cpp` | 新：同上 |
| `Source/StellarEra/Public/StellarEraPlayerController.h` | 新：构造输入资产、分派输入、编排登船/离船、提示 UI |
| `Source/StellarEra/Private/StellarEraPlayerController.cpp` | 新：同上 |
| `Source/StellarEra/Public/SpaceshipPawn.h` | 新：街机式太空飞船 |
| `Source/StellarEra/Private/SpaceshipPawn.cpp` | 新：同上 |
| `Config/DefaultEngine.ini` | 改：把 GlobalDefaultGameMode 指向本项目的 GameMode |
| `Config/DefaultInput.ini` | 新：把输入组件换成 Enhanced Input 版本 |

任务顺序即依赖顺序：Task 1 → 2 → 3 → 4 → 5，不可乱序。

---

## Task 1: 角色站上球面

让 PIE 里出现一个站在球面上的角色，脚下有碰撞体，不掉穿、不陷进去。

**Files:**
- Modify: `Source/StellarEra/Public/VoxelPlanetActor.h`
- Modify: `Source/StellarEra/Private/VoxelPlanetActor.cpp`
- Create: `Source/StellarEra/Public/StellarEraGameMode.h`
- Create: `Source/StellarEra/Private/StellarEraGameMode.cpp`
- Create: `Source/StellarEra/Public/PlanetCharacter.h`
- Create: `Source/StellarEra/Private/PlanetCharacter.cpp`
- Modify: `Config/DefaultEngine.ini`

**Interfaces:**
- Consumes: 无（本任务是起点）
- Produces:
  - `float AVoxelPlanetActor::GetSurfaceRadiusCm() const` —— 球面半径（厘米，含 Actor 缩放）
  - `class AStellarEraGameMode : public AGameModeBase`
  - `class APlanetCharacter : public ACharacter`，含成员 `TObjectPtr<AVoxelPlanetActor> PlanetActor`、`TObjectPtr<USceneComponent> CameraPivot`、`float CameraYaw`、`float CameraPitch`

---

- [ ] **Step 1: 给 `AVoxelPlanetActor` 加球面半径查询**

角色需要知道球面在世界单位下的半径，但 `GetScaledRadiusVoxels()` 是私有的且以体素计。

在 `Source/StellarEra/Public/VoxelPlanetActor.h` 的 `RebuildPlanet()` 声明**下面**插入：

```cpp
	/** 行星表面半径（厘米，含 Actor 等比缩放）。角色据此贴合球面。 */
	UFUNCTION(BlueprintPure, Category = "Planet")
	float GetSurfaceRadiusCm() const;
```

在 `Source/StellarEra/Private/VoxelPlanetActor.cpp` 的 `RebuildPlanet()` 函数体**之后**插入：

```cpp
float AVoxelPlanetActor::GetSurfaceRadiusCm() const
{
	// 半径以体素计，乘以单个体素的厘米尺寸才是世界单位。
	return GetScaledRadiusVoxels() * VoxelSize;
}
```

- [ ] **Step 2: 新建 `StellarEraGameMode`**

没有 GameMode，PIE 会生成引擎默认的 `ADefaultPawn`（一个旁观球），`APlanetCharacter` 永远不会被实例化。

新建 `Source/StellarEra/Public/StellarEraGameMode.h`：

```cpp
// Copyright Voxel Plugin SAS. All Rights Reserved.
// 游戏模式：决定 PIE 与打包时生成哪个 Pawn 与 PlayerController。

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "StellarEraGameMode.generated.h"

/**
 * 本项目的游戏模式。
 *
 * 没有它，PIE 会生成引擎默认的 ADefaultPawn（一个旁观球），角色类永远不会
 * 被实例化。DefaultEngine.ini 里的 GlobalDefaultGameMode 指向本类。
 */
UCLASS()
class AStellarEraGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AStellarEraGameMode();
};
```

新建 `Source/StellarEra/Private/StellarEraGameMode.cpp`：

```cpp
// Copyright Voxel Plugin SAS. All Rights Reserved.

#include "StellarEraGameMode.h"

#include "PlanetCharacter.h"

AStellarEraGameMode::AStellarEraGameMode()
{
	// 关卡里没有 PlayerStart，角色会生成在世界原点。APlanetCharacter 的
	// BeginPlay 会把自己摆到球面正上方，所以这里不需要额外处理出生点。
	DefaultPawnClass = APlanetCharacter::StaticClass();
}
```

- [ ] **Step 3: 新建 `APlanetCharacter`（本任务的骨架部分）**

新建 `Source/StellarEra/Public/PlanetCharacter.h`：

```cpp
// Copyright Voxel Plugin SAS. All Rights Reserved.
// 在体素球体行星表面行走的角色。

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "PlanetCharacter.generated.h"

class AVoxelPlanetActor;
class UCameraComponent;
class USpringArmComponent;
class UVoxelSimpleInvokerComponent;

/**
 * 在体素球体行星表面行走的角色。
 *
 * 球面行走没有手写任何物理：每帧把指向球心的方向喂给
 * UCharacterMovementComponent::SetGravityDirection，CMC 自带的落地判定、
 * 斜坡、台阶、跳跃就都在“重力空间”里成立了。
 */
UCLASS()
class APlanetCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	APlanetCharacter();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	/** 当前所在的行星。BeginPlay 时自动查找最近的一颗。 */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Planet")
	TObjectPtr<AVoxelPlanetActor> PlanetActor;

	/** 查找行星时的最大距离（厘米）。默认 1000 公里，够覆盖任意单颗行星。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet")
	float PlanetSearchRadius = 1.0e8f;

protected:
	/** 把角色摆到球面正上方，胶囊底贴住表面，并把朝向摆正。 */
	void SnapToPlanetSurface();

	/** 在关卡里找离角色最近的一颗行星；找不到返回 nullptr。 */
	AVoxelPlanetActor* FindNearestPlanet() const;

	/**
	 * 相机枢轴。每帧被设成“以球面法线为上、按 CameraYaw 绕该轴偏航”的世界旋转。
	 * 弹簧臂挂在它下面，于是相机天然切于球面——这是唯一可行的做法，因为
	 * ControlRotation 是世界空间的，在球体背面绕世界 Z 转会让画面翻滚。
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera")
	TObjectPtr<USceneComponent> CameraPivot;

	/** 第三人称相机臂。相对相机枢轴。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera")
	TObjectPtr<USpringArmComponent> SpringArm;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera")
	TObjectPtr<UCameraComponent> Camera;

	/**
	 * 体素 Invoker。行星自己的 Invoker 显式关掉了碰撞（见 VoxelPlanetActor.cpp），
	 * 所以脚下要生成碰撞体，必须由角色自己带一个开启碰撞的 Invoker。
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Planet")
	TObjectPtr<UVoxelSimpleInvokerComponent> VoxelInvoker;

	/** 相机臂长（厘米）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera")
	float CameraArmLength = 600.f;

	/** 相机水平角（度），绕球面法线。由鼠标输入驱动。 */
	float CameraYaw = 0.f;

	/** 相机俯仰角（度），相对球面切平面。由鼠标输入驱动。 */
	float CameraPitch = 0.f;

	/** 俯仰角下限（度）。 */
	UPROPERTY(EditAnywhere, Category = "Camera")
	float MinCameraPitch = -75.f;

	/** 俯仰角上限（度）。 */
	UPROPERTY(EditAnywhere, Category = "Camera")
	float MaxCameraPitch = 75.f;

private:
	/** 是否已经第一次踩到地面。在此之前每帧把角色按回球面，等碰撞体流送出来。 */
	bool bHasLanded = false;
};
```

新建 `Source/StellarEra/Private/PlanetCharacter.cpp`：

```cpp
// Copyright Voxel Plugin SAS. All Rights Reserved.

#include "PlanetCharacter.h"

#include "VoxelPlanetActor.h"
#include "VoxelComponents/VoxelInvokerComponent.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "UObject/ConstructorHelpers.h"
#include "EngineUtils.h"

APlanetCharacter::APlanetCharacter()
{
	PrimaryActorTick.bCanEverTick = true;

	GetCapsuleComponent()->InitCapsuleSize(42.f, 88.f);

	// 朝向交给 CMC：PhysicsRotation 会把目标旋转换算到重力空间、在那里清零
	// 俯仰与横滚再换回世界空间，因此角色天然垂直于球面。自己再插值一次只会
	// 和它打架。这里把控制器的旋转影响全部关掉，让引擎独占朝向。
	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = false;
	bUseControllerRotationRoll = false;

	UCharacterMovementComponent* CharacterMovement = GetCharacterMovement();
	CharacterMovement->bOrientRotationToMovement = true;
	CharacterMovement->RotationRate = FRotator(0.f, 540.f, 0.f);
	CharacterMovement->JumpZVelocity = 600.f;
	CharacterMovement->AirControl = 0.35f;
	CharacterMovement->MaxWalkSpeed = 600.f;
	CharacterMovement->MaxStepHeight = 60.f;
	// 球面是连续曲面，落地判定要给一点容差，否则下坡时会被判成“悬空”而抖。
	CharacterMovement->PerchRadiusThreshold = 0.f;
	CharacterMovement->LedgeCheckThreshold = 4.f;

	CameraPivot = CreateDefaultSubobject<USceneComponent>(TEXT("CameraPivot"));
	CameraPivot->SetupAttachment(GetCapsuleComponent());

	SpringArm = CreateDefaultSubobject<USpringArmComponent>(TEXT("SpringArm"));
	SpringArm->SetupAttachment(CameraPivot);
	// 相机由 CameraPivot 驱动，绝不走 ControlRotation：那是世界空间的。
	SpringArm->bUsePawnControlRotation = false;
	SpringArm->bInheritPitch = false;
	SpringArm->bInheritYaw = false;
	SpringArm->bInheritRoll = false;
	SpringArm->TargetArmLength = CameraArmLength;
	SpringArm->SocketOffset = FVector(0.f, 0.f, 120.f);
	SpringArm->bDoCollisionTest = true;

	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(SpringArm, USpringArmComponent::SocketName);
	Camera->bUsePawnControlRotation = false;

	VoxelInvoker = CreateDefaultSubobject<UVoxelSimpleInvokerComponent>(TEXT("VoxelInvoker"));
	VoxelInvoker->SetupAttachment(GetCapsuleComponent());
	// LODToSet = 0 表示最精细，这是插件对“贴着表面的 Pawn”的约定用法。
	VoxelInvoker->bUseForLOD = true;
	VoxelInvoker->LODToSet = 0;
	VoxelInvoker->LODRange = 20000.f;
	VoxelInvoker->bUseForCollisions = true;
	VoxelInvoker->CollisionsRange = 20000.f;
	// 导航、事件、优先级本任务用不到，关掉省算力。
	VoxelInvoker->bUseForNavmesh = false;
	VoxelInvoker->bUseForEvents = false;
	VoxelInvoker->bUseForPriorities = false;

	// 占位网格：随引擎发布的立方体，保证没有美术资源时角色也看得见。
	// 导入真实模型后替换这里的路径（见 Task 5）。
	static ConstructorHelpers::FObjectFinder<USkeletalMesh> PlaceholderMesh(
		TEXT("/Engine/EngineMeshes/SkeletalCube.SkeletalCube"));
	if (PlaceholderMesh.Succeeded())
	{
		GetMesh()->SetSkeletalMesh(PlaceholderMesh.Object);
	}
	// 胶囊半高 88：把网格下移到胶囊底，让立方体站在地面上而不是穿进地里。
	GetMesh()->SetRelativeLocation(FVector(0.f, 0.f, -88.f));
	GetMesh()->SetRelativeScale3D(FVector(0.7f));
}

void APlanetCharacter::BeginPlay()
{
	Super::BeginPlay();

	PlanetActor = FindNearestPlanet();
	if (!PlanetActor)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("APlanetCharacter: 关卡里找不到 AVoxelPlanetActor，角色不会获得球面重力。"));
		return;
	}

	// 关卡里没有 PlayerStart 时角色会生成在世界原点；对球心在原点的行星来说
	// 就是生成在实心内部。这里直接摆到球面正上方。
	SnapToPlanetSurface();
}

void APlanetCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (!PlanetActor)
	{
		return;
	}

	const FVector ToCenter = (PlanetActor->GetActorLocation() - GetActorLocation()).GetSafeNormal();
	if (ToCenter.IsNearlyZero())
	{
		// 正好在球心，重力方向无意义，保持上一帧的姿态。
		return;
	}

	// 球面行走的全部物理：告诉 CMC“下”指向球心。落地判定、斜坡滑动、台阶
	// 与跳跃都在重力空间里自动成立，不需要手写球面物理。
	GetCharacterMovement()->SetGravityDirection(ToCenter);

	// 相机枢轴：以球面法线为“上”，再按玩家偏航绕这根轴上转。
	const FQuat AlignToSurface = FQuat::FindBetweenNormals(FVector::UpVector, -ToCenter);
	CameraPivot->SetWorldRotation(
		AlignToSurface * FRotator(CameraPitch, CameraYaw, 0.f).Quaternion());

	// 碰撞体是异步流送出来的：第一次踩到地面之前，每帧把角色按回球面，
	// 否则它会在碰撞体生成之前落进球体内部，然后卡在实心里。
	if (!bHasLanded)
	{
		if (GetCharacterMovement()->IsMovingOnGround())
		{
			bHasLanded = true;
		}
		else
		{
			SnapToPlanetSurface();
		}
	}
}

AVoxelPlanetActor* APlanetCharacter::FindNearestPlanet() const
{
	AVoxelPlanetActor* Nearest = nullptr;
	float NearestDistSq = FMath::Square(PlanetSearchRadius);

	for (TActorIterator<AVoxelPlanetActor> It(GetWorld()); It; ++It)
	{
		const float DistSq = FVector::DistSquared(It->GetActorLocation(), GetActorLocation());
		if (DistSq < NearestDistSq)
		{
			NearestDistSq = DistSq;
			Nearest = *It;
		}
	}
	return Nearest;
}

void APlanetCharacter::SnapToPlanetSurface()
{
	if (!PlanetActor)
	{
		return;
	}

	const FVector PlanetCenter = PlanetActor->GetActorLocation();

	FVector Up = (GetActorLocation() - PlanetCenter).GetSafeNormal();
	if (Up.IsNearlyZero())
	{
		// 生成点恰好落在球心：随便挑一个方向，至少保证不卡在实心里。
		Up = FVector::UpVector;
	}

	const float CapsuleHalfHeight = GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	// 多抬 1 厘米，避免刚好贴面时被判定为“已经在实体内部”。
	const FVector Location =
		PlanetCenter + Up * (PlanetActor->GetSurfaceRadiusCm() + CapsuleHalfHeight + 1.f);

	SetActorLocation(Location, /*bSweep=*/false);
	SetActorRotation((FQuat::FindBetweenNormals(FVector::UpVector, Up) * GetActorQuat()).Rotator());

	// 摆位之后残留的向下速度会让角色立刻穿进球体，必须清掉。
	GetCharacterMovement()->Velocity = FVector::ZeroVector;
}
```

- [ ] **Step 4: 补 UTF-8 BOM**

`Write` 工具不写 BOM。对四个新建文件执行：

```bash
powershell -NoProfile -Command "foreach (\$p in @('F:\workspace\ue\StellarEra\Source\StellarEra\Public\StellarEraGameMode.h','F:\workspace\ue\StellarEra\Source\StellarEra\Private\StellarEraGameMode.cpp','F:\workspace\ue\StellarEra\Source\StellarEra\Public\PlanetCharacter.h','F:\workspace\ue\StellarEra\Source\StellarEra\Private\PlanetCharacter.cpp')) { \$c = Get-Content -Raw -Encoding UTF8 \$p; [IO.File]::WriteAllText(\$p, \$c, (New-Object Text.UTF8Encoding \$true)) }"
```

验证四个文件都以 `efbbbf` 开头：

```bash
for f in Source/StellarEra/Public/StellarEraGameMode.h Source/StellarEra/Private/StellarEraGameMode.cpp Source/StellarEra/Public/PlanetCharacter.h Source/StellarEra/Private/PlanetCharacter.cpp; do printf "%s: " "$f"; head -c 3 "$f" | od -An -tx1 | tr -d ' \n'; echo; done
```

期望：四行全部输出 `efbbbf`。

- [ ] **Step 5: 把 GameMode 挂到项目设置**

在 `Config/DefaultEngine.ini` 的 `[/Script/EngineSettings.GameMapsSettings]` 小节里追加一行：

```ini
[/Script/EngineSettings.GameMapsSettings]
GameDefaultMap=/Game/Maps/UniverseMap.UniverseMap
EditorStartupMap=/Game/Maps/UniverseMap.UniverseMap
GlobalDefaultGameMode=/Script/StellarEra.StellarEraGameMode
```

- [ ] **Step 6: 构建**

关闭编辑器，然后：

```bash
powershell -NoProfile -File "C:/Users/23080/AppData/Local/Temp/ue_mcp/ue_build.ps1"
```

期望：`Build succeeded`，退出码 0，且输出里没有来自 `PlanetCharacter.cpp` / `StellarEraGameMode.cpp` / `VoxelPlanetActor.cpp` 的 warning。

- [ ] **Step 7: 在 PIE 里验证**

打开编辑器，加载 `UniverseMap`，点 Play。逐条核对：

1. 视口里能看到一颗球体行星，球面上站着一个立方体（占位网格）。
2. 角色**没有**掉进球体内部——它停在表面上方不动。
3. Output Log 里不出现 `APlanetCharacter: 关卡里找不到 AVoxelPlanetActor`。
4. 角色脚下的地面能看到碰撞体（控制台输入 `show Collision` 切换显示）。
5. 不出现 ensure、不出现 `LogVoxel` 的 Error。

若第 2 条失败（角色缓慢陷进球体），说明 `bHasLanded` 提前置真或碰撞体一直没生成；
检查 `VoxelInvoker->bUseForCollisions` 是否为 true，以及行星 Actor 的 `bEnableCollisions`。

- [ ] **Step 8: 提交**

```bash
git add Source/StellarEra/Public/VoxelPlanetActor.h Source/StellarEra/Private/VoxelPlanetActor.cpp \
        Source/StellarEra/Public/StellarEraGameMode.h Source/StellarEra/Private/StellarEraGameMode.cpp \
        Source/StellarEra/Public/PlanetCharacter.h Source/StellarEra/Private/PlanetCharacter.cpp \
        Config/DefaultEngine.ini
git commit -m "feat: 角色生成在球面并受球心重力

新增 AStellarEraGameMode 与 APlanetCharacter。角色自带开启碰撞的体素
Invoker，每帧把指向球心的方向喂给 CMC 的自定义重力，并在碰撞体流送出
来之前持续把自己按回球面。

Co-Authored-By: Claude Code <noreply@anthropic.com>"
```

---

## Task 2: 输入与球面行走

让 WASD 能在球面任意方向行走、鼠标能转视角、Space 能跳。

**Files:**
- Create: `Source/StellarEra/Public/StellarEraPlayerController.h`
- Create: `Source/StellarEra/Private/StellarEraPlayerController.cpp`
- Create: `Config/DefaultInput.ini`
- Modify: `Source/StellarEra/Public/PlanetCharacter.h`
- Modify: `Source/StellarEra/Private/PlanetCharacter.cpp`
- Modify: `Source/StellarEra/Private/StellarEraGameMode.cpp`

**Interfaces:**
- Consumes: `APlanetCharacter`（Task 1）、`AVoxelPlanetActor::GetSurfaceRadiusCm()`（Task 1）
- Produces:
  - `class AStellarEraPlayerController : public APlayerController`，含受保护成员 `TObjectPtr<UInputMappingContext> MappingContext`、`TObjectPtr<UInputAction> IA_Move/IA_Look/IA_Jump/IA_Sprint`、`float LookSensitivity`
  - `void APlanetCharacter::Move(const FVector2D& Value)`
  - `void APlanetCharacter::Look(float DeltaYaw, float DeltaPitch)`
  - `void APlanetCharacter::SetSprinting(bool bSprinting)`

---

- [ ] **Step 1: 新建 `Config/DefaultInput.ini`**

没有这个文件，`APlayerController::InitInputSystem` 会造一个普通 `UInputComponent`，
`Cast<UEnhancedInputComponent>` 会失败，所有 `BindAction` 静默失效。

新建 `Config/DefaultInput.ini`：

```ini
[/Script/Engine.InputSettings]
DefaultPlayerInputClass=/Script/EnhancedInput.EnhancedPlayerInput
DefaultInputComponentClass=/Script/EnhancedInput.EnhancedInputComponent
```

**注意**：这个文件在编辑器下次启动时才生效，Step 7 的构建之后需要重启编辑器。

- [ ] **Step 2: 新建 `AStellarEraPlayerController` 头文件**

新建 `Source/StellarEra/Public/StellarEraPlayerController.h`：

```cpp
// Copyright Voxel Plugin SAS. All Rights Reserved.
// 玩家控制器：构造输入资产、分派输入、编排登船/离船。

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "StellarEraPlayerController.generated.h"

class UInputAction;
class UInputMappingContext;
struct FInputActionValue;

/**
 * 本项目的玩家控制器。
 *
 * 输入资产（IMC / IA）在运行时用 C++ 构造：零手工步骤，改完代码立刻能跑，
 * 不需要先在编辑器里点出一堆资产。代价是键位暂时不能在编辑器里改；等键位
 * 定稿后再换成 .uasset 是平滑的替换（数据结构一样，只是来源变了）。
 *
 * 所有动作都绑定在本控制器上，再按“当前 Possess 的是哪个 Pawn”分派。
 * 这样切换角色/飞船时不需要重新绑定输入。
 */
UCLASS()
class AStellarEraPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	AStellarEraPlayerController();

	virtual void SetupInputComponent() override;
	virtual void BeginPlay() override;

protected:
	/** 构造 IMC 与全部 IA。幂等：重复调用会直接返回。 */
	void BuildInputAssets();

	// --- 角色输入回调 ---
	void OnMove(const FInputActionValue& Value);
	void OnLook(const FInputActionValue& Value);
	void OnJump(const FInputActionValue& Value);
	void OnSprintStarted(const FInputActionValue& Value);
	void OnSprintCompleted(const FInputActionValue& Value);

	/** 鼠标灵敏度（度/像素）。 */
	UPROPERTY(EditAnywhere, Category = "Input")
	float LookSensitivity = 1.2f;

	/** 上下视角反向。觉得鼠标上下颠倒时把它改成 true。 */
	UPROPERTY(EditAnywhere, Category = "Input")
	bool bInvertLookY = false;

	UPROPERTY(Transient)
	TObjectPtr<UInputMappingContext> MappingContext;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> IA_Move;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> IA_Look;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> IA_Jump;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> IA_Sprint;
};
```

- [ ] **Step 3: 实现 `AStellarEraPlayerController`**

新建 `Source/StellarEra/Private/StellarEraPlayerController.cpp`：

```cpp
// Copyright Voxel Plugin SAS. All Rights Reserved.

#include "StellarEraPlayerController.h"

#include "PlanetCharacter.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"
#include "Engine/LocalPlayer.h"

namespace
{
	/** 造一个运行时输入动作。ValueType 决定回调里 Get<T>() 的模板参数。 */
	UInputAction* MakeAction(UObject* Outer, const TCHAR* Name, EInputActionValueType ValueType)
	{
		UInputAction* Action = NewObject<UInputAction>(Outer, Name);
		Action->ValueType = ValueType;
		return Action;
	}

	/** 把一个按键映射到动作，并按顺序挂上修饰器。 */
	void MapKey(
		UInputMappingContext* Context,
		UInputAction* Action,
		const FKey& Key,
		const TArray<UInputModifier*>& Modifiers = TArray<UInputModifier*>())
	{
		FEnhancedActionKeyMapping& Mapping = Context->MapKey(Action, Key);
		for (UInputModifier* Modifier : Modifiers)
		{
			Mapping.Modifiers.Add(Modifier);
		}
	}

	UInputModifier* MakeNegate(UObject* Outer)
	{
		return NewObject<UInputModifierNegate>(Outer);
	}

	/** Swizzle(YXZ) 把按键的 X 轴值搬到 Y 轴，用于把 WASD 合成一个二维向量。 */
	UInputModifier* MakeSwizzleYXZ(UObject* Outer)
	{
		UInputModifierSwizzleAxis* Swizzle = NewObject<UInputModifierSwizzleAxis>(Outer);
		Swizzle->Order = EInputAxisSwizzle::YXZ;
		return Swizzle;
	}
}

AStellarEraPlayerController::AStellarEraPlayerController()
{
	// 输入组件由 DefaultInput.ini 指定为 EnhancedInput 版本；这里不额外设置。
	// 若 DefaultInput.ini 缺失，Cast<UEnhancedInputComponent> 会失败。
}

void AStellarEraPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();

	BuildInputAssets();

	UEnhancedInputComponent* EnhancedInput = Cast<UEnhancedInputComponent>(InputComponent);
	if (!EnhancedInput)
	{
		UE_LOG(LogTemp, Error,
			TEXT("AStellarEraPlayerController: InputComponent 不是 UEnhancedInputComponent，"
				 "请检查 Config/DefaultInput.ini 的 DefaultInputComponentClass。"));
		return;
	}

	EnhancedInput->BindAction(IA_Move, ETriggerEvent::Triggered, this, &AStellarEraPlayerController::OnMove);
	EnhancedInput->BindAction(IA_Look, ETriggerEvent::Triggered, this, &AStellarEraPlayerController::OnLook);
	EnhancedInput->BindAction(IA_Jump, ETriggerEvent::Started, this, &AStellarEraPlayerController::OnJump);
	EnhancedInput->BindAction(IA_Sprint, ETriggerEvent::Started, this, &AStellarEraPlayerController::OnSprintStarted);
	EnhancedInput->BindAction(IA_Sprint, ETriggerEvent::Completed, this, &AStellarEraPlayerController::OnSprintCompleted);
}

void AStellarEraPlayerController::BeginPlay()
{
	Super::BeginPlay();

	// SetupInputComponent 通常先于 BeginPlay 跑完，但这里再兜一次底。
	BuildInputAssets();

	if (UEnhancedInputLocalPlayerSubsystem* Subsystem =
		ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer()))
	{
		Subsystem->AddMappingContext(MappingContext, /*Priority=*/0);
	}
}

void AStellarEraPlayerController::BuildInputAssets()
{
	if (MappingContext)
	{
		return;
	}

	MappingContext = NewObject<UInputMappingContext>(this, TEXT("IMC_StellarEra"));

	IA_Move = MakeAction(this, TEXT("IA_Move"), EInputActionValueType::Axis2D);
	IA_Look = MakeAction(this, TEXT("IA_Look"), EInputActionValueType::Axis2D);
	IA_Jump = MakeAction(this, TEXT("IA_Jump"), EInputActionValueType::Boolean);
	IA_Sprint = MakeAction(this, TEXT("IA_Sprint"), EInputActionValueType::Boolean);

	// WASD 合成一个二维向量：W/S 走 X 轴，A/D 先用 Swizzle 搬到 Y 轴再取反。
	// 修饰器按数组顺序依次作用，所以 A 是“先 Swizzle 再 Negate”。
	MapKey(MappingContext, IA_Move, EKeys::W);
	MapKey(MappingContext, IA_Move, EKeys::S, { MakeNegate(this) });
	MapKey(MappingContext, IA_Move, EKeys::D, { MakeSwizzleYXZ(this) });
	MapKey(MappingContext, IA_Move, EKeys::A, { MakeSwizzleYXZ(this), MakeNegate(this) });

	MapKey(MappingContext, IA_Look, EKeys::Mouse2D);

	MapKey(MappingContext, IA_Jump, EKeys::SpaceBar);
	MapKey(MappingContext, IA_Sprint, EKeys::LeftShift);
}

void AStellarEraPlayerController::OnMove(const FInputActionValue& Value)
{
	// 在飞船里时 WASD 归 IA_Thrust / IA_Roll 管，这里直接不响应。
	if (APlanetCharacter* Character = Cast<APlanetCharacter>(GetPawn()))
	{
		Character->Move(Value.Get<FVector2D>());
	}
}

void AStellarEraPlayerController::OnLook(const FInputActionValue& Value)
{
	const FVector2D Axis = Value.Get<FVector2D>();
	const float DeltaPitch = bInvertLookY ? -Axis.Y : Axis.Y;

	if (APlanetCharacter* Character = Cast<APlanetCharacter>(GetPawn()))
	{
		Character->Look(Axis.X * LookSensitivity, DeltaPitch * LookSensitivity);
	}
}

void AStellarEraPlayerController::OnJump(const FInputActionValue& Value)
{
	if (APlanetCharacter* Character = Cast<APlanetCharacter>(GetPawn()))
	{
		Character->Jump();
	}
}

void AStellarEraPlayerController::OnSprintStarted(const FInputActionValue& Value)
{
	if (APlanetCharacter* Character = Cast<APlanetCharacter>(GetPawn()))
	{
		Character->SetSprinting(true);
	}
}

void AStellarEraPlayerController::OnSprintCompleted(const FInputActionValue& Value)
{
	if (APlanetCharacter* Character = Cast<APlanetCharacter>(GetPawn()))
	{
		Character->SetSprinting(false);
	}
}
```

- [ ] **Step 4: 给 `APlanetCharacter` 加输入接口**

在 `Source/StellarEra/Public/PlanetCharacter.h` 的 `virtual void Tick(float DeltaSeconds) override;` **下面**插入：

```cpp
	/** 球面行走输入：X = 前后，Y = 左右，取值 [-1, 1]。 */
	void Move(const FVector2D& Value);

	/** 相机输入，单位是度。由 PlayerController 乘完灵敏度后传进来。 */
	void Look(float DeltaYaw, float DeltaPitch);

	/** 切换冲刺。 */
	void SetSprinting(bool bSprinting);
```

在 `PlanetCharacter.h` 的 `MaxCameraPitch` 声明**下面**插入两个可调速度：

```cpp
	/** 行走速度（厘米/秒）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet")
	float WalkSpeed = 600.f;

	/** 冲刺速度（厘米/秒）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet")
	float SprintSpeed = 1200.f;
```

在 `Source/StellarEra/Private/PlanetCharacter.cpp` 的 `SnapToPlanetSurface()` 函数体**之后**追加：

```cpp
void APlanetCharacter::Move(const FVector2D& Value)
{
	if (!CameraPivot || Value.IsNearlyZero())
	{
		return;
	}

	// 以相机朝向为基准：W 永远是“往画面里走”。相机枢轴已经被摆成“以球面
	// 法线为上”，所以它的前向天然切于球面。
	const FVector Desired =
		CameraPivot->GetForwardVector() * Value.X + CameraPivot->GetRightVector() * Value.Y;

	// 相机俯仰会让前向偏出切平面，投影回重力平面，否则输入会把角色往天上推。
	const FVector GravityDir = GetCharacterMovement()->GetGravityDirection();
	const FVector OnFloor = FVector::VectorPlaneProject(Desired, GravityDir);
	if (OnFloor.IsNearlyZero())
	{
		return;
	}

	AddMovementInput(OnFloor.GetSafeNormal(), FMath::Min(1.f, OnFloor.Size()));
}

void APlanetCharacter::Look(float DeltaYaw, float DeltaPitch)
{
	// 偏航绕球面法线累积；俯仰钳制在一个不会翻过去的范围内。
	CameraYaw = FRotator::NormalizeAxis(CameraYaw + DeltaYaw);
	CameraPitch = FMath::Clamp(CameraPitch + DeltaPitch, MinCameraPitch, MaxCameraPitch);
}

void APlanetCharacter::SetSprinting(bool bSprinting)
{
	GetCharacterMovement()->MaxWalkSpeed = bSprinting ? SprintSpeed : WalkSpeed;
}
```

同时把构造函数里的硬编码速度改成用新属性，保证初始值只有一个来源——
把 `CharacterMovement->MaxWalkSpeed = 600.f;` 改成：

```cpp
	CharacterMovement->MaxWalkSpeed = WalkSpeed;
```

- [ ] **Step 5: 让 GameMode 使用新的 PlayerController**

在 `Source/StellarEra/Private/StellarEraGameMode.cpp` 顶部加 include：

```cpp
#include "StellarEraPlayerController.h"
```

把构造函数改成：

```cpp
AStellarEraGameMode::AStellarEraGameMode()
{
	// 关卡里没有 PlayerStart，角色会生成在世界原点。APlanetCharacter 的
	// BeginPlay 会把自己摆到球面正上方，所以这里不需要额外处理出生点。
	DefaultPawnClass = APlanetCharacter::StaticClass();
	PlayerControllerClass = AStellarEraPlayerController::StaticClass();
}
```

- [ ] **Step 6: 补 UTF-8 BOM**

```bash
powershell -NoProfile -Command "foreach (\$p in @('F:\workspace\ue\StellarEra\Source\StellarEra\Public\StellarEraPlayerController.h','F:\workspace\ue\StellarEra\Source\StellarEra\Private\StellarEraPlayerController.cpp')) { \$c = Get-Content -Raw -Encoding UTF8 \$p; [IO.File]::WriteAllText(\$p, \$c, (New-Object Text.UTF8Encoding \$true)) }"
```

- [ ] **Step 7: 构建**

关闭编辑器（本步骤修改了 `DefaultInput.ini`，构建后需要重启编辑器才能读到）：

```bash
powershell -NoProfile -File "C:/Users/23080/AppData/Local/Temp/ue_mcp/ue_build.ps1"
```

期望：`Build succeeded`，退出码 0。

- [ ] **Step 8: 在 PIE 里验证**

重启编辑器（让 `DefaultInput.ini` 生效），加载 `UniverseMap`，点 Play。逐条核对：

1. 按 `W` 角色向远离相机的方向走；按 `S` 反向；`A` / `D` 左右横移。
2. 一直按 `W` 走，角色能**绕球走一整圈**；走到球的"另一面"时角色仍然直立
   （脚朝球心），没有倒挂或侧躺。
3. 鼠标左右移动，相机绕角色水平旋转；上下移动改变俯仰，且俯仰被限制在
   上下各 75° 以内，不会翻过去。
4. 走到球体背面（相对世界 Z 轴"下方"）时，鼠标左右移动**仍然是水平环绕**，
   画面不出现翻滚——这是本任务最容易失败的一条，若失败说明相机枢轴没生效。
5. 按 `Space` 角色跳起并落回地面，不穿模。
6. 按住 `LeftShift` 行走速度明显变快。
7. Output Log 里不出现 `InputComponent 不是 UEnhancedInputComponent`。

- [ ] **Step 9: 提交**

```bash
git add Source/StellarEra/Public/StellarEraPlayerController.h \
        Source/StellarEra/Private/StellarEraPlayerController.cpp \
        Source/StellarEra/Public/PlanetCharacter.h \
        Source/StellarEra/Private/PlanetCharacter.cpp \
        Source/StellarEra/Private/StellarEraGameMode.cpp \
        Config/DefaultInput.ini
git commit -m "feat: 运行时构造 Enhanced Input 并实现球面行走

输入资产在 PlayerController 里用 C++ 构造，按当前 Possess 的 Pawn 类型
分派。相机走自建的枢轴而非 ControlRotation——后者是世界空间的，在球体
背面绕世界 Z 转会让画面翻滚。

Co-Authored-By: Claude Code <noreply@anthropic.com>"
```

---

## Task 3: 街机式太空飞船

让飞船存在、能被操控、在太空中以街机手感飞行，并且能停在球面上。

**Files:**
- Create: `Source/StellarEra/Public/SpaceshipPawn.h`
- Create: `Source/StellarEra/Private/SpaceshipPawn.cpp`
- Modify: `Source/StellarEra/Public/StellarEraGameMode.h`
- Modify: `Source/StellarEra/Private/StellarEraGameMode.cpp`
- Modify: `Source/StellarEra/Public/StellarEraPlayerController.h`
- Modify: `Source/StellarEra/Private/StellarEraPlayerController.cpp`

**Interfaces:**
- Consumes: `AVoxelPlanetActor::GetSurfaceRadiusCm()`（Task 1）、`AStellarEraPlayerController`（Task 2）
- Produces:
  - `class ASpaceshipPawn : public APawn`
  - `void ASpaceshipPawn::SetThrust(float)` / `SetRoll(float)` / `SetVertical(float)` / `Look(float DeltaYaw, float DeltaPitch)`
  - `bool ASpaceshipPawn::IsLanded() const`
  - `AStellarEraPlayerController::IA_Thrust / IA_Roll / IA_Vertical / IA_Interact`

---

- [ ] **Step 1: 新建 `ASpaceshipPawn` 头文件**

新建 `Source/StellarEra/Public/SpaceshipPawn.h`：

```cpp
// Copyright Voxel Plugin SAS. All Rights Reserved.
// 街机式太空飞船。

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "SpaceshipPawn.generated.h"

class UCameraComponent;
class UFloatingPawnMovement;
class USphereComponent;
class USpringArmComponent;
class UStaticMeshComponent;

/**
 * 街机式太空飞船。
 *
 * 位移交给 UFloatingPawnMovement（自带扫掠、加速度、阻尼），手感写在 Tick 里：
 * 鼠标给的是角速度冲量而不是直接设朝向，松手后指数衰减——这是“开飞机”的
 * 惯性感来源，也是《戴森球计划》伊卡洛斯的做法。
 */
UCLASS()
class ASpaceshipPawn : public APawn
{
	GENERATED_BODY()

public:
	ASpaceshipPawn();

	virtual void Tick(float DeltaSeconds) override;

	/** 主推力输入（-1 = 反推，+1 = 前进）。 */
	void SetThrust(float Value);

	/** 横滚输入（-1 / +1）。 */
	void SetRoll(float Value);

	/** 升降输入（-1 = 降，+1 = 升）。 */
	void SetVertical(float Value);

	/** 鼠标输入（度）。以冲量形式累加进角速度，松手后衰减。 */
	void Look(float DeltaYaw, float DeltaPitch);

	/** 速度低于阈值且机身下方命中地面时为真。 */
	UFUNCTION(BlueprintPure, Category = "Spaceship")
	bool IsLanded() const;

protected:
	/** 碰撞球：既是根组件也是 UFloatingPawnMovement 扫掠用的形状。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Spaceship")
	TObjectPtr<USphereComponent> Collision;

	/** 外壳网格。占位用引擎立方体，导入真实模型后替换。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Spaceship")
	TObjectPtr<UStaticMeshComponent> Hull;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Spaceship")
	TObjectPtr<UFloatingPawnMovement> FloatingMovement;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera")
	TObjectPtr<USpringArmComponent> SpringArm;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera")
	TObjectPtr<UCameraComponent> Camera;

	/** 偏航角速度上限（度/秒）。 */
	UPROPERTY(EditAnywhere, Category = "Spaceship|Handling")
	float MaxYawRate = 90.f;

	/** 俯仰角速度上限（度/秒）。 */
	UPROPERTY(EditAnywhere, Category = "Spaceship|Handling")
	float MaxPitchRate = 70.f;

	/** 横滚角速度（度/秒），由 A/D 直接驱动，不带惯性。 */
	UPROPERTY(EditAnywhere, Category = "Spaceship|Handling")
	float MaxRollRate = 120.f;

	/** 鼠标位移到角速度的换算系数（度/秒 每度）。越大越灵敏。 */
	UPROPERTY(EditAnywhere, Category = "Spaceship|Handling")
	float MouseImpulseScale = 6.f;

	/** 角速度衰减速率（FInterpTo 速率）。越小惯性越久。 */
	UPROPERTY(EditAnywhere, Category = "Spaceship|Handling")
	float AngularDamping = 3.5f;

	/** 自动回正速率（度/秒）。只回正横滚，不动玩家给的俯仰。0 = 关闭。 */
	UPROPERTY(EditAnywhere, Category = "Spaceship|Handling")
	float AutoLevelSpeed = 45.f;

	/** 判定“已落地”的速度阈值（厘米/秒）。 */
	UPROPERTY(EditAnywhere, Category = "Spaceship|Landing")
	float LandingSpeedThreshold = 200.f;

	/** 落地射线的长度（厘米）。 */
	UPROPERTY(EditAnywhere, Category = "Spaceship|Landing")
	float LandingProbeDistance = 600.f;

private:
	float ThrustInput = 0.f;
	float RollInput = 0.f;
	float VerticalInput = 0.f;
	float YawRate = 0.f;
	float PitchRate = 0.f;
};
```

- [ ] **Step 2: 实现 `ASpaceshipPawn`**

新建 `Source/StellarEra/Private/SpaceshipPawn.cpp`：

```cpp
// Copyright Voxel Plugin SAS. All Rights Reserved.

#include "SpaceshipPawn.h"

#include "Camera/CameraComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/FloatingPawnMovement.h"
#include "GameFramework/SpringArmComponent.h"
#include "UObject/ConstructorHelpers.h"

ASpaceshipPawn::ASpaceshipPawn()
{
	PrimaryActorTick.bCanEverTick = true;

	Collision = CreateDefaultSubobject<USphereComponent>(TEXT("Collision"));
	Collision->InitSphereRadius(300.f);
	Collision->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Collision->SetCollisionObjectType(ECC_Pawn);
	Collision->SetCollisionResponseToAllChannels(ECR_Block);
	SetRootComponent(Collision);

	FloatingMovement = CreateDefaultSubobject<UFloatingPawnMovement>(TEXT("FloatingMovement"));
	FloatingMovement->MaxSpeed = 8000.f;
	FloatingMovement->Acceleration = 4000.f;
	FloatingMovement->Deceleration = 2000.f;
	FloatingMovement->TurningBoost = 8.f;

	SpringArm = CreateDefaultSubobject<USpringArmComponent>(TEXT("SpringArm"));
	SpringArm->SetupAttachment(Collision);
	SpringArm->bUsePawnControlRotation = false;
	SpringArm->bInheritPitch = true;
	SpringArm->bInheritYaw = true;
	// 不继承横滚：机身可以翻滚，画面不该跟着倒过来。
	SpringArm->bInheritRoll = false;
	SpringArm->TargetArmLength = 1800.f;
	SpringArm->SocketOffset = FVector(0.f, 0.f, 400.f);
	SpringArm->bDoCollisionTest = true;

	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(SpringArm, USpringArmComponent::SocketName);
	Camera->bUsePawnControlRotation = false;

	Hull = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Hull"));
	Hull->SetupAttachment(Collision);
	// 外壳只负责好看，碰撞交给根部的球体。
	Hull->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Hull->SetRelativeScale3D(FVector(6.f, 2.f, 1.f));

	// 占位外壳：随引擎发布的立方体。导入真实模型后替换这里的路径（见 Task 5）。
	static ConstructorHelpers::FObjectFinder<UStaticMesh> PlaceholderHull(
		TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (PlaceholderHull.Succeeded())
	{
		Hull->SetStaticMesh(PlaceholderHull.Object);
	}
}

void ASpaceshipPawn::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// --- 1. 角速度衰减 ---
	// 鼠标给的是冲量（见 Look），这里让角速度平滑地衰减回 0。两者的平衡
	// 就是“甩一下鼠标，飞船转一会儿再停下”的手感。
	YawRate = FMath::FInterpTo(YawRate, 0.f, DeltaSeconds, AngularDamping);
	PitchRate = FMath::FInterpTo(PitchRate, 0.f, DeltaSeconds, AngularDamping);

	// --- 2. 应用旋转 ---
	FRotator DeltaRotation(PitchRate * DeltaSeconds, YawRate * DeltaSeconds, 0.f);
	DeltaRotation.Roll = RollInput * MaxRollRate * DeltaSeconds;
	AddActorLocalRotation(DeltaRotation);

	// --- 3. 自动回正：只把横滚拉回水平，俯仰留给玩家 ---
	if (AutoLevelSpeed > 0.f)
	{
		const FQuat Current = GetActorQuat();
		const FVector Forward = Current.GetForwardVector();
		// 世界 Z 投影到垂直于机身前向的平面，就是机身“上”应该朝向的方向。
		FVector DesiredUp = FVector::VectorPlaneProject(FVector::UpVector, Forward);
		if (DesiredUp.Normalize())
		{
			const FQuat Correction = FQuat::FindBetweenNormals(Current.GetUpVector(), DesiredUp);
			const float AngleRad = Correction.GetAngle();
			if (AngleRad > KINDA_SMALL_NUMBER)
			{
				// 限制每秒最多转 AutoLevelSpeed 度，避免生硬地瞬间弹正。
				const float MaxRad = FMath::DegreesToRadians(AutoLevelSpeed) * DeltaSeconds;
				const float Alpha = FMath::Min(1.f, MaxRad / AngleRad);
				AddActorWorldRotation(FQuat::Slerp(FQuat::Identity, Correction, Alpha));
			}
		}
	}

	// --- 4. 位移：推力交给 UFloatingPawnMovement，它自带扫掠与加减速 ---
	FloatingMovement->AddInputVector(GetActorForwardVector() * ThrustInput);
	FloatingMovement->AddInputVector(GetActorUpVector() * VerticalInput);
}

void ASpaceshipPawn::SetThrust(float Value)
{
	ThrustInput = FMath::Clamp(Value, -1.f, 1.f);
}

void ASpaceshipPawn::SetRoll(float Value)
{
	RollInput = FMath::Clamp(Value, -1.f, 1.f);
}

void ASpaceshipPawn::SetVertical(float Value)
{
	VerticalInput = FMath::Clamp(Value, -1.f, 1.f);
}

void ASpaceshipPawn::Look(float DeltaYaw, float DeltaPitch)
{
	// 冲量模型：鼠标位移直接累加进角速度。甩一下鼠标，飞船会转一会儿再
	// 慢慢停下，而不是跟着鼠标立刻静止——这就是街机飞行的惯性感。
	YawRate = FMath::Clamp(YawRate + DeltaYaw * MouseImpulseScale, -MaxYawRate, MaxYawRate);
	PitchRate = FMath::Clamp(PitchRate + DeltaPitch * MouseImpulseScale, -MaxPitchRate, MaxPitchRate);
}

bool ASpaceshipPawn::IsLanded() const
{
	if (GetVelocity().Size() > LandingSpeedThreshold)
	{
		return false;
	}

	// 沿机身“下”方打射线，而不是世界下方：飞船停在球体侧面时世界下方是空的。
	const FVector Start = GetActorLocation();
	const FVector End = Start - GetActorUpVector() * LandingProbeDistance;

	FCollisionQueryParams Params(SCENE_QUERY_STAT(SpaceshipLanding), /*bTraceComplex=*/false, this);
	FHitResult Hit;
	return GetWorld()->LineTraceSingleByChannel(Hit, Start, End, ECC_WorldStatic, Params);
}
```

- [ ] **Step 3: 让 GameMode 自动补一艘飞船**

在 `Source/StellarEra/Public/StellarEraGameMode.h` 的 `AStellarEraGameMode();` **下面**插入：

```cpp
	/** 飞船相对行星球心的生成方向（会归一化）。默认放在赤道一侧。 */
	UPROPERTY(EditDefaultsOnly, Category = "StellarEra")
	FVector ShipSpawnDirection = FVector(0.f, 1.f, 0.f);

	/** 飞船生成点离行星表面的高度（厘米）。 */
	UPROPERTY(EditDefaultsOnly, Category = "StellarEra")
	float ShipSpawnHeight = 400.f;

	/** 关卡里没有飞船时是否自动生成一艘，方便开箱即测。 */
	UPROPERTY(EditDefaultsOnly, Category = "StellarEra")
	bool bSpawnSpaceshipIfMissing = true;

protected:
	virtual void BeginPlay() override;

	/** 在球面上找一处空地生成飞船；关卡里已存在则跳过。 */
	void EnsureSpaceshipExists();
```

在 `Source/StellarEra/Private/StellarEraGameMode.cpp` 里追加 include：

```cpp
#include "SpaceshipPawn.h"
#include "VoxelPlanetActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
```

并在文件末尾追加：

```cpp
void AStellarEraGameMode::BeginPlay()
{
	Super::BeginPlay();
	EnsureSpaceshipExists();
}

void AStellarEraGameMode::EnsureSpaceshipExists()
{
	if (!bSpawnSpaceshipIfMissing)
	{
		return;
	}

	// 关卡里已经手摆了一艘就不再生成，让美术摆位优先于自动布置。
	for (TActorIterator<ASpaceshipPawn> It(GetWorld()); It; ++It)
	{
		return;
	}

	AVoxelPlanetActor* Planet = nullptr;
	for (TActorIterator<AVoxelPlanetActor> It(GetWorld()); It; ++It)
	{
		Planet = *It;
		break;
	}
	if (!Planet)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("StellarEraGameMode: 关卡里没有 AVoxelPlanetActor，跳过飞船生成。"));
		return;
	}

	const FVector Direction = ShipSpawnDirection.GetSafeNormal();
	if (Direction.IsNearlyZero())
	{
		return;
	}

	const FVector Location =
		Planet->GetActorLocation() + Direction * (Planet->GetSurfaceRadiusCm() + ShipSpawnHeight);

	// 让飞船的“上”与球面法线一致：否则在极地附近它会横躺着。
	const FQuat AlignToSurface = FQuat::FindBetweenNormals(FVector::UpVector, Direction);

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	GetWorld()->SpawnActor<ASpaceshipPawn>(Location, AlignToSurface.Rotator(), Params);
}
```

- [ ] **Step 4: 加飞船输入动作与一个调试用切换命令**

在 `Source/StellarEra/Public/StellarEraPlayerController.h` 的 `OnSprintCompleted` 声明**下面**插入：

```cpp
	// --- 飞船输入回调 ---
	void OnThrust(const FInputActionValue& Value);
	void OnRoll(const FInputActionValue& Value);
	void OnVertical(const FInputActionValue& Value);

	/**
	 * 调试用：直接在角色与飞船之间切换 Possess，跳过登船流程。
	 * 在 PIE 控制台输入 `DebugPossessShip` 调用。Task 4 做好登船后，
	 * 这个命令仍保留作为开发工具。
	 */
	UFUNCTION(Exec)
	void DebugPossessShip();
```

在 `IA_Sprint` 声明**下面**插入：

```cpp
	UPROPERTY(Transient)
	TObjectPtr<UInputAction> IA_Thrust;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> IA_Roll;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> IA_Vertical;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> IA_Interact;
```

在 `Source/StellarEra/Private/StellarEraPlayerController.cpp` 顶部加：

```cpp
#include "SpaceshipPawn.h"
#include "EngineUtils.h"
```

在 `BuildInputAssets()` 里 `IA_Sprint` 那行**下面**插入：

```cpp
	IA_Thrust = MakeAction(this, TEXT("IA_Thrust"), EInputActionValueType::Axis1D);
	IA_Roll = MakeAction(this, TEXT("IA_Roll"), EInputActionValueType::Axis1D);
	IA_Vertical = MakeAction(this, TEXT("IA_Vertical"), EInputActionValueType::Axis1D);
	IA_Interact = MakeAction(this, TEXT("IA_Interact"), EInputActionValueType::Boolean);
```

在 `BuildInputAssets()` 的 `MapKey(MappingContext, IA_Sprint, EKeys::LeftShift);` **下面**插入：

```cpp
	// 飞船：W/S 主推力，D/A 横滚，Space/LeftControl 升降。与角色的 WASD
	// 共用按键没问题——同一个 IMC 里的动作互不吞输入，分派由 Pawn 类型决定。
	MapKey(MappingContext, IA_Thrust, EKeys::W);
	MapKey(MappingContext, IA_Thrust, EKeys::S, { MakeNegate(this) });
	MapKey(MappingContext, IA_Roll, EKeys::D);
	MapKey(MappingContext, IA_Roll, EKeys::A, { MakeNegate(this) });
	MapKey(MappingContext, IA_Vertical, EKeys::SpaceBar);
	MapKey(MappingContext, IA_Vertical, EKeys::LeftControl, { MakeNegate(this) });

	MapKey(MappingContext, IA_Interact, EKeys::E);
```

在 `SetupInputComponent()` 的 `IA_Sprint ... OnSprintCompleted);` **下面**插入：

```cpp
	EnhancedInput->BindAction(IA_Thrust, ETriggerEvent::Triggered, this, &AStellarEraPlayerController::OnThrust);
	EnhancedInput->BindAction(IA_Thrust, ETriggerEvent::Completed, this, &AStellarEraPlayerController::OnThrust);
	EnhancedInput->BindAction(IA_Roll, ETriggerEvent::Triggered, this, &AStellarEraPlayerController::OnRoll);
	EnhancedInput->BindAction(IA_Roll, ETriggerEvent::Completed, this, &AStellarEraPlayerController::OnRoll);
	EnhancedInput->BindAction(IA_Vertical, ETriggerEvent::Triggered, this, &AStellarEraPlayerController::OnVertical);
	EnhancedInput->BindAction(IA_Vertical, ETriggerEvent::Completed, this, &AStellarEraPlayerController::OnVertical);
```

把 `OnLook` 整个函数体替换成同时分派给飞船：

```cpp
void AStellarEraPlayerController::OnLook(const FInputActionValue& Value)
{
	const FVector2D Axis = Value.Get<FVector2D>();
	const float DeltaYaw = Axis.X * LookSensitivity;
	const float DeltaPitch = (bInvertLookY ? -Axis.Y : Axis.Y) * LookSensitivity;

	if (APlanetCharacter* Character = Cast<APlanetCharacter>(GetPawn()))
	{
		Character->Look(DeltaYaw, DeltaPitch);
	}
	else if (ASpaceshipPawn* Ship = Cast<ASpaceshipPawn>(GetPawn()))
	{
		Ship->Look(DeltaYaw, DeltaPitch);
	}
}
```

在文件末尾追加：

```cpp
void AStellarEraPlayerController::OnThrust(const FInputActionValue& Value)
{
	// Completed 事件里 Value 是 0，正好用来在松手时归零。
	if (ASpaceshipPawn* Ship = Cast<ASpaceshipPawn>(GetPawn()))
	{
		Ship->SetThrust(Value.Get<float>());
	}
}

void AStellarEraPlayerController::OnRoll(const FInputActionValue& Value)
{
	if (ASpaceshipPawn* Ship = Cast<ASpaceshipPawn>(GetPawn()))
	{
		Ship->SetRoll(Value.Get<float>());
	}
}

void AStellarEraPlayerController::OnVertical(const FInputActionValue& Value)
{
	if (ASpaceshipPawn* Ship = Cast<ASpaceshipPawn>(GetPawn()))
	{
		Ship->SetVertical(Value.Get<float>());
	}
}

void AStellarEraPlayerController::DebugPossessShip()
{
	// 已经在飞船里就换回角色，否则找一艘最近的飞船坐进去。
	if (Cast<ASpaceshipPawn>(GetPawn()))
	{
		for (TActorIterator<APlanetCharacter> It(GetWorld()); It; ++It)
		{
			Possess(*It);
			return;
		}
		return;
	}

	APawn* CurrentPawn = GetPawn();
	if (!CurrentPawn)
	{
		return;
	}

	ASpaceshipPawn* NearestShip = nullptr;
	float NearestDistSq = TNumericLimits<float>::Max();
	for (TActorIterator<ASpaceshipPawn> It(GetWorld()); It; ++It)
	{
		const float DistSq = FVector::DistSquared(It->GetActorLocation(), CurrentPawn->GetActorLocation());
		if (DistSq < NearestDistSq)
		{
			NearestDistSq = DistSq;
			NearestShip = *It;
		}
	}

	if (NearestShip)
	{
		Possess(NearestShip);
	}
}
```

- [ ] **Step 5: 补 UTF-8 BOM**

```bash
powershell -NoProfile -Command "foreach (\$p in @('F:\workspace\ue\StellarEra\Source\StellarEra\Public\SpaceshipPawn.h','F:\workspace\ue\StellarEra\Source\StellarEra\Private\SpaceshipPawn.cpp')) { \$c = Get-Content -Raw -Encoding UTF8 \$p; [IO.File]::WriteAllText(\$p, \$c, (New-Object Text.UTF8Encoding \$true)) }"
```

- [ ] **Step 6: 构建**

```bash
powershell -NoProfile -File "C:/Users/23080/AppData/Local/Temp/ue_mcp/ue_build.ps1"
```

期望：`Build succeeded`，退出码 0。

- [ ] **Step 7: 在 PIE 里验证**

加载 `UniverseMap`，点 Play。逐条核对：

1. 角色附近能看到一艘扁平的飞船（占位立方体），停靠在球面上方，机身"上"指向球心。
2. 控制台输入 `DebugPossessShip`，视角切到飞船。
3. 按 `W` 飞船加速前进；松手后平滑减速停下（不是立刻停）。
4. 快速甩一下鼠标，飞船开始转，**松手后还会继续转一小会儿再停**——这是角速度
   惯性的验收点。若松手立刻停，说明 `MouseImpulseScale` 或 `AngularDamping` 没生效。
5. 按 `D` / `A` 机身横滚；松开后机身**缓慢自动回正**。
6. 按 `Space` / `LeftControl` 上升 / 下降。
7. 用 `W` 直冲行星表面，飞船**被挡在球面外**，不会穿进球体。
8. 让飞船贴着球面停下（速度降到阈值以下），控制台输入
   `DebugPossessShip` 能换回角色。
9. Output Log 里不出现 ensure 或 `LogVoxel` 的 Error。

- [ ] **Step 8: 提交**

```bash
git add Source/StellarEra/Public/SpaceshipPawn.h \
        Source/StellarEra/Private/SpaceshipPawn.cpp \
        Source/StellarEra/Public/StellarEraGameMode.h \
        Source/StellarEra/Private/StellarEraGameMode.cpp \
        Source/StellarEra/Public/StellarEraPlayerController.h \
        Source/StellarEra/Private/StellarEraPlayerController.cpp
git commit -m "feat: 街机式太空飞船

位移交给 UFloatingPawnMovement，手感在 Tick 里实现：鼠标位移作为角速度
冲量累加，松手后指数衰减；横滚自动回正但不干预玩家给的俯仰。GameMode
在关卡没有飞船时自动补一艘。

Co-Authored-By: Claude Code <noreply@anthropic.com>"
```

---

## Task 4: 登船与离船

走近飞船出现提示、按 E 登船；飞船落地减速后按 E 离船。这是 spec 的验收标准 4 与 6。

**Files:**
- Modify: `Source/StellarEra/Public/StellarEraPlayerController.h`
- Modify: `Source/StellarEra/Private/StellarEraPlayerController.cpp`
- Modify: `Source/StellarEra/Public/PlanetCharacter.h`
- Modify: `Source/StellarEra/Private/PlanetCharacter.cpp`
- Modify: `Source/StellarEra/Public/SpaceshipPawn.h`
- Modify: `Source/StellarEra/Private/SpaceshipPawn.cpp`

**Interfaces:**
- Consumes: `ASpaceshipPawn::IsLanded()`（Task 3）、`IA_Interact`（Task 3）
- Produces:
  - `void APlanetCharacter::SetBoarded(bool bBoarded)`
  - `ASpaceshipPawn* APlanetCharacter::FindNearestShip() const`
  - `bool ASpaceshipPawn::CanBoardFrom(const FVector& CharacterLocation) const`
  - `void ASpaceshipPawn::SetBoardedCharacter(APlanetCharacter* Character)`
  - `APlanetCharacter* ASpaceshipPawn::GetBoardedCharacter() const`

---

- [ ] **Step 1: 给角色加登船/离船接口**

在 `Source/StellarEra/Public/PlanetCharacter.h` 的 `void SetSprinting(bool bSprinting);` **下面**插入：

```cpp
	/**
	 * 登船 / 离船。登船时隐藏并关掉碰撞——附着的 Actor 仍然参与碰撞，
	 * 不关的话角色的胶囊会卡在飞船的碰撞球里。
	 */
	void SetBoarded(bool bBoarded);

	/** 找离角色最近的飞船；找不到返回 nullptr。 */
	ASpaceshipPawn* FindNearestShip() const;
```

在 `PlanetCharacter.h` 顶部的前置声明区加上：

```cpp
class ASpaceshipPawn;
```

在 `Source/StellarEra/Private/PlanetCharacter.cpp` 顶部补 include：

```cpp
#include "SpaceshipPawn.h"
```

在 `PlanetCharacter.cpp` 末尾追加：

```cpp
void APlanetCharacter::SetBoarded(bool bBoarded)
{
	SetActorHiddenInGame(bBoarded);
	SetActorEnableCollision(!bBoarded);

	// 关掉移动，避免登船期间残留输入继续推动角色（它已经附着在飞船上了）。
	GetCharacterMovement()->StopMovementImmediately();
	GetCharacterMovement()->SetMovementMode(bBoarded ? MOVE_None : MOVE_Walking);

	if (!bBoarded)
	{
		// 离船后重新踩上球面：碰撞体可能已经因为距离太远被回收了。
		bHasLanded = false;
	}
}

ASpaceshipPawn* APlanetCharacter::FindNearestShip() const
{
	ASpaceshipPawn* Nearest = nullptr;
	float NearestDistSq = TNumericLimits<float>::Max();

	for (TActorIterator<ASpaceshipPawn> It(GetWorld()); It; ++It)
	{
		const float DistSq = FVector::DistSquared(It->GetActorLocation(), GetActorLocation());
		if (DistSq < NearestDistSq)
		{
			NearestDistSq = DistSq;
			Nearest = *It;
		}
	}
	return Nearest;
}
```

- [ ] **Step 2: 给飞船加登船判定与携带角色**

在 `Source/StellarEra/Public/SpaceshipPawn.h` 的 `bool IsLanded() const;` **下面**插入：

```cpp
	/** 角色是否近到可以登船。 */
	UFUNCTION(BlueprintPure, Category = "Spaceship")
	bool CanBoardFrom(const FVector& CharacterLocation) const;

	/** 记录登船的角色；传 nullptr 表示离船。 */
	void SetBoardedCharacter(APlanetCharacter* Character);

	/** 当前在船上的角色；没有则为 nullptr。 */
	APlanetCharacter* GetBoardedCharacter() const { return BoardedCharacter; }
```

在 `SpaceshipPawn.h` 顶部的前置声明区加上：

```cpp
class APlanetCharacter;
```

在 `SpaceshipPawn.h` 的 `float LandingProbeDistance = 600.f;` **下面**插入：

```cpp
	/** 登船触发半径（厘米）。 */
	UPROPERTY(EditAnywhere, Category = "Spaceship|Boarding")
	float BoardingRadius = 600.f;

private:
	/** 当前在船上的角色。 */
	UPROPERTY(Transient)
	TObjectPtr<APlanetCharacter> BoardedCharacter;
```

在 `Source/StellarEra/Private/SpaceshipPawn.cpp` 顶部补 include：

```cpp
#include "PlanetCharacter.h"
```

在 `SpaceshipPawn.cpp` 末尾追加：

```cpp
bool ASpaceshipPawn::CanBoardFrom(const FVector& CharacterLocation) const
{
	return FVector::DistSquared(CharacterLocation, GetActorLocation()) <= FMath::Square(BoardingRadius);
}

void ASpaceshipPawn::SetBoardedCharacter(APlanetCharacter* Character)
{
	BoardedCharacter = Character;
}
```

- [ ] **Step 3: 在 PlayerController 里实现登船/离船流程**

在 `Source/StellarEra/Public/StellarEraPlayerController.h` 的 `void DebugPossessShip();` **下面**插入：

```cpp
	virtual void PlayerTick(float DeltaTime) override;

	/** 按 E 时的分派：没在船上就尝试登船，在船上就尝试离船。 */
	void OnInteract(const FInputActionValue& Value);

	/** 尝试让角色登上指定飞船。 */
	void BoardShip(ASpaceshipPawn* Ship);

	/** 尝试从当前飞船下船。 */
	void LeaveShip(ASpaceshipPawn* Ship);

	/** 在屏幕上显示一行提示（每帧刷新，duration 0）。 */
	void ShowPrompt(const FString& Text);

	/** 触发登船提示的距离（厘米）。 */
	UPROPERTY(EditAnywhere, Category = "Boarding")
	float BoardingPromptRange = 600.f;
```

在 `StellarEraPlayerController.h` 顶部的前置声明区加上：

```cpp
class ASpaceshipPawn;
```

在 `Source/StellarEra/Private/StellarEraPlayerController.cpp` 顶部补 include：

```cpp
#include "Engine/Engine.h"
```

在 `SetupInputComponent()` 里追加绑定：

```cpp
	EnhancedInput->BindAction(IA_Interact, ETriggerEvent::Started, this, &AStellarEraPlayerController::OnInteract);
```

在文件末尾追加：

```cpp
void AStellarEraPlayerController::PlayerTick(float DeltaTime)
{
	Super::PlayerTick(DeltaTime);

	// 提示只在“站在飞船旁”时出现，所以每帧重算一次距离。
	if (APlanetCharacter* Character = Cast<APlanetCharacter>(GetPawn()))
	{
		ASpaceshipPawn* Ship = Character->FindNearestShip();
		if (Ship && Ship->CanBoardFrom(Character->GetActorLocation()))
		{
			ShowPrompt(TEXT("按 E 登船"));
		}
	}
	else if (ASpaceshipPawn* Ship = Cast<ASpaceshipPawn>(GetPawn()))
	{
		if (Ship->IsLanded())
		{
			ShowPrompt(TEXT("按 E 离船"));
		}
	}
}

void AStellarEraPlayerController::OnInteract(const FInputActionValue& Value)
{
	if (APlanetCharacter* Character = Cast<APlanetCharacter>(GetPawn()))
	{
		ASpaceshipPawn* Ship = Character->FindNearestShip();
		if (Ship && Ship->CanBoardFrom(Character->GetActorLocation()))
		{
			BoardShip(Ship);
		}
		return;
	}

	if (ASpaceshipPawn* Ship = Cast<ASpaceshipPawn>(GetPawn()))
	{
		// 空中不能下船：速度没降到阈值以下、或下方没有地面时 IsLanded 为假。
		if (Ship->IsLanded())
		{
			LeaveShip(Ship);
		}
	}
}

void AStellarEraPlayerController::BoardShip(ASpaceshipPawn* Ship)
{
	APlanetCharacter* Character = Cast<APlanetCharacter>(GetPawn());
	if (!Ship || !Character)
	{
		return;
	}

	// 先附着再隐藏：隐藏与关碰撞由角色自己负责，顺序反了会在附着那一刻
	// 因为碰撞被关掉而丢掉相对变换。
	Character->AttachToActor(Ship, FAttachmentTransformRules::KeepWorldTransform);
	Character->SetBoarded(true);
	Ship->SetBoardedCharacter(Character);

	Possess(Ship);
}

void AStellarEraPlayerController::LeaveShip(ASpaceshipPawn* Ship)
{
	APlanetCharacter* Character = Ship ? Ship->GetBoardedCharacter() : nullptr;
	if (!Ship || !Character)
	{
		return;
	}

	Character->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
	Character->SetBoarded(false);
	Ship->SetBoardedCharacter(nullptr);

	// 摆到飞船旁边而不是原地：原地正好在飞船的碰撞球里，会立刻被挤开。
	const FVector ExitLocation = Ship->GetActorLocation() + Ship->GetActorRightVector() * 400.f;
	Character->SetActorLocation(ExitLocation, /*bSweep=*/false);

	Possess(Character);
}

void AStellarEraPlayerController::ShowPrompt(const FString& Text)
{
	if (!GEngine)
	{
		return;
	}

	// duration 0 = 只显示这一帧。固定 key 让屏幕上永远只有一行，不会堆积。
	static const uint64 PromptMessageKey = 0x5745'4C43'4F4D'4521ULL;
	GEngine->AddOnScreenDebugMessage(PromptMessageKey, 0.f, FColor::Yellow, Text);
}
```

- [ ] **Step 4: 补 UTF-8 BOM**

本任务修改的都是已有文件，BOM 不受影响。用这条命令确认没有被意外清掉：

```bash
for f in Source/StellarEra/Public/*.h Source/StellarEra/Private/*.cpp; do printf "%s: " "$f"; head -c 3 "$f" | od -An -tx1 | tr -d ' \n'; echo; done
```

期望：每一行都是 `efbbbf`。若有文件不是，用 Step 1 里同样的 PowerShell 命令补上。

- [ ] **Step 5: 构建**

```bash
powershell -NoProfile -File "C:/Users/23080/AppData/Local/Temp/ue_mcp/ue_build.ps1"
```

期望：`Build succeeded`，退出码 0。

- [ ] **Step 6: 在 PIE 里验证**

加载 `UniverseMap`，点 Play。逐条核对：

1. 用 WASD 走到飞船旁，屏幕上出现黄色 `按 E 登船`。
2. 走远后提示消失。
3. 按 `E`：角色消失，视角切到飞船的追尾相机。
4. 飞起来后按 `E` **没有反应**（空中不能下船，`IsLanded` 为假）。
5. 飞回球面、减速停稳，屏幕出现 `按 E 离船`。
6. 按 `E`：角色出现在飞船**旁边**（不是船里），可以继续用 WASD 行走。
7. 重复登船/离船 3 次，不出现角色卡在船里、相机错乱或 ensure。
8. 离船后角色不会掉穿地面（`bHasLanded` 被重置，会重新贴合球面）。

- [ ] **Step 7: 提交**

```bash
git add Source/StellarEra/Public/StellarEraPlayerController.h \
        Source/StellarEra/Private/StellarEraPlayerController.cpp \
        Source/StellarEra/Public/PlanetCharacter.h \
        Source/StellarEra/Private/PlanetCharacter.cpp \
        Source/StellarEra/Public/SpaceshipPawn.h \
        Source/StellarEra/Private/SpaceshipPawn.cpp
git commit -m "feat: 登船与离船流程

PlayerController 每帧按距离给出提示，E 键分派登船/离船。登船时角色隐藏、
关碰撞并附着到飞船；离船时摆到飞船旁边，避免生成在飞船的碰撞球内部。
空中不允许下船。

Co-Authored-By: Claude Code <noreply@anthropic.com>"
```

---

## Task 5: 模型导入与挂接

把 Fab 下载的两个模型导入工程，替换掉占位网格。

**Files:**
- Create: `ArtSource/`（工程外的原始文件存放目录，不纳入 git）
- Modify: `Source/StellarEra/Private/PlanetCharacter.cpp`
- Modify: `Source/StellarEra/Public/PlanetCharacter.h`
- Modify: `Source/StellarEra/Private/SpaceshipPawn.cpp`
- Modify: `Source/StellarEra/Public/SpaceshipPawn.h`

**Interfaces:**
- Consumes: `APlanetCharacter`（Task 1）、`ASpaceshipPawn`（Task 3）
- Produces: 无新接口，只替换网格资源与新增朝向/缩放调节属性

---

- [ ] **Step 1: 把原始模型复制出临时目录**

两个 `.glb` 现在在 Fab 的**临时**目录里（`C:\Users\23080\AppData\Local\Temp\FabLibrary\<guid>\`），
系统清理时会删掉。先复制到工程外的 `ArtSource\`：

```bash
mkdir -p /f/workspace/ue/StellarEra/ArtSource
cp "/c/Users/23080/AppData/Local/Temp/FabLibrary/"*/pomni_3d_model.glb /f/workspace/ue/StellarEra/ArtSource/
cp "/c/Users/23080/AppData/Local/Temp/FabLibrary/"*/12b5_atmospherhuttle002.glb /f/workspace/ue/StellarEra/ArtSource/
ls -la /f/workspace/ue/StellarEra/ArtSource/
```

期望：列出两个文件，`pomni_3d_model.glb` 约几十 MB，`12b5_atmospherhuttle002.glb` 约 149 MB。
若 `cp` 报找不到文件，说明 Temp 已经被清理，需要重新从 Fab 下载。

确认 `ArtSource/` 不被 git 跟踪：

```bash
git status --porcelain | grep ArtSource || echo "ArtSource 未被跟踪（正确）"
```

若被跟踪，把 `ArtSource/` 加进 `.gitignore`。

- [ ] **Step 2: 在编辑器里导入（手动步骤）**

这一步需要编辑器 UI，无法自动化：

1. 打开编辑器，在 Content Browser 里右键 → New Folder，建 `Characters` 和 `Vehicles` 两个目录。
2. 把 `ArtSource\pomni_3d_model.glb` 从资源管理器拖进 `Content/Characters/`。
   导入选项里 **Skeletal Mesh 勾选 Import，Import Animations 不勾**（模型没有动画）。
3. 把 `ArtSource\12b5_atmospherhuttle002.glb` 拖进 `Content/Vehicles/`。
   导入选项里确认 **Import As Skeletal 不勾**（静态网格）。
4. 记录导入后的资产路径（Content Browser 里右键 → Copy Reference）。

- [ ] **Step 3: 给角色加朝向/缩放调节属性**

Sketchfab 导出的模型常见 100 倍缩放与 Z-up 朝向问题。与其反复改代码重编译，
不如把校正量做成属性，在细节面板里调。

在 `Source/StellarEra/Public/PlanetCharacter.h` 的 `float SprintSpeed = 1200.f;` **下面**插入：

```cpp
	/** 模型相对胶囊的额外旋转。用于校正 Sketchfab 模型的朝向。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Character|Mesh")
	FRotator MeshRelativeRotation = FRotator(0.f, -90.f, 0.f);

	/** 模型相对胶囊的额外缩放。用于校正 Sketchfab 模型的 100 倍缩放。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Character|Mesh")
	float MeshScale = 0.7f;
```

- [ ] **Step 4: 把角色的占位网格换成真实模型**

在 `Source/StellarEra/Private/PlanetCharacter.cpp` 的构造函数里，**替换**这一段：

```cpp
	// 占位网格：随引擎发布的立方体，保证没有美术资源时角色也看得见。
	// 导入真实模型后替换这里的路径（见 Task 5）。
	static ConstructorHelpers::FObjectFinder<USkeletalMesh> PlaceholderMesh(
		TEXT("/Engine/EngineMeshes/SkeletalCube.SkeletalCube"));
	if (PlaceholderMesh.Succeeded())
	{
		GetMesh()->SetSkeletalMesh(PlaceholderMesh.Object);
	}
	// 胶囊半高 88：把网格下移到胶囊底，让立方体站在地面上而不是穿进地里。
	GetMesh()->SetRelativeLocation(FVector(0.f, 0.f, -88.f));
	GetMesh()->SetRelativeScale3D(FVector(0.7f));
```

换成（**把路径换成 Step 2 里记录的实际路径**）：

```cpp
	// Pomni 模型。Sketchfab 导出的朝向与缩放通常都不对，校正量走
	// MeshRelativeRotation / MeshScale 两个属性，在细节面板里调，不必重编译。
	static ConstructorHelpers::FObjectFinder<USkeletalMesh> CharacterMesh(
		TEXT("/Game/Characters/pomni_3d_model.pomni_3d_model"));
	if (CharacterMesh.Succeeded())
	{
		GetMesh()->SetSkeletalMesh(CharacterMesh.Object);
	}
	else
	{
		// 资源缺失时退回引擎自带立方体，保证角色仍然可见、不静默消失。
		static ConstructorHelpers::FObjectFinder<USkeletalMesh> FallbackMesh(
			TEXT("/Engine/EngineMeshes/SkeletalCube.SkeletalCube"));
		if (FallbackMesh.Succeeded())
		{
			GetMesh()->SetSkeletalMesh(FallbackMesh.Object);
		}
		UE_LOG(LogTemp, Warning, TEXT("APlanetCharacter: 找不到角色模型，退回占位网格。"));
	}

	// 胶囊半高 88：把网格下移到胶囊底，让模型站在地面上而不是穿进地里。
	GetMesh()->SetRelativeLocation(FVector(0.f, 0.f, -88.f));
	GetMesh()->SetRelativeRotation(MeshRelativeRotation);
	GetMesh()->SetRelativeScale3D(FVector(MeshScale));
```

- [ ] **Step 5: 把飞船的占位网格换成真实模型**

在 `Source/StellarEra/Public/SpaceshipPawn.h` 的 `float BoardingRadius = 600.f;` **下面**插入
（`BoardingRadius` 是 Task 4 加的；若你的文件里顺序不同，插在任一 `Spaceship|Mesh` 属性旁即可）：

```cpp
	/** 模型相对碰撞球的额外旋转。用于校正模型朝向。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spaceship|Mesh")
	FRotator MeshRelativeRotation = FRotator::ZeroRotator;

	/** 模型相对碰撞球的额外缩放。用于把模型缩放到 6 米左右。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spaceship|Mesh")
	float MeshScale = 1.f;
```

在 `Source/StellarEra/Private/SpaceshipPawn.cpp` 的构造函数里，**替换**这一段：

```cpp
	Hull->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Hull->SetRelativeScale3D(FVector(6.f, 2.f, 1.f));

	// 占位外壳：随引擎发布的立方体。导入真实模型后替换这里的路径（见 Task 5）。
	static ConstructorHelpers::FObjectFinder<UStaticMesh> PlaceholderHull(
		TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (PlaceholderHull.Succeeded())
	{
		Hull->SetStaticMesh(PlaceholderHull.Object);
	}
```

换成（**把路径换成 Step 2 里记录的实际路径**）：

```cpp
	Hull->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> ShipMesh(
		TEXT("/Game/Vehicles/12b5_atmospherhuttle002.12b5_atmospherhuttle002"));
	if (ShipMesh.Succeeded())
	{
		Hull->SetStaticMesh(ShipMesh.Object);
	}
	else
	{
		// 资源缺失时退回引擎立方体，保证飞船仍然可见、不静默消失。
		static ConstructorHelpers::FObjectFinder<UStaticMesh> FallbackHull(
			TEXT("/Engine/BasicShapes/Cube.Cube"));
		if (FallbackHull.Succeeded())
		{
			Hull->SetStaticMesh(FallbackHull.Object);
		}
		UE_LOG(LogTemp, Warning, TEXT("ASpaceshipPawn: 找不到飞船模型，退回占位网格。"));
	}

	Hull->SetRelativeRotation(MeshRelativeRotation);
	Hull->SetRelativeScale3D(FVector(MeshScale));
```

- [ ] **Step 6: 补 UTF-8 BOM**

```bash
for f in Source/StellarEra/Public/*.h Source/StellarEra/Private/*.cpp; do printf "%s: " "$f"; head -c 3 "$f" | od -An -tx1 | tr -d ' \n'; echo; done
```

期望：每一行都是 `efbbbf`。

- [ ] **Step 7: 构建**

```bash
powershell -NoProfile -File "C:/Users/23080/AppData/Local/Temp/ue_mcp/ue_build.ps1"
```

期望：`Build succeeded`，退出码 0。

- [ ] **Step 8: 在 PIE 里验证**

加载 `UniverseMap`，点 Play。逐条核对：

1. 角色显示为 Pomni 模型而不是立方体，站在球面上。
2. 模型的朝向正确——面朝角色的前进方向，不是侧躺或倒立。
3. 模型大小合理——大约两个人高，不是蚂蚁也不是巨人。
4. 飞船显示为航天飞机模型而不是立方体，大小与角色比例协调。
5. Output Log 里**不出现** `找不到角色模型` / `找不到飞船模型` 的 Warning。
6. 若第 2/3/4 条不对：选中关卡里的角色/飞船，在细节面板的
   `Character|Mesh` / `Spaceship|Mesh` 下调 `Mesh Relative Rotation` 与
   `Mesh Scale`，**不需要重编译**，调完重新 Play 即可。
7. 重新过一遍 Task 4 Step 6 的登船/离船清单，确认换模型没有破坏流程。

- [ ] **Step 9: 提交**

```bash
git add Source/StellarEra/Public/PlanetCharacter.h \
        Source/StellarEra/Private/PlanetCharacter.cpp \
        Source/StellarEra/Public/SpaceshipPawn.h \
        Source/StellarEra/Private/SpaceshipPawn.cpp \
        Content/Characters Content/Vehicles
git commit -m "feat: 接入 Pomni 角色与航天飞机模型

模型朝向与缩放做成 UPROPERTY，Sketchfab 导出的常见问题可以在细节面板
里调，不必重编译。资源缺失时退回引擎占位网格并打 Warning，不静默消失。

Co-Authored-By: Claude Code <noreply@anthropic.com>"
```

---

## 验收清单（全部任务完成后跑一遍）

对应 spec §8：

- [ ] 1. 角色出生在球面上，不掉穿地面，能在球面任意方向行走；走到"另一面"时角色仍直立（脚朝球心）。
- [ ] 2. 按 Space 能跳跃并落回地面，不穿模。
- [ ] 3. 相机跟随角色，第三人称，不穿地（弹簧臂开了 `bDoCollisionTest`）。
- [ ] 4. 走近飞船出现"按 E 登船"提示，按 E 后 Possess 飞船，角色消失。
- [ ] 5. 飞船能起飞、自由飞行，鼠标转向有角速度惯性，松手会衰减，机身会自动回正。
- [ ] 6. 飞船降落到星球表面并减速后出现"按 E 离船"，按 E 后角色出现在飞船旁并可继续行走。
- [ ] 7. 全程无崩溃、无 ensure、无 `LogVoxel` 错误。

---

## 本计划相对 spec 的实现级修正

写计划时读引擎源码发现的、与 spec 文字不一致之处。行为目标不变，实现方式更可靠。

| # | spec 原文 | 实际做法 | 依据 |
|---|---|---|---|
| 1 | §5.1 步骤 3"手动用 `FQuat::FindBetweenNormals` + `RInterpTo` 对齐朝向，必须插值" | **删掉**。改回 `bOrientRotationToMovement = true`，由引擎做 | `PhysicsRotation` 已在重力空间里清零俯仰/横滚并按 `RotationRate` 插值（`CharacterMovementComponent.cpp:6673-6680`、`6711-6728`）。手动插值会和它打架 |
| 2 | §4 架构只列 3 个类 | 增加 `AStellarEraGameMode`（第 4 个） | 项目没有配 GameMode，也没有 PlayerStart。不设 `DefaultPawnClass` 则 PIE 生成 `ADefaultPawn` |
| 3 | 未提及出生点 | `BeginPlay` 把角色摆到球面正上方，落地前每帧重贴 | 关卡里没有 PlayerStart，默认出生点是世界原点——正好在球心内部 |
| 4 | §5.1"`USpringArmComponent` + `UCameraComponent`，第三人称" | 增加一个 `CameraPivot` 中间组件，相机走自建枢轴 | `ControlRotation` 是世界空间的（`Pawn.cpp:360-363`），在球体背面绕世界 Z 转会让画面翻滚 |
| 5 | §3.2 写的是 `UVoxelInvokerComponent` | 实际类名是 `UVoxelSimpleInvokerComponent` | 插件里不存在 `UVoxelInvokerComponent` 这个类 |
| 6 | §5.4"在 PlayerController 的 BeginPlay 里构造" | 构造在 `SetupInputComponent`（`BeginPlay` 兜底），并新增 `Config/DefaultInput.ini` | `APlayerController::InitInputSystem` 用 `UInputSettings::GetDefaultInputComponentClass()`（`PlayerController.cpp:2654`），不配 ini 会拿到普通 `UInputComponent` |
| 7 | 未提及 | 飞船的自动回正**只回正横滚**，不干预玩家给的俯仰 | 俯仰也回正会和玩家的转向输入持续对抗，手感拧巴 |
