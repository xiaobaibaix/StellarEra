// 相机横滚滞后的手感回归测试。
// 滞后是「机身滚多少先欠着、再指数还回去」：欠得多就是滚转开头画面跟不上（星球像卡住
// 不动），还得快就是松手瞬间画面反甩。两个量都由 MaxRollRate / CameraRollFollowSpeed
// 决定，这里按实际 CDO 参数数值积分，锁住上界。

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "SpaceshipPawn.h"
#include "UObject/UnrealType.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraSpaceshipCameraRollLagTest,
	"StellarEra.Spaceship.CameraRollLag",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraSpaceshipCameraRollLagTest::RunTest(const FString& Parameters)
{
	UClass* ShipClass = LoadClass<ASpaceshipPawn>(
		nullptr, TEXT("/Game/Code/Pawns/BP_SpaceshipPawn.BP_SpaceshipPawn_C"));
	if (!TestNotNull(TEXT("BP_SpaceshipPawn_C 可加载"), ShipClass))
	{
		return false;
	}

	const ASpaceshipPawn* CDO = Cast<ASpaceshipPawn>(ShipClass->GetDefaultObject());
	if (!TestNotNull(TEXT("BP_SpaceshipPawn CDO"), CDO))
	{
		return false;
	}

	const auto GetFloat = [CDO](const TCHAR* Name) -> float
	{
		const FFloatProperty* Prop = FindFProperty<FFloatProperty>(CDO->GetClass(), Name);
		return Prop ? Prop->GetPropertyValue_InContainer(CDO) : -1.f;
	};

	const float MaxRollRate = GetFloat(TEXT("MaxRollRate"));
	const float FollowSpeed = GetFloat(TEXT("CameraRollFollowSpeed"));
	if (!TestTrue(TEXT("MaxRollRate / CameraRollFollowSpeed 可读"),
		MaxRollRate > 0.f && FollowSpeed > 0.f))
	{
		return false;
	}

	// 与 Tick 里同一套积分：每帧先累加机身滚过的角度，再 FInterpTo 回 0。
	const float Dt = 1.f / 60.f;
	const auto Step = [](float Lag, float Rolled, float DeltaSeconds, float Speed)
	{
		return FMath::FInterpTo(Lag + Rolled, 0.f, DeltaSeconds, Speed);
	};

	// 持续压杆 1 秒后的稳态滞后：机身滚得最狠时画面欠了多少度。
	float Lag = 0.f;
	for (int32 i = 0; i < 60; ++i)
	{
		Lag = Step(Lag, MaxRollRate * Dt, Dt, FollowSpeed);
	}
	TestTrue(FString::Printf(TEXT("满滚转稳态滞后 %.2f° 不超过 5°"), Lag),
		FMath::Abs(Lag) <= 5.f);

	// 松手后滞后归零：超过 ~0.25 秒就是能看见的「反甩」。
	int32 FramesToSettle = 0;
	for (; FramesToSettle < 120 && FMath::Abs(Lag) > 0.5f; ++FramesToSettle)
	{
		Lag = Step(Lag, 0.f, Dt, FollowSpeed);
	}
	TestTrue(FString::Printf(TEXT("松手后 %.3f s 归零，不超过 0.25 s"), FramesToSettle * Dt),
		FramesToSettle * Dt <= 0.25f);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
