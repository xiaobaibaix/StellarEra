// 电网 —— 实现。

#include "PowerGrid.h"

#include "ProcessingBuilding.h"
#include "MiningRig.h"
#include "Engine/StaticMesh.h"
#include "UObject/ConstructorHelpers.h"

APowerPlant::APowerPlant()
{
	PrimaryActorTick.bCanEverTick = true;

	MeshComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	SetRootComponent(MeshComponent);

	StatusLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("StatusLight"));
	StatusLight->SetupAttachment(MeshComponent);
	StatusLight->SetIntensity(2000.f);
	StatusLight->SetAttenuationRadius(1500.f);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderMesh(
		TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	if (CylinderMesh.Succeeded())
	{
		MeshComponent->SetStaticMesh(CylinderMesh.Object);
	}

	MeshComponent->SetRelativeScale3D(FVector(1.5f, 1.5f, 3.f));

	bCanBeDamaged = false;

	// 碰撞：让发电机能被其他系统查到
	MeshComponent->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	MeshComponent->SetCollisionObjectType(ECC_WorldDynamic);
	MeshComponent->SetCollisionResponseToAllChannels(ECR_Overlap);
}

void APowerPlant::BeginPlay()
{
	Super::BeginPlay();

	// 提前创建动态材质实例
	CachedMID = MeshComponent->CreateAndSetMaterialInstanceDynamic(0);

	// 开局就扫一次，不用等第一个 Tick
	RefreshConnections();
	UpdateVisual();
}

///////////////////////////////////////////////////////////////////////////////
// 每帧

void APowerPlant::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	ConsumeFuel(DeltaSeconds);

	// 定期重扫连接。不用每帧扫全场——设备位置不会每帧变。
	RescanTimer -= DeltaSeconds;
	if (RescanTimer <= 0.f)
	{
		RescanTimer = 1.0f;
		RefreshConnections();
	}

	DistributePower();
	UpdateVisual();
}

void APowerPlant::RefreshConnections()
{
	ConnectedBuildings.Reset();

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// 扫描供电范围内的所有加工机
	TArray<AActor*> Overlapping;
	const FVector HalfExtent(SupplyRadius, SupplyRadius, SupplyRadius);
	FCollisionQueryParams Params(FName(TEXT("PowerGridScan")), false, this);

	World->OverlapMultiByChannel(
		Overlapping,
		GetActorLocation(),
		FQuat::Identity,
		ECC_WorldDynamic,
		FCollisionShape::MakeBox(HalfExtent),
		Params);

	TotalDemand = 0.f;

	for (AActor* Candidate : Overlapping)
	{
		AProcessingBuilding* Building = Cast<AProcessingBuilding>(Candidate);
		if (Building && Building != this)
		{
			ConnectedBuildings.Add(Building);
			TotalDemand += Building->PowerDraw;

			// 通知它已接入。用 AddUnique 防止重复连接。
			if (!Building->bConnectedToGrid)
			{
				Building->ConnectToGrid(this);
			}
		}
	}

	// 把已经移出供电范围的设备断开。
	// 用移除后残留的弱指针反查，比重新扫全场省。
	for (int32 i = ConnectedBuildings.Num() - 1; i >= 0; --i)
	{
		AProcessingBuilding* Building = ConnectedBuildings[i].Get();
		const bool bStillInRange =
			Building &&
			FVector::DistSquared(Building->GetActorLocation(), GetActorLocation())
				<= FMath::Square(SupplyRadius);

		if (!bStillInRange)
		{
			if (Building && Building->bConnectedToGrid)
			{
				Building->DisconnectFromGrid();
			}
			ConnectedBuildings.RemoveAtSwap(i);
		}
	}
}

void APowerPlant::DistributePower()
{
	// 先算实际输出
	ActualOutput = bOnline ? MaxOutput : 0.f;

	// 燃料不足时按比例降额
	if (bRequiresFuel && FuelPerSecond > 0.f)
	{
		const int32* Have = FuelBuffer.Find(FuelItem);
		if (!Have || *Have <= 0)
		{
			ActualOutput = 0.f;   // 没燃料就完全停机
		}
	}

	// 分配：所有设备共享同一个满足率。
	//
	// 这里刻意用「统一比例」而不是「优先级」——
	// 优先级会让玩家要算电力配额，多了一套 UI 和决策成本。
	// 统一比例更直观：电不够就大家一起变慢。
	const float Satisfaction = (TotalDemand > 0.001f)
		? FMath::Clamp(ActualOutput / TotalDemand, 0.f, 1.f)
		: 1.f;

	for (const TWeakObjectPtr<AProcessingBuilding>& Weak : ConnectedBuildings)
	{
		if (AProcessingBuilding* Building = Weak.Get())
		{
			Building->PowerSatisfaction = Satisfaction;
		}
	}
}

void APowerPlant::ConsumeFuel(float DeltaSeconds)
{
	if (!bRequiresFuel || !bOnline || FuelPerSecond <= 0.f)
	{
		return;
	}

	const float Want = FuelPerSecond * DeltaSeconds;
	int32* Have = FuelBuffer.Find(FuelItem);
	if (!Have || *Have <= 0)
	{
		return;
	}

	// 燃料按整数消耗，不足一单位的部分累积到下次
	const int32 Consumed = FMath::FloorToInt(Want);
	if (Consumed > 0)
	{
		*Have -= Consumed;
		if (*Have <= 0)
		{
			FuelBuffer.Remove(FuelItem);
		}
	}
}

void APowerPlant::UpdateVisual()
{
	// 网格在 BeginPlay 设一次就好，不要每帧重设
	if (MeshComponent && CachedMID)
	{
		// 超载时偏红，正常是原色，离线是灰
		const float Load = GetLoadFactor();
		FLinearColor Color = OnlineColor;
		if (Load > 1.f)
		{
			Color = FLinearColor(1.f, 0.3f, 0.2f);   // 超载：红
		}
		else if (!bOnline)
		{
			Color = OfflineColor;
		}

		CachedMID->SetVectorParameterValue(TEXT("Color"), Color);
	}

	if (StatusLight)
	{
		StatusLight->SetLightColor(ActualOutput > 0.f ? OnlineColor : OfflineColor);
	}
}
