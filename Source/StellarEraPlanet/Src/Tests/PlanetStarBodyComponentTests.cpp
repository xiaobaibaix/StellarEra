// 恒星组件的两件事：找到离给定点最近的那颗，以及恒星方向解析。

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Engine/DirectionalLight.h"
#include "Components/SceneComponent.h"
#include "StarBodyComponent.h"
#include "PlanetAtmosphereComponent.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	/** 取编辑器世界。不能写 GEngine->GetEditorWorldContext() —— 那是 UEditorEngine 的
	 *  方法，本模块不带 UnrealEd 依赖；按 WorldType 扫 UEngine 的世界上下文等价。 */
	UWorld* FindStarBodyEditorWorld()
	{
		if (!GEngine)
		{
			return nullptr;
		}
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			if (Context.WorldType == EWorldType::Editor && Context.World())
			{
				return Context.World();
			}
		}
		return nullptr;
	}

	/** 把 world 里的一个 Actor 挂好根组件并摆到 Location。返回 Actor 供调用方销毁。 */
	AActor* SpawnRootedActorAt(UWorld* World, const FVector& Location)
	{
		AActor* Actor = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform(Location));
		if (!Actor)
		{
			return nullptr;
		}

		// **必须挂根组件**：SpawnActor 出来的裸 AActor 没有根组件，SetActorLocation
		// 是空操作、GetActorLocation() 恒为零（见 CLAUDE.md「测试裸 Actor 的位置恒为 0」）。
		USceneComponent* Root = NewObject<USceneComponent>(Actor);
		Actor->SetRootComponent(Root);
		Root->RegisterComponent();
		Actor->SetActorLocation(Location);
		return Actor;
	}

	/** 在世界里放一个挂好 UStarBodyComponent 的 Actor，摆到 Location。 */
	AActor* SpawnStarActorAt(UWorld* World, const FVector& Location, UStarBodyComponent*& OutStar)
	{
		AActor* Actor = SpawnRootedActorAt(World, Location);
		if (!Actor)
		{
			OutStar = nullptr;
			return nullptr;
		}

		// 不要调 AddInstanceComponent：它把 CreationMethod 改成 Instance，而 OnRegister
		// 对 Instance 组件**故意跳过** AddOwnedComponent（要等 owner 初始化）。这里是
		// SpawnActor 之后才挂组件、初始化已过，组件永远进不了 OwnedComponents，而
		// FindComponentByClass 只遍历它 —— FindNearest 会一直返回 nullptr。
		UStarBodyComponent* Star = NewObject<UStarBodyComponent>(Actor);
		Star->RegisterComponent();

		OutStar = Star;
		return Actor;
	}
} // namespace

// 找最近的那颗。位置刻意放得离关卡里的天体很远，避免依赖关卡内容。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraStarBodyFindNearestTest,
	"StellarEra.Planet.Atmosphere.StarBody.FindNearest",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FStellarEraStarBodyFindNearestTest::RunTest(const FString& Parameters)
{
	TestNull(TEXT("空世界返回 nullptr"), UStarBodyComponent::FindNearest(nullptr, FVector::ZeroVector));

	UWorld* World = FindStarBodyEditorWorld();
	if (!TestNotNull(TEXT("编辑器世界"), World))
	{
		return false;
	}

	const FVector NearLocation(1.0e9, 0.0, 0.0);
	const FVector FarLocation(1.0e9 + 10000.0, 0.0, 0.0);

	UStarBodyComponent* NearStar = nullptr;
	UStarBodyComponent* FarStar = nullptr;
	AActor* NearActor = SpawnStarActorAt(World, NearLocation, NearStar);
	AActor* FarActor = SpawnStarActorAt(World, FarLocation, FarStar);

	bool bOk = true;
	bOk &= TestNotNull(TEXT("近处恒星组件"), NearStar);
	bOk &= TestNotNull(TEXT("远处恒星组件"), FarStar);

	if (bOk)
	{
		// 站在近处那颗旁边探，必须返回近处这颗。必须用 TestSamePtr：`TestSame` 比的是
		// 两个**参数自己的地址**（&Actual != &Expected，AutomationTest.h:2549），传两个
		// 不同指针变量进去永远失败。
		UStarBodyComponent* Found = UStarBodyComponent::FindNearest(World, NearLocation + FVector(200.0, 0.0, 0.0));
		TestSamePtr(TEXT("离近处恒星 200cm 时找到它"), Found, NearStar);
		TestEqual(TEXT("近处恒星的位置"), NearStar->GetOwner()->GetActorLocation(), NearLocation);

		Found = UStarBodyComponent::FindNearest(World, FarLocation - FVector(200.0, 0.0, 0.0));
		TestSamePtr(TEXT("离远处恒星 200cm 时找到它"), Found, FarStar);
	}

	if (NearActor)
	{
		NearActor->Destroy();
	}
	if (FarActor)
	{
		FarActor->Destroy();
	}
	return true;
}

