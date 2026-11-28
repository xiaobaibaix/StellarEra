// Copyright Voxel Plugin SAS. All Rights Reserved.

#include "StarMapRigComponent.h"

#include "Camera/CameraComponent.h"
#include "CosmosSimulationSubsystem.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "PlanetCharacter.h"
#include "PlanetEditComponent.h"
#include "PlanetSurfaceRay.h"
#include "SpaceshipPawn.h"
#include "StarMapPicking.h"
#include "VoxelComponents/VoxelInvokerComponent.h"
#include "VoxelPlanetActor.h"

void UStarMapRigComponent::FocusOn(AActor* Body, double DistanceCm)
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

void UStarMapRigComponent::OrbitLook(float DeltaYaw, float DeltaPitch)
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

void UStarMapRigComponent::AimLook(float DeltaYaw, float DeltaPitch)
{
	// 不按距离缩放：转头不挪相机，任何距离上同样的位移转过的角度都一样。只累加、不在此夹取
	// （夹在这里则拖到边界后往回拖要先抵消多出的部分），夹取在 CalcCamera 里由 ClampAimOffset 做。
	AimOffset.Yaw += DeltaYaw;
	AimOffset.Pitch = FMath::Clamp(AimOffset.Pitch + DeltaPitch, -89.0, 89.0);
	AimOffset.Roll = 0.0;
}

