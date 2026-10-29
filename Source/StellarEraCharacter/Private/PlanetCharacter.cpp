// Copyright Voxel Plugin SAS. All Rights Reserved.

#include "PlanetCharacter.h"

#include "PlanetCharacterMovementComponent.h"
#include "PlanetEditComponent.h"
#include "VoxelPlanetActor.h"
#include "AlwaysLocalVoxelInvokerComponent.h"
#include "VoxelViewpointInvokers.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerStart.h"
#include "GameFramework/SpringArmComponent.h"
#include "Animation/AnimInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "UObject/ConstructorHelpers.h"
#include "EngineUtils.h"
#include "CollisionQueryParams.h"
#include "Engine/World.h"

namespace
{
	// 占位网格的资产路径。构造函数和 ApplyCharacterMesh 都要用，写在一处，
	// 免得两边的字符串对不上。
	const TCHAR* FallbackMeshPath = TEXT("/Engine/EngineMeshes/SkeletalCube.SkeletalCube");

	/**
	 * 角色的「屏幕射线 → 编辑目标」：普通碰撞射线。
	 *
	 * 角色站在地表、射线只打一千米，LineTrace 最直接：它天然尊重体素碰撞，
	 * 也天然被地形挡住。星图不能这么做 —— 那里离地表几千公里，碰撞范围只有
	 * 几十米，射线什么也打不到。这正是把解析器做成可注入的原因。
	 *
	 * 逐字保留旧 TraceEditTarget 的语义：Visibility 优先，整条射线打空才退到
	 * WorldStatic；命中的 Actor 必须是脚下那颗行星。
	 */
	bool TraceTargetForCharacter(
		APlanetCharacter& Character,
		AVoxelPlanetActor* Planet,
		const FVector& RayOrigin,
		const FVector& RayDirection,
		const float MaxDistanceCm,
		FPlanetEditTarget& OutTarget)
	{
		if (!IsValid(Planet) || !Planet->IsCreated())
		{
			return false;
		}

		const UWorld* World = Character.GetWorld();
		if (!World)
		{
			return false;
		}

		FCollisionQueryParams Params(
			SCENE_QUERY_STAT(PlanetEditBrush), /*bTraceComplex=*/true);
		Params.AddIgnoredActor(&Character);

		const FVector End = RayOrigin + RayDirection * MaxDistanceCm;

		FHitResult Hit;
		if (!World->LineTraceSingleByChannel(Hit, RayOrigin, End, ECC_Visibility, Params))
		{
			Hit = FHitResult();
			if (!World->LineTraceSingleByChannel(Hit, RayOrigin, End, ECC_WorldStatic, Params))
			{
				return false;
			}
		}

		if (Hit.GetActor() != Planet)
		{
			return false;
		}

		OutTarget.Planet = Planet;
		OutTarget.HitLocation = Hit.Location;
		OutTarget.HitNormal = Hit.ImpactNormal;
		return true;
	}
}

