// 天体 Actor —— 实现。

#include "CosmosBodyActor.h"

#include "NBodySimulator.h"
#include "CosmosTrailComponent.h"
#include "VoxelPlanetActor.h"
#include "Engine/StaticMesh.h"
#include "UObject/ConstructorHelpers.h"

ACosmosBodyActor::ACosmosBodyActor()
{
	PrimaryActorTick.bCanEverTick = false;   // 位置更新由 GameMode 统一驱动

	MeshComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	SetRootComponent(MeshComponent);

	// 点光源只在 Point 模式下用。默认隐藏——
	// 平行光模式下，每个天体身上挂的是 DirectionalLights 数组里的那些。
	PointLightComponent = CreateDefaultSubobject<UPointLightComponent>(TEXT("PointLight"));
	PointLightComponent->SetupAttachment(MeshComponent);
	PointLightComponent->SetVisibility(false);

	// 轨道拖尾。自己开 Tick，与 Actor 的 Tick 无关。
	TrailComponent = CreateDefaultSubobject<UCosmosTrailComponent>(TEXT("Trail"));
	TrailComponent->SetupAttachment(MeshComponent);

	// 默认用引擎自带球体，省得手工指定
	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereMesh(
		TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	if (SphereMesh.Succeeded())
	{
		MeshComponent->SetStaticMesh(SphereMesh.Object);
	}

	bCanBeDamaged = false;
	SetActorEnableCollision(false);
}

void ACosmosBodyActor::BeginPlay()
{
	Super::BeginPlay();

	// 父子关系优先：如果在关卡里把体素星球挂到了本 Actor 下面，
	// 自动识别并采用它，省去手动指定 VoxelPlanetActor 字段。
	if (!VoxelPlanetActor && bAutoFindChildVoxelPlanet)
	{
		AVoxelPlanetActor* Attached = FindAttachedVoxelPlanet();
		if (Attached)
		{
			VoxelPlanetActor = Attached;
			UE_LOG(LogTemp, Log,
				TEXT("[Cosmos] %s 自动识别到子级体素星球 %s"),
				*GetName(), *Attached->GetName());
		}
	}

	UpdateVisuals();
	UpdateLight();
}

AVoxelPlanetActor* ACosmosBodyActor::FindAttachedVoxelPlanet() const
{
	// 遍历所有子 Actor，找第一个 AVoxelPlanetActor。
	// 支持任意层级嵌套，不只是直接子级。
	TArray<AActor*> Children;
	GetAllChildActors(Children, /*bResetArray*/ true);

	for (AActor* Child : Children)
	{
		if (AVoxelPlanetActor* Planet = Cast<AVoxelPlanetActor>(Child))
		{
			return Planet;
		}
	}
	return nullptr;
}

void ACosmosBodyActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
}

void ACosmosBodyActor::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	// 编辑器里改参数立刻生效，不用等 PIE
	UpdateVisuals();
	UpdateLight();
}

///////////////////////////////////////////////////////////////////////////////
// 求解器接入

void ACosmosBodyActor::InitializeBody(FNBodySimulator& Simulator)
{
	FCosmosBodyState State;

	// 起始位置先用 Actor 当前摆放的位置
	State.Position = Simulator.ToGameUnitPosition(GetActorLocation());
	State.Mass = MassSolar;

	// 视觉半径 AU → 游戏单位距离（本身就是 AU，所以直接用）
	State.Radius = VisualRadiusAu;
	State.OwnerActor = this;

	SimulatorIndex = Simulator.AddBody(State);

	// 用轨道根数覆盖初始位置与速度
	if (bSimulated)
	{
		Simulator.ApplyOrbitInit(SimulatorIndex, Orbit, CentralMassSolar);
	}

	// 立即同步一次，让关卡里能看到天体的初始摆放
	SyncFromSimulator(Simulator);
}

void ACosmosBodyActor::SyncFromSimulator(const FNBodySimulator& Simulator)
{
	if (SimulatorIndex == INDEX_NONE)
	{
		return;
	}

	const FCosmosBodyState& State = Simulator.GetBody(SimulatorIndex);

	// 从求解器读回真实位置
	SetActorLocation(Simulator.ToWorldLocation(State.Position));
}

