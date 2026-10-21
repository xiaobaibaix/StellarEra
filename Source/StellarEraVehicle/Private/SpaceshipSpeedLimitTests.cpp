// Copyright Voxel Plugin SAS. All Rights Reserved.
//
// 飞船速度上限的验收测试。
//
// 分两层断言：
//   1. ComputeSpeedLimitTaper 是纯函数：静止 / 半程 / 九成 / 到顶 / 超顶 / 上限为 0；
//   2. GetThrustAccelerationWorldCmPerS2 的推力剖面：半程只剩 0.75 的沿速推力，
//      到顶后正向推力归零；S 制动与横向（升降）推力不衰减 —— 否则到顶以后
//      转不了向、刹不住车，在重力井里也抬不起头。
//
// 行为断言在独立编辑器世界里生成的真飞船上做（CDO 没有世界，GetStatus 里的
// 落地查询会空指针），世界随测试销毁，不用手工还原状态。

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "UObject/UnrealType.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "SpaceshipPawn.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	/** 测试用上限：10 km/s = 1e6 厘米/秒，比默认值好算。 */
	constexpr float TestCapKmPerSecond = 10.f;

	/** 与上面的上限同刻度（世界厘米/秒）。 */
	constexpr double TestCapCmPerSecond = 1.0e6;

	/**
	 * 读 / 写飞船上的速度上限。属性是 protected，测试不为它往公开 API 里加
	 * setter —— 反射只是按属性名取指针的一层薄封装。
	 */
	float ReadMaxSpeedKmPerSecond(ASpaceshipPawn* Ship)
	{
		const FFloatProperty* Property = FindFProperty<FFloatProperty>(
			ASpaceshipPawn::StaticClass(), TEXT("MaxSpeedKmPerSecond"));
		return Property ? *Property->ContainerPtrToValuePtr<float>(Ship) : 0.f;
	}

	void WriteMaxSpeedKmPerSecond(ASpaceshipPawn* Ship, float Value)
	{
		if (const FFloatProperty* Property = FindFProperty<FFloatProperty>(
			ASpaceshipPawn::StaticClass(), TEXT("MaxSpeedKmPerSecond")))
		{
			*Property->ContainerPtrToValuePtr<float>(Ship) = Value;
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraSpeedLimitTaperTest,
	"StellarEra.Vehicle.SpeedLimitTaper",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraSpeedLimitTaperTest::RunTest(const FString& Parameters)
{
	auto ExpectTaper = [this](const TCHAR* What, double Speed, double MaxSpeed, float Expected)
	{
		const float Actual = ASpaceshipPawn::ComputeSpeedLimitTaper(Speed, MaxSpeed);
		const bool bMatches = FMath::IsNearlyEqual(Actual, Expected, 1.0e-4f);
		TestTrue(What, bMatches);
		if (!bMatches)
		{
			AddError(FString::Printf(TEXT("%s：v=%.0f cap=%.0f 期望 %.3f，实际 %.3f"),
				What, Speed, MaxSpeed, Expected, Actual));
		}
	};

	ExpectTaper(TEXT("静止时满推力"), 0.0, TestCapCmPerSecond, 1.f);
	ExpectTaper(TEXT("半程剩 3/4"), 0.5 * TestCapCmPerSecond, TestCapCmPerSecond, 0.75f);
	ExpectTaper(TEXT("九成剩 0.19"), 0.9 * TestCapCmPerSecond, TestCapCmPerSecond, 0.19f);
	ExpectTaper(TEXT("到顶为 0"), TestCapCmPerSecond, TestCapCmPerSecond, 0.f);
	ExpectTaper(TEXT("超顶仍为 0"), 2.0 * TestCapCmPerSecond, TestCapCmPerSecond, 0.f);
	ExpectTaper(TEXT("上限 0 = 不限"), 5.0 * TestCapCmPerSecond, 0.0, 1.f);
	ExpectTaper(TEXT("负上限按不限处理"), 5.0 * TestCapCmPerSecond, -1.0, 1.f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraSpeedLimitThrustTest,
	"StellarEra.Vehicle.SpeedLimitThrustProfile",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraSpeedLimitThrustTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Editor, false);
	if (!TestNotNull(TEXT("测试世界"), World))
	{
		return false;
	}
	FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Editor);
	WorldContext.SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());

	ON_SCOPE_EXIT
	{
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
	};

	UClass* ShipClass = LoadClass<ASpaceshipPawn>(
		nullptr, TEXT("/Game/Code/BP_SpaceshipPawn.BP_SpaceshipPawn_C"));
	TestNotNull(TEXT("BP_SpaceshipPawn_C 可加载"), ShipClass);
	if (!ShipClass)
	{
		return false;
	}

	// 生成点放在真空里（没有行星 → 升降方向退回机身 up），离世界原点远一点，
	// 免得测试世界里的任何默认 Actor 干扰。
	ASpaceshipPawn* Ship = World->SpawnActor<ASpaceshipPawn>(
		ShipClass, FTransform(FRotator::ZeroRotator, FVector(0.0, 0.0, 1.0e7)));
	TestNotNull(TEXT("能生成飞船"), Ship);
	if (!Ship)
	{
		return false;
	}

	WriteMaxSpeedKmPerSecond(Ship, TestCapKmPerSecond);
	TestTrue(TEXT("测试写入速度上限成功"),
		FMath::IsNearlyEqual(ReadMaxSpeedKmPerSecond(Ship), TestCapKmPerSecond));

	Ship->SetBoost(false);
	Ship->SetVertical(0.f);
	Ship->SetThrust(1.f);

	const FVector Forward = Ship->GetActorForwardVector();
	const FVector Up = Ship->GetActorUpVector();
	constexpr float DeltaSeconds = 1.0f / 60.0f;

	// 静止时的推力就是基准 A：v = 0 处衰减系数必须是 1，否则上限改动了低速手感。
	Ship->SetCosmosVelocity(FVector::ZeroVector);
	const double FullThrust =
		Ship->GetThrustAccelerationWorldCmPerS2(DeltaSeconds).Size();
	TestTrue(TEXT("静止时有推力"), FullThrust > 0.0);

	// 半程：f = 1 − 0.5² = 0.75。
	{
		Ship->SetCosmosVelocity(Forward * (0.5 * TestCapCmPerSecond));
		const FVector Acceleration =
			Ship->GetThrustAccelerationWorldCmPerS2(DeltaSeconds);
		TestTrue(TEXT("半程沿速推力剩 75%"),
			FMath::IsNearlyEqual(Acceleration.Size(), FullThrust * 0.75, FullThrust * 1.0e-3));
	}

	// 到顶 / 超顶：正向推力归零，速度不会再被推高。
	{
		Ship->SetCosmosVelocity(Forward * TestCapCmPerSecond);
		TestTrue(TEXT("到顶时正向推力归零"),
			Ship->GetThrustAccelerationWorldCmPerS2(DeltaSeconds).Size()
				< FullThrust * 1.0e-3);

		Ship->SetCosmosVelocity(Forward * (1.2 * TestCapCmPerSecond));
		TestTrue(TEXT("超顶时正向推力仍为 0"),
			Ship->GetThrustAccelerationWorldCmPerS2(DeltaSeconds).Size()
				< FullThrust * 1.0e-3);
	}

	// S 制动：到顶时按 S 仍拿满推力，方向与速度相反。
	{
		Ship->SetThrust(-1.f);
		Ship->SetCosmosVelocity(Forward * TestCapCmPerSecond);
		const FVector Brake = Ship->GetThrustAccelerationWorldCmPerS2(DeltaSeconds);
		TestTrue(TEXT("到顶时 S 制动不打折"),
			FMath::IsNearlyEqual(
				FVector::DotProduct(Brake, Forward), -FullThrust, FullThrust * 1.0e-3));

		Ship->SetThrust(1.f);
	}

	// 横向推力：速度 90% 上限，升降键沿机身 up —— 与速度垂直，不该被衰减。
	{
		Ship->SetThrust(0.f);
		Ship->SetVertical(1.f);
		Ship->SetCosmosVelocity(Forward * (0.9 * TestCapCmPerSecond));
		const FVector Acceleration =
			Ship->GetThrustAccelerationWorldCmPerS2(DeltaSeconds);
		TestTrue(TEXT("90% 上限时横向推力不打折"),
			FMath::IsNearlyEqual(Acceleration.Size(), FullThrust, FullThrust * 1.0e-3));
		TestTrue(TEXT("横向推力沿机身 up，不混进沿速分量"),
			FMath::IsNearlyEqual(
				FVector::DotProduct(Acceleration, Up), FullThrust, FullThrust * 1.0e-3)
			&& FMath::IsNearlyEqual(
				FVector::DotProduct(Acceleration, Forward), 0.0, FullThrust * 1.0e-3));
	}

	// 上限填 0 = 不限：超速时照给满推力。
	{
		WriteMaxSpeedKmPerSecond(Ship, 0.f);
		Ship->SetThrust(1.f);
		Ship->SetVertical(0.f);
		Ship->SetCosmosVelocity(Forward * (2.0 * TestCapCmPerSecond));
		TestTrue(TEXT("上限为 0 时没有衰减"),
			FMath::IsNearlyEqual(
				Ship->GetThrustAccelerationWorldCmPerS2(DeltaSeconds).Size(),
				FullThrust, FullThrust * 1.0e-3));
	}

	// HUD 读的极速：巡航关着时是阻尼极速（比硬上限小）；巡航开着时才是硬上限。
	WriteMaxSpeedKmPerSecond(Ship, TestCapKmPerSecond);
	{
		const FSpaceshipStatus Status = Ship->GetStatus();
		if (!Status.bPreserveVelocity)
		{
			TestTrue(TEXT("巡航关着时极速不超过硬上限"),
				Status.MaxSpeedKmPerSecond > 0.f
				&& Status.MaxSpeedKmPerSecond <= TestCapKmPerSecond);
		}

		Ship->TogglePreserveVelocity();
		const FSpaceshipStatus CruiseStatus = Ship->GetStatus();
		TestTrue(TEXT("巡航开着时极速就是硬上限"),
			FMath::IsNearlyEqual(
				CruiseStatus.MaxSpeedKmPerSecond, TestCapKmPerSecond));
		Ship->TogglePreserveVelocity();
	}

	return true;
}

#endif
