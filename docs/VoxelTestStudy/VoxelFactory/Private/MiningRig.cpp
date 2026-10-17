// 挖掘机 —— 实现。

#include "MiningRig.h"

#include "PlanetSurfaceProbe.h"
#include "OreSystem.h"
#include "CosmosBodyActor.h"
#include "VoxelPlanetActor.h"
#include "VoxelWorld.h"
#include "VoxelData/VoxelDataIncludes.h"
#include "Engine/StaticMesh.h"
#include "UObject/ConstructorHelpers.h"

AMiningRig::AMiningRig()
{
	PrimaryActorTick.bCanEverTick = true;

	MeshComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	SetRootComponent(MeshComponent);

	StatusLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("StatusLight"));
	StatusLight->SetupAttachment(MeshComponent);
	// 指示灯是纯视觉辅助，不做物理照明，也不需要衰减计算
	StatusLight->SetIntensity(500.f);
	StatusLight->SetAttenuationRadius(3000.f);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(
		TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (CubeMesh.Succeeded())
	{
		MeshComponent->SetStaticMesh(CubeMesh.Object);
	}

	MeshComponent->SetRelativeScale3D(FVector(1.5f));

	// 碰撞配置：让传送带的自动连接能找到我们。
	// QueryOnly + 全 Overlap：能被查到，但不阻挡任何东西。
	MeshComponent->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	MeshComponent->SetCollisionObjectType(ECC_WorldDynamic);
	MeshComponent->SetCollisionResponseToAllChannels(ECR_Overlap);
}

void AMiningRig::BeginPlay()
{
	Super::BeginPlay();

	CachedMID = MeshComponent->CreateAndSetMaterialInstanceDynamic(0);

	// 如果矿脉配置是空的（玩家没填），用预设填一份
	if (OreConfig.PlanetRadius <= 0.0)
	{
		FPlanetPresets::ApplyOreOnly(
			PlanetType, Seed, PlanetRadiusVoxels, OreConfig);
	}

	// 构造矿脉采样器
	OreSampler = MakeUnique<FOreSystem>();
	OreSampler->Init(OreConfig);
	UpdateStatusLight();
}

///////////////////////////////////////////////////////////////////////////////
// 锚定

bool AMiningRig::AnchorToPlanet(
	AActor* Planet,
	AActor* PlanetActor,
	const FVector& WorldDirection,
	float HeightOffset)
{
	if (!Planet || !PlanetActor)
	{
		return false;
	}

	// 方向：优先用调用者给的，否则从星球中心指过来
	FVector Direction = WorldDirection;
	if (Direction.IsNearlyZero())
	{
		Direction = GetActorLocation() - Planet->GetActorLocation();
	}
	Direction = Direction.GetSafeNormal();
	if (Direction.IsNearlyZero())
	{
		return false;
	}

	Anchor.Planet = Planet;
	Anchor.LocalDirection = Direction;
	Anchor.LocalHeight = HeightOffset;

	PlanetActorRef = PlanetActor;

	// 记录星球半径（体素），供深度计算用
	if (const AVoxelPlanetActor* VoxelPlanet = Cast<AVoxelPlanetActor>(PlanetActor))
	{
		PlanetRadiusVoxels = VoxelPlanet->Radius;
	}

	UpdateTransformFromAnchor();
	UpdateStatusLight();

	UE_LOG(LogTemp, Log,
		TEXT("[Mining] %s 已锚定到 %s，方向 (%.3f, %.3f, %.3f)"),
		*GetName(), *Planet->GetName(),
		Direction.X, Direction.Y, Direction.Z);

	return true;
}

void AMiningRig::Unanchor()
{
	Anchor.Planet.Reset();
	PlanetActorRef = nullptr;
	SetWorking(false);
}

