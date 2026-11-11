// Copyright Voxel Plugin SAS. All Rights Reserved.

#include "CosmosTrajectoryComponent.h"

#include "CosmosOrbitComponent.h"
#include "CosmosSimulationSubsystem.h"
#include "CosmosTypes.h"
#include "CosmosBody.h"
#include "CosmosCurveFilter.h"
#include "VoxelPlanetActor.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UObjectIterator.h"

#include <limits>

namespace
{
	/**
	 * 线宽。**必须传 0**：0 走真正的 1 像素 GPU 线，正值走「世界单位」四边形
	 * （bScreenSpace 写死为 false）—— 近了变粗带、远了直接消失。
	 */
	constexpr float LineThickness = 0.0f;

	/** 深度优先级。SDPG_World = 0：走常规深度测试，星球能挡住它背后的轨道线。 */
	constexpr uint8 LineDepthPriority = SDPG_World;

	/** 箭头段长度 = 该系数 × 杆长。 */
	constexpr double ArrowHeadLengthFraction = 0.15;

	/** 箭头段与杆的夹角。150° 即「从箭尖往回折 30°」。 */
	constexpr double ArrowHeadAngleDegrees = 150.0;

	/** 有限预测的采样点数。步长 = 窗口 / (该值 - 1)。 */
	constexpr int32 PredictionSamples = 256;

	/** 闭合轨道每圈的采样点数。与有限路径的半圈 256 点同密度，点数太少会看到棱角。 */
	constexpr int32 ClosedOrbitSamples = 512;

	/** 自动预测窗口 = 该系数 × 估算周期。略大于 1，让画出的椭圆首尾相接。 */
	constexpr double AutoHorizonPeriodFactor = 1.05;

	/** 估算不出周期时的预测窗口（年）。 */
	constexpr double FallbackPredictionYears = 1.0;

	/** 拖尾的标称采样点数，也是保留点数的上界（至多 513）。 */
	constexpr int32 TrailSamples = 512;

	/** 自动拖尾时长 = 该系数 × 预测窗口。 */
	constexpr double AutoTrailHorizonFraction = 0.25;

	/** 闭合判定的滞回：误差 < 0.3 a 才进入闭环，> 0.45 a 才退出。 */
	constexpr double ClosedOrbitEnterTolerance = 0.3;
	constexpr double ClosedOrbitExitTolerance = 0.45;

	/** 前后两帧轨道面法向夹角超过约 25° 就不混合，直接换线（轨道被拽翻了）。 */
	constexpr double ClosedOrbitBlendMinNormalDot = 0.9;

	/** 星图里选中天体时，颜色朝白插值的比例。 */
	constexpr float HighlightWhiteLerp = 0.45f;
}

UCosmosTrajectoryComponent::UCosmosTrajectoryComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	bTickInEditor = true;   // 编辑器态也要画预测线与箭头（组件级开关，非 tick function）

	// 排在位置写回之后（见类注释）。
	PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
}

void UCosmosTrajectoryComponent::OnRegister()
{
	Super::OnRegister();

	EnsureLineBatch();
}

void UCosmosTrajectoryComponent::OnUnregister()
{
	// 只清线，不销毁行批：outer 是 Actor、重注册时要按名字复用。不 Flush 的话旧的会带着上一版预测线留在场景里。
	if (IsValid(LineBatch))
	{
		LineBatch->Flush();
	}
	bDrewLastFrame = false;

	// 重注册期间不该留着上一帧的箭头；下次 Tick 会按需重新显示。
	SetNetForceArrowVisible(false);

	Super::OnUnregister();
}

ULineBatchComponent* UCosmosTrajectoryComponent::EnsureLineBatch()
{
	if (IsValid(LineBatch))
	{
		// 世界重载/组件重注册后可能处于未注册状态，补一次注册。
		if (!LineBatch->IsRegistered())
		{
			LineBatch->RegisterComponent();
		}
		return LineBatch;
	}

	AActor* Owner = GetOwner();
	if (!Owner)
	{
		LineBatch = nullptr;
		return nullptr;
	}

	// 自己造、自己注册，**不挂到任何父组件下**：线用世界坐标直接交给 PDI，行批自身变换
	// 只影响包围盒，挂在移动中的星球下反而会算错。名字带组件名，重注册时才能找回复用。
	const FName BatchName(*FString::Printf(TEXT("CosmosTrajectoryLineBatch_%s"), *GetName()));

	// 用 FindObjectFast 直接查 FName：UE 5.8 的 FindObject 只接受 FStringView / FTopLevelAssetPath。
	LineBatch = FindObjectFast<ULineBatchComponent>(Owner, BatchName);
	if (!IsValid(LineBatch))
	{
		LineBatch = NewObject<ULineBatchComponent>(Owner, BatchName);
	}

	if (IsValid(LineBatch))
	{
		LineBatch->ComponentTags.AddUnique(TEXT("CosmosTrajectoryLineBatch"));
		if (!LineBatch->IsRegistered())
		{
			LineBatch->RegisterComponent();
		}

		DestroyStaleLineBatches();
	}

	return LineBatch;
}

void UCosmosTrajectoryComponent::DestroyStaleLineBatches()
{
	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return;
	}

	// 只有「本 Actor 只有一个轨迹组件」时才敢清理：多组件时无法凭名字判断哪些是遗留的。
	TArray<UCosmosTrajectoryComponent*> TrajectoryComponents;
	Owner->GetComponents<UCosmosTrajectoryComponent>(TrajectoryComponents);
	if (TrajectoryComponents.Num() != 1)
	{
		return;
	}

	TArray<ULineBatchComponent*> Batches;
	Owner->GetComponents<ULineBatchComponent>(Batches);
	for (ULineBatchComponent* Batch : Batches)
	{
		if (Batch && Batch != LineBatch)
		{
			// 老版本按默认名创建的遗留行批（outer = Actor）。清掉，旧预测线才会消失。
			Batch->DestroyComponent();
		}
	}
}

UCosmosOrbitComponent* UCosmosTrajectoryComponent::GetOrbitComponent()
{
	// 懒解析并缓存。找不到（挂错 Actor）就一直重试，成本只是一次哈希查找。
	if (Orbit)
	{
		return Orbit;
	}

	const AActor* Owner = GetOwner();
	if (!Owner)
	{
		return nullptr;
	}

	Orbit = Owner->FindComponentByClass<UCosmosOrbitComponent>();
	return Orbit;
}

