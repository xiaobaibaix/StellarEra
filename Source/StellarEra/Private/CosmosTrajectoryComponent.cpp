// Copyright Voxel Plugin SAS. All Rights Reserved.

#include "CosmosTrajectoryComponent.h"

#include "CosmosOrbitComponent.h"
#include "CosmosSimulationSubsystem.h"
#include "Engine/World.h"

namespace
{
	/**
	 * 线宽。**必须传 0**。
	 *
	 * 行批把 Thickness = 0 走成真正的 1 像素 GPU 线（BatchedElements.cpp:167），
	 * 而正值走的是「世界单位」的四边形（行批把 bScreenSpace 写死成 false）。
	 * 传正值的话近了变成粗带、远了直接消失；0 则任何距离都是 1 像素，正是想要的。
	 */
	constexpr float LineThickness = 0.0f;

	/** 深度优先级。SDPG_World = 0：走常规深度测试，星球能挡住它背后的轨道线。 */
	constexpr uint8 LineDepthPriority = SDPG_World;

	/** 箭头段长度 = 该系数 × 杆长。 */
	constexpr double ArrowHeadLengthFraction = 0.15;

	/** 箭头段与杆的夹角。150° 即「从箭尖往回折 30°」。 */
	constexpr double ArrowHeadAngleDegrees = 150.0;

	/** 预测采样点数。步长 = 窗口 / (该值 - 1)。 */
	constexpr int32 PredictionSamples = 256;

	/** 自动预测窗口 = 该系数 × 估算周期。略大于 1，让画出的椭圆首尾相接。 */
	constexpr double AutoHorizonPeriodFactor = 1.05;

	/** 估算不出周期时的预测窗口（年）。 */
	constexpr double FallbackPredictionYears = 1.0;

	/**
	 * 拖尾的标称采样点数，**也是保留点数的上界（至多 513）**。
	 *
	 * 采样门限 = TrailSpan / TrailSamples：距上次采样不足这个时长就不采，于是相邻两点
	 * 的间隔 ≥ 门限 → 窗口内的点数 ≤ TrailSamples（含端点至多 +1）。
	 *
	 * 点数随帧率变，但**覆盖时长 ≈ TrailSpan**（至多差一个采样间隔 —— 这正是按模拟时间裁剪要买的东西）：
	 * 帧率越低，每帧推进越大、点越稀（60 fps 约 119 点，30 fps 约 60 点）；帧率越高
	 * 点越密，每帧推进小于门限后趋近上界（本关卡约 1000 fps 时约 493 点，量化所致）。
	 * 想要固定点数就得改回按点数裁剪，那会让覆盖时长随帧率漂移 —— 不换。
	 */
	constexpr int32 TrailSamples = 512;

	/** 自动拖尾时长 = 该系数 × 预测窗口。论证见 spec §6。 */
	constexpr double AutoTrailHorizonFraction = 0.25;

	/** 星图里选中天体时，颜色朝白插值的比例。 */
	constexpr float HighlightWhiteLerp = 0.45f;
}

UCosmosTrajectoryComponent::UCosmosTrajectoryComponent()
{
	PrimaryComponentTick.bCanEverTick = true;

	// 排在位置写回之后（见类注释）。
	PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
}

void UCosmosTrajectoryComponent::Initialize(UCosmosOrbitComponent* InOrbit)
{
	Orbit = InOrbit;

	// 线批组件自己造、自己注册。**不挂到任何父组件下**：行批的线是用世界坐标
	// 直接交给 PDI 的（LineBatchComponent.cpp 里 DrawTranslucentLine(Line.Start,
	// Line.End, ...)），所以它自己的变换只影响包围盒、不影响线。挂在移动中的
	// 星球下面反而会让包围盒跟着跑、算错。引擎自己的行批也是这么造的。
	LineBatch = NewObject<ULineBatchComponent>(GetOwner());
	if (LineBatch)
	{
		LineBatch->RegisterComponent();
	}

	UE_LOG(LogTemp, Log, TEXT("[Cosmos] 轨迹组件已创建：%s"),
		*GetNameSafe(GetOwner()));
}

