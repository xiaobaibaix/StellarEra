// 传送带 —— 实现。

#include "ConveyorBelt.h"

#include "ProcessingBuilding.h"
#include "MiningRig.h"
#include "Engine/StaticMesh.h"
#include "UObject/ConstructorHelpers.h"

AConveyorBelt::AConveyorBelt()
{
	PrimaryActorTick.bCanEverTick = true;

	MeshComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	SetRootComponent(MeshComponent);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(
		TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (CubeMesh.Succeeded())
	{
		MeshComponent->SetStaticMesh(CubeMesh.Object);
	}

	// 传送带沿本地 X 轴方向
	MeshComponent->SetRelativeScale3D(FVector(1.f, 0.6f, 0.15f));

	bCanBeDamaged = false;

	// 碰撞配置：让传送带的自动连接能找到我们。
	MeshComponent->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	MeshComponent->SetCollisionObjectType(ECC_WorldDynamic);
	MeshComponent->SetCollisionResponseToAllChannels(ECR_Overlap);
}

void AConveyorBelt::BeginPlay()
{
	Super::BeginPlay();
}

///////////////////////////////////////////////////////////////////////////////
// 每帧

void AConveyorBelt::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	PullFromUpstream(DeltaSeconds);
	PushToDownstream(DeltaSeconds);

	if (BeltMesh)
	{
		MeshComponent->SetStaticMesh(BeltMesh);
	}
}

void AConveyorBelt::PullFromUpstream(float DeltaSeconds)
{
	if (GetTotalCargo() >= Capacity)
	{
		return;   // 满了，不再取货
	}

	// 每帧最多搬 Capacity/Speed 比例的量，避免小步长下过载
	const int32 Wanted = FMath::Max(1, int32(Speed * DeltaSeconds));

	if (AProcessingBuilding* Source = Cast<AProcessingBuilding>(InputSource.Get()))
	{
		PullFromProcessing(Source, DeltaSeconds);
		return;
	}

	if (AMiningRig* Rig = Cast<AMiningRig>(InputSource.Get()))
	{
		PullFromMiningRig(Rig, DeltaSeconds);
		return;
	}

	// 没有指定上游时，尝试自动找。
	// 只在第一次尝试，之后 InputSource 一旦设上就不再搜，
	// 避免每帧全场扫描。
	if (!InputSource.IsValid())
	{
		AutoConnect();
	}
}

void AConveyorBelt::PullFromProcessing(AProcessingBuilding* Source, float DeltaSeconds)
{
	if (!Source)
	{
		return;
	}

	// 上游的输出槽里有什么就给什么
	const TMap<EItemType, int32>& Outputs = Source->OutputBuffer;
	for (const TPair<EItemType, int32>& Pair : Outputs)
	{
		if (FilterItem != EItemType::None && Pair.Key != FilterItem)
		{
			continue;
		}

		if (GetTotalCargo() >= Capacity)
		{
			break;
		}

		const int32 Want = FMath::Min(
			FMath::Max(1, int32(Speed * DeltaSeconds)),
			FMath::Min(Pair.Value, Capacity - GetTotalCargo()));

		if (Want > 0)
		{
			Source->TakeOutput(Pair.Key, Want);
			Receive(Pair.Key, Want);
		}
	}
}

void AConveyorBelt::PullFromMiningRig(AMiningRig* Source, float DeltaSeconds)
{
	if (!Source || Source->IsStorageFull())
	{
		return;
	}

	// 挖机产什么就取什么。矿机只会有一种矿，所以直接试主矿。
	const EItemType Item = URecipeLibrary::OreToItem(Source->PrimaryOre);
	if (Item == EItemType::None)
	{
		return;
	}

	if (FilterItem != EItemType::None && Item != FilterItem)
	{
		return;
	}

	const int32 Want = FMath::Max(1, int32(Speed * DeltaSeconds));
	const int32 Got = Source->TakeOre(Item, Want);
	if (Got > 0)
	{
		Receive(Item, Got);
	}
}

void AConveyorBelt::PushToDownstream(float DeltaSeconds)
{
	if (GetTotalCargo() == 0)
	{
		return;
	}

	// 没指定下游时自动找
	if (!OutputTarget.IsValid())
	{
		AutoConnect();
	}

	AProcessingBuilding* Target = Cast<AProcessingBuilding>(OutputTarget.Get());
	if (!Target)
	{
		return;
	}

	PushToProcessing(Target, DeltaSeconds);
}

