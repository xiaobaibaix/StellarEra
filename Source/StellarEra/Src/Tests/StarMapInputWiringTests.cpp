// Copyright Voxel Plugin SAS. All Rights Reserved.
//
// 星图相机的键鼠输入真的接到了相机身上，而控制器不再绑视角、缩放、编辑与矿物透视键。
//
// 守的是这次搬运最容易哑掉的一环：`AStarMapCameraPawn::SetupPlayerInputComponent`
// 只是四行转发，整个 override 删掉、或里面的 `PlayerInput->Bind(...)` 被删，
// 都不会有编译错误，进星图后视角与滚轮直接没反应。
// CDO 断言（StellarEra.Pawns.StarMapInputComponentWiring）只证明组件存在，
// 证明不了它被 Bind 过 —— 这条补上。
//
// 覆盖不到：漏调 `Super::SetupPlayerInputComponent`。基类那句是空实现，删了本测试照绿。
//
// 不走 Possess：`AController::Possess` 会顺带动鼠标模式、遍历体素调用器，在测试世界里
// 全是噪音。`APawn::PossessedBy` 只设 `Controller` 指针，正是 `Bind` 唯一需要的东西。

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Components/InputComponent.h"
#include "EnhancedInputComponent.h"
#include "InputAction.h"

#include "StarMapCameraPawn.h"
#include "StellarEraPlayerController.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraStarMapInputWiringTest,
	"StellarEra.StarMap.InputWiring",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraStarMapInputWiringTest::RunTest(const FString& Parameters)
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

	AStellarEraPlayerController* Controller = World->SpawnActor<AStellarEraPlayerController>();
	AStarMapCameraPawn* Camera = World->SpawnActor<AStarMapCameraPawn>();
	if (!TestNotNull(TEXT("控制器"), Controller) || !TestNotNull(TEXT("星图相机"), Camera))
	{
		return false;
	}

	// 只设 Controller 指针：Bind 要的是 `Cast<IStellarEraInputSource>(Pawn->GetController())`。
	Camera->PossessedBy(Controller);

	UEnhancedInputComponent* Input = NewObject<UEnhancedInputComponent>(Camera);
	Camera->SetupPlayerInputComponent(Input);

	// Enhanced Input 不往 UInputComponent::ActionBindings 里塞东西，
	// 它有自己的 EnhancedActionEventBindings —— 读错容器会得到恒 0。
	const TArray<TUniquePtr<FEnhancedInputActionEventBinding>>& Bindings = Input->GetActionEventBindings();

	// 星图视角、编辑与矿物透视、选择与会话共二十二条：Look /
	// LookActivate(Started,Completed) / LookMiddle(Started,Completed) / Zoom /
	// EditModeToggle / BrushSize / MineralViewToggle / MineralFilter×9 /
	// MineralShowAll / StarMapSelect(Started,Completed) / StarMapToggle。
	// 少一条就是某个键静默失灵。
	if (!TestEqual(TEXT("星图相机绑定的动作数"), Bindings.Num(), 22))
	{
		return false;
	}

	int32 LookCount = 0;
	int32 LookActivateCount = 0;
	int32 LookMiddleCount = 0;
	int32 ZoomCount = 0;
	int32 EditModeCount = 0;
	int32 BrushSizeCount = 0;
	int32 MineralViewToggleCount = 0;
	int32 MineralFilterCount = 0;
	int32 MineralShowAllCount = 0;
	int32 StarMapSelectCount = 0;
	int32 StarMapToggleCount = 0;
	for (const TUniquePtr<FEnhancedInputActionEventBinding>& Binding : Bindings)
	{
		const UInputAction* Action = Binding ? Binding->GetAction() : nullptr;
		const FName ActionName = Action ? Action->GetFName() : NAME_None;
		LookCount += ActionName == TEXT("IA_StarMap_Look") ? 1 : 0;
		LookActivateCount += ActionName == TEXT("IA_StarMap_LookActivate") ? 1 : 0;
		LookMiddleCount += ActionName == TEXT("IA_StarMap_LookMiddle") ? 1 : 0;
		ZoomCount += ActionName == TEXT("IA_StarMap_Zoom") ? 1 : 0;
		EditModeCount += ActionName == TEXT("IA_StarMap_EditModeToggle") ? 1 : 0;
		BrushSizeCount += ActionName == TEXT("IA_StarMap_BrushSize") ? 1 : 0;
		MineralViewToggleCount += ActionName == TEXT("IA_StarMap_MineralViewToggle") ? 1 : 0;
		MineralFilterCount += ActionName.ToString().StartsWith(TEXT("IA_StarMap_MineralFilter")) ? 1 : 0;
		MineralShowAllCount += ActionName == TEXT("IA_StarMap_MineralShowAll") ? 1 : 0;
		StarMapSelectCount += ActionName == TEXT("IA_StarMap_Select") ? 1 : 0;
		StarMapToggleCount += ActionName == TEXT("IA_StarMap_StarMapToggle") ? 1 : 0;
	}

	TestEqual(TEXT("IA_StarMap_Look 绑定数"), LookCount, 1);
	TestEqual(TEXT("IA_StarMap_LookActivate 绑定数（按住 / 松开）"), LookActivateCount, 2);
	TestEqual(TEXT("IA_StarMap_LookMiddle 绑定数（按住 / 松开）"), LookMiddleCount, 2);
	TestEqual(TEXT("IA_StarMap_Zoom 绑定数"), ZoomCount, 1);
	TestEqual(TEXT("IA_StarMap_EditModeToggle 绑定数"), EditModeCount, 1);
	TestEqual(TEXT("IA_StarMap_BrushSize 绑定数"), BrushSizeCount, 1);
	TestEqual(TEXT("IA_StarMap_MineralViewToggle 绑定数"), MineralViewToggleCount, 1);
	TestEqual(TEXT("IA_StarMap_MineralFilter1..9 绑定数"), MineralFilterCount, 9);
	TestEqual(TEXT("IA_StarMap_MineralShowAll 绑定数"), MineralShowAllCount, 1);
	TestEqual(TEXT("IA_StarMap_Select 绑定数（按住 / 松开）"), StarMapSelectCount, 2);
	TestEqual(TEXT("IA_StarMap_StarMapToggle 绑定数"), StarMapToggleCount, 1);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
