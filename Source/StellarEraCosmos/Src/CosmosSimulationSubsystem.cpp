// Copyright Voxel Plugin SAS. All Rights Reserved.

#include "CosmosSimulationSubsystem.h"

#include "CosmosOrbitComponent.h"
#include "VoxelPlanetActor.h"
#include "CosmosBody.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "Stats/Stats.h"

void UCosmosSimulationSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	// 引擎已在 PostInitProperties 按 UCLASS(Config = Game) 载入过，这里再显式载一次让「值从 ini 来」看得见。
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

		// 角向同理；角速度不会自己衰减（阻尼刚被清零），探针也得清 —— 否则幽灵天体继续
		// 拿上百个探针和行星做接触求解，结果既不写回也没人看。
		Body.ExternalAngularAcceleration = FVector3d::ZeroVector;
		Body.AngularDamping = 0.0;
		Body.AngularVelocity = FVector3d::ZeroVector;
		Body.SetContactProbes({});
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
	State.Velocity = CosmosUnits::KilometerPerSecondToAuPerYear(
		FVector3d(Component->InitialVelocityKmPerSecond));
	State.Mass = CosmosUnits::EarthMassToSolarMass(Component->MassEarth);
	State.OwnerActor = BodyActor;

	// 单向引力：bExertsGravity 关掉的是测试粒子，引力源质量按 0 算、自己仍被吸引；Mass 保持真实值供接触与诊断用。
	State.bTestParticle = !Component->bExertsGravity;

	// 半径只对「有地表」的天体（星球、飞船）有意义，其余留 0 = 点质量永不接触。
	if (const AVoxelPlanetActor* Planet = Cast<AVoxelPlanetActor>(BodyActor))
	{
		// 平均球半径只是兜底：第一次 Tick 会被 RefreshPlanetContactRadii 换成地形半径。
		State.Radius = double(Planet->GetSurfaceRadiusCm()) / CentimetersPerAu;
	}
	else if (const ICosmosBody* Ship = Cast<ICosmosBody>(BodyActor))
	{
		// 300 cm 球只是兜底：第一次 Tick 会被 RefreshShipContactProbes 换成凸包探针（Radius 置 0）。
		State.Radius = double(Ship->GetContactRadiusCm()) / CentimetersPerAu;

		// 刚体：姿态归求解器，Actor 的 Rotation 每帧从这里写回；关卡摆放朝向是初态。
		// FQuat 与 FQuat4d 精度不同，逐个分量转。
		State.bRigidBody = true;
		const FQuat InitialRotation = BodyActor->GetActorQuat();
		State.Orientation = FQuat4d(
			InitialRotation.X, InitialRotation.Y, InitialRotation.Z, InitialRotation.W);

		// 不再置 bZeroVelocityOnContact（撞地即停）：刚体接触靠摩擦停。
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

		// 只有最近的那个天体可能进到接触距离内，只算朝它那个方向的地表高度。
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

		// 方向 = 星球中心 → 最近天体。AU 与世界空间只差等比缩放、没有旋转，可原样交给地形查询。
		const FVector Direction =
			FVector(Simulator.GetBody(NearestIndex).Position - Simulator.GetBody(Index).Position);

		const double SurfaceRadiusCm = double(Planet->GetSurfaceRadiusCmAtDirection(Direction));
		if (SurfaceRadiusCm > 0.0)
		{
			Simulator.GetBodyMutable(Index).Radius = SurfaceRadiusCm / CentimetersPerAu;
		}
	}
}

