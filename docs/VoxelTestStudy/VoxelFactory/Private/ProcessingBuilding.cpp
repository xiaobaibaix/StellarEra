// 通用加工机 —— 实现。

#include "ProcessingBuilding.h"

#include "PowerGrid.h"
#include "Engine/StaticMesh.h"
#include "UObject/ConstructorHelpers.h"

AProcessingBuilding::AProcessingBuilding()
{
	PrimaryActorTick.bCanEverTick = true;

	MeshComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	SetRootComponent(MeshComponent);

	// 进度条：用一个压扁的立方体，靠缩放表现进度
	ProgressBarMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ProgressBar"));
	ProgressBarMesh->SetupAttachment(MeshComponent);
	ProgressBarMesh->SetRelativeLocation(FVector(0.f, 0.f, 150.f));
	ProgressBarMesh->SetVisibility(false);

	// 占位网格：全部用引擎自带的基础形状，不需要任何美术资源。
	// 换一个对象就变一个形状，靠缩放调整尺寸。
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(
		TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereMesh(
		TEXT("/Engine/BasicShapes/Sphere.Sphere"));

	if (CubeMesh.Succeeded())
	{
		MeshComponent->SetStaticMesh(CubeMesh.Object);
	}
	if (SphereMesh.Succeeded())
	{
		// 进度条用球，压扁后当指示灯
		ProgressBarMesh->SetStaticMesh(SphereMesh.Object);
	}

	MeshComponent->SetRelativeScale3D(FVector(2.f, 2.f, 1.5f));

	bCanBeDamaged = false;

	// 碰撞配置：让传送带的自动连接能找到我们。
	MeshComponent->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	MeshComponent->SetCollisionObjectType(ECC_WorldDynamic);
	MeshComponent->SetCollisionResponseToAllChannels(ECR_Overlap);
}

void AProcessingBuilding::BeginPlay()
{
	Super::BeginPlay();

	// 从配方同步一次功耗与加工时长
	if (Recipe.PowerDraw > 0.f)
	{
		PowerDraw = Recipe.PowerDraw;
	}
	if (Recipe.CraftTime > 0.f)
	{
		CraftTime = Recipe.CraftTime;
	}

	// 提前创建动态材质实例，避免每帧重建
	CachedMID = MeshComponent->CreateAndSetMaterialInstanceDynamic(0);

	// 按建筑分类换占位形状。
	// 没有美术资源时也能一眼看出是什么厂：
	//   冶炼 = 扁平方块（像个炉子）
	//   零件 = 立方体
	//   组装 = 竖长方块
	//   发电 = 圆柱
	ApplyPlaceholderMesh();

	// 检查槽位容量是否够配方用
	const int32 NeededSlots = Recipe.Inputs.Num();
	if (NeededSlots > InputSlotCapacity)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[Factory] %s 配方需要 %d 个输入槽，但配置只有 %d 个"),
			*GetName(), NeededSlots, InputSlotCapacity);
	}

	UpdateStatusLight();
}

