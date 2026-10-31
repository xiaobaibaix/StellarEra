// Copyright Voxel Plugin SAS. All Rights Reserved.

#include "StarMapCameraPawn.h"

#include "Camera/CameraComponent.h"
#include "CosmosSimulationSubsystem.h"
#include "Engine/World.h"
#include "PlanetEditComponent.h"
#include "PlanetSurfaceRay.h"
#include "VoxelPlanetActor.h"
#include "AlwaysLocalVoxelInvokerComponent.h"
#include "VoxelViewpointInvokers.h"

AStarMapCameraPawn::AStarMapCameraPawn()
{
	// 不 tick：位置在 CalcCamera 里现算，见类注释。
	PrimaryActorTick.bCanEverTick = false;

	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("StarMapCamera"));
	SetRootComponent(Camera);

	// 视线方向由本 Pawn 自己管（OrbitRotation），不走控制器的旋转。
	Camera->bUsePawnControlRotation = false;

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

	// --- 行星编辑：与角色共用组件，参数按星图尺度调大 ---
	//
	// 尺寸 8..1280 体素：1 体素 = 100 cm（前两颗行星），1280 = 直径 1.28 km 的坑。
	// 默认 64 体素：默认值偏大是因为「直径 32 的坑在这个距离上几乎看不见」，
	// 64 既看得出是一个坑，又不至于一点就挖穿。
	// 一档 10 体素（Ctrl 粗调 ×10）：区间跨 1272 格，一档一格的按法等于没反应；
	// 从 64 直接按到 1280 要 128 下，所以加了 Ctrl 粗调。
	// 角色仍是 1 —— 它的区间只有 1..8。
	//
	// 蓝图子类 BP_StarMapCameraPawn 只把「在蓝图细节面板里手改过」的组件属性记进
	// 自己的资产，没改过的继续跟这里的 C++ 默认值走。所以目前蓝图为空，改下面这几个
	// 数照常生效；一旦有人在蓝图里改过笔刷区间 / 默认尺寸，那条覆盖就会钉住，
	// 此后改 C++ 不再传导 —— 那时两边要同步。
	PlanetEdit = CreateDefaultSubobject<UPlanetEditComponent>(TEXT("PlanetEdit"));
	// 注意：UPlanetEditComponent 是 UActorComponent（非 SceneComponent），没有
	// SetupAttachment；它不需要变换，Tick 组已在组件构造函数里设为 TG_PostUpdateWork。
	PlanetEdit->ConfigureBrushRange(
		PlanetEditBrush::MinStarMapSizeVoxels, PlanetEditBrush::MaxStarMapSizeVoxels,
		/*InitialSize=*/64);
	// 按键 / 滚轮一档走 10 体素。从默认 64 走到 1280 是 122 档，按住 Ctrl 粗调（×10）
	// 则是 12 档。
	PlanetEdit->BrushSizeStepVoxels = 10;
	// 1280 是直径、半径 640 体素的球：整块包围盒是十亿格量级，单线程会把整帧卡死，
	// 多线程按八叉树叶子并行是唯一能接受的方式。
	PlanetEdit->bMultiThreadedEdits = true;
	// 右键在星图里已经归绕轨，所以「填」改用 Ctrl+左键；Shift+左键按住是连续挖。
	PlanetEdit->PlaceActionHint = TEXT("Shift+左键按住连续挖 · Ctrl+左键填");
	// 与 MaxOrbitDistanceCm 同一尺度：从最远 50 AU 处也该打得到行星。
	PlanetEdit->MaxEditDistanceCm = 5.0e7f;
}

void AStarMapCameraPawn::BeginPlay()
{
	Super::BeginPlay();

	// 解析器捕获 this：Pawn 与组件同生共死，组件在 EndPlay 里会清掉它。
	if (PlanetEdit)
	{
		PlanetEdit->SetTargetResolver(
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
	return PlanetEdit;
}

void AStarMapCameraPawn::EnterFrom(const FVector& InCameraLocation, const FRotator& InCameraRotation)
{
	// 「原地不动、不选中」：把进入前的视角直接拿来当基准，于是进入星图时画面不变。
	CameraLocation = InCameraLocation;
	OrbitRotation = InCameraRotation;
	LastSeenSelected = nullptr;

	// 上一次退出星图时可能还有没走完的拉远；重新进入从新视角起步，它必须作废。
	bSmoothFocusActive = false;

	// 顺手把 Actor 摆过去，方便在 PIE 里看它、点它。它不影响相机。
	SetActorLocationAndRotation(InCameraLocation, InCameraRotation, /*bSweep=*/false);
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
	OutResult.Rotation = OrbitRotation;
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