void UCosmosSimulationSubsystem::RefreshShipContactProbes()
{
	for (UCosmosOrbitComponent* Component : Bodies)
	{
		AActor* OwnerActor = Component ? Component->GetOwner() : nullptr;
		ICosmosBody* Ship = Cast<ICosmosBody>(OwnerActor);
		if (!Ship || Component->SimulatorIndex == INDEX_NONE)
		{
			continue;
		}

		// 与 ApplyControlledBodyForces 同一个理由：GetBodyMutable 越界不报错，只能在这里查。
		if (Component->SimulatorIndex >= Simulator.GetNumBodies())
		{
			continue;
		}

		FCosmosBodyState& State = Simulator.GetBodyMutable(Component->SimulatorIndex);

		// 没有探针（网格没导 Simple Collision）时退回 300 cm 球 —— 行为与加探针前一致。
		const TArray<FVector>& LocalProbes = Ship->GetContactProbesLocalCm();
		if (LocalProbes.Num() == 0)
		{
			State.SetContactProbes({});
			State.Radius = double(Ship->GetContactRadiusCm()) / CentimetersPerAu;

			// 按 spec §3.4 退回球体惯量。当前是空转：探针已清空，接触走球-球分支，而那条
			// 分支不施加角冲量。写它只为将来球-球开始做旋转接触时不必回头找。
			//
			// ⚠ 行为变了：旧 bZeroVelocityOnContact 会让飞船撞上就停，现在球-球只抹法向、
			// 没有摩擦，飞船会贴着地表一直滑。只在 Hull 没导 Simple Collision 时才会走到。
			State.InverseInertiaLocal = FCosmosBodyState::InverseInertiaFromSphere(
				State.Mass, FMath::Max(State.Radius, 1.0e-9));
			continue;
		}

		// 本地厘米 → 世界厘米（只取旋转+缩放，探针相对船心）→ AU。每帧重算是为了跟上船的转向。
		const FTransform ShipTransform = OwnerActor->GetActorTransform();
		TArray<FVector3d> Probes;
		Probes.Reserve(LocalProbes.Num());
		for (const FVector& LocalProbe : LocalProbes)
		{
			const FVector RelativeCm = ShipTransform.TransformVector(LocalProbe);
			Probes.Add(FVector3d(RelativeCm) / CentimetersPerAu);
		}

		State.SetContactProbes(MoveTemp(Probes));
		State.Radius = 0.0;

		// 惯性张量：用探针点的本地 AABB 当长方体。探针即船体凸包代表点，AABB 就是船体包围盒，
		// 不必读网格资产。⚠ 这里用**未缩放**的本地探针，而上面的探针走了世界缩放；飞船 scale=1
		// 时两者一致，哪天飞船被缩放，惯量得按缩放后的半边长算。
		FBox ProbeBox(ForceInit);
		for (const FVector& LocalProbe : LocalProbes)
		{
			ProbeBox += LocalProbe;
		}

		const FVector HalfExtentCm = ProbeBox.GetExtent();
		if (HalfExtentCm.X > 0.f && HalfExtentCm.Y > 0.f && HalfExtentCm.Z > 0.f)
		{
			State.InverseInertiaLocal = FCosmosBodyState::InverseInertiaFromBox(
				State.Mass, FVector3d(HalfExtentCm) / CentimetersPerAu);
		}
		else
		{
			// 探针全落在一个平面 / 一条线上：长方体公式退化，退回均匀球，至少不会是零惯量。
			State.InverseInertiaLocal = FCosmosBodyState::InverseInertiaFromSphere(
				State.Mass, FMath::Max(State.ContactProbeRadius, 1.0e-9));
		}

		// 逐探针地表半径必须在推进前查一次并缓存 —— 一帧最多 200 个子步，体素查询绝不能放进去。
		// 粗筛：距离 < 包围球（1.25 × 基准半径）+ 最远探针。
		int32 NearestPlanetIndex = INDEX_NONE;
		double NearestPlanetDistance = 0.0;

		for (int32 Other = 0; Other < Simulator.GetNumBodies(); ++Other)
		{
			if (Other == Component->SimulatorIndex)
			{
				continue;
			}

			const AVoxelPlanetActor* Candidate =
				Cast<AVoxelPlanetActor>(Simulator.GetBody(Other).OwnerActor.Get());
			if (!Candidate)
			{
				continue;
			}

			const double Distance =
				(State.Position - Simulator.GetBody(Other).Position).Size();
			const double GateCm = double(Candidate->GetSurfaceRadiusCm()) * 1.25
				+ State.ContactProbeRadius * CentimetersPerAu;
			if (Distance * CentimetersPerAu >= GateCm)
			{
				continue;
			}

			if (NearestPlanetIndex == INDEX_NONE || Distance < NearestPlanetDistance)
			{
				NearestPlanetIndex = Other;
				NearestPlanetDistance = Distance;
			}
		}

		if (NearestPlanetIndex == INDEX_NONE)
		{
			continue;
		}

		const AVoxelPlanetActor* Planet =
			Cast<AVoxelPlanetActor>(Simulator.GetBody(NearestPlanetIndex).OwnerActor.Get());
		const FVector3d PlanetPosition = Simulator.GetBody(NearestPlanetIndex).Position;

		// 方向量化：一格 ≈ 地表 1 体素。船体投影到地表通常只有个位数格，真正 march 的次数也是。
		const double BaseRadiusVoxels =
			double(Planet->GetSurfaceRadiusCm()) / FMath::Max(double(Planet->VoxelSize), 1.0);

		TMap<FIntVector, double> SurfaceRadiusCache;
		int32 NumQueries = 0;
		// 超上限告警的闸门：按 Ship 每次调用一次，而不是每个方向一次。
		bool bLoggedOverCap = false;

		TArray<double> SurfaceRadii;
		SurfaceRadii.Reserve(State.ContactProbes.Num());

		for (const FVector3d& Probe : State.ContactProbes)
		{
			const FVector UnitDirection =
				FVector(State.Position + Probe - PlanetPosition).GetSafeNormal();
			if (UnitDirection.IsNearlyZero())
			{
				// 探针正好落在星心：方向无定义，交给解析半径兜底。
				SurfaceRadii.Add(double(Planet->GetSurfaceRadiusCm()) / CentimetersPerAu);
				continue;
			}

			const FIntVector Key(
				FMath::RoundToInt(UnitDirection.X * BaseRadiusVoxels),
				FMath::RoundToInt(UnitDirection.Y * BaseRadiusVoxels),
				FMath::RoundToInt(UnitDirection.Z * BaseRadiusVoxels));

			if (const double* Cached = SurfaceRadiusCache.Find(Key))
			{
				SurfaceRadii.Add(*Cached);
				continue;
			}

			double SurfaceRadiusCm = 0.0;
			if (NumQueries >= MaxContactSurfaceQueriesPerFrame)
			{
				// 超上限：这一格退回解析半径。每个 Ship 每次调用只吭一声。
				if (!bLoggedOverCap)
				{
					bLoggedOverCap = true;
					UE_LOG(LogTemp, Warning,
						TEXT("[Cosmos] 单帧逐探针地表查询达到上限 %d，其余方向退回解析半径。"),
						MaxContactSurfaceQueriesPerFrame);
				}
				SurfaceRadiusCm = double(Planet->GetSurfaceRadiusCmAtDirection(UnitDirection));
			}
			else
			{
				++NumQueries;
				SurfaceRadiusCm =
					double(Planet->GetVoxelSurfaceRadiusCmAtDirection(UnitDirection));
			}

			// 体素查询出错时退回解析半径：NaN 会让穿透量变成 NaN、探针静默不接触。
			if (!FMath::IsFinite(SurfaceRadiusCm) || SurfaceRadiusCm <= 0.0)
			{
				SurfaceRadiusCm = double(Planet->GetSurfaceRadiusCmAtDirection(UnitDirection));
			}

			// 缓存与逐探针数组都存 AU —— 求解器单位。
			const double SurfaceRadiusAu = SurfaceRadiusCm / CentimetersPerAu;
			SurfaceRadiusCache.Add(Key, SurfaceRadiusAu);
			SurfaceRadii.Add(SurfaceRadiusAu);
		}

		State.ContactProbeSurfaceRadii = MoveTemp(SurfaceRadii);
		State.ContactSurfaceSphereIndex = NearestPlanetIndex;
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
				Planet->FindComponentByClass<UCosmosOrbitComponent>();
			if (PlanetOrbit && PlanetOrbit->SimulatorIndex != INDEX_NONE
				&& PlanetOrbit->SimulatorIndex < Simulator.GetNumBodies())
			{
				ReferenceVelocity = ToWorldVelocityScaled(
					Simulator.GetBody(PlanetOrbit->SimulatorIndex).Velocity);
			}
		}
		Ship->SetReferencePlanetVelocity(ReferenceVelocity);

		// 帧时长原样转给飞船：制动按「本帧满推力能改变多少速度」收敛。
		Body.ExternalAcceleration = FVector3d(CosmosUnits::WorldCmPerS2ToGameAcceleration(
			FVector3d(Ship->GetThrustAccelerationWorldCmPerS2(DeltaSeconds)),
			CentimetersPerAu, TimeScale));

		// τ 是真实秒、求解器用模拟年，所以除 TimeScale。
		// ⚠ 阻尼作用在惯性系速度上，不是相对携带天体 —— 行星自己在跑时它会抽走 k·v_行星。
		const float DampingTime = Ship->GetDampingTime();
		Body.LinearDamping = DampingTime > 0.f
			? 1.0 / (double(DampingTime) * TimeScale)
			: 0.0;

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
				CosmosUnits::GravitationalConstant_Game);

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

	if (FMath::IsFinite(ShortestPeriod) && ShortestPeriod > 0.0 && StepsPerOrbit > 0)
	{
		BaseTimeStep = FMath::Clamp(ShortestPeriod / double(StepsPerOrbit), 1e-12, 1e-2);
	}

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
			TEXT("[Cosmos]   天体 %d：质量 %.6g M☉（%.6g M⊕），半径 %.4f AU，位置 (%.4f, %.4f, %.4f) AU，速度 (%.4f, %.4f, %.4f) AU/年（%.4f km/s），最近邻 %d（%.4f AU），最短周期 %.6f 年"),
			Index, Body.Mass, CosmosUnits::SolarMassToEarthMass(Body.Mass), Body.Radius,
			Body.Position.X, Body.Position.Y, Body.Position.Z,
			Body.Velocity.X, Body.Velocity.Y, Body.Velocity.Z,
			CosmosUnits::AuPerYearToKilometerPerSecond(Body.Velocity).Size(),
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

	// ── 0. 刷新接触形状 ──
	// 必须在推进之前：接触解算发生在 StepExact 内部，读的就是这一刻的 Radius / ContactProbes。
	// 外力不在这里读 —— 只在真正会推进的帧里读，见下面 NumSteps > 0 那一支。
	RefreshPlanetContactRadii();
	RefreshShipContactProbes();

	// ── 1. 时间推进：真实秒 × TimeScale = 模拟年数 ──
	// 余量必须跨帧累积（固定步长是辛积分的前提）；单帧另设上限，加载/卡顿后不补一大段。
	const double MaxAdvancePerFrame =
		double(FMath::Max(MaxStepsPerFrame - 1, 1)) * BaseTimeStep;
	AdvanceAccumulator += FMath::Min(double(DeltaTime) * TimeScale, MaxAdvancePerFrame);

	const int32 NumSteps = FMath::FloorToInt(AdvanceAccumulator / BaseTimeStep);
	if (NumSteps > 0)
	{
		const int32 StepsToRun = FMath::Min(NumSteps, MaxStepsPerFrame);

		// 外力只在真正会推进的帧里读：零子步帧里读会清掉飞船的待发角速度冲量，却没有 StepExact
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
			Simulator.StepExact(BaseTimeStep);
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

			// 刚体（飞船）位置 + 朝向一起写回。非刚体只写位置：它们的 Orientation 恒为单位四元数，
			// 走 SetActorTransform 会把关卡里摆好的行星朝向瞬间抹成零。
			if (State.bRigidBody)
			{
				BodyActor->SetActorTransform(
					FTransform(
						FQuat(
							State.Orientation.X, State.Orientation.Y,
							State.Orientation.Z, State.Orientation.W),
						Simulator.ToWorldLocation(State.Position),
						BodyActor->GetActorScale3D()),
					/*bSweep=*/false, nullptr, ETeleportType::None);
			}
			else
			{
				BodyActor->SetActorLocation(
					Simulator.ToWorldLocation(State.Position), /*bSweep=*/false);
			}

			// 受控天体（飞船）：速度经 ICosmosBody::SetCosmosVelocity 写回 Actor，体素调用器、IsLanded() 与 HUD 都读那一处。
			if (ICosmosBody* Ship = Cast<ICosmosBody>(BodyActor))
			{
				Ship->SetCosmosVelocity(ToWorldVelocityScaled(State.Velocity));
			}
		}

		// 位移取 Actor 实际变化量而非求解器差值：bSimulated 为假时求解器在漂、Actor 不动，取实际才两边自洽。
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

bool UCosmosSimulationSubsystem::PredictClosedOrbitWorld(int32 SimulatorIndex,
                                                        int32 NumSamplesPerOrbit,
                                                        TArray<FVector>& OutWorldPositions) const
{
	OutWorldPositions.Reset();

	FCosmosClosedOrbitPrediction Prediction;
	if (!Simulator.PredictClosedOrbit(SimulatorIndex, NumSamplesPerOrbit, Prediction))
	{
		return false;
	}

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
	return FVector(CosmosUnits::GameVelocityToWorldCmPerSecond(
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
		Rider->AddActorWorldOffset(Delta, /*bSweep=*/false);
	}
}
