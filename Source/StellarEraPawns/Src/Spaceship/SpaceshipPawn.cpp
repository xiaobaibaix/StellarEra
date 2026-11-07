// Copyright Voxel Plugin SAS. All Rights Reserved.

#include "SpaceshipPawn.h"

#include "VoxelPlanetActor.h"
#include "PlanetAtmosphereComponent.h"
#include "PlanetCharacter.h"
#include "Camera/CameraComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "LandingDebugSphereComponent.h"
#include "UObject/ConstructorHelpers.h"
#include "EngineUtils.h"
#include "Engine/StaticMesh.h"
#include "PhysicsEngine/BodySetup.h"
#include "PhysicsEngine/ConvexElem.h"
#include "AlwaysLocalVoxelInvokerComponent.h"
#include "VoxelViewpointInvokers.h"
#include "VoxelViewpointInvokerUtils.h"
#include "SpaceshipInputComponent.h"
#include "GameFramework/PlayerController.h"

namespace
{
	// 占位立方体的资产路径：裸放的 ASpaceshipPawn（没有蓝图填 HullMesh）也得看得见机身。
	const TCHAR* FallbackHullPath = TEXT("/Engine/BasicShapes/Cube.Cube");

	// 100cm 立方体撑成 600×200×100 才像机身；只对占位体有意义，蓝图覆盖后以蓝图为准。
	const FVector FallbackHullScale(6.f, 2.f, 1.f);

	/**
	 * 1 千米 = 1e5 厘米。写成本地常数而不是用 Cosmos 的换算函数：本模块只依赖 Planet，
	 * 不引入 Cosmos 单位体系。
	 */
	constexpr float CentimetersPerKilometer = 1.0e5f;
}

ASpaceshipPawn::ASpaceshipPawn()
{
	PrimaryActorTick.bCanEverTick = true;

	// --- 船体 ---
	// Hull 是根组件：Actor 变换锚点、求解器写回位置的落点都在它身上，弹簧臂与体素调用器
	// 也挂在它下面。碰撞形状取网格自身的 Simple Collision —— 3 m 球罩不住 55 m × 83 m 的
	// 航天飞机。它只负责「别人撞船」与射线/重叠查询；飞船自己的位移由求解器
	// SetActorLocation(bSweep=false) 瞬移写入，接触高度由凸包探针（或兜底半径）决定。
	Hull = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Hull"));
	SetRootComponent(Hull);
	Hull->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Hull->SetCollisionObjectType(ECC_Pawn);
	Hull->SetCollisionResponseToAllChannels(ECR_Block);

	// C++ 只给一个引擎立方体兜底，让裸放的 ASpaceshipPawn 不至于隐形。真实模型归子蓝图：
	// 换模型用 HullMesh，摆变换在蓝图视口里拖 Hull。这里不能写项目资产路径 —— 构造函数
	// 跑在蓝图加载之前，构造期读到的永远是 C++ 默认值，真正套用在 ApplyHullMesh。
	static ConstructorHelpers::FObjectFinder<UStaticMesh> FallbackHull(FallbackHullPath);
	if (FallbackHull.Succeeded())
	{
		Hull->SetStaticMesh(FallbackHull.Object);
		Hull->SetRelativeScale3D(FallbackHullScale);
	}
	else
	{
		// 连引擎立方体都拿不到时会得到一个完全不可见的空 Actor，宁可吵一句也不要静默失败。
		UE_LOG(LogTemp, Warning,
			TEXT("ASpaceshipPawn: 找不到占位立方体 %s，Hull 将没有网格。"), FallbackHullPath);
	}

	// --- 体素调用器 ---
	// 挂根节点而不是机头：调用器位置同时决定 LOD 与碰撞范围，碰撞范围必须罩住飞船
	// 自身的物理体。它的位置是搬到**预测点**的，所以每个范围都要满足「范围 ≥ 最大速度 ×
	// PredictionTime」，高速门控保证只在低速时开着。真正卡上限的是显存而非插件块数阀：
	// LODToSet 每加一档块数少 8 倍；碰撞那份锁死 Height 0，不能跟着 LODRange 放大。
	VoxelInvoker = CreateDefaultSubobject<UAlwaysLocalVoxelInvokerWithPredictionComponent>(TEXT("VoxelInvoker"));
	VoxelInvoker->SetupAttachment(Hull);
	VoxelInvoker->bUseForLOD = true;
	VoxelInvoker->LODToSet = 1;
	VoxelInvoker->LODRange = 8000.f;
	VoxelInvoker->bUseForCollisions = true;
	VoxelInvoker->CollisionsRange = 3000.f;
	VoxelInvoker->bUseForNavmesh = false;
	VoxelInvoker->bUseForEvents = false;
	VoxelInvoker->bUseForPriorities = false;
	VoxelInvoker->bEnablePrediction = true;
	VoxelInvoker->PredictionTime = 0.03f;

	// ---- 中间 LOD 环：见头文件声明处的说明 ----
	// 预测偏移的硬约束：范围 ≥ 速度 × PredictionTime。低速门控下细气泡 18 米、
	// LOD3 60 米、LOD5 120 米，分别小于各自范围。
	VoxelInvokerLOD3 = CreateDefaultSubobject<UAlwaysLocalVoxelInvokerWithPredictionComponent>(TEXT("VoxelInvokerLOD3"));
	VoxelInvokerLOD3->SetupAttachment(Hull);
	VoxelInvokerLOD3->bUseForLOD = true;
	VoxelInvokerLOD3->LODToSet = 3;
	VoxelInvokerLOD3->LODRange = 30000.f;
	VoxelInvokerLOD3->bUseForCollisions = false;
	VoxelInvokerLOD3->bUseForNavmesh = false;
	VoxelInvokerLOD3->bUseForEvents = false;
	VoxelInvokerLOD3->bUseForPriorities = false;
	VoxelInvokerLOD3->bEnablePrediction = true;
	VoxelInvokerLOD3->PredictionTime = 0.1f;

	VoxelInvokerLOD5 = CreateDefaultSubobject<UAlwaysLocalVoxelInvokerWithPredictionComponent>(TEXT("VoxelInvokerLOD5"));
	VoxelInvokerLOD5->SetupAttachment(Hull);
	VoxelInvokerLOD5->bUseForLOD = true;
	VoxelInvokerLOD5->LODToSet = 5;
	VoxelInvokerLOD5->LODRange = 100000.f;
	VoxelInvokerLOD5->bUseForCollisions = false;
	VoxelInvokerLOD5->bUseForNavmesh = false;
	VoxelInvokerLOD5->bUseForEvents = false;
	VoxelInvokerLOD5->bUseForPriorities = false;
	VoxelInvokerLOD5->bEnablePrediction = true;
	VoxelInvokerLOD5->PredictionTime = 0.2f;

	// 过渡环：叶块 1.02 公里，补 LOD5 到行星 MaxLOD 之间的那一档；范围推到地平线之外
	// （20 公里半径星约 6.4 公里），台阶不会横在画面中间。预测偏移 72000 ≤ 范围。
	VoxelInvokerLOD6 = CreateDefaultSubobject<UAlwaysLocalVoxelInvokerWithPredictionComponent>(TEXT("VoxelInvokerLOD6"));
	VoxelInvokerLOD6->SetupAttachment(Hull);
	VoxelInvokerLOD6->bUseForLOD = true;
	VoxelInvokerLOD6->LODToSet = 6;
	VoxelInvokerLOD6->LODRange = 1000000.f;
	VoxelInvokerLOD6->bUseForCollisions = false;
	VoxelInvokerLOD6->bUseForNavmesh = false;
	VoxelInvokerLOD6->bUseForEvents = false;
	VoxelInvokerLOD6->bUseForPriorities = false;
	VoxelInvokerLOD6->bEnablePrediction = true;
	VoxelInvokerLOD6->PredictionTime = 0.2f;

	// 没有移动组件：位移完全归 N 体求解器。留着 UFloatingPawnMovement 会和求解器的
	// 写回抢着改位置，而且「不喂输入」也挡不住它按残留速度搬人。

	SpringArm = CreateDefaultSubobject<USpringArmComponent>(TEXT("SpringArm"));
	SpringArm->SetupAttachment(Hull);
	SpringArm->bUsePawnControlRotation = false;
	SpringArm->bInheritPitch = true;
	SpringArm->bInheritYaw = true;
	// 继承横滚：相机与机身刚性绑定，按 A/D 压弯时画面跟着侧倾。剥掉它时画面纹丝不动，
	// 且 bInheritRoll=false 只剥世界坐标的 roll 分量，而机身的"水平"对齐到球面法线，
	// 赤道以外平飞时地平线会是歪的。
	SpringArm->bInheritRoll = true;
	SpringArm->TargetArmLength = 1800.f;
	SpringArm->SocketOffset = FVector(0.f, 0.f, 400.f);
	SpringArm->bDoCollisionTest = true;

	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(SpringArm, USpringArmComponent::SocketName);
	Camera->bUsePawnControlRotation = false;

	// B 键调试视角的检测球。tick 在 TG_PostUpdateWork，必须晚于 N 体子系统写回位置。
	LandingDebugSphere = CreateDefaultSubobject<ULandingDebugSphereComponent>(TEXT("LandingDebugSphere"));

	PlayerInput = CreateDefaultSubobject<USpaceshipInputComponent>(TEXT("PlayerInput"));
}