void UCosmosTrajectoryComponent::TickComponent(float DeltaTime, ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!LineBatch)
	{
		return;
	}

	// Initialize 先赋 Orbit 再建 LineBatch，所以 !Orbit 蕴含 !LineBatch。
	// 这条实际上已被上面那条覆盖，留着只为把「Orbit 可能为空」写明白。
	if (!Orbit)
	{
		ClearDrawnLines();
		return;
	}

	const UCosmosSimulationSubsystem* Subsystem =
		GetWorld() ? GetWorld()->GetSubsystem<UCosmosSimulationSubsystem>() : nullptr;
	if (!Subsystem)
	{
		// 与上面 !Orbit 分支对齐：线是 LifeTime = 0 画的、永不过期，
		// 早退不擦的话上一帧的轨迹会冻在画布上。
		ClearDrawnLines();
		return;
	}

	// 预测窗口拖尾和绘制都要用，先算。
	const double Horizon = ResolvePredictionHorizonYears(*Subsystem);

	// ── 拖尾：采样和裁剪**都**按模拟时间，都不按帧 ──
	// 只「按时间采样」是不够的。采样间隔可能比每帧推进还短（本关卡 1.28e-5 年
	// vs 60fps 下每帧推进 5.55e-5 年），那时每帧都会采，「点数上限」就等于
	// 「时长上限」，覆盖时长又变回由帧率决定 —— 正是要避免的事。
	// 所以裁剪也必须按时间：覆盖时长 ≈ TrailSpan，帧率只影响点的密度（缺口至多一个采样间隔）。
	const double SimulatedTime = Subsystem->GetSimulatedTimeYears();

	const double TrailSpan = Orbit->TrailYears > 0.0
		? Orbit->TrailYears
		: Horizon * AutoTrailHorizonFraction;
	const double TrailInterval = TrailSpan / double(TrailSamples);

	const AActor* OwnerActor = GetOwner();
	const FVector CurrentLocation = OwnerActor
		? OwnerActor->GetActorLocation()
		: FVector::ZeroVector;

	if (Trail.Num() == 0)
	{
		// 开局先放一个点，否则拖尾要等到第一个采样时刻才开始长，缺开头一小段。
		Trail.Add(CurrentLocation);
		TrailTimes.Add(SimulatedTime);
		LastTrailSampleTime = SimulatedTime;
	}
	else if (TrailInterval > 0.0 && SimulatedTime - LastTrailSampleTime >= TrailInterval)
	{
		// 一帧最多补一个点：掉帧时宁可让采样间隔临时变长，也不要在一帧里塞进
		// 几十个几乎重合的点。
		Trail.Add(CurrentLocation);
		TrailTimes.Add(SimulatedTime);
		LastTrailSampleTime = SimulatedTime;

		// 按**模拟时间**裁剪，不按点数。这是本步的关键：按点数裁的话，
		// 覆盖时长 = (TrailSamples-1) × max(TrailInterval, 每帧推进)，上面那种
		// 「间隔比每帧推进还小」的情形下就退化成由帧数决定，帧率一变拖尾长度就变。
		// 两个数组只在这里一起改，长度恒等。
		const double TrailStartTime = SimulatedTime - TrailSpan;
		int32 DropCount = 0;
		while (DropCount < TrailTimes.Num() && TrailTimes[DropCount] < TrailStartTime)
		{
			++DropCount;
		}
		if (DropCount > 0)
		{
			// EAllowShrinking::No = 保留容量，别每次裁剪都把分配器折腾一遍。
			Trail.RemoveAt(0, DropCount, EAllowShrinking::No);
			TrailTimes.RemoveAt(0, DropCount, EAllowShrinking::No);
		}
	}

	// ── 门控：只门控「画」 ──
	// 采样在上面**无条件**跑完了 —— 拖尾是真实历史。星图模式之外看不见它，
	// 但历史一直在攒，所以一进星图就是完整的，不是从零开始长。
	//
	// 三样东西各有一个逐天体开关，彼此独立、谁也不否决谁。星图可见性那一关
	// （星图模式 或 bShowOutsideStarMap）同时管住三样。
	const bool bInStarMapView = Subsystem->IsStarMapActive() || Orbit->bShowOutsideStarMap;
	const bool bWantTrail = bInStarMapView && Orbit->bDrawTrail;
	const bool bWantPrediction = bInStarMapView && Orbit->bDrawPrediction;
	const bool bWantArrow = bInStarMapView && Orbit->bDrawInitialVelocity;

	if (!bWantTrail && !bWantPrediction && !bWantArrow)
	{
		ClearDrawnLines();
		return;
	}

	// ── 预测：每帧重算 ──
	// 不做节流。理由有两条：拖尾本来就要每帧重画（见 Task 6），而预测一旦节流，
	// 画出的椭圆会比星球的真实位置落后「节流时长 / 一圈时长」那么多圈 ——
	// 凑近星球看时那条线明显接不上。每帧重算把滞后压到 1 帧。
	//
	// 滞后一律用**角度**说，不要用米：本关卡一圈约 7.5 秒，节流 0.2 秒 ≈ 10°，
	// 1 帧（60fps）≈ 0.8°。换关卡时按「时长 ÷ 一圈时长 × 360°」自己算。
	//
	// bDrawPrediction 为假时整段跳过 —— 这正是那个开关省下的大头（整系统复制 + 255 步）。
	PredictedWorld.Reset();
	if (bWantPrediction && Orbit->SimulatorIndex != INDEX_NONE)
	{
		Subsystem->PredictTrajectoryWorld(Orbit->SimulatorIndex, Horizon,
			PredictionSamples, PredictedWorld);
	}

	// ── 高亮：选中的天体两种颜色都向白提 ──
	// 只改颜色不改线宽 —— 理由见 BrightenTowardWhite 的声明处。
	const bool bSelected = (OwnerActor != nullptr)
		&& (Subsystem->GetSelectedBody() == OwnerActor);
	const FLinearColor TrailColor = bSelected
		? FLinearColor(BrightenTowardWhite(Orbit->TrailColor, HighlightWhiteLerp))
		: FLinearColor(Orbit->TrailColor);
	const FLinearColor PredictionColor = bSelected
		? FLinearColor(BrightenTowardWhite(Orbit->PredictionColor, HighlightWhiteLerp))
		: FLinearColor(Orbit->PredictionColor);

	Lines.Reset();

	if (bWantTrail && Trail.Num() >= 2)
	{
		AppendPolyline(Trail, TrailColor);
	}

	if (bWantPrediction && PredictedWorld.Num() >= 2)
	{
		AppendPolyline(PredictedWorld, PredictionColor);
	}

	if (bWantArrow)
	{
		AppendVelocityArrow(FLinearColor(Orbit->InitialVelocityColor));
	}

	// Flush 是必须的：线用 LifeTime = 0 画上去就永不过期，不清的话每帧的线会
	// 无限叠加（这是 spec §7.1 对着引擎源码确认过的行为）。
	LineBatch->Flush();
	if (Lines.Num() > 0)
	{
		LineBatch->DrawLines(Lines);
	}
	bDrewLastFrame = Lines.Num() > 0;
}