void UCosmosTrajectoryComponent::TickComponent(float DeltaTime, ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!EnsureLineBatch())
	{
		return;
	}

	UCosmosOrbitComponent* OrbitComponent = GetOrbitComponent();
	if (!OrbitComponent)
	{
		// 没挂 Orbit 组件：拿不到任何物理量，画不了，清掉上一帧的线。
		ClearDrawnLines();
		return;
	}

	// ── 编辑器态（不 Play）：走离线重建，完全不碰子系统 ──
	// 编辑器里子系统不 Tick、天体没注册（SimulatorIndex 恒为 INDEX_NONE），运行期路径只会拿到零向量。
	const UWorld* World = GetWorld();
	if (World && !World->IsGameWorld())
	{
		if (bShowInEditor)
		{
			DrawInEditor();
		}
		else
		{
			ClearDrawnLines();
		}
		return;
	}

	const UCosmosSimulationSubsystem* Subsystem =
		GetWorld() ? GetWorld()->GetSubsystem<UCosmosSimulationSubsystem>() : nullptr;
	if (!Subsystem)
	{
		ClearDrawnLines();
		return;
	}

	// 预测窗口拖尾和绘制都要用，先算。
	const double Horizon = ResolvePredictionHorizonYears(*Subsystem);

	// ── 拖尾：采样和裁剪**都**按模拟时间，都不按帧 ──
	const double SimulatedTime = Subsystem->GetSimulatedTimeYears();

	const double TrailSpan = TrailYears > 0.0
		? TrailYears
		: Horizon * AutoTrailHorizonFraction;
	const double TrailInterval = TrailSpan / double(TrailSamples);

	const AActor* OwnerActor = GetOwner();
	const FVector CurrentLocation = OwnerActor
		? OwnerActor->GetActorLocation()
		: FVector::ZeroVector;

	if (Trail.Num() == 0)
	{
		Trail.Add(CurrentLocation);
		TrailTimes.Add(SimulatedTime);
		LastTrailSampleTime = SimulatedTime;
	}
	else if (TrailInterval > 0.0 && SimulatedTime - LastTrailSampleTime >= TrailInterval)
	{
		Trail.Add(CurrentLocation);
		TrailTimes.Add(SimulatedTime);
		LastTrailSampleTime = SimulatedTime;

		const double TrailStartTime = SimulatedTime - TrailSpan;
		int32 DropCount = 0;
		while (DropCount < TrailTimes.Num() && TrailTimes[DropCount] < TrailStartTime)
		{
			++DropCount;
		}
		if (DropCount > 0)
		{
			Trail.RemoveAt(0, DropCount, EAllowShrinking::No);
			TrailTimes.RemoveAt(0, DropCount, EAllowShrinking::No);
		}
	}

	// ── 门控：只门控「画」 ──
	const bool bInStarMapView = Subsystem->IsStarMapActive() || bShowOutsideStarMap;
	const bool bWantTrail = bInStarMapView && bDrawTrail;
	const bool bWantPrediction = bInStarMapView && bDrawPrediction;
	const bool bWantArrow = bInStarMapView && bDrawVelocityArrow;
	const bool bWantGravityArrow = bInStarMapView && bDrawGravityArrow;
	const bool bWantHillSphere = bInStarMapView && bDrawHillSphere;
	const bool bWantDominance = bInStarMapView && bDrawDominancePartition;

	// ── 实体合力箭头：独立门控 ──
	// 属于「操控飞船」而非星图：**不**读 bInStarMapView，也不受 bShowOutsideStarMap 影响
	// （那个开关会把预测线一并拖进游戏画面）。另要求调试视角开着，不再一上船就常驻。
	const APawn* ControlledPawn = Cast<APawn>(OwnerActor);
	const ICosmosBody* ControlledBody = Cast<ICosmosBody>(OwnerActor);
	const bool bWantsNetForceArrow = ShouldShowNetForceArrow(
		bDrawNetForceArrow,
		ControlledPawn != nullptr && ControlledPawn->IsLocallyControlled(),
		Subsystem->IsStarMapActive(),
		ControlledBody != nullptr && ControlledBody->IsDebugViewMode());

	if (bWantsNetForceArrow)
	{
		UpdateNetForceArrow(*Subsystem);
	}
	else
	{
		SetNetForceArrowVisible(false);
	}

	if (!bWantTrail && !bWantPrediction && !bWantArrow && !bWantGravityArrow
		&& !bWantHillSphere && !bWantDominance)
	{
		ClearDrawnLines();
		return;
	}

	// ── 预测：每帧重算 ──
	// 先试闭合轨道（与 PredictionYears 无关）；非椭圆或被拽走时才退回有限窗口。
	PredictedWorld.Reset();
	if (bWantPrediction && OrbitComponent->SimulatorIndex != INDEX_NONE)
	{
		UpdateFilteredPrediction(*Subsystem, OrbitComponent->SimulatorIndex, Horizon,
			CurrentLocation, DeltaTime);
	}
	else
	{
		ResetPredictionFilter();
	}

	// ── 高亮：选中的天体两种颜色都向白提 ──
	const bool bSelected = (OwnerActor != nullptr)
		&& (Subsystem->GetSelectedBody() == OwnerActor);
	const FLinearColor HighlightedTrailColor = bSelected
		? FLinearColor(BrightenTowardWhite(TrailColor, HighlightWhiteLerp))
		: FLinearColor(TrailColor);
	const FLinearColor HighlightedPredictionColor = bSelected
		? FLinearColor(BrightenTowardWhite(PredictionColor, HighlightWhiteLerp))
		: FLinearColor(PredictionColor);

	Lines.Reset();

	if (bWantTrail && Trail.Num() >= 2)
	{
		// 只平滑画出来的副本：采样历史本身不改，否则每帧再平滑一遍会越磨越短。
		TrailDrawPoints = Trail;
		CosmosCurveFilter::SmoothOpen(TrailDrawPoints, CurveSmoothIterations);
		AppendPolyline(TrailDrawPoints, HighlightedTrailColor, TrailThicknessPixels);
	}

	if (bWantPrediction && PredictedWorld.Num() >= 2)
	{
		AppendPolyline(PredictedWorld, HighlightedPredictionColor);
	}

	if (bWantArrow)
	{
		AppendVelocityArrow(*Subsystem, FLinearColor(VelocityArrowColor));
	}

	if (bWantGravityArrow)
	{
		AppendGravityArrow(*Subsystem, FLinearColor(GravityArrowColor));
	}

	// ── 希尔球：球心跟着天体走，半径按当前主导天体现算 ──
	if (bWantHillSphere)
	{
		double HillRadiusCm = 0.0;
		if (ResolveHillSphereRadiusCm(HillRadiusCm) && HillRadiusCm >= MinHillSphereRadiusCm)
		{
			AppendHillSphere(CurrentLocation, HillRadiusCm, FLinearColor(HillSphereColor));
		}
	}

	// ── 瞬时引力主导剖分：逐帧按当前天体位置重算 ──
	if (bWantDominance)
	{
		AppendDominancePartition(CurrentLocation, FLinearColor(DominanceColor));
	}

	LineBatch->Flush();
	if (Lines.Num() > 0)
	{
		LineBatch->DrawLines(Lines);
	}
	bDrewLastFrame = Lines.Num() > 0;
}

void UCosmosTrajectoryComponent::ResetPredictionFilter()
{
	FilteredClosed.Reset();
	FilteredClosedNormal = FVector3d::ZeroVector;
	FilteredOpen.Reset();
	bClosedOrbitLatched = false;
}

void UCosmosTrajectoryComponent::UpdateFilteredPrediction(const UCosmosSimulationSubsystem& Subsystem,
	int32 SimulatorIndex, double HorizonYears, const FVector& SelfLocation, float DeltaTime)
{
	PredictedWorld.Reset();
	const double Alpha = CosmosCurveFilter::SmoothingAlpha(DeltaTime, PredictionSmoothingSeconds);
	const double Tolerance = bClosedOrbitLatched ? ClosedOrbitExitTolerance : ClosedOrbitEnterTolerance;

	FVector PrimaryWorld = FVector::ZeroVector;
	if (Subsystem.PredictClosedOrbitWorld(SimulatorIndex, ClosedOrbitSamples, RawPrediction,
		PrimaryWorld, Tolerance))
	{
		bClosedOrbitLatched = true;
		FilteredOpen.Reset();

		// 相对主导天体：它在动，世界系里混合会让整圈拖在它身后。
		for (FVector3d& Point : RawPrediction)
		{
			Point -= PrimaryWorld;
		}
		CosmosCurveFilter::SmoothClosed(RawPrediction, CurveSmoothIterations);

		FVector3d Normal;
		if (CosmosCurveFilter::ResampleClosedByAngle(RawPrediction, ClosedOrbitSamples,
			ResampledPrediction, Normal))
		{
			const bool bContinuous = FilteredClosed.Num() == ResampledPrediction.Num()
				&& FVector3d::DotProduct(Normal, FilteredClosedNormal) > ClosedOrbitBlendMinNormalDot;
			if (!bContinuous || !CosmosCurveFilter::BlendToward(FilteredClosed, ResampledPrediction, Alpha))
			{
				FilteredClosed = ResampledPrediction;
			}
			FilteredClosedNormal = Normal;
		}
		else
		{
			// 角度不单调（自交）：没法逐点对应，这一帧直接画、不混合。
			FilteredClosed = RawPrediction;
			FilteredClosedNormal = FVector3d::ZeroVector;
		}

		PredictedWorld.Reserve(FilteredClosed.Num());
		for (const FVector3d& Relative : FilteredClosed)
		{
			PredictedWorld.Add(PrimaryWorld + Relative);
		}
		return;
	}

	bClosedOrbitLatched = false;
	FilteredClosed.Reset();
	FilteredClosedNormal = FVector3d::ZeroVector;

	if (!Subsystem.PredictTrajectoryWorld(SimulatorIndex, HorizonYears, PredictionSamples, RawPrediction)
		|| RawPrediction.Num() < 2)
	{
		FilteredOpen.Reset();
		return;
	}

	// 相对自身当前位置、按「距现在的时间」对齐；首点钉在 0，线永远从天体身上出发。
	for (FVector3d& Point : RawPrediction)
	{
		Point -= SelfLocation;
	}
	RawPrediction[0] = FVector3d::ZeroVector;
	CosmosCurveFilter::SmoothOpen(RawPrediction, CurveSmoothIterations);

	if (!CosmosCurveFilter::BlendToward(FilteredOpen, RawPrediction, Alpha))
	{
		FilteredOpen = RawPrediction;
	}
	FilteredOpen[0] = FVector3d::ZeroVector;

	PredictedWorld.Reserve(FilteredOpen.Num());
	for (const FVector3d& Relative : FilteredOpen)
	{
		PredictedWorld.Add(SelfLocation + Relative);
	}
}