void AProcessingBuilding::ApplyPlaceholderMesh()
{
	// 如果玩家已经指定了自己的网格，就用他的
	if (BuildingMesh)
	{
		MeshComponent->SetStaticMesh(BuildingMesh);
		return;
	}

	// 否则按分类挑一个基础形状当占位
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeFinder(
		TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereFinder(
		TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderFinder(
		TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> ConeFinder(
		TEXT("/Engine/BasicShapes/Cone.Cone"));

	switch (Recipe.Category)
	{
	case EBuildingCategory::Smelter:
		// 扁平方块，像炉子
		if (CubeFinder.Succeeded()) { MeshComponent->SetStaticMesh(CubeFinder.Object); }
		MeshComponent->SetRelativeScale3D(FVector(2.5f, 2.5f, 1.f));
		break;

	case EBuildingCategory::Parts:
		// 标准立方体
		if (CubeFinder.Succeeded()) { MeshComponent->SetStaticMesh(CubeFinder.Object); }
		MeshComponent->SetRelativeScale3D(FVector(1.6f, 1.6f, 1.6f));
		break;

	case EBuildingCategory::Assembly:
		// 竖长方块
		if (CubeFinder.Succeeded()) { MeshComponent->SetStaticMesh(CubeFinder.Object); }
		MeshComponent->SetRelativeScale3D(FVector(2.f, 2.f, 3.f));
		break;

	case EBuildingCategory::Power:
		// 圆柱
		if (CylinderFinder.Succeeded()) { MeshComponent->SetStaticMesh(CylinderFinder.Object); }
		MeshComponent->SetRelativeScale3D(FVector(1.5f, 1.5f, 3.f));
		break;

	default:
		if (SphereFinder.Succeeded()) { MeshComponent->SetStaticMesh(SphereFinder.Object); }
		MeshComponent->SetRelativeScale3D(FVector(2.f));
		break;
	}

	// 让锥体也加载进来，避免未使用警告
	(void)ConeFinder;
}

///////////////////////////////////////////////////////////////////////////////
// 每帧逻辑

void AProcessingBuilding::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	UpdatePower(DeltaSeconds);

	if (bCrafting)
	{
		// 加工中：累积进度。
		// 速度受供电满足率影响——电力紧张时变慢，而不是停下。
		CraftProgress += DeltaSeconds * PowerSatisfaction;

		if (CraftProgress >= CraftTime)
		{
			FinishCrafting();
		}
	}
	else
	{
		// 空闲时自动检查能否开工，让产线能无人值守运行
		EvaluateCrafting(DeltaSeconds);
	}

	UpdateStatusLight();
}

void AProcessingBuilding::UpdatePower(float DeltaSeconds)
{
	(void)DeltaSeconds;

	if (bConnectedToGrid && ConnectedPlant.IsValid())
	{
		// 供电率由 APowerPlant 每帧写入 PowerSatisfaction。
		// 这里只处理「电厂被拆了」的情况。
		PowerSatisfaction = 0.f;
		return;
	}

	// 未接入电网：按StandalonePowerRatio。
	// 默认是 0，所以忘接电的机器会完全不动 —— 这是明显的错误提示，
	// 比默默满速运行骗玩家要好。
	PowerSatisfaction = StandalonePowerRatio;
}

///////////////////////////////////////////////////////////////////////////////
// 电网接入

void AProcessingBuilding::ConnectToGrid(AActor* Plant)
{
	if (!Plant)
	{
		return;
	}

	ConnectedPlant = Plant;
	bConnectedToGrid = true;

	// 立即取一次当前供电率，不用等电厂的下一帧。
	// 这里的初值只是过渡，真正的分配由电厂每帧统一算。
	if (const APowerPlant* PowerPlant = Cast<APowerPlant>(Plant))
	{
		PowerSatisfaction = FMath::Clamp(
			PowerPlant->GetActualOutput() / FMath::Max(PowerDraw, 0.001f), 0.f, 1.f);
	}
}

void AProcessingBuilding::DisconnectFromGrid()
{
	ConnectedPlant.Reset();
	bConnectedToGrid = false;
	PowerSatisfaction = StandalonePowerRatio;
}

void AProcessingBuilding::EvaluateCrafting(float DeltaSeconds)
{
	(void)DeltaSeconds;

	// 无配方不工作
	if (Recipe.Inputs.Num() == 0)
	{
		return;
	}

	if (!HasAllInputs())
	{
		return;
	}

	// 输出槽满就别开工——否则扣了料却放不下产物
	if (!HasOutputSpace())
	{
		return;
	}

	TryStartCrafting();
}

///////////////////////////////////////////////////////////////////////////////
// 加工控制

bool AProcessingBuilding::TryStartCrafting()
{
	if (bCrafting)
	{
		return false;
	}

	if (Recipe.Inputs.Num() == 0)
	{
		return false;
	}

	if (!HasAllInputs() || !HasOutputSpace())
	{
		return false;
	}

	// 没电不开工。已在加工的不受影响（会变慢）。
	if (PowerSatisfaction <= 0.001f)
	{
		return false;
	}

	// 扣料
	for (const FRecipeIO& Input : Recipe.Inputs)
	{
		int32& Have = InputBuffer.FindOrAdd(Input.Item);
		Have -= Input.Count;
		if (Have <= 0)
		{
			InputBuffer.Remove(Input.Item);
		}
	}

	CraftTime = FMath::Max(Recipe.CraftTime, 0.01f);
	CraftProgress = 0.f;
	bCrafting = true;

	return true;
}

void AProcessingBuilding::FinishCrafting()
{
	bCrafting = false;
	CraftProgress = 0.f;

	// 组装类配方：生成建筑 Actor
	if (Recipe.bProducesActor)
	{
		SpawnProducedActor();
		return;
	}

	// 普通配方：产物进输出槽
	for (const FRecipeIO& Output : Recipe.Outputs)
	{
		if (Output.Item == EItemType::None || Output.Count <= 0)
		{
			continue;
		}

		int32& Have = OutputBuffer.FindOrAdd(Output.Item);
		Have += Output.Count;
	}
}

void AProcessingBuilding::SpawnProducedActor()
{
	// 组装产物不产生可堆叠物品，而是生成一个可放置的建筑 Actor。
	if (!Recipe.ProducedActor)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[Factory] %s 的配方 %s 标记了 Produces Actor 但没指定生成什么"),
			*GetName(), *Recipe.Id.ToString());
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// 放置位置：默认在本机旁边。
	// TODO: 建造交互做好后，应该改成「玩家放置的位置」——
	//       比如放置预览 Actor 记录的位置，或者玩家摄像机前方。
	const FVector SpawnLocation = GetActorLocation() + FVector(200.f, 0.f, 0.f);
	const FRotator SpawnRotation = GetActorRotation();

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride =
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	AActor* NewBuilding = World->SpawnActor<AActor>(
		Recipe.ProducedActor, SpawnLocation, SpawnRotation, SpawnParams);

	if (!NewBuilding)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[Factory] %s 生成 %s 失败"),
			*GetName(), *Recipe.DisplayName.ToString());
		return;
	}

	UE_LOG(LogTemp, Log,
		TEXT("[Factory] %s 完成了组装：%s → %s"),
		*GetName(), *Recipe.DisplayName.ToString(), *NewBuilding->GetName());

	// TODO: 如果新建筑需要锚定到星球（例如放在星球表面），
	//       这里要设置它的 FPlanetAnchor。
	//       当前生成在加工机旁边，等锚定系统接过来再改。
}