void ASpaceshipPawn::ApplyHullMesh()
{
	// 只换网格，不碰变换：位置/朝向/缩放归蓝图，在这里写会被 OnConstruction 反复重跑
	// 冲掉，表现就是"模型怎么都调不动"。
	if (Hull && HullMesh)
	{
		Hull->SetStaticMesh(HullMesh);
	}

	// HullMesh 为空：什么都不做，而不是退回占位立方体 —— 空表示「网格交给蓝图决定」，
	// 强行重置会把蓝图里选好的模型每次编译都清掉。
}

void ASpaceshipPawn::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	// OnConstruction 是编辑器里能读到蓝图覆盖值的第一个时机，在这里套用 HullMesh。
	ApplyHullMesh();
	RebuildContactProbes();

#if WITH_EDITOR
	// 让飞船退出 World Partition 的空间加载：按空间加载的 Actor 会被烘进流送单元，
	// PIE 启动后随流送卸载，开局登船那 5 秒窗口里 FindNearestTo 一艘船都找不到。
	// CanChangeIsSpatiallyLoadedFlag 在已烘进流送单元后返回 false，那种情况只能手动改。
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

	// 再套一次：关卡实例可能覆盖过 HullMesh，而实例的 OnConstruction 未必跑过。重复调用幂等。
	ApplyHullMesh();
	RebuildContactProbes();

	// 重力/参考坐标系需要一颗行星。PlanetActor 现在喂给升降推力方向、大气阻力、
	// 参考速度（SetReferencePlanetVelocity）与 GetAutoLevelUp()。
	PlanetActor = FindNearestPlanet();

	// 抄下基准臂长：蓝图或关卡实例可能改过 TargetArmLength，抄一次才不会被覆盖。
	BaseArmLength = SpringArm ? SpringArm->TargetArmLength : 0.f;

	// CameraBaseRotation：蓝图可能给相机设过相对朝向，横滚偏移叠加在它上面。
	// PreviousRotation 必须在**这里**初始化：留到第一次 Tick 的话差分从单位四元数
	// 起算，开局摆位那一次旋转会被算成一次巨大的横滚。
	CameraBaseRotation = Camera ? Camera->GetRelativeRotation().Quaternion() : FQuat::Identity;
	PreviousRotation = GetActorQuat();

	// 摇臂基准朝向同样抄一份：蓝图可能调过，每帧叠加偏移而不是覆盖，才不会冲掉那个值。
	SpringArmBaseRotation = SpringArm ? SpringArm->GetRelativeRotation().Quaternion() : FQuat::Identity;

	// 调用器在 OnRegister 已自动启用，这里按「还不是视点」关掉：PIE 开局到登船之间它不该参与 LOD。
	ApplyViewpointInvokerState();
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

FVector ASpaceshipPawn::ResolveVerticalThrustUp(
	const FVector& ShipLocation,
	const FVector& PlanetLocation,
	bool bHasPlanet,
	const FVector& BodyUp)
{
	// 与 GetAutoLevelUp 只在退化回退上不同：这里退回**机身本地 up**，太空中升降键应沿机身背向。
	if (bHasPlanet)
	{
		const FVector Radial = (ShipLocation - PlanetLocation).GetSafeNormal();
		if (!Radial.IsNearlyZero())
		{
			return Radial;
		}
	}
	return BodyUp;
}

void ASpaceshipPawn::SetVoxelInvokerCollisionEnabled(const bool bEnabled)
{
	if (!VoxelInvoker)
	{
		return;
	}

	// 只改碰撞通道，bUseForLOD 必须保持 true —— 关掉后身边只剩行星轨道 LOD（约 LOD 8）、
	// 星球明显变糊。插件在下次 LOD 更新时发现标志变化并自行重建。
	VoxelInvoker->bUseForCollisions = bEnabled;
}

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
	VoxelViewpointInvokers::SetEnabled(VoxelInvokerLOD6, bIsViewpoint);
}

