// Copyright Voxel Plugin SAS. All Rights Reserved.

#include "StarMapCameraPawn.h"

#include "Camera/CameraComponent.h"
#include "Components/ChildActorComponent.h"
#include "CosmosSimulationSubsystem.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "PlanetCharacter.h"
#include "PlanetEditComponent.h"
#include "PlanetMineralViewComponent.h"
#include "PlanetSurfaceRay.h"
#include "SpaceshipPawn.h"
#include "StarMapCameraInputComponent.h"
#include "StarMapPicking.h"
#include "TimerManager.h"
#include "VoxelPlanetActor.h"
#include "AlwaysLocalVoxelInvokerComponent.h"
#include "VoxelViewpointInvokers.h"
#include "VoxelViewpointInvokerUtils.h"

AStarMapCameraPawn::AStarMapCameraPawn()
{
	// 不 tick：位置在 CalcCamera 里现算，见类注释。
	PrimaryActorTick.bCanEverTick = false;

	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("StarMapCamera"));
	SetRootComponent(Camera);

	// 视线方向由本 Pawn 自己管（OrbitRotation），不走控制器的旋转。
	Camera->bUsePawnControlRotation = false;

	// 星图的键鼠输入：与角色 / 飞船同构，回调住在组件里。
	PlayerInput = CreateDefaultSubobject<UStarMapCameraInputComponent>(TEXT("PlayerInput"));

	// --- 体素调用器：四档 LOD，见头文件 ---
	// 参数照抄飞船：两者交替出现在同一片地形，不一致会抖。AlwaysLocal 版避免 Possess 时挤掉飞船 / 角色的调用器。
	VoxelInvoker = CreateDefaultSubobject<UAlwaysLocalVoxelInvokerComponent>(TEXT("VoxelInvoker"));
	VoxelInvoker->SetupAttachment(Camera);
	VoxelInvoker->bUseForLOD = true;
	VoxelInvoker->LODToSet = 1;
	VoxelInvoker->LODRange = 8000.f;
	VoxelInvoker->bUseForCollisions = true;
	VoxelInvoker->CollisionsRange = 3000.f;
	VoxelInvoker->bUseForNavmesh = false;
	VoxelInvoker->bUseForEvents = false;
	VoxelInvoker->bUseForPriorities = true;   // 按到本 Pawn 的距离排生成任务，否则先后随机

	VoxelInvokerLOD3 = CreateDefaultSubobject<UAlwaysLocalVoxelInvokerComponent>(TEXT("VoxelInvokerLOD3"));
	VoxelInvokerLOD3->SetupAttachment(Camera);
	VoxelInvokerLOD3->bUseForLOD = true;
	VoxelInvokerLOD3->LODToSet = 3;
	VoxelInvokerLOD3->LODRange = 30000.f;
	VoxelInvokerLOD3->bUseForCollisions = false;
	VoxelInvokerLOD3->bUseForNavmesh = false;
	VoxelInvokerLOD3->bUseForEvents = false;
	VoxelInvokerLOD3->bUseForPriorities = false;

	VoxelInvokerLOD5 = CreateDefaultSubobject<UAlwaysLocalVoxelInvokerComponent>(TEXT("VoxelInvokerLOD5"));
	VoxelInvokerLOD5->SetupAttachment(Camera);
	VoxelInvokerLOD5->bUseForLOD = true;
	VoxelInvokerLOD5->LODToSet = 5;
	VoxelInvokerLOD5->LODRange = 100000.f;
	VoxelInvokerLOD5->bUseForCollisions = false;
	VoxelInvokerLOD5->bUseForNavmesh = false;
	VoxelInvokerLOD5->bUseForEvents = false;
	VoxelInvokerLOD5->bUseForPriorities = false;

	// 1 公里外的过渡环：±10 公里盖住整条地平线，环内全是 LOD 6，台阶落到看不见。
	// 块数：盒与球面相交 ≈ π(10² − 1²) = 311 km²，除以叶块面积 ≈ 296 块。
	VoxelInvokerLOD6 = CreateDefaultSubobject<UAlwaysLocalVoxelInvokerComponent>(TEXT("VoxelInvokerLOD6"));
	VoxelInvokerLOD6->SetupAttachment(Camera);
	VoxelInvokerLOD6->bUseForLOD = true;
	VoxelInvokerLOD6->LODToSet = 6;
	VoxelInvokerLOD6->LODRange = 1000000.f;
	VoxelInvokerLOD6->bUseForCollisions = false;
	VoxelInvokerLOD6->bUseForNavmesh = false;
	VoxelInvokerLOD6->bUseForEvents = false;
	VoxelInvokerLOD6->bUseForPriorities = false;

	// --- 行星编辑 ---
	// 组件由 BP_StarMapCameraPawn 挂载，参数在它的类默认值里（1280 体素单线程会把整帧卡死）；原生实例没有编辑功能。
}

void AStarMapCameraPawn::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	if (PlayerInput)
	{
		PlayerInput->Bind(PlayerInputComponent);
	}
}