FVector ACosmosBodyActor::GetCurrentPosition() const
{
	return GetActorLocation();
}

FVector ACosmosBodyActor::GetCurrentVelocity() const
{
	// 没有求解器上下文时返回零
	return FVector::ZeroVector;
}

///////////////////////////////////////////////////////////////////////////////
// 体素星球对接

FVector ACosmosBodyActor::GetOrbitNormal() const
{
	// 轨道法线：垂直于轨道平面的方向。
	// 用当前位置与中心天体的方向叉乘轨道角动量，
	// 若算不出来则退回到世界 Z 轴。
	FVector Reference = FVector::UpVector;
	if (CentralMassSolar > 0.0)
	{
		Reference = -GetActorLocation().GetSafeNormal();
	}

	// 用一个与 Reference 不平行的辅助向量构造正交基
	const FVector Helper = FMath::Abs(Reference.Z) < 0.9f
		? FVector(0.f, 0.f, 1.f)
		: FVector(1.f, 0.f, 0.f);

	const FVector Normal = FVector::CrossProduct(Reference, Helper);
	if (Normal.IsNearlyZero())
	{
		return FVector::UpVector;
	}
	return Normal.GetSafeNormal();
}

void ACosmosBodyActor::UpdateSpin(double DeltaGameSeconds, const FNBodySimulator& Simulator)
{
	if (SpinDegreesPerSecond == 0.0)
	{
		return;
	}

	// 自转让整个世界跟着转。玩家降落后站在星球表面时，
	// 这种旋转会让人眩晕（相当于地球自转一整圈只花几秒）。
	//
	// 解法：体素星球激活时冻结自转，远景球体则正常转。
	// 这样远看有自转暗示，近看脚下是稳定的。
	//
	// 冻结时不重置累计角度，而是保持 SpinAngleAtFreeze，
	// 否则玩家靠近的瞬间星球朝向会跳变。
	if (bVoxelPlanetActive)
	{
		AccumulatedSpinDegrees = SpinAngleAtFreeze;
	}
	else
	{
		AccumulatedSpinDegrees += SpinDegreesPerSecond * DeltaGameSeconds;
	}

	const FVector OrbitNormal = GetOrbitNormal();

	// 先绕轨道法线自转，再施加自转轴倾角。
	// 这样「赤道面」相对轨道的倾角保持恒定——和真实行星一致。

	// 把 Up 轴对齐到轨道法线的旋转（四元数）
	const FQuat AlignToOrbit = FQuat::FindBetweenNormals(FVector::UpVector, OrbitNormal);

	// 绕该对齐后的轴自转
	const FQuat Spin = FQuat(OrbitNormal, FMath::DegreesToRadians(AccumulatedSpinDegrees));

	// 再叠加自转轴倾角
	const FQuat Tilt = FRotator(0.f, 0.f, AxialTiltDegrees).Quaternion();

	SetActorRotation(AlignToOrbit * Spin * Tilt);
}

void ACosmosBodyActor::UpdateVoxelPlanetLOD(const FVector& CameraWorldLocation)
{
	if (!VoxelPlanetActor)
	{
		return;
	}

	const bool bShouldBeActive = (VoxelActivationDistanceAu <= 0.0)
		? true
		: (FVector::Dist(CameraWorldLocation, GetActorLocation()) / CosmosUnits::AuToCentimeter)
			<= VoxelActivationDistanceAu;

	if (bShouldBeActive != bVoxelPlanetActive)
	{
		SetVoxelPlanetActive(bShouldBeActive);
	}

	if (bVoxelPlanetActive)
	{
		// 如果体素星球是本 Actor 的子级，UE 的附着机制会自动
		// 让它跟随父级变换，这里绝对不能手动设世界坐标——
		// 否则会和附着系统打架，每帧来回抖动。
		//
		// 只有「未附着」的情况才需要手动定位。
		const bool bIsAttached = VoxelPlanetActor->GetAttachParentActor() != nullptr
			|| VoxelPlanetActor->IsAttachedTo(this);

		if (!bIsAttached)
		{
			// 体素世界用局部坐标定位时（bUseLocalCoordinates），
			// 这里的偏移只影响「非附着」的兜底情况。
			const FVector Offset = VoxelLocalOffset * CosmosUnits::AuToCentimeter;
			VoxelPlanetActor->SetActorLocation(GetActorLocation() + Offset);
		}
		// 附着的情况下位置由 UE 自己处理，无需干预。
	}
}