void ASpaceshipPawn::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// 行星在动，最近的一颗会换人；换个参考方向就行，不需要像角色那样重新摆位。
	if (PlanetReevaluateInterval > 0.f)
	{
		PlanetReevaluateTimer -= DeltaSeconds;
		if (PlanetReevaluateTimer <= 0.f)
		{
			PlanetReevaluateTimer = PlanetReevaluateInterval;
			PlanetActor = FindNearestPlanet();
		}
	}

	// --- 0. 细气泡的碰撞通道按（行星系）速度启停 ---
	// 远离行星仍卡的原因不是距离：调用器的盒子随飞船平移，每个 LOD 更新周期都要把
	// 扫过的块网格化，块数 × 速度就是游戏线程上的无界开销。只关碰撞不关 LOD ——
	// 碰撞强制 Height 0，是更贵的一路。阈值按**相对行星**的速度算，逐帧位移相减即可。
	const AVoxelPlanetActor* const SamplePlanet = PlanetActor;
	const FVector PlanetLocation = SamplePlanet ? SamplePlanet->GetActorLocation() : FVector::ZeroVector;
	const FVector ShipLocation = GetActorLocation();

	if (VoxelInvokerToggleTimer > 0.f)
	{
		VoxelInvokerToggleTimer = FMath::Max(0.f, VoxelInvokerToggleTimer - DeltaSeconds);
	}
	else if (VoxelInvokerSuspendSpeedKmPerSecond > 0.f
		&& bHasVoxelInvokerSpeedSample
		&& LastVoxelInvokerPlanet == SamplePlanet
		&& DeltaSeconds > SMALL_NUMBER)
	{
		const FVector RelativeDisplacement =
			(ShipLocation - LastVoxelInvokerShipLocation) - (PlanetLocation - LastVoxelInvokerPlanetLocation);
		const float RelativeSpeedKmPerSecond =
			RelativeDisplacement.Size() / DeltaSeconds / CentimetersPerKilometer;
		const float ResumeSpeed =
			FMath::Min(VoxelInvokerResumeSpeedKmPerSecond, VoxelInvokerSuspendSpeedKmPerSecond);

		if (!bVoxelInvokerCollisionSuspended && RelativeSpeedKmPerSecond > VoxelInvokerSuspendSpeedKmPerSecond)
		{
			bVoxelInvokerCollisionSuspended = true;
			VoxelInvokerToggleTimer = VoxelInvokerToggleCooldownSeconds;
			SetVoxelInvokerCollisionEnabled(false);
			UE_LOG(LogTemp, Display,
				TEXT("[Ship] 细气泡碰撞关闭（LOD 保留）：相对行星 %.2f km/s > %.2f"),
				RelativeSpeedKmPerSecond, VoxelInvokerSuspendSpeedKmPerSecond);
		}
		else if (bVoxelInvokerCollisionSuspended && RelativeSpeedKmPerSecond < ResumeSpeed)
		{
			bVoxelInvokerCollisionSuspended = false;
			VoxelInvokerToggleTimer = VoxelInvokerToggleCooldownSeconds;
			SetVoxelInvokerCollisionEnabled(true);
			UE_LOG(LogTemp, Display,
				TEXT("[Ship] 细气泡碰撞开启：相对行星 %.2f km/s < %.2f"),
				RelativeSpeedKmPerSecond, ResumeSpeed);
		}
	}

	// 采样每帧都更新，冷却期间也一样，否则冷却结束后的第一帧会把 0.5 秒位移当成一帧。
	LastVoxelInvokerShipLocation = ShipLocation;
	LastVoxelInvokerPlanetLocation = PlanetLocation;
	LastVoxelInvokerPlanet = PlanetActor;
	bHasVoxelInvokerSpeedSample = SamplePlanet != nullptr;

	// --- 0. 落地自由视角判定 ---
	// 落地且没有起飞方向的输入时，鼠标改为环绕停住的飞船。IsLanded() 每帧一次，开销可忽略。
	const bool bLandedNow = IsLanded();

	const bool bWasFreeLookActive = bFreeLookActive;
	bFreeLookActive = ShouldUseFreeLook(bLandedNow, ThrustInput, VerticalInput);

	// 刚进入：丢弃待发冲量，否则落地那一刻的最后一甩会在自由视角里继续带着船转。
	// 已经转起来的角速度归求解器，由它的 AngularDamping 衰减，本类改不了。
	if (bFreeLookActive && !bWasFreeLookActive)
	{
		PendingAngularImpulseLocalDeg = FVector::ZeroVector;
	}

	// --- 1–3. 姿态归求解器 ---
	// 这里原来做三件事：角速度 FInterpTo 衰减、AddActorLocalRotation、自动回正。全部搬走：
	// 鼠标冲量 → GetAngularAccelerationWorldRadPerS2()；A/D → 持续角加速度；
	// 衰减 → 求解器的 AngularDamping；翻滚 → 接触冲量的力矩。
	//
	// **不要再在这里 AddActorLocalRotation**：两处都写姿态就是互相抹，表现是机身抖或转不动。
	// AutoLevelSpeed 与 GetAutoLevelUp() 保留但都没有消费者。

	// --- 4. 推力 ---
	// 推力不在这里施加：本类只暴露「想施加多少」（GetThrustAcceleration / GetDampingTime），
	// 由 UCosmosSimulationSubsystem 每帧读走并写进求解器。

	// --- 5. 按 W 时相机后缩 ---
	// 驱动量是**推力输入本身**（不是速度/加速度）：按住就缩、松开回位，匀速飞与加速飞
	// 后缩量一样。用 FInterpTo 平滑收回；只认正向输入，按 S 制动不后缩。
	const float TargetPullback = ThrustInput > 0.f
		? CameraPullbackAtFullThrust * (bBoosting ? BoostPullbackMultiplier : 1.f)
		: 0.f;
	CameraPullback = FMath::FInterpTo(
		CameraPullback, TargetPullback, DeltaSeconds, CameraPullbackInterpSpeed);

	// 滚轮缩放：平滑追向目标偏移，与后缩量相加后**只在这里**写 TargetArmLength ——
	// 别处直接赋值会在下一帧被这行冲掉，表现为"滚轮/后缩时有时无"。
	ZoomOffset = FMath::FInterpTo(
		ZoomOffset, TargetZoomOffset, DeltaSeconds, ZoomInterpSpeed);

	if (SpringArm)
	{
		SpringArm->TargetArmLength = FMath::Clamp(
			BaseArmLength + ZoomOffset + CameraPullback, MinArmLength, MaxArmLength);
	}

	// --- 6. 相机横滚滞后 ---
	// 机身每帧滚出来的角度先累进 CameraRollLag，再指数衰减回 0，相机反向转这么多：
	// 点一下 A/D 画面几乎不动，持续压杆稳定滞后 MaxRollRate / 本值，松手补齐且无残留。
	// 用**实际旋转差**而不是 RollInput * MaxRollRate，因为接触力矩与求解器积分的角速度也会滚机身。
	const FQuat CurrentRotation = GetActorQuat();
	{
		// 机身本帧相对上一帧多转了多少，表达在上一帧的机身坐标系里。
		const FQuat RotationDelta = PreviousRotation.Inverse() * CurrentRotation;

		// 只取绕机身前方的分量：小角度下 2 × 四元数 X 分量即转角（弧度），比走 acos 的
		// GetAngle() 稳 —— 后者在 θ→0 时精度塌陷。
		float RollDelta = FMath::RadiansToDegrees(2.f * RotationDelta.X);

		// 单帧滚过 45° 只可能是开局摆位或传送（120°/s 在 5fps 下也才 24°/帧），夹一下。
		RollDelta = FMath::Clamp(RollDelta, -45.f, 45.f);

		CameraRollLag += RollDelta;
	}
	PreviousRotation = CurrentRotation;

	// CameraRollFollowSpeed <= 0 时 FInterpTo 直接返回目标值 0，等价于关掉滞后。
	CameraRollLag = FMath::FInterpTo(CameraRollLag, 0.f, DeltaSeconds, CameraRollFollowSpeed);

	if (Camera)
	{
		// 绕相机自身 X 轴反向转 lag，抵消从机身多继承的那部分横滚。左乘基准朝向 =
		// 先在相机局部空间转，再套蓝图设的朝向；位置不动。
		Camera->SetRelativeRotation(
			CameraBaseRotation * FQuat(FVector::ForwardVector, FMath::DegreesToRadians(-CameraRollLag)));
	}

	// --- 7. 落地自由视角：相机绕停住的飞船环绕 ---
	// 转的是摇臂而不是相机：摇臂枢轴就是飞船根节点。退出后偏移平滑回 0，镜头滑回船尾
	// 正后方；每帧写一次基准朝向的叠加值，退出时能精确回到基准。
	if (!bFreeLookActive)
	{
		FreeLookYaw = FMath::FInterpTo(FreeLookYaw, 0.f, DeltaSeconds, FreeLookReturnSpeed);
		FreeLookPitch = FMath::FInterpTo(FreeLookPitch, 0.f, DeltaSeconds, FreeLookReturnSpeed);

		// FInterpTo 只会无限逼近 0，永远到不了；低于 0.01° 直接归零，让回位真正结束。
		if (FMath::Abs(FreeLookYaw) < 0.01f)
		{
			FreeLookYaw = 0.f;
		}
		if (FMath::Abs(FreeLookPitch) < 0.01f)
		{
			FreeLookPitch = 0.f;
		}
	}

	if (SpringArm)
	{
		SpringArm->SetRelativeRotation(
			SpringArmBaseRotation * FRotator(FreeLookPitch, FreeLookYaw, 0.f).Quaternion());
	}

	// --- 8. 调试视角（B）：落地检测球 ---
	// 不在这里画：本 Tick 跑在 TG_PrePhysics，求解器还没写回位置，球会落后一帧。
	// 绘制由 ULandingDebugSphereComponent 在 TG_PostUpdateWork 完成。
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

