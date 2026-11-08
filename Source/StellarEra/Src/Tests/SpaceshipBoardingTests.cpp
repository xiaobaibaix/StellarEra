// 登船射程必须覆盖下船落脚点。
//
// 守的回归：旧值 BoardingRadius = 600 cm，而球心就是船体几何中心、船体外壳半宽 4037 cm
// —— 整颗球埋在船壳里，下船后站在 4287 cm 处永远等不到「按 E 登船」。

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Engine/Engine.h"
#include "Engine/World.h"

#include "SpaceshipPawn.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraSpaceshipBoardingRangeTest,
	"StellarEra.Spaceship.Boarding.Range",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraSpaceshipBoardingRangeTest::RunTest(const FString& Parameters)
{
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

	// 挪离原点：射程押在中心距离上，中心为 0 时「球心 / 边界」算错也看不出来。
	ASpaceshipPawn* Ship = World->SpawnActor<ASpaceshipPawn>(
		ASpaceshipPawn::StaticClass(), FTransform(FVector(100000.0, 200000.0, 3000.0)));
	if (!TestNotNull(TEXT("飞船"), Ship))
	{
		return false;
	}

	const FVector Center = Ship->GetActorLocation();
	const float Radius = Ship->GetBoardingRadiusCm();

	TestTrue(TEXT("射程不小于落地检测球"), Radius >= Ship->GetLandingQueryRadiusCm());

	// 下船就落在这条线上：它必须在圈内，否则下船当场按 E 没反应。
	const float ExitDistanceCm = FVector::Dist(Ship->GetCrewExitLocation(), Center);
	TestTrue(TEXT("下船落脚点在射程内"), ExitDistanceCm <= Radius + 0.5f);

	TestTrue(TEXT("边界内侧 1 cm 可登船"),
		Ship->CanBoardFrom(Center + FVector(Radius - 1.f, 0.f, 0.f)));
	TestFalse(TEXT("边界外侧 1 cm 不可登船"),
		Ship->CanBoardFrom(Center + FVector(Radius + 1.f, 0.f, 0.f)));

	return true;
}

#endif
