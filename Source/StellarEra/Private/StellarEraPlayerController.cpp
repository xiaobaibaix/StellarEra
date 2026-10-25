// Copyright Voxel Plugin SAS. All Rights Reserved.

#include "StellarEraPlayerController.h"

#include "PlanetCharacter.h"
#include "PlanetMineralViewComponent.h"
#include "StarMapUISystem.h"
#include "PlanetEditBrush.h"
#include "PlanetEditComponent.h"
#include "PlanetEditUI.h"
#include "SpaceshipPawn.h"
#include "StellarEraHUD.h"
#include "CosmosSimulationSubsystem.h"
#include "StarMapCameraPawn.h"
#include "VoxelPlanetActor.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "EngineUtils.h"

namespace
{
	/** 造一个运行时输入动作。ValueType 决定回调里 Get<T>() 的模板参数。 */
	UInputAction* MakeAction(UObject* Outer, const TCHAR* Name, EInputActionValueType ValueType)
	{
		UInputAction* Action = NewObject<UInputAction>(Outer, Name);
		Action->ValueType = ValueType;
		return Action;
	}

	/** 把一个按键映射到动作，并按顺序挂上修饰器。 */
	void MapKey(
		UInputMappingContext* Context,
		UInputAction* Action,
		const FKey& Key,
		const TArray<UInputModifier*>& Modifiers = TArray<UInputModifier*>())
	{
		FEnhancedActionKeyMapping& Mapping = Context->MapKey(Action, Key);
		for (UInputModifier* Modifier : Modifiers)
		{
			Mapping.Modifiers.Add(Modifier);
		}
	}

	/**
	 * 左键「点击」与「拖拽」的分界（像素）。
	 *
	 * 同一个左键既选中又转视角，靠按下与松开的位移区分：小于它算点击（拾取），
	 * 否则算拖拽结束（不改选中）。太空里天体在动，手抖几个像素很正常，
	 * 阈值太小会把「想选中」判成「想转视角」。
	 */
	constexpr double ClickMoveThresholdPx = 5.0;

	UInputModifier* MakeNegate(UObject* Outer)
	{
		return NewObject<UInputModifierNegate>(Outer);
	}

	/** Swizzle(YXZ) 把按键的 X 轴值搬到 Y 轴，用于把 WASD 合成一个二维向量。 */
	UInputModifier* MakeSwizzleYXZ(UObject* Outer)
	{
		UInputModifierSwizzleAxis* Swizzle = NewObject<UInputModifierSwizzleAxis>(Outer);
		Swizzle->Order = EInputAxisSwizzle::YXZ;
		return Swizzle;
	}

	/** 任意 Pawn 上的编辑组件，不判模式。角色与星图相机各有一个。 */
	UPlanetEditComponent* GetEditComponentForPawn(APawn* InPawn)
	{
		if (APlanetCharacter* PawnCharacter = Cast<APlanetCharacter>(InPawn))
		{
			return PawnCharacter->GetPlanetEditComponent();
		}
		if (AStarMapCameraPawn* StarMap = Cast<AStarMapCameraPawn>(InPawn))
		{
			return StarMap->GetPlanetEditComponent();
		}
		return nullptr;
	}
}

AStellarEraPlayerController::AStellarEraPlayerController()
{
	// 输入组件由 DefaultInput.ini 指定为 EnhancedInput 版本；这里不额外设置。
	// 若 DefaultInput.ini 缺失，Cast<UEnhancedInputComponent> 会失败。
}

void AStellarEraPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();

	BuildInputAssets();

	UEnhancedInputComponent* EnhancedInput = Cast<UEnhancedInputComponent>(InputComponent);
	if (!EnhancedInput)
	{
		UE_LOG(LogTemp, Error,
			TEXT("AStellarEraPlayerController: InputComponent 不是 UEnhancedInputComponent，"
				 "请检查 Config/DefaultInput.ini 的 DefaultInputComponentClass。"));
		return;
	}

	EnhancedInput->BindAction(IA_Move, ETriggerEvent::Triggered, this, &AStellarEraPlayerController::OnMove);
	EnhancedInput->BindAction(IA_Look, ETriggerEvent::Triggered, this, &AStellarEraPlayerController::OnLook);
	EnhancedInput->BindAction(IA_LookActivate, ETriggerEvent::Started, this, &AStellarEraPlayerController::OnLookActivateStarted);
	EnhancedInput->BindAction(IA_LookActivate, ETriggerEvent::Completed, this, &AStellarEraPlayerController::OnLookActivateCompleted);
	EnhancedInput->BindAction(IA_Jump, ETriggerEvent::Started, this, &AStellarEraPlayerController::OnJump);
	EnhancedInput->BindAction(IA_Sprint, ETriggerEvent::Started, this, &AStellarEraPlayerController::OnSprintStarted);
	EnhancedInput->BindAction(IA_Sprint, ETriggerEvent::Completed, this, &AStellarEraPlayerController::OnSprintCompleted);

	EnhancedInput->BindAction(IA_Thrust, ETriggerEvent::Triggered, this, &AStellarEraPlayerController::OnThrust);
	EnhancedInput->BindAction(IA_Thrust, ETriggerEvent::Completed, this, &AStellarEraPlayerController::OnThrust);
	EnhancedInput->BindAction(IA_Roll, ETriggerEvent::Triggered, this, &AStellarEraPlayerController::OnRoll);
	EnhancedInput->BindAction(IA_Roll, ETriggerEvent::Completed, this, &AStellarEraPlayerController::OnRoll);
	EnhancedInput->BindAction(IA_Vertical, ETriggerEvent::Triggered, this, &AStellarEraPlayerController::OnVertical);
	EnhancedInput->BindAction(IA_Vertical, ETriggerEvent::Completed, this, &AStellarEraPlayerController::OnVertical);

	EnhancedInput->BindAction(IA_Boost, ETriggerEvent::Started, this, &AStellarEraPlayerController::OnBoostStarted);
	EnhancedInput->BindAction(IA_Boost, ETriggerEvent::Completed, this, &AStellarEraPlayerController::OnBoostCompleted);

	// 只绑 Started：按住 Tab 只切一次，不会随按键重复翻转状态。
	EnhancedInput->BindAction(IA_Cruise, ETriggerEvent::Started, this, &AStellarEraPlayerController::OnCruise);

	// 只绑 Triggered：滚轮每格是一次独立冲量，不滚就不产生值，没有 Completed 可收。
	EnhancedInput->BindAction(IA_Zoom, ETriggerEvent::Triggered, this, &AStellarEraPlayerController::OnZoom);

	EnhancedInput->BindAction(IA_StarMapToggle, ETriggerEvent::Started,
		this, &AStellarEraPlayerController::ToggleStarMap);
	EnhancedInput->BindAction(IA_StarMapSelect, ETriggerEvent::Started,
		this, &AStellarEraPlayerController::OnStarMapSelectStarted);
	EnhancedInput->BindAction(IA_StarMapSelect, ETriggerEvent::Completed,
		this, &AStellarEraPlayerController::OnStarMapSelectCompleted);

	EnhancedInput->BindAction(IA_EditModeToggle, ETriggerEvent::Started,
		this, &AStellarEraPlayerController::OnEditModeToggle);

	// 只绑 Started：Axis1D 的 Triggered 在按住时每帧触发一次，笔刷会瞬间冲到上限。
	EnhancedInput->BindAction(IA_BrushSize, ETriggerEvent::Started, this,
		&AStellarEraPlayerController::OnBrushSize);

	EnhancedInput->BindAction(IA_Interact, ETriggerEvent::Started, this, &AStellarEraPlayerController::OnInteract);

	EnhancedInput->BindAction(IA_MineralViewToggle, ETriggerEvent::Started,
		this, &AStellarEraPlayerController::OnMineralViewToggle);

	// 数字键用变参 payload：同一个回调按下标分派，省掉九个一模一样的函数。
	for (int32 ElementIndex = 0; ElementIndex < IA_MineralFilter.Num(); ++ElementIndex)
	{
		EnhancedInput->BindAction(IA_MineralFilter[ElementIndex], ETriggerEvent::Started,
			this, &AStellarEraPlayerController::OnMineralFilter, ElementIndex);
	}
	EnhancedInput->BindAction(IA_MineralShowAll, ETriggerEvent::Started,
		this, &AStellarEraPlayerController::OnMineralShowAll);
}