///////////////////////////////////////////////////////////////////////////////
// 槽位操作

int32 AProcessingBuilding::GetInputCount(EItemType Item) const
{
	const int32* Found = InputBuffer.Find(Item);
	return Found ? *Found : 0;
}

int32 AProcessingBuilding::GetOutputCount(EItemType Item) const
{
	const int32* Found = OutputBuffer.Find(Item);
	return Found ? *Found : 0;
}

bool AProcessingBuilding::HasAllInputs() const
{
	for (const FRecipeIO& Input : Recipe.Inputs)
	{
		if (GetInputCount(Input.Item) < Input.Count)
		{
			return false;
		}
	}
	return true;
}

bool AProcessingBuilding::HasOutputSpace() const
{
	// 组装类配方不产物品，永远有空间
	if (Recipe.bProducesActor)
	{
		return true;
	}

	for (const FRecipeIO& Output : Recipe.Outputs)
	{
		if (Output.Item == EItemType::None || Output.Count <= 0)
		{
			continue;
		}

		const int32 Have = GetOutputCount(Output.Item);

		// 该物品还没占槽位
		if (Have == 0 && OutputBuffer.Num() >= InputSlotCapacity)
		{
			return false;
		}

		// 已有该物品，但堆满了
		if (Have > 0 && Have + Output.Count > SlotCapacity)
		{
			return false;
		}
	}

	return true;
}