void ASpaceshipPawn::SetBoost(bool bValue)
{
	bBoosting = bValue;
}

void ASpaceshipPawn::AddZoom(float Value)
{
	// 夹的是**目标偏移**而不是最终臂长：只夹最终值的话，滚过头会攒下被吃掉的无效偏移，
	// 往回滚要先"还债"、前几格像没反应。区间由 Min/MaxArmLength 相对基准臂长换算。
	const float MinOffset = MinArmLength - BaseArmLength;
	// 防御 MinArmLength > MaxArmLength 的误配：Clamp 要求 Min <= Max。
	const float MaxOffset = FMath::Max(MaxArmLength - BaseArmLength, MinOffset);

	// 取负：Value > 0 是滚轮上滚 = 拉近 = 摇臂变短。
	TargetZoomOffset = FMath::Clamp(TargetZoomOffset - Value * ZoomStep, MinOffset, MaxOffset);
}

void ASpaceshipPawn::TogglePreserveVelocity()
{
	bPreserveVelocity = !bPreserveVelocity;
}

void ASpaceshipPawn::ToggleDebugViewMode()
{
	bDebugViewMode = !bDebugViewMode;
}

FSpaceshipStatus ASpaceshipPawn::GetStatus() const
{
	FSpaceshipStatus Status;

	// 绝对速度取求解器写回的值；相对速度减去参考行星中心速度；厘米/秒 → km/s。
	Status.SpeedKmPerSecond = GetVelocity().Size() / CentimetersPerKilometer;
	Status.RelativeSpeedKmPerSecond = GetRelativeVelocity().Size() / CentimetersPerKilometer;
	Status.bHasReferencePlanet = PlanetActor != nullptr;

	// 实际极速 = 阻尼极速（推力 × τ）与当前生效的硬上限里较小且非 0 的那个；都为 0 = 无上限。
	// 硬上限按 Shift 分两档，HUD 印的必须是玩家当前真正受着的那一条。
	const float Damping = GetDampingTime();
	const float DampingTopSpeed = Damping > 0.f
		? ThrustAccelerationKmPerS2 * (bBoosting ? BoostMultiplier : 1.f) * Damping
		: 0.f;

	const float ActiveMaxSpeedKmPerSecond =
		bBoosting ? BoostMaxSpeedKmPerSecond : MaxSpeedKmPerSecond;

	float EffectiveMaxSpeed = DampingTopSpeed;
	if (ActiveMaxSpeedKmPerSecond > 0.f)
	{
		EffectiveMaxSpeed = EffectiveMaxSpeed > 0.f
			? FMath::Min(EffectiveMaxSpeed, ActiveMaxSpeedKmPerSecond)
			: ActiveMaxSpeedKmPerSecond;
	}
	Status.MaxSpeedKmPerSecond = EffectiveMaxSpeed;

	Status.ThrustInput = ThrustInput;
	Status.bBoosting = bBoosting;
	Status.bPreserveVelocity = bPreserveVelocity;
	Status.bLanded = IsLanded();
	Status.MaxThrustAccelerationKmPerS2 =
		ThrustAccelerationKmPerS2 * (bBoosting ? BoostMultiplier : 1.f);

	// 大气：密度比每帧都算（HUD 用它区分真空 / 大气），阻力大小只在有系数时非零。
	Status.AtmosphereDensityRatio = GetAtmosphereDensityRatio();
	Status.AtmosphereDragKmPerS2 =
		GetAtmosphereDragAccelerationWorldCmPerS2().Size() / CentimetersPerKilometer;

	return Status;
}