void AStellarEraPlayerController::BeginPlay()
{
	Super::BeginPlay();

	// SetupInputComponent 通常先于 BeginPlay 跑完，但这里再兜一次底。
	BuildInputAssets();

	if (UEnhancedInputLocalPlayerSubsystem* Subsystem =
		ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer()))
	{
		Subsystem->AddMappingContext(MappingContext, /*Priority=*/0);
	}
}

void AStellarEraPlayerController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);

	// 换 Pawn（登船 / 离船 / 进出星图 / DebugPossessShip）一律退出矿物透视。
	ExitMineralViewIfActive();

	ApplyMouseModeForPawn(InPawn);
}

void AStellarEraPlayerController::ApplyMouseModeForPawn(APawn* InPawn)
{
	// 换 Pawn 时把两个「按住」标志都清零。按着键登船/离船/进星图的话，松开事件
	// 会送到新 Pawn 身上，标志就永远停在 true —— 角色一下船就自己转视角，
	// 或者星图里没按左键也在转视角。
	bLookHeld = false;
	bStarMapSelectHeld = false;
	bEditRightPressValid = false;

	const bool bWalking = Cast<APlanetCharacter>(InPawn) != nullptr;
	const bool bStarMap = Cast<AStarMapCameraPawn>(InPawn) != nullptr;

	// 走路时光标常驻。飞船里保持隐藏：鼠标就是它的操纵杆，藏起来才像在驾驶。
	// 星图里光标也要可见可点。
	bShowMouseCursor = bWalking || bStarMap;

	UGameViewportClient* ViewportClient = GetWorld() ? GetWorld()->GetGameViewport() : nullptr;
	if (!ViewportClient)
	{
		return;
	}

	if (bWalking)
	{
		// SetInputMode 一次做三件这里都必需、手写又容易漏的事：
		//
		//   1. SlateOperations.ReleaseMouseCapture() —— 从飞船里下来时视口还攥着
		//      永久捕获，不放掉光标就被锁在窗口里，看得见也动不了。SetMouseCaptureMode
		//      单独调是没用的，它只改一个枚举，不会松开已经握在手里的捕获。
		//   2. SetHideCursorDuringCapture(false) —— **光标能看见的原因就是这一条**。
		//      引擎在捕获鼠标时会把光标藏掉：FSceneViewport::AcquireFocusAndCapture
		//      判断 HideCursorDuringCapture() 且 ShouldShowMouseCursor()，两个都为真
		//      才藏。光把 bShowMouseCursor 设成 true 不够，捕获一发生光标照样消失，
		//      而 CaptureDuringRightMouseDown 下按住右键一定会捕获。
		//   3. SetMouseCaptureMode(CaptureDuringMouseDown) —— 紧跟着改成右键版。
		//
		// 锁鼠标用 LockOnCapture：只有按住右键时才会锁，而那正是需要无限旋转的时候；
		// 不按的时候光标自由。
		SetInputMode(FInputModeGameAndUI()
			.SetLockMouseToViewportBehavior(EMouseLockMode::LockOnCapture)
			.SetHideCursorDuringCapture(false));

		// 只有按住右键才捕获。**捕获是必需的，不是可选的美化**：
		// FSceneViewport::OnMouseMove 只在视口握着鼠标捕获时才累加位移，没有捕获就
		// 没有 Mouse2D，相机收不到任何输入 —— 那样右键"按住了也没反应"。
		// 反过来，CaptureDuringRightMouseDown 让 OnMouseButtonDown 里的 bTemporaryCapture
		// 在右键按下时为真，于是同一次右键既被送进 InputKey（触发 IA_LookActivate）、
		// 又触发 AcquireFocusAndCapture 拿到位移，松开时再自动放掉。
		ViewportClient->SetMouseCaptureMode(EMouseCaptureMode::CaptureDuringRightMouseDown);
	}
	else if (bStarMap)
	{
		// 星图：光标要可见可点，而且**必须有捕获** —— SceneViewport 里 MouseDelta 的
		// 累加整段包在 if (bViewportHasCapture) 里（SceneViewport.cpp:905 判定、
		// :923-933 累加），没有捕获就收不到任何位移，左键拖拽等于没接上。
		// CaptureDuringMouseDown 正是为此存在的枚举：「按住鼠标时捕获，松开释放」
		// （EngineBaseTypes.h:49）。角色用的是右键版，星图要任意键版。
		SetInputMode(FInputModeGameAndUI()
			.SetLockMouseToViewportBehavior(EMouseLockMode::LockOnCapture)
			.SetHideCursorDuringCapture(false));
		ViewportClient->SetMouseCaptureMode(EMouseCaptureMode::CaptureDuringMouseDown);
	}
	else
	{
		// 飞船：永久捕获 + 光标隐藏。鼠标是它的操纵杆，位移必须一直有值。
		// GameOnly 自己会把捕获模式设成 CapturePermanently*，但不管
		// HideCursorDuringCapture，所以这里显式写回引擎默认值，免得被上面的
		// 角色模式留下的 false 带过去。
		SetInputMode(FInputModeGameOnly());
		ViewportClient->SetHideCursorDuringCapture(true);
	}
}

