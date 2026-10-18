// Copyright Voxel Plugin SAS. All Rights Reserved.

#include "CosmosSimulationSubsystem.h"

#include "Camera/PlayerCameraManager.h"
#include "Components/SkyAtmosphereComponent.h"
#include "CosmosOrbitComponent.h"
#include "VoxelPlanetActor.h"
#include "SpaceshipPawn.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "PlanetAtmosphereParams.h"
#include "PlanetAtmosphereTargeting.h"
#include "Stats/Stats.h"

void UCosmosSimulationSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	// 引擎在 PostInitProperties 里已经按 UCLASS(Config = Game) 载入过一次。
	// 这里再显式载一次，是为了让「这些值从 ini 来」这件事在代码里看得见。
	LoadConfig();

	Simulator.SetUnitSystem(ECosmosUnitSystem::Game);
	Simulator.SetIntegrator(ECosmosIntegrator::VelocityVerlet);
	Simulator.SetSofteningLength(SofteningLength);
	Simulator.SetCentimetersPerAu(CentimetersPerAu);

	Super::Initialize(Collection);
}

void UCosmosSimulationSubsystem::Deinitialize()
{
	Bodies.Reset();
	Simulator.Clear();
	bInitialized = false;

	Super::Deinitialize();
}

bool UCosmosSimulationSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	// 只在 PIE 与打包世界里跑。
	//
	// 编辑器世界里体素星球走的是预览路径，天体没必要动、也不该动 ——
	// 动了会把关卡里手工摆好的位置改掉，每次打开地图都漂一点。
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId UCosmosSimulationSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UCosmosSimulationSubsystem, STATGROUP_Tickables);
}

void UCosmosSimulationSubsystem::RegisterBody(UCosmosOrbitComponent* Component)
{
	if (!Component)
	{
		return;
	}

	for (const TObjectPtr<UCosmosOrbitComponent>& Existing : Bodies)
	{
		if (Existing == Component)
		{
			return;
		}
	}

	Bodies.Add(Component);
}

void UCosmosSimulationSubsystem::UnregisterBody(UCosmosOrbitComponent* Component)
{
	if (!Component)
	{
		return;
	}

	for (int32 Index = Bodies.Num() - 1; Index >= 0; --Index)
	{
		if (Bodies[Index] == Component)
		{
			Bodies.RemoveAt(Index);
		}
	}

	// 只从列表里摘掉，求解器里的那个天体留着不动 —— 中途把它从数组里删掉会让
	// 后面所有索引整体前移，其余组件已存的 SimulatorIndex 全部失效。
	// 本项目的天体在整局游戏里不增不减，这个取舍是安全的。
	//
	// 但要把「每帧由组件写进去」的那两个字段清零：否则按住 W 销毁飞船时，那颗
	// 幽灵天体会一直带着最后一帧的外力与阻尼飞下去。物理上这点力可以忽略，
	// 真正的问题是 bHasForcedBodies 下一帧就变回 false —— 诊断里的 ⚠ 跟着消失，
	// 等于诊断在撒谎。
	if (Component->SimulatorIndex != INDEX_NONE
		&& Component->SimulatorIndex < Simulator.GetNumBodies())
	{
		FCosmosBodyState& Body = Simulator.GetBodyMutable(Component->SimulatorIndex);
		Body.ExternalAcceleration = FVector3d::ZeroVector;
		Body.LinearDamping = 0.0;
	}
}

void UCosmosSimulationSubsystem::AddBodyToSimulator(UCosmosOrbitComponent* Component)
{
	AActor* BodyActor = Component ? Component->GetOwner() : nullptr;
	if (!BodyActor)
	{
		return;
	}

	// ── 初态 = 关卡里的摆放 + 组件上的 InitialVelocity ──
	// 这一段只**读**，不写：位置取 Actor 当前所在，速度取 InitialVelocity，
	// 组件不搬运任何东西。关卡里怎么摆，运行后就从什么样开始跑。
	FCosmosBodyState State;
	State.Position = Simulator.ToGameUnitPosition(BodyActor->GetActorLocation());
	State.Velocity = FVector3d(Component->InitialVelocity);
	State.Mass = Component->MassSolar;
	State.OwnerActor = BodyActor;

	// 半径只对**有「地表」这个概念的天体**有意义 —— 就是下面这两个分支：星球
	// 与飞船。其余天体留 0 即可（点质量、永不接触）。携带判定走的是
	// CarryRiders 里的 Cast<AVoxelPlanetActor>，与这里写不写半径无关。
	if (const AVoxelPlanetActor* Planet = Cast<AVoxelPlanetActor>(BodyActor))
	{
		// 这里先给平均球半径当**兜底**：第一次 Tick 就会由
		// RefreshPlanetContactRadii 换成朝最近天体那个方向的地形半径。
		// 保留它是因为开局日志要打一行半径，那时还没刷过。
		State.Radius = double(Planet->GetSurfaceRadiusCm()) / CentimetersPerAu;
	}
	else if (const ASpaceshipPawn* Ship = Cast<ASpaceshipPawn>(BodyActor))
	{
		// 飞船的接触半径取碰撞球半径，让**机身外壳**贴地而不是**球心**贴地。
		// 留 0 的话求解器把中心钳在地表上，机身下半截埋进地形里 —— 300 厘米的
		// 球埋掉 3 米，正是 PIE 里看到的那一幕。
		State.Radius = double(Ship->GetContactRadiusCm()) / CentimetersPerAu;

		// 撞上行星时**整条速度归零**（相对速度归零、随行星一起动），而不是默认的
		// 「只抹法向、切向沿地表滑」—— 玩家期望撞地即停，不是擦着地表一路滑。
		State.bZeroVelocityOnContact = true;

		// 飞船是**测试粒子**：行星/恒星对它产生引力，但它不反过来吸引行星 ——
		// 玩家飞船的质量不该把行星从轨道上拽动。对行星完全无反作用。
		State.bTestParticle = true;
	}

	Component->SimulatorIndex = Simulator.AddBody(State);
}

