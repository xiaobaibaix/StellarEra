// Copyright Voxel Plugin SAS. All Rights Reserved.

#include "StarMapCameraPawn.h"

#include "Camera/CameraComponent.h"
#include "Components/ChildActorComponent.h"
#include "CosmosSimulationSubsystem.h"
#include "Engine/World.h"
#include "PlanetCharacter.h"
#include "PlanetEditComponent.h"
#include "PlanetMineralViewComponent.h"
#include "PlanetSurfaceRay.h"
#include "SpaceshipPawn.h"
#include "StarMapCameraInputComponent.h"
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

	// --- 体素调用器：三档 LOD，见头文件里的说明 ---
	//
	// 参数照抄飞船（SpaceshipPawn.cpp）：两者会在同一片地形上交替出现，
	// 两套值不一致的话，切模式时 LOD 会抖一下。细气泡的碰撞盒只罩 ±30 米，
	// 块数 ≈ (60/32)³ ≈ 7；LOD 两份分别 ≈ 16 / 7 块，是 4 GB 显存的取舍。
	// AlwaysLocal 版：本 Pawn 被 Possess 时不能把飞船/角色的调用器挤掉，也不能
	// 在退出星图时反过来挤一次。详见 AlwaysLocalVoxelInvokerComponent.h。
	VoxelInvoker = CreateDefaultSubobject<UAlwaysLocalVoxelInvokerComponent>(TEXT("VoxelInvoker"));
	VoxelInvoker->SetupAttachment(Camera);
	VoxelInvoker->bUseForLOD = true;
	VoxelInvoker->LODToSet = 1;
	VoxelInvoker->LODRange = 8000.f;
	VoxelInvoker->bUseForCollisions = true;
	VoxelInvoker->CollisionsRange = 3000.f;
	VoxelInvoker->bUseForNavmesh = false;
	VoxelInvoker->bUseForEvents = false;
	VoxelInvoker->bUseForPriorities = false;

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

	// 1 公里外的过渡环：±10 公里的盒子，叶块 1.02 公里（行星 3 的体素是 50 cm）。
	// 行星的 MaxLOD 把全星压到 LOD 7（2.05 公里）之后，这里是 512 米与 2.05 公里
	// 之间唯一的台阶。范围刻意推到地平线之外：20 公里半径的星，1 公里高度的
	// 地平线约 sqrt(2·20·1) = 6.4 公里，2.5 公里高度约 10.3 公里 —— 环内全是
	// LOD 6，那条台阶就落到看不见的地方，不会横在画面中间。
	// 块数：盒 ±10 km 与球面相交 ≈ π(10² − 1²) = 311 km²，除以叶块面积 ≈ 296 块。
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
	//
	// 组件由 BP_StarMapCameraPawn 挂载（2026-10-31 移出），参数在它的类默认值里：
	//   尺寸 8..1280 体素（1 体素 = 100 cm，1280 = 直径 1.28 km 的坑）；
	//   默认 64 —— 直径 32 的坑在星图距离上几乎看不见，64 既看得出又不至于挖穿；
	//   一档 10 体素（Ctrl 粗调 ×10）—— 区间跨 1272 格，一档一格等于没反应；
	//   bMultiThreadedEdits —— 1280 是半径 640 体素的球，单线程会把整帧卡死；
	//   提示串写 Ctrl+左键填（右键已归绕轨）、MaxEditDistanceCm = 5e7（与最远
	//   50 AU 的绕轨距离同一尺度）。
	// 原生 AStarMapCameraPawn 实例没有编辑功能，调用方按空处理。
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
	// 只有挂在行星上的那台（BP_StarMapCameraPawn 作为 CAC 子 Actor）有挂载父；
	// 深空单例相机没有，保持单位变换即可 —— ResetToAttachOrigin 会直接跳过。
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

	// 调用器在 OnRegister 里已经自动启用了。星图相机只在被 Possess（进星图）
	// 期间才是视点，此前一律关掉。
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

