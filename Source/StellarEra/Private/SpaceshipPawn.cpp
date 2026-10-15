// Copyright Voxel Plugin SAS. All Rights Reserved.

#include "SpaceshipPawn.h"

#include "VoxelPlanetActor.h"
#include "PlanetCharacter.h"
#include "Camera/CameraComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/FloatingPawnMovement.h"
#include "GameFramework/SpringArmComponent.h"
#include "UObject/ConstructorHelpers.h"
#include "EngineUtils.h"
#include "VoxelComponents/VoxelInvokerComponent.h"

namespace
{
	// 占位立方体的资产路径与比例，只在构造函数里用：裸放的 ASpaceshipPawn
	// （没有蓝图填 HullMesh）也得看得见一个大致像机身的东西。
	const TCHAR* FallbackHullPath = TEXT("/Engine/BasicShapes/Cube.Cube");

	// 100cm 见方的立方体缩在 300cm 的碰撞球里几乎看不见，撑成 600×200×100
	// 才像个机身。这个比例只对占位立方体有意义 —— 蓝图里覆盖过 Hull 的缩放后
	// 以蓝图为准（子蓝图的覆盖值在构造函数之后才套用）。
	const FVector FallbackHullScale(6.f, 2.f, 1.f);
}

ASpaceshipPawn::ASpaceshipPawn()
{
	PrimaryActorTick.bCanEverTick = true;

	Collision = CreateDefaultSubobject<USphereComponent>(TEXT("Collision"));
	Collision->InitSphereRadius(300.f);
	Collision->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Collision->SetCollisionObjectType(ECC_Pawn);
	Collision->SetCollisionResponseToAllChannels(ECR_Block);
	SetRootComponent(Collision);

	// --- 体素调用器 ---
	//
	// 挂在根节点而不是机头：调用器的位置同时决定 LOD 范围和碰撞范围，碰撞范围
	// 必须罩住飞船自身的物理体，否则飞机会撞进还没生成碰撞体的地形里。
	//
	// 用带预测的版本，而不是角色那种 UVoxelSimpleInvokerComponent：巡航速度
	// 8000 cm/s 而范围只有 200 m，不预测就只剩 2.5 秒的提前量，地形还没网格化完
	// 人已经撞上去了。预测把采样点沿速度方向前移 PredictionTime 秒
	// （UVoxelInvokerWithPredictionComponent::GetInvokerGlobalPosition），凭空
	// 多出 80 m 预警，且不额外扩大细分区域。停稳时速度为零，退化成普通调用器。
	//
	// 数值与角色一致（见 PlanetCharacter.cpp）：LODToSet = 0 是插件对「贴着表面的
	// Pawn」的约定用法，200 m 是本项目已经在跑的量级。范围留在组件面板上可调 ——
	// 嫌地形追不上就同时调大 LODRange 与 CollisionsRange（两个都要，LOD 只影响
	// 画质，碰撞由 CollisionsRange 单独驱动），或把 bUseForPriorities 打开让
	// 飞船附近的区块优先网格化。
	VoxelInvoker = CreateDefaultSubobject<UVoxelInvokerWithPredictionComponent>(TEXT("VoxelInvoker"));
	VoxelInvoker->SetupAttachment(Collision);
	VoxelInvoker->bUseForLOD = true;
	VoxelInvoker->LODToSet = 0;
	VoxelInvoker->LODRange = 20000.f;
	VoxelInvoker->bUseForCollisions = true;
	VoxelInvoker->CollisionsRange = 20000.f;
	VoxelInvoker->bUseForNavmesh = false;
	VoxelInvoker->bUseForEvents = false;
	VoxelInvoker->bUseForPriorities = false;
	VoxelInvoker->bEnablePrediction = true;
	VoxelInvoker->PredictionTime = 1.f;

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

	// C++ 只给一个引擎立方体兜底，让裸放的 ASpaceshipPawn 不至于隐形。
	//
	// 真实模型不在这里。它属于「具体表现」，由子蓝图 BP_SpaceshipPawn 提供：
	// 换模型用 HullMesh 属性（或直接覆盖 Hull 的静态网格体），摆位置/朝向/缩放
	// 直接在蓝图视口里拖 Hull —— C++ 之后不再写 Hull 的任何变换，拖出来的值
	// 才不会被每次编译冲掉。
	//
	// 也正因为如此，这里不能写项目资产路径：蓝图 CDO 构造时会先跑一遍这个
	// 构造函数，之后才加载蓝图里存的值，构造期读到的永远是 C++ 默认值。
	// 真正套用 HullMesh 的地方是 ApplyHullMesh。
	static ConstructorHelpers::FObjectFinder<UStaticMesh> FallbackHull(FallbackHullPath);
	if (FallbackHull.Succeeded())
	{
		Hull->SetStaticMesh(FallbackHull.Object);
		Hull->SetRelativeScale3D(FallbackHullScale);
	}
	else
	{
		// 连引擎立方体都拿不到时会得到一个没有网格、完全不可见的空 Actor，
		// 看上去就像"飞船没生成"。宁可吵一句，也不要静默失败。
		UE_LOG(LogTemp, Warning,
			TEXT("ASpaceshipPawn: 找不到占位立方体 %s，Hull 将没有网格。"), FallbackHullPath);
	}
}

