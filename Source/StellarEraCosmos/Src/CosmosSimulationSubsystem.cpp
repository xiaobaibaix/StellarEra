// Copyright Voxel Plugin SAS. All Rights Reserved.

#include "CosmosSimulationSubsystem.h"
#include "NBodyIntegration.h"
#include "TrajectoryPrediction.h"

#include "CosmosOrbitComponent.h"
#include "VoxelPlanetActor.h"
#include "CosmosBody.h"
#include "Components/ChildActorComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "Stats/Stats.h"

void UCosmosSimulationSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	// 引擎已在 PostInitProperties 按 UCLASS(Config = Game) 载入过，这里再显式载一次让「值从 ini 来」看得见。
	LoadConfig();

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
	// 只在 PIE 与打包世界里跑。编辑器世界里天体走预览路径，动了会把手工摆好的位置改掉。
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

	// 只从列表摘掉，求解器里的天体留着 —— 删它会让后面所有索引前移、SimulatorIndex 全失效。
	// 但必须清外力与阻尼：否则销毁飞船后那颗幽灵天体一直带着最后一帧的力飞，还让诊断 ⚠ 消失。
	if (Component->SimulatorIndex != INDEX_NONE
		&& Component->SimulatorIndex < Simulator.GetNumBodies())
	{
		FCosmosBodyState& Body = Simulator.GetBodyMutable(Component->SimulatorIndex);
		Body.ExternalAcceleration = FVector3d::ZeroVector;
		Body.LinearDamping = 0.0;

		// 角向同理；角速度不会自己衰减（阻尼刚被清零），力矩也得清。
		Body.ExternalAngularAcceleration = FVector3d::ZeroVector;
		Body.AngularDamping = 0.0;
		Body.AngularVelocity = FVector3d::ZeroVector;
	}
}

void UCosmosSimulationSubsystem::AddBodyToSimulator(UCosmosOrbitComponent* Component)
{
	AActor* BodyActor = Component ? Component->GetOwner() : nullptr;
	if (!BodyActor)
	{
		return;
	}

	// 初态 = 关卡摆放（位置）+ 组件初速，只读不写。单位边界：设计层 M⊕ / km/s → 求解器 M☉ / AU每年。
	if (!FMath::IsFinite(Component->MassEarth) || Component->MassEarth <= 0.0)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[Cosmos] 天体 %s 的 MassEarth = %g 无效（必须 > 0），跳过注册。"),
			*GetNameSafe(BodyActor), Component->MassEarth);
		return;
	}

	FCosmosBodyState State;
	State.Position = Simulator.ToGameUnitPosition(BodyActor->GetActorLocation());
	State.Velocity = CosmosSpeed::KilometerPerSecondToAuPerYear(
		FVector3d(Component->InitialVelocityKmPerSecond));
	State.Mass = CosmosMass::EarthMassToSolarMass(Component->MassEarth);
	State.OwnerActor = BodyActor;

	// 单向引力：bExertsGravity 关掉的是测试粒子，引力源质量按 0 算、自己仍被吸引；Mass 保持真实值供接触与诊断用。
	State.bTestParticle = !Component->bExertsGravity;
	State.bReceivesForces = Component->bReceivesForces;

	if (const ICosmosBody* Ship = Cast<ICosmosBody>(BodyActor))
	{
		// 刚体：姿态归求解器，Actor 的 Rotation 每帧从这里写回；关卡摆放朝向是初态。
		// FQuat 与 FQuat4d 精度不同，逐个分量转。
		State.bRigidBody = true;
		const FQuat InitialRotation = BodyActor->GetActorQuat();
		State.Orientation = FQuat4d(
			InitialRotation.X, InitialRotation.Y, InitialRotation.Z, InitialRotation.W);
	}
	else
	{
		// 行星/恒星：位移交给独立质量体，Chaos 积分；求解器只算力。
		State.bPhysicsDriven = true;
	}

	Component->SimulatorIndex = Simulator.AddBody(State);

	if (!Cast<ICosmosBody>(BodyActor))
	{
		BindPhysicsBody(Component, BodyActor);
	}
}

