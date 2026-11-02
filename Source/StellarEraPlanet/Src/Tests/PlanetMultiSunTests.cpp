#include "CoreMinimal.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInterface.h"
#include "Misc/AutomationTest.h"
#include "PlanetAtmosphereComponent.h"
#include "PlanetCloudParams.h"
#include "StarBodyComponent.h"

#if WITH_DEV_AUTOMATION_TESTS

// 具名命名空间而不是匿名命名空间：UBT 默认开 unity build，
// 匿名命名空间在 unity blob 里其实是同一个作用域，会和别的测试文件撞名。
namespace StellarEraMultiSunTests
{
	UWorld* GetTestWorld()
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

	/** 裸 spawn 的 Actor 没有根组件，GetActorLocation() 恒为 0（踩过），所以必须补一个。 */
	AActor* SpawnRootedActor(UWorld* World, const FVector& Location, TArray<AActor*>& Spawned)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AActor* Actor = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform(Location), Params);
		if (!Actor)
		{
			return nullptr;
		}
		USceneComponent* Root = NewObject<USceneComponent>(Actor);
		Root->RegisterComponent();
		Actor->SetRootComponent(Root);
		Actor->SetActorLocation(Location);
		Spawned.Add(Actor);
		return Actor;
	}

	/** 根组件是 StaticMeshComponent，用来把恒星材质实例塞进材质槽 0。 */
	AActor* SpawnMeshActor(UWorld* World, const FVector& Location, const TCHAR* MaterialPath,
		TArray<AActor*>& Spawned)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AActor* Actor = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform(Location), Params);
		if (!Actor)
		{
			return nullptr;
		}
		UStaticMeshComponent* Mesh = NewObject<UStaticMeshComponent>(Actor);
		Mesh->RegisterComponent();
		Actor->SetRootComponent(Mesh);
		Actor->SetActorLocation(Location);
		if (UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr, MaterialPath))
		{
			Mesh->SetMaterial(0, Material);
		}
		Spawned.Add(Actor);
		return Actor;
	}

	UStarBodyComponent* AddStarComponent(AActor* Actor)
	{
		if (!Actor)
		{
			return nullptr;
		}
		UStarBodyComponent* Star = NewObject<UStarBodyComponent>(
			Actor, UStarBodyComponent::StaticClass(), TEXT("StarBody"));
		Star->RegisterComponent();
		return Star;
	}

	ADirectionalLight* SpawnLight(UWorld* World, const FVector& Location, TArray<AActor*>& Spawned)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		ADirectionalLight* Light =
			World->SpawnActor<ADirectionalLight>(ADirectionalLight::StaticClass(), FTransform(Location), Params);
		if (Light)
		{
			Spawned.Add(Light);
		}
		return Light;
	}

	void DestroyAll(TArray<AActor*>& Spawned)
	{
		for (AActor* Actor : Spawned)
		{
			if (IsValid(Actor))
			{
				Actor->Destroy();
			}
		}
		Spawned.Reset();
	}

	const TCHAR* const MI_SunM2V = TEXT("/Game/Code/Res/Star/Materials/MI_Sun_M2V.MI_Sun_M2V");
	const TCHAR* const MI_SunG2V = TEXT("/Game/Code/Res/Star/Materials/MI_Sun_G2V.MI_Sun_G2V");
	const TCHAR* const MI_SunB3V = TEXT("/Game/Code/Res/Star/Materials/MI_Sun_B3V.MI_Sun_B3V");
} // namespace StellarEraMultiSunTests

// 纯数学：归一化与相对亮度换算。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraSunRadianceMathTest,
	"StellarEra.Planet.Atmosphere.SunRadiance.Math",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FStellarEraSunRadianceMathTest::RunTest(const FString& Parameters)
{
	const FLinearColor Normalized =
		UStarBodyComponent::NormalizeSunColor(FLinearColor(0.4f, 2.0f, 1.0f, 1.0f));
	TestEqual(TEXT("归一化后最大分量（G）为 1"), Normalized.G, 1.0f, 1e-5f);
	TestEqual(TEXT("归一化保持比例 R"), Normalized.R, 0.2f, 1e-5f);
	TestEqual(TEXT("归一化保持比例 B"), Normalized.B, 0.5f, 1e-5f);

	const FLinearColor Black = UStarBodyComponent::NormalizeSunColor(FLinearColor(0.0f, 0.0f, 0.0f, 1.0f));
	TestEqual(TEXT("全黑回退白色 R"), Black.R, 1.0f, 1e-5f);
	TestEqual(TEXT("全黑回退白色 G"), Black.G, 1.0f, 1e-5f);
	TestEqual(TEXT("全黑回退白色 B"), Black.B, 1.0f, 1e-5f);

	TestEqual(TEXT("Brightness 60 → 0.4"), UStarBodyComponent::BrightnessToRelative(60.0f), 0.4f, 1e-4f);
	TestEqual(TEXT("Brightness 150 → 1.0"), UStarBodyComponent::BrightnessToRelative(150.0f), 1.0f, 1e-4f);
	TestEqual(TEXT("Brightness 400 → 2.6667"), UStarBodyComponent::BrightnessToRelative(400.0f), 2.6667f, 1e-3f);
	TestEqual(TEXT("Brightness 0 钳到下限"), UStarBodyComponent::BrightnessToRelative(0.0f), 0.05f, 1e-5f);
	TestEqual(TEXT("Brightness 1e6 钳到上限"), UStarBodyComponent::BrightnessToRelative(1.0e6f), 4.0f, 1e-5f);

	return true;
}