void UCosmosTrajectoryComponent::ClearDrawnLines()
{
	if (bDrewLastFrame && IsValid(LineBatch))
	{
		LineBatch->Flush();
	}
	bDrewLastFrame = false;
}

double UCosmosTrajectoryComponent::ClampPredictionHorizonYears(
	double RequestedYears, double OwnPeriodYears, double MaxPeriods)
{
	// 非正 / 非有限 = 调用方没有有效值，原样返回（0 在本组件里表示「自动」）。
	if (!(RequestedYears > 0.0) || !FMath::IsFinite(RequestedYears))
	{
		return RequestedYears;
	}

	// MaxPeriods = 0 表示不设限；周期算不出来（单天体 / 退化配对）时也无从钳起。
	if (!(MaxPeriods > 0.0) || !(OwnPeriodYears > 0.0)
		|| !FMath::IsFinite(OwnPeriodYears))
	{
		return RequestedYears;
	}

	return FMath::Min(RequestedYears, MaxPeriods * OwnPeriodYears);
}

double UCosmosTrajectoryComponent::ResolvePredictionHorizonYears(
	const UCosmosSimulationSubsystem& Subsystem) const
{
	UCosmosOrbitComponent* OrbitComponent = GetOwner()
		? GetOwner()->FindComponentByClass<UCosmosOrbitComponent>()
		: nullptr;

	const int32 SimulatorIndex = OrbitComponent
		? OrbitComponent->SimulatorIndex
		: INDEX_NONE;
	const double OwnPeriod = SimulatorIndex != INDEX_NONE
		? Subsystem.GetOrbitalPeriodYears(SimulatorIndex)
		: 0.0;

	// 手填优先，与估算完全无关。
	double Horizon = FallbackPredictionYears;

	if (PredictionYears > 0.0)
	{
		Horizon = PredictionYears;
	}
	else
	{
		// 自动窗口只在第一次真的算出周期时解析一次，之后一直用缓存值。
		if (!bAutoHorizonResolved && OwnPeriod > 0.0 && FMath::IsFinite(OwnPeriod))
		{
			CachedAutoHorizonYears = OwnPeriod * AutoHorizonPeriodFactor;
			bAutoHorizonResolved = true;
		}

		if (bAutoHorizonResolved)
		{
			Horizon = CachedAutoHorizonYears;
		}
	}

	// 手填与自动吃同一上限：窗口太长时固定步长会粗到画错轨道。上限按**自身**周期而非全系统最短。
	return ClampPredictionHorizonYears(Horizon, OwnPeriod, MaxAutoPredictionPeriods);
}

void UCosmosTrajectoryComponent::AppendPolyline(TArrayView<const FVector> Points,
	const FLinearColor& Color, const float ThicknessPixels)
{
	// 线批的正线宽是世界单位（bScreenSpace 写死 false），远了就没了：按每段到相机的距离
	// 换算成「此处一个像素多宽」，远近都是同样的像素粗细。拿不到相机就退回 1 像素细线。
	FVector CameraLocation = FVector::ZeroVector;
	double WorldPerPixelPerCm = 0.0;
	if (ThicknessPixels > 1.0f)
	{
		const UWorld* World = GetWorld();
		const APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
		if (PC && PC->PlayerCameraManager)
		{
			CameraLocation = PC->PlayerCameraManager->GetCameraLocation();
			FVector2D ViewportSize(1920.0, 1080.0);
			if (GEngine && GEngine->GameViewport)
			{
				GEngine->GameViewport->GetViewportSize(ViewportSize);
			}
			const double HalfFovRad = FMath::DegreesToRadians(double(PC->PlayerCameraManager->GetFOVAngle()) * 0.5);
			WorldPerPixelPerCm = 2.0 * FMath::Tan(HalfFovRad) / FMath::Max(ViewportSize.X, 1.0);
		}
	}

	for (int32 Index = 1; Index < Points.Num(); ++Index)
	{
		float Thickness = LineThickness;
		if (WorldPerPixelPerCm > 0.0)
		{
			const FVector Mid = (Points[Index - 1] + Points[Index]) * 0.5;
			Thickness = float(ThicknessPixels * WorldPerPixelPerCm * FVector::Dist(Mid, CameraLocation));
		}
		Lines.Emplace(Points[Index - 1], Points[Index], Color,
			/*LifeTime=*/0.0f, Thickness, LineDepthPriority);
	}
}

void UCosmosTrajectoryComponent::AppendVelocityArrow(const UCosmosSimulationSubsystem& Subsystem,
	const FLinearColor& Color)
{
	UCosmosOrbitComponent* OrbitComponent = GetOrbitComponent();
	if (!OrbitComponent)
	{
		return;
	}

	const AActor* OwnerActor = GetOwner();
	if (!OwnerActor)
	{
		return;
	}
	const FVector Anchor = OwnerActor->GetActorLocation();

	const FVector Velocity = Subsystem.GetBodyVelocityGameUnits(OrbitComponent->SimulatorIndex);
	const double Speed = Velocity.Size();
	if (Speed <= 0.0 || VelocityArrowScaleCm <= 0.0)
	{
		return;
	}

	const FVector Direction = Velocity.GetSafeNormal();
	if (Direction.IsNearlyZero())
	{
		return;
	}

	const double ShaftLength = VelocityArrowScaleCm * Speed;
	const FVector Tip = Anchor + Direction * ShaftLength;

	Lines.Emplace(Anchor, Tip, Color,
		/*LifeTime=*/0.0f, LineThickness, LineDepthPriority);

	const FVector Reference = FMath::Abs(Direction.Z) < 0.9 ? FVector::UpVector : FVector::RightVector;
	const FVector Perp = FVector::CrossProduct(Direction, Reference).GetSafeNormal();
	if (Perp.IsNearlyZero())
	{
		return;
	}

	const double HeadLength = ShaftLength * ArrowHeadLengthFraction;
	const double HeadAngleRad = FMath::DegreesToRadians(ArrowHeadAngleDegrees);
	const double HeadCos = FMath::Cos(HeadAngleRad);
	const double HeadSin = FMath::Sin(HeadAngleRad);

	Lines.Emplace(Tip, Tip + (Direction * HeadCos + Perp * HeadSin) * HeadLength,
		Color, 0.0f, LineThickness, LineDepthPriority);
	Lines.Emplace(Tip, Tip + (Direction * HeadCos - Perp * HeadSin) * HeadLength,
		Color, 0.0f, LineThickness, LineDepthPriority);
}

void UCosmosTrajectoryComponent::AppendGravityArrow(const UCosmosSimulationSubsystem& Subsystem,
	const FLinearColor& Color)
{
	UCosmosOrbitComponent* OrbitComponent = GetOrbitComponent();
	if (!OrbitComponent)
	{
		return;
	}

	const AActor* OwnerActor = GetOwner();
	if (!OwnerActor)
	{
		return;
	}
	const FVector Anchor = OwnerActor->GetActorLocation();

	const FVector Gravity = Subsystem.GetBodyGravityGameUnits(OrbitComponent->SimulatorIndex);
	const double Magnitude = Gravity.Size();
	if (Magnitude <= 0.0 || GravityArrowScaleCm <= 0.0)
	{
		return;
	}

	const FVector Direction = Gravity.GetSafeNormal();
	if (Direction.IsNearlyZero())
	{
		return;
	}

	const double ShaftLength = GravityArrowScaleCm * Magnitude;
	const FVector Tip = Anchor + Direction * ShaftLength;

	Lines.Emplace(Anchor, Tip, Color,
		/*LifeTime=*/0.0f, LineThickness, LineDepthPriority);

	const FVector Reference = FMath::Abs(Direction.Z) < 0.9 ? FVector::UpVector : FVector::RightVector;
	const FVector Perp = FVector::CrossProduct(Direction, Reference).GetSafeNormal();
	if (Perp.IsNearlyZero())
	{
		return;
	}

	const double HeadLength = ShaftLength * ArrowHeadLengthFraction;
	const double HeadAngleRad = FMath::DegreesToRadians(ArrowHeadAngleDegrees);
	const double HeadCos = FMath::Cos(HeadAngleRad);
	const double HeadSin = FMath::Sin(HeadAngleRad);

	Lines.Emplace(Tip, Tip + (Direction * HeadCos + Perp * HeadSin) * HeadLength,
		Color, 0.0f, LineThickness, LineDepthPriority);
	Lines.Emplace(Tip, Tip + (Direction * HeadCos - Perp * HeadSin) * HeadLength,
		Color, 0.0f, LineThickness, LineDepthPriority);
}

