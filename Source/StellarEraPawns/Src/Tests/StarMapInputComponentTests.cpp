// Copyright Voxel Plugin SAS. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "StarMapCameraInputComponent.h"
#include "StarMapCameraPawn.h"

#if WITH_DEV_AUTOMATION_TESTS

// 断言 CDO，验的是「组件真的建在类上、默认值没被人改跑」。
// 「真的绑上了」的证据是 StellarEra.StarMap.InputWiring（它真调 SetupPlayerInputComponent
// 并断言六条动作都进了 EnhancedInputComponent）—— 不要指望无头日志：进星图要靠按键，
// 而进星图那条路径（输入组件 → FindStarMapCameraFor → EnterStarMap）不是 UFUNCTION(exec)，
// 命令行到不了。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraStarMapInputComponentWiringTest,
	"StellarEra.Pawns.StarMapInputComponentWiring",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraStarMapInputComponentWiringTest::RunTest(const FString& Parameters)
{
	const AStarMapCameraPawn* CameraCDO = GetDefault<AStarMapCameraPawn>();
	if (!TestNotNull(TEXT("星图相机 CDO"), CameraCDO))
	{
		return false;
	}

	const UStarMapCameraInputComponent* Input = CameraCDO->FindComponentByClass<UStarMapCameraInputComponent>();
	if (!TestNotNull(TEXT("相机 CDO 上有 UStarMapCameraInputComponent"), Input))
	{
		return false;
	}

	TestEqual(TEXT("默认灵敏度"), Input->LookSensitivity, 1.2f);
	TestFalse(TEXT("默认不反 Y"), Input->bInvertLookY);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
