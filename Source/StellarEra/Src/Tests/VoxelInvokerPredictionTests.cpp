// 体素调用器的预测速度必须是**相对参考行星**的。插件默认读 GetOwner()->GetVelocity()，
// 而飞船把它覆写成了求解器的绝对速度 —— 行星公转 6 km/s 时，落地静止的飞船预测点也会
// 漂到船头 181 米外，细分级气泡看着不在船心。

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Engine/Engine.h"
#include "Engine/World.h"

#include "VoxelComponents/VoxelInvokerComponent.h"
#include "AlwaysLocalVoxelInvokerComponent.h"
#include "SpaceshipPawn.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraSpaceshipInvokerPredictionTest,
	"StellarEra.Planet.InvokerPrediction.RelativeVelocity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraSpaceshipInvokerPredictionTest::RunTest(const FString& Parameters)
{
	// 一次性编辑器世界，照抄 VoxelViewpointInvokerTests.cpp。
	UWorld* World = UWorld::CreateWorld(EWorldType::Editor, false);
	if (!TestNotNull(TEXT("测试世界"), World))
	{
		return false;
	}

	FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Editor);
	Context.SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());
	ON_SCOPE_EXIT
	{
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
	};

	ASpaceshipPawn* Ship = World->SpawnActor<ASpaceshipPawn>(ASpaceshipPawn::StaticClass(), FTransform::Identity);
	if (!TestNotNull(TEXT("飞船"), Ship))
	{
		return false;
	}

	TArray<UAlwaysLocalVoxelInvokerWithPredictionComponent*> Invokers;
	Ship->GetComponents<UAlwaysLocalVoxelInvokerWithPredictionComponent>(Invokers);
	UAlwaysLocalVoxelInvokerWithPredictionComponent* const* Found =
		Invokers.FindByPredicate([](const UAlwaysLocalVoxelInvokerWithPredictionComponent* Invoker)
		{
			return Invoker->GetFName() == TEXT("VoxelInvoker");
		});
	if (!Found || !TestNotNull(TEXT("细气泡 VoxelInvoker"), *Found))
	{
		return false;
	}

	UAlwaysLocalVoxelInvokerWithPredictionComponent* const Invoker = *Found;
	Invoker->PredictionTime = 1.f;

	// 绝对 600 m/s、行星 590 m/s：相对 10 m/s。
	Ship->SetCosmosVelocity(FVector(0.f, 60000.f, 0.f));
	Ship->SetReferencePlanetVelocity(FVector(0.f, 59000.f, 0.f));

	const FVector Base = Invoker->GetComponentLocation();
	TestTrue(TEXT("预测点 = 位置 + 相对速度 × 时间（10 m/s → 1000 cm）"),
		Invoker->GetInvokerGlobalPosition().Equals(Base + FVector(0.f, 1000.f, 0.f), 1.0));

	// 没有参考行星时退回插件原行为（Actor 绝对速度），不是干脆不预测。
	Ship->SetReferencePlanetVelocity(FVector::ZeroVector);
	TestTrue(TEXT("无参考行星 → 绝对速度（600 m/s → 60000 cm）"),
		Invoker->GetInvokerGlobalPosition().Equals(Base + FVector(0.f, 60000.f, 0.f), 1.0));

	// 落地：相对速度 0 → 预测点就压在船身上，气泡居中。
	Ship->SetReferencePlanetVelocity(Ship->GetVelocity());
	TestTrue(TEXT("落地相对 0 → 预测点不偏移"),
		Invoker->GetInvokerGlobalPosition().Equals(Base, 1.0));

	Invoker->bEnablePrediction = false;
	TestTrue(TEXT("关掉预测 → 组件位置"),
		Invoker->GetInvokerGlobalPosition().Equals(Base, 1.0));

	return true;
}

#endif
