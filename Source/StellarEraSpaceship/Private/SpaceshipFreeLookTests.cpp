// Copyright Voxel Plugin SAS. All Rights Reserved.
//
// 落地自由视角的验收测试。
//
// 断言的是「何时冻结飞船、鼠标偏移怎么累加」的纯数学，不生成 Actor、不依赖关卡：
//   - 落地且没有 W / Space 时才进入自由视角；S / Ctrl 不退出；未落地一律不自由；
//   - 偏航累加后归一化到 (-180, 180]，俯仰夹在配置区间内。

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "SpaceshipPawn.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraShipFreeLookTest,
	"StellarEra.Spaceship.ShipFreeLook",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraShipFreeLookTest::RunTest(const FString& Parameters)
{
	// --- 激活条件 ---
	TestTrue(TEXT("落地、无输入 → 自由视角"),
		ASpaceshipPawn::ShouldUseFreeLook(true, 0.f, 0.f));
	TestTrue(TEXT("落地、按 S（制动）→ 仍自由视角"),
		ASpaceshipPawn::ShouldUseFreeLook(true, -1.f, 0.f));
	TestTrue(TEXT("落地、按 Ctrl（下降）→ 仍自由视角"),
		ASpaceshipPawn::ShouldUseFreeLook(true, 0.f, -1.f));
	TestFalse(TEXT("落地、按 W（推力）→ 退出自由视角"),
		ASpaceshipPawn::ShouldUseFreeLook(true, 1.f, 0.f));
	TestFalse(TEXT("落地、按 Space（上升）→ 退出自由视角"),
		ASpaceshipPawn::ShouldUseFreeLook(true, 0.f, 1.f));
	TestFalse(TEXT("未落地、无输入 → 不自由视角"),
		ASpaceshipPawn::ShouldUseFreeLook(false, 0.f, 0.f));
	TestFalse(TEXT("未落地、按 S → 不自由视角"),
		ASpaceshipPawn::ShouldUseFreeLook(false, -1.f, 0.f));

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