bool UCosmosTrajectoryComponent::ShouldShowNetForceArrow(bool bDrawArrow,
	bool bControlledShip, bool bInStarMap, bool bDebugViewMode)
{
	// 抽成纯函数：内联在 Tick 里只能靠 PIE 肉眼验，「少任何一个条件都不显示」无法断言。
	return bDrawArrow && bControlledShip && !bInStarMap && bDebugViewMode;
}

FLinearColor UCosmosTrajectoryComponent::NetForceArrowColor(double MagnitudeCmPerS2,
	double ColdAtCmPerS2, double HotAtCmPerS2,
	const FLinearColor& Cold, const FLinearColor& Hot)
{
	if (!FMath::IsFinite(MagnitudeCmPerS2) || !FMath::IsFinite(ColdAtCmPerS2)
		|| !FMath::IsFinite(HotAtCmPerS2)
		|| ColdAtCmPerS2 <= 0.0 || HotAtCmPerS2 <= ColdAtCmPerS2)
	{
		return Cold;
	}

	// 先夹到 [ColdAt, HotAt] 再取对数：M ≤ 0 时 ln 无定义，夹一次就顺带挡住了。
	const double Clamped = FMath::Clamp(MagnitudeCmPerS2, ColdAtCmPerS2, HotAtCmPerS2);
	const double Alpha = (FMath::Loge(Clamped) - FMath::Loge(ColdAtCmPerS2))
		/ (FMath::Loge(HotAtCmPerS2) - FMath::Loge(ColdAtCmPerS2));

	return FMath::Lerp(Cold, Hot, static_cast<float>(FMath::Clamp(Alpha, 0.0, 1.0)));
}

bool UCosmosTrajectoryComponent::ComputeNetForceArrowTransforms(
	const FVector& Anchor, const FVector& Direction, double TotalLengthCm,
	double ShaftRadiusCm, double HeadRadiusCm, double HeadFraction,
	FTransform& OutShaft, FTransform& OutHead)
{
	OutShaft = FTransform::Identity;
	OutHead = FTransform::Identity;

	if (!FMath::IsFinite(TotalLengthCm) || TotalLengthCm <= 0.0
		|| !FMath::IsFinite(ShaftRadiusCm) || ShaftRadiusCm <= 0.0
		|| !FMath::IsFinite(HeadRadiusCm) || HeadRadiusCm <= 0.0
		|| Direction.IsNearlyZero())
	{
		return false;
	}

	const FVector Dir = Direction.GetSafeNormal();
	if (Dir.IsNearlyZero())
	{
		return false;
	}

	// 引擎基础形状（Cylinder / Cone）是 100 厘米高、底半径 50 厘米、+Z 朝上。
	constexpr double BasicShapeHeight = 100.0;
	constexpr double BasicShapeRadius = 50.0;

	const double Fraction = FMath::Clamp(HeadFraction, 0.05, 0.95);
	const double HeadLength = TotalLengthCm * Fraction;
	const double ShaftLength = TotalLengthCm - HeadLength;

	// 锥头「底在下、尖在上」，整体前移到杆末端；两者都以自身中点定位，Z 缩放 = 实际长度/100。
	const FRotator Rotation = FRotationMatrix::MakeFromZ(Dir).Rotator();
	const double RadialScale = 1.0 / BasicShapeRadius;

	OutShaft = FTransform(Rotation,
		Anchor + Dir * (ShaftLength * 0.5),
		FVector(ShaftRadiusCm * RadialScale, ShaftRadiusCm * RadialScale,
			ShaftLength / BasicShapeHeight));

	OutHead = FTransform(Rotation,
		Anchor + Dir * (ShaftLength + HeadLength * 0.5),
		FVector(HeadRadiusCm * RadialScale, HeadRadiusCm * RadialScale,
			HeadLength / BasicShapeHeight));

	return true;
}

bool UCosmosTrajectoryComponent::EnsureNetForceArrowMeshes()
{
	if (IsValid(NetForceArrowShaft) && IsValid(NetForceArrowHead))
	{
		return true;
	}

	AActor* OwnerActor = GetOwner();
	if (!OwnerActor || !OwnerActor->GetRootComponent())
	{
		return false;
	}

	UStaticMesh* CylinderMesh = LoadObject<UStaticMesh>(nullptr,
		TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	UStaticMesh* ConeMesh = LoadObject<UStaticMesh>(nullptr,
		TEXT("/Engine/BasicShapes/Cone.Cone"));

	// 尾端钉在 Actor 原点（船体几何中心），整根箭头埋在舰体内，材质必须关深度测试才能看到。
	// M_PlanetGhostOverlay 只比 M_PlanetGhost 多勾了这一项。
	UMaterialInterface* ArrowMaterial = LoadObject<UMaterialInterface>(nullptr,
		TEXT("/Game/Code/Planet/Materials/M_PlanetGhostOverlay.M_PlanetGhostOverlay"));
	if (!ArrowMaterial)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[Cosmos] 找不到 M_PlanetGhostOverlay（关深度测试的箭头材质），退回 M_PlanetGhost；"
				"箭头会被船体遮住，追尾视角看不见。"));
		ArrowMaterial = LoadObject<UMaterialInterface>(nullptr,
			TEXT("/Game/Code/Planet/Materials/M_PlanetGhost.M_PlanetGhost"));
	}

	if (!CylinderMesh || !ConeMesh || !ArrowMaterial)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[Cosmos] 合力箭头缺少基础形状或材质，箭头不显示（Cylinder=%d Cone=%d Material=%d）。"),
			CylinderMesh != nullptr, ConeMesh != nullptr, ArrowMaterial != nullptr);
		return false;
	}

	auto CreatePart = [OwnerActor, ArrowMaterial](const TCHAR* Name, UStaticMesh* Mesh)
		-> UStaticMeshComponent*
	{
		UStaticMeshComponent* Part = NewObject<UStaticMeshComponent>(OwnerActor, FName(Name));
		if (!Part)
		{
			return nullptr;
		}

		Part->SetMobility(EComponentMobility::Movable);
		Part->SetupAttachment(OwnerActor->GetRootComponent());
		Part->SetStaticMesh(Mesh);
		Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Part->SetGenerateOverlapEvents(false);
		Part->SetCastShadow(false);
		Part->SetReceivesDecals(false);
		Part->SetMaterial(0, ArrowMaterial);
		Part->RegisterComponent();
		Part->SetVisibility(false);
		return Part;
	};

	NetForceArrowShaft = CreatePart(TEXT("NetForceArrowShaft"), CylinderMesh);
	NetForceArrowHead = CreatePart(TEXT("NetForceArrowHead"), ConeMesh);
	if (!IsValid(NetForceArrowShaft) || !IsValid(NetForceArrowHead))
	{
		return false;
	}

	// MID 的 Outer 给瞬态包：否则会把资产包拖脏，切图时还可能被旧世界的引用链挂住。
	NetForceArrowMID = UMaterialInstanceDynamic::Create(ArrowMaterial, GetTransientPackage());
	if (NetForceArrowMID)
	{
		NetForceArrowShaft->SetMaterial(0, NetForceArrowMID);
		NetForceArrowHead->SetMaterial(0, NetForceArrowMID);
	}

	return true;
}

void UCosmosTrajectoryComponent::SetNetForceArrowVisible(bool bVisible)
{
	// 不能按缓存状态早退：部件挂在 Hull 下，藏船体的 SetVisibility(..., bPropagateToChildren=true)
	// 会把它们一并改成 false，而缓存仍是 true —— 早退会让箭头永久消失。SetVisibility 自己判等。
	bNetForceArrowVisible = bVisible;

	if (IsValid(NetForceArrowShaft))
	{
		NetForceArrowShaft->SetVisibility(bVisible, /*bPropagateToChildren=*/true);
	}
	if (IsValid(NetForceArrowHead))
	{
		NetForceArrowHead->SetVisibility(bVisible, /*bPropagateToChildren=*/true);
	}
}