// 从材质实例读颜色与亮度。不写死预设数值（预设调色会红测试），
// 只断言三个预设的相对关系 —— 这才是"真的读到 MI 覆盖值"的证据。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraSunMaterialReadTest,
	"StellarEra.Planet.Atmosphere.SunRadiance.MaterialRead",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FStellarEraSunMaterialReadTest::RunTest(const FString& Parameters)
{
	UWorld* World = StellarEraMultiSunTests::GetTestWorld();
	if (!TestNotNull(TEXT("编辑器世界"), World))
	{
		return false;
	}

	TArray<AActor*> Spawned;
	const FVector Base(2.0e13, 0.0, 0.0);
	UStarBodyComponent* M2V = StellarEraMultiSunTests::AddStarComponent(
		StellarEraMultiSunTests::SpawnMeshActor(World, Base, StellarEraMultiSunTests::MI_SunM2V, Spawned));
	UStarBodyComponent* G2V = StellarEraMultiSunTests::AddStarComponent(
		StellarEraMultiSunTests::SpawnMeshActor(World, Base + FVector(0, 1.0e6, 0),
			StellarEraMultiSunTests::MI_SunG2V, Spawned));
	UStarBodyComponent* B3V = StellarEraMultiSunTests::AddStarComponent(
		StellarEraMultiSunTests::SpawnMeshActor(World, Base + FVector(0, 2.0e6, 0),
			StellarEraMultiSunTests::MI_SunB3V, Spawned));

	if (TestNotNull(TEXT("M2V 恒星"), M2V) && TestNotNull(TEXT("G2V 恒星"), G2V) &&
		TestNotNull(TEXT("B3V 恒星"), B3V))
	{
		TestTrue(TEXT("M2V 比 G2V 暗"), M2V->GetRelativeBrightness() < G2V->GetRelativeBrightness());
		TestTrue(TEXT("G2V 比 B3V 暗"), G2V->GetRelativeBrightness() < B3V->GetRelativeBrightness());
		TestTrue(TEXT("相对亮度都在物理量级内"),
			G2V->GetRelativeBrightness() > 0.1f && G2V->GetRelativeBrightness() < 10.0f);
		TestFalse(TEXT("两颗不同预设的颜色不同"),
			M2V->GetSunColor().Equals(B3V->GetSunColor(), 1e-3f));
		TestEqual(TEXT("归一化后最大分量为 1（G2V）"),
			FMath::Max3(G2V->GetSunColor().R, G2V->GetSunColor().G, G2V->GetSunColor().B), 1.0f, 1e-3f);
	}

	StellarEraMultiSunTests::DestroyAll(Spawned);
	return true;
}

// 灯绑定：显式优先；不显式时两颗星不会认领同一盏灯。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraSunLightBindTest,
	"StellarEra.Planet.Atmosphere.SunLight.Bind",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FStellarEraSunLightBindTest::RunTest(const FString& Parameters)
{
	UWorld* World = StellarEraMultiSunTests::GetTestWorld();
	if (!TestNotNull(TEXT("编辑器世界"), World))
	{
		return false;
	}

	TArray<AActor*> Spawned;
	UStarBodyComponent* CompA = StellarEraMultiSunTests::AddStarComponent(
		StellarEraMultiSunTests::SpawnRootedActor(World, FVector(3.0e13, 0.0, 0.0), Spawned));
	UStarBodyComponent* CompB = StellarEraMultiSunTests::AddStarComponent(
		StellarEraMultiSunTests::SpawnRootedActor(World, FVector(3.0e13, 1.0e8, 0.0), Spawned));
	ADirectionalLight* LightA = StellarEraMultiSunTests::SpawnLight(World, FVector(0, 0, 1.0e10), Spawned);
	ADirectionalLight* LightB = StellarEraMultiSunTests::SpawnLight(World, FVector(0, 1.0e10, 0), Spawned);

	if (!TestNotNull(TEXT("恒星 A"), CompA) || !TestNotNull(TEXT("恒星 B"), CompB) ||
		!TestNotNull(TEXT("灯 A"), LightA) || !TestNotNull(TEXT("灯 B"), LightB))
	{
		StellarEraMultiSunTests::DestroyAll(Spawned);
		return false;
	}

	// 显式绑定优先。
	CompA->SunLight = LightA;
	CompB->SunLight = LightB;
	TestSamePtr(TEXT("A 用显式指定的灯"), CompA->ResolveSunLight(), LightA);
	TestSamePtr(TEXT("B 用显式指定的灯"), CompB->ResolveSunLight(), LightB);

	// 都不显式时各自认领不同的灯。
	CompA->SunLight = nullptr;
	CompB->SunLight = nullptr;
	ADirectionalLight* AutoA = CompA->ResolveSunLight();
	ADirectionalLight* AutoB = CompB->ResolveSunLight();
	TestNotNull(TEXT("A 自动认领到一盏灯"), AutoA);
	TestNotNull(TEXT("B 自动认领到一盏灯"), AutoB);
	TestFalse(TEXT("两颗星不共用同一盏灯"), AutoA == AutoB);

	StellarEraMultiSunTests::DestroyAll(Spawned);
	return true;
}