// SetDefaultSubobjectClass 必须写在初始化列表里：UObjectGlobals.h:1729 明确 assert
// 「不能在构造函数体内调用」。ACharacter 的构造函数会用 CharacterMovementComponentName
// 这个名字建 CMC，在这里先把它换成我们自己的子类，Super 跑完 GetCharacterMovement()
// 拿到的就是 UPlanetCharacterMovementComponent。
APlanetCharacter::APlanetCharacter(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.SetDefaultSubobjectClass<UPlanetCharacterMovementComponent>(
		ACharacter::CharacterMovementComponentName))
{
	PrimaryActorTick.bCanEverTick = true;

	GetCapsuleComponent()->InitCapsuleSize(42.f, 88.f);

	// 朝向交给 CMC：PhysicsRotation 会把目标旋转换算到重力空间、在那里清零
	// 俯仰与横滚再换回世界空间，因此角色天然垂直于球面。自己再插值一次只会
	// 和它打架。这里把控制器的旋转影响全部关掉，让引擎独占朝向。
	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = false;
	bUseControllerRotationRoll = false;

	// 局部变量不能叫 CharacterMovement：那是 ACharacter 的成员，UE 把变量遮蔽
	// 当错误（C4458）。
	UCharacterMovementComponent* Movement = GetCharacterMovement();
	Movement->bOrientRotationToMovement = true;
	Movement->RotationRate = FRotator(0.f, 540.f, 0.f);
	Movement->JumpZVelocity = 600.f;
	Movement->AirControl = 0.35f;
	Movement->MaxWalkSpeed = WalkSpeed;
	Movement->MaxStepHeight = 60.f;
	// 球面是连续曲面，落地判定要给一点容差，否则下坡时会被判成“悬空”而抖。
	Movement->PerchRadiusThreshold = 0.f;
	Movement->LedgeCheckThreshold = 4.f;

	// 关掉「离开底座时把底座自己的速度加进角色 Velocity」。
	//
	// CMC 默认在每次 Walking→Falling 时调 ApplyImpartedMovementBaseVelocity()
	// （CharacterMovementComponent.cpp:1463 → :2747），而它加的就是底座的速度
	// （同文件 :1202 MovementBaseUtility::GetMovementBaseVelocity）。行星速度是 km/s，
	// 角色一离地就被发射出去。而且 FormerBaseVelocityDecayHalfLife 默认 0（同文件
	// CharacterMovementComponent.h:1092），注入值不衰减，直接变成角色自己的速度。
	//
	// 四个开关是 UPROPERTY(EditAnywhere, BlueprintReadWrite) 且在 public 段
	// （CharacterMovementComponent.h:928-954），这里关掉之后
	// GetImpartedMovementBaseVelocity() 恒返回零向量。
	//
	// 注意：走路、摩擦、地板判定、台阶、跳跃都不走这条路，关掉它们不受影响。
	Movement->bImpartBaseVelocityX = false;
	Movement->bImpartBaseVelocityY = false;
	Movement->bImpartBaseVelocityZ = false;
	Movement->bImpartBaseAngularVelocity = false;

	CameraPivot = CreateDefaultSubobject<USceneComponent>(TEXT("CameraPivot"));
	CameraPivot->SetupAttachment(GetCapsuleComponent());
	// 位置跟随胶囊，旋转必须**不**跟随。枢轴的旋转是 Tick 每帧按球面法线写死的
	// 世界旋转，而 SetWorldRotation 存下来的是「相对父级」的旋转；角色一拐弯
	// （bOrientRotationToMovement 转的是整个 Actor，胶囊跟着转），父级随后转动，
	// 枢轴的世界旋转就被带着一起甩 —— 表现为走动拐弯时画面跟着晃。
	// 绝对旋转让 CalcNewComponentToWorld 直接取枢轴自己的旋转，父级转多少都不影响。
	CameraPivot->SetUsingAbsoluteRotation(true);

	SpringArm = CreateDefaultSubobject<USpringArmComponent>(TEXT("SpringArm"));
	SpringArm->SetupAttachment(CameraPivot);
	// 相机由 CameraPivot 驱动，绝不走 ControlRotation：那是世界空间的。
	SpringArm->bUsePawnControlRotation = false;
	// bInheritPitch/Yaw/Roll 必须保持引擎默认的 true。置 false 并不是“忽略父级”，
	// 而是拿弹簧臂自己的局部相对旋转覆盖该轴——那是单位阵，DesiredRot 会被清零成
	// 世界单位旋转，CameraPivot 每帧算出的朝向被完全抹掉，相机永远盯着世界 +X。
	SpringArm->TargetArmLength = CameraArmLength;
	SpringArm->SocketOffset = FVector(0.f, 0.f, 120.f);
	SpringArm->bDoCollisionTest = true;

	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(SpringArm, USpringArmComponent::SocketName);
	Camera->bUsePawnControlRotation = false;

	// 用 AlwaysLocal 版：进星图时控制器 Possess 星图相机，普通调用器会当场掉出
	// LOD（IsLocalInvoker 只认本地控制的 Pawn），触发整棵八叉树重建。详见
	// AlwaysLocalVoxelInvokerComponent.h。
	VoxelInvoker = CreateDefaultSubobject<UAlwaysLocalVoxelInvokerComponent>(TEXT("VoxelInvoker"));
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

	PlanetEdit = CreateDefaultSubobject<UPlanetEditComponent>(TEXT("PlanetEdit"));

	// C++ 只给一个引擎骷髅方块兜底，让裸放的 APlanetCharacter 不至于隐形。
	//
	// 真实模型不在这里。它属于「具体表现」，由子蓝图 BP_PlanetCharacter 在
	// Class Defaults 里填 CharacterMesh / MeshRelativeLocation /
	// MeshRelativeRotation / MeshScale 提供，换模型、调朝向都不必重新编译。
	//
	// 也正因为如此，这里不能写项目资产路径：蓝图 CDO 构造时会先跑一遍这个
	// 构造函数，之后才加载蓝图里存的值，构造期读到的永远是 C++ 默认值。
	// 真正套用那四个属性的地方是 ApplyCharacterMesh。
	static ConstructorHelpers::FObjectFinder<USkeletalMesh> FallbackMesh(FallbackMeshPath);
	if (FallbackMesh.Succeeded())
	{
		GetMesh()->SetSkeletalMesh(FallbackMesh.Object);
	}
	else
	{
		// 连引擎骷髅方块都拿不到时会得到一个没有网格、完全不可见的角色。
		// 宁可吵一句，也不要静默失败。
		UE_LOG(LogTemp, Warning,
			TEXT("APlanetCharacter: 找不到占位网格 %s，Mesh 将没有模型。"), FallbackMeshPath);
	}
}