void UCosmosTrajectoryComponent::ClearDrawnLines()
{
	// 关掉时把已画的线清一次就够，不必每帧 MarkRenderStateDirty。
	if (bDrewLastFrame)
	{
		LineBatch->Flush();
		bDrewLastFrame = false;
	}
}

double UCosmosTrajectoryComponent::ResolvePredictionHorizonYears(
	const UCosmosSimulationSubsystem& Subsystem) const
{
	if (!Orbit)
	{
		return FallbackPredictionYears;
	}

	// 手填优先，与估算完全无关 —— 自动与手填的分支只在这一处判断。
	// 这一条**不进缓存**：它是人在详情面板里填的，随时可能改，改了要立刻生效。
	if (Orbit->PredictionYears > 0.0)
	{
		return Orbit->PredictionYears;
	}

	// 自动窗口只在第一次真的算出周期时解析一次，之后一直用缓存值。
	//
	// 每帧重算的话窗口会跟着当前间距跑：周期 ∝ 间距^1.5，天体一分开窗口就涨、
	// 画出来的线跟着变长；最近邻一换（下面 EstimateOrbitalPeriod 里 min 的那一项
	// 换人）还会跳一下。缓存下来之后窗口是个常量，线长稳定、可预期。
	//
	// 只在真的算出周期时才置位 bAutoHorizonResolved，所以「轨迹组件已经 Tick 了、
	// 但子系统还没 FinalizeRegistration、SimulatorIndex 仍是 INDEX_NONE」的那几帧
	// 不会把回退值钉死 —— 下一帧继续试。
	if (!bAutoHorizonResolved && Orbit->SimulatorIndex != INDEX_NONE)
	{
		const double Period = Subsystem.GetOrbitalPeriodYears(Orbit->SimulatorIndex);
		if (Period > 0.0 && FMath::IsFinite(Period))
		{
			CachedAutoHorizonYears = Period * AutoHorizonPeriodFactor;
			bAutoHorizonResolved = true;
		}
	}

	if (bAutoHorizonResolved)
	{
		// 上限**不缓存**，在返回时才施加 —— 这样它与手填那条一样，运行期改了
		// 立刻生效。上限的单位是「系统最短周期的倍数」，所以还得问子系统要那个
		// 周期；它按**开局**间距算一次就不变，与这里的缓存无关。
		//
		// 0 = 不设限；子系统还没算出最短周期（返回 0，比如关卡里只有一颗天体）
		// 时也无从钳起，原样返回。为什么要钳见 MaxAutoPredictionPeriods 的注释：
		// 预测是粗积分，误差按 (窗口/最短周期)³ 涨。
		const double ShortestPeriod = Subsystem.GetShortestPeriodYears();
		if (Orbit->MaxAutoPredictionPeriods > 0.0 && ShortestPeriod > 0.0)
		{
			return FMath::Min(CachedAutoHorizonYears,
				Orbit->MaxAutoPredictionPeriods * ShortestPeriod);
		}
		return CachedAutoHorizonYears;
	}

	// 求解器里只有一颗天体、或所有配对都算不出有限正周期 —— 每帧重试。
	return FallbackPredictionYears;
}