void ASpaceshipPawn::ApplyHullMesh()
{
	// 只换网格，不碰变换。
	//
	// 位置/朝向/缩放全部归蓝图：在蓝图视口里选中 Hull 拖 Gizmo，值就存在蓝图
	// 资产里（关卡实例上也能单独调）。这里每编译一次都会被 OnConstruction 重跑，
	// 若在此写变换，蓝图里拖好的值会被反复冲回去，表现就是"模型怎么都调不动"。
	if (Hull && HullMesh)
	{
		Hull->SetStaticMesh(HullMesh);
	}

	// HullMesh 为空：什么都不做，而不是退回占位立方体。
	//
	// 空的语义是「网格体交给蓝图自己决定」—— 蓝图里覆盖的模型、或构造期塞进去的
	// 占位立方体都保持原样。这里若强行重置，蓝图里选好的模型会在每次编译时被
	// 清掉，那是比"属性没生效"更难查的坑。
}

void ASpaceshipPawn::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	// 网格体在这里套用，而不是构造函数里：OnConstruction 是编辑器里能读到蓝图
	// 覆盖值的第一个时机，在蓝图 Class Defaults 里改 HullMesh 立刻能在视口看到。
	// 这里只换网格 —— 位置/朝向/缩放归蓝图，见 ApplyHullMesh 的注释。
	ApplyHullMesh();

#if WITH_EDITOR
	// 让飞船退出 World Partition 的空间加载。
	//
	// 按空间加载的 Actor 会被烘进流送单元，PIE 启动后随流送被卸载。实测开局登船那
	// 5 秒重试窗口里 FindNearestShip 一艘船都找不到（同一时刻关卡里明明摆着一艘），
	// 于是玩家拿到的是角色而不是飞船。行星 Actor 早就用同样的办法退出了流送
	// （见 AVoxelPlanetActor::OnConstruction 的 bAlwaysLoaded），这里保持一致。
	//
	// CanChangeIsSpatiallyLoadedFlag 在 Actor 已经烘进流送单元后会返回 false，
	// 那种情况只能由人在细节面板里手动改，这里不做无声的失败重试。
	if (GetIsSpatiallyLoaded() && CanChangeIsSpatiallyLoadedFlag())
	{
		Modify();
		SetIsSpatiallyLoaded(false);
	}
#endif
}

void ASpaceshipPawn::BeginPlay()
{
	Super::BeginPlay();

	// 再套一次：关卡里实例化出来的蓝图可能覆盖过 HullMesh，而实例的
	// OnConstruction 未必在属性就位之后跑过。重复调用是幂等的（只换网格）。
	ApplyHullMesh();

	// 自动回正需要一个重力参考：贴着行星时对齐到球面法线，太空中对齐到世界 Z。
	PlanetActor = FindNearestPlanet();
}

AVoxelPlanetActor* ASpaceshipPawn::FindNearestPlanet() const
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

FVector ASpaceshipPawn::GetAutoLevelUp() const
{
	if (PlanetActor)
	{
		const FVector Radial =
			(GetActorLocation() - PlanetActor->GetActorLocation()).GetSafeNormal();
		if (!Radial.IsNearlyZero())
		{
			return Radial;
		}
	}
	return FVector::UpVector;
}

void ASpaceshipPawn::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// 行星在动，最近的一颗会换人。飞船不需要像角色那样重新摆位 ——
	// PlanetActor 只喂给 GetAutoLevelUp()，换个参考方向就行。
	if (PlanetReevaluateInterval > 0.f)
	{
		PlanetReevaluateTimer -= DeltaSeconds;
		if (PlanetReevaluateTimer <= 0.f)
		{
			PlanetReevaluateTimer = PlanetReevaluateInterval;
			PlanetActor = FindNearestPlanet();
		}
	}

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
		// 参考“上”投影到垂直于机身前向的平面，就是机身“上”应该朝向的方向。
		// 参考方向贴着行星时是球面法线，太空中才是世界 Z —— 若一律用世界 Z，
		// 飞船在赤道附近会直接滚转 90°，落地射线随之指偏，IsLanded() 永远为假。
		FVector DesiredUp = FVector::VectorPlaneProject(GetAutoLevelUp(), Forward);
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
	// 登船角色附着在飞船附近。它现在关着碰撞，但将来一旦因为交互、布娃娃之类重新打开，
	// 这条射线会一直打到它，IsLanded() 就永远为真、飞到空中也提示离船。先排除掉。
	if (BoardedCharacter)
	{
		Params.AddIgnoredActor(BoardedCharacter);
	}
	FHitResult Hit;
	return GetWorld()->LineTraceSingleByChannel(Hit, Start, End, ECC_WorldStatic, Params);
}

bool ASpaceshipPawn::CanBoardFrom(const FVector& CharacterLocation) const
{
	return FVector::DistSquared(CharacterLocation, GetActorLocation()) <= FMath::Square(BoardingRadius);
}

void ASpaceshipPawn::SetBoardedCharacter(APlanetCharacter* Character)
{
	BoardedCharacter = Character;
}