void AStellarEraPlayerController::BuildInputAssets()
{
	if (MappingContext)
	{
		return;
	}

	MappingContext = NewObject<UInputMappingContext>(this, TEXT("IMC_StellarEra"));

	IA_Move = MakeAction(this, TEXT("IA_Move"), EInputActionValueType::Axis2D);
	IA_Look = MakeAction(this, TEXT("IA_Look"), EInputActionValueType::Axis2D);
	IA_LookActivate = MakeAction(this, TEXT("IA_LookActivate"), EInputActionValueType::Boolean);
	IA_Jump = MakeAction(this, TEXT("IA_Jump"), EInputActionValueType::Boolean);
	IA_Sprint = MakeAction(this, TEXT("IA_Sprint"), EInputActionValueType::Boolean);

	IA_Thrust = MakeAction(this, TEXT("IA_Thrust"), EInputActionValueType::Axis1D);
	IA_Roll = MakeAction(this, TEXT("IA_Roll"), EInputActionValueType::Axis1D);
	IA_Vertical = MakeAction(this, TEXT("IA_Vertical"), EInputActionValueType::Axis1D);
	IA_Boost = MakeAction(this, TEXT("IA_Boost"), EInputActionValueType::Boolean);
	IA_Cruise = MakeAction(this, TEXT("IA_Cruise"), EInputActionValueType::Boolean);
	IA_Zoom = MakeAction(this, TEXT("IA_Zoom"), EInputActionValueType::Axis1D);
	IA_Interact = MakeAction(this, TEXT("IA_Interact"), EInputActionValueType::Boolean);
	IA_StarMapToggle = MakeAction(this, TEXT("IA_StarMapToggle"), EInputActionValueType::Boolean);
	IA_StarMapSelect = MakeAction(this, TEXT("IA_StarMapSelect"), EInputActionValueType::Boolean);
	IA_EditModeToggle = MakeAction(this, TEXT("IA_EditModeToggle"), EInputActionValueType::Boolean);
	IA_MineralViewToggle = MakeAction(this, TEXT("IA_MineralViewToggle"), EInputActionValueType::Boolean);
	IA_MineralShowAll = MakeAction(this, TEXT("IA_MineralShowAll"), EInputActionValueType::Boolean);
	IA_MineralFilter.Reset();
	for (int32 Slot = 1; Slot <= 9; ++Slot)
	{
		IA_MineralFilter.Add(MakeAction(this,
			*FString::Printf(TEXT("IA_MineralFilter%d"), Slot),
			EInputActionValueType::Boolean));
	}
	IA_BrushSize = MakeAction(this, TEXT("IA_BrushSize"), EInputActionValueType::Axis1D);

	// WASD 合成一个二维向量：W/S 走 X 轴，A/D 先用 Swizzle 搬到 Y 轴再取反。
	// 修饰器按数组顺序依次作用，所以 A 是“先 Swizzle 再 Negate”。
	MapKey(MappingContext, IA_Move, EKeys::W);
	MapKey(MappingContext, IA_Move, EKeys::S, { MakeNegate(this) });
	MapKey(MappingContext, IA_Move, EKeys::D, { MakeSwizzleYXZ(this) });
	MapKey(MappingContext, IA_Move, EKeys::A, { MakeSwizzleYXZ(this), MakeNegate(this) });

	// 鼠标位移本身一直有值，能不能用由 IA_LookActivate（右键）在 OnLook 里把关。
	MapKey(MappingContext, IA_Look, EKeys::Mouse2D);
	MapKey(MappingContext, IA_LookActivate, EKeys::RightMouseButton);

	MapKey(MappingContext, IA_Jump, EKeys::SpaceBar);
	MapKey(MappingContext, IA_Sprint, EKeys::LeftShift);

	// 飞船：W 推力 / S 制动，D/A 横滚，Space/LeftControl 升降，LeftShift 加速，Tab 巡航。
	// 与角色的 WASD、LeftShift 共用按键没问题——同一个 IMC 里的动作互不吞输入，
	// 分派由 Pawn 类型决定（IA_Sprint 只认 APlanetCharacter，IA_Boost 只认飞船）。
	MapKey(MappingContext, IA_Thrust, EKeys::W);
	MapKey(MappingContext, IA_Thrust, EKeys::S, { MakeNegate(this) });
	MapKey(MappingContext, IA_Roll, EKeys::D);
	MapKey(MappingContext, IA_Roll, EKeys::A, { MakeNegate(this) });
	MapKey(MappingContext, IA_Vertical, EKeys::SpaceBar);
	MapKey(MappingContext, IA_Vertical, EKeys::LeftControl, { MakeNegate(this) });
	MapKey(MappingContext, IA_Boost, EKeys::LeftShift);
	MapKey(MappingContext, IA_Cruise, EKeys::Tab);

	// 滚轮是轴键（每格 ±1），不是按键，所以映射到 Axis1D 的动作上直接取值。
	// 不需要任何修饰器：方向约定（上滚 = 拉近）由 ASpaceshipPawn::AddZoom 负责。
	MapKey(MappingContext, IA_Zoom, EKeys::MouseWheelAxis);

	MapKey(MappingContext, IA_Interact, EKeys::E);
	MapKey(MappingContext, IA_StarMapToggle, EKeys::M);
	MapKey(MappingContext, IA_StarMapSelect, EKeys::LeftMouseButton);
	MapKey(MappingContext, IA_EditModeToggle, EKeys::G);

	// 矿物透视：H 进出；数字键 1..9 切换前 9 个元素，0 全部打开。
	MapKey(MappingContext, IA_MineralViewToggle, EKeys::H);

	const FKey DigitKeys[] = {
		EKeys::One, EKeys::Two, EKeys::Three, EKeys::Four, EKeys::Five,
		EKeys::Six, EKeys::Seven, EKeys::Eight, EKeys::Nine
	};
	for (int32 Slot = 0; Slot < IA_MineralFilter.Num(); ++Slot)
	{
		MapKey(MappingContext, IA_MineralFilter[Slot], DigitKeys[Slot]);
	}
	MapKey(MappingContext, IA_MineralShowAll, EKeys::Zero);

	// 笔刷尺寸：星图里滚轮已归缩放（角色编辑模式下滚轮同样调笔刷），尺寸因此另占
	// 一对键，两个模式都能用。左括号接 Negate，于是 ] = +1、[ = −1。
	MapKey(MappingContext, IA_BrushSize, EKeys::RightBracket);
	MapKey(MappingContext, IA_BrushSize, EKeys::LeftBracket, { MakeNegate(this) });
}

UPlanetEditComponent* AStellarEraPlayerController::GetEditComponentInEditMode() const
{
	UPlanetEditComponent* Edit = GetEditComponentForPawn(GetPawn());
	return Edit && Edit->IsEditMode() ? Edit : nullptr;
}

UPlanetEditComponent* AStellarEraPlayerController::GetCharacterEditComponentInEditMode() const
{
	APlanetCharacter* PawnCharacter = Cast<APlanetCharacter>(GetPawn());
	UPlanetEditComponent* Edit = PawnCharacter ? PawnCharacter->GetPlanetEditComponent() : nullptr;
	return Edit && Edit->IsEditMode() ? Edit : nullptr;
}

void AStellarEraPlayerController::ForceExitEditMode(APawn* InPawn)
{
	// 形参不能叫 Pawn：AController 自己有个 Pawn 成员，遮蔽会被当成错误（C4458）。
	UPlanetEditComponent* Edit = GetEditComponentForPawn(InPawn);
	if (Edit && Edit->IsEditMode())
	{
		Edit->SetEditMode(false);
	}
}

void AStellarEraPlayerController::OnMove(const FInputActionValue& Value)
{
	// 在飞船里时 WASD 归 IA_Thrust / IA_Roll 管，这里直接不响应。
	// 局部变量不能叫 Character：那是 AController 的成员，UE 把变量遮蔽当错误（C4458）。
	if (APlanetCharacter* PawnCharacter = Cast<APlanetCharacter>(GetPawn()))
	{
		PawnCharacter->Move(Value.Get<FVector2D>());
	}
}

