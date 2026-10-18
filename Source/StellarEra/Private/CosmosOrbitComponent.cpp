// Copyright Voxel Plugin SAS. All Rights Reserved.

#include "CosmosOrbitComponent.h"

#include "CosmosSimulationSubsystem.h"
#include "CosmosTrajectoryComponent.h"
#include "Engine/World.h"

UCosmosOrbitComponent::UCosmosOrbitComponent()
{
	// 本组件自己不 tick：推进由 UCosmosSimulationSubsystem 统一做。
	PrimaryComponentTick.bCanEverTick = false;
}

void UCosmosOrbitComponent::BeginPlay()
{
	Super::BeginPlay();

	// 开局位置 = 速度矢量的锚点。缓存下来，因为「初始位置」指的是天体**出发**
	// 的地方：星球飞走之后箭头要留在原地标出起点。
	if (const AActor* OwnerActor = GetOwner())
	{
		InitialWorldLocation = OwnerActor->GetActorLocation();
	}

	CreateTrajectoryComponent();

	// 组件自己找上门注册，而不是让子系统去遍历全关卡的 Actor：
	// 遍历全世界既慢，又会漏掉运行期生成的 Actor。
	if (const UWorld* OwningWorld = GetWorld())
	{
		if (UCosmosSimulationSubsystem* CosmosSubsystem = OwningWorld->GetSubsystem<UCosmosSimulationSubsystem>())
		{
			CosmosSubsystem->RegisterBody(this);
		}
	}
}

void UCosmosOrbitComponent::CreateTrajectoryComponent()
{
	AActor* OwnerActor = GetOwner();
	if (!OwnerActor)
	{
		return;
	}

	// 无条件创建，即使两个绘制开关都为假 —— 这样运行期把它们打开能立刻生效。
	// 代价是关掉时那一帧仍要走完采样：子系统查询、预测窗口解析、模拟时间读取，
	// 以及拖尾的采样与裁剪（拖尾是真实历史，两种模式下都一直在攒）。省下的是
	// 预测重算与画线 —— 那是开销的大头，见 UCosmosTrajectoryComponent::TickComponent。
	UCosmosTrajectoryComponent* Component = NewObject<UCosmosTrajectoryComponent>(OwnerActor);
	if (!Component)
	{
		return;
	}

	// 先 Initialize 再 RegisterComponent，顺序不能反。RegisterComponent 会**立刻**
	// 触发这个组件的 BeginPlay：AActor::HandleRegisterComponentWithWorld 里算的是
	// bOwnerBeginPlayStarted = HasActorBegunPlay() || IsActorBeginningPlay()，
	// 而 owner 的 BeginPlay 正在跑，IsActorBeginningPlay() 为真。
	// 反过来的话，引擎那次 BeginPlay 会跑在 Orbit / LineBatch 还是 nullptr 的时候 ——
	// 现在不疼（本类没 override BeginPlay），但谁要在 BeginPlay 里用它们就是空指针。
	Component->Initialize(this);
	Component->RegisterComponent();
}

void UCosmosOrbitComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (const UWorld* OwningWorld = GetWorld())
	{
		if (UCosmosSimulationSubsystem* CosmosSubsystem = OwningWorld->GetSubsystem<UCosmosSimulationSubsystem>())
		{
			CosmosSubsystem->UnregisterBody(this);
		}
	}

	Super::EndPlay(EndPlayReason);
}