void AMiningRig::UpdateTransformFromAnchor()
{
	if (!Anchor.IsValid())
	{
		return;
	}

	// 从星球中心指向锚定方向
	const FVector PlanetLocation = Anchor.Planet->GetActorLocation();
	FVector WorldOffset = Anchor.LocalDirection.GetSafeNormal() * 1000.f;

	// 高度：沿径向偏移
	WorldOffset += Anchor.LocalDirection.GetSafeNormal() * float(Anchor.LocalHeight);

	SetActorLocation(PlanetLocation + WorldOffset);

	// 朝向：机器的"上"对齐径向，这样它贴着地表站立
	FVector Up = Anchor.LocalDirection.GetSafeNormal();
	FQuat AlignUp = FQuat::FindBetweenNormals(FVector::UpVector, Up);

	// 叠加局部自转
	SetActorRotation((AlignUp * Anchor.LocalRotation.Quaternion()).Rotator());
}

///////////////////////////////////////////////////////////////////////////////
// 工作循环

void AMiningRig::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (!bWorking)
	{
		return;
	}

	if (!Anchor.IsValid())
	{
		SetWorking(false);
		return;
	}

	// 星球在动，锚点每帧都要重新投影
	UpdateTransformFromAnchor();

	// 探测当前作业点
	const FSurfaceProbeResult Probe = ProbeSurface();
	if (!Probe.bHit)
	{
		// 探测失败说明体素世界还没加载好，停机等待
		return;
	}

	CurrentAbundance = Probe.OreAbundance;

	if (CurrentAbundance < Config.DepletionThreshold)
	{
		// 矿挖空了
		UE_LOG(LogTemp, Log, TEXT("[Mining] %s 矿脉枯竭，停止工作"), *GetName());
		SetWorking(false);
		UpdateStatusLight();
		return;
	}

	// 按频率挖掘
	DigTimer += DeltaSeconds;
	const float Interval = 1.f / FMath::Max(Config.DigsPerSecond, 0.01f);

	while (DigTimer >= Interval)
	{
		DigTimer -= Interval;
		PerformDig();

		if (!bWorking)
		{
			break;
		}
	}
}

FSurfaceProbeResult AMiningRig::ProbeSurface() const
{
	FSurfaceProbeResult Result;

	AVoxelWorld* VoxelWorld = GetVoxelWorld();
	if (!VoxelWorld || !OreSampler.IsValid())
	{
		return Result;
	}

	// 从星球外侧往里打射线。
	// 起点用基准半径的两倍，保证在星球外面。
	const FVector StartVoxels = Anchor.LocalDirection.GetSafeNormal()
		* (PlanetRadiusVoxels * 2.0);

	const FVector DirectionVoxels = -Anchor.LocalDirection.GetSafeNormal();

	Result = FPlanetSurfaceProbe::ProbeVoxelWorld(
		VoxelWorld, StartVoxels, DirectionVoxels,
		/*StepVoxels*/ 4.0,
		/*MaxDistanceVoxels*/ PlanetRadiusVoxels * 2.0);

	if (!Result.bHit)
	{
		return Result;
	}

	// 选矿种：自动模式下挑当前点最富的
	if (bAutoSelectOre)
	{
		float BestRichness = 0.f;
		const EOreType Dominant = OreSampler->GetDominantOre(Result.VoxelLocation, BestRichness);
		ActiveOre = (Dominant != EOreType::None) ? Dominant : PrimaryOre;
	}
	else
	{
		ActiveOre = PrimaryOre;
	}

	// 计算挖掘难度与矿物丰富度
	{
		// 深度：命中点相对基准半径的距离差，负值代表在地下
		const double Depth = Result.SurfaceDistanceVoxels - PlanetRadiusVoxels;
		// 矿脉采样：用 FOreSystem，有深度分层与空间噪声
		Result.OreAbundance = OreSampler->SampleRichness(Result.VoxelLocation, ActiveOre);

		// 开采难度用该矿的实际硬度
		if (const FOreDistribution* Dist = OreConfig.GetDistribution(ActiveOre))
		{
			Result.DigDifficulty = FOreSystem::GetDigDifficulty(*Dist, Depth);
		}
		else
		{
			Result.DigDifficulty = 1.0f;
		}
	}

	return Result;
}

