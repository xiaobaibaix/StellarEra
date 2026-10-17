// 宇宙游戏模式 —— 实现。

#include "CosmosGameMode.h"

#include "NBodySimulator.h"
#include "CosmosBodyActor.h"
#include "CosmosCameraPawn.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"

ACosmosGameMode::ACosmosGameMode()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;

	// 默认玩家就是一个自由飞行相机
	DefaultPawnClass = ACosmosCameraPawn::StaticClass();
	PlayerControllerClass = APlayerController::StaticClass();
}

///////////////////////////////////////////////////////////////////////////////
// 初始化

void ACosmosGameMode::BeginPlay()
{
	Super::BeginPlay();

	// ── 1. 配置求解器 ──
	Simulator.SetUnitSystem(UnitSystem);
	Simulator.SetIntegrator(Integrator);
	Simulator.SetSofteningLength(SofteningLength);
	Simulator.SetGravityCutoff(GravityCutoffAu);
	Simulator.Clear();

	// ── 2. 收集并注册天体 ──
	CollectBodies();

	// ── 3. 自动调优步长 ──
	if (bAutoTuneTimeStep)
	{
		AutoTuneTimeStep();
	}

	// ── 4. 建立照明关系 ──
	// 每颗恒星给每个受照天体挂一个平行光。
	// 多恒星系统下，一颗行星会同时被多个方向的平行光照亮。
	for (ACosmosBodyActor* Body : Bodies)
	{
		if (Body)
		{
			Body->RebuildLighting(Bodies);
		}
	}

	// ── 5. 记录能量基准，供漂移诊断 ──
	Simulator.MarkEnergyBaseline();

	// ── 5. 把天体注册给相机，供聚焦功能使用 ──
	if (AController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr)
	{
		if (ACosmosCameraPawn* CameraPawn = Cast<ACosmosCameraPawn>(PC->GetPawn()))
		{
			RegisterFocusTargets(CameraPawn);
		}
	}

	// 注意：这里不主动停用体素星球。
	// UE 的调用顺序是 GameMode::BeginPlay 先于关卡里的 Actor，
	// 此时 ACosmosBodyActor::BeginPlay 还没执行，
	// 父子查找也还没做完。停用逻辑放在 Tick 的首帧处理。

	UE_LOG(LogTemp, Log,
		TEXT("[Cosmos] 初始化完成：%d 个天体，步长 %.3e 年，TimeScale %.1f"),
		Bodies.Num(), BaseTimeStep, TimeScale);
}

void ACosmosGameMode::CollectBodies()
{
	Bodies.Reset();

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	for (TActorIterator<ACosmosBodyActor> It(World); It; ++It)
	{
		ACosmosBodyActor* Body = *It;
		if (!Body)
		{
			continue;
		}

		Bodies.Add(Body);
		Body->InitializeBody(Simulator);
	}

	// 按类型排序：恒星先进求解器，这样后续天体算轨道时中心质量已经在了。
	// 实际上顺序不影响 N 体结果（同时求解），但便于阅读诊断输出。
	Bodies.Sort([](const ACosmosBodyActor& A, const ACosmosBodyActor& B)
	{
		return A.BodyType < B.BodyType;
	});
}

void ACosmosGameMode::RegisterFocusTargets(ACosmosCameraPawn* CameraPawn) const
{
	if (!CameraPawn)
	{
		return;
	}
	for (ACosmosBodyActor* Body : Bodies)
	{
		CameraPawn->RegisterFocusTarget(Body);
	}
}

void ACosmosGameMode::AutoTuneTimeStep()
{
	// 找出最短轨道周期，然后取其 1/1000 作为步长。
	// 开普勒第三定律：T² = 4π²a³/(GM)
	if (UnitSystem != ECosmosUnitSystem::Game)
	{
		return;   // SI 模式下让用户自己填
	}

	double ShortestPeriod = TNumericLimits<double>::Max();

	for (ACosmosBodyActor* Body : Bodies)
	{
		if (!Body || !Body->bSimulated || Body->Orbit.SemiMajorAxis <= 0.0)
		{
			continue;
		}

		const double a = Body->Orbit.SemiMajorAxis;
		const double GM = CosmosUnits::GravitationalConstant_Game * FMath::Max(Body->CentralMassSolar, 1e-6);

		const double Period = 2.0 * PI * FMath::Sqrt(a * a * a / GM);
		ShortestPeriod = FMath::Min(ShortestPeriod, Period);
	}

	if (!FMath::IsFinite(ShortestPeriod) || ShortestPeriod <= 0.0)
	{
		return;
	}

	const double Tuned = ShortestPeriod / 1000.0;
	BaseTimeStep = FMath::Clamp(Tuned, 1e-12, 1e-2);

	UE_LOG(LogTemp, Log,
		TEXT("[Cosmos] 自动调优：最短周期 %.6f 年 → 步长 %.3e 年"),
		ShortestPeriod, BaseTimeStep);
}

///////////////////////////////////////////////////////////////////////////////
// 模拟循环