void AStarMapCameraPawn::ToggleMineralView()
{
	if (ActiveMineralView)
	{
		ExitMineralViewIfActive();
		return;
	}

	UCosmosSimulationSubsystem* Subsystem = GetWorld()
		? GetWorld()->GetSubsystem<UCosmosSimulationSubsystem>()
		: nullptr;
	AVoxelPlanetActor* Planet = Subsystem
		? Cast<AVoxelPlanetActor>(Subsystem->GetSelectedBody())
		: nullptr;
	UPlanetMineralViewComponent* MineralView =
		Planet ? Planet->FindComponentByClass<UPlanetMineralViewComponent>() : nullptr;

	// 提示归控制器：相机没有 HUD，失败原因经 IStarMapLookContext 转出去。
	IStarMapLookContext* Context = Cast<IStarMapLookContext>(GetController());
	if (!MineralView)
	{
		if (Context)
		{
			Context->ShowStarMapPrompt(TEXT("矿物观察只支持行星"), 2.f);
		}
		return;
	}

	if (!MineralView->Activate(Planet))
	{
		// 失败只有三种：没有地质场、采样参数非法、观察体/材质创建失败
		// （后两种会先打 Error 日志）。
		if (Context)
		{
			Context->ShowStarMapPrompt(TEXT("该行星没有矿物分布"), 2.f);
		}
		return;
	}

	// 观察里左键只点图例、右键转视角：若此前按过 G，先退出编辑模式。
	if (UPlanetEditComponent* Edit = GetPlanetEditComponent())
	{
		if (Edit->IsEditMode())
		{
			Edit->SetEditMode(false);
		}
	}

	// 只拉远、不改朝向：按 H 前看哪儿，进入后还是那个角度（spec §3）。
	// 距离与按 M 进场聚焦行星用同一个倍率，两种进入方式构图一致。
	FocusOn(Planet, PlanetFocusRadiusScale * Planet->GetSurfaceRadiusCm());

	ActiveMineralView = MineralView;
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

void AStarMapCameraPawn::EnterFrom(const FVector& InCameraLocation, const FRotator& InCameraRotation)
{
	// 位置由调用方给，通常是行星表面锚点（ComputeEntryAnchorLocation）而不是进入前的
	// 视点 —— 这台相机挂在行星球心，直接原地接手会先看见星体内部。朝向仍用进入前的。
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

	// 方向取「球心 → 当前视点」：相机从玩家正在看的那一侧贴上来，进场不至于从背面
	// 钻进球里。两者重合时没有方向可言，退回世界 +Z。
	FVector Direction = TowardWorldLocation - PlanetCenter;
	if (!Direction.Normalize())
	{
		Direction = FVector::UpVector;
	}

	// 高度上限按半径的 1/4 夹：星图相机的拉远目标默认才 5e5 cm，锚点若浮到半空，
	// 进场那一帧与目标的距离会远得看不出是同一颗天体（也会让 LOD 一上来就是低的）。
	const double ClampedAltitude = FMath::Clamp(AltitudeCm, 0.0, SurfaceRadiusCm * 0.25);
	return PlanetCenter + Direction * (SurfaceRadiusCm + ClampedAltitude);
}

FVector AStarMapCameraPawn::ComputeEntryAnchorLocation(const FVector& TowardWorldLocation) const
{
	// 只有挂在行星上的那台（作为 CAC 子 Actor）算得出锚点；深空单例相机没有宿主，
	// 保持当前 Actor 位置 —— 它本来就摆在关卡里，不需要搬家。
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
	// 星图期间 CalcCamera 每帧把 Actor 拽到渲染相机位置，退出后它还留在半空；
	// 不搬回挂载原点，下次从外面看行星时那台子相机就飘在球心外。
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

	// 焦点设为它，朝向用「当前相机位置 → 焦点」的方向，于是相机沿进入前的
	// 视线方向后退 —— 位置代数上「沿视线拉远」，观感是画面保持朝向、只是向后缩。
	const FVector Focus = Body->GetActorLocation();
	const FVector ToFocus = Focus - CameraLocation;

	if (!ToFocus.IsNearlyZero())
	{
		OrbitRotation = ToFocus.Rotation();
	}

	// 按 M 进场是「重新锁定」，右键拖出来的偏移一并清掉：入场第一眼必须正对焦点，
	// 否则上一颗天体的构图会带到这一颗上。
	AimOffset = FRotator::ZeroRotator;

	// 距离**不在这里一次写完**：瞬移到目标距离会让整颗行星在同一帧里从
	// LOD 1/3/5 塌到 6/7，画面会「啪」地掉精度。这里只记起点与目标，插值交给
	// CalcCamera，让 LOD 一档一档退。
	//
	// 注意：这**不是**在躲 VoxelFree 的 ensure。2026-10-24 用日志对过时间线，
	// 进出星图的那几条 ensure 来自调用器所有权翻转（Possess）引发的全量 LOD
	// 重建，不是相机距离跳变；那是另一处修复，见 AlwaysLocalVoxelInvokerComponent.h。
	// 平滑只负责观感。
	//
	// 起点取「相机当前位置 → 焦点」的实际距离，目标取 DistanceCm；两者都夹进
	// 「不穿行星」的合法区间 —— 与 CalcCamera「选中变化」那条分支用的是同一个
	// 函数，所以拉远的第一帧位置仍然连续（唯一会跳的仍是距离被下限夹起来那一刻，
	// 这是既有行为）。
	//
	// 这里夹的是**目标**，不是最终相机位置：玩家滚过头不会攒下无效偏移。
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

	// 「选中变化」那一帧该做的事（朝向、起始距离）上面已经按当前位置算完了。
	// 这里同步 LastSeenSelected，否则下一帧 CalcCamera 会把这次变化当成「用户
	// 换了选中」而取消掉刚启动的拉远。
	LastSeenSelected = Body;
}

void AStarMapCameraPawn::OrbitLook(float DeltaYaw, float DeltaPitch)
{
	// 越贴着焦点表面越慢、拉得越远越快 —— 倍率见 OrbitLookScale。
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
	// 不按距离缩放：转头不挪相机，同样的鼠标位移在任何距离上转过的角度都一样。
	// 绕行那套「贴地慢、远处快」（OrbitLookScale）是为了让屏幕上的位移量恒定，
	// 而转头本身就是纯角度操作，套上去只会让贴地时几乎转不动。
	//
	// 只累加、不在这里夹取：真的夹在这里，拖到边界后再往回拖也要先把「多拖的
	// 那一段」抵消掉才动，手感像卡住。夹取在 CalcCamera 合成时做（ClampAimOffset）。
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

	// 偏移里有 NaN / 无穷时按「没有偏移」处理。宁可这一帧不转头，也不能把 NaN
	// 写进 OutResult.Rotation —— 那会让整帧的画面矩阵全变成 NaN。
	if (!FMath::IsFinite(AimOffset.Pitch) || !FMath::IsFinite(AimOffset.Yaw))
	{
		return BaseRotation;
	}

	// 用四元数合成，不用 FRotator 的 operator+：后者是欧拉角逐分量相加，
	// 基准俯仰不为零时与真实旋转不是一回事，锥面会算歪。
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

	// 超界：绕「基准 × 现状」这个轴把基准方向转到锥面上（右手定则，
	// +90° 的偏航正好转到 +Y，符号已验证）。基线与现状共线时叉积为零，
	// 此时夹角只可能是 0°（上面已返回）或 180°（转到背后）—— 任取一根垂直轴
	// 都能把方向送到锥面边界上。
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

	// 「倍率 1」的参考高度 = 焦点表面半径；小于 5 米的焦点按 5 米算，否则
	// 贴着飞船看一眼就是几十倍灵敏度。5 米与 MinFocusDistanceCm 的默认值同源，
	// 这里写成常量是为了让本函数保持纯函数（能无头测）。
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
	// 用户主动缩放：立刻接管距离。平滑拉远只是入场的过渡，不该在玩家已经
	// 开始滚轮之后还继续按自己的曲线走 —— 两者同时改 OrbitDistanceCm 会互相打架。
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

	// 缩放作用在**高度**上，不是到球心的距离 —— 见 ZoomAltitudeCm 的说明。
	// 夹取放在缩放之后、且夹的是高度：等价于夹「目标距离」，玩家滚过头不会
	// 攒下被 Clamp 吃掉的无效偏移（这条经验来自飞船的 AddZoom）。
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
	// 取负号：Value > 0 是滚轮上滚 = 拉近 = 高度变小。StepFactor ≤ 1 时缩放
	// 退化（乘 1 或越乘越大），按「没有缩放」处理。
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
	// 实现搬到了 StellarEraTerrain/PlanetSurfaceRay：行星编辑的射线也要用它。
	// 这里保留同名静态函数，星图现有的调用点与测试都不用改。
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

		// 相机停在穿入点之前 MinAltitudeCm 处。命中点比下限还近时不让上限低于
		// 下限：下限由焦点自己的半径决定，两者冲突说明焦点贴在山里，只能保下限。
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
		// 用「焦点 → 相机」方向上的真实地表：焦点是行星时，相机可能正对着
		// 山区，用基准球半径会让相机扎进山体。
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
	// 焦点是行星：下限是地表往上一个 MinSurfaceAltitudeCm（贴地看细节）。
	// 焦点是飞船/角色：下限只是「别把相机塞进模型」，用 MinFocusDistanceCm ——
	// 拿 50 米当飞船的最近距离，会让停在地面的飞船一进星图就把相机顶到地下。
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
	// 相机在焦点的哪一侧：OrbitRotation 是相机朝焦点的朝向，反过来就是
	// 「焦点 → 相机」的射线方向。
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

		// 地形起伏让「表面半径」依赖方向：先按基准球求交，命中后再用命中点
		// 的方向取一次真实地表半径。一次修正足够 —— 振幅只有基准半径的 1% 量级。
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
		// 选中状态变了。从**当前**相机位置反推绕行参数：视线方向 = 相机→焦点，
		// 距离 = 两者实际距离。下面重算出的位置因此与当前位置重合 —— 不跳变。
		//
		// 用户换了选中、或焦点消失：入场的平滑拉远作废，从当前位置重新推导。
		// FocusOn 自己会同步 LastSeenSelected，所以刚启动的那次拉远不会走到这里。
		bSmoothFocusActive = false;
		LastSeenSelected = Selected;

		// 换了观测目标：右键偏移归零，新目标第一眼是正对的。归零与 OrbitRotation
		// 的重算是一体的 —— 偏移是相对「正对焦点」的，换了焦点它就没有意义了。
		AimOffset = FRotator::ZeroRotator;

		if (Selected)
		{
			const FVector ToFocus = Selected->GetActorLocation() - CameraLocation;
			const double Distance = ToFocus.Size();

			// 先摆朝向再算限值：限值按「焦点 → 相机」的射线求行星遮挡，
			// 方向就是 OrbitRotation 的反向。
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

			// 对数插值：每帧的距离**倍率**恒定，LOD 平滑地一档一档退；线性插值
			// 会把时间耗在远处、近处几帧就冲过去，仍然是一帧塌数档。
			// 下限 1 cm 只为躲开 Loge(0)，实际距离都被合法区间的下限夹着。
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

	// 体素调用器挂在 Actor 上，而 Actor 的变换只在 EnterFrom 里摆过一次。
	// 不跟着相机走的话，调用器会永远停在「进星图时」的位置，把 LOD 细分到看不见
	// 的地方去。相机本身由 OutResult 覆盖，所以这里搬 Actor 不改变观感。
	// （不做 sweep：相机位置每帧现算，扫掠只会白白多一次检测。）
	if (!GetActorLocation().Equals(CameraLocation))
	{
		SetActorLocation(CameraLocation, /*bSweep=*/false);
	}

	OutResult.Location = CameraLocation;

	// 朝向 = 绕行朝向 + 右键偏移，并压回半球内。位置用的是 OrbitRotation、
	// 与 AimOffset 无关，所以「转头」不会挪相机（见 AimOffset 的说明）。
	//
	// 未选中时放开上限（180° = 不夹）：那时相机位置本来就冻结，没有「绕谁」
	// 可言，再夹锥只会让画面卡死在进入时的朝向上 —— 右键这时就是自由环顾。
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
	// 与 ResolvePlanetStarMapCamera 同一个解析顺序（先 CAC、再附加关系），
	// 免得「点得中」与「取得到相机」两套判据互相矛盾。
	return FindChildStarMapCamera(Body) != nullptr
		|| FindAttachedStarMapCamera(Body) != nullptr;
}

AVoxelPlanetActor* AStarMapCameraPawn::ResolveReferencePlanet(const APawn* EnteringPawn)
{
	if (!EnteringPawn)
	{
		return nullptr;
	}

	// 驾驶中：飞船的参考行星由 CosmosSimulationSubsystem 每帧维护（最近的行星），
	// 是「玩家心中的当前行星」的权威来源，不要在这里重新算一遍距离。
	if (const ASpaceshipPawn* Ship = Cast<ASpaceshipPawn>(EnteringPawn))
	{
		return Ship->GetPlanetActor();
	}

	// 步行：角色的 PlanetActor 由它自己的重评定时器维护（ReevaluatePlanet）。
	// 深空里两者都可能为空 —— 那就退回单例相机，见 ToggleStarMap 进入分支。
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

	// 只扫 CAC 的「已创建子 Actor」。GetChildActor() 在组件注册后才有值；
	// CDO / 未实例化的模板上恒为空 —— FindChildStarMapCamera(CDO) 返回空是预期行为。
	//
	// 用 AActor::GetComponents<T>（C++ 口径），不是 GetComponentsByClass ——
	// 后者在 5.8 里只有蓝图包装 K2_GetComponentsByClass，C++ 直接调不到。
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

	// 兜底路径：相机是关卡里的独立 Actor、手动 Attach 到星球（UniverseMap 里
	// 现存的 3 台就是这种）。CAC 的子 Actor 也会 Attach 到宿主，所以 CAC 相机
	// 在这里同样能被取到 —— 也就是说这条路对两种摆法都成立，只是让位给 CAC 扫描。
	//
	// 不递归：只认直接挂在星球上的第一层，避免行星的其它挂载物（飞船、角色、
	// 后续的采矿平台）里再藏一台相机时被误取。
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