bool ASpaceshipPawn::ShouldUseFreeLook(bool bLanded, float InThrustInput, float InVerticalInput)
{
	// W / Space 是起飞方向；S / Ctrl 给负值，落地时只是制动 / 下降，不退出自由视角。
	return bLanded && InThrustInput <= 0.f && InVerticalInput <= 0.f;
}

void ASpaceshipPawn::ApplyFreeLookLookInput(
	float DeltaYaw, float DeltaPitch, float MinPitch, float MaxPitch,
	float& InOutYaw, float& InOutPitch)
{
	InOutYaw = FRotator::NormalizeAxis(InOutYaw + DeltaYaw);

	// 防御 MinPitch > MaxPitch 的误配：Clamp 要求下限不大于上限。
	InOutPitch = FMath::Clamp(InOutPitch + DeltaPitch,
		FMath::Min(MinPitch, MaxPitch), FMath::Max(MinPitch, MaxPitch));
}

void ASpaceshipPawn::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	if (PlayerInput)
	{
		PlayerInput->Bind(PlayerInputComponent);
	}
}

void ASpaceshipPawn::Look(float DeltaYaw, float DeltaPitch)
{
	// 落地自由视角：鼠标转的是相机，不是飞船。偏移量在第 7 段平滑回位。
	if (bFreeLookActive)
	{
		ApplyFreeLookLookInput(DeltaYaw, DeltaPitch,
			MinFreeLookPitch, MaxFreeLookPitch, FreeLookYaw, FreeLookPitch);
		return;
	}

	// 冲量模型：鼠标位移累加成**角速度增量**（机体系、FRotator 语义：+Y 抬头、+Z 右转，度/秒），
	// 由读取侧把它加到当前转速上再夹到 MaxYawRate / MaxPitchRate。夹目标而不是夹单帧增量，
	// 反打才不会先被增量上限拖一帧。
	PendingAngularImpulseLocalDeg +=
		FVector(0.f, DeltaPitch, DeltaYaw) * MouseImpulseScale;
}

float ASpaceshipPawn::GetLandingQueryRadiusCm() const
{
	// 球心固定在机身中心；半径是常量，不按姿态现算 —— 现算会让球随机身横滚胀缩。
	return FMath::Max(LandingQueryRadiusCm, 0.f);
}

bool ASpaceshipPawn::IsLanded() const
{
	// 速度门比**相对参考行星**的速度：飞船是 N 体天体，停在一颗公转中的行星上时绝对
	// 速度就是行星轨道速度（约 7.3 km/s），而阈值只有 200 cm/s，用绝对速度会让
	// IsLanded 永远为假。深空时相对速度退化为绝对速度。
	if (GetRelativeVelocity().Size() > LandingSpeedThreshold)
	{
		return false;
	}

	// 全向检测球，球心在机身中心、与横滚无关，半径按船体深度现算。原本是沿机身"下方"
	// 的射线：自动回正关闭后机身可任意横滚，滚 90° 射线就指向侧面、打不到地面，
	// IsLanded 恒为假，人被卡在船里按 E 没反应。
	FCollisionQueryParams Params(SCENE_QUERY_STAT(SpaceshipLanding), /*bTraceComplex=*/false, this);
	// 登船角色附着在飞船附近；将来碰撞一旦因交互 / 布娃娃重新打开，这个球会一直罩住它，先排除。
	if (BoardedCharacter)
	{
		Params.AddIgnoredActor(BoardedCharacter);
	}
	return GetWorld()->OverlapAnyTestByChannel(
		GetActorLocation(), FQuat::Identity, ECC_WorldStatic,
		FCollisionShape::MakeSphere(GetLandingQueryRadiusCm()), Params);
}

bool ASpaceshipPawn::CanBoardFrom(const FVector& CharacterLocation) const
{
	return FVector::DistSquared(CharacterLocation, GetActorLocation()) <= FMath::Square(BoardingRadius);
}

ASpaceshipPawn* ASpaceshipPawn::FindNearestTo(const UWorld* World, const FVector& FromLocation)
{
	if (!World)
	{
		return nullptr;
	}

	ASpaceshipPawn* Nearest = nullptr;
	float NearestDistSq = TNumericLimits<float>::Max();

	for (TActorIterator<ASpaceshipPawn> It(World); It; ++It)
	{
		const float DistSq = FVector::DistSquared(It->GetActorLocation(), FromLocation);
		if (DistSq < NearestDistSq)
		{
			NearestDistSq = DistSq;
			Nearest = *It;
		}
	}
	return Nearest;
}

void ASpaceshipPawn::SetBoardedCharacter(APlanetCharacter* Character)
{
	BoardedCharacter = Character;
}

void ASpaceshipPawn::Board(APlanetCharacter* Character)
{
	if (!Character)
	{
		return;
	}

	// KeepWorldTransform：相对变换从当前世界变换推导，与 SetBoarded 的先后无关。
	Character->AttachToActor(this, FAttachmentTransformRules::KeepWorldTransform);
	Character->SetBoarded(true);
	SetBoardedCharacter(Character);
}

void ASpaceshipPawn::DisembarkCrew(APlanetCharacter* Character)
{
	if (!Character)
	{
		return;
	}

	// 舱里的角色先摘下来、恢复显示与碰撞，再清账；刚生成的角色跳过这段，只做摆位。
	if (BoardedCharacter == Character)
	{
		Character->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
		Character->SetBoarded(false);
		SetBoardedCharacter(nullptr);
	}

	// 摆到飞船旁边而不是原地：原地正好在飞船的碰撞体里，会立刻被挤开。
	Character->SetActorLocation(GetCrewExitLocation(), /*bSweep=*/false);

	// 再按当地球面法线摆正：登船期间角色朝向冻结，不重新对齐的话在行星另一侧下船会
	// 看到角色侧躺在地上。下船前先重评最近行星 —— 登船期间重评是关着的，此刻
	// PlanetActor 可能还是上船时那颗，直接按球面摆正会把人按到旧星地表上。
	Character->ReevaluatePlanet();
	Character->SnapToPlanetSurface();

	// 抬高只能放在吸附之后：吸附按球面重算半径，会把落脚点的高度整段抹掉。抬一点点是为了
	// 让胶囊底不卡在船边缓坡的地形棱上，人随后自己落回地面。
	Character->AddActorWorldOffset(
		Character->GetActorUpVector() * CrewExitLiftCm, /*bSweep=*/false);
}

