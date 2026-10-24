// Copyright Voxel Plugin SAS. All Rights Reserved.

#include "StarMapCameraPawn.h"

#include "Camera/CameraComponent.h"
#include "CosmosSimulationSubsystem.h"
#include "Engine/World.h"
#include "AlwaysLocalVoxelInvokerComponent.h"

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
	// 起点取「相机当前位置 → 焦点」的实际距离并夹进 Min/Max —— 与 CalcCamera
	// 「选中变化」那条分支反推出来的值一致，所以拉远的第一帧位置仍然连续
	// （唯一会跳的仍是距离被 Min 夹起来那一刻，这是既有行为）。
	SmoothFocusStartCm  = FMath::Clamp(ToFocus.Size(), MinOrbitDistanceCm, MaxOrbitDistanceCm);
	SmoothFocusTargetCm = FMath::Clamp(DistanceCm, MinOrbitDistanceCm, MaxOrbitDistanceCm);
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
	// 只动朝向不动距离 —— 下一帧 CalcCamera 按新朝向重算位置，看起来就是「绕着焦点转」。
	OrbitRotation.Yaw += DeltaYaw;
	// 夹到 ±89°：到 90° 时视线与 up 平行，Rotation 的 roll 会翻掉，画面会打转。
	OrbitRotation.Pitch = FMath::Clamp(OrbitRotation.Pitch + DeltaPitch, -89.0, 89.0);
	OrbitRotation.Roll = 0.0;
}

void AStarMapCameraPawn::AddZoom(float Value)
{
	// 夹的是**目标距离本身**，不是最终算出来的相机位置 —— 这条经验来自飞船的
	// AddZoom（SpaceshipPawn.cpp:469-471）：夹最终值的话，玩家滚过头会攒下一堆
	// 被 Clamp 吃掉的无效偏移，往回滚要先「还债」，前几格看起来像没反应。
	//
	// 用户主动缩放：立刻接管距离。平滑拉远只是入场的过渡，不该在玩家已经
	// 开始滚轮之后还继续按自己的曲线走 —— 两者同时改 OrbitDistanceCm 会互相打架。
	bSmoothFocusActive = false;

	// 取负号：Value > 0 是滚轮上滚 = 拉近 = 距离变小。
	const double Factor = FMath::Pow(ZoomStepFactor, -double(Value));
	OrbitDistanceCm = FMath::Clamp(
		OrbitDistanceCm * Factor, MinOrbitDistanceCm, MaxOrbitDistanceCm);
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

			// 夹距离本身（不是夹算出来的位置）。夹是必要的：相机不能钻进星体里。
			// 真夹到时会有一次跳变，这是唯一允许跳变的场合。
			OrbitDistanceCm = FMath::Clamp(Distance, MinOrbitDistanceCm, MaxOrbitDistanceCm);

			if (!ToFocus.IsNearlyZero())
			{
				OrbitRotation = ToFocus.Rotation();
			}
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
			// 下限 1 cm 只为躲开 Loge(0)，实际距离都被 MinOrbitDistanceCm 夹着。
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