void AStarMapCameraPawn::BeginPlay()
{
	Super::BeginPlay();

	// 拍下挂载原点：星图期间 Actor 每帧跟着渲染相机走，退出时靠它搬回球心。
	// 没有挂载父时保持单位变换即可 —— ResetToAttachOrigin 会直接跳过。
	if (GetRootComponent() && GetRootComponent()->GetAttachParent())
	{
		AttachOriginRelativeTransform = GetRootComponent()->GetRelativeTransform();
	}

	// 解析器捕获 this：Pawn 与组件同生共死，组件在 EndPlay 里会清掉它。
	if (UPlanetEditComponent* Edit = GetPlanetEditComponent())
	{
		Edit->SetTargetResolver(
			[this](const FVector& RayOrigin, const FVector& RayDirection,
				const float MaxDistanceCm, FPlanetEditTarget& OutTarget)
			{
				return ResolveEditTargetByPlanets(
					RayOrigin, RayDirection, MaxDistanceCm, OutTarget);
			});
	}

	// 调用器在 OnRegister 里已自动启用；星图相机只在被 Possess 期间才是视点。
	ApplyViewpointInvokerState();
}

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
	VoxelViewpointInvokers::SetEnabled(VoxelInvokerLOD6, bIsViewpoint);
}

UPlanetEditComponent* AStarMapCameraPawn::GetPlanetEditComponent() const
{
	// 组件由 BP_StarMapCameraPawn 挂载，现查不缓存（同 APlanetCharacter 的理由）。
	return FindComponentByClass<UPlanetEditComponent>();
}

FString AStarMapCameraPawn::ToggleMineralView()
{
	if (ActiveMineralView)
	{
		ExitMineralViewIfActive();
		return FString();
	}

	UCosmosSimulationSubsystem* Subsystem = GetWorld()
		? GetWorld()->GetSubsystem<UCosmosSimulationSubsystem>()
		: nullptr;
	AVoxelPlanetActor* Planet = Subsystem
		? Cast<AVoxelPlanetActor>(Subsystem->GetSelectedBody())
		: nullptr;
	UPlanetMineralViewComponent* MineralView =
		Planet ? Planet->FindComponentByClass<UPlanetMineralViewComponent>() : nullptr;

	// 失败原因回给输入组件，由它经 HUD 弹提示（本 Pawn 不直接碰屏幕）。
	if (!MineralView)
	{
		return TEXT("矿物观察只支持行星");
	}

	if (!MineralView->Activate(Planet))
	{
		// 失败只有三种：没有地质场、采样参数非法、观察体/材质创建失败
		// （后两种会先打 Error 日志）。
		return TEXT("该行星没有矿物分布");
	}

	// 观察里左键只点图例、右键转视角：若此前按过 G，先退出编辑模式。
	if (UPlanetEditComponent* Edit = GetPlanetEditComponent())
	{
		if (Edit->IsEditMode())
		{
			Edit->SetEditMode(false);
		}
	}

	// 只拉远、不改朝向：按 H 前看哪儿，进入后还是那个角度。
	// 倍率与按 M 进场聚焦行星共用，两种进入方式构图一致。
	FocusOn(Planet, PlanetFocusRadiusScale * Planet->GetSurfaceRadiusCm());

	ActiveMineralView = MineralView;
	return FString();
}

void AStarMapCameraPawn::ExitMineralViewIfActive()
{
	if (!ActiveMineralView)
	{
		return;
	}

	ActiveMineralView->Deactivate();
	ActiveMineralView = nullptr;
}

void AStarMapCameraPawn::ToggleMineralElement(const int32 Index)
{
	if (ActiveMineralView)
	{
		ActiveMineralView->ToggleElement(Index);
	}
}

void AStarMapCameraPawn::ShowAllMineralElements()
{
	if (ActiveMineralView)
	{
		ActiveMineralView->SetAllElementsVisible(true);
	}
}

bool AStarMapCameraPawn::EnterStarMap(APawn* From, APlayerController* PC)
{
	if (!From || !PC || !GetWorld())
	{
		return false;
	}

	UCosmosSimulationSubsystem* Subsystem =
		GetWorld()->GetSubsystem<UCosmosSimulationSubsystem>();
	if (!Subsystem)
	{
		return false;
	}

	// 记下返回对象，必须在 Possess 之前 —— 之后 GetPawn() 已经是本相机了。
	StarMapEnterer = From;

	// 入场位置：相机挂在球心，原地接手会先看见星体内部，所以沿「球心 → 进场前视点」搬到球面附近。
	// 必须用 PC 视点而非 From 的变换（Pawn 变换不等于玩家看到的画面）；朝向仍取进入前的视线。
	FVector ViewLocation;
	FRotator ViewRotation;
	PC->GetPlayerViewPoint(ViewLocation, ViewRotation);
	EnterFrom(ComputeEntryAnchorLocation(ViewLocation), ViewRotation);

	// 相机由行星的 CAC 提供，所以「借用谁的相机」与「聚焦谁」是同一颗。
	// 挂载父为空说明它不是行星带来的（不该发生），那就不聚焦。
	AActor* const FocusBody = GetAttachParentActor();

	Subsystem->SetSelectedBody(nullptr);
	Subsystem->SetStarMapActive(true);
	PC->Possess(this);

	// 放在 Possess 之后：FocusOn 只改本 Pawn 状态，语义上「进入星图」应当整体先完成。
	// 图内点选别的星球只走 SetSelectedBody + FocusOn、**不换相机** —— 换相机 = 换 Possess = 体素八叉树全量重建。
	if (FocusBody)
	{
		Subsystem->SetSelectedBody(FocusBody);
		FocusOn(
			FocusBody,
			GetPlanetFocusRadiusScale() * StarMapPicking::GetBodyRadiusCm(FocusBody));
	}

	return true;
}