void AMiningRig::PerformDig()
{
	AVoxelWorld* VoxelWorld = GetVoxelWorld();
	if (!VoxelWorld)
	{
		return;
	}

	int32& Stored = Storage.FindOrAdd(PrimaryOre);
	if (Stored >= StorageCapacity)
	{
		return;
	}

	const FSurfaceProbeResult Probe = ProbeSurface();
	if (!Probe.bHit)
	{
		return;
	}

	// ------------------------------------------------------------------
	// 核心：真正挖掉体素
	// ------------------------------------------------------------------
	// 用 FVoxelData::SetValue 逐体素写成「空」。
	//
	// 为什么不用 FVoxelSphereToolsImpl::RemoveSphere：
	// 它需要 TVoxelDataImpl<T>，而 GetDataImpl() 是 UVoxelToolBase 的成员方法，
	// 插件外部拿不到。直接调 SetValue 虽然是逐体素而非球形遍历，
	// 但挖掘半径只有几十体素，性能完全可接受，且不依赖内部 API。
	const FVector DownDirection = (-Probe.VoxelLocation.GetSafeNormal()).GetSafeNormal();
	const float DigRadius = FMath::Max(Config.DigRadiusVoxels, 1.f);

	// 球心：地表往下 DigDepthRatio × 半径 处，
	// 保证真的挖进地里而不是只削掉表层
	const double Depth = DigRadius * Config.DigDepthRatio;
	const FVector DigCenterVoxels = Probe.VoxelLocation + DownDirection * Depth;

	FVoxelData& Data = VoxelWorld->GetData();

	const int32 R = FMath::CeilToInt(DigRadius);
	const int32 RadiusSq = FMath::FloorToInt(DigRadius * DigRadius);
	const FIntVector CenterInt(
		FMath::RoundToInt(DigCenterVoxels.X),
		FMath::RoundToInt(DigCenterVoxels.Y),
		FMath::RoundToInt(DigCenterVoxels.Z));

	int32 RemovedVoxels = 0;

	for (int32 DX = -R; DX <= R; ++DX)
	{
		for (int32 DY = -R; DY <= R; ++DY)
		{
			for (int32 DZ = -R; DZ <= R; ++DZ)
			{
				const int32 DistSq = DX * DX + DY * DY + DZ * DZ;
				if (DistSq > RadiusSq)
				{
					continue;
				}

				const FIntVoxel P(CenterInt.X + DX, CenterInt.Y + DY, CenterInt.Z + DZ);

				// 先确认这里原本是实体，避免把空气也"挖"一遍
				float CurrentDensity = 0.f;
				if (!Data.GetValue(P, /*LOD*/ 0, FVoxelItemStack::EmptyRef, CurrentDensity))
				{
					continue;   // 该体素不存在（LOD 范围外）
				}

				if (CurrentDensity > 0.f)
				{
					continue;   // 本来就是空气
				}

				// 挖掉 → 设成满值（完全空气）
				Data.SetValue(P, FVoxelValue::Empty());
				++RemovedVoxels;
			}
		}
	}

	++TotalDigs;

	// 没挖到东西就不用产出（比如已经把这一层挖穿了）
	if (RemovedVoxels == 0)
	{
		return;
	}

	// 产量由矿脉系统决定：基础产量 × (0.5 + 浓度)
	float BaseYield = float(YieldPerDig);
	if (const FOreDistribution* YieldDist = OreConfig.GetDistribution(ActiveOre))
	{
		BaseYield = YieldDist->BaseYield;
	}

	const int32 Produced = FMath::Max(1,
		FMath::RoundToInt(BaseYield * (0.5f + FMath::Clamp(Probe.OreAbundance, 0.f, 1.f) * 0.5f)));

	Stored = FMath::Min(Stored + Produced, StorageCapacity);

	UE_LOG(LogTemp, Verbose,
		TEXT("[Mining] %s 第 %d 次作业：中心 (%.0f, %.0f, %.0f) 挖掉 %d 体素 产出 %d"),
		*GetName(), TotalDigs,
		DigCenterVoxels.X, DigCenterVoxels.Y, DigCenterVoxels.Z,
		RemovedVoxels, Produced);
}

