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

	FCosmosBodyState State;

	// 起始位置用 Actor 当前摆放的位置；bApplyOrbitInit 为真时随后会被轨道根数覆盖。
	State.Position = Simulator.ToGameUnitPosition(BodyActor->GetActorLocation());
	State.Mass = Component->MassSolar;
	State.OwnerActor = BodyActor;

	// 半径只对体素星球有意义（只有它才有「地表」这个概念）。恒星之类的天体
	// 不携带任何人，留 0 即可 —— 携带判定走的是 Task 5 里单独的一条路径。
	if (const AVoxelPlanetActor* Planet = Cast<AVoxelPlanetActor>(BodyActor))
	{
		State.Radius = double(Planet->GetSurfaceRadiusCm()) / CentimetersPerAu;
	}

	Component->SimulatorIndex = Simulator.AddBody(State);

	if (Component->bApplyOrbitInit)
	{
		Simulator.ApplyOrbitInit(Component->SimulatorIndex, Component->Orbit, Component->CentralMassSolar);
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

	for (UCosmosOrbitComponent* Component : Bodies)
	{
		AddBodyToSimulator(Component);

		AActor* BodyActor = Component ? Component->GetOwner() : nullptr;
		if (!BodyActor || !Component->bSimulated || Component->SimulatorIndex == INDEX_NONE)
		{
			continue;
		}

		// 立即同步一次：让开局第一帧就在轨道根数算出的位置上，
		// 而不是关卡里手摆的位置（手摆的位置跟轨道对不上，会先弹一下）。
		const FCosmosBodyState& State = Simulator.GetBody(Component->SimulatorIndex);
		BodyActor->SetActorLocation(Simulator.ToWorldLocation(State.Position), /*bSweep=*/false);

		if (AVoxelPlanetActor* Planet = Cast<AVoxelPlanetActor>(BodyActor))
		{
			Planet->SyncVoxelWorldTransform();
		}
	}

	// ── 自动步长 ──
	// 取最短的一条轨道周期，除以 StepsPerOrbit。开普勒第三定律：T = 2π√(a³/GM)。
	double ShortestPeriod = TNumericLimits<double>::Max();

	for (const UCosmosOrbitComponent* Component : Bodies)
	{
		if (!Component || !Component->bApplyOrbitInit || Component->Orbit.SemiMajorAxis <= 0.0)
		{
			continue;
		}

		const double SemiMajorAxis = Component->Orbit.SemiMajorAxis;
		const double Mu = CosmosUnits::GravitationalConstant_Game * FMath::Max(Component->CentralMassSolar, 1e-6);
		const double Period = 2.0 * PI * FMath::Sqrt(SemiMajorAxis * SemiMajorAxis * SemiMajorAxis / Mu);

		ShortestPeriod = FMath::Min(ShortestPeriod, Period);
	}

	if (FMath::IsFinite(ShortestPeriod) && ShortestPeriod > 0.0 && StepsPerOrbit > 0)
	{
		BaseTimeStep = FMath::Clamp(ShortestPeriod / double(StepsPerOrbit), 1e-12, 1e-2);
	}

	Simulator.MarkEnergyBaseline();

	UE_LOG(LogTemp, Log,
		TEXT("[Cosmos] 初始化完成：%d 个天体，最短周期 %.6f 年，步长 %.6e 年，TimeScale %.5f 年/秒，1 AU = %.0f 厘米"),
		Simulator.GetNumBodies(), ShortestPeriod, BaseTimeStep, TimeScale, CentimetersPerAu);
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

		// 持续超限说明 TimeScale 相对步长太大。降一点，而不是让帧率崩掉。
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

		if (AVoxelPlanetActor* Planet = Cast<AVoxelPlanetActor>(BodyActor))
		{
			// 体素世界是 Actor 子对象，变换不会跟着 PlanetRoot 走。
			// 星球移动后必须同步，否则渲染网格留在原地、星球跑掉。
			Planet->SyncVoxelWorldTransform();
		}

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

		if (FVector::DistSquared(Rider->GetActorLocation(), PlanetLocation) > RideRadiusSq)
		{
			continue;
		}

		// 不用 Sweep：Pawn 本来就在地面上，硬扫会把它从地形里挤出去。
		// 这一帧星球平移多少，Pawn 就跟着平移多少，相对关系原样保留。
		Rider->AddActorWorldOffset(Delta, /*bSweep=*/false);
	}
}