void AStarMapCameraPawn::ExitStarMap()
{
	UCosmosSimulationSubsystem* Subsystem =
		GetWorld() ? GetWorld()->GetSubsystem<UCosmosSimulationSubsystem>() : nullptr;
	if (!Subsystem)
	{
		return;
	}

	// 混合还没走完时又按了一次 M：忽略。不拦的话会把整段退出流程重跑一遍、
	// 再压一个定时器上去。
	if (StarMapExitTimerHandle.IsValid()
		&& GetWorldTimerManager().IsTimerActive(StarMapExitTimerHandle))
	{
		return;
	}

	// 先退矿物透视再换 Pawn：它隐藏了行星网格、关了碰撞与大气，必须在恢复视角之前还原。
	ExitMineralViewIfActive();

	Subsystem->SetSelectedBody(nullptr);
	Subsystem->SetStarMapActive(false);

	// 编辑模式不跟着换 Pawn 走：Possess 回去后预览 Tick 与笔刷线会留在原地，退出即关。
	if (UPlanetEditComponent* Edit = GetPlanetEditComponent())
	{
		if (Edit->IsEditMode())
		{
			Edit->SetEditMode(false);
		}
	}

	// 返回对象可能已经没了（引擎 FellOutOfWorld / KillZ 会 Destroy Pawn），这是已知、可达的死角：
	// 不 Possess 不会崩，但 GetPawn() 仍是本相机，下次按 M 会再次命中退出分支空转，玩家出不去。
	// 真修需三条：进场不记相机自己、进出判据换成 IsStarMapActive()、退出分支兜底重新拿默认 Pawn。
	TWeakObjectPtr<APawn> const WeakReturn = StarMapEnterer;
	StarMapEnterer.Reset();

	APlayerController* const PC = Cast<APlayerController>(GetController());
	if (PC && WeakReturn.IsValid() && ExitBlendSeconds > 0.f)
	{
		// 不在一帧内切回：交给引擎做视点混合，起点是仍被 Possess 的本相机，
		// 终点是返回 Pawn 的相机，位置 / 朝向 / FOV 一起过渡。
		PC->SetViewTargetWithBlend(
			WeakReturn.Get(),
			ExitBlendSeconds,
			EViewTargetBlendFunction::VTBlend_Cubic,
			/*BlendExp=*/0.f,
			/*bLockOutgoing=*/false);

		// 混合结束再 Possess —— Possess 会立刻把视点设成返回 Pawn，提前调用
		// 等于把混合掐掉。多等 0.05 s，避开「定时器与混合在同一帧收尾」的边界。
		GetWorldTimerManager().SetTimer(
			StarMapExitTimerHandle,
			FTimerDelegate::CreateWeakLambda(
				this,
				[this, WeakReturn]()
				{
					FinishStarMapExit(WeakReturn.Get());
				}),
			ExitBlendSeconds + 0.05f,
			/*bLoop=*/false);
	}
	else
	{
		// 没有可回的 Pawn（它已经没了），或混合被关掉：立刻收尾。
		FinishStarMapExit(WeakReturn.Get());
	}
}

void AStarMapCameraPawn::FinishStarMapExit(APawn* ReturnPawn)
{
	if (ReturnPawn)
	{
		if (APlayerController* const PC = Cast<APlayerController>(GetController()))
		{
			PC->Possess(ReturnPawn);
		}
	}

	// 星图期间 CalcCamera 每帧把 Actor 拽到渲染位置，退出后不搬回球心会飘在球外。
	// 放在 Possess 之后：换人后 CalcCamera 不再跑，搬过去的变换才不会被下一帧覆盖回来。
	ResetToAttachOrigin();
}

void AStarMapCameraPawn::EnterFrom(const FVector& InCameraLocation, const FRotator& InCameraRotation)
{
	// 位置通常是行星表面锚点而不是进入前的视点：相机挂在球心，原地接手会先看见星体内部。
	CameraLocation = InCameraLocation;
	OrbitRotation = InCameraRotation;
	// 上次退出前可能还带着右键拖出来的偏移；重新进入从「正对」起步。
	AimOffset = FRotator::ZeroRotator;
	LastSeenSelected = nullptr;

	// 上一次退出星图时可能还有没走完的拉远；重新进入从新视角起步，它必须作废。
	bSmoothFocusActive = false;

	// 顺手把 Actor 摆过去，方便在 PIE 里看它、点它。它不影响相机。
	SetActorLocationAndRotation(InCameraLocation, InCameraRotation, /*bSweep=*/false);
}

FVector AStarMapCameraPawn::ComputeSurfaceAnchorLocation(
	const FVector& PlanetCenter,
	const FVector& TowardWorldLocation,
	const double SurfaceRadiusCm,
	const double AltitudeCm)
{
	if (SurfaceRadiusCm <= 0.0)
	{
		return PlanetCenter;
	}

	// 方向取「球心 → 当前视点」：相机从玩家正在看的那侧贴上来，不至于从背面钻进球里。
	// 两者重合时没有方向可言，退回世界 +Z。
	FVector Direction = TowardWorldLocation - PlanetCenter;
	if (!Direction.Normalize())
	{
		Direction = FVector::UpVector;
	}

	// 高度上限按半径的 1/4 夹：锚点若浮到半空，进场那一帧与拉远目标的距离会远得
	// 看不出是同一颗天体，LOD 也会一上来就是低的。
	const double ClampedAltitude = FMath::Clamp(AltitudeCm, 0.0, SurfaceRadiusCm * 0.25);
	return PlanetCenter + Direction * (SurfaceRadiusCm + ClampedAltitude);
}

FVector AStarMapCameraPawn::ComputeEntryAnchorLocation(const FVector& TowardWorldLocation) const
{
	// 只有挂在行星上的那台算得出锚点；没有挂载父时保持当前 Actor 位置，入场画面不变。
	const AVoxelPlanetActor* const Planet = Cast<AVoxelPlanetActor>(GetAttachParentActor());
	if (!Planet)
	{
		return GetActorLocation();
	}

	return ComputeSurfaceAnchorLocation(
		Planet->GetActorLocation(),
		TowardWorldLocation,
		Planet->GetSurfaceRadiusCm(),
		EntryAnchorAltitudeCm);
}