FVector ASpaceshipPawn::GetCrewExitLocation() const
{
	// 取探针 AABB 里 X/Y 的较大半宽：飞船能任意横滚，写死一个距离迟早把人塞进船体里。
	float HalfWidthCm = 0.f;
	for (const FVector& Probe : ContactProbesLocalCm)
	{
		HalfWidthCm = FMath::Max(HalfWidthCm,
			FMath::Max(FMath::Abs(Probe.X), FMath::Abs(Probe.Y)));
	}

	// 没有凸包时退回落地检测半径，那正好也是本资源横滚到任何姿态都够用的外壳尺寸。
	if (HalfWidthCm <= 0.f)
	{
		HalfWidthCm = GetLandingQueryRadiusCm();
	}

	const float Scale = GetActorScale3D().GetAbsMax();
	return GetActorLocation()
		+ GetActorRightVector() * (HalfWidthCm * Scale + CrewExitClearanceCm);
}

float ASpaceshipPawn::GetContactRadiusCm() const
{
	// 属性本身就是厘米，不乘任何缩放；钳到非负 —— 负半径会让接触解算反向推开飞船。
	return FMath::Max(ContactFallbackRadiusCm, 0.f);
}

bool ASpaceshipPawn::BuildContactProbes(TArray<FVector>& OutProbes) const
{
	OutProbes.Reset();

	const UStaticMesh* Mesh = Hull ? Hull->GetStaticMesh() : nullptr;
	const UBodySetup* BodySetup = Mesh ? Mesh->GetBodySetup() : nullptr;
	if (!BodySetup)
	{
		return false;
	}

	// 凸包顶点是元素空间坐标，先乘元素变换，再乘 Hull → Actor 的相对变换（含蓝图缩放），
	// 才是 Actor 本地坐标。⚠ 不能用 Hull->GetRelativeTransform()：Hull 是根组件，没有
	// attach 父级时 UE 把 RelativeLocation 直接存成世界坐标，探针会被整体平移一个飞船的
	// 世界位置。FTransform operator* 是「先 A 后 B」，所以 Elem 写在左边。
	const FTransform HullToLocal =
		Hull->GetComponentTransform().GetRelativeTransform(GetActorTransform());

	for (const FKConvexElem& Elem : BodySetup->AggGeom.ConvexElems)
	{
		const FTransform ElemToLocal = Elem.GetTransform() * HullToLocal;
		for (const FVector& Vertex : Elem.VertexData)
		{
			OutProbes.Add(ElemToLocal.TransformPosition(Vertex));
		}
	}

	if (OutProbes.Num() == 0)
	{
		return false;
	}

	// 12 个凸包几百个顶点，接触只需要形状：按 10 cm 网格去重（船体 50 m 量级下误差可忽略）。
	constexpr double BucketCm = 10.0;
	TSet<FIntVector> Seen;
	TArray<FVector> Unique;
	Unique.Reserve(OutProbes.Num());

	for (const FVector& Probe : OutProbes)
	{
		const FIntVector Key(
			FMath::RoundToInt(Probe.X / BucketCm),
			FMath::RoundToInt(Probe.Y / BucketCm),
			FMath::RoundToInt(Probe.Z / BucketCm));

		if (Seen.Contains(Key))
		{
			continue;
		}
		Seen.Add(Key);
		Unique.Add(Probe);
	}

	OutProbes = MoveTemp(Unique);
	return true;
}

void ASpaceshipPawn::RebuildContactProbes()
{
	TArray<FVector> Probes;
	if (BuildContactProbes(Probes))
	{
		ContactProbesLocalCm = MoveTemp(Probes);
		UE_LOG(LogTemp, Display, TEXT("[Ship] 接触探针 %d 个"),
			ContactProbesLocalCm.Num());
	}
	else
	{
		ContactProbesLocalCm.Reset();
		UE_LOG(LogTemp, Warning,
			TEXT("[Ship] Hull 没有 Simple Collision，接触退回 %.0f cm 球"),
			ContactFallbackRadiusCm);
	}
}

float ASpaceshipPawn::ComputeSpeedLimitTaper(double SpeedCmPerSecond, double MaxSpeedCmPerSecond)
{
	// ≤ 0 = 不限。负值也按不限：ClampMin 只在编辑器里拦，运行时写成负数不该把推力关死。
	if (MaxSpeedCmPerSecond <= 0.0)
	{
		return 1.f;
	}

	// f = 1 − (v/vmax)²：渐近逼近。用平方而非线性 —— 线性在贴近上限时突然"掉力"，
	// 半程只剩 50%、九成只剩 10%，手感像被拽住；平方半程还有 75%。
	const double Ratio = FMath::Clamp(SpeedCmPerSecond / MaxSpeedCmPerSecond, 0.0, 1.0);
	return float(FMath::Clamp(1.0 - Ratio * Ratio, 0.0, 1.0));
}

float ASpaceshipPawn::ComputeAtmosphereDensityRatio(
	double AltitudeCm, double SurfaceRadiusCm,
	double ScaleHeightRatio, double AtmosphereHeightRatio,
	bool bAtmosphereEnabled)
{
	// 总开关关掉 = 这颗行星没有大气：密度恒为 0。放在参数校验之前，与几何参数无关。
	if (!bAtmosphereEnabled)
	{
		return 0.f;
	}

	// 参数非法 = 没有可用的大气模型：宁可不加阻力，也不要除零或 exp 溢出把飞船弹飞。
	if (SurfaceRadiusCm <= 0.0 || ScaleHeightRatio <= 0.0 || AtmosphereHeightRatio <= 0.0)
	{
		return 0.f;
	}

	// 壳顶以上是真空。用 >= 而不是 >，避免边界上残留一份阻力。
	if (AltitudeCm >= SurfaceRadiusCm * AtmosphereHeightRatio)
	{
		return 0.f;
	}

	// 地面以下（撞进地形、或数值抖动给出负高度）按海平面密度处理。
	const double ScaleHeightCm = SurfaceRadiusCm * ScaleHeightRatio;
	const double NormalizedAltitude = FMath::Max(AltitudeCm, 0.0) / ScaleHeightCm;
	return float(FMath::Exp(-NormalizedAltitude));
}

FVector ASpaceshipPawn::ComputeAtmosphereDragAccelerationWorldCmPerS2(
	const FVector& VelocityCmPerSecond, double DensityRatio,
	double DragAccelerationKmPerS2)
{
	const double SpeedCmPerSecond = VelocityCmPerSecond.Size();
	if (SpeedCmPerSecond <= 0.0 || DensityRatio <= 0.0 || DragAccelerationKmPerS2 <= 0.0)
	{
		return FVector::ZeroVector;
	}

	// 速度换算成 km/s 代入 (v / 1 km/s)²，结果再乘回厘米/秒²。
	const double SpeedKmPerSecond = SpeedCmPerSecond / double(CentimetersPerKilometer);
	const double MagnitudeCmPerS2 = DragAccelerationKmPerS2 * DensityRatio
		* SpeedKmPerSecond * SpeedKmPerSecond * double(CentimetersPerKilometer);

	// 与速度严格反向：阻力只减速，不产生侧向力。
	return -VelocityCmPerSecond / SpeedCmPerSecond * MagnitudeCmPerS2;
}