void AStellarEraPlayerController::OnLook(const FInputActionValue& Value)
{
	const FVector2D Axis = Value.Get<FVector2D>();
	const float DeltaYaw = Axis.X * LookSensitivity;
	const float DeltaPitch = (bInvertLookY ? -Axis.Y : Axis.Y) * LookSensitivity;

	if (APlanetCharacter* PawnCharacter = Cast<APlanetCharacter>(GetPawn()))
	{
		// 角色的视角只在按住右键时跟鼠标动。不按的时候鼠标划过屏幕不带画面，
		// 角色自己拐弯（A/D 横移时 bOrientRotationToMovement 转的身）也不会把
		// 相机拖走 —— 想转视角就按住右键。
		if (bLookHeld)
		{
			PawnCharacter->Look(DeltaYaw, DeltaPitch);
		}
	}
	else if (ASpaceshipPawn* Ship = Cast<ASpaceshipPawn>(GetPawn()))
	{
		// 飞船不设这道门：鼠标就是它的操纵杆，要按住右键才能转向等于飞不动。
		Ship->Look(DeltaYaw, DeltaPitch);
	}
	else if (AStarMapCameraPawn* StarMap = Cast<AStarMapCameraPawn>(GetPawn()))
	{
		// 星图里左键拖拽就是转视角，不设 bLookHeld 那道门 —— 那道门是给角色
		// 「不按右键就别动视角」用的；星图的相机除了转视角没有别的职责。
		//
		// 矿物透视是唯一例外：左键归图例点击，视角只认右键（与角色同一套门）。
		if (ActiveMineralView && !bLookHeld)
		{
			return;
		}
		StarMap->OrbitLook(DeltaYaw, DeltaPitch);
	}
}

void AStellarEraPlayerController::OnLookActivateStarted(const FInputActionValue& Value)
{
	bLookHeld = true;

	// 只认角色：星图里右键是转视角，通用版会让松开右键变成「放」。
	UPlanetEditComponent* Edit = GetCharacterEditComponentInEditMode();
	if (!Edit)
	{
		return;
	}

	// 右键按住期间预览切到「放」，与松开右键时 PlaceAtScreenPosition 的中心一致。
	Edit->SetPreviewPlacing(true);

	double MouseX = 0.0;
	double MouseY = 0.0;
	bEditRightPressValid = GetMousePosition(MouseX, MouseY);
	if (bEditRightPressValid)
	{
		EditRightPressPosition = FVector2D(MouseX, MouseY);
	}
}

void AStellarEraPlayerController::OnLookActivateCompleted(const FInputActionValue& Value)
{
	bLookHeld = false;

	// 只认角色：星图里右键是转视角，通用版会让松开右键变成「放」。
	UPlanetEditComponent* Edit = GetCharacterEditComponentInEditMode();
	// 松开右键后预览回到「挖」。必须在下面的早退**之前**复位：没记下按下位置
	// （bEditRightPressValid 为 false）时也要复位，否则预览会一直停在「放」。
	if (Edit)
	{
		Edit->SetPreviewPlacing(false);
	}

	if (!bEditRightPressValid)
	{
		return;
	}
	bEditRightPressValid = false;

	if (!Edit)
	{
		return;
	}

	double MouseX = 0.0;
	double MouseY = 0.0;
	if (!GetMousePosition(MouseX, MouseY))
	{
		return;
	}

	// 与星图的左键点击共用同一个阈值：按住右键拖拽是转视角，松开时若几乎
	// 没动才算「点放」。拖动中相机转了，光标回到原点附近也不会误解成点击，
	// 因为玩家在拖动时不会刻意回到起点。
	const double MovedSq = FVector2D::DistSquared(
		EditRightPressPosition, FVector2D(MouseX, MouseY));
	if (MovedSq > ClickMoveThresholdPx * ClickMoveThresholdPx)
	{
		return;
	}

	Edit->PlaceAtScreenPosition(FVector2D(MouseX, MouseY));
}

void AStellarEraPlayerController::OnJump(const FInputActionValue& Value)
{
	if (APlanetCharacter* PawnCharacter = Cast<APlanetCharacter>(GetPawn()))
	{
		PawnCharacter->Jump();
	}
}

void AStellarEraPlayerController::OnSprintStarted(const FInputActionValue& Value)
{
	if (APlanetCharacter* PawnCharacter = Cast<APlanetCharacter>(GetPawn()))
	{
		PawnCharacter->SetSprinting(true);
	}
}

void AStellarEraPlayerController::OnSprintCompleted(const FInputActionValue& Value)
{
	if (APlanetCharacter* PawnCharacter = Cast<APlanetCharacter>(GetPawn()))
	{
		PawnCharacter->SetSprinting(false);
	}
}

void AStellarEraPlayerController::OnThrust(const FInputActionValue& Value)
{
	// Completed 事件里 Value 是 0，正好用来在松手时归零。
	if (ASpaceshipPawn* Ship = Cast<ASpaceshipPawn>(GetPawn()))
	{
		Ship->SetThrust(Value.Get<float>());
	}
}

void AStellarEraPlayerController::OnRoll(const FInputActionValue& Value)
{
	if (ASpaceshipPawn* Ship = Cast<ASpaceshipPawn>(GetPawn()))
	{
		Ship->SetRoll(Value.Get<float>());
	}
}

void AStellarEraPlayerController::OnVertical(const FInputActionValue& Value)
{
	if (ASpaceshipPawn* Ship = Cast<ASpaceshipPawn>(GetPawn()))
	{
		Ship->SetVertical(Value.Get<float>());
	}
}

void AStellarEraPlayerController::OnBoostStarted(const FInputActionValue& Value)
{
	if (ASpaceshipPawn* Ship = Cast<ASpaceshipPawn>(GetPawn()))
	{
		Ship->SetBoost(true);
	}
}

void AStellarEraPlayerController::OnBoostCompleted(const FInputActionValue& Value)
{
	// Completed 不判 Pawn 类型：松开 Shift 时必须无条件清掉加速状态。
	// 若这里也 Cast 一次，玩家在按着 Shift 时下船（或 Possess 换了对象），
	// 松开的那一下会被丢掉，飞船的 bBoosting 就永远卡在 true。
	if (ASpaceshipPawn* Ship = Cast<ASpaceshipPawn>(GetPawn()))
	{
		Ship->SetBoost(false);
	}
}

void AStellarEraPlayerController::OnCruise(const FInputActionValue& Value)
{
	ASpaceshipPawn* Ship = Cast<ASpaceshipPawn>(GetPawn());
	if (!Ship)
	{
		return;
	}

	Ship->TogglePreserveVelocity();

	// 切换本身在画面上不留任何痕迹（速度保持与否要飞一段才感觉得到），所以弹一条
	// 提示。没有它，玩家按了 Tab 只能靠"好像还在飞"来猜，按没按到都分不清。
	if (AStellarEraHUD* StellarHUD = Cast<AStellarEraHUD>(GetHUD()))
	{
		StellarHUD->ShowNotice(Ship->IsPreserveVelocity() ? TEXT("巡航：开") : TEXT("巡航：关"));
	}
}

void AStellarEraPlayerController::OnZoom(const FInputActionValue& Value)
{
	const float Scroll = Value.Get<float>();

	if (APlanetCharacter* PawnCharacter = Cast<APlanetCharacter>(GetPawn()))
	{
		UPlanetEditComponent* Edit = GetEditComponentInEditMode();
		if (Edit)
		{
			// 编辑模式下滚轮改笔刷尺寸，相机臂长保持不动 —— 挖洞时要盯着脚下，
			// 顺手把镜头拉远/拉近会打断瞄准。
			if (!FMath::IsNearlyZero(Scroll))
			{
				Edit->AdjustBrushSize(Scroll > 0.f ? 1 : -1);
			}
			return;
		}

		PawnCharacter->AddZoom(Scroll);
	}
	else if (ASpaceshipPawn* Ship = Cast<ASpaceshipPawn>(GetPawn()))
	{
		Ship->AddZoom(Scroll);
	}
	else if (AStarMapCameraPawn* StarMap = Cast<AStarMapCameraPawn>(GetPawn()))
	{
		StarMap->AddZoom(Scroll);
	}
}