void UCosmosSimulationSubsystem::RefreshPlanetContactRadii()
{
	const int32 NumBodies = Simulator.GetNumBodies();

	for (int32 Index = 0; Index < NumBodies; ++Index)
	{
		const AVoxelPlanetActor* Planet = Cast<AVoxelPlanetActor>(Simulator.GetBody(Index).OwnerActor.Get());
		if (!Planet)
		{
			continue;
		}

		// 只有最近的那个天体可能进到接触距离内，所以只需要朝它那个方向的地表高度。
		// 其它方向就算矮了 80 米也不会有人碰到，不必白算。
		int32 NearestIndex = INDEX_NONE;
		double NearestDistanceSq = 0.0;

		for (int32 Other = 0; Other < NumBodies; ++Other)
		{
			if (Other == Index)
			{
				continue;
			}

			const double DistanceSq =
				(Simulator.GetBody(Other).Position - Simulator.GetBody(Index).Position).SizeSquared();

			if (NearestIndex == INDEX_NONE || DistanceSq < NearestDistanceSq)
			{
				NearestIndex = Other;
				NearestDistanceSq = DistanceSq;
			}
		}

		if (NearestIndex == INDEX_NONE)
		{
			continue;
		}

		// 方向 = 星球中心 → 最近天体。AU 空间与世界空间只差一个等比缩放、没有旋转，
		// 所以方向可以原样交给地形查询（它内部会归一化）。
		const FVector Direction =
			FVector(Simulator.GetBody(NearestIndex).Position - Simulator.GetBody(Index).Position);

		const double SurfaceRadiusCm = double(Planet->GetSurfaceRadiusCmAtDirection(Direction));
		if (SurfaceRadiusCm > 0.0)
		{
			Simulator.GetBodyMutable(Index).Radius = SurfaceRadiusCm / CentimetersPerAu;
		}
	}
}

void UCosmosSimulationSubsystem::ApplyControlledBodyForces(float DeltaSeconds)
{
	bHasForcedBodies = false;

	// 1 AU/年² 等于多少厘米/秒²。与速度那条是同一个换算族：
	// 1 AU/年² = CentimetersPerAu × TimeScale²，默认 1e6 × 0.00333² = 11.089。
	//
	// 这道守卫挡的是 ini 里把 TimeScale 改成 0：那时 AdvanceAccumulator 每帧只加 0，
	// 求解器根本不推进，所以留下上一帧的力也无所谓；但不守卫的话除零会得到 inf，
	// 而 inf 一旦进了天体状态，GetEnergyDriftRatio 会变成 nan 把日志打花。
	const double CmPerS2PerAuPerY2 = CentimetersPerAu * TimeScale * TimeScale;
	if (CmPerS2PerAuPerY2 <= 0.0)
	{
		return;
	}

	for (UCosmosOrbitComponent* Component : Bodies)
	{
		if (!Component || Component->SimulatorIndex == INDEX_NONE)
		{
			continue;
		}

		const ASpaceshipPawn* Ship = Cast<ASpaceshipPawn>(Component->GetOwner());
		if (!Ship)
		{
			continue;
		}

		// 求解器的 GetBodyMutable 是裸的 Bodies[Index]，越界不报错、直接是 UB ——
		// 校验只能在这一层做，与 GetBodyVelocityGameUnits 同一个理由。
		if (Component->SimulatorIndex >= Simulator.GetNumBodies())
		{
			continue;
		}

		FCosmosBodyState& Body = Simulator.GetBodyMutable(Component->SimulatorIndex);

		// 帧时长原样转给飞船：它的制动要按「本帧满推力能改变多少速度」收敛，
		// 见 ASpaceshipPawn::GetThrustAccelerationWorld。
		Body.ExternalAcceleration =
			FVector3d(Ship->GetThrustAccelerationWorld(DeltaSeconds)) / CmPerS2PerAuPerY2;

		// τ 是真实秒，求解器的时间单位是模拟年，所以除一个 TimeScale。
		//
		// ⚠ 阻尼作用在**惯性系**速度上，不是相对携带天体的速度 —— 求解器不知道
		// 谁携带谁（那是 CarryRiders 的事，而且是单向的）。行星自己在跑时，阻尼
		// 抽走的是 k·v_行星 那个量级；后果见 spec §7。
		const float DampingTime = Ship->GetDampingTime();
		Body.LinearDamping = DampingTime > 0.f
			? 1.0 / (double(DampingTime) * TimeScale)
			: 0.0;

		bHasForcedBodies |= !Body.ExternalAcceleration.IsNearlyZero()
			|| Body.LinearDamping != 0.0;
	}
}