void UCosmosTrajectoryComponent::UpdateNetForceArrow(const UCosmosSimulationSubsystem& Subsystem)
{
	UCosmosOrbitComponent* OrbitComponent = GetOrbitComponent();
	const AActor* OwnerActor = GetOwner();
	if (!OrbitComponent || !OwnerActor || OrbitComponent->SimulatorIndex == INDEX_NONE)
	{
		SetNetForceArrowVisible(false);
		return;
	}

	// 纯引力（AU/年²）→ 世界厘米/秒²，换算只走 CosmosUnits。
	const FVector3d Gravity = Subsystem.GetBodyGravityGameUnits(OrbitComponent->SimulatorIndex);
	const FVector3d GravityCmPerS2 = CosmosUnits::GameAccelerationToWorldCmPerS2(
		Gravity, Subsystem.CentimetersPerAu, Subsystem.TimeScale);

	const double Magnitude = GravityCmPerS2.Size();
	if (!FMath::IsFinite(Magnitude) || Magnitude <= 0.0)
	{
		// 完全失重：方向无定义，宁可隐藏也不留一根指向随机方向的箭头。
		SetNetForceArrowVisible(false);
		return;
	}

	const FVector Direction(GravityCmPerS2 / Magnitude);
	if (Direction.IsNearlyZero())
	{
		SetNetForceArrowVisible(false);
		return;
	}

	// 默认 Offset 为 0（起点钉在飞船 0,0,0）；调大会把箭头抬到船背上方、露出悬空段。
	const FVector Anchor = OwnerActor->GetActorLocation()
		+ OwnerActor->GetActorUpVector() * NetForceArrowOffsetCm;

	FTransform ShaftTransform;
	FTransform HeadTransform;
	if (!ComputeNetForceArrowTransforms(Anchor, Direction, NetForceArrowLengthCm,
		NetForceArrowShaftRadiusCm, NetForceArrowHeadRadiusCm, NetForceArrowHeadFraction,
		ShaftTransform, HeadTransform))
	{
		SetNetForceArrowVisible(false);
		return;
	}

	if (!EnsureNetForceArrowMeshes())
	{
		return;
	}

	NetForceArrowShaft->SetWorldTransform(ShaftTransform);
	NetForceArrowHead->SetWorldTransform(HeadTransform);

	if (NetForceArrowMID)
	{
		NetForceArrowMID->SetVectorParameterValue(TEXT("ShellColor"),
			NetForceArrowColor(Magnitude, NetForceArrowColdAtCmPerS2,
				NetForceArrowHotAtCmPerS2, NetForceArrowColdColor, NetForceArrowHotColor));
		NetForceArrowMID->SetScalarParameterValue(TEXT("Opacity"), NetForceArrowOpacity);
	}

	SetNetForceArrowVisible(true);
}

bool UCosmosTrajectoryComponent::ComputeHillRadiusCm(double BodyMass, double PrimaryMass,
	double DistanceCm, double& OutRadiusCm)
{
	OutRadiusCm = 0.0;

	if (!FMath::IsFinite(BodyMass) || !FMath::IsFinite(PrimaryMass)
		|| !FMath::IsFinite(DistanceCm))
	{
		return false;
	}
	if (BodyMass <= 0.0 || PrimaryMass <= 0.0 || DistanceCm <= 0.0)
	{
		return false;
	}
	if (BodyMass >= PrimaryMass)
	{
		// 希尔近似的前提是次体远轻于主星；一样重或更重时这个公式没有意义。
		return false;
	}

	const double MassRatio = BodyMass / (3.0 * PrimaryMass);
	const double RadiusCm = DistanceCm * FMath::Pow(MassRatio, 1.0 / 3.0);
	if (!FMath::IsFinite(RadiusCm) || RadiusCm <= 0.0)
	{
		return false;
	}

	OutRadiusCm = RadiusCm;
	return true;
}

bool UCosmosTrajectoryComponent::ComputeDominanceBoundary(const FVector3d& PositionA,
	double MassA, const FVector3d& PositionB, double MassB,
	FVector3d& OutCenter, double& OutRadiusCm, FVector3d& OutNormal)
{
	OutCenter = FVector3d::ZeroVector;
	OutRadiusCm = 0.0;
	OutNormal = FVector3d::ZeroVector;

	if (!FMath::IsFinite(PositionA.X) || !FMath::IsFinite(PositionA.Y)
		|| !FMath::IsFinite(PositionA.Z) || !FMath::IsFinite(PositionB.X)
		|| !FMath::IsFinite(PositionB.Y) || !FMath::IsFinite(PositionB.Z)
		|| !FMath::IsFinite(MassA) || !FMath::IsFinite(MassB)
		|| MassA <= 0.0 || MassB <= 0.0)
	{
		return false;
	}

	const FVector3d Delta = PositionB - PositionA;
	const double Distance = Delta.Size();
	if (!FMath::IsFinite(Distance) || Distance <= 0.0)
	{
		return false;
	}
	OutNormal = Delta / Distance;

	// k = sqrt(mA/mB)：边界上 |P-A|/|P-B| = k。
	const double K = FMath::Sqrt(MassA / MassB);
	const double Denominator = 1.0 - K * K;

	// 等质量：球半径发散，退化为中垂面。用 1e-9 把「数值上等重」也归进来，
	// 否则会得到一个半径约 1/|1-k²| 倍间距的巨球，采样全在裁剪球外，什么都画不出。
	if (FMath::Abs(Denominator) <= 1.0e-9)
	{
		OutCenter = (PositionA + PositionB) * 0.5;
		return true;
	}

	OutCenter = (PositionA - PositionB * (K * K)) / Denominator;
	OutRadiusCm = K * Distance / FMath::Abs(Denominator);

	if (!FMath::IsFinite(OutCenter.X) || !FMath::IsFinite(OutCenter.Y)
		|| !FMath::IsFinite(OutCenter.Z) || !FMath::IsFinite(OutRadiusCm))
	{
		OutCenter = FVector3d::ZeroVector;
		OutRadiusCm = 0.0;
		OutNormal = FVector3d::ZeroVector;
		return false;
	}
	return true;
}

bool UCosmosTrajectoryComponent::ArePairDominantAt(const FVector3d& Point,
	const TArray<FVector3d>& Positions, const TArray<double>& Masses,
	int32 IndexA, int32 IndexB)
{
	if (IndexA == IndexB || Positions.Num() != Masses.Num()
		|| !Positions.IsValidIndex(IndexA) || !Positions.IsValidIndex(IndexB))
	{
		return false;
	}
	if (Masses[IndexA] <= 0.0 || Masses[IndexB] <= 0.0)
	{
		return false;
	}

	const double DistanceA2 = (Point - Positions[IndexA]).SizeSquared();
	const double DistanceB2 = (Point - Positions[IndexB]).SizeSquared();
	if (DistanceA2 <= 0.0 || DistanceB2 <= 0.0)
	{
		return false;   // 采样点正好压在天体上，不在边界上。
	}

	const double PairPull = FMath::Max(Masses[IndexA] / DistanceA2,
		Masses[IndexB] / DistanceB2);
	if (!FMath::IsFinite(PairPull))
	{
		return false;
	}

	// 其余天体只需和 PairPull 比：两人都不输给第三名 ⇔ 两人就是前两名。
	constexpr double Tolerance = 1.0e-9;
	for (int32 Index = 0; Index < Positions.Num(); ++Index)
	{
		if (Index == IndexA || Index == IndexB || Masses[Index] <= 0.0)
		{
			continue;
		}
		const double Distance2 = (Point - Positions[Index]).SizeSquared();
		const double OtherPull = Distance2 > 0.0
			? Masses[Index] / Distance2
			: std::numeric_limits<double>::infinity();
		if (OtherPull > PairPull * (1.0 + Tolerance))
		{
			return false;
		}
	}
	return true;
}

bool UCosmosTrajectoryComponent::ResolveHillSphereRadiusCm(double& OutRadiusCm)
{
	OutRadiusCm = 0.0;

	const AActor* SelfActor = GetOwner();
	UCosmosOrbitComponent* SelfOrbit = GetOrbitComponent();
	const UWorld* World = GetWorld();
	if (!SelfActor || !SelfOrbit || !World)
	{
		return false;
	}
	if (!SelfOrbit->bExertsGravity)
	{
		return false;   // 不产生引力的天体（飞船）没有「引力主导范围」
	}

	const FVector SelfLocation = SelfActor->GetActorLocation();
	const UCosmosOrbitComponent* Primary = nullptr;
	double BestPull = 0.0;
	double PrimaryDistanceCm = 0.0;

	for (TObjectIterator<UCosmosOrbitComponent> It; It; ++It)
	{
		const UCosmosOrbitComponent* Candidate = *It;
		if (!Candidate || Candidate == SelfOrbit || !Candidate->bExertsGravity)
		{
			continue;
		}

		const AActor* CandidateActor = Candidate->GetOwner();
		if (!CandidateActor || CandidateActor->GetWorld() != World)
		{
			continue;   // 只收本关卡的天体
		}
		if (!FMath::IsFinite(Candidate->MassEarth) || Candidate->MassEarth <= 0.0)
		{
			continue;
		}

		const double DistanceCm = FVector::Dist(SelfLocation, CandidateActor->GetActorLocation());
		if (DistanceCm <= 0.0)
		{
			continue;
		}

		// 引力强度 ∝ M / d²：只取最强的一颗当主星。
		const double Pull = Candidate->MassEarth / (DistanceCm * DistanceCm);
		if (Pull > BestPull)
		{
			BestPull = Pull;
			Primary = Candidate;
			PrimaryDistanceCm = DistanceCm;
		}
	}

	if (!Primary)
	{
		return false;
	}

	return ComputeHillRadiusCm(SelfOrbit->MassEarth, Primary->MassEarth,
		PrimaryDistanceCm, OutRadiusCm);
}