UPlanetEditComponent* APlanetCharacter::GetPlanetEditComponent() const
{
	return PlanetEdit;
}

void APlanetCharacter::ApplyCharacterMesh()
{
	USkeletalMeshComponent* MeshComponent = GetMesh();
	if (!MeshComponent)
	{
		return;
	}

	// 有就用蓝图给的，没有就退回占位网格。
	//
	// 这里不能直接 return —— 在编辑器里把 CharacterMesh 清空后，上一次套用的模型
	// 会留在 Mesh 上，看起来像"属性没生效"。显式放回占位网格才是所见即所得。
	USkeletalMesh* MeshToUse = CharacterMesh
		? CharacterMesh.Get()
		: LoadObject<USkeletalMesh>(nullptr, FallbackMeshPath);

	MeshComponent->SetSkeletalMesh(MeshToUse);
	MeshComponent->SetRelativeLocation(MeshRelativeLocation);
	MeshComponent->SetRelativeRotation(MeshRelativeRotation);
	MeshComponent->SetRelativeScale3D(FVector(MeshScale));

	// 动画类也在这里套用：它与上面四个属性一样是 EditAnywhere，构造期读不到蓝图值。
	// SetAnimClass 在 5.5 起已弃用，用 SetAnimInstanceClass。
	MeshComponent->SetAnimInstanceClass(CharacterAnimClass.Get());
}

void APlanetCharacter::AttachFaceMeshesToHead()
{
	USkeletalMeshComponent* BodyMesh = GetMesh();
	const USkeletalMesh* BodyAsset =
		BodyMesh ? BodyMesh->GetSkeletalMeshAsset() : nullptr;
	const USkeleton* BodySkeleton = BodyAsset ? BodyAsset->GetSkeleton() : nullptr;
	if (!BodySkeleton || FaceAttachBone.IsNone())
	{
		return;
	}

	TArray<USkeletalMeshComponent*> MeshComponents;
	GetComponents<USkeletalMeshComponent>(MeshComponents);
	for (USkeletalMeshComponent* MeshComponent : MeshComponents)
	{
		// 只处理直接挂在身体网格下、且骨架不同的附属网格（面部）。
		if (!MeshComponent || MeshComponent == BodyMesh)
		{
			continue;
		}
		if (MeshComponent->GetAttachParent() != BodyMesh)
		{
			continue;
		}

		const USkeletalMesh* OtherAsset = MeshComponent->GetSkeletalMeshAsset();
		const USkeleton* OtherSkeleton =
			OtherAsset ? OtherAsset->GetSkeleton() : nullptr;
		if (OtherSkeleton == BodySkeleton)
		{
			continue;
		}

		// KeepWorldTransform：改挂瞬间脸不能跳位置。
		MeshComponent->AttachToComponent(
			BodyMesh, FAttachmentTransformRules::KeepWorldTransform, FaceAttachBone);
	}
}

void APlanetCharacter::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	// 外观在这里套用，而不是构造函数里：OnConstruction 是编辑器里能读到蓝图
	// 覆盖值的第一个时机，在蓝图 Class Defaults 里改 CharacterMesh 立刻能在视口看到。
	ApplyCharacterMesh();
}

