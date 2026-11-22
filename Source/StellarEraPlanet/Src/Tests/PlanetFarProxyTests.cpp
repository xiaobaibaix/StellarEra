// 远场模式：代理可见、其它图元隐藏、关碰撞；近场原样恢复（包括进入前就藏着的）。
// 需要注册态组件树，否则 AttachChildren 为空，测不到根与子级的可见性传播。

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "VoxelPlanetActor.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	/** 一次性编辑器世界，析构时销毁。照抄 PlanetRiderAttachTests.cpp 的写法。 */
	struct FScopedFarProxyTestWorld
	{
		UWorld* World = nullptr;

		bool Create()
		{
			World = UWorld::CreateWorld(EWorldType::Editor, false);
			if (!World)
			{
				return false;
			}
			FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Editor);
			Context.SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			return true;
		}

		~FScopedFarProxyTestWorld()
		{
			if (World)
			{
				GEngine->DestroyWorldContext(World);
				World->DestroyWorld(false);
			}
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraPlanetFarProxyTest,
	"StellarEra.Planet.Geo.FarProxy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FStellarEraPlanetFarProxyTest::RunTest(const FString& Parameters)
{
	FScopedFarProxyTestWorld Scoped;
	if (!TestTrue(TEXT("测试世界"), Scoped.Create()))
	{
		return false;
	}

	AVoxelPlanetActor* Planet = NewObject<AVoxelPlanetActor>(GetTransientPackage());
	if (!TestNotNull(TEXT("行星"), Planet))
	{
		return false;
	}
	USceneComponent* Root = Planet->GetRootComponent();
	if (!TestNotNull(TEXT("根组件"), Root) || !TestNotNull(TEXT("远场代理"), Planet->FarProxy.Get()))
	{
		return false;
	}

	// 两个普通子图元：一个进入前可见，一个进入前已隐藏（验记录式恢复）。
	UStaticMeshComponent* VisibleChild = NewObject<UStaticMeshComponent>(Planet);
	VisibleChild->SetupAttachment(Root);
	UStaticMeshComponent* HiddenChild = NewObject<UStaticMeshComponent>(Planet);
	HiddenChild->SetupAttachment(Root);
	HiddenChild->SetVisibility(false, false);

	Root->RegisterComponentWithWorld(Scoped.World);
	Planet->FarProxy->RegisterComponentWithWorld(Scoped.World);
	VisibleChild->RegisterComponentWithWorld(Scoped.World);
	HiddenChild->RegisterComponentWithWorld(Scoped.World);
	// 注册态的 AttachToComponent 才会写父级 AttachChildren（SetupAttachment 只记待挂）。
	Planet->FarProxy->AttachToComponent(Root, FAttachmentTransformRules::KeepRelativeTransform);
	VisibleChild->AttachToComponent(Root, FAttachmentTransformRules::KeepRelativeTransform);
	HiddenChild->AttachToComponent(Root, FAttachmentTransformRules::KeepRelativeTransform);

	TestFalse(TEXT("默认近场"), Planet->IsFarFieldMode());
	TestFalse(TEXT("近场代理隐藏"), Planet->FarProxy->IsVisible());
	TestTrue(TEXT("近场普通子图元可见"), VisibleChild->IsVisible());

	Planet->SetFarFieldMode(true);
	TestTrue(TEXT("进入远场"), Planet->IsFarFieldMode());
	TestFalse(TEXT("远场关碰撞"), Planet->GetActorEnableCollision());
	TestTrue(TEXT("远场代理可见"), Planet->FarProxy->IsVisible());
	TestFalse(TEXT("远场隐藏子图元"), VisibleChild->IsVisible());
	TestFalse(TEXT("远场保持原有隐藏"), HiddenChild->IsVisible());
	TestTrue(TEXT("根组件不被隐藏"), Root->IsVisible());

	Planet->SetFarFieldMode(false);
	TestTrue(TEXT("回到近场开碰撞"), Planet->GetActorEnableCollision());
	TestFalse(TEXT("近场代理隐藏"), Planet->FarProxy->IsVisible());
	TestTrue(TEXT("近场恢复子图元"), VisibleChild->IsVisible());
	TestFalse(TEXT("进入前已隐藏的退出后仍隐藏"), HiddenChild->IsVisible());
	TestTrue(TEXT("根组件仍可见"), Root->IsVisible());

	HiddenChild->UnregisterComponent();
	VisibleChild->UnregisterComponent();
	Planet->FarProxy->UnregisterComponent();
	Root->UnregisterComponent();

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