void UCosmosSimulationSubsystem::FinalizeRegistration()
{
	bInitialized = true;

	if (Bodies.Num() == 0)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[Cosmos] 关卡里没有任何 UCosmosOrbitComponent，N 体系统不启动。"));
		return;
	}

	// 按质量降序注册：中心天体先进求解器。
	// N 体是同时求解的，顺序不影响结果，这样只是让日志更好读。
	Bodies.Sort([](const UCosmosOrbitComponent& A, const UCosmosOrbitComponent& B)
	{
		return A.MassSolar > B.MassSolar;
	});

	// 注册完就完事：位置取自关卡摆放、速度取自 InitialVelocity，**不需要（也不该）
	// 在开局写回一次位置**。
	//
	// 早先这里有一次「立即同步」，那是为了掩盖轨道根数算出的位置与摆放位置对不上
	// 造成的跳变。轨道根数已经删了，那次同步就只剩副作用：厘米 → AU → 厘米的往返
	// 会把摆放位置按 float 精度挪动一点 —— 正是要消灭的「组件改变初始状态」。
	//
	// 删掉它之后，第一次 Tick 的写回仍会做同一趟往返（求解器只认 AU），但那是
	// 亚厘米级的取整，且方向是「Actor 向求解器对齐」而不是反过来。
	for (UCosmosOrbitComponent* Component : Bodies)
	{
		AddBodyToSimulator(Component);
	}

	// ── 自动步长 ──
	// 轨道周期不再来自手填的半长轴，而是由**实际几何**反推：对每一对天体 (i, j)，
	// 把它们的间距 r 当成圆轨道半径，算出这对天体相互绕转的周期
	//     T = 2π√(r³ / (G·(m_i + m_j)))
	// 再取其中最短的一条除以 StepsPerOrbit。
	//
	// 为什么取最短的那条：它就是这个系统里最快的动力学时标，积分步长必须跟着它走。
	// 具体数值逐关卡不同（改质量、改初始位置都会变）—— 要看就去看开局日志那行，
	// 「最短周期」与「步长」两个字段都在上面。
	double ShortestPeriod = TNumericLimits<double>::Max();

	for (int32 I = 0; I < Simulator.GetNumBodies(); ++I)
	{
		for (int32 J = I + 1; J < Simulator.GetNumBodies(); ++J)
		{
			// 与求解器里 EstimateOrbitalPeriod 用的是同一个函数 —— 两处曾经各写
			// 一遍同样的公式，抽出来之后就只有一个定义。
			const double PairPeriod = FNBodySimulator::PairOrbitalPeriod(
				Simulator.GetBody(I), Simulator.GetBody(J),
				CosmosUnits::GravitationalConstant_Game);

			if (PairPeriod > 0.0)
			{
				ShortestPeriod = FMath::Min(ShortestPeriod, PairPeriod);
			}
		}
	}

	// 与 BaseTimeStep 同源，但**分开判**：下面那一支还额外要求 StepsPerOrbit > 0，
	// 而最短周期本身是个独立的量 —— StepsPerOrbit 填 0 时步长退回默认值，最短周期
	// 照样有效，预测窗口的上限还要用它。没算出任何正周期时它保持 0。
	if (FMath::IsFinite(ShortestPeriod) && ShortestPeriod > 0.0)
	{
		ShortestPeriodYears = ShortestPeriod;
	}

	if (FMath::IsFinite(ShortestPeriod) && ShortestPeriod > 0.0 && StepsPerOrbit > 0)
	{
		BaseTimeStep = FMath::Clamp(ShortestPeriod / double(StepsPerOrbit), 1e-12, 1e-2);
	}

	Simulator.MarkEnergyBaseline();

	UE_LOG(LogTemp, Log,
		TEXT("[Cosmos] 初始化完成：%d 个天体，最短周期 %.6f 年，步长 %.6e 年，TimeScale %.5f 年/秒，1 AU = %.0f 厘米"),
		Simulator.GetNumBodies(), ShortestPeriod, BaseTimeStep, TimeScale, CentimetersPerAu);

	// ── 每个天体一行初态，只在开局打这一次 ──
	//
	// 有这三样就够手算了：质量给 M，位置给 r，速度给 v —— 由它们能算出逃逸速度
	// √(2M/r) 与束缚能 v²/2 − M/r。「轨道为什么不闭合」「预测窗口为什么这么长」
	// 这两个问题都从这里开始查，不必再跑一遍模拟。
	//
	// 最近邻单独列出来，因为**它才是预测窗口的来源**：窗口 = 1.05 × 该天体的
	// 最短周期，而最短周期 = min over j 的 PairOrbitalPeriod，即与最近邻的那一对。
	// 上面那行「最短周期」是全系统的最小值，不一定属于这个天体。
	//
	// 每帧打会淹掉日志，而初态本来就不变，所以只在这里打一次。
	for (int32 Index = 0; Index < Simulator.GetNumBodies(); ++Index)
	{
		const FCosmosBodyState& Body = Simulator.GetBody(Index);

		int32 NearestIndex = INDEX_NONE;
		double NearestDistance = 0.0;
		for (int32 Other = 0; Other < Simulator.GetNumBodies(); ++Other)
		{
			if (Other == Index)
			{
				continue;
			}

			const double Distance = (Simulator.GetBody(Other).Position - Body.Position).Size();
			if (NearestIndex == INDEX_NONE || Distance < NearestDistance)
			{
				NearestIndex = Other;
				NearestDistance = Distance;
			}
		}

		UE_LOG(LogTemp, Log,
			TEXT("[Cosmos]   天体 %d：质量 %.6g M☉，半径 %.4f AU，位置 (%.4f, %.4f, %.4f) AU，速度 (%.4f, %.4f, %.4f) AU/年，最近邻 %d（%.4f AU），最短周期 %.6f 年"),
			Index, Body.Mass, Body.Radius,
			Body.Position.X, Body.Position.Y, Body.Position.Z,
			Body.Velocity.X, Body.Velocity.Y, Body.Velocity.Z,
			NearestIndex, NearestDistance,
			Simulator.EstimateOrbitalPeriod(Index));
	}
}

void UCosmosSimulationSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (!bInitialized)
	{
		FinalizeRegistration();
	}

	if (Simulator.GetNumBodies() == 0)
	{
		return;
	}

	// ── 0. 刷新行星的接触半径 + 施加受控天体的外力 ──
	// 两者都是「推进之前把引擎侧状态喂给求解器」。
	//
	// 接触半径必须在推进之前：接触解算发生在 StepExact 内部，读的就是这一刻的
	// Radius。放到推进之后，这一帧的接触用的还是上一帧的地形高度。
	//
	// 外力同理：本帧推进读的就是这一刻写进去的值。
	RefreshPlanetContactRadii();
	UpdateSkyAtmosphereRetarget();
	ApplyControlledBodyForces(DeltaTime);

	// ── 1. 时间推进 ──
	// 真实秒 × TimeScale = 需要模拟的天文年数。
	//
	// 余量必须跨帧累积。步长固定是辛积分器的前提（变步长会破坏辛性质、让能量
	// 漂移失去上界），而每帧要推进的年数不是步长的整数倍 —— 本关卡 60fps 下
	// 每帧需要 1.81 步。直接 floor 丢掉余量的话每帧只跑 1 步，实际速度只有
	// 名义值的 55%，而且随帧率漂移：名义「公转一圈 300 秒」会变成 540 秒，
	// 帧率一掉还要更慢。
	//
	// 单帧的推进量另设上限：加载或卡顿之后不该让模拟去补一大段（那既没有
	// 意义，又会让 TimeScale 因为连续超限而永久掉下去）。
	const double MaxAdvancePerFrame =
		double(FMath::Max(MaxStepsPerFrame - 1, 1)) * BaseTimeStep;
	AdvanceAccumulator += FMath::Min(double(DeltaTime) * TimeScale, MaxAdvancePerFrame);

	const int32 NumSteps = FMath::FloorToInt(AdvanceAccumulator / BaseTimeStep);
	if (NumSteps > 0)
	{
		const int32 StepsToRun = FMath::Min(NumSteps, MaxStepsPerFrame);

		// 只扣掉真正跑了的那些步，剩下的留到下一帧。
		AdvanceAccumulator -= double(StepsToRun) * BaseTimeStep;

		for (int32 StepIndex = 0; StepIndex < StepsToRun; ++StepIndex)
		{
			Simulator.StepExact(BaseTimeStep);
		}

		// 兜底，正常配置下不会触发：AdvanceAccumulator 每帧最多只加
		// (MaxStepsPerFrame − 1) 步的量，而它每次又只扣掉整数步，余量恒小于一个
		// 步长，所以 floor 之后最多就是 MaxStepsPerFrame − 1 步。真正约束单帧
		// 开销的是上面那道推进上限，不是这里。
		//
		// 只有 MaxStepsPerFrame 被直接改 ini 成 0 或负数时才会持续超限 ——
		// 属性上的 ClampMin = 1 挡的是编辑器那条路，这段留着兜 ini 那条路：
		// 宁可让 TimeScale 掉下去，也不要让帧率崩掉。
		if (NumSteps > MaxStepsPerFrame)
		{
			TimeScale *= 0.9;
			UE_LOG(LogTemp, Warning,
				TEXT("[Cosmos] 单帧积分步数超限（%d > %d），TimeScale 自动降至 %.5f"),
				NumSteps, MaxStepsPerFrame, TimeScale);
		}
	}

	// ── 2. 写回各个天体 ──
	for (UCosmosOrbitComponent* Component : Bodies)
	{
		AActor* BodyActor = Component ? Component->GetOwner() : nullptr;
		if (!BodyActor || Component->SimulatorIndex == INDEX_NONE)
		{
			continue;
		}

		const FVector PreviousLocation = BodyActor->GetActorLocation();

		if (Component->bSimulated)
		{
			const FCosmosBodyState& State = Simulator.GetBody(Component->SimulatorIndex);
			BodyActor->SetActorLocation(Simulator.ToWorldLocation(State.Position), /*bSweep=*/false);

			// 受控天体（飞船）：把求解器速度一并写回 Actor。
			// ASpaceshipPawn::GetVelocity() 的覆写读的就是它 —— 体素调用器的预测、
			// IsLanded() 的阈值判断、HUD 的速度显示都走那一处收口。
			if (ASpaceshipPawn* Ship = Cast<ASpaceshipPawn>(BodyActor))
			{
				Ship->SetCosmosVelocity(ToWorldVelocityScaled(State.Velocity));
			}
		}

		// 位移取 Actor 的**实际**变化量，而不是求解器算出的差值：
		// bSimulated 为假的天体在求解器里会漂、在 Actor 上不动，用实际变化量
		// 两边都自洽（那种天体的位移恒为零）。
		const FVector Delta = BodyActor->GetActorLocation() - PreviousLocation;
		if (Delta.IsNearlyZero())
		{
			continue;
		}

		// 体素世界就是星球 Actor 自己（AVoxelPlanetActor 直接继承 AVoxelWorld），
		// 移动 Actor 即移动世界 —— 没有第二个变换要同步。

		// 顺序要紧：先把星球自己搬到新位置，再把人按同样的位移带过去。
		CarryRiders(BodyActor, Delta);
	}

	// ── 3. 诊断 ──
	if (DiagnosticInterval > 0.f)
	{
		DiagnosticTimer += DeltaTime;
		if (DiagnosticTimer >= DiagnosticInterval)
		{
			DiagnosticTimer = 0.f;
			LogDiagnostics();
		}
	}
}