void AStarMapCameraPawn::ResetToAttachOrigin()
{
	// 星图期间 CalcCamera 每帧把 Actor 拽到渲染位置，不搬回挂载原点就会飘在球心外。
	if (!GetRootComponent() || !GetRootComponent()->GetAttachParent())
	{
		return;
	}

	SetActorRelativeTransform(
		AttachOriginRelativeTransform,
		/*bSweep=*/false,
		nullptr,
		ETeleportType::TeleportPhysics);
}

void AStarMapCameraPawn::FocusOn(AActor* Body, double DistanceCm)
{
	if (!Body)
	{
		return;
	}

	// 朝向取「当前相机位置 → 焦点」，于是相机沿进入前的视线方向后退，
	// 观感是画面保持朝向、只是向后缩。
	const FVector Focus = Body->GetActorLocation();
	const FVector ToFocus = Focus - CameraLocation;

	if (!ToFocus.IsNearlyZero())
	{
		OrbitRotation = ToFocus.Rotation();
	}

	// 按 M 进场是「重新锁定」，偏移一并清掉：入场第一眼必须正对焦点。
	AimOffset = FRotator::ZeroRotator;

	// 距离**不在这里一次写完**：瞬移会让整颗行星一帧内塌多档 LOD，这里只记起点与目标，
	// 插值交给 CalcCamera。这**不是**在躲 VoxelFree 的 ensure —— 那来自 Possess 引发的全量重建。
	// 起点与目标都夹进不穿行星的合法区间，与 CalcCamera 选中变化分支同源。
	double MinCm = 0.0;
	double MaxCm = 0.0;
	ComputeOrbitDistanceLimitsCm(Body, MinCm, MaxCm);

	SmoothFocusStartCm  = FMath::Clamp(ToFocus.Size(), MinCm, MaxCm);
	SmoothFocusTargetCm = FMath::Clamp(DistanceCm, MinCm, MaxCm);
	SmoothFocusElapsedSeconds = 0.f;
	bSmoothFocusActive = !FMath::IsNearlyEqual(SmoothFocusStartCm, SmoothFocusTargetCm);

	if (!bSmoothFocusActive)
	{
		// 起点与目标重合：没有可插值的东西，直接落到目标上。
		OrbitDistanceCm = SmoothFocusTargetCm;
	}

	// 同步 LastSeenSelected，否则下一帧 CalcCamera 会把这次变化当成换选中而取消拉远。
	LastSeenSelected = Body;
}

void AStarMapCameraPawn::OrbitLook(float DeltaYaw, float DeltaPitch)
{
	// 越贴焦点表面越慢、拉得越远越快，倍率见 OrbitLookScale。
	// 没有焦点时不缩放：那时相机本来就冻结，转朝向只为下一次选中做准备。
	double Scale = 1.0;
	if (AActor* Selected = ResolveSelectedBody())
	{
		Scale = OrbitLookScale(
			OrbitDistanceCm, ResolveFocusSurfaceRadiusCm(Selected),
			MinOrbitLookScale, MaxOrbitLookScale);
	}

	// 只动朝向不动距离 —— 下一帧 CalcCamera 按新朝向重算位置，看起来就是「绕着焦点转」。
	OrbitRotation.Yaw += DeltaYaw * Scale;
	// 夹到 ±89°：到 90° 时视线与 up 平行，Rotation 的 roll 会翻掉，画面会打转。
	OrbitRotation.Pitch = FMath::Clamp(OrbitRotation.Pitch + DeltaPitch * Scale, -89.0, 89.0);
	OrbitRotation.Roll = 0.0;
}

void AStarMapCameraPawn::AimLook(float DeltaYaw, float DeltaPitch)
{
	// 不按距离缩放：转头不挪相机，任何距离上同样的位移转过的角度都一样。只累加、不在此夹取
	// （夹在这里则拖到边界后往回拖要先抵消多出的部分），夹取在 CalcCamera 里由 ClampAimOffset 做。
	AimOffset.Yaw += DeltaYaw;
	AimOffset.Pitch = FMath::Clamp(AimOffset.Pitch + DeltaPitch, -89.0, 89.0);
	AimOffset.Roll = 0.0;
}