void ACosmosGameMode::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (Bodies.IsEmpty())
	{
		return;
	}

	// ── 时间推进 ──
	// 游戏真实时间 × TimeScale = 需要模拟的天文时间
	const double WantedAdvance = double(DeltaSeconds) * double(TimeScale);
	int32 StepsToRunLastFrame = 0;

	if (WantedAdvance > 0.0)
	{
		// 需要推进多少个完整步长
		const int32 NumSteps = FMath::FloorToInt(WantedAdvance / BaseTimeStep);

		if (NumSteps > 0)
		{
			const int32 StepsToRun = FMath::Min(NumSteps, MaxStepsPerFrame);
			StepsToRunLastFrame = StepsToRun;

			for (int32 i = 0; i < StepsToRun; ++i)
			{
				Simulator.StepExact(BaseTimeStep);
			}

			// 超限说明 TimeScale 太大，降一点而不是让帧率崩掉
			if (NumSteps > MaxStepsPerFrame)
			{
				TimeScale *= 0.9f;
				UE_LOG(LogTemp, Warning,
					TEXT("[Cosmos] 单帧积分步数超限(%d>%d)，TimeScale 自动降至 %.1f"),
					NumSteps, MaxStepsPerFrame, TimeScale);
			}
		}
	}

	// ── 同步回各个 Actor ──
	FVector CameraLocation = FVector::ZeroVector;
	if (const APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr)
	{
		// GetPlayerViewPoint 需要非 const，这里用取值的方式避开
		FVector ViewLocation = FVector::ZeroVector;
		FRotator ViewRotation = FRotator::ZeroRotator;
		PC->GetPlayerViewPoint(ViewLocation, ViewRotation);
		CameraLocation = ViewLocation;
	}

	// 本帧实际推进的天文时间，用于自转计算
	const double AdvancedTime = double(StepsToRunLastFrame) * BaseTimeStep;

	for (ACosmosBodyActor* Body : Bodies)
	{
		if (!Body)
		{
			continue;
		}

		if (Body->bSimulated)
		{
			Body->SyncFromSimulator(Simulator);

			// 自转：与公转同步推进，保持视觉合理
			Body->UpdateSpin(AdvancedTime, Simulator);
		}

		// 体素星球按相机距离切换 LOD。
		// 远处只有球体网格，近处才挂载体素地形。
		if (Body->VoxelPlanetActor)
		{
			// 首帧兜底：确保远处天体的体素星球是停用的。
			// 放在这里而不是 BeginPlay，是因为 GameMode 先于关卡 Actor 执行，
			// 那时父子关系还没建立完。
			if (!bFirstFrameDone)
			{
				Body->SetVoxelPlanetActive(false);
			}

			Body->UpdateVoxelPlanetLOD(CameraLocation);
		}

		// 光照方向每帧更新——恒星也在公转
		Body->UpdateLighting();
	}

	bFirstFrameDone = true;
}

///////////////////////////////////////////////////////////////////////////////
// 诊断

double ACosmosGameMode::GetEnergyDriftRatio() const
{
	return Simulator.GetEnergyDriftRatio();
}

double ACosmosGameMode::GetEffectiveTimeStep() const
{
	return BaseTimeStep;
}

double ACosmosGameMode::GetSimulatedYears() const
{
	return Simulator.GetSimulatedTime();
}

void ACosmosGameMode::LogDiagnostics() const
{
	UE_LOG(LogTemp, Log, TEXT("========== Cosmos 诊断 =========="));
	UE_LOG(LogTemp, Log, TEXT("天体数量:%d"), Bodies.Num());
	UE_LOG(LogTemp, Log, TEXT("单位系统  : %s"),
		UnitSystem == ECosmosUnitSystem::Game ? TEXT("Game(AU/年/M☉)") : TEXT("SI(cm/s/kg)"));
	UE_LOG(LogTemp, Log, TEXT("积分器    : %s"),
		Integrator == ECosmosIntegrator::VelocityVerlet ? TEXT("VelocityVerlet")
		: Integrator == ECosmosIntegrator::Euler ? TEXT("Euler") : TEXT("None"));
	UE_LOG(LogTemp, Log, TEXT("步长      : %.6e"), BaseTimeStep);
	UE_LOG(LogTemp, Log, TEXT("TimeScale : %.1f"), TimeScale);
	UE_LOG(LogTemp, Log, TEXT("已模拟    : %.6f"), Simulator.GetSimulatedTime());
	UE_LOG(LogTemp, Log, TEXT("能量漂移  : %.6e  (辛积分应<1e-5)"), Simulator.GetEnergyDriftRatio());
	UE_LOG(LogTemp, Log, TEXT("总动量    : %.6e"), Simulator.GetTotalMomentum().Size());
	UE_LOG(LogTemp, Log, TEXT("角动量    : %.6e"), Simulator.GetTotalAngularMomentum().Size());

	for (const ACosmosBodyActor* Body : Bodies)
	{
		if (!Body)
		{
			continue;
		}
		const FVector Pos = Body->GetActorLocation();
		UE_LOG(LogTemp, Log,
			TEXT("  %-24s质量=%.6e  a=%.4f  位置AU=(%.4f, %.4f, %.4f)"),
			*Body->GetName(), Body->MassSolar, Body->Orbit.SemiMajorAxis,
			double(Pos.X) / CosmosUnits::AuToCentimeter,
			double(Pos.Y) / CosmosUnits::AuToCentimeter,
			double(Pos.Z) / CosmosUnits::AuToCentimeter);
	}

	UE_LOG(LogTemp, Log, TEXT("=================================="));
}