void UCosmosTrajectoryComponent::AppendHillSphere(const FVector& Center, double RadiusCm,
	const FLinearColor& Color)
{
	if (!FMath::IsFinite(RadiusCm) || RadiusCm <= 0.0)
	{
		return;
	}

	const FVector3d CenterD(Center);
	const int32 Segments = FMath::Clamp(HillSphereSegments, 3, 128);

	auto AppendCircle = [this, &CenterD, RadiusCm, Segments, &Color](
		const FVector3d& AxisU, const FVector3d& AxisV)
	{
		FVector3d Previous = CenterD + AxisU * RadiusCm;
		for (int32 Segment = 1; Segment <= Segments; ++Segment)
		{
			const double Angle = 2.0 * UE_DOUBLE_PI * double(Segment) / double(Segments);
			const FVector3d Point = CenterD
				+ (AxisU * FMath::Cos(Angle) + AxisV * FMath::Sin(Angle)) * RadiusCm;
			Lines.Emplace(FVector(Previous), FVector(Point), Color,
				0.0f, LineThickness, LineDepthPriority);
			Previous = Point;
		}
	};

	// 纬线：固定纬度的水平圆。不含两极（极点退化成一点，画不出线）。
	const int32 LatitudeRings = FMath::Clamp(HillSphereLatitudeRings, 0, 16);
	for (int32 Ring = 0; Ring < LatitudeRings; ++Ring)
	{
		const double Latitude = -0.5 * UE_DOUBLE_PI
			+ UE_DOUBLE_PI * double(Ring + 1) / double(LatitudeRings + 1);
		const double RingRadius = RadiusCm * FMath::Cos(Latitude);
		const FVector3d RingCenter = CenterD
			+ FVector3d(0.0, 0.0, RadiusCm * FMath::Sin(Latitude));

		FVector3d Previous = RingCenter + FVector3d(RingRadius, 0.0, 0.0);
		for (int32 Segment = 1; Segment <= Segments; ++Segment)
		{
			const double Angle = 2.0 * UE_DOUBLE_PI * double(Segment) / double(Segments);
			const FVector3d Point = RingCenter + FVector3d(
				RingRadius * FMath::Cos(Angle),
				RingRadius * FMath::Sin(Angle),
				0.0);
			Lines.Emplace(FVector(Previous), FVector(Point), Color,
				0.0f, LineThickness, LineDepthPriority);
			Previous = Point;
		}
	}

	// 赤道
	AppendCircle(FVector3d::UnitX(), FVector3d::UnitY());

	// 经线：每条都是过两极的大圆。
	const int32 LongitudeRings = FMath::Clamp(HillSphereLongitudeRings, 3, 32);
	for (int32 Ring = 0; Ring < LongitudeRings; ++Ring)
	{
		const double Angle = UE_DOUBLE_PI * double(Ring) / double(LongitudeRings);
		const FVector3d Horizontal(FMath::Cos(Angle), FMath::Sin(Angle), 0.0);
		AppendCircle(FVector3d::UnitZ(), Horizontal);
	}
}

bool UCosmosTrajectoryComponent::ShouldDrawDominancePair(double SelfMassEarth,
	double OtherMassEarth, const AActor& SelfActor, const AActor& OtherActor) const
{
	// 对方有开着的剖分组件就交给「较重者/名字」规则去重；没有则本组件负责，否则只开一个时画不出来。
	const bool bEditorWorld = GetWorld() != nullptr && !GetWorld()->IsGameWorld();
	bool bOtherEnabled = false;
	TArray<UCosmosTrajectoryComponent*> OtherTrajectories;
	OtherActor.GetComponents<UCosmosTrajectoryComponent>(OtherTrajectories);
	for (const UCosmosTrajectoryComponent* OtherTrajectory : OtherTrajectories)
	{
		if (OtherTrajectory && OtherTrajectory != this
			&& OtherTrajectory->bDrawDominancePartition
			&& (!bEditorWorld || OtherTrajectory->bShowInEditor))
		{
			bOtherEnabled = true;
			break;
		}
	}
	if (!bOtherEnabled)
	{
		return true;
	}

	// 较重者画；等重按 Actor 名排序，保证全局只有一方画。
	if (SelfMassEarth != OtherMassEarth)
	{
		return SelfMassEarth > OtherMassEarth;
	}
	return SelfActor.GetName() < OtherActor.GetName();
}