FRotator AStarMapCameraPawn::ClampAimOffset(
	const FRotator& BaseRotation,
	const FRotator& AimOffset,
	const double MaxOffsetDeg)
{
	// 上限退化（0 / 负数 / NaN）＝「永远正对」。
	if (!FMath::IsFinite(MaxOffsetDeg) || MaxOffsetDeg <= 0.0)
	{
		return BaseRotation;
	}

	// 偏移有 NaN / 无穷时按「没有偏移」处理：不能把 NaN 写进相机朝向，
	// 那会让整帧的画面矩阵全变成 NaN。
	if (!FMath::IsFinite(AimOffset.Pitch) || !FMath::IsFinite(AimOffset.Yaw))
	{
		return BaseRotation;
	}

	// 用四元数合成而非 FRotator 的 operator+：后者逐分量相加，基准俯仰不为零时
	// 与真实旋转不是一回事，锥面会算歪。
	const FQuat BaseQuat = BaseRotation.Quaternion();
	const FQuat ViewQuat = BaseQuat * FRotator(AimOffset.Pitch, AimOffset.Yaw, 0.0).Quaternion();

	const FVector BaseDir = BaseQuat.GetForwardVector();
	const FVector ViewDir = ViewQuat.GetForwardVector();

	// 夹角（度）。Dot 先夹到 [-1, 1]：浮点残差会让 acos 拿到 1.0000001 而返回 NaN。
	const double CosAngle = FMath::Clamp(double(FVector::DotProduct(BaseDir, ViewDir)), -1.0, 1.0);
	const double AngleDeg = FMath::RadiansToDegrees(FMath::Acos(CosAngle));

	if (AngleDeg <= MaxOffsetDeg)
	{
		// 没超界：原样返回（GetNormalized 把 pitch 收进 ±90、roll 归零）。
		return ViewQuat.Rotator().GetNormalized();
	}

	// 超界：绕「基准 × 现状」这个轴把基准方向转到锥面上。共线时叉积为零
	// （夹角只可能是 0°，上面已返回，或 180°），任取一根垂直轴即可。
	FVector Axis = FVector::CrossProduct(BaseDir, ViewDir);
	if (Axis.IsNearlyZero())
	{
		Axis = FVector::CrossProduct(BaseDir, FVector::UpVector);
		if (Axis.IsNearlyZero())
		{
			Axis = FVector::CrossProduct(BaseDir, FVector::RightVector);
		}
	}
	Axis = Axis.GetSafeNormal();

	return BaseDir.RotateAngleAxis(MaxOffsetDeg, Axis).Rotation();
}

double AStarMapCameraPawn::OrbitLookScale(
	const double DistanceCm,
	const double FocusSurfaceRadiusCm,
	const double MinScale,
	const double MaxScale)
{
	// 还没选中过焦点：OrbitDistanceCm 是哨兵 0，不缩放（也躲开除零）。
	if (DistanceCm <= 0.0)
	{
		return 1.0;
	}

	// 没有可信的表面几何（飞船/角色没有碰撞体）：同样不缩放。
	if (FocusSurfaceRadiusCm <= 0.0)
	{
		return 1.0;
	}

	// 「倍率 1」的参考高度 = 焦点表面半径；小于 5 米的按 5 米算，否则贴着飞船
	// 看一眼就是几十倍灵敏度。写成常量是为了保持纯函数（能无头测）。
	constexpr double MinReferenceAltitudeCm = 5.0e2;
	const double ReferenceAltitudeCm =
		FMath::Max(FocusSurfaceRadiusCm, MinReferenceAltitudeCm);

	// 高度 / 参考高度：贴着表面 → 0（夹到下限），整颗行星入画（2–3 个半径高）
	// → 1–2，再拉远就继续线性增长，直到封顶。
	const double AltitudeCm =
		FMath::Max(DistanceCm - FocusSurfaceRadiusCm, 0.0);
	const double RawScale = AltitudeCm / ReferenceAltitudeCm;

	// 上限非负；下限再被上限压住 —— 参数写反（Min > Max）时返回上限，
	// 而不是让 FMath::Clamp 拿到反向区间（那是未定义行为）。
	const double Upper = FMath::Max(MaxScale, 0.0);
	const double Lower = FMath::Clamp(MinScale, 0.0, Upper);
	return FMath::Clamp(RawScale, Lower, Upper);
}

void AStarMapCameraPawn::AddZoom(float Value)
{
	// 用户主动缩放：立刻接管距离。平滑拉远只是入场的过渡，两者同时改
	// OrbitDistanceCm 会互相打架。
	bSmoothFocusActive = false;

	AActor* Selected = ResolveSelectedBody();

	double MinCm = MinSurfaceAltitudeCm;
	double MaxCm = MaxOrbitDistanceCm;
	double FocusRadiusCm = 0.0;
	if (Selected)
	{
		ComputeOrbitDistanceLimitsCm(Selected, MinCm, MaxCm);
		FocusRadiusCm = ResolveFocusSurfaceRadiusCm(Selected);
	}

	// 缩放作用在**高度**上，不是到球心的距离，见 ZoomAltitudeCm。夹取放在缩放之后：
	// 等价于夹目标距离，玩家滚过头不会攒下被 Clamp 吃掉的无效偏移。
	const double AltitudeCm = FMath::Max(OrbitDistanceCm - FocusRadiusCm, 0.0);
	const double NewAltitudeCm = ZoomAltitudeCm(
		AltitudeCm,
		ZoomStepFactor,
		Value,
		FMath::Max(MinCm - FocusRadiusCm, 0.0),
		FMath::Max(MaxCm - FocusRadiusCm, 0.0));

	OrbitDistanceCm = FMath::Clamp(NewAltitudeCm + FocusRadiusCm, MinCm, MaxCm);
}

double AStarMapCameraPawn::ZoomAltitudeCm(
	double AltitudeCm,
	double StepFactor,
	float Value,
	double MinAltitudeCm,
	double MaxAltitudeCm)
{
	// 取负号：Value > 0 = 拉近 = 高度变小。StepFactor ≤ 1 时缩放退化，按「没有缩放」处理。
	const double Factor = FMath::Pow(FMath::Max(StepFactor, 1.0), -double(Value));
	const double Scaled = FMath::Max(AltitudeCm, 0.0) * Factor;

	// 夹在缩放之后，等价于夹「目标高度」：玩家滚过头不会攒下无效偏移。
	const double Upper = FMath::Max(MaxAltitudeCm, MinAltitudeCm);
	return FMath::Clamp(Scaled, MinAltitudeCm, Upper);
}

