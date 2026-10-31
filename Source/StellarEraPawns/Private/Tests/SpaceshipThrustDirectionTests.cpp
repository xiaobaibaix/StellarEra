// 升降推力方向的验收测试。
//
// 断言纯数学，不生成 Actor、不依赖关卡：
//   - 有参考行星时，「上」是行星球心 → 飞船中心的径向，与机身姿态无关；
//   - 太空中（没有参考行星）退回机身本地 up；
//   - 船心与星心重合的退化情形同样退回本地 up。

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "SpaceshipPawn.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraShipThrustDirectionTest,
	"StellarEra.Spaceship.ShipThrustDirection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraShipThrustDirectionTest::RunTest(const FString& Parameters)
{
	const FVector PlanetLocation(1000.0, 2000.0, 3000.0);
	// 飞船在行星 +X 方向的地表；机身本地 up 故意指向世界 Z（相当于机身歪了 90°）。
	const FVector ShipLocation = PlanetLocation + FVector(750000.0, 0.0, 0.0);
	const FVector BodyUp = FVector::UpVector;

	const FVector RadialUp = ASpaceshipPawn::ResolveVerticalThrustUp(
		ShipLocation, PlanetLocation, /*bHasPlanet=*/true, BodyUp);
	TestTrue(TEXT("有行星：升降 up 取星心 → 船心径向"),
		RadialUp.Equals(FVector(1.0, 0.0, 0.0), 1e-4));
	TestFalse(TEXT("有行星：升降 up 不再是机身本地 up"),
		RadialUp.Equals(BodyUp, 1e-4));

	// 飘在深空：没有参考行星，退回机身本地 up。
	const FVector DeepSpaceUp = ASpaceshipPawn::ResolveVerticalThrustUp(
		ShipLocation, FVector::ZeroVector, /*bHasPlanet=*/false, BodyUp);
	TestTrue(TEXT("无行星：退回机身本地 up"),
		DeepSpaceUp.Equals(BodyUp, 1e-4));

	// 退化：船心与星心重合，径向为零，退回本地 up。
	const FVector DegenerateUp = ASpaceshipPawn::ResolveVerticalThrustUp(
		PlanetLocation, PlanetLocation, /*bHasPlanet=*/true, BodyUp);
	TestTrue(TEXT("船心与星心重合：退回机身本地 up"),
		DegenerateUp.Equals(BodyUp, 1e-4));

	return true;
}

#endif