float ASpaceshipPawn::GetAtmosphereDensityRatio() const
{
	if (!PlanetActor)
	{
		return 0.f;
	}

	const UPlanetAtmosphereComponent* Atmosphere =
		PlanetActor->FindComponentByClass<UPlanetAtmosphereComponent>();
	if (!Atmosphere)
	{
		return 0.f;
	}

	const double SurfaceRadiusCm = double(PlanetActor->GetSurfaceRadiusCm());
	if (SurfaceRadiusCm <= 0.0)
	{
		return 0.f;
	}

	// 高度取「到球心距离 − 基准球面半径」，不含地形起伏：逐帧做体素地表查询不值得。
	const double AltitudeCm =
		double(FVector::Dist(GetActorLocation(), PlanetActor->GetActorLocation()))
		- SurfaceRadiusCm;

	return ComputeAtmosphereDensityRatio(AltitudeCm, SurfaceRadiusCm,
		double(Atmosphere->RayleighScaleHeightRatio),
		double(Atmosphere->AtmosphereHeightRatio),
		Atmosphere->bAtmosphereEnabled);
}

FVector ASpaceshipPawn::GetAtmosphereDragAccelerationWorldCmPerS2() const
{
	if (!PlanetActor)
	{
		return FVector::ZeroVector;
	}

	const UPlanetAtmosphereComponent* Atmosphere =
		PlanetActor->FindComponentByClass<UPlanetAtmosphereComponent>();
	if (!Atmosphere || Atmosphere->DragAccelerationAtKmPerSecond <= 0.f)
	{
		return FVector::ZeroVector;
	}

	// 用相对行星的速度：大气跟着行星平移。拿绝对速度的话，贴着一颗公转中的行星静止
	// 也会吃到「迎面风」，表现为飞船在地表持续滑行。
	return ComputeAtmosphereDragAccelerationWorldCmPerS2(
		GetRelativeVelocity(), double(GetAtmosphereDensityRatio()),
		double(Atmosphere->DragAccelerationAtKmPerSecond));
}

FVector ASpaceshipPawn::GetThrustAccelerationWorldCmPerS2(float DeltaSeconds) const
{
	// 机头 / 升降只认**正向**输入：ThrustInput < 0 是制动，方向由速度决定。升降方向取
	// 行星球心 → 飞船的径向（贴地时机身可能横滚/俯仰，按 Space 才不会斜着擦地），
	// 太空中才退回机身本地 up。合成向量夹到长度 1：斜按 W+Space 不该多出 41% 推力。
	const FVector UpDirection = ResolveVerticalThrustUp(
		GetActorLocation(),
		PlanetActor ? PlanetActor->GetActorLocation() : FVector::ZeroVector,
		PlanetActor != nullptr,
		GetActorUpVector());

	FVector Direction =
		GetActorForwardVector() * FMath::Max(ThrustInput, 0.f)
		+ UpDirection * VerticalInput;

	// Shift 乘在大小上而不是方向分量上；km/s² → 厘米/秒²，1e5 是本模块的本地常数。
	const float Magnitude = ThrustAccelerationKmPerS2 * CentimetersPerKilometer
		* (bBoosting ? BoostMultiplier : 1.f);

	if (ThrustInput < 0.f)
	{
		// 制动：沿速度反方向，|a| = A · min(1, |v| / (A·Δt))，一帧内恰好停住、绝不反向。
		// 刻度必须是 A·Δt（而非固定速度）：求解器只在帧首读一次外力、整帧恒定，帧率越低
		// A·Δt 越大，固定刻度会先过冲再翻符号抖动，3 fps 下直接发散。Δt 用调用方那一帧
		// 的：自己 Tick 抄一份的话两个 Tick 先后不保证，制动量会按旧帧长估成几十 km/s。
		const FVector Velocity = GetVelocity();
		const double Speed = Velocity.Size();
		const double DeltaVPerFrame = double(Magnitude) * double(DeltaSeconds);
		if (Speed > 0.0 && DeltaVPerFrame > 0.0)
		{
			const double Taper = FMath::Min(1.0, Speed / DeltaVPerFrame);
			Direction += (-Velocity / Speed) * float(-ThrustInput * Taper);
		}
	}

	// 速度上限：只削**沿速度方向的正向**分量（f = 1 − (v/vmax)²），横向推力与制动原样
	// 保留 —— 整根缩放的话到顶后既转不了向也刹不住车。上限按 Shift 分两档，规则相同。
	// 速度取 GetRelativeVelocity()：用绝对速度会含行星轨道速度（约 7.17 km/s），f 恒为 0、
	// 正向推力被整根投影掉，表现为「按 W 往侧边飞」。投影在夹长度之前做。
	const float ActiveMaxSpeedKmPerSecond =
		bBoosting ? BoostMaxSpeedKmPerSecond : MaxSpeedKmPerSecond;
	const double MaxSpeedCmPerSecond =
		double(ActiveMaxSpeedKmPerSecond) * double(CentimetersPerKilometer);
	if (MaxSpeedCmPerSecond > 0.0)
	{
		const FVector Velocity = GetRelativeVelocity();
		const double Speed = Velocity.Size();
		if (Speed > KINDA_SMALL_NUMBER)
		{
			const FVector VelocityDirection = Velocity / Speed;
			const double AlongSpeed =
				FVector::DotProduct(Direction, VelocityDirection);
			if (AlongSpeed > 0.0)
			{
				const double Taper =
					ComputeSpeedLimitTaper(Speed, MaxSpeedCmPerSecond);
				Direction -= VelocityDirection * float(AlongSpeed * (1.0 - Taper));
			}
		}
	}

	// 大气阻力最后叠加：它是**环境力**，不受速度上限衰减，推力为 0 或巡航开着时照样要有
	// —— 否则「巡航关阻尼」会连空气阻力一起关掉。
	FVector Acceleration = Direction.GetClampedToMaxSize(1.0) * Magnitude;
	Acceleration += GetAtmosphereDragAccelerationWorldCmPerS2();
	return Acceleration;
}

float ASpaceshipPawn::GetDampingTime() const
{
	// 巡航 = 只关阻尼、推力照给；阻尼极速只有圆轨道速度的 0.156 倍。
	return bPreserveVelocity ? 0.f : DampingTime;
}