double AStarMapCameraPawn::RaySphereFirstHitCm(
	const FVector& RayOrigin,
	const FVector& UnitDir,
	const FVector& SphereCenter,
	double SphereRadiusCm)
{
	// 实现搬到了 StellarEraTerrain/PlanetSurfaceRay（行星编辑的射线也要用）；这里保留同名函数，调用点与测试不用改。
	return PlanetSurfaceRay::RaySphereFirstHit(RayOrigin, UnitDir, SphereCenter, SphereRadiusCm);
}

double AStarMapCameraPawn::LimitDistanceByObstaclesCm(
	const FVector& FocusLocation,
	const FVector& RayDirection,
	TArrayView<const FStarMapObstacle> Obstacles,
	double MinAltitudeCm,
	double MinDistanceCm,
	double CurrentMaxCm)
{
	double MaxCm = CurrentMaxCm;
	for (const FStarMapObstacle& Obstacle : Obstacles)
	{
		const double HitCm = RaySphereFirstHitCm(
			FocusLocation, RayDirection, Obstacle.Center, Obstacle.RadiusCm);
		if (HitCm < 0.0)
		{
			continue;
		}

		// 相机停在穿入点之前 MinAltitudeCm 处。命中点比下限还近时保下限 ——
		// 冲突说明焦点贴在山里。
		MaxCm = FMath::Min(MaxCm, FMath::Max(HitCm - MinAltitudeCm, 0.0));
	}

	return FMath::Max(MaxCm, MinDistanceCm);
}

double AStarMapCameraPawn::ResolveFocusSurfaceRadiusCm(AActor* Focus) const
{
	if (!Focus)
	{
		return 0.0;
	}

	if (const AVoxelPlanetActor* Planet = Cast<AVoxelPlanetActor>(Focus))
	{
		// 用「焦点 → 相机」方向上的真实地表，否则相机正对山区时会扎进山体。
		const FVector Direction =
			(CameraLocation - Focus->GetActorLocation()).GetSafeNormal();
		return Planet->GetSurfaceRadiusCmAtDirection(Direction);
	}

	// 飞船/角色：只要不把相机塞进模型里。碰撞半径是唯一现成的几何。
	return Focus->GetSimpleCollisionRadius();
}

void AStarMapCameraPawn::ComputeOrbitDistanceLimitsCm(
	AActor* Focus, double& OutMinCm, double& OutMaxCm) const
{
	// 行星的下限是地表往上一个 MinSurfaceAltitudeCm；飞船 / 角色用 MinFocusDistanceCm
	// —— 拿 50 米当飞船最近距离，会让停在地面的飞船一进星图就把相机顶到地下。
	const bool bFocusIsPlanet = Cast<AVoxelPlanetActor>(Focus) != nullptr;
	const double FocusStandoffCm = bFocusIsPlanet ? MinSurfaceAltitudeCm : MinFocusDistanceCm;

	OutMinCm = ResolveFocusSurfaceRadiusCm(Focus) + FocusStandoffCm;
	OutMaxCm = FMath::Max(MaxOrbitDistanceCm, OutMinCm);

	UWorld* World = GetWorld();
	UCosmosSimulationSubsystem* Subsystem = World
		? World->GetSubsystem<UCosmosSimulationSubsystem>()
		: nullptr;
	if (!Subsystem)
	{
		return;
	}

	const FVector FocusLocation = Focus ? Focus->GetActorLocation() : CameraLocation;
	// OrbitRotation 是相机朝焦点的方向，反过来就是「焦点 → 相机」的射线。
	const FVector RayDirection = -OrbitRotation.Vector();

	TArray<AActor*> Bodies;
	Subsystem->GetRegisteredBodies(Bodies);

	TArray<FStarMapObstacle> Obstacles;
	Obstacles.Reserve(Bodies.Num());
	for (AActor* Body : Bodies)
	{
		// 焦点自己由 OutMinCm 保护；非体素天体没有可信的表面半径。
		if (!Body || Body == Focus)
		{
			continue;
		}

		const AVoxelPlanetActor* Planet = Cast<AVoxelPlanetActor>(Body);
		if (!Planet)
		{
			continue;
		}

		// 地形起伏让表面半径依赖方向：先按基准球求交，命中后再按命中点方向取一次
		// 真实半径。一次修正足够 —— 振幅只有基准半径的 1% 量级。
		double SurfaceRadiusCm = Planet->GetSurfaceRadiusCm();
		const double FirstHitCm = RaySphereFirstHitCm(
			FocusLocation, RayDirection, Body->GetActorLocation(), SurfaceRadiusCm);
		if (FirstHitCm >= 0.0)
		{
			const FVector HitDirection =
				(FocusLocation + RayDirection * FirstHitCm - Body->GetActorLocation())
					.GetSafeNormal();
			SurfaceRadiusCm = Planet->GetSurfaceRadiusCmAtDirection(HitDirection);
		}

		FStarMapObstacle& Obstacle = Obstacles.AddDefaulted_GetRef();
		Obstacle.Center = Body->GetActorLocation();
		Obstacle.RadiusCm = SurfaceRadiusCm;
	}

	OutMaxCm = LimitDistanceByObstaclesCm(
		FocusLocation,
		RayDirection,
		Obstacles,
		MinSurfaceAltitudeCm,
		OutMinCm,
		OutMaxCm);
}