void UCosmosSimulationSubsystem::LogDiagnostics() const
{
	// 整块都带 `[Cosmos]` 前缀 —— probe 只打印带前缀的行（见函数上方说明）。
	UE_LOG(LogTemp, Log, TEXT("[Cosmos] ========== 诊断 =========="));
	UE_LOG(LogTemp, Log, TEXT("[Cosmos] 天体数量  : %d"), Simulator.GetNumBodies());
	UE_LOG(LogTemp, Log, TEXT("[Cosmos] 步长      : %.6e 年"), BaseTimeStep);
	UE_LOG(LogTemp, Log, TEXT("[Cosmos] TimeScale : %.5f 年/秒（公转一圈约 %.0f 秒）"),
		TimeScale, TimeScale > 0.0 ? 1.0 / TimeScale : 0.0);
	UE_LOG(LogTemp, Log, TEXT("[Cosmos] 已模拟    : %.6f 年"), Simulator.GetSimulatedTime());

	// 有受控天体时，总能量与总动量这两项都不再守恒，这个漂移值于是不再是积分
	// 误差：推力做功把它推正，阻尼与接触耗散把它拉负（接触抹的是相对法向速度，
	// 只吃能量、不动总动量）。加一句提醒，免得看日志的人去查积分器。
	UE_LOG(LogTemp, Log, TEXT("[Cosmos] 能量漂移  : %.6e  （辛积分应 < 1e-5）%s"),
		Simulator.GetEnergyDriftRatio(),
		bHasForcedBodies ? TEXT("  ⚠ 含受控天体，此值不守恒") : TEXT(""));
	UE_LOG(LogTemp, Log, TEXT("[Cosmos] 总动量    : %.6e"), Simulator.GetTotalMomentum().Size());
	UE_LOG(LogTemp, Log, TEXT("[Cosmos] 角动量    : %.6e"), Simulator.GetTotalAngularMomentum().Size());
	UE_LOG(LogTemp, Log, TEXT("[Cosmos] 1 AU      : %.0f 厘米"), CentimetersPerAu);

	// 受控天体（飞船）单独一行，值非零才打 —— 它的存在本身就是
	// 「子系统真的下来读到飞船了」的证据：没有输入时外力为 0，但阻尼是
	// 1/(DampingTime × TimeScale) = 3.0030e+04 /年，非零。
	for (int32 Index = 0; Index < Simulator.GetNumBodies(); ++Index)
	{
		const FCosmosBodyState& Body = Simulator.GetBody(Index);
		if (Body.ExternalAcceleration.IsNearlyZero() && Body.LinearDamping == 0.0)
		{
			continue;
		}

		UE_LOG(LogTemp, Log, TEXT("[Cosmos] 受控天体 %d：外力 %.4e AU/年²，阻尼 %.4e /年"),
			Index, Body.ExternalAcceleration.Size(), Body.LinearDamping);
	}
	UE_LOG(LogTemp, Log, TEXT("[Cosmos] =========================="));
}