void APlanetCharacter::BeginPlay()
{
	Super::BeginPlay();

	// WalkSpeed 是 EditAnywhere：在蓝图/实例上改过之后这里应用一次，否则要等到
	// 按一次冲刺（SetSprinting）才会生效。
	GetCharacterMovement()->MaxWalkSpeed = WalkSpeed;

	// 再套一次：角色由 GameMode 按 DefaultPawnClass 运行时生成，实例的
	// OnConstruction 未必在蓝图属性就位之后跑过。重复调用是幂等的。
	ApplyCharacterMesh();

	// 面部网格用不同骨架，必须在身体网格有模型之后才能挂到 Head_50。
	AttachFaceMeshesToHead();

	// 臂长同理：CameraArmLength 是 EditAnywhere，蓝图或关卡实例改过的值构造函数
	// 读不到，这里才是它第一次生效的地方。基准臂长只在这一处抄，之后滚轮改的是
	// 相对它的偏移（见 AddZoom 与 Tick），不会把蓝图设的值冲掉。
	if (SpringArm)
	{
		SpringArm->TargetArmLength = CameraArmLength;
		BaseCameraArmLength = CameraArmLength;
	}

	// 解析器必须在「找不到行星」的早退之前装好：它读的是 PlanetActor 的当前值，
	// 而不是安装时的快照，所以先装后查没有时序问题；反过来（查到之后才装）
	// 一旦此刻没找到行星，早退就把安装也跳过了，再也补不上。
	if (PlanetEdit)
	{
		PlanetEdit->SetTargetResolver(
			[this](const FVector& RayOrigin, const FVector& RayDirection,
				const float MaxDistanceCm, FPlanetEditTarget& OutTarget)
			{
				return TraceTargetForCharacter(
					*this, PlanetActor.Get(), RayOrigin, RayDirection, MaxDistanceCm, OutTarget);
			});
	}

	// 调用器在 OnRegister 里自动启用（VoxelInvokerComponent.cpp:83），那时还没有
	// 任何人告诉过我们是不是视点。这里按当前已知状态重新应用一次。
	// 必须在下面「找不到行星」的早退之前 —— 早退之后就没有第二次机会，
	// 角色会在没被 Possess 的情况下一直开着调用器。
	ApplyViewpointInvokerState(GetAttachParentActor() != nullptr);

	PlanetActor = FindNearestPlanet();

	if (PlanetEdit)
	{
		// 网格只跟角色走。星图 Pawn 从不调这个，所以星图里没有地表网格。
		PlanetEdit->SetGridPlanet(PlanetActor);
	}

	if (!PlanetActor)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("APlanetCharacter: 关卡里找不到 AVoxelPlanetActor，角色不会获得球面重力。"));
		return;
	}

	// 出生点以关卡里的 PlayerStart 为准：有 PlayerStart 时角色已经被引擎摆在那儿，
	// 再吸附一次等于把出生点作废（GameMode 也把飞船摆在同一个 PlayerStart 上，
	// 吸到地表会让两者拉开距离，开局登船失败）。
	//
	// 没有 PlayerStart 时才需要摆位：角色会生成在世界原点，对球心在原点的行星
	// 来说就是生成在实心内部。
	bSpawnedAtPlayerStart = HasPlayerStartInLevel();
	if (!bSpawnedAtPlayerStart)
	{
		SnapToPlanetSurface();
	}
}

bool APlanetCharacter::HasPlayerStartInLevel() const
{
	// APlayerStartPIE 派生自 APlayerStart，所以「Play From Here」同样算数 ——
	// 那种情况下引擎会把它摆在编辑器摄像机处，正是作者当时想要的出生点。
	for (TActorIterator<APlayerStart> It(GetWorld()); It; ++It)
	{
		return true;
	}
	return false;
}

void APlanetCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// 滚轮缩放。放在行星判空之前：臂长与重力无关，没找到行星时也该能拉远拉近。
	// 这里是**唯一**写 TargetArmLength 的地方，别处直接赋值会在下一帧被冲掉。
	if (SpringArm)
	{
		ZoomOffset = FMath::FInterpTo(ZoomOffset, TargetZoomOffset, DeltaSeconds, ZoomInterpSpeed);
		SpringArm->TargetArmLength = FMath::Clamp(
			BaseCameraArmLength + ZoomOffset, MinCameraArmLength, MaxCameraArmLength);
	}

	// 行星在动，最近的一颗会换人。放在 PlanetActor 判空之前：开局若一颗都没
	// 找到（PlanetActor 为空），这里才有机会在后续帧找回来。
	//
	// 登船期间不重评：角色此刻挂在飞船下跟着飞船跨星球移动，重评会把角色从
	// 船里瞬移到新星的地表上。本项目只有一处 AttachToActor（登船，见
	// StellarEraPlayerController.cpp），所以附着关系就是「在船上」。
	const bool bBoarded = GetAttachParentActor() != nullptr;

	if (!bBoarded && PlanetReevaluateInterval > 0.f)
	{
		PlanetReevaluateTimer -= DeltaSeconds;
		if (PlanetReevaluateTimer <= 0.f)
		{
			PlanetReevaluateTimer = PlanetReevaluateInterval;
			ReevaluatePlanet();
		}
	}

	// 「上」的来源：有行星就是球面法线，没有就退回角色自己的 up。
	//
	// 相机必须放在行星判空**之前**算。以前这里跟着行星一起 return，后果是
	// 「关卡里没有体素行星时，鼠标怎么拖相机都不动」—— CameraYaw / CameraPitch
	// 一直在累加，却没有任何一行代码去消费它们（写枢轴旋转的全工程只有下面
	// 那一处）。相机是角色的相机，不该以「脚下有没有我们自己的行星 Actor」
	// 为前提：纯地形测试图、或者还没摆行星的关卡，都该能正常看。
	FVector Up;
	if (PlanetActor)
	{
		const FVector ToCenter = (PlanetActor->GetActorLocation() - GetActorLocation()).GetSafeNormal();
		if (ToCenter.IsNearlyZero())
		{
			// 正好在球心，重力方向无意义，保持上一帧的姿态。
			return;
		}

		// 球面行走的全部物理：告诉 CMC“下”指向球心。落地判定、斜坡滑动、台阶
		// 与跳跃都在重力空间里自动成立，不需要手写球面物理。
		GetCharacterMovement()->SetGravityDirection(ToCenter);
		Up = -ToCenter;
	}
	else
	{
		// 没有行星：用角色自己的 up。平地上它就是世界 up，于是下面的基座退化成
		// 一个恒定的世界空间基座 —— 偏航仍是绕竖直轴水平环绕，俯仰仍是上下看。
		Up = GetActorUpVector();
	}

	// 基座沿球面连续平移：求出把上一帧法线转到本帧法线的旋转，作用在基座上。
	// 这样朝向在任何位置都连续，极点处也不会退化。
	// 平地上 Up 逐帧不变，这个旋转是单位阵，基座就是那个恒定基座。
	if (LastSurfaceUp.IsNearlyZero())
	{
		// 第一帧：任取一个切向参考建基，保证“上”就是球面法线。
		FVector Reference = FVector::VectorPlaneProject(GetActorForwardVector(), Up).GetSafeNormal();
		if (Reference.IsNearlyZero())
		{
			Reference = FVector::VectorPlaneProject(FVector::ForwardVector, Up).GetSafeNormal();
		}
		if (Reference.IsNearlyZero())
		{
			Reference = FVector::VectorPlaneProject(FVector::RightVector, Up).GetSafeNormal();
		}
		CameraBaseRotation = FRotationMatrix::MakeFromXZ(Reference, Up).ToQuat();
	}
	else
	{
		CameraBaseRotation = FQuat::FindBetweenNormals(LastSurfaceUp, Up) * CameraBaseRotation;
	}
	LastSurfaceUp = Up;

	// 玩家视角是相对基座的局部旋转：偏航绕法线水平环绕，俯仰绕切向倾斜。
	// 基座连续 ⟹ 画面不会翻滚。
	CameraPivot->SetWorldRotation(
		CameraBaseRotation * FRotator(CameraPitch, CameraYaw, 0.f).Quaternion());

	// 以下只对行星有意义。没有行星时（纯地形关卡）角色由地形碰撞支撑，
	// 既不需要也不该被按到「球面」上。
	if (!PlanetActor)
	{
		return;
	}

	// 碰撞体是异步流送出来的：第一次踩到地面之前，每帧把角色按回球面，
	// 否则它会在碰撞体生成之前落进球体内部，然后卡在实心里。
	//
	// 出生点来自 PlayerStart 时整段跳过（见 bSpawnedAtPlayerStart）：角色出生在
	// 作者选好的位置，可能在半空也可能在球体之外，按回球面等于把出生点作废。
	if (!bSpawnedAtPlayerStart && !bHasLanded)
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
	// 按方向取地表：有地形时地表在基准球面上下浮动，用基准半径会把角色塞进
	// 山体里（振幅 8 体素 = 800cm，山峰比基准面高 1600cm）。
	// 多抬 1 厘米，避免刚好贴面时被判定为“已经在实体内部”。
	const FVector Location =
		PlanetCenter + Up * (PlanetActor->GetSurfaceRadiusCmAtDirection(Up) + CapsuleHalfHeight + 1.f);

	SetActorLocation(Location, /*bSweep=*/false);
	SetActorRotation((FQuat::FindBetweenNormals(FVector::UpVector, Up) * GetActorQuat()).Rotator());

	// 摆位之后残留的向下速度会让角色立刻穿进球体，必须清掉。
	GetCharacterMovement()->Velocity = FVector::ZeroVector;

	// 这次瞬移会让基座被平移过一个大跨度，下一帧重新初始化而不是硬搬。
	LastSurfaceUp = FVector::ZeroVector;
}