bool AStarMapCameraPawn::ResolveEditTargetByPlanets(
	const FVector& RayOrigin,
	const FVector& RayDirection,
	const float MaxDistanceCm,
	FPlanetEditTarget& OutTarget) const
{
	const UWorld* World = GetWorld();
	const UCosmosSimulationSubsystem* Subsystem = World
		? World->GetSubsystem<UCosmosSimulationSubsystem>()
		: nullptr;
	if (!Subsystem)
	{
		return false;
	}

	TArray<AActor*> Bodies;
	Subsystem->GetRegisteredBodies(Bodies);

	// 取最近的一颗：每颗都按「当前最好距离」剪枝，更远的行星连体素行进都不用做。
	float BestDistanceCm = MaxDistanceCm;
	bool bFound = false;
	FVector BestHitLocation = FVector::ZeroVector;
	FVector BestHitNormal = FVector::UpVector;

	for (AActor* Body : Bodies)
	{
		AVoxelPlanetActor* Planet = Cast<AVoxelPlanetActor>(Body);
		if (!IsValid(Planet))
		{
			// 飞船、角色没有体素数据，不是编辑目标。
			continue;
		}

		FVector HitLocation = FVector::ZeroVector;
		FVector HitNormal = FVector::UpVector;
		float DistanceCm = 0.f;
		if (!Planet->RaycastSurfaceCm(
			RayOrigin, RayDirection, BestDistanceCm, HitLocation, HitNormal, DistanceCm))
		{
			continue;
		}

		BestDistanceCm = DistanceCm;
		BestHitLocation = HitLocation;
		BestHitNormal = HitNormal;
		OutTarget.Planet = Planet;
		bFound = true;
	}

	if (!bFound)
	{
		return false;
	}

	OutTarget.HitLocation = BestHitLocation;
	OutTarget.HitNormal = BestHitNormal;
	return true;
}

AActor* AStarMapCameraPawn::ResolveSelectedBody() const
{
	UWorld* World = GetWorld();
	UCosmosSimulationSubsystem* Subsystem = World
		? World->GetSubsystem<UCosmosSimulationSubsystem>()
		: nullptr;
	return Subsystem ? Subsystem->GetSelectedBody() : nullptr;
}

void AStarMapCameraPawn::CalcCamera(float DeltaTime, FMinimalViewInfo& OutResult)
{
	// 先让父类从相机组件取 FOV / 后处理等，再把位置和朝向覆盖掉。
	Super::CalcCamera(DeltaTime, OutResult);

	AActor* Selected = ResolveSelectedBody();
	if (Selected != LastSeenSelected.Get())
	{
		// 选中状态变了：从**当前**相机位置反推绕行参数（方向 = 相机→焦点），
		// 重算出的位置因此与当前位置重合、不跳变。入场的平滑拉远作废。
		bSmoothFocusActive = false;
		LastSeenSelected = Selected;

		// 偏移是相对「正对焦点」的，换焦点后没有意义，归零。
		AimOffset = FRotator::ZeroRotator;

		if (Selected)
		{
			const FVector ToFocus = Selected->GetActorLocation() - CameraLocation;
			const double Distance = ToFocus.Size();

			// 先摆朝向再算限值：限值按「焦点 → 相机」的射线求遮挡。
			if (!ToFocus.IsNearlyZero())
			{
				OrbitRotation = ToFocus.Rotation();
			}

			// 夹距离本身（不是夹算出来的位置）。夹是必要的：相机不能钻进星体里。
			// 真夹到时会有一次跳变，这是唯一允许跳变的场合。
			double MinCm = 0.0;
			double MaxCm = 0.0;
			ComputeOrbitDistanceLimitsCm(Selected, MinCm, MaxCm);
			OrbitDistanceCm = FMath::Clamp(Distance, MinCm, MaxCm);
		}
	}

	if (Selected)
	{
		if (bSmoothFocusActive)
		{
			SmoothFocusElapsedSeconds += DeltaTime;

			// 显式转 double：FMath::Clamp 是同类型模板，float 分子配 double 边界推不出类型。
			const double Alpha = FMath::Clamp(
				double(SmoothFocusElapsedSeconds) / double(FMath::Max(SmoothFocusSeconds, 0.01f)),
				0.0, 1.0);
			// smoothstep：两端速度为零，起步与收尾都不突兀。
			const double Eased = Alpha * Alpha * (3.0 - 2.0 * Alpha);

			// 对数插值：每帧距离倍率恒定，LOD 一档一档退；线性插值会让近处几帧就冲过去。
			// 下限 1 cm 只为躲开 Loge(0)，实际距离都被合法区间夹着。
			const double StartCm = FMath::Max(SmoothFocusStartCm, 1.0);
			const double TargetCm = FMath::Max(SmoothFocusTargetCm, 1.0);
			OrbitDistanceCm = FMath::Exp(
				FMath::Lerp(FMath::Loge(StartCm), FMath::Loge(TargetCm), Eased));

			if (Alpha >= 1.0)
			{
				bSmoothFocusActive = false;
				// 收在精确目标上，避免对数往返的浮点残差让距离永远差一点点。
				OrbitDistanceCm = SmoothFocusTargetCm;
			}
		}

		// 行星每帧都在动，「不穿行星」的合法区间也跟着动。入场那一刻夹过一次
		// 不够：0.8 秒的平滑拉远期间，焦点与行星的相对位置可能已经变了。
		{
			double MinCm = 0.0;
			double MaxCm = 0.0;
			ComputeOrbitDistanceLimitsCm(Selected, MinCm, MaxCm);
			OrbitDistanceCm = FMath::Clamp(OrbitDistanceCm, MinCm, MaxCm);
		}

		CameraLocation = Selected->GetActorLocation()
			- OrbitRotation.Vector() * OrbitDistanceCm;
	}
	// 未选中：CameraLocation 保持不动 —— 这就是「冻结」。

	// 调用器挂在 Actor 上，不跟着相机走的话会永远停在进星图时的位置，把 LOD 细分到
	// 看不见的地方。相机本身由 OutResult 覆盖，搬 Actor 不改变观感（不做 sweep）。
	if (!GetActorLocation().Equals(CameraLocation))
	{
		SetActorLocation(CameraLocation, /*bSweep=*/false);
	}

	OutResult.Location = CameraLocation;

	// 朝向 = 绕行朝向 + 右键偏移并压回半球；位置仍用 OrbitRotation，所以转头不挪相机。
	// 未选中时放开上限（180° 不夹）：那时相机位置已冻结，否则右键无法自由环顾。
	OutResult.Rotation = ClampAimOffset(
		OrbitRotation, AimOffset, Selected ? MaxAimOffsetDeg : 180.0);
}