void UCosmosTrajectoryComponent::AppendDominancePartition(const FVector& Anchor,
	const FLinearColor& Color)
{
	UWorld* World = GetWorld();
	AActor* SelfActor = GetOwner();
	UCosmosOrbitComponent* SelfOrbit = GetOrbitComponent();
	if (!World || !SelfActor || !SelfOrbit || !SelfOrbit->bExertsGravity)
	{
		return;   // 飞船等不产生引力的天体没有主导区
	}

	// ── 收集本关卡所有产生引力的天体（每个 Actor 一个），按名字排序保证确定性 ──
	TArray<AActor*> Actors;
	for (TObjectIterator<UCosmosOrbitComponent> It; It; ++It)
	{
		UCosmosOrbitComponent* Candidate = *It;
		if (!Candidate || Candidate->GetWorld() != World || !Candidate->bExertsGravity)
		{
			continue;
		}
		AActor* CandidateActor = Candidate->GetOwner();
		if (!CandidateActor || Actors.Contains(CandidateActor))
		{
			continue;
		}
		if (!FMath::IsFinite(Candidate->MassEarth) || Candidate->MassEarth <= 0.0)
		{
			continue;   // 与求解器同口径：无效质量不参与
		}
		Actors.Add(CandidateActor);
	}
	if (Actors.Num() < 2)
	{
		return;
	}
	// TArray<AActor*>::Sort 会用 TDereferenceWrapper 解引用元素，谓词收到的是 Actor 引用。
	Actors.Sort([](const AActor& A, const AActor& B)
	{
		return A.GetName() < B.GetName();
	});

	TArray<FVector3d> Positions;
	TArray<double> Masses;
	Positions.Reserve(Actors.Num());
	Masses.Reserve(Actors.Num());

	int32 SelfIndex = INDEX_NONE;
	for (int32 Index = 0; Index < Actors.Num(); ++Index)
	{
		AActor* CandidateActor = Actors[Index];
		Positions.Add(FVector3d(CandidateActor->GetActorLocation()));
		Masses.Add(0.0);

		if (CandidateActor == SelfActor)
		{
			SelfIndex = Index;
			Masses[Index] = SelfOrbit->MassEarth;
			continue;
		}

		// 每个 Actor 取第一个产生引力的 Orbit 组件的质量（正常情况下只有一个）。
		TArray<UCosmosOrbitComponent*> CandidateOrbits;
		CandidateActor->GetComponents<UCosmosOrbitComponent>(CandidateOrbits);
		for (const UCosmosOrbitComponent* CandidateOrbit : CandidateOrbits)
		{
			if (CandidateOrbit && CandidateOrbit->bExertsGravity
				&& FMath::IsFinite(CandidateOrbit->MassEarth)
				&& CandidateOrbit->MassEarth > 0.0)
			{
				Masses[Index] = CandidateOrbit->MassEarth;
				break;
			}
		}
	}

	if (SelfIndex == INDEX_NONE || Masses[SelfIndex] <= 0.0)
	{
		return;
	}

	const double ClipRadiusCm = FMath::Max(DominanceClipRadiusCm, 1.0);
	const FVector3d AnchorD(Anchor);

	for (int32 OtherIndex = 0; OtherIndex < Actors.Num(); ++OtherIndex)
	{
		if (OtherIndex == SelfIndex)
		{
			continue;
		}

		AActor* OtherActor = Actors[OtherIndex];
		if (!ShouldDrawDominancePair(Masses[SelfIndex], Masses[OtherIndex],
			*SelfActor, *OtherActor))
		{
			continue;
		}

		FVector3d Center;
		double RadiusCm = 0.0;
		FVector3d Normal;
		if (!ComputeDominanceBoundary(Positions[SelfIndex], Masses[SelfIndex],
			Positions[OtherIndex], Masses[OtherIndex], Center, RadiusCm, Normal))
		{
			continue;
		}

		auto Keep = [&](const FVector3d& Point)
		{
			if ((Point - AnchorD).SizeSquared() > ClipRadiusCm * ClipRadiusCm)
			{
				return false;   // 无界区域按本天体为中心的半径裁剪
			}
			return ArePairDominantAt(Point, Positions, Masses, SelfIndex, OtherIndex);
		};
		auto AddSegment = [&](const FVector3d& From, const FVector3d& To)
		{
			Lines.Emplace(FVector(From), FVector(To), Color,
				0.0f, LineThickness, LineDepthPriority);
		};

		// 等质量：中垂面上的正交网格（半径 0）。球半径过小时跳过。
		if (RadiusCm < MinDominanceRadiusCm)
		{
			if (RadiusCm > 0.0)
			{
				continue;
			}

			const FVector3d AxisU = Normal.GetSafeNormal();
			const FVector3d Reference = FMath::Abs(AxisU.Z) < 0.9
				? FVector3d(0.0, 0.0, 1.0)
				: FVector3d(1.0, 0.0, 0.0);
			const FVector3d AxisV = (Reference ^ AxisU).GetSafeNormal();
			const FVector3d AxisW = AxisU ^ AxisV;

			const int32 Segments = FMath::Max(DominancePlaneSegments, 2);
			const double CellSize = 2.0 * ClipRadiusCm / double(Segments);
			const double Half = 0.5 * double(Segments) * CellSize;
			const int32 Stride = Segments + 1;

			TArray<uint8> KeepFlags;
			KeepFlags.SetNumZeroed(Stride * Stride);
			auto PointAt = [&](int32 U, int32 V)
			{
				return Center + AxisV * (-Half + double(U) * CellSize)
					+ AxisW * (-Half + double(V) * CellSize);
			};
			auto FlagAt = [&](int32 U, int32 V)
			{
				return KeepFlags[V * Stride + U] != 0;
			};

			for (int32 V = 0; V <= Segments; ++V)
			{
				for (int32 U = 0; U <= Segments; ++U)
				{
					KeepFlags[V * Stride + U] = Keep(PointAt(U, V)) ? 1 : 0;
				}
			}
			for (int32 V = 0; V <= Segments; ++V)
			{
				for (int32 U = 0; U <= Segments; ++U)
				{
					if (!FlagAt(U, V))
					{
						continue;
					}
					if (U < Segments && FlagAt(U + 1, V))
					{
						AddSegment(PointAt(U, V), PointAt(U + 1, V));
					}
					if (V < Segments && FlagAt(U, V + 1))
					{
						AddSegment(PointAt(U, V), PointAt(U, V + 1));
					}
				}
			}
			continue;
		}

		// 球面：以边界轴为极轴的经纬网格，只连两端都保留的相邻点。
		const FVector3d AxisU = Normal.GetSafeNormal();
		const FVector3d Reference = FMath::Abs(AxisU.Z) < 0.9
			? FVector3d(0.0, 0.0, 1.0)
			: FVector3d(1.0, 0.0, 0.0);
		const FVector3d AxisV = (Reference ^ AxisU).GetSafeNormal();
		const FVector3d AxisW = AxisU ^ AxisV;

		const int32 Longitude = FMath::Max(DominanceSphereSegments, 3);
		const int32 LatitudeCount = FMath::Clamp(DominanceSphereRings, 1, 32) + 1;

		TArray<uint8> RingKeep;
		RingKeep.SetNumZeroed(Longitude * LatitudeCount);
		TArray<FVector3d> Points;
		Points.SetNumUninitialized(Longitude * LatitudeCount);

		for (int32 Ring = 0; Ring < LatitudeCount; ++Ring)
		{
			const double Phi = UE_DOUBLE_PI * double(Ring) / double(LatitudeCount);
			const double SinPhi = FMath::Sin(Phi);
			const double CosPhi = FMath::Cos(Phi);
			for (int32 Segment = 0; Segment < Longitude; ++Segment)
			{
				const double Theta = 2.0 * UE_DOUBLE_PI * double(Segment) / double(Longitude);
				const FVector3d Unit = AxisU * CosPhi
					+ AxisV * (SinPhi * FMath::Cos(Theta))
					+ AxisW * (SinPhi * FMath::Sin(Theta));
				const int32 PointIndex = Ring * Longitude + Segment;
				Points[PointIndex] = Center + Unit * RadiusCm;
				RingKeep[PointIndex] = Keep(Points[PointIndex]) ? 1 : 0;
			}
		}

		for (int32 Ring = 0; Ring < LatitudeCount; ++Ring)
		{
			for (int32 Segment = 0; Segment < Longitude; ++Segment)
			{
				const int32 Index = Ring * Longitude + Segment;
				if (!RingKeep[Index])
				{
					continue;
				}
				const int32 NextSegment = (Segment + 1) % Longitude;
				if (RingKeep[Ring * Longitude + NextSegment])
				{
					AddSegment(Points[Index], Points[Ring * Longitude + NextSegment]);
				}
				if (Ring + 1 < LatitudeCount
					&& RingKeep[(Ring + 1) * Longitude + Segment])
				{
					AddSegment(Points[Index], Points[(Ring + 1) * Longitude + Segment]);
				}
			}
		}
	}
}