void AStellarEraPlayerController::DebugPossessShip()
{
	// 已经在飞船里就换回角色，否则找一艘最近的飞船坐进去。
	if (ASpaceshipPawn* CurrentShip = Cast<ASpaceshipPawn>(GetPawn()))
	{
		// 真登船（走 E 流程）时不接管：角色此时是隐藏、附着在飞船上、MOVE_None 的，
		// 直接 possess 回来会让飞船的 BoardedCharacter 仍然指着它，状态不一致。
		// 这种情况请按 E 走正常的离船流程。
		if (CurrentShip->GetBoardedCharacter())
		{
			UE_LOG(LogTemp, Warning,
				TEXT("AStellarEraPlayerController: 角色已正常登船，请按 E 离船，"
					 "不要用 DebugPossessShip 切回。"));
			return;
		}

		for (TActorIterator<APlanetCharacter> It(GetWorld()); It; ++It)
		{
			Possess(*It);
			return;
		}
		return;
	}

	APawn* CurrentPawn = GetPawn();
	if (!CurrentPawn)
	{
		return;
	}

	ASpaceshipPawn* NearestShip = nullptr;
	float NearestDistSq = TNumericLimits<float>::Max();
	for (TActorIterator<ASpaceshipPawn> It(GetWorld()); It; ++It)
	{
		const float DistSq = FVector::DistSquared(It->GetActorLocation(), CurrentPawn->GetActorLocation());
		if (DistSq < NearestDistSq)
		{
			NearestDistSq = DistSq;
			NearestShip = *It;
		}
	}

	if (NearestShip)
	{
		ForceExitEditMode(GetPawn());

		Possess(NearestShip);
	}
}

bool AStellarEraPlayerController::EnterNearestShip()
{
	// 开局这一刻玩家还应该是角色。不是的话（比如已经手动切进飞船）就不重复登船。
	APlanetCharacter* PawnCharacter = Cast<APlanetCharacter>(GetPawn());
	if (!PawnCharacter)
	{
		return false;
	}

	ASpaceshipPawn* Ship = ASpaceshipPawn::FindNearestTo(GetWorld(), PawnCharacter->GetActorLocation());
	if (!Ship)
	{
		return false;
	}

	BoardShip(Ship);
	return true;
}

void AStellarEraPlayerController::PlayerTick(float DeltaTime)
{
	Super::PlayerTick(DeltaTime);

	// 矿物透视绑在「选中的那颗行星」上。焦点被改选、或 Pawn 已经不是星图相机，
	// 就立刻退出 —— 否则球壳与矿体会跟着一颗已取消选中的行星。
	if (ActiveMineralView)
	{
		UCosmosSimulationSubsystem* ViewSubsystem = GetWorld()
			? GetWorld()->GetSubsystem<UCosmosSimulationSubsystem>()
			: nullptr;
		// 视图行星本身也要非空：两颗都为空时 == 会判成「同一颗」，视图就退不出去。
		const AVoxelPlanetActor* ViewPlanet = ActiveMineralView->GetViewPlanet();
		const bool bSamePlanet = ViewSubsystem && ViewPlanet != nullptr &&
			ViewSubsystem->GetSelectedBody() == ViewPlanet;
		if (!Cast<AStarMapCameraPawn>(GetPawn()) || !bSamePlanet)
		{
			ExitMineralViewIfActive();
		}
	}

	// 提示只在“站在飞船旁”时出现，所以每帧重算一次距离。
	if (APlanetCharacter* PawnCharacter = Cast<APlanetCharacter>(GetPawn()))
	{
		ASpaceshipPawn* Ship = ASpaceshipPawn::FindNearestTo(GetWorld(), PawnCharacter->GetActorLocation());
		if (Ship && Ship->CanBoardFrom(PawnCharacter->GetActorLocation()))
		{
			ShowPrompt(TEXT("按 E 登船"));
		}
	}
	else if (ASpaceshipPawn* Ship = Cast<ASpaceshipPawn>(GetPawn()))
	{
		// 只有真的载着角色时才提示离船。DebugPossessShip 直接 possess 飞船时
		// GetBoardedCharacter() 为空，否则会出现「显示了提示但按 E 什么都不发生」。
		if (Ship->GetBoardedCharacter() && Ship->IsLanded())
		{
			ShowPrompt(TEXT("按 E 离船"));
		}
	}

	// 星图模式不在这里刷操作提示：那是每帧一条 AddOnScreenDebugMessage（黄字），
	// 看起来就是调试输出，而且和星图自己的 UI 重复。操作说明归星图 UI 管。
}

void AStellarEraPlayerController::OnEditModeToggle(const FInputActionValue& Value)
{
	// 矿物透视下左键归图例：编辑模式会抢左键并改写体素，所以直接忽略切换，
	// 保持进入观察时强制退出的状态。
	if (ActiveMineralView)
	{
		return;
	}

	// 角色与星图相机各有一个编辑组件；飞船等其他 Pawn 上没有，返回空。
	if (UPlanetEditComponent* Edit = GetEditComponentForPawn(GetPawn()))
	{
		Edit->ToggleEditMode();
	}
}

void AStellarEraPlayerController::OnBrushSize(const FInputActionValue& Value)
{
	if (UPlanetEditComponent* Edit = GetEditComponentInEditMode())
	{
		const float Axis = Value.Get<float>();
		if (!FMath::IsNearlyZero(Axis))
		{
			Edit->AdjustBrushSize(Axis > 0.f ? 1 : -1);
		}
	}
}

void AStellarEraPlayerController::OnInteract(const FInputActionValue& Value)
{
	if (APlanetCharacter* PawnCharacter = Cast<APlanetCharacter>(GetPawn()))
	{
		ASpaceshipPawn* Ship = ASpaceshipPawn::FindNearestTo(GetWorld(), PawnCharacter->GetActorLocation());
		if (Ship && Ship->CanBoardFrom(PawnCharacter->GetActorLocation()))
		{
			BoardShip(Ship);
		}
		return;
	}

	if (ASpaceshipPawn* Ship = Cast<ASpaceshipPawn>(GetPawn()))
	{
		// 空中不能下船：速度没降到阈值以下、或下方没有地面时 IsLanded 为假。
		if (Ship->IsLanded())
		{
			LeaveShip(Ship);
		}
	}
}

void AStellarEraPlayerController::BoardShip(ASpaceshipPawn* Ship)
{
	APlanetCharacter* PawnCharacter = Cast<APlanetCharacter>(GetPawn());
	if (!Ship || !PawnCharacter)
	{
		return;
	}

	// 上船强制退出编辑模式：网格是行星上的组件，不退出会一直留在外面。
	// 必须留在 AttachToActor 之前。
	ForceExitEditMode(PawnCharacter);

	// 附着用 KeepWorldTransform：相对变换是从当前世界变换推导出来的，与 SetBoarded
	// 的先后无关——后者只切可见性、碰撞和移动模式，不碰变换。顺序只是为了读起来顺。
	PawnCharacter->AttachToActor(Ship, FAttachmentTransformRules::KeepWorldTransform);
	PawnCharacter->SetBoarded(true);
	Ship->SetBoardedCharacter(PawnCharacter);

	Possess(Ship);
}