void UCosmosSimulationSubsystem::BindPhysicsBody(UCosmosOrbitComponent* Component, AActor* Actor)
{
	if (!Component || !Actor || Component->SimulatorIndex == INDEX_NONE
		|| Component->SimulatorIndex >= Simulator.GetNumBodies())
	{
		return;
	}

	FCosmosBodyState& State = Simulator.GetBodyMutable(Component->SimulatorIndex);
	if (State.PhysicsBody.IsValid())
	{
		return;
	}

	// 只认根组件：代理天体的根就是质量球，Actor 的 Transform 与刚体状态天然同一份。
	UPrimitiveComponent* Body = Cast<UPrimitiveComponent>(Actor->GetRootComponent());
	if (!Body || !Body->IsSimulatingPhysics())
	{
		// 根组件不是物理体（如体素世界根组件）：退回求解器积分 + 每帧写回。
		State.bPhysicsDriven = false;
		UE_LOG(LogTemp, Warning,
			TEXT("[Cosmos] %s 的根组件没有模拟物理，退回求解器积分"), *GetNameSafe(Actor));
		return;
	}

	// 位置/速度以物理体为准；物理体静止时用组件的初速播种（BodyInstance 不序列化初速）。
	const FVector InitialWorldVelocity = Body->GetPhysicsLinearVelocity();
	State.Position = Simulator.ToGameUnitPosition(Body->GetComponentLocation());
	State.Velocity = CosmosWorld::WorldCmPerSecondToGameVelocity(
		FVector3d(InitialWorldVelocity), CentimetersPerAu, TimeScale);
	if (InitialWorldVelocity.IsNearlyZero())
	{
		const FVector3d SeededVelocity = CosmosSpeed::KilometerPerSecondToAuPerYear(
			FVector3d(Component->InitialVelocityKmPerSecond));
		if (SeededVelocity.SizeSquared() > 0.0)
		{
			State.Velocity = SeededVelocity;
			Body->SetPhysicsLinearVelocity(ToWorldVelocityScaled(SeededVelocity));
		}
	}
	State.PhysicsBody = Body;

	UE_LOG(LogTemp, Display, TEXT("[Cosmos] %s 绑定物理体 %s（%.3e kg）"),
		*GetNameSafe(Actor), *Body->GetName(), Body->GetMass());
}

AActor* UCosmosSimulationSubsystem::ResolveBodyVisualActor(AActor* BodyActor)
{
	if (!BodyActor)
	{
		return nullptr;
	}

	TArray<UChildActorComponent*> ChildComponents;
	BodyActor->GetComponents<UChildActorComponent>(ChildComponents);
	for (UChildActorComponent* Component : ChildComponents)
	{
		if (AActor* Child = Component ? Component->GetChildActor() : nullptr)
		{
			return Child;
		}
	}
	return BodyActor;
}

UCosmosOrbitComponent* UCosmosSimulationSubsystem::ResolveDrivingOrbit(AActor* Actor)
{
	if (!Actor)
	{
		return nullptr;
	}

	UCosmosOrbitComponent* Orbit = Actor->FindComponentByClass<UCosmosOrbitComponent>();
	if (Orbit && Orbit->SimulatorIndex != INDEX_NONE)
	{
		return Orbit;
	}

	// 代理预制体：天体是被挂载的子 Actor，N 体身份在父级代理上。
	if (AActor* Parent = Actor->GetAttachParentActor())
	{
		if (UCosmosOrbitComponent* ParentOrbit = Parent->FindComponentByClass<UCosmosOrbitComponent>())
		{
			return ParentOrbit;
		}
	}

	return Orbit;
}

void UCosmosSimulationSubsystem::SetTimeScaleMultiplier(const float InMultiplier)
{
	TimeScaleMultiplier = InMultiplier;
}