// 太阳列表：按距离升序、截断到 MaxSuns、Direction 是单位向量。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraSunListBuildTest,
	"StellarEra.Planet.Atmosphere.SunList.Build",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FStellarEraSunListBuildTest::RunTest(const FString& Parameters)
{
	UWorld* World = StellarEraMultiSunTests::GetTestWorld();
	if (!TestNotNull(TEXT("编辑器世界"), World))
	{
		return false;
	}

	// 探针点放在离关卡内容极远的地方，保证最近的是下面这几颗。
	const FVector Probe(5.0e13, 0.0, 0.0);
	TArray<AActor*> Spawned;

	// 距离 1e6 / 2e6 / 3e6，方向 +X / +Y / +Z。
	TArray<UStarBodyComponent*> Stars;
	Stars.Add(StellarEraMultiSunTests::AddStarComponent(
		StellarEraMultiSunTests::SpawnRootedActor(World, Probe + FVector(1.0e6, 0, 0), Spawned)));
	Stars.Add(StellarEraMultiSunTests::AddStarComponent(
		StellarEraMultiSunTests::SpawnRootedActor(World, Probe + FVector(0, 2.0e6, 0), Spawned)));
	Stars.Add(StellarEraMultiSunTests::AddStarComponent(
		StellarEraMultiSunTests::SpawnRootedActor(World, Probe + FVector(0, 0, 3.0e6), Spawned)));

	TArray<FPlanetSun> Suns;
	UPlanetAtmosphereComponent::CollectSunsFor(World, Probe, Suns);

	// 编辑器世界加载的是 UniverseMap，关卡自己带了恒星，所以不能断言总数恰为 3；
	// 能断言的是"测试放的这三颗比关卡里的都近，因此占据前三个槽位"。
	if (TestTrue(TEXT("至少收进三颗恒星"), Suns.Num() >= 3))
	{
		TestTrue(TEXT("槽位 0 是最近的星（+X）"), Suns[0].Direction.Equals(FVector(1, 0, 0), 1e-3f));
		TestTrue(TEXT("槽位 1 是第二近（+Y）"), Suns[1].Direction.Equals(FVector(0, 1, 0), 1e-3f));
		TestTrue(TEXT("槽位 2 是第三近（+Z）"), Suns[2].Direction.Equals(FVector(0, 0, 1), 1e-3f));
		TestEqual(TEXT("Direction 是单位向量"), Suns[0].Direction.Size(), 1.0, 1e-3);
		TestSamePtr(TEXT("Star 指回组件"), Suns[0].Star, Stars[0]);
		TestTrue(TEXT("Radiance 非负"), Suns[0].Radiance.R >= 0.0f && Suns[0].Radiance.G >= 0.0f);
	}

	StellarEraMultiSunTests::DestroyAll(Spawned);

	// 截断：5 颗星只留最近的 MaxSuns 颗。
	for (int32 Index = 0; Index < 5; ++Index)
	{
		StellarEraMultiSunTests::AddStarComponent(StellarEraMultiSunTests::SpawnRootedActor(
			World, Probe + FVector(1.0e6 * (Index + 1), 0, 0), Spawned));
	}
	UPlanetAtmosphereComponent::CollectSunsFor(World, Probe, Suns);
	TestEqual(TEXT("超过 MaxSuns 时截断"), Suns.Num(), PlanetClouds::MaxSuns);
	TestTrue(TEXT("截断保留最近的那颗（距离 1e6，+X）"),
		Suns.IsValidIndex(0) && Suns[0].Direction.Equals(FVector(1, 0, 0), 1e-3f));

	StellarEraMultiSunTests::DestroyAll(Spawned);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