int32 AProcessingBuilding::TryAddInput(EItemType Item, int32 Count)
{
	if (Item == EItemType::None || Count <= 0)
	{
		return 0;
	}

	// 配方不需要这种物品 → 不收
	bool bNeeded = false;
	for (const FRecipeIO& Input : Recipe.Inputs)
	{
		if (Input.Item == Item) { bNeeded = true; break; }
	}
	if (!bNeeded)
	{
		return 0;
	}

	// 已有该物品 → 检查能否堆满
	const int32 Have = GetInputCount(Item);
	if (Have > 0)
	{
		const int32 Space = SlotCapacity - Have;
		const int32 Added = FMath::Min(Space, Count);
		InputBuffer.FindOrAdd(Item) = Have + Added;
		return Added;
	}

	// 新物品 → 槽位满了吗
	if (InputBuffer.Num() >= InputSlotCapacity)
	{
		return 0;
	}

	const int32 Added = FMath::Min(SlotCapacity, Count);
	InputBuffer.FindOrAdd(Item) = Added;
	return Added;
}

int32 AProcessingBuilding::TakeOutput(EItemType Item, int32 Count)
{
	if (Item == EItemType::None || Count <= 0)
	{
		return 0;
	}

	int32* Found = OutputBuffer.Find(Item);
	if (!Found)
	{
		return 0;
	}

	const int32 Taken = FMath::Min(*Found, Count);
	*Found -= Taken;

	if (*Found <= 0)
	{
		OutputBuffer.Remove(Item);
	}

	return Taken;
}

void AProcessingBuilding::ClearBuffers()
{
	InputBuffer.Reset();
	OutputBuffer.Reset();

	// 中断加工并把料退回来？—— 不退。
	// 理由：已开始加工的料视为已消耗，这是常见做法，
	// 也避免「拆电闸骗进度」这类漏洞。
	bCrafting = false;
	CraftProgress = 0.f;
}

///////////////////////////////////////////////////////////////////////////////
// 外观

void AProcessingBuilding::UpdateStatusLight()
{
	// 视觉反馈：绿=在工作，黄=缺料/没空间，灰=空闲
	// 这里用网格缩放做简单示意，正式版应该是材质自发光
	if (!ProgressBarMesh)
	{
		return;
	}

	if (bCrafting)
	{
		const float T = GetProgress();
		ProgressBarMesh->SetVisibility(true);
		ProgressBarMesh->SetRelativeScale3D(
			FVector(FMath::Max(T, 0.01f) * 2.f, FMath::Max(T, 0.01f) * 2.f, 0.4f));
	}
	else if (Recipe.Inputs.Num() > 0 && !HasAllInputs())
	{
		// 缺料：显示一个扁平的黄色条
		ProgressBarMesh->SetVisibility(true);
		ProgressBarMesh->SetRelativeScale3D(FVector(0.3f, 0.3f, 0.2f));
	}
	else
	{
		ProgressBarMesh->SetVisibility(false);
	}

	if (MeshComponent)
	{
		// 注意：这里不再 SetStaticMesh。
		// 网格由 ApplyPlaceholderMesh 在 BeginPlay 时设一次就够了，
		// 每帧重设会浪费资源，而且会覆盖占位形状。
		// 用缓存的 MID，只改参数不重建
		if (CachedMID)
		{
			const FLinearColor Color = bCrafting ? CraftingColor
				: (Recipe.Inputs.Num() > 0 && !HasAllInputs() ? StarvedColor : IdleColor);
			CachedMID->SetVectorParameterValue(TEXT("Color"), Color);
		}
	}
}