void UCosmosSimulationSubsystem::SyncPhysicsDrivenBodies()
{
	if (CentimetersPerAu <= 0.0 || TimeScale <= 0.0)
	{
		return;
	}

	for (UCosmosOrbitComponent* Component : Bodies)
	{
		AActor* OwnerActor = Component ? Component->GetOwner() : nullptr;
		if (!OwnerActor || Component->SimulatorIndex == INDEX_NONE
			|| Component->SimulatorIndex >= Simulator.GetNumBodies())
		{
			continue;
		}

		FCosmosBodyState& State = Simulator.GetBodyMutable(Component->SimulatorIndex);
		if (!State.bPhysicsDriven)
		{
			continue;
		}

		// 有独立质量体就读它；飞船没有（Actor 根就是刚体），读 Actor。
		if (const UPrimitiveComponent* Body = State.PhysicsBody.Get())
		{
			State.Position = Simulator.ToGameUnitPosition(Body->GetComponentLocation());
			State.Velocity = CosmosWorld::WorldCmPerSecondToGameVelocity(
				FVector3d(Body->GetPhysicsLinearVelocity()), CentimetersPerAu, TimeScale);
		}
		else if (const ICosmosBody* Ship = Cast<ICosmosBody>(OwnerActor))
		{
			State.Position = Simulator.ToGameUnitPosition(OwnerActor->GetActorLocation());
			State.Velocity = CosmosWorld::WorldCmPerSecondToGameVelocity(
				FVector3d(OwnerActor->GetVelocity()), CentimetersPerAu, TimeScale);
		}
	}
}

void UCosmosSimulationSubsystem::ApplyPhysicsForces()
{
	if (CentimetersPerAu <= 0.0 || TimeScale <= 0.0)
	{
		return;
	}

	for (UCosmosOrbitComponent* Component : Bodies)
	{
		AActor* OwnerActor = Component ? Component->GetOwner() : nullptr;
		if (!OwnerActor || Component->SimulatorIndex == INDEX_NONE
			|| Component->SimulatorIndex >= Simulator.GetNumBodies())
		{
			continue;
		}

		const FCosmosBodyState& State = Simulator.GetBody(Component->SimulatorIndex);
		if (!State.bPhysicsDriven || !State.bReceivesForces)
		{
			continue;
		}

		// 力统一由求解器合成（引力 + 外力 − 阻尼），子系统只把结果加到物理体上。
		const FVector AccelerationWorld = FVector(CosmosWorld::GameAccelerationToWorldCmPerS2(
			State.Acceleration, CentimetersPerAu, TimeScale));

		if (UPrimitiveComponent* Body = State.PhysicsBody.Get())
		{
			Body->AddForce(AccelerationWorld * Body->GetMass());
		}
		else if (ICosmosBody* Ship = Cast<ICosmosBody>(OwnerActor))
		{
			Ship->AddCosmosForceWorldCmPerS2(AccelerationWorld);
		}
	}
}