AActor* AStarMapCameraPawn::PickBodyAtScreenPosition(
	TArrayView<const FStarMapScreenCandidate> Candidates,
	const FVector2D& ScreenPoint,
	float PixelRadius)
{
	double BestDistSq = TNumericLimits<double>::Max();
	AActor* Best = nullptr;

	for (const FStarMapScreenCandidate& Candidate : Candidates)
	{
		if (!Candidate.Actor)
		{
			continue;
		}

		// 容差逐候选算：固定像素容差管远处的小天体，投影半径管近处的大盘面。
		const double Radius = FMath::Max(
			double(PixelRadius), double(Candidate.ScreenRadiusPx));

		const double DistSq = FVector2D::DistSquared(Candidate.ScreenPosition, ScreenPoint);
		if (DistSq > Radius * Radius)
		{
			// 超出容差。边界含：恰好等于半径不算「超出」。
			continue;
		}

		if (DistSq < BestDistSq)
		{
			// 严格小于 —— 等距时先出现的赢，结果确定。
			BestDistSq = DistSq;
			Best = Candidate.Actor;
		}
	}

	return Best;
}

bool AStarMapCameraPawn::HasStarMapCamera(const AActor* Body)
{
	// 与 ResolvePlanetStarMapCamera 同一解析顺序，免得「点得中」与「取得到相机」矛盾。
	return FindChildStarMapCamera(Body) != nullptr
		|| FindAttachedStarMapCamera(Body) != nullptr;
}

AVoxelPlanetActor* AStarMapCameraPawn::ResolveReferencePlanet(const APawn* EnteringPawn)
{
	if (!EnteringPawn)
	{
		return nullptr;
	}

	// 驾驶中：飞船的参考行星由 CosmosSimulationSubsystem 每帧维护，是权威来源，
	// 不要在这里重算距离。
	if (const ASpaceshipPawn* Ship = Cast<ASpaceshipPawn>(EnteringPawn))
	{
		return Ship->GetPlanetActor();
	}

	// 步行：角色的 PlanetActor 由它自己的重评定时器维护。深空里可能为空 ——
	// 那就不进星图（解析出的相机为空，输入组件直接返回）。
	if (const APlanetCharacter* Character = Cast<APlanetCharacter>(EnteringPawn))
	{
		return Character->PlanetActor;
	}

	return nullptr;
}

AStarMapCameraPawn* AStarMapCameraPawn::FindChildStarMapCamera(const AActor* PlanetOwner)
{
	if (!PlanetOwner)
	{
		return nullptr;
	}

	// 只扫 CAC 的「已创建子 Actor」：CDO 上恒空是预期。用 AActor::GetComponents<T>（GetComponentsByClass 只有蓝图版）。
	TArray<UChildActorComponent*> ChildActorComponents;
	PlanetOwner->GetComponents<UChildActorComponent>(ChildActorComponents);
	for (UChildActorComponent* ChildActorComponent : ChildActorComponents)
	{
		if (AStarMapCameraPawn* Camera = Cast<AStarMapCameraPawn>(ChildActorComponent->GetChildActor()))
		{
			return Camera;
		}
	}
	return nullptr;
}

AStarMapCameraPawn* AStarMapCameraPawn::FindAttachedStarMapCamera(const AActor* PlanetOwner)
{
	if (!PlanetOwner)
	{
		return nullptr;
	}

	// 兜底路径：相机是关卡里手摆的独立 Actor，手动 Attach 到星球。CAC 子 Actor
	// 也会 Attach，所以两种摆法都成立。不递归，避免行星其他挂载物里的相机被误取。
	TArray<AActor*> AttachedActors;
	PlanetOwner->GetAttachedActors(AttachedActors, /*bResetArray=*/true,
		/*bRecursivelyIncludeAttachedActors=*/false);
	for (AActor* AttachedActor : AttachedActors)
	{
		if (AStarMapCameraPawn* Camera = Cast<AStarMapCameraPawn>(AttachedActor))
		{
			return Camera;
		}
	}
	return nullptr;
}

AStarMapCameraPawn* AStarMapCameraPawn::ResolvePlanetStarMapCamera(const APawn* EnteringPawn)
{
	const AActor* PlanetOwner = ResolveReferencePlanet(EnteringPawn);
	if (AStarMapCameraPawn* ChildCamera = FindChildStarMapCamera(PlanetOwner))
	{
		return ChildCamera;
	}
	return FindAttachedStarMapCamera(PlanetOwner);
}

double AStarMapCameraPawn::SilhouetteRadiusScale(double CameraDistanceCm, double BodyRadiusCm)
{
	if (BodyRadiusCm <= 0.0 || CameraDistanceCm <= BodyRadiusCm)
	{
		return 0.0;
	}

	// 1/cos α，其中 sin α = R/d ⇒ cos α = √(1 − (R/d)²)。
	const double Ratio = BodyRadiusCm / CameraDistanceCm;
	return 1.0 / FMath::Sqrt(1.0 - Ratio * Ratio);
}