///////////////////////////////////////////////////////////////////////////////
// 辅助

AVoxelWorld* AMiningRig::GetVoxelWorld() const
{
	const AVoxelPlanetActor* VoxelPlanet = Cast<AVoxelPlanetActor>(PlanetActorRef);
	if (!VoxelPlanet)
	{
		return nullptr;
	}
	return VoxelPlanet->VoxelWorld;
}

void AMiningRig::SetWorking(bool bInWorking)
{
	if (bWorking == bInWorking)
	{
		return;
	}

	bWorking = bInWorking;
	DigTimer = 0.f;
	UpdateStatusLight();
}

void AMiningRig::UpdateStatusLight()
{
	if (!StatusLight)
	{
		return;
	}

	StatusLight->SetLightColor(bWorking ? StatusColorOn : StatusColorOff);
}

///////////////////////////////////////////////////////////////////////////////
// 物流接口

int32 AMiningRig::TakeOre(EItemType Item, int32 Count)
{
	if (Item == EItemType::None || Count <= 0)
	{
		return 0;
	}

	// Storage 的键是 EOreType，这里把物品类型转回去
	// （挖掘机只产出矿石，不产出加工品）
	EOreType Ore = EOreType::None;
	switch (Item)
	{
	case EItemType::IronOre:     Ore = EOreType::Iron;      break;
	case EItemType::CopperOre:   Ore = EOreType::Copper;    break;
	case EItemType::SiliconOre:  Ore = EOreType::Silicon;   break;
	case EItemType::TitaniumOre: Ore = EOreType::Titanium;  break;
	case EItemType::UraniumOre:  Ore = EOreType::Uranium;   break;
	case EItemType::Water:       Ore = EOreType::Water;     break;
	default:                     return 0;
	}

	int32* Stored = Storage.Find(Ore);
	if (!Stored)
	{
		return 0;
	}

	const int32 Taken = FMath::Min(*Stored, Count);
	*Stored -= Taken;

	if (*Stored <= 0)
	{
		Storage.Remove(Ore);
	}

	return Taken;
}

int32 AMiningRig::GetTotalStored() const
{
	int32 Total = 0;
	for (const TPair<EOreType, int32>& Pair : Storage)
	{
		Total += Pair.Value;
	}
	return Total;
}

bool AMiningRig::IsStorageFull() const
{
	return GetTotalStored() >= StorageCapacity;
}

int32 AMiningRig::GetProducedAmount(EOreType Ore) const
{
	const int32* Found = Storage.Find(Ore);
	return Found ? *Found : 0;
}

///////////////////////////////////////////////////////////////////////////////
// FPlanetAnchor 实现

void FPlanetAnchor::RefreshDirection(const FVector& WorldLocation)
{
	if (!Planet.IsValid())
	{
		return;
	}

	// 从星球中心指过来的方向
	FVector Dir = (WorldLocation - Planet->GetActorLocation()).GetSafeNormal();
	if (!Dir.IsNearlyZero())
	{
		LocalDirection = Dir;
	}
}

void FPlanetAnchor::ToWorldTransform(FTransform& OutTransform) const
{
	if (!Planet.IsValid())
	{
		OutTransform = FTransform::Identity;
		return;
	}

	const FVector PlanetLocation = Planet->GetActorLocation();
	const FVector Up = LocalDirection.GetSafeNormal();

	// 位置：星球中心 + 径向方向 × 基础距离 + 高度偏移
	const FVector Position = PlanetLocation
		+ Up * 1000.f
		+ Up * float(LocalHeight);

	// 朝向：上对齐径向
	const FQuat Align = FQuat::FindBetweenNormals(FVector::UpVector, Up);
	OutTransform = FTransform(Align * LocalRotation.Quaternion(), Position, FVector::OneVector);
}