void UCosmosSimulationSubsystem::ApplyControlledBodyForces(float DeltaSeconds)
{
	bHasForcedBodies = false;

	// 只检查标定合法性：TimeScale = 0 时求解器根本不推进；CentimetersPerAu = 0 会除零。
	if (CentimetersPerAu <= 0.0 || TimeScale <= 0.0)
	{
		return;
	}

	for (UCosmosOrbitComponent* Component : Bodies)
	{
		if (!Component || Component->SimulatorIndex == INDEX_NONE)
		{
			continue;
		}

		ICosmosBody* Ship = Cast<ICosmosBody>(Component->GetOwner());
		if (!Ship)
		{
			continue;
		}

		// GetBodyMutable 是裸的 Bodies[Index]，越界是 UB —— 校验只能在这一层做。
		if (Component->SimulatorIndex >= Simulator.GetNumBodies())
		{
			continue;
		}

		FCosmosBodyState& Body = Simulator.GetBodyMutable(Component->SimulatorIndex);

		// 大气随行星平移（行星不自转），阻力按相对行星的速度算。飞船不能反向依赖本子系统
		// （成环），由这里推参考速度；无行星时给零向量 = 退化为绝对速度。
		FVector ReferenceVelocity = FVector::ZeroVector;
		if (const AVoxelPlanetActor* Planet = Ship->GetPlanetActor())
		{
			const UCosmosOrbitComponent* PlanetOrbit =
				ResolveDrivingOrbit(const_cast<AVoxelPlanetActor*>(Planet));
			if (PlanetOrbit && PlanetOrbit->SimulatorIndex != INDEX_NONE
				&& PlanetOrbit->SimulatorIndex < Simulator.GetNumBodies())
			{
				ReferenceVelocity = ToWorldVelocityScaled(
					Simulator.GetBody(PlanetOrbit->SimulatorIndex).Velocity);
			}
		}
		Ship->SetReferencePlanetVelocity(ReferenceVelocity);

		// 帧时长原样转给飞船：制动按「本帧满推力能改变多少速度」收敛。
		Body.ExternalAcceleration = FVector3d(CosmosWorld::WorldCmPerS2ToGameAcceleration(
			FVector3d(Ship->GetThrustAccelerationWorldCmPerS2(DeltaSeconds)),
			CentimetersPerAu, TimeScale));

		// τ 是真实秒、求解器用模拟年，所以除 TimeScale。
		// ⚠ 阻尼作用在惯性系速度上，不是相对携带天体 —— 行星自己在跑时它会抽走 k·v_行星。
		const float DampingTime = Ship->GetDampingTime();
		Body.LinearDamping = DampingTime > 0.f
			? 1.0 / (double(DampingTime) * TimeScale)
			: 0.0;

		// 角加速度的机体轴要用求解器真实姿态，不能用 Actor 的插值渲染姿态。

		Ship->SetCosmosOrientation(FQuat(

			Body.Orientation.X, Body.Orientation.Y, Body.Orientation.Z, Body.Orientation.W));



		// 实际角速度先写给飞船：横滚的「到顶衰减」要读它，且必须读上一帧写回的值。

		// 求解器单位（rad/模拟年）→ 真实单位（rad/秒）。
		Ship->SetCosmosAngularVelocity(FVector(Body.AngularVelocity * TimeScale));

		// 角加速度：真实 rad/s² → 求解器 rad/模拟年²。多除一个 TimeScale 因为它是 dt 的二阶量。
		Body.ExternalAngularAcceleration =
			FVector3d(Ship->GetAngularAccelerationWorldRadPerS2(DeltaSeconds))
			/ (TimeScale * TimeScale);

		// 角阻尼同 LinearDamping：τ 是真实秒，求解器的时间单位是模拟年。
		Body.AngularDamping = double(Ship->GetAngularDampingPerSecond()) / TimeScale;

		bHasForcedBodies |= !Body.ExternalAcceleration.IsNearlyZero()
			|| !Body.ExternalAngularAcceleration.IsNearlyZero()
			|| Body.LinearDamping != 0.0
			|| Body.AngularDamping != 0.0;
	}
}

double UCosmosSimulationSubsystem::ComputeBaseTimeStep(double InShortestPeriod, int32 InStepsPerOrbit,
                                                       double InTimeScale, int32 InMinStepsPerSecond)
{
	// 推不出精度要求时（单天体 / 退化配对）仍要守住下限，否则同一种「整帧不动」会换个关卡复现。
	constexpr double DefaultStepYears = 5.0e-5;
	const double SmoothStep = (InMinStepsPerSecond > 0 && InTimeScale > 0.0)
		? InTimeScale / double(InMinStepsPerSecond)
		: TNumericLimits<double>::Max();

	if (!FMath::IsFinite(InShortestPeriod) || InShortestPeriod <= 0.0 || InStepsPerOrbit <= 0)
	{
		return FMath::Clamp(FMath::Min(DefaultStepYears, SmoothStep), 1e-12, 1e-2);
	}

	return FMath::Clamp(
		FMath::Min(InShortestPeriod / double(InStepsPerOrbit), SmoothStep), 1e-12, 1e-2);
}

void UCosmosSimulationSubsystem::InterpolateRenderPose(const FVector3d& PrevPosition, const FQuat4d& PrevOrientation,
                                                       const FVector3d& CurPosition, const FQuat4d& CurOrientation,
                                                       double Alpha, FVector3d& OutPosition, FQuat4d& OutOrientation)
{
	const double T = FMath::Clamp(Alpha, 0.0, 1.0);
	OutPosition = FMath::Lerp(PrevPosition, CurPosition, T);
	OutOrientation = FQuat4d::Slerp(PrevOrientation, CurOrientation, T);
}