void UCosmosTrajectoryComponent::DrawInEditor()
{
	if (!GetWorld() || !IsValid(LineBatch))
	{
		return;
	}

	const UWorld* World = GetWorld();

	// ── 标定值从子系统读 ──
	double CmPerAu = 1.0e6;
	double Softening = 0.0001;
	if (const UCosmosSimulationSubsystem* Subsystem =
		World->GetSubsystem<UCosmosSimulationSubsystem>())
	{
		CmPerAu = Subsystem->CentimetersPerAu;
		Softening = Subsystem->SofteningLength;
	}

	// ── 离线重建求解器 ──
	FNBodySimulator EditorSimulator;
	EditorSimulator.SetUnitSystem(ECosmosUnitSystem::Game);
	EditorSimulator.SetIntegrator(ECosmosIntegrator::VelocityVerlet);
	EditorSimulator.SetSofteningLength(Softening);
	EditorSimulator.SetCentimetersPerAu(CmPerAu);

	for (TObjectIterator<UCosmosOrbitComponent> It; It; ++It)
	{
		UCosmosOrbitComponent* Component = *It;
		if (!Component)
		{
			continue;
		}

		AActor* OwnerActor = Component->GetOwner();
		if (!OwnerActor || OwnerActor->GetWorld() != World)
		{
			continue;   // 只收本关卡的天体
		}

		if (!FMath::IsFinite(Component->MassEarth) || Component->MassEarth <= 0.0)
		{
			continue;   // 与 AddBodyToSimulator 同口径：无效质量不参与预览
		}

		FCosmosBodyState State;
		State.Position = EditorSimulator.ToGameUnitPosition(OwnerActor->GetActorLocation());
		State.Velocity = CosmosUnits::KilometerPerSecondToAuPerYear(
			FVector3d(Component->InitialVelocityKmPerSecond));
		State.Mass = CosmosUnits::EarthMassToSolarMass(Component->MassEarth);
		State.OwnerActor = OwnerActor;

		// 与 AddBodyToSimulator 同口径：单向引力天体在预览里也不拽别人，否则会看着行星被飞船拉偏。
		State.bTestParticle = !Component->bExertsGravity;

		// 接触形状必须跟运行期一致：全是点质量时飞船会穿过中心星扎进软化核、被踢成一条
		// 直线。半径来源同 AddBodyToSimulator；预览从零重建，用平均球半径即可。
		if (const AVoxelPlanetActor* Planet = Cast<AVoxelPlanetActor>(OwnerActor))
		{
			State.Radius = double(Planet->GetSurfaceRadiusCm()) / CmPerAu;
		}
		else if (const ICosmosBody* Ship = Cast<ICosmosBody>(OwnerActor))
		{
			State.Radius = double(Ship->GetContactRadiusCm()) / CmPerAu;

			// 撞上即停，与运行期同口径；否则飞船会沿表面滑行，预测线贴着星球绕圈。
			State.bZeroVelocityOnContact = true;
		}

		EditorSimulator.AddBody(State);
	}

	const AActor* SelfActor = GetOwner();
	if (!SelfActor)
	{
		return;
	}

	// 自己这颗天体在离线求解器里的下标：按 OwnerActor 匹配。
	int32 SelfIndex = INDEX_NONE;
	{
		const TArray<FCosmosBodyState>& Bodies = EditorSimulator.GetBodies();
		for (int32 Index = 0; Index < Bodies.Num(); ++Index)
		{
			if (Bodies[Index].OwnerActor.Get() == SelfActor)
			{
				SelfIndex = Index;
				break;
			}
		}
	}

	if (SelfIndex == INDEX_NONE)
	{
		return;   // 自己这颗天体没被收进离线求解器（没挂 Orbit 组件），画不了
	}

	const FVector Anchor = SelfActor->GetActorLocation();
	Lines.Reset();

	// ── 预测折线 ──
	if (bDrawPrediction)
	{
		PredictedWorld.Reset();

		// 先试闭合（与 PredictionYears 无关），相对位置锚到主导天体 t=0 处再转世界坐标。
		FCosmosClosedOrbitPrediction ClosedOrbit;
		const bool bUsedClosedOrbit =
			EditorSimulator.PredictClosedOrbit(SelfIndex, ClosedOrbitSamples, ClosedOrbit);

		if (bUsedClosedOrbit)
		{
			PredictedWorld.Reserve(ClosedOrbit.RelativePositions.Num());
			for (const FVector3d& Relative : ClosedOrbit.RelativePositions)
			{
				PredictedWorld.Add(EditorSimulator.ToWorldLocation(
					Relative + ClosedOrbit.PrimaryPosition));
			}
		}
		else
		{
			// 手填优先；自动 = 1.05 × **自身**最短周期（不是全系统最短，否则飞船会把行星的椭圆压成一小段）。
			double Horizon = PredictionYears > 0.0 ? PredictionYears : FallbackPredictionYears;
			const double OwnPeriod = EditorSimulator.EstimateOrbitalPeriod(SelfIndex);
			if (PredictionYears <= 0.0 && OwnPeriod > 0.0 && FMath::IsFinite(OwnPeriod))
			{
				Horizon = OwnPeriod * AutoHorizonPeriodFactor;
			}
			Horizon = ClampPredictionHorizonYears(Horizon, OwnPeriod, MaxAutoPredictionPeriods);

			TArray<FVector3d> GameUnitPositions;
			if (EditorSimulator.PredictTrajectory(SelfIndex, Horizon, PredictionSamples,
				GameUnitPositions))
			{
				PredictedWorld.Reserve(GameUnitPositions.Num());
				for (const FVector3d& Position : GameUnitPositions)
				{
					PredictedWorld.Add(EditorSimulator.ToWorldLocation(Position));
				}
			}
		}

		// 编辑器态不 Tick 求解器、线不跳，只做空间平滑。平移不影响线性平滑，世界系里直接做。
		if (bUsedClosedOrbit)
		{
			CosmosCurveFilter::SmoothClosed(PredictedWorld, CurveSmoothIterations);
		}
		else
		{
			CosmosCurveFilter::SmoothOpen(PredictedWorld, CurveSmoothIterations);
		}

		if (PredictedWorld.Num() >= 2)
		{
			AppendPolyline(PredictedWorld, FLinearColor(PredictionColor));
		}
	}

	// ── 速度箭头（InitialVelocityKmPerSecond） ──
	if (bDrawVelocityArrow)
	{
		UCosmosOrbitComponent* OrbitComponent = GetOrbitComponent();
		AppendEditorVelocityArrow(Anchor,
			OrbitComponent
				? CosmosUnits::KilometerPerSecondToAuPerYear(
					FVector3d(OrbitComponent->InitialVelocityKmPerSecond))
				: FVector3d::ZeroVector,
			FLinearColor(VelocityArrowColor));
	}

	// ── 引力箭头（离线算出的纯引力） ──
	if (bDrawGravityArrow)
	{
		EditorSimulator.StepExact(1e-6);
		AppendEditorGravityArrow(Anchor,
			EditorSimulator.GetBody(SelfIndex).GravitationalAcceleration,
			FLinearColor(GravityArrowColor));
	}

	// ── 希尔球：只用质量与摆放位置，不需要求解器状态 ──
	if (bDrawHillSphere)
	{
		double HillRadiusCm = 0.0;
		if (ResolveHillSphereRadiusCm(HillRadiusCm) && HillRadiusCm >= MinHillSphereRadiusCm)
		{
			AppendHillSphere(Anchor, HillRadiusCm, FLinearColor(HillSphereColor));
		}
	}

	// ── 瞬时引力主导剖分：编辑器态只用摆放位置与质量 ──
	if (bDrawDominancePartition)
	{
		AppendDominancePartition(Anchor, FLinearColor(DominanceColor));
	}

	LineBatch->Flush();
	if (Lines.Num() > 0)
	{
		LineBatch->DrawLines(Lines);
	}
	bDrewLastFrame = Lines.Num() > 0;
}

void UCosmosTrajectoryComponent::AppendEditorVelocityArrow(const FVector& Anchor,
	const FVector3d& Velocity, const FLinearColor& Color)
{
	const double Speed = Velocity.Size();
	if (Speed <= 0.0 || VelocityArrowScaleCm <= 0.0)
	{
		return;
	}

	const FVector Direction = FVector(Velocity).GetSafeNormal();
	if (Direction.IsNearlyZero())
	{
		return;
	}

	const double ShaftLength = VelocityArrowScaleCm * Speed;
	const FVector Tip = Anchor + Direction * ShaftLength;

	Lines.Emplace(Anchor, Tip, Color,
		/*LifeTime=*/0.0f, LineThickness, LineDepthPriority);

	const FVector Reference = FMath::Abs(Direction.Z) < 0.9 ? FVector::UpVector : FVector::RightVector;
	const FVector Perp = FVector::CrossProduct(Direction, Reference).GetSafeNormal();
	if (Perp.IsNearlyZero())
	{
		return;
	}

	const double HeadLength = ShaftLength * ArrowHeadLengthFraction;
	const double HeadAngleRad = FMath::DegreesToRadians(ArrowHeadAngleDegrees);
	const double HeadCos = FMath::Cos(HeadAngleRad);
	const double HeadSin = FMath::Sin(HeadAngleRad);

	Lines.Emplace(Tip, Tip + (Direction * HeadCos + Perp * HeadSin) * HeadLength,
		Color, 0.0f, LineThickness, LineDepthPriority);
	Lines.Emplace(Tip, Tip + (Direction * HeadCos - Perp * HeadSin) * HeadLength,
		Color, 0.0f, LineThickness, LineDepthPriority);
}

void UCosmosTrajectoryComponent::AppendEditorGravityArrow(const FVector& Anchor,
	const FVector3d& Gravity, const FLinearColor& Color)
{
	const double Magnitude = Gravity.Size();
	if (Magnitude <= 0.0 || GravityArrowScaleCm <= 0.0)
	{
		return;
	}

	const FVector Direction = FVector(Gravity).GetSafeNormal();
	if (Direction.IsNearlyZero())
	{
		return;
	}

	const double ShaftLength = GravityArrowScaleCm * Magnitude;
	const FVector Tip = Anchor + Direction * ShaftLength;

	Lines.Emplace(Anchor, Tip, Color,
		/*LifeTime=*/0.0f, LineThickness, LineDepthPriority);

	const FVector Reference = FMath::Abs(Direction.Z) < 0.9 ? FVector::UpVector : FVector::RightVector;
	const FVector Perp = FVector::CrossProduct(Direction, Reference).GetSafeNormal();
	if (Perp.IsNearlyZero())
	{
		return;
	}

	const double HeadLength = ShaftLength * ArrowHeadLengthFraction;
	const double HeadAngleRad = FMath::DegreesToRadians(ArrowHeadAngleDegrees);
	const double HeadCos = FMath::Cos(HeadAngleRad);
	const double HeadSin = FMath::Sin(HeadAngleRad);

	Lines.Emplace(Tip, Tip + (Direction * HeadCos + Perp * HeadSin) * HeadLength,
		Color, 0.0f, LineThickness, LineDepthPriority);
	Lines.Emplace(Tip, Tip + (Direction * HeadCos - Perp * HeadSin) * HeadLength,
		Color, 0.0f, LineThickness, LineDepthPriority);
}

FColor UCosmosTrajectoryComponent::BrightenTowardWhite(const FColor& In, float Amount)
{
	const float T = FMath::Clamp(Amount, 0.f, 1.f);

	return FColor(
		uint8(FMath::RoundToInt32(In.R + T * (255.f - In.R))),
		uint8(FMath::RoundToInt32(In.G + T * (255.f - In.G))),
		uint8(FMath::RoundToInt32(In.B + T * (255.f - In.B))),
		In.A);
}