namespace
{
	/** FRotator 本地角速度 ↔ 四元数标准轴：Pitch 绕 −Y、Roll 绕 −X，Yaw 才是 +Z，X/Y 取反即互换。 */
	FVector FrLocalRatesToQuatAxes(const FVector& InFrLocal)
	{
		return FVector(-InFrLocal.X, -InFrLocal.Y, InFrLocal.Z);
	}
} // namespace

FVector ASpaceshipPawn::ComputeAngularAccelerationWorldRadPerS2(
	const FQuat& BodyRotation,
	const FVector& PendingImpulseLocalDegPerSec,
	const FVector& ActualAngularVelocityWorld,
	float InRollInput,
	float InRollAngularAccelerationDegPerS2,
	float InMaxRollRateDegPerSec,
	float DeltaSeconds)
{
	FVector AlphaWorld = FVector::ZeroVector;

	// 鼠标：待发冲量已是「到目标转速还差多少」，除以 dt 就是一帧内兑现它所需的角加速度。
	// 目标被夹住后稳态转速恒为上限，与帧长无关。dt ≈ 0 时只跳过这一支，横滚不依赖 dt。
	if (DeltaSeconds > KINDA_SMALL_NUMBER)
	{
		const FVector LocalDeltaRadPerSec = FrLocalRatesToQuatAxes(FVector(
			FMath::DegreesToRadians(PendingImpulseLocalDegPerSec.X),
			FMath::DegreesToRadians(PendingImpulseLocalDegPerSec.Y),
			FMath::DegreesToRadians(PendingImpulseLocalDegPerSec.Z)));

		AlphaWorld = BodyRotation.RotateVector(LocalDeltaRadPerSec) / DeltaSeconds;
	}

	// 横滚：持续角加速度，与 dt 无关。只有**同向**时才按接近上限线性衰减（旧版「到顶
	// 就不再增加」的连续版本）。反打必须给全额 A：带阻尼的稳态是 93.75°/s，若反打也
	// 衰减就只剩 0.22A，压回 0 要 0.28 秒，而旧版是瞬时反向。
	if (!FMath::IsNearlyZero(InRollInput) && InMaxRollRateDegPerSec > 0.f)
	{
		const FVector LocalAngularVelocity =
			BodyRotation.UnrotateVector(ActualAngularVelocityWorld);
		const float RollRateDeg = FMath::RadiansToDegrees(
			float(FrLocalRatesToQuatAxes(LocalAngularVelocity).X));

		const bool bSameDirectionAsRate =
			!FMath::IsNearlyZero(RollRateDeg)
			&& FMath::Sign(InRollInput) == FMath::Sign(RollRateDeg);

		const float Taper = bSameDirectionAsRate
			? FMath::Clamp(1.f - FMath::Abs(RollRateDeg) / InMaxRollRateDegPerSec, 0.f, 1.f)
			: 1.f;

		const float AlphaRollRad =
			FMath::DegreesToRadians(InRollInput * InRollAngularAccelerationDegPerS2 * Taper);
		AlphaWorld += BodyRotation.RotateVector(FVector(-AlphaRollRad, 0.f, 0.f));
	}

	return AlphaWorld;
}

namespace
{
	/**
	 * 「当前转速 + 输入增量」→ 目标转速（度/秒）。
	 *
	 * 两个不变量：界内朝上限加速时削到 ±Limit；**绝不在没有反向输入时主动减速** ——
	 * 被落地冲击带到 300°/s 的翻滚，玩家没摸鼠标就该保持，同向输入也不该把它往 90 拽。
	 * 这就是「落地恢复 = 纯物理，不辅助」在输入端口上的落点。
	 */
	double ResolveRateTarget(double Current, double Delta, double Limit)
	{
		const double Target = Current + Delta;

		if (FMath::Abs(Current) <= Limit)
		{
			return FMath::Clamp(Target, -Limit, Limit);
		}

		// 已被物理带出上限：同向与零输入都不改转速；反向原样给，不额外夹到对侧上限。
		const bool bReversing = Delta * Current < 0.0;
		return bReversing ? Target : Current;
	}
} // namespace

FVector ASpaceshipPawn::GetAngularAccelerationWorldRadPerS2(float DeltaSeconds)
{
	// 自由视角：鼠标归相机。这里也必须清掉待发冲量 —— Look() 走了相机分支不会写它，
	// 攒下的是进入自由视角之前那一帧的旧值。
	if (bFreeLookActive)
	{
		PendingAngularImpulseLocalDeg = FVector::ZeroVector;
		return FVector::ZeroVector;
	}

	const FQuat BodyRotation = GetActorQuat();

	// 速率目标模型：先把**目标转速**定下来，再把「到目标还差多少」交给纯函数除以 dt。
	// 不能直接把冲量除以 dt：那样每帧追加同一个增量、只靠角阻尼衰减，稳态转速
	// = 增量 / (角阻尼 × dt)，60 fps 下 3 px/帧约 370°/s、随帧率变。
	// 求解器回写的 ω 是标准四元数轴，待发冲量是 FRotator 轴，减之前先换到同一套。
	const FVector CurrentLocalRadPerSec = FrLocalRatesToQuatAxes(
		BodyRotation.UnrotateVector(CosmosAngularVelocity));
	const FVector CurrentLocalDegPerSec(
		FMath::RadiansToDegrees(CurrentLocalRadPerSec.X),
		FMath::RadiansToDegrees(CurrentLocalRadPerSec.Y),
		FMath::RadiansToDegrees(CurrentLocalRadPerSec.Z));

	// 横滚没有鼠标冲量（Look 只写 Y/Z），X 原样带过去；Y/Z 按各自上限。上限取 Abs 兜住负配置。
	const double YawLimit = double(FMath::Abs(MaxYawRate));
	const double PitchLimit = double(FMath::Abs(MaxPitchRate));
	const FVector TargetLocalDegPerSec(
		CurrentLocalDegPerSec.X + PendingAngularImpulseLocalDeg.X,
		ResolveRateTarget(CurrentLocalDegPerSec.Y, PendingAngularImpulseLocalDeg.Y, PitchLimit),
		ResolveRateTarget(CurrentLocalDegPerSec.Z, PendingAngularImpulseLocalDeg.Z, YawLimit));

	const FVector PendingDeltaLocalDegPerSec =
		TargetLocalDegPerSec - CurrentLocalDegPerSec;

	const FVector Result = ComputeAngularAccelerationWorldRadPerS2(
		BodyRotation, PendingDeltaLocalDegPerSec, CosmosAngularVelocity,
		RollInput, RollAngularAcceleration, MaxRollRate, DeltaSeconds);

	PendingAngularImpulseLocalDeg = FVector::ZeroVector;
	return Result;
}