void ACosmosBodyActor::SetVoxelPlanetActive(bool bActive)
{
	bVoxelPlanetActive = bActive;

	// 切换时把当前朝向存下来。
	// 否则自转冻结在某个任意角度，玩家靠近时星球的朝向会突然跳变。
	if (bActive)
	{
		SpinAngleAtFreeze = AccumulatedSpinDegrees;
	}

	if (VoxelPlanetActor)
	{
		VoxelPlanetActor->SetActorHiddenInGame(!bActive);
	}

	// 激活体素后隐藏球体网格，避免看到两层球
	if (MeshComponent)
	{
		MeshComponent->SetVisibility(!bActive || BodyType == ECosmosBodyType::Star);
	}

	UE_LOG(LogTemp, Log,
		TEXT("[Cosmos] %s 体素星球 %s"),
		*GetName(), bActive ? TEXT("已激活") : TEXT("已停用"));
}

///////////////////////////////////////////////////////////////////////////////
// 外观

void ACosmosBodyActor::UpdateVisuals()
{
	if (!MeshComponent)
	{
		return;
	}

	if (BodyMesh)
	{
		MeshComponent->SetStaticMesh(BodyMesh);
	}

	// AU → 厘米。球体网格默认直径 100，所以半径缩放要乘 0.5。
	if (VisualRadiusAu > 0.0)
	{
		const float Scale = float(VisualRadiusAu * CosmosUnits::AuToCentimeter * 0.5);
		MeshComponent->SetRelativeScale3D(FVector(Scale));
	}

	// 颜色写进材质实例
	UMaterialInstanceDynamic* MID = MeshComponent->CreateAndSetMaterialInstanceDynamic(0);
	if (MID)
	{
		MID->SetVectorParameterValue(TEXT("Color"), DisplayColor);

		// 恒星自发光，普通天体不自发光
		MID->SetScalarParameterValue(TEXT("EmissiveStrength"),
			BodyType == ECosmosBodyType::Star ? 8.0 : 0.0);
	}

	// 同步拖尾开关
	if (TrailComponent)
	{
		TrailComponent->bEnabled = bShowTrail;
	}
}

void ACosmosBodyActor::UpdateLight()
{
	// 点光源只在 Point 模式下工作。
	// 平行光由 DirectionalLights 数组管理，见 RebuildLighting / UpdateLighting。
	if (!PointLightComponent)
	{
		return;
	}

	const bool bUsePoint = (LightMode == ECosmosLightMode::Point);

	// Point 模式下只有恒星自身发光
	const bool bEmitsPointLight = bUsePoint && (BodyType == ECosmosBodyType::Star);

	PointLightComponent->SetVisibility(bEmitsPointLight);
	PointLightComponent->SetIntensity(bEmitsPointLight ? PointLightIntensity : 0.f);
	PointLightComponent->SetLightColor(LightColor);

	if (bEmitsPointLight && PointLightAttenuationRadiusAu > 0.0)
	{
		const float Radius = float(PointLightAttenuationRadiusAu * CosmosUnits::AuToCentimeter);
		PointLightComponent->SetAttenuationRadius(Radius);
	}
	else
	{
		PointLightComponent->SetAttenuationRadius(0.f);
	}
}

///////////////////////////////////////////////////////////////////////////////
// 多恒星照明