// 太阳方向解析。纯函数：恒星只贡献一个位置，不碰材质、不碰 N 体。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraSunDirectionComputeTest,
	"StellarEra.Planet.Atmosphere.SunDirection.Compute",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FStellarEraSunDirectionComputeTest::RunTest(const FString& Parameters)
{
	UWorld* World = FindStarBodyEditorWorld();
	if (!TestNotNull(TEXT("编辑器世界"), World))
	{
		return false;
	}

	// 1) 有恒星：方向 = 从探针点指向恒星的单位向量，与回退的灯无关。
	AActor* Star = SpawnRootedActorAt(World, FVector(1000.0, 0.0, 0.0));
	if (!TestNotNull(TEXT("恒星 Actor"), Star))
	{
		return false;
	}

	const FVector Direction = UPlanetAtmosphereComponent::ComputeSunDirection(
		Star, nullptr, FVector::ZeroVector);
	TestEqual(TEXT("恒星在 +X：方向应为 +X"), Direction, FVector(1.0, 0.0, 0.0), 1e-6);

	const FVector Offset = UPlanetAtmosphereComponent::ComputeSunDirection(
		Star, nullptr, FVector(1000.0, 1000.0, 0.0));
	TestEqual(TEXT("探针在恒星 +Y 侧：方向指向 −Y"), Offset, FVector(0.0, -1.0, 0.0), 1e-6);

	// 2) 只有灯：沿用旧行为 —— 灯 forward 是光传播方向，太阳在反向。期望值**不能**
	// 从 spawn 的 FRotator 推：ADirectionalLight 的构造函数给灯组件写死相对旋转
	// pitch −46°（Light.cpp:239），GetActorForwardVector 返回灯组件的世界朝向，
	// 传 (−90,0,0) 实际是 −136°。故拿灯自己的 forward 当参照物。
	ADirectionalLight* Light = World->SpawnActor<ADirectionalLight>(
		FVector(0.0, 0.0, 500.0), FRotator(-90.0, 0.0, 0.0));
	if (!TestNotNull(TEXT("方向光"), Light))
	{
		Star->Destroy();
		return false;
	}

	const FVector LightForward = Light->GetActorForwardVector();
	const FVector FromLight = UPlanetAtmosphereComponent::ComputeSunDirection(
		nullptr, Light, FVector::ZeroVector);
	TestEqual(TEXT("只有灯时：方向是灯 forward 的反向"), FromLight, -LightForward, 1e-6);
	TestTrue(TEXT("不是灯 forward 本身（符号不能写反）"),
		(FromLight | LightForward) < -0.99);

	TestTrue(TEXT("没有恒星也没有灯 = 零向量"),
		UPlanetAtmosphereComponent::ComputeSunDirection(nullptr, nullptr, FVector::ZeroVector).IsNearlyZero());

	Light->Destroy();
	Star->Destroy();
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