void AStellarEraPlayerController::LeaveShip(ASpaceshipPawn* Ship)
{
	APlanetCharacter* PawnCharacter = Ship ? Ship->GetBoardedCharacter() : nullptr;
	if (!Ship || !PawnCharacter)
	{
		return;
	}

	PawnCharacter->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
	PawnCharacter->SetBoarded(false);
	Ship->SetBoardedCharacter(nullptr);

	// 摆到飞船旁边而不是原地：原地正好在飞船的碰撞体里，会立刻被挤开。
	const FVector ExitLocation = Ship->GetActorLocation() + Ship->GetActorRightVector() * 400.f;
	PawnCharacter->SetActorLocation(ExitLocation, /*bSweep=*/false);

	// 再按当地球面法线摆正。登船期间角色的朝向是冻结的（bOrientRotationToMovement
	// 只在有加速度时旋转，而附着时没有加速度），不重新对齐的话，在行星另一侧下船
	// 会看到角色侧躺在地上。这一步同时把角色按到球面上，并重置相机基座，
	// 避免下船瞬间镜头甩一下。
	// 下船前先重评一次最近行星。登船期间 Tick 里的重评是关着的（附着关系就是
	// 「在船上」），此刻 PlanetActor 很可能还是上船时那颗 —— 直接按球面摆正会把
	// 角色按到几公里外的旧星地表上，下一帧重评再把他拉回来。那一次瞬移通常来不及
	// 渲染（相机在所有 tick 组之后才更新），但它确实是两次位移，只是被 tick 顺序
	// 掩盖了。
	PawnCharacter->ReevaluatePlanet();
	PawnCharacter->SnapToPlanetSurface();

	Possess(PawnCharacter);
}

ASpaceshipPawn* AStellarEraPlayerController::ResolveStarMapFocusShip(APawn* EnteringPawn)
{
	if (ASpaceshipPawn* Ship = Cast<ASpaceshipPawn>(EnteringPawn))
	{
		return Ship;
	}

	// 步行进星图：角色没有「驾驶中的飞船」，用最近的一艘当焦点。
	// 不给焦点的话相机停在角色眼位（CalcCamera 冻结），星图 UI 又只画选中/悬停，
	// 画面与普通视角完全一样 —— 玩家会以为 M 键没生效。
	if (APlanetCharacter* Character = Cast<APlanetCharacter>(EnteringPawn))
	{
		return ASpaceshipPawn::FindNearestTo(Character->GetWorld(), Character->GetActorLocation());
	}

	return nullptr;
}

void AStellarEraPlayerController::OnMineralViewToggle()
{
	if (!Cast<AStarMapCameraPawn>(GetPawn()))
	{
		return;
	}

	if (ActiveMineralView)
	{
		ExitMineralViewIfActive();
		return;
	}

	UCosmosSimulationSubsystem* Subsystem = GetWorld()
		? GetWorld()->GetSubsystem<UCosmosSimulationSubsystem>()
		: nullptr;
	AVoxelPlanetActor* Planet = Subsystem
		? Cast<AVoxelPlanetActor>(Subsystem->GetSelectedBody())
		: nullptr;
	UPlanetMineralViewComponent* MineralView =
		Planet ? Planet->GetMineralViewComponent() : nullptr;
	if (!MineralView)
	{
		ShowPromptFor(TEXT("矿物观察只支持行星"), 2.f);
		return;
	}

	if (!MineralView->Activate(Planet))
	{
		// 失败只有三种：没有地质场、采样参数非法、观察体/材质创建失败
		// （后两种会先打 Error 日志）。
		ShowPromptFor(TEXT("该行星没有矿物分布"), 2.f);
		return;
	}

	// 观察里左键只点图例、右键转视角：若此前按过 G，先退出编辑模式。
	ForceExitEditMode(GetPawn());

	// 只拉远、不改朝向：按 H 前看哪儿，进入后还是那个角度（spec §3）。
	if (AStarMapCameraPawn* StarMap = Cast<AStarMapCameraPawn>(GetPawn()))
	{
		StarMap->FocusOn(Planet, 3.0 * Planet->GetSurfaceRadiusCm());
	}

	ActiveMineralView = MineralView;
}

void AStellarEraPlayerController::OnMineralFilter(
	const FInputActionValue& Value, int32 ElementIndex)
{
	if (!ActiveMineralView)
	{
		return;
	}
	ActiveMineralView->ToggleElement(ElementIndex);
}

void AStellarEraPlayerController::OnMineralShowAll(const FInputActionValue& Value)
{
	if (!ActiveMineralView)
	{
		return;
	}
	ActiveMineralView->SetAllElementsVisible(true);
}

void AStellarEraPlayerController::ExitMineralViewIfActive()
{
	if (!ActiveMineralView)
	{
		return;
	}

	ActiveMineralView->Deactivate();
	ActiveMineralView = nullptr;
}

void AStellarEraPlayerController::ToggleStarMap()
{
	AStarMapCameraPawn* CurrentStarMapPawn = Cast<AStarMapCameraPawn>(GetPawn());

	UCosmosSimulationSubsystem* Subsystem =
		GetWorld() ? GetWorld()->GetSubsystem<UCosmosSimulationSubsystem>() : nullptr;
	if (!Subsystem)
	{
		return;
	}

	if (CurrentStarMapPawn)
	{
		// ── 退出 ──
		// 先退矿物透视再换 Pawn：它隐藏了行星网格、关了碰撞与大气，必须在恢复视角之前还原。
		ExitMineralViewIfActive();

		Subsystem->SetSelectedBody(nullptr);
		Subsystem->SetStarMapActive(false);

		// PreviousPawn 可能已经没了（比如角色在星图里被销毁）。那时**不 Possess** ——
		// 留在相机上，不会崩。
		//
		// **但这不是「还能再进」**：此刻 GetPawn() 仍是星图 Pawn，所以下次按 M 会
		// 再次命中这个退出分支（星图已经关了，等于空转），**进不去**；唯一的脱身
		// 办法是 PIE 控制台里的 DebugPossessShip。
		//
		// **这条路径是可达的** —— 别因为 Source/ 里没有 Destroy 就以为安全：引擎自己会
		// 销毁 Pawn。AActor::CheckStillInWorld 在 GetActorLocation().Z < WorldSettings->KillZ
		// （且 WorldBoundsChecks 开着）时调 AActor::FellOutOfWorld；AKillZVolume 则**直接**调
		// FellOutOfWorld、不经 CheckStillInWorld。两条路都在 FellOutOfWorld 的函数体里调 Destroy()。
		//
		// **这一整条链有授权门控**：CheckStillInWorld 与 FellOutOfWorld 两处都要求
		// `HasAuthority() || GetLocalRole() == ROLE_None`，不满足就直接跳过、不销毁。
		// PIE / 单机下本地玩家就是授权方（或 actor 是 ROLE_None 的非网络 actor），成立；
		// **纯客户端上不成立**。结论不变 —— 路径仍可达，只是它发生在有授权的一侧。
		//
		// 蓝图子类、关卡蓝图、PIE 里手动删 Actor 也都不在 Source/ 里。另外进入分支
		// 那句 PreviousPawn = GetPawn() 没有空指针保护，GetPawn() 为空时进入就会落到
		// 同一状态，连销毁都不需要。
		//
		// 真要修，**三条缺一不可**：
		//   1. 进入时只在 GetPawn() 非空、且**不是**星图 Pawn 时才写 PreviousPawn
		//      —— 否则第二次进入会把星图 Pawn 自己记成 PreviousPawn，此后
		//      Possess(PreviousPawn) 换不了 Pawn；
		//   2. 进出判据从「GetPawn() 是不是星图 Pawn」换成 Subsystem->IsStarMapActive()；
		//   3. **退出分支必须兜底** —— 只做前两条只能让这个状态**可检出**、不能**可恢复**：
		//      PreviousPawn 已经失效时，退出分支照样没有可 Possess 的对象，玩家还是卡在
		//      相机上。得重新拿一个默认 Pawn（如走 AGameModeBase::RestartPlayer，
		//      或按 DefaultPawnClass 生成）。
		if (APawn* ReturnPawn = PreviousPawn.Get())
		{
			Possess(ReturnPawn);
		}
		// 星图的编辑模式不跟着换 Pawn 走：Possess 回飞船后组件已不在手上，
		// 预览 Tick 与笔刷线都会留在原地。退出即关，重进再按 G。
		ForceExitEditMode(CurrentStarMapPawn);
		PreviousPawn.Reset();
		return;
	}

	// ── 进入 ──
	if (!StarMapPawn)
	{
		// 懒创建，之后复用：每次进出都 Spawn/Destroy 没必要。
		FActorSpawnParameters SpawnParams;
		SpawnParams.Owner = this;
		SpawnParams.SpawnCollisionHandlingOverride =
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		StarMapPawn = GetWorld()->SpawnActor<AStarMapCameraPawn>(
			AStarMapCameraPawn::StaticClass(), FTransform::Identity, SpawnParams);
	}
	if (!StarMapPawn)
	{
		return;
	}

	// 「进入时相机原地不动、不选中」：把进入前的视角直接交给它当基准。
	// 用 GetPlayerViewPoint 而不是读 Pawn 的变换 —— 相机在 tick 组之后才更新，
	// Pawn 的变换不等于玩家此刻看到的画面。
	FVector ViewLocation;
	FRotator ViewRotation;
	GetPlayerViewPoint(ViewLocation, ViewRotation);
	StarMapPawn->EnterFrom(ViewLocation, ViewRotation);

	// 记住进入前 Possess 的是谁：退出时要换回去；进入后以飞船为焦点拉远 ——
	// 驾驶中就是当前飞船，步行则是角色最近的飞船。步行若没有焦点，相机只会
	// 冻在眼位、UI 也没有选中可画，看起来就像 M 键没生效。
	PreviousPawn = GetPawn();

	// 进星图前退出编辑模式：星图里左键是拾取，不能同时保留笔刷状态与网格。
	ForceExitEditMode(PreviousPawn.Get());

	ASpaceshipPawn* EnteringShip = ResolveStarMapFocusShip(PreviousPawn.Get());

	Subsystem->SetSelectedBody(nullptr);
	Subsystem->SetStarMapActive(true);
	Possess(StarMapPawn);

	// 进入前有焦点飞船（驾驶中就是当前飞船，步行时是最近的飞船）：选中它，
	// 并让相机以它为焦点、沿当前视线拉远到能看清飞船连同附近轨道。放在 Possess
	// 之后 —— FocusOn 只改相机 Pawn 自己的状态，不依赖 Possess，但语义上
	// 「进入星图」这个动作应当整体先完成再调相机。
	if (EnteringShip)
	{
		Subsystem->SetSelectedBody(EnteringShip);
		StarMapPawn->FocusOn(EnteringShip, StarMapFocusDistanceCm);
	}
}