bool UCosmosSimulationSubsystem::PredictTrajectoryWorld(int32 SimulatorIndex, double HorizonYears,
                                                        int32 NumSamples, TArray<FVector>& OutWorldPositions) const
{
	OutWorldPositions.Reset();

	// 输出走调用方给的数组；AU → 厘米 的中间结果用一个局部数组接一下。
	// 每帧每个天体多两次分配，N ≤ 10 时无关紧要。
	TArray<FVector3d> GameUnitPositions;
	if (!Simulator.PredictTrajectory(SimulatorIndex, HorizonYears, NumSamples, GameUnitPositions))
	{
		return false;
	}

	OutWorldPositions.Reserve(GameUnitPositions.Num());
	for (const FVector3d& Position : GameUnitPositions)
	{
		OutWorldPositions.Add(Simulator.ToWorldLocation(Position));
	}
	return true;
}

double UCosmosSimulationSubsystem::GetSimulatedTimeYears() const
{
	return Simulator.GetSimulatedTime();
}

double UCosmosSimulationSubsystem::GetOrbitalPeriodYears(int32 SimulatorIndex) const
{
	return Simulator.EstimateOrbitalPeriod(SimulatorIndex);
}

FVector3d UCosmosSimulationSubsystem::GetBodyVelocityGameUnits(int32 SimulatorIndex) const
{
	// 求解器的 GetBody 是裸的 Bodies[Index]，越界不会断言、直接是 UB ——
	// 所以校验只能在这一层做，不能省。
	if (SimulatorIndex < 0 || SimulatorIndex >= Simulator.GetNumBodies())
	{
		return FVector3d::ZeroVector;
	}

	return Simulator.GetBody(SimulatorIndex).Velocity;
}

FVector3d UCosmosSimulationSubsystem::GetBodyGravityGameUnits(int32 SimulatorIndex) const
{
	// 与 GetBodyVelocityGameUnits 同一个校验理由：GetBody 越界是 UB。
	if (SimulatorIndex < 0 || SimulatorIndex >= Simulator.GetNumBodies())
	{
		return FVector3d::ZeroVector;
	}

	return Simulator.GetBody(SimulatorIndex).GravitationalAcceleration;
}

FVector UCosmosSimulationSubsystem::ToWorldVelocityScaled(const FVector3d& GameUnitVelocity) const
{
	return FVector(GameUnitVelocity) * (CentimetersPerAu * TimeScale);
}

void UCosmosSimulationSubsystem::SetSelectedBody(AActor* Body)
{
	SelectedBody = Body;
}

AActor* UCosmosSimulationSubsystem::GetSelectedBody() const
{
	return SelectedBody.Get();
}

void UCosmosSimulationSubsystem::GetRegisteredBodies(TArray<AActor*>& Out) const
{
	Out.Reset();

	for (const TObjectPtr<UCosmosOrbitComponent>& Component : Bodies)
	{
		// 组件可能在关卡卸载的间隙里失效；Outer 不是 Actor 的组件也拿不到 Actor。
		if (!Component)
		{
			continue;
		}

		if (AActor* OwnerActor = Component->GetOwner())
		{
			Out.Add(OwnerActor);
		}
	}
}

