// Copyright Voxel Plugin SAS. All Rights Reserved.

#include "CosmosSimulationSubsystem.h"

#include "CosmosOrbitComponent.h"
#include "VoxelPlanetActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
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

	// 半径只对体素星球有意义（只有它才有「地表」这个概念）。恒星之类的天体
	// 不携带任何人，留 0 即可 —— 携带判定走的是 Task 5 里单独的一条路径。
	if (const AVoxelPlanetActor* Planet = Cast<AVoxelPlanetActor>(BodyActor))
	{
		State.Radius = double(Planet->GetSurfaceRadiusCm()) / CentimetersPerAu;
	}

	Component->SimulatorIndex = Simulator.AddBody(State);
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
	// 本关卡实测这条是 0.790569 年（开局日志那行的「最短周期」），步长 3.952847e-05 年
	// 正好是它除以 StepsPerOrbit = 20000。
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
	UE_LOG(LogTemp, Log, TEXT("========== Cosmos 诊断 =========="));
	UE_LOG(LogTemp, Log, TEXT("天体数量  : %d"), Simulator.GetNumBodies());
	UE_LOG(LogTemp, Log, TEXT("步长      : %.6e 年"), BaseTimeStep);
	UE_LOG(LogTemp, Log, TEXT("TimeScale : %.5f 年/秒（公转一圈约 %.0f 秒）"),
		TimeScale, TimeScale > 0.0 ? 1.0 / TimeScale : 0.0);
	UE_LOG(LogTemp, Log, TEXT("已模拟    : %.6f 年"), Simulator.GetSimulatedTime());
	UE_LOG(LogTemp, Log, TEXT("能量漂移  : %.6e  （辛积分应 < 1e-5）"), Simulator.GetEnergyDriftRatio());
	UE_LOG(LogTemp, Log, TEXT("总动量    : %.6e"), Simulator.GetTotalMomentum().Size());
	UE_LOG(LogTemp, Log, TEXT("角动量    : %.6e"), Simulator.GetTotalAngularMomentum().Size());
	UE_LOG(LogTemp, Log, TEXT("1 AU      : %.0f 厘米"), CentimetersPerAu);
	UE_LOG(LogTemp, Log, TEXT("=================================="));
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
	// 只有体素星球携带人。恒星是网格、没有「地表」这个概念，
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

		const FVector RiderLocation = Rider->GetActorLocation();

		if (FVector::DistSquared(RiderLocation, PlanetLocation) > RideRadiusSq)
		{
			continue;
		}

		// 最近者独占。两颗星的判定球可能重叠：本关卡行星一 7.181 km、行星二
		// 9.833 km，相隔 2.652 km，而每颗星的判定半径各 2 km（半径 1 km ×
		// RideRadiusScale 2.0），地表之间只隔 0.652 km —— 远小于判定半径。
		// 重叠区里的 Pawn 会被两颗星各推一次，位移叠加成两倍：行星一相对行星二
		// 的参考系速度是 246 m/s，远超飞船的 MaxSpeed 8000 cm/s，飞船会被卡在
		// 行星二判定球边界上横向扫走，根本飞不到行星二上。
		//
		// 这条规则依赖「所有体素星球的判定半径相同」（RideRadiusScale 是子系统级
		// 配置，本关卡四颗行星半径都是 1 km）。将来若加入半径更小的星球，「更近」就
		// 不再蕴含「够得着」：站在大星判定球内、却离小星更近的 Pawn 会被两颗星同时
		// 放弃。届时要把比较条件改成「更近**且**够得着」。
		//
		// 所以只由「离 Pawn 最近的携带者」推它，其余天体的调用跳过。恒星不是
		// 携带者，它的远近与本规则无关，不能参与比较、更不能否决行星。
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

			// 恒星不是携带者，不参与比较。
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
