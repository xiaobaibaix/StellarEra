// 远场模式只做两件可观测的事：切换状态、关 Actor 碰撞；代理本身由构造函数建。

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "VoxelPlanetActor.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraPlanetFarProxyTest,
	"StellarEra.Planet.Geo.FarProxy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FStellarEraPlanetFarProxyTest::RunTest(const FString& Parameters)
{
	AVoxelPlanetActor* Planet = NewObject<AVoxelPlanetActor>(GetTransientPackage());
	if (!TestNotNull(TEXT("行星"), Planet))
	{
		return false;
	}

	TestFalse(TEXT("默认近场"), Planet->IsFarFieldMode());
	Planet->SetFarFieldMode(true);
	TestTrue(TEXT("进入远场"), Planet->IsFarFieldMode());
	TestFalse(TEXT("远场关碰撞"), Planet->GetActorEnableCollision());
	Planet->SetFarFieldMode(false);
	TestTrue(TEXT("回到近场开碰撞"), Planet->GetActorEnableCollision());

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