void APlanetCharacter::ReevaluatePlanet()
{
	AVoxelPlanetActor* Nearest = FindNearestPlanet();
	if (Nearest == PlanetActor)
	{
		return;
	}

	// 换星：旧星已经不在脚下了（或者本来就没找到）。把「已落地」清掉 ——
	// 新星的地表还没流送出来，得让 Tick 末尾的兜底逻辑重新把角色按到球面上。
	PlanetActor = Nearest;
	bHasLanded = false;

	if (PlanetActor)
	{
		// 内部会把 LastSurfaceUp 清零，相机基座下一帧重建。
		SnapToPlanetSurface();
	}

	// 编辑网格跟着角色走：组件内部会先把旧星的网格收掉再点亮新星。
	if (PlanetEdit)
	{
		PlanetEdit->SetGridPlanet(PlanetActor);
	}
}

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

void APlanetCharacter::AddZoom(float Value)
{
	// 夹的是**目标偏移**而不是最终臂长。只夹最终值的话，玩家滚过头会攒下一堆
	// 被 Clamp 吃掉的无效偏移，往回滚要先"还债"——前几格看起来像没反应。
	// ASpaceshipPawn::AddZoom 是同一套做法，两边保持一致。
	const float MinOffset = MinCameraArmLength - BaseCameraArmLength;
	// 防御 MinCameraArmLength > MaxCameraArmLength 的误配：Clamp 要求 Min <= Max。
	const float MaxOffset = FMath::Max(MaxCameraArmLength - BaseCameraArmLength, MinOffset);

	// 取负：Value > 0 是滚轮上滚 = 拉近 = 摇臂变短。
	TargetZoomOffset = FMath::Clamp(TargetZoomOffset - Value * ZoomStep, MinOffset, MaxOffset);
}

void APlanetCharacter::SetSprinting(bool bSprinting)
{
	GetCharacterMovement()->MaxWalkSpeed = bSprinting ? SprintSpeed : WalkSpeed;
}

void APlanetCharacter::SetBoarded(bool bBoarded)
{
	SetActorHiddenInGame(bBoarded);
	SetActorEnableCollision(!bBoarded);

	// 关掉移动，避免登船期间残留输入继续推动角色（它已经附着在飞船上了）。
	GetCharacterMovement()->StopMovementImmediately();
	GetCharacterMovement()->SetMovementMode(bBoarded ? MOVE_None : MOVE_Walking);

	// 登船后角色只是被隐藏/禁撞，Actor 和调用器仍然挂在飞船上，会跟着飞船
	// 一起高速平移：LODToSet = 0 + 200 米的盒子每帧都要重网格化，是飞船自己
	// 那三档之外的一块大头。这里连同「是不是当前视点」一起交给同一个谓词。
	ApplyViewpointInvokerState(bBoarded);

	if (!bBoarded)
	{
		// 离船后重新踩上球面：碰撞体可能已经因为距离太远被回收了。
		bHasLanded = false;

		// 离船时可能已经在另一颗星球旁边了（开船过来、下船）。把计时器清零，
		// 让下一帧立刻重找最近的行星，而不是拿着旧星的重力方向等半秒。
		PlanetReevaluateTimer = 0.f;
	}
}

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