void AConveyorBelt::PushToProcessing(AProcessingBuilding* Target, float DeltaSeconds)
{
	if (!Target)
	{
		return;
	}

	// 看看下游需要什么，只送它要的
	for (const TPair<EItemType, int32>& Pair : Cargo)
	{
		if (FilterItem != EItemType::None && Pair.Key != FilterItem)
		{
			continue;
		}

		// 下游要多少
		const int32 Needed = Target->Recipe.GetInputCount(Pair.Key);
		if (Needed <= 0)
		{
			continue;   // 这台机器不需要这种物品
		}

		// 下游当前有多少
		const int32 Has = Target->GetInputCount(Pair.Key);
		if (Has >= Needed)
		{
			continue;   // 已经够了
		}

		const int32 Want = FMath::Min(
			FMath::Max(1, int32(Speed * DeltaSeconds)),
			FMath::Min(Pair.Value, Needed - Has));

		if (Want <= 0)
		{
			continue;
		}

		// 尝试投入下游
		const int32 Added = Target->TryAddInput(Pair.Key, Want);
		if (Added > 0)
		{
			// 从自己这里扣掉
			int32& Have = Cargo.FindRef(Pair.Key);
			Have -= Added;
			if (Have <= 0)
			{
				Cargo.Remove(Pair.Key);
			}
		}
	}
}

///////////////////////////////////////////////////////////////////////////////
// 自身接口

int32 AConveyorBelt::Receive(EItemType Item, int32 Count)
{
	if (Item == EItemType::None || Count <= 0)
	{
		return 0;
	}

	if (FilterItem != EItemType::None && Item != FilterItem)
	{
		return 0;
	}

	const int32 Space = Capacity - GetTotalCargo();
	if (Space <= 0)
	{
		return 0;
	}

	const int32 Added = FMath::Min(Space, Count);
	Cargo.FindOrAdd(Item) += Added;
	return Added;
}

int32 AConveyorBelt::GetTotalCargo() const
{
	int32 Total = 0;
	for (const TPair<EItemType, int32>& Pair : Cargo)
	{
		Total += Pair.Value;
	}
	return Total;
}

bool AConveyorBelt::IsEmpty() const
{
	return Cargo.Num() == 0;
}

///////////////////////////////////////////////////////////////////////////////
// 自动连接

void AConveyorBelt::AutoConnect()
{
	// 用 OverlapMultiByChannel 找周围的传送带与设备。
	// 第一版做得比较简单：找到最近的传送带，把首尾接上。
	// 这样玩家摆一排带子就能自动串起来。
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	TArray<AActor*> Overlapping;
	const FVector HalfExtent(FVector(AutoConnectRadius, AutoConnectRadius, AutoConnectRadius));

	World->OverlapMultiByChannel(
		Overlapping,
		GetActorLocation(),
		FQuat::Identity,
		// 用 ObjectType 而不是 TraceChannel：我们要找的是「所有碰撞体」，
		// 不关心它们是什么 trace 类型。传送带默认是 WorldDynamic，
		// 用 ECC_WorldStatic 会一个都找不到。
		ECC_WorldDynamic,
		FCollisionShape::MakeBox(HalfExtent),
		// bIgnoreThis = true → 不把自己算进结果
		FCollisionQueryParams Params(FName(TEXT("ConveyorAutoConnect")), false, this));

	double BestUpDist = TNumericLimits<double>::Max();
	double BestDownDist = TNumericLimits<double>::Max();

	for (AActor* Candidate : Overlapping)
	{
		if (!Candidate || Candidate == this)
		{
			continue;
		}

		// 上下游只认传送带与生产设备
		const bool bIsBelt = Cast<AConveyorBelt>(Candidate) != nullptr;
		const bool bIsProducer = Cast<AProcessingBuilding>(Candidate) != nullptr;
		const bool bIsRig = Cast<AMiningRig>(Candidate) != nullptr;

		if (!bIsBelt && !bIsProducer && !bIsRig)
		{
			continue;
		}

		const double Dist = double(FVector::Dist(Candidate->GetActorLocation(), GetActorLocation()));
		if (Dist >= AutoConnectRadius)
		{
			continue;
		}

		// 对方已经有连接就跳过
		if (AConveyorBelt* OtherBelt = Cast<AConveyorBelt>(Candidate))
		{
			if (OtherBelt->InputSource.IsValid())
			{
				// 对方有上游，我们不能当它的上游
				continue;
			}
			if (BestDownDist > Dist && !OtherBelt->OutputTarget.IsValid())
			{
				BestDownDist = Dist;
				OutputTarget = Candidate;
			}
		}
		else
		{
			// 生产设备和矿机只能当上游
			if (!InputSource.IsValid() && Dist < BestUpDist)
			{
				BestUpDist = Dist;
				InputSource = Candidate;
			}
		}
	}
}
