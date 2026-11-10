// Copyright Voxel Plugin SAS. All Rights Reserved.
//
// 落地自由视角的验收测试：只测纯数学，不生成 Actor、不依赖关卡。
// 覆盖「偏航归一化、俯仰夹取」；Tab 开关的状态契约在 AngularInputState 里测。

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "SpaceshipPawn.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraShipFreeLookTest,
	"StellarEra.Spaceship.ShipFreeLook",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraShipFreeLookTest::RunTest(const FString& Parameters)
{
	// --- 鼠标累加 ---
	float Yaw = 0.f;
	float Pitch = 0.f;
	ASpaceshipPawn::ApplyFreeLookLookInput(30.f, 10.f, -75.f, 75.f, Yaw, Pitch);
	TestEqual(TEXT("偏航累加"), Yaw, 30.f, 0.01f);
	TestEqual(TEXT("俯仰累加"), Pitch, 10.f, 0.01f);

	// 170 + 20 = 190 → 归一化 -170
	Yaw = 170.f;
	Pitch = 0.f;
	ASpaceshipPawn::ApplyFreeLookLookInput(20.f, 0.f, -75.f, 75.f, Yaw, Pitch);
	TestEqual(TEXT("偏航归一化到 (-180, 180]"), Yaw, -170.f, 0.01f);

	// -170 - 20 = -190 → 170
	Yaw = -170.f;
	Pitch = 0.f;
	ASpaceshipPawn::ApplyFreeLookLookInput(-20.f, 0.f, -75.f, 75.f, Yaw, Pitch);
	TestEqual(TEXT("负偏航同样归一化"), Yaw, 170.f, 0.01f);

	// --- 俯仰夹取 ---
	Yaw = 0.f;
	Pitch = 0.f;
	ASpaceshipPawn::ApplyFreeLookLookInput(0.f, 100.f, -75.f, 75.f, Yaw, Pitch);
	TestEqual(TEXT("俯仰上限 75"), Pitch, 75.f, 0.01f);

	ASpaceshipPawn::ApplyFreeLookLookInput(0.f, -300.f, -75.f, 75.f, Yaw, Pitch);
	TestEqual(TEXT("俯仰下限 -75"), Pitch, -75.f, 0.01f);

	// 配置反了也不崩：下限取 min、上限取 max。
	Yaw = 0.f;
	Pitch = 0.f;
	ASpaceshipPawn::ApplyFreeLookLookInput(0.f, 100.f, 75.f, -75.f, Yaw, Pitch);
	TestEqual(TEXT("Min/Max 反配时仍夹在区间内"), Pitch, 75.f, 0.01f);

	return true;
}

#endif