void UCosmosSimulationSubsystem::CarryRiders(AActor* BodyActor, const FVector& Delta)
{
	// 只有 AVoxelPlanetActor 携带人；不是它的天体没有「地表」这个概念，
	// 站在上面的东西靠自身重力待着就行。
	const AVoxelPlanetActor* Planet = Cast<AVoxelPlanetActor>(BodyActor);
	if (!Planet)
	{
		return;
	}

	UWorld* OwningWorld = GetWorld();
	if (!OwningWorld)
	{
		return;
	}

	const double RideRadius = double(Planet->GetSurfaceRadiusCm()) * RideRadiusScale;
	const double RideRadiusSq = RideRadius * RideRadius;
	const FVector PlanetLocation = Planet->GetActorLocation();

	for (TActorIterator<APawn> It(OwningWorld); It; ++It)
	{
		APawn* Rider = *It;
		if (!Rider)
		{
			continue;
		}

		// 已经挂在别的 Actor 上的 Pawn 一律跳过，两个理由：
		//   1. 挂在**这颗星球**上的：星球移动时附着关系本来就把它带走了，
		//      再加一次位移就是走两步；
		//   2. 挂在**别的 Pawn** 上的（登船后的角色挂在飞船下）：那个 Pawn
		//      如果也被本函数推着走，附着关系已经带走了它，同样会走两步。
		// TActorIterator 的遍历顺序不保证，靠顺序去重是不可靠的，所以按附着
		// 关系去重。（本项目当前只有一处 AttachToActor：登船时角色挂到飞船下。）
		if (Rider->GetAttachParentActor())
		{
			continue;
		}

		// 自身就是 N 体天体的 Pawn 也跳过：本函数上方已经用 SetActorLocation 把它
		// 搬到新位置了，这里再按星球位移叠一次就是走两步。飞船属于这一类
		// —— 它挂 UCosmosOrbitComponent，位置由求解器决定，不是被星球「带着」的乘客。
		//
		// 判据用 SimulatorIndex 而不是「有没有挂组件」：组件可能存在但还没注册进
		// 求解器（FinalizeRegistration 之前），那种 Pawn 求解器不管，仍该按乘客
		// 处理，否则它会掉在原地不动。
		if (const UCosmosOrbitComponent* RiderOrbit =
			Rider->FindComponentByClass<UCosmosOrbitComponent>())
		{
			if (RiderOrbit->SimulatorIndex != INDEX_NONE)
			{
				continue;
			}
		}

		const FVector RiderLocation = Rider->GetActorLocation();

		if (FVector::DistSquared(RiderLocation, PlanetLocation) > RideRadiusSq)
		{
			continue;
		}

		// 最近者独占。判定球理论上可能重叠：RideRadiusScale 是子系统级的，同关卡里
		// 地表半径相同的星球判定半径就相同，两颗靠近到「判定半径之和」以内时，重叠
		// 区里的 Pawn 会被两颗星各推一次，位移叠加成两倍。
		//
		// 本关卡三颗行星地表半径都是 0.075 AU 量级、彼此相隔 10 AU，判定球远够不着
		// 对方，碰不到重叠；这条规则是为将来星球挨得近时准备的。
		//
		// 这条规则依赖「所有体素星球的判定半径相同」（RideRadiusScale 是子系统级
		// 配置）。将来若加入半径更小的星球，「更近」就不再蕴含「够得着」：站在大星
		// 判定球内、却离小星更近的 Pawn 会被两颗星同时放弃。届时要把比较条件改成
		// 「更近**且**够得着」。
		//
		// 所以只由「离 Pawn 最近的携带者」推它，其余天体的调用跳过。不是
		// AVoxelPlanetActor 的天体不是携带者，它的远近与本规则无关，不能参与
		// 比较、更不能否决行星。
		//
		// 距离相等时按 (距离平方, UniqueID) 取最小，保证同帧恰好一颗星携带：
		// 只比距离的话，正好落在两个等半径判定球中点的 Pawn 会被推两次。
		const double SelfDistanceSq = FVector::DistSquared(RiderLocation, PlanetLocation);
		const uint32 SelfUniqueId = BodyActor->GetUniqueID();
		bool bIsNearestCarrier = true;

		for (UCosmosOrbitComponent* OtherComponent : Bodies)
		{
			AActor* OtherBody = OtherComponent ? OtherComponent->GetOwner() : nullptr;
			if (!OtherBody || OtherBody == BodyActor)
			{
				continue;
			}

			// 不是 AVoxelPlanetActor 的天体不是携带者，不参与比较。
			if (!Cast<AVoxelPlanetActor>(OtherBody))
			{
				continue;
			}

			const double OtherDistanceSq =
				FVector::DistSquared(RiderLocation, OtherBody->GetActorLocation());
			if (OtherDistanceSq < SelfDistanceSq
				|| (OtherDistanceSq == SelfDistanceSq && OtherBody->GetUniqueID() < SelfUniqueId))
			{
				bIsNearestCarrier = false;
				break;
			}
		}

		if (!bIsNearestCarrier)
		{
			continue;
		}

		// 不用 Sweep：Pawn 本来就在地面上，硬扫会把它从地形里挤出去。
		// 这一帧星球平移多少，Pawn 就跟着平移多少，相对关系原样保留。
		Rider->AddActorWorldOffset(Delta, /*bSweep=*/false);
	}
}