void ACosmosBodyActor::RebuildLighting(const TArray<ACosmosBodyActor*>& AllBodies)
{
	// 清掉旧的平行光
	for (UDirectionalLightComponent* Light : DirectionalLights)
	{
		if (Light)
		{
			Light->DestroyComponent();
		}
	}
	DirectionalLights.Reset();
	Illuminators.Reset();

	// Point 模式不走这条路
	if (LightMode != ECosmosLightMode::Directional)
	{
		return;
	}

	// 恒星本身不需要被照亮
	if (BodyType == ECosmosBodyType::Star)
	{
		return;
	}

	// 确定照明来源
	TArray<ACosmosBodyActor*> Sources;
	if (IlluminatingStars.IsEmpty())
	{
		// 没指定就用场景里所有恒星
		for (ACosmosBodyActor* Body : AllBodies)
		{
			if (Body && Body->BodyType == ECosmosBodyType::Star && Body != this)
			{
				Sources.Add(Body);
			}
		}
	}
	else
	{
		for (const TWeakObjectPtr<ACosmosBodyActor>& Weak : IlluminatingStars)
		{
			if (ACosmosBodyActor* Star = Weak.Get())
			{
				if (Star->BodyType == ECosmosBodyType::Star)
				{
					Sources.Add(Star);
				}
			}
		}
	}

	const FVector SelfLocation = GetActorLocation();

	for (ACosmosBodyActor* Star : Sources)
	{
		if (!Star)
		{
			continue;
		}

		const double DistanceAu = FVector::Dist(SelfLocation, Star->GetActorLocation())
			/ CosmosUnits::AuToCentimeter;

		// 超出演示范围的恒星直接跳过，省掉无意义的灯光
		if (Star->StarLightRangeAu > 0.0 && DistanceAu > Star->StarLightRangeAu)
		{
			continue;
		}

		// 每颗恒星给本天体挂一个平行光
		UDirectionalLightComponent* DirLight = NewObject<UDirectionalLightComponent>(this);
		DirLight->SetupAttachment(MeshComponent);
		DirLight->RegisterComponent();

		DirLight->SetIntensity(Star->LuminosityLux);
		DirLight->SetLightColor(Star->LightColor);

		// 平行光不投影：AU 尺度下阴影贴图毫无意义且极贵
		DirLight->SetCastShadows(false);

		// 光从恒星射来，所以方向是从恒星指向本天体
		const FVector LightDirection = (SelfLocation - Star->GetActorLocation()).GetSafeNormal();
		DirLight->SetRelativeRotation((-LightDirection).Rotation());

		DirectionalLights.Add(DirLight);
		Illuminators.Add(Star);
	}

	UE_LOG(LogTemp, Log,
		TEXT("[Cosmos] %s 被 %d 颗恒星照亮"),
		*GetName(), DirectionalLights.Num());
}

void ACosmosBodyActor::UpdateLighting()
{
	// 恒星也在公转，所以光的方向需要每帧重算。
	// 平行光之间可以叠加——多恒星系统天然支持，
	// 一颗行星被两颗恒星照射时，两个方向的贡献会自动相加。
	//
	// 只更新强度为 0 的那些：超出照明范围的恒星可以直接跳过。
	const int32 Count = FMath::Min(DirectionalLights.Num(), Illuminators.Num());
	const FVector SelfLocation = GetActorLocation();

	for (int32 i = 0; i < Count; ++i)
	{
		UDirectionalLightComponent* Light = DirectionalLights[i];
		ACosmosBodyActor* Star = Illuminators[i].Get();

		if (!Light || !Star)
		{
			continue;
		}

		// 恒星被销毁或超出范围 → 关掉这个光
		const double DistanceAu = FVector::Dist(SelfLocation, Star->GetActorLocation())
			/ CosmosUnits::AuToCentimeter;

		const bool bInRange = (Star->StarLightRangeAu <= 0.0)
			|| (DistanceAu <= Star->StarLightRangeAu);

		Light->SetVisibility(bInRange);
		if (!bInRange)
		{
			continue;
		}

		// 距离越远恒星看起来越暗，这是真实的视觉规律。
		// 用平方反比归一化到 1 AU 处的强度。
		Light->SetIntensity(Star->LuminosityLux / FMath::Max(DistanceAu * DistanceAu, 1e-6));

		// 更新光照方向：从恒星指向本天体。
		// 因为组件附着在本 Actor 上，要用相对旋转，所以要减去自身旋转。
		const FVector LightDirection = (SelfLocation - Star->GetActorLocation()).GetSafeNormal();
		const FRotator WorldRotation = (-LightDirection).Rotation();
		Light->SetWorldRotation(WorldRotation);
	}
}