FRotator UStarMapRigComponent::ClampAimOffset(
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

double UStarMapRigComponent::OrbitLookScale(
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

void UStarMapRigComponent::AddZoom(float Value)
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

double UStarMapRigComponent::ZoomAltitudeCm(
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

double UStarMapRigComponent::RaySphereFirstHitCm(
	const FVector& RayOrigin,
	const FVector& UnitDir,
	const FVector& SphereCenter,
	double SphereRadiusCm)
{
	// 实现搬到了 StellarEraTerrain/PlanetSurfaceRay（行星编辑的射线也要用）；这里保留同名函数，调用点与测试不用改。
	return PlanetSurfaceRay::RaySphereFirstHit(RayOrigin, UnitDir, SphereCenter, SphereRadiusCm);
}

double UStarMapRigComponent::LimitDistanceByObstaclesCm(
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

double UStarMapRigComponent::ResolveFocusSurfaceRadiusCm(AActor* Focus) const
{
	if (!Focus)
	{
		return 0.0;
	}

	if (const AVoxelPlanetActor* Planet =
		Cast<AVoxelPlanetActor>(UCosmosSimulationSubsystem::ResolveBodyVisualActor(Focus)))
	{
		// 用「焦点 → 相机」方向上的真实地表，否则相机正对山区时会扎进山体。
		const FVector Direction =
			(CameraLocation - Focus->GetActorLocation()).GetSafeNormal();
		return Planet->GetSurfaceRadiusCmAtDirection(Direction);
	}

	// 飞船/角色：只要不把相机塞进模型里。碰撞半径是唯一现成的几何。
	return Focus->GetSimpleCollisionRadius();
}

void UStarMapRigComponent::ComputeOrbitDistanceLimitsCm(
	AActor* Focus, double& OutMinCm, double& OutMaxCm) const
{
	// 行星的下限是地表往上一个 MinSurfaceAltitudeCm；飞船 / 角色用 MinFocusDistanceCm
	// —— 拿 50 米当飞船最近距离，会让停在地面的飞船一进星图就把相机顶到地下。
	const bool bFocusIsPlanet =
		Cast<AVoxelPlanetActor>(UCosmosSimulationSubsystem::ResolveBodyVisualActor(Focus)) != nullptr;
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

		const AVoxelPlanetActor* Planet =
			Cast<AVoxelPlanetActor>(UCosmosSimulationSubsystem::ResolveBodyVisualActor(Body));
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

bool UStarMapRigComponent::ResolveEditTargetByPlanets(
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
		AVoxelPlanetActor* Planet =
			Cast<AVoxelPlanetActor>(UCosmosSimulationSubsystem::ResolveBodyVisualActor(Body));
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

AActor* UStarMapRigComponent::ResolveSelectedBody() const
{
	UWorld* World = GetWorld();
	UCosmosSimulationSubsystem* Subsystem = World
		? World->GetSubsystem<UCosmosSimulationSubsystem>()
		: nullptr;
	return Subsystem ? Subsystem->GetSelectedBody() : nullptr;
}

AActor* UStarMapRigComponent::PickBodyAtScreenPosition(
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

double UStarMapRigComponent::SilhouetteRadiusScale(double CameraDistanceCm, double BodyRadiusCm)
{
	if (BodyRadiusCm <= 0.0 || CameraDistanceCm <= BodyRadiusCm)
	{
		return 0.0;
	}

	// 1/cos α，其中 sin α = R/d ⇒ cos α = √(1 − (R/d)²)。
	const double Ratio = BodyRadiusCm / CameraDistanceCm;
	return 1.0 / FMath::Sqrt(1.0 - Ratio * Ratio);
}

UStarMapRigComponent::UStarMapRigComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UStarMapRigComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// Pawn 在星图里被销毁时没人会走 Exit()，状态不落叶会让 HUD 永远画面板。
	if (bActive)
	{
		if (UCosmosSimulationSubsystem* Subsystem =
			GetWorld() ? GetWorld()->GetSubsystem<UCosmosSimulationSubsystem>() : nullptr)
		{
			Subsystem->SetSelectedBody(nullptr);
			Subsystem->SetStarMapActive(false);
		}
	}

	if (UPlanetEditComponent* Edit = GetPlanetEditComponent())
	{
		Edit->SetTargetResolver(nullptr);
	}
	EndInvokerFollow();
	Super::EndPlay(EndPlayReason);
}

UPlanetEditComponent* UStarMapRigComponent::GetPlanetEditComponent() const
{
	// rig 挂在相机 Pawn 上时，编辑组件仍在源 Pawn（飞船 / 角色）的蓝图里。
	const AActor* Owner = SourcePawn.IsValid() ? SourcePawn.Get() : GetOwner();
	return Owner ? Owner->FindComponentByClass<UPlanetEditComponent>() : nullptr;
}

FVector UStarMapRigComponent::ResolveOwnerCameraLocation() const
{
	// 退场终点是源 Pawn 的相机；相机 Pawn 自己没有相机组件。
	const AActor* Owner = SourcePawn.IsValid() ? SourcePawn.Get() : GetOwner();
	const UCameraComponent* Camera = Owner ? Owner->FindComponentByClass<UCameraComponent>() : nullptr;
	return Camera ? Camera->GetComponentLocation() : CameraLocation;
}

FRotator UStarMapRigComponent::ResolveOwnerCameraRotation() const
{
	const AActor* Owner = SourcePawn.IsValid() ? SourcePawn.Get() : GetOwner();
	const UCameraComponent* Camera = Owner ? Owner->FindComponentByClass<UCameraComponent>() : nullptr;
	return Camera ? Camera->GetComponentRotation() : LastViewRotation;
}

void UStarMapRigComponent::SetSourcePawn(APawn* InSourcePawn)
{
	SourcePawn = InSourcePawn;
}

AActor* UStarMapRigComponent::FindNearestBody(
	const FVector& FromLocation, const TArray<AActor*>& Bodies)
{
	AActor* Best = nullptr;
	double BestDistSq = TNumericLimits<double>::Max();
	for (AActor* Body : Bodies)
	{
		if (!IsValid(Body))
		{
			continue;
		}
		const double DistSq = FVector::DistSquared(FromLocation, Body->GetActorLocation());
		// 严格小于：等距时取先出现的，结果确定。
		if (DistSq < BestDistSq)
		{
			BestDistSq = DistSq;
			Best = Body;
		}
	}
	return Best;
}

void UStarMapRigComponent::AttachOwnerToFocus(AActor* Focus)
{
	if (AActor* Owner = GetOwner(); Owner && IsValid(Focus))
	{
		Owner->AttachToActor(Focus, FAttachmentTransformRules::KeepWorldTransform);
	}
}

void UStarMapRigComponent::EnterFrom(APawn* InSourcePawn, AActor* Focus,
	const FVector& ViewLocation, const FRotator& ViewRotation)
{
	UCosmosSimulationSubsystem* Subsystem =
		GetWorld() ? GetWorld()->GetSubsystem<UCosmosSimulationSubsystem>() : nullptr;
	if (!InSourcePawn || !Subsystem || !Focus)
	{
		return;
	}

	SourcePawn = InSourcePawn;

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

	// 星图激活后 Completed 事件到不了 Pawn，不归零的话推力会一直停在最后按下的值。
	if (ASpaceshipPawn* Ship = Cast<ASpaceshipPawn>(InSourcePawn))
	{
		Ship->SetThrust(0.f);
		Ship->SetRoll(0.f);
		Ship->SetVertical(0.f);
		Ship->SetBoost(false);
	}

	Subsystem->SetSelectedBody(nullptr);
	Subsystem->SetStarMapActive(true);
	Subsystem->SetSelectedBody(Focus);

	FocusOn(Focus, PlanetFocusRadiusScale * StarMapPicking::GetBodyRadiusCm(Focus));

	ApplyStarMapEditSettings();
	if (UPlanetEditComponent* Edit = GetPlanetEditComponent())
	{
		// 星图编辑光标下的行星，不是角色脚下那颗；脚下的贴地网格此时没有意义。
		if (Cast<APlanetCharacter>(InSourcePawn))
		{
			Edit->SetGridPlanet(nullptr);
		}
		Edit->SetTargetResolver(
			[this](const FVector& RayOrigin, const FVector& RayDirection,
				const float MaxDistanceCm, FPlanetEditTarget& OutTarget)
			{
				return ResolveEditTargetByPlanets(RayOrigin, RayDirection, MaxDistanceCm, OutTarget);
			});
	}

	BeginInvokerFollow();
	AttachOwnerToFocus(Focus);
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
	if (APlanetCharacter* Character = Cast<APlanetCharacter>(SourcePawn.Get()))
	{
		Character->InstallEditTargetResolver();
		if (UPlanetEditComponent* Edit = GetPlanetEditComponent())
		{
			Edit->SetGridPlanet(Character->PlanetActor);
		}
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

		// 相机 Pawn 永远挂在当前焦点天体上：行星在动，挂载层级负责跟随。
		AttachOwnerToFocus(Selected);
	}

	if (Selected)
	{
		if (bSmoothFocusActive)
		{
			SmoothFocusElapsedSeconds += DeltaSeconds;

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

	OutResult.Location = CameraLocation;

	// 朝向 = 绕行朝向 + 右键偏移并压回半球；位置仍用 OrbitRotation，所以转头不挪相机。
	// 未选中时放开上限（180° 不夹）：那时相机位置已冻结，否则右键无法自由环顾。
	OutResult.Rotation = ClampAimOffset(
		OrbitRotation, AimOffset, Selected ? MaxAimOffsetDeg : 180.0);

	UpdateInvokerFollow();
	LastViewRotation = OutResult.Rotation;
}

void UStarMapRigComponent::BeginInvokerFollow()
{
	FollowedInvokers.Reset();

	AActor* Owner = SourcePawn.IsValid() ? SourcePawn.Get() : GetOwner();
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