void UCosmosSimulationSubsystem::UpdateSkyAtmosphereRetarget()
{
	if (!bSkyAtmosphereRetargetEnabled)
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// 相机位置。星图模式下 Possess 的是 AStarMapCameraPawn，一样能取到。
	const APlayerController* PC = World->GetFirstPlayerController();
	const APlayerCameraManager* CamMgr = PC ? PC->PlayerCameraManager : nullptr;
	if (!CamMgr)
	{
		return;
	}
	const FVector CameraPos = CamMgr->GetCameraLocation();

	// 收集所有行星
	TArray<FVector> Centers;
	TArray<float> SurfaceRadiiCm;
	TArray<AVoxelPlanetActor*> Planets;
	for (TActorIterator<AVoxelPlanetActor> It(World); It; ++It)
	{
		AVoxelPlanetActor* Planet = *It;
		const float RadiusCm = Planet->GetSurfaceRadiusCm();
		if (RadiusCm <= 0.0f)
		{
			continue;
		}
		Planets.Add(Planet);
		Centers.Add(Planet->GetActorLocation());
		SurfaceRadiiCm.Add(RadiusCm);
	}

	const int32 NearestIndex =
		PlanetAtmosphereTargeting::FindNearestPlanet(Centers, SurfaceRadiiCm, CameraPos);
	if (NearestIndex == INDEX_NONE)
	{
		// 一颗行星都没有：保持上一次的设置。
		// 绝对不能把 AtmosphereHeight 写成 0 —— 着色器里的 exp(-h / H) 会除零。
		return;
	}

	AVoxelPlanetActor* NearestPlanet = Planets[NearestIndex];

	// 只有目标真的换了才写，避免每帧抖动着改渲染状态。
	if (LastAtmosphereTargetPlanet.Get() == NearestPlanet)
	{
		return;
	}

	ASkyAtmosphere* SkyAtmosphere = nullptr;
	for (TActorIterator<ASkyAtmosphere> It(World); It; ++It)
	{
		SkyAtmosphere = *It;
		break;
	}
	if (!SkyAtmosphere)
	{
		if (!bLoggedMissingSkyAtmosphere)
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[大气] 关卡里没有 ASkyAtmosphere，行星表面不会有天空。"));
			bLoggedMissingSkyAtmosphere = true;
		}

		// **这里不能落 LastAtmosphereTargetPlanet**：落下去就等于宣告"这颗行星已经
		// 处理过了"，于是上面那个早退在之后每一帧都生效，重定位再也不会发生 —— 天空
		// 大气稍后被加进关卡（或流式加载进来）也救不回来，只能重启 PIE。
		//
		// 代价是天空大气缺失期间每帧都走一次 TActorIterator（本关卡 9 个 actor）——
		// 这正是要的：它让子系统能发现天空大气"出现了"。所以别为了省这次遍历而把
		// 标记挪回来，也别加"放弃重试"的一次性标志，那会把同一个 bug 重新引入。
		//
		// 警告本身仍然只打一次（bLoggedMissingSkyAtmosphere），不会变成每帧一行。
		return;
	}

	USkyAtmosphereComponent* Atmosphere = SkyAtmosphere->GetComponent();
	if (!Atmosphere)
	{
		return;
	}

	// 查找成功了才落标记。上面任何一步返回都还没写进 LastAtmosphereTargetPlanet，
	// 下一帧会重新尝试 —— 一次瞬时失败不会永久关掉这个功能。
	LastAtmosphereTargetPlanet = NearestPlanet;

	const float SurfaceRadiusKm = NearestPlanet->GetSurfaceRadiusCm() / 100000.0f; // cm → km
	const PlanetAtmosphere::FSkyAtmosphereScaleParams ScaleParams =
		PlanetAtmosphere::ComputeSkyAtmosphereScaleParams(SurfaceRadiusKm, AtmosphereHeightRatio);
	if (ScaleParams.Scale <= 0.0f)
	{
		return;
	}

	SkyAtmosphere->SetActorLocation(NearestPlanet->GetActorLocation());

	// TransformMode 没有 setter —— 引擎只为 USKYATMOSPHERE_VARIABLES_WITH_SETFUNCTION
	// 那一批生成了 Set##Name（SkyAtmosphereComponent.h:188-232），TransformMode 不在其中，
	// 只能直接赋成员。
	Atmosphere->TransformMode = ESkyAtmosphereTransformMode::PlanetCenterAtComponentTransform;

	// 其余 7 个**必须走 setter**，不能直接赋成员。
	// 引擎的 setter 宏（SkyAtmosphereComponent.cpp:462-471）在赋值后调 MarkRenderStateDirty()，
	// 直接赋成员不会 —— 场景代理是在 CreateSceneProxy 里把参数拷进 FAtmosphereSetup 的，
	// 不置脏就永远用旧值。后果是静默失效：不报错、不崩、只是什么都没变。
	// （SkyAtmosphereGetClamped 对 float 是直通，setter 不会偷偷夹我们的值。）
	Atmosphere->SetBottomRadius(ScaleParams.BottomRadiusKm);
	Atmosphere->SetAtmosphereHeight(ScaleParams.AtmosphereHeightKm);
	Atmosphere->SetRayleighScatteringScale(ScaleParams.RayleighScatteringScale * AtmosphereCoefficientScale);
	Atmosphere->SetMieScatteringScale(ScaleParams.MieScatteringScale * AtmosphereCoefficientScale);
	Atmosphere->SetMieAbsorptionScale(ScaleParams.MieAbsorptionScale * AtmosphereCoefficientScale);
	Atmosphere->SetRayleighExponentialDistribution(ScaleParams.RayleighExponentialDistributionKm);
	Atmosphere->SetMieExponentialDistribution(ScaleParams.MieExponentialDistributionKm);

	// TransformMode 那行没有 setter 兜底，所以显式置一次脏。
	// 上面 7 个 setter 里只要有一个真的改了值就已经置过脏了，这一行是换行星时
	// 半径恰好相同、只有 TransformMode 变了那种情况下的保险。
	Atmosphere->MarkRenderStateDirty();

	UE_LOG(LogTemp, Display,
		TEXT("[大气] 引擎大气重定位到 %s：R=%.4f km，厚度=%.5f km，瑞利系数=%.2f，米氏系数=%.4f"),
		*NearestPlanet->GetName(), ScaleParams.BottomRadiusKm, ScaleParams.AtmosphereHeightKm,
		Atmosphere->RayleighScatteringScale, Atmosphere->MieScatteringScale);
}
