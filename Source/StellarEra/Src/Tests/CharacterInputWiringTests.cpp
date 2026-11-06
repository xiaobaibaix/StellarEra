// Copyright Voxel Plugin SAS. All Rights Reserved.
//
// 角色的键鼠输入真的接到了角色身上，且编辑键（G / 笔刷尺寸）也在其中。
//
// 守的是这次搬运最容易哑掉的一环：`APlanetCharacter::SetupPlayerInputComponent`
// 只是转发，整个 override 删掉、或组件 `Bind` 里少一行 `BindAction`，都不会有
// 编译错误 —— 进游戏后那个键直接没反应。CDO 断言
// （StellarEra.Pawns.InputComponentWiring）只证明组件存在，证明不了它绑了什么。
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

#include "PlanetCharacter.h"
#include "StellarEraPlayerController.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraCharacterInputWiringTest,
	"StellarEra.Character.InputWiring",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraCharacterInputWiringTest::RunTest(const FString& Parameters)
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
	APlanetCharacter* Character = World->SpawnActor<APlanetCharacter>();
	if (!TestNotNull(TEXT("控制器"), Controller) || !TestNotNull(TEXT("角色"), Character))
	{
		return false;
	}

	// 只设 Controller 指针：Bind 要的是 `Cast<IStellarEraInputSource>(Pawn->GetController())`。
	Character->PossessedBy(Controller);

	UEnhancedInputComponent* Input = NewObject<UEnhancedInputComponent>(Character);
	Character->SetupPlayerInputComponent(Input);

	// Enhanced Input 不往 UInputComponent::ActionBindings 里塞东西，
	// 它有自己的 EnhancedActionEventBindings —— 读错容器会得到恒 0。
	const TArray<TUniquePtr<FEnhancedInputActionEventBinding>>& Bindings = Input->GetActionEventBindings();

	// 十三条：Move / Look / LookActivate(Started,Completed) / Jump /
	// Sprint(Started,Completed) / Zoom / EditModeToggle / BrushSize，
	// 加角色表自己的 StarMapToggle / Interact / EditPrimary(Started)。
	// 少一条就是某个键静默失灵。
	if (!TestEqual(TEXT("角色绑定的动作数"), Bindings.Num(), 13))
	{
		return false;
	}

	int32 MoveCount = 0;
	int32 LookCount = 0;
	int32 LookActivateCount = 0;
	int32 JumpCount = 0;
	int32 SprintCount = 0;
	int32 ZoomCount = 0;
	int32 EditModeCount = 0;
	int32 BrushSizeCount = 0;
	int32 StarMapToggleCount = 0;
	int32 InteractCount = 0;
	int32 StarMapSelectCount = 0;
	for (const TUniquePtr<FEnhancedInputActionEventBinding>& Binding : Bindings)
	{
		const UInputAction* Action = Binding ? Binding->GetAction() : nullptr;
		const FName ActionName = Action ? Action->GetFName() : NAME_None;
		MoveCount += ActionName == TEXT("IA_Character_Move") ? 1 : 0;
		LookCount += ActionName == TEXT("IA_Character_Look") ? 1 : 0;
		LookActivateCount += ActionName == TEXT("IA_Character_LookActivate") ? 1 : 0;
		JumpCount += ActionName == TEXT("IA_Character_Jump") ? 1 : 0;
		SprintCount += ActionName == TEXT("IA_Character_Sprint") ? 1 : 0;
		ZoomCount += ActionName == TEXT("IA_Character_Zoom") ? 1 : 0;
		EditModeCount += ActionName == TEXT("IA_Character_EditModeToggle") ? 1 : 0;
		BrushSizeCount += ActionName == TEXT("IA_Character_BrushSize") ? 1 : 0;
		StarMapToggleCount += ActionName == TEXT("IA_Character_StarMapToggle") ? 1 : 0;
		InteractCount += ActionName == TEXT("IA_Character_Interact") ? 1 : 0;
		StarMapSelectCount += ActionName == TEXT("IA_Character_EditPrimary") ? 1 : 0;
	}

	TestEqual(TEXT("IA_Character_Move 绑定数"), MoveCount, 1);
	TestEqual(TEXT("IA_Character_Look 绑定数"), LookCount, 1);
	TestEqual(TEXT("IA_Character_LookActivate 绑定数（按住 / 松开）"), LookActivateCount, 2);
	TestEqual(TEXT("IA_Character_Jump 绑定数"), JumpCount, 1);
	TestEqual(TEXT("IA_Character_Sprint 绑定数（按住 / 松开）"), SprintCount, 2);
	TestEqual(TEXT("IA_Character_Zoom 绑定数"), ZoomCount, 1);
	TestEqual(TEXT("IA_Character_EditModeToggle 绑定数"), EditModeCount, 1);
	TestEqual(TEXT("IA_Character_BrushSize 绑定数"), BrushSizeCount, 1);
	TestEqual(TEXT("IA_Character_StarMapToggle 绑定数"), StarMapToggleCount, 1);
	TestEqual(TEXT("IA_Character_Interact 绑定数"), InteractCount, 1);
	TestEqual(TEXT("IA_Character_EditPrimary 绑定数（只有 Started）"), StarMapSelectCount, 1);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