void UCosmosTrajectoryComponent::AppendPolyline(TArrayView<const FVector> Points,
	const FLinearColor& Color)
{
	// 第 0 个点不画线，它是后面每一段的起点 —— 所以从 1 开始。
	for (int32 Index = 1; Index < Points.Num(); ++Index)
	{
		Lines.Emplace(Points[Index - 1], Points[Index], Color,
			/*LifeTime=*/0.0f, LineThickness, LineDepthPriority);
	}
}

void UCosmosTrajectoryComponent::AppendVelocityArrow(const FLinearColor& Color)
{
	if (!Orbit)
	{
		return;
	}

	const double Speed = Orbit->InitialVelocity.Size();
	if (Speed <= 0.0 || Orbit->VelocityArrowScaleCm <= 0.0)
	{
		// 静止天体没有速度矢量可画。
		return;
	}

	// 方向就是 InitialVelocity 的方向：ToWorldVelocity 只是把它按一个**正常数**
	// 缩放（AU/年 → 厘米/秒），归一化之后完全相同，所以直接用游戏单位向量。
	//
	// 长度**不是**物理速度的世界长度 —— 按本项目世界尺度换算，7.4 AU/年 只有 0.24 厘米/秒，
	// 画出来看不见。长度是「速度大小 × VelocityArrowScaleCm」的示意图。
	const FVector Direction = Orbit->InitialVelocity.GetSafeNormal();
	if (Direction.IsNearlyZero())
	{
		return;
	}

	const double ShaftLength = Orbit->VelocityArrowScaleCm * Speed;
	const FVector Tip = Orbit->InitialWorldLocation + Direction * ShaftLength;

	// 杆。FBatchedLine 的构造参数序是 (Start, End, Color, LifeTime, Thickness,
	// DepthPriority) —— 与 DrawLine 的函数参数序相反，别写混。
	Lines.Emplace(Orbit->InitialWorldLocation, Tip, Color,
		/*LifeTime=*/0.0f, LineThickness, LineDepthPriority);

	// 箭头：从箭尖往回折两条短线。需要一个与杆垂直的参考方向；杆若几乎平行于
	// UpVector，就换 RightVector 做叉乘，避免叉乘退化。
	const FVector Reference = FMath::Abs(Direction.Z) < 0.9 ? FVector::UpVector : FVector::RightVector;
	const FVector Perp = FVector::CrossProduct(Direction, Reference).GetSafeNormal();
	if (Perp.IsNearlyZero())
	{
		return;   // 上面已经排除了退化，这里只是兜底
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
	// 全部按 float 算：FMath::RoundToInt32 的 float 重载和 double 重载结果一致，
	// 但混着写容易让人以为这里在做双精度插值。
	const float T = FMath::Clamp(Amount, 0.f, 1.f);

	return FColor(
		uint8(FMath::RoundToInt32(In.R + T * (255.f - In.R))),
		uint8(FMath::RoundToInt32(In.G + T * (255.f - In.G))),
		uint8(FMath::RoundToInt32(In.B + T * (255.f - In.B))),
		In.A);
}