void AStellarEraPlayerController::OnStarMapSelectStarted(const FInputActionValue& Value)
{
	// 编辑模式（角色或星图）：按钮优先，其次一次笔刷操作。按钮命中不能顺带挖到脚下的地形。
	// 矿物透视激活期间左键归图例：短路编辑分支，否则左键会挖/填体素。
	UPlanetEditComponent* Edit = ActiveMineralView ? nullptr : GetEditComponentInEditMode();
	if (Edit)
	{
		double MouseX = 0.0;
		double MouseY = 0.0;
		if (!GetMousePosition(MouseX, MouseY))
		{
			return;
		}
		const FVector2D MousePosition(MouseX, MouseY);

		// AHUD::Canvas 只在 PostRender 期间有效（HUD.h 里写明 "Only valid during
		// PostRender() event"），而输入回调跑在 tick 里 —— 问 HUD 要视口尺寸永远
		// 拿到 (0,0)，按钮命中会全部落空。所以这里从控制器自己查视口。
		// 绘制侧仍用 HUD 的 GetViewportSize()，那里 Canvas 是有效的。
		int32 ViewportSizeX = 0;
		int32 ViewportSizeY = 0;
		GetViewportSize(ViewportSizeX, ViewportSizeY);
		const FVector2D ViewportSize(
			static_cast<float>(ViewportSizeX), static_cast<float>(ViewportSizeY));

		AStellarEraHUD* StellarHUD = Cast<AStellarEraHUD>(GetHUD());

		int32 ButtonIndex = INDEX_NONE;
		if (ViewportSize.X > 0.0 && ViewportSize.Y > 0.0 &&
			PlanetEditUI::HitTestBrushButton(ViewportSize, MousePosition, ButtonIndex))
		{
			const bool bSphere = ButtonIndex == 0;
			Edit->SetBrushShape(bSphere ? EPlanetBrushShape::Sphere : EPlanetBrushShape::Cube);
			if (StellarHUD)
			{
				StellarHUD->ShowNotice(bSphere ? TEXT("笔刷：球体") : TEXT("笔刷：正方体"));
			}
			return;
		}

		// 这次点击归不归编辑，由「射线打没打到行星」回答。不能用 DigAtScreenPosition
		// 的返回值判断：打到了行星但没改到任何体素（比如往空气里挖）也会返回 false，
		// 那一下就会漏到松开的选中逻辑里。
		FPlanetEditTarget Target;
		if (Edit->TryResolveTarget(MousePosition, Target))
		{
			// 星图：Shift+左键填。角色仍只有右键点放（Shift 对角色不新增行为）。
			const bool bStarMap = Cast<AStarMapCameraPawn>(GetPawn()) != nullptr;
			const bool bPlacing = bStarMap
				&& (IsInputKeyDown(EKeys::LeftShift) || IsInputKeyDown(EKeys::RightShift));
			if (bPlacing)
			{
				Edit->PlaceAtScreenPosition(MousePosition);
			}
			else
			{
				Edit->DigAtScreenPosition(MousePosition);
			}
			return;
		}

		// 没打到行星：星图继续走下面的「记录按下位置」，松开时照常拾取天体；
		// 角色没有选择逻辑，会被下面的 `if (!Cast<AStarMapCameraPawn>(GetPawn())) return;`
		// 接住 —— 与改动前一致。
	}

	if (!Cast<AStarMapCameraPawn>(GetPawn()))
	{
		return;
	}

	if (ActiveMineralView)
	{
		double MouseX = 0.0;
		double MouseY = 0.0;
		if (!GetMousePosition(MouseX, MouseY))
		{
			return;
		}

		// 与绘制同一份纯函数：视口尺寸从控制器查（Canvas 只在 PostRender 有效）。
		int32 ViewportSizeX = 0;
		int32 ViewportSizeY = 0;
		GetViewportSize(ViewportSizeX, ViewportSizeY);
		const FVector2D ViewportSize(
			static_cast<float>(ViewportSizeX), static_cast<float>(ViewportSizeY));

		FStarMapPanelRect Panel;
		TArray<FStarMapPanelRect> RowRects;
		UStarMapUISystem::ComputeMineralLegendLayout(
			ViewportSize, ActiveMineralView->NumElements(), Panel, RowRects);

		const int32 RowIndex = UStarMapUISystem::HitTestMineralLegendRow(
			RowRects, FVector2D(MouseX, MouseY));
		if (RowRects.IsValidIndex(RowIndex))
		{
			ActiveMineralView->ToggleElement(RowIndex);
		}

		// 观察模式下左键不再拾取天体，也不转视角（转视角归右键）。
		return;
	}

	// 用 double 重载，是为了后面直接喂给 FVector2D（UE5 里是双精度）时少一次转换。
	// **但精度上并不占便宜**：引擎那个重载（PlayerController.cpp:6186）内部就是
	// `float X, Y; GetMousePosition(X, Y);` —— 先过 float 再展宽，真正决定精度的
	// 是底层的 UGameViewportClient::GetMousePosition，两个重载拿到的是同一个值。
	double MouseX = 0.0;
	double MouseY = 0.0;
	if (GetMousePosition(MouseX, MouseY))
	{
		StarMapPressPosition = FVector2D(MouseX, MouseY);
		bStarMapSelectHeld = true;
	}
}