void UCosmosSimulationSubsystem::CaptureRenderPrevPose()
{
	const int32 NumBodies = Simulator.GetNumBodies();
	RenderPrevPositions.SetNum(NumBodies);
	RenderPrevOrientations.SetNum(NumBodies);

	for (int32 Index = 0; Index < NumBodies; ++Index)
	{
		const FCosmosBodyState& Body = Simulator.GetBody(Index);
		RenderPrevPositions[Index] = Body.Position;
		RenderPrevOrientations[Index] = Body.Orientation;
	}

	bHasRenderPrevPose = true;
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

	// 按质量降序注册：中心天体先进求解器。N 体同时求解，顺序不影响结果，只为日志好读。
	Bodies.Sort([](const UCosmosOrbitComponent& A, const UCosmosOrbitComponent& B)
	{
		return A.MassEarth > B.MassEarth;
	});

	// 注册完就完事，**不要在开局写回一次位置**：厘米 → AU → 厘米的往返会按 float 精度挪动摆放位置。
	for (UCosmosOrbitComponent* Component : Bodies)
	{
		AddBodyToSimulator(Component);
	}

	// ── 自动步长：由实际几何反推，最短配对周期 / StepsPerOrbit，数值看开局日志 ──
	double ShortestPeriod = TNumericLimits<double>::Max();

	for (int32 I = 0; I < Simulator.GetNumBodies(); ++I)
	{
		for (int32 J = I + 1; J < Simulator.GetNumBodies(); ++J)
		{
			// 与 EstimateOrbitalPeriod 共用同一份公式，避免两处各写一遍。
			const double PairPeriod = FNBodySimulator::PairOrbitalPeriod(
				Simulator.GetBody(I), Simulator.GetBody(J),
				CosmosConstants::GravitationalConstant);

			if (PairPeriod > 0.0)
			{
				ShortestPeriod = FMath::Min(ShortestPeriod, PairPeriod);
			}
		}
	}

	// 与 BaseTimeStep 分开判：StepsPerOrbit = 0 时步长退回默认值，但最短周期仍有效（预测窗口要用）。
	if (FMath::IsFinite(ShortestPeriod) && ShortestPeriod > 0.0)
	{
		ShortestPeriodYears = ShortestPeriod;
	}

	// 推不出周期时也要走这一步：单天体系统同样要守住步长下限，否则整帧零步会在别处复现。
	BaseTimeStep = ComputeBaseTimeStep(ShortestPeriod, StepsPerOrbit, TimeScale, MinStepsPerSecond);

	// 注册改了天体顺序，上一份快照的下标已经对不上。
	bHasRenderPrevPose = false;

	Simulator.MarkEnergyBaseline();

	UE_LOG(LogTemp, Log,
		TEXT("[Cosmos] 初始化完成：%d 个天体，最短周期 %.6f 年，步长 %.6e 年，TimeScale %.5f 年/秒，1 AU = %.0f 厘米"),
		Simulator.GetNumBodies(), ShortestPeriod, BaseTimeStep, TimeScale, CentimetersPerAu);

	// ── 每个天体一行初态，只在开局打这一次。最近邻才是该天体预测窗口的来源；
	// 上面的「最短周期」是全系统最小值，不一定属于它 ──
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
			TEXT("[Cosmos]   天体 %d：质量 %.6g M☉（%.6g M⊕），位置 (%.4f, %.4f, %.4f) AU，速度 (%.4f, %.4f, %.4f) AU/年（%.4f km/s），最近邻 %d（%.4f AU），最短周期 %.6f 年"),
			Index, Body.Mass, CosmosMass::SolarMassToEarthMass(Body.Mass),
			Body.Position.X, Body.Position.Y, Body.Position.Z,
			Body.Velocity.X, Body.Velocity.Y, Body.Velocity.Z,
			CosmosSpeed::AuPerYearToKilometerPerSecond(Body.Velocity).Size(),
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

	// 物理驱动的体：先把上一物理步的结果读回求解器，再决定本帧模式。
	SyncPhysicsDrivenBodies();

	// 1× 才开刚体；切模式时对齐位置/速度，避免瞬移后弹开。
	const bool bWantPhysics = FMath::IsNearlyEqual(TimeScaleMultiplier, 1.f);
	for (UCosmosOrbitComponent* Component : Bodies)
	{
		AActor* OwnerActor = Component ? Component->GetOwner() : nullptr;
		ICosmosBody* Ship = Cast<ICosmosBody>(OwnerActor);
		if (!Ship || Component->SimulatorIndex == INDEX_NONE
			|| Component->SimulatorIndex >= Simulator.GetNumBodies())
		{
			continue;
		}

		FCosmosBodyState& State = Simulator.GetBodyMutable(Component->SimulatorIndex);
		if (bWantPhysics && !Ship->IsPhysicsDriven())
		{
			OwnerActor->SetActorLocation(Simulator.ToWorldLocation(State.Position), false);
			Ship->SetCosmosVelocity(ToWorldVelocityScaled(State.Velocity));
			const float MassKg = float(State.Mass * CosmosConstants::SolarMassToKilogram);
			if (Ship->SetPhysicsDriven(true, MassKg))
			{
				State.bPhysicsDriven = true;
				UE_LOG(LogTemp, Display, TEXT("[Ship] 刚体物理开启（质量 %.0f kg）"), MassKg);
			}
			else
			{
				UE_LOG(LogTemp, Error, TEXT("[Ship] 刚体不可用，保持求解器驱动"));
			}
		}
		else if (!bWantPhysics && Ship->IsPhysicsDriven())
		{
			Ship->SetPhysicsDriven(false, 0.f);
			State.bPhysicsDriven = false;
			UE_LOG(LogTemp, Display, TEXT("[Ship] 刚体物理关闭，回求解器驱动"));
		}
	}

	// ── 1. 时间推进：真实秒 × TimeScale = 模拟年数 ──
	// 余量必须跨帧累积（固定步长是辛积分的前提）；单帧另设上限，加载/卡顿后不补一大段。
	const double MaxAdvancePerFrame =
		double(FMath::Max(MaxStepsPerFrame - 1, 1)) * BaseTimeStep;
	AdvanceAccumulator += FMath::Min(double(DeltaTime) * TimeScale, MaxAdvancePerFrame);

	const int32 NumSteps = FMath::FloorToInt(AdvanceAccumulator / BaseTimeStep);
	if (NumSteps > 0)
	{
		const int32 StepsToRun = FMath::Min(NumSteps, MaxStepsPerFrame);

		// 外力只在真正会推进的帧里读：零子步帧里读会清掉飞船的待发角速度冲量，却没有积分
		// 消费写进去的角加速度，整帧鼠标输入静默丢失（144 Hz 下约一半帧）。
		//
		// 传**本帧真正消费的真实秒**而不是帧时长：跳过的帧里冲量是累加的，除数若还是单帧
		// DeltaTime，Δω = (target−current)·T_c/DeltaTime 会放大成 N 倍，帧率越高甩得越狠。
		const double StepRealSeconds = double(StepsToRun) * BaseTimeStep / TimeScale;
		ApplyControlledBodyForces(float(StepRealSeconds));

		// 只扣掉真正跑了的那些步，剩下的留到下一帧。
		AdvanceAccumulator -= double(StepsToRun) * BaseTimeStep;

		for (int32 StepIndex = 0; StepIndex < StepsToRun; ++StepIndex)
		{
			// 快照取「本帧最后一步之前」：取帧首会多落后整整一个步长，插值反而更钝。
			if (StepIndex + 1 == StepsToRun)
			{
				CaptureRenderPrevPose();
			}

			NBodyIntegration::StepVelocityVerlet(Simulator, BaseTimeStep);
		}

		// 兜底，正常不会触发（只把 MaxStepsPerFrame 填成 0/负数时才持续超限）：宁可掉 TimeScale 也不崩帧率。
		if (NumSteps > MaxStepsPerFrame)
		{
			TimeScale *= 0.9;
			UE_LOG(LogTemp, Warning,
				TEXT("[Cosmos] 单帧积分步数超限（%d > %d），TimeScale 自动降至 %.5f"),
				NumSteps, MaxStepsPerFrame, TimeScale);
		}
	}

	// 力每帧都要加：刚体在物理线程里每帧都会推进，零子步帧也不能断。
	ApplyPhysicsForces();

	// 整数步数会把时间的零头丢掉：不插值就是整帧不动、再整步跳（帧率却满）。
	// 按累积余量摊平，代价是画面最多落后一个步长。
	const double RenderAlpha = (bHasRenderPrevPose && BaseTimeStep > 0.0)
		? FMath::Clamp(AdvanceAccumulator / BaseTimeStep, 0.0, 1.0)
		: 1.0;

	// ── 2. 写回各个天体 ──
	for (UCosmosOrbitComponent* Component : Bodies)
	{
		AActor* BodyActor = Component ? Component->GetOwner() : nullptr;
		if (!BodyActor || Component->SimulatorIndex == INDEX_NONE)
		{
			continue;
		}

		const FVector PreviousLocation = BodyActor->GetActorLocation();

		const FCosmosBodyState& State = Simulator.GetBody(Component->SimulatorIndex);

		// 写回插值后的位姿，不写求解器当前解 —— 后者是「整步跳」的源头。
		FVector3d RenderPosition = State.Position;
		FQuat4d RenderOrientation = State.Orientation;
		if (RenderAlpha < 1.0
			&& RenderPrevPositions.IsValidIndex(Component->SimulatorIndex)
			&& RenderPrevOrientations.IsValidIndex(Component->SimulatorIndex))
		{
			InterpolateRenderPose(
				RenderPrevPositions[Component->SimulatorIndex],
				RenderPrevOrientations[Component->SimulatorIndex],
				State.Position, State.Orientation, RenderAlpha,
				RenderPosition, RenderOrientation);
		}

		// 刚体（飞船）位置 + 朝向一起写回。非刚体只写位置：它们的 Orientation 恒为单位四元数，
		// 走 SetActorTransform 会把关卡里摆好的行星朝向瞬间抹成零。
		if (State.bRigidBody)
		{
			const FQuat WorldOrientation(
				RenderOrientation.X, RenderOrientation.Y,
				RenderOrientation.Z, RenderOrientation.W);
			if (State.bPhysicsDriven)
			{
				// 位移归 Chaos，这里只写姿态（第一版不做力矩）。
				BodyActor->SetActorRotation(WorldOrientation, ETeleportType::None);
			}
			else
			{
				BodyActor->SetActorTransform(
					FTransform(WorldOrientation,
						Simulator.ToWorldLocation(RenderPosition),
						BodyActor->GetActorScale3D()),
					/*bSweep=*/false, nullptr, ETeleportType::None);
			}
		}
		else if (!State.bPhysicsDriven)
		{
			BodyActor->SetActorLocation(
				Simulator.ToWorldLocation(RenderPosition), /*bSweep=*/false);
		}
		// 物理驱动的天体：根组件就是物理体，Transform 归 Chaos，写回会把它瞬移回上一帧。

		// 物理驱动的船速度由刚体自己答，不再用求解器快照覆盖。
		if (ICosmosBody* Ship = Cast<ICosmosBody>(BodyActor); Ship && !State.bPhysicsDriven)
		{
			Ship->SetCosmosVelocity(ToWorldVelocityScaled(State.Velocity));
		}

		// 位移取 Actor 实际变化量而非求解器差值：物理驱动的体位移归 Chaos，取实际才两边自洽。
		const FVector Delta = BodyActor->GetActorLocation() - PreviousLocation;
		if (Delta.IsNearlyZero())
		{
			continue;
		}

		// 体素世界就是星球 Actor 自己（继承 AVoxelWorld），移动 Actor 即移动世界。

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

	// 有受控天体时能量/动量都不再守恒，漂移值不是积分误差：推力推正、阻尼与接触耗散拉负。
	UE_LOG(LogTemp, Log, TEXT("[Cosmos] 能量漂移  : %.6e  （辛积分应 < 1e-5）%s"),
		Simulator.GetEnergyDriftRatio(),
		bHasForcedBodies ? TEXT("  ⚠ 含受控天体，此值不守恒") : TEXT(""));
	UE_LOG(LogTemp, Log, TEXT("[Cosmos] 总动量    : %.6e"), Simulator.GetTotalMomentum().Size());
	UE_LOG(LogTemp, Log, TEXT("[Cosmos] 角动量    : %.6e"), Simulator.GetTotalAngularMomentum().Size());
	UE_LOG(LogTemp, Log, TEXT("[Cosmos] 1 AU      : %.0f 厘米"), CentimetersPerAu);

	// 受控天体单独一行，值非零才打：没有输入时外力为 0，但阻尼恒非零，这行就是「读到了飞船」的证据。
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

	// AU → 厘米的中间结果用局部数组接一下；每帧每个天体两次分配，N ≤ 10 无所谓。
	TArray<FVector3d> GameUnitPositions;
	if (!TrajectoryPrediction::Predict(Simulator, SimulatorIndex, HorizonYears, NumSamples, GameUnitPositions))
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

bool UCosmosSimulationSubsystem::PredictClosedOrbitWorld(int32 SimulatorIndex,
                                                        int32 NumSamplesPerOrbit,
                                                        TArray<FVector>& OutWorldPositions,
                                                        FVector& OutPrimaryWorld,
                                                        double ReturnToleranceFraction) const
{
	OutWorldPositions.Reset();
	OutPrimaryWorld = FVector::ZeroVector;

	FCosmosClosedOrbitPrediction Prediction;
	if (!TrajectoryPrediction::PredictClosedOrbit(Simulator, SimulatorIndex, NumSamplesPerOrbit, Prediction,
		ReturnToleranceFraction))
	{
		return false;
	}
	OutPrimaryWorld = Simulator.ToWorldLocation(Prediction.PrimaryPosition);

	// 闭环相对主导天体：锚回它 t=0 的位置再转世界坐标。主导天体自己也在动，绝对路径是摆线。
	OutWorldPositions.Reserve(Prediction.RelativePositions.Num());
	for (const FVector3d& Relative : Prediction.RelativePositions)
	{
		OutWorldPositions.Add(Simulator.ToWorldLocation(Relative + Prediction.PrimaryPosition));
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
	// GetBody 是裸的 Bodies[Index]，越界是 UB —— 校验只能在这一层做。
	if (SimulatorIndex < 0 || SimulatorIndex >= Simulator.GetNumBodies())
	{
		return FVector3d::ZeroVector;
	}

	return Simulator.GetBody(SimulatorIndex).Velocity;
}

FVector3d UCosmosSimulationSubsystem::GetBodyGravityGameUnits(int32 SimulatorIndex) const
{
	// 理由同上：GetBody 越界是 UB。
	if (SimulatorIndex < 0 || SimulatorIndex >= Simulator.GetNumBodies())
	{
		return FVector3d::ZeroVector;
	}

	return Simulator.GetBody(SimulatorIndex).GravitationalAcceleration;
}

FVector UCosmosSimulationSubsystem::ToWorldVelocityScaled(const FVector3d& GameUnitVelocity) const
{
	return FVector(CosmosWorld::GameVelocityToWorldCmPerSecond(
		GameUnitVelocity, CentimetersPerAu, TimeScale));
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
	// 只有 AVoxelPlanetActor 携带人；其它天体没有「地表」，站在上面的东西靠自身重力待着。
	// 代理预制体下挂的才是行星本体，先解析再转型。
	const AVoxelPlanetActor* Planet = Cast<AVoxelPlanetActor>(ResolveBodyVisualActor(BodyActor));
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

		// 已挂在别的 Actor 上的 Pawn 一律跳过：附着关系本来就会带走它，再位移就是走两步。
		// TActorIterator 顺序不保证，按附着关系去重才可靠。本函数是兜底，只管没有附着父级
		// 却仍够得着判定球的 Pawn。
		if (Rider->GetAttachParentActor())
		{
			continue;
		}

		// 自身是 N 体天体的 Pawn 也跳过（上方 SetActorLocation 已搬过）。判据用 SimulatorIndex：
		// 注册前的组件求解器不管，那种 Pawn 仍算乘客。
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

		// 最近者独占：判定球重叠时 Pawn 会被推两次。只由最近的 AVoxelPlanetActor 推，距离相等按
		// (距离平方, UniqueID) 取最小。该规则依赖「所有星球判定半径相同」，加更小星球要重写。
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
			if (!Cast<AVoxelPlanetActor>(ResolveBodyVisualActor(OtherBody)))
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
		Rider->AddActorWorldOffset(Delta, /*bSweep=*/false);
	}
}