void AStellarEraPlayerController::OnStarMapSelectCompleted(const FInputActionValue& Value)
{
	if (ActiveMineralView)
	{
		// 观察模式下左键只用来点图例，松手不做任何事。
		return;
	}

	if (!bStarMapSelectHeld)
	{
		return;
	}
	bStarMapSelectHeld = false;

	if (!Cast<AStarMapCameraPawn>(GetPawn()))
	{
		return;
	}

	double MouseX = 0.0;
	double MouseY = 0.0;
	if (!GetMousePosition(MouseX, MouseY))
	{
		return;
	}

	// 按下与松开的位移大于阈值 → 这是「拖拽转视角」的收尾，不改选中。
	const double MovedSq = FVector2D::DistSquared(
		StarMapPressPosition, FVector2D(MouseX, MouseY));
	if (MovedSq > ClickMoveThresholdPx * ClickMoveThresholdPx)
	{
		return;
	}

	// 点空白什么都不做（spec §5.2：取消选中会让相机停止跟随、丢失目标，
	// 绑在误点很容易触发的动作上不划算）。要取消就明确地点回它自己。
	AActor* Picked = PickBodyUnderScreenPosition(FVector2D(MouseX, MouseY));
	if (!Picked)
	{
		return;
	}

	UCosmosSimulationSubsystem* Subsystem =
		GetWorld() ? GetWorld()->GetSubsystem<UCosmosSimulationSubsystem>() : nullptr;
	if (!Subsystem)
	{
		return;
	}

	Subsystem->SetSelectedBody(
		Subsystem->GetSelectedBody() == Picked ? nullptr : Picked);
}

/**
 * 天体的世界半径（厘米）。体素星球用它的地表半径；其它天体退回碰撞半径。
 * 取不到（非正）返回 0 —— 调用方按「没算出来」处理。
 */
static double ResolveBodyRadiusCm(const AActor* Body)
{
	if (const AVoxelPlanetActor* Planet = Cast<AVoxelPlanetActor>(Body))
	{
		return Planet->GetSurfaceRadiusCm();
	}
	return Body ? double(Body->GetSimpleCollisionRadius()) : 0.0;
}

bool AStellarEraPlayerController::MakeScreenCandidate(
	AActor* Body, FStarMapScreenCandidate& Out) const
{
	Out = FStarMapScreenCandidate();
	if (!IsValid(Body))
	{
		return false;
	}

	const FVector Center = Body->GetActorLocation();
	if (!ProjectWorldLocationToScreen(Center, Out.ScreenPosition))
	{
		return false;
	}

	// 视线右向：算「与视线垂直」的半径必须用它，不能用固定世界轴。
	FVector ViewLocation;
	FRotator ViewRotation;
	GetPlayerViewPoint(ViewLocation, ViewRotation);
	const FVector ViewRight = FRotationMatrix(ViewRotation).GetUnitAxis(EAxis::Y);

	// 盘面半径 = |投影(中心) − 投影(中心 + 右向 × 世界半径)|。
	// 边缘点投不出来（越过后平面等）就留 0，退回固定像素容差。
	const double WorldRadiusCm = ResolveBodyRadiusCm(Body);
	if (WorldRadiusCm > 0.0)
	{
		FVector2D EdgePosition;
		if (ProjectWorldLocationToScreen(Center + ViewRight * WorldRadiusCm, EdgePosition))
		{
			// 直接投影「中心 + 右向 × R」量到的是 f·R/d，比切线决定的真实轮廓半径
			// 小一个 cos α（见 SilhouetteRadiusScale）。系数为 0 表示相机在球内
			// 或贴着球面，此时留 0、退回固定像素容差。
			const double CameraDistanceCm = FVector::Dist(ViewLocation, Center);
			Out.ScreenRadiusPx = float(
				FVector2D::Distance(Out.ScreenPosition, EdgePosition)
				* AStarMapCameraPawn::SilhouetteRadiusScale(CameraDistanceCm, WorldRadiusCm));
		}
	}

	Out.Actor = Body;
	return true;
}

bool AStellarEraPlayerController::ProjectBodyToScreen(
	AActor* Body, FVector2D& OutCenter, float& OutRadiusPx) const
{
	FStarMapScreenCandidate Candidate;
	if (!MakeScreenCandidate(Body, Candidate))
	{
		return false;
	}

	OutCenter = Candidate.ScreenPosition;
	OutRadiusPx = Candidate.ScreenRadiusPx;
	return true;
}

bool AStellarEraPlayerController::GetStarMapMousePositionPx(FVector2D& OutPosition) const
{
	double MouseX = 0.0;
	double MouseY = 0.0;
	if (!GetMousePosition(MouseX, MouseY))
	{
		return false;
	}
	OutPosition = FVector2D(MouseX, MouseY);
	return true;
}

AActor* AStellarEraPlayerController::PickBodyUnderScreenPosition(
	const FVector2D& ScreenPosition) const
{
	UCosmosSimulationSubsystem* Subsystem =
		GetWorld() ? GetWorld()->GetSubsystem<UCosmosSimulationSubsystem>() : nullptr;
	if (!Subsystem)
	{
		return nullptr;
	}

	TArray<AActor*> Bodies;
	Subsystem->GetRegisteredBodies(Bodies);

	// 投影留在调用方，拾取函数才能是纯函数、才能无头测。
	TArray<FStarMapScreenCandidate> Candidates;
	Candidates.Reserve(Bodies.Num());
	for (AActor* Body : Bodies)
	{
		FStarMapScreenCandidate Candidate;
		if (MakeScreenCandidate(Body, Candidate))
		{
			Candidates.Add(Candidate);
		}
	}

	return AStarMapCameraPawn::PickBodyAtScreenPosition(
		Candidates, ScreenPosition, SelectionPixelRadius);
}

void AStellarEraPlayerController::ShowPrompt(const FString& Text)
{
	if (!GEngine)
	{
		return;
	}

	// duration 0 = 只显示这一帧。固定 key 让屏幕上永远只有一行，不会堆积。
	static const uint64 PromptMessageKey = 0x5745'4C43'4F4D'4521ULL;
	GEngine->AddOnScreenDebugMessage(PromptMessageKey, 0.f, FColor::Yellow, Text);
}

void AStellarEraPlayerController::ShowPromptFor(const FString& Text, float DurationSeconds)
{
	if (!GEngine)
	{
		return;
	}

	// 与 ShowPrompt 分开的 key：那条每帧刷新（duration 0），这条到点自动消失。
	static const uint64 TransientPromptMessageKey = 0x43555441ULL;
	GEngine->AddOnScreenDebugMessage(
		TransientPromptMessageKey, DurationSeconds, FColor::Yellow, Text);
}
