// Copyright Voxel Plugin SAS. All Rights Reserved.

#include "StellarEraPlayerController.h"

#include "PlanetCharacter.h"
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

	EnhancedInput->BindAction(IA_Interact, ETriggerEvent::Started, this, &AStellarEraPlayerController::OnInteract);
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

	ApplyMouseModeForPawn(InPawn);
}

void AStellarEraPlayerController::ApplyMouseModeForPawn(APawn* InPawn)
{
	// 换 Pawn 时把两个「按住」标志都清零。按着键登船/离船/进星图的话，松开事件
	// 会送到新 Pawn 身上，标志就永远停在 true —— 角色一下船就自己转视角，
	// 或者星图里没按左键也在转视角。
	bLookHeld = false;
	bStarMapSelectHeld = false;

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

	// 飞船：W/S 主推力，D/A 横滚，Space/LeftControl 升降，LeftShift 加速，Tab 巡航。
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
		StarMap->OrbitLook(DeltaYaw, DeltaPitch);
	}
}

void AStellarEraPlayerController::OnLookActivateStarted(const FInputActionValue& Value)
{
	bLookHeld = true;
}

void AStellarEraPlayerController::OnLookActivateCompleted(const FInputActionValue& Value)
{
	bLookHeld = false;
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
	// 缩放不分角色还是飞船，两边都响应：滚轮是纯粹的相机操作，不像 Look 那样
	// 兼着飞船的操纵杆职责，没有理由为它设门。
	if (APlanetCharacter* PawnCharacter = Cast<APlanetCharacter>(GetPawn()))
	{
		PawnCharacter->AddZoom(Value.Get<float>());
	}
	else if (ASpaceshipPawn* Ship = Cast<ASpaceshipPawn>(GetPawn()))
	{
		Ship->AddZoom(Value.Get<float>());
	}
	else if (AStarMapCameraPawn* StarMap = Cast<AStarMapCameraPawn>(GetPawn()))
	{
		StarMap->AddZoom(Value.Get<float>());
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

	ASpaceshipPawn* Ship = PawnCharacter->FindNearestShip();
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

	// 提示只在“站在飞船旁”时出现，所以每帧重算一次距离。
	if (APlanetCharacter* PawnCharacter = Cast<APlanetCharacter>(GetPawn()))
	{
		ASpaceshipPawn* Ship = PawnCharacter->FindNearestShip();
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
	else if (AStarMapCameraPawn* StarMap = Cast<AStarMapCameraPawn>(GetPawn()))
	{
		if (AActor* Selected = StarMap->ResolveSelectedBody())
		{
			ShowPrompt(FString::Printf(
				TEXT("%s　左键拖拽绕行　滚轮缩放　再点它取消选中　M 退出"),
				*Selected->GetName()));
		}
		else
		{
			ShowPrompt(TEXT("左键点击选中天体　拖拽转视角　滚轮缩放　M 退出星图"));
		}
	}
}

void AStellarEraPlayerController::OnInteract(const FInputActionValue& Value)
{
	if (APlanetCharacter* PawnCharacter = Cast<APlanetCharacter>(GetPawn()))
	{
		ASpaceshipPawn* Ship = PawnCharacter->FindNearestShip();
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

	// 摆到飞船旁边而不是原地：原地正好在飞船的碰撞球里，会立刻被挤开。
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
		// 销毁 Pawn。AActor::CheckStillInWorld（Actor.cpp:2300）在 Z < WorldSettings->KillZ
		// （:2326）时调 FellOutOfWorld（:2329），后者的函数体在 :3382 调 Destroy()；
		// AKillZVolume 走同一条路。
		//
		// **这一整条链有授权门控**：CheckStillInWorld 与 FellOutOfWorld 两处都要求
		// `HasAuthority() || GetLocalRole() == ROLE_None`（:2312 与 :3377），不满足就
		// 直接跳过、不销毁。PIE / 单机下本地玩家就是授权方（或 actor 是 ROLE_None 的
		// 非网络 actor），成立；**纯客户端上不成立**。结论不变 —— 路径仍可达，只是
		// 它发生在有授权的一侧。
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

	PreviousPawn = GetPawn();
	Subsystem->SetSelectedBody(nullptr);
	Subsystem->SetStarMapActive(true);
	Possess(StarMapPawn);
}

void AStellarEraPlayerController::OnStarMapSelectStarted(const FInputActionValue& Value)
{
	if (!Cast<AStarMapCameraPawn>(GetPawn()))
	{
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
	AActor* Picked = PickBodyUnderCursor();
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

AActor* AStellarEraPlayerController::PickBodyUnderCursor() const
{
	UCosmosSimulationSubsystem* Subsystem =
		GetWorld() ? GetWorld()->GetSubsystem<UCosmosSimulationSubsystem>() : nullptr;
	if (!Subsystem)
	{
		return nullptr;
	}

	TArray<AActor*> Bodies;
	Subsystem->GetRegisteredBodies(Bodies);

	// 视线右向：算「与视线垂直」的半径必须用它，不能用固定世界轴。
	FVector ViewLocation;
	FRotator ViewRotation;
	GetPlayerViewPoint(ViewLocation, ViewRotation);
	const FVector ViewRight = FRotationMatrix(ViewRotation).GetUnitAxis(EAxis::Y);

	// 投影留在调用方，拾取函数才能是纯函数、才能无头测。
	TArray<FStarMapScreenCandidate> Candidates;
	Candidates.Reserve(Bodies.Num());
	for (AActor* Body : Bodies)
	{
		if (!Body)
		{
			continue;
		}

		const FVector Center = Body->GetActorLocation();
		FVector2D ScreenPosition;
		if (!ProjectWorldLocationToScreen(Center, ScreenPosition))
		{
			continue;
		}

		// 盘面半径 = |投影(中心) − 投影(中心 + 右向 × 世界半径)|。
		// 边缘点投不出来（越过后平面等）就留 0，退回固定像素容差。
		float ScreenRadiusPx = 0.f;
		const double WorldRadiusCm = ResolveBodyRadiusCm(Body);
		if (WorldRadiusCm > 0.0)
		{
			FVector2D EdgePosition;
			if (ProjectWorldLocationToScreen(Center + ViewRight * WorldRadiusCm, EdgePosition))
			{
				// 直接投影「中心 + 右向 × R」量到的是 f·R/d，比切线决定的真实轮廓半径
				// 小一个 cos α（见 SilhouetteRadiusScale）。系数为 0 表示相机在球内，
				// 此时留 0、退回固定像素容差。
				const double CameraDistanceCm = FVector::Dist(ViewLocation, Center);
				ScreenRadiusPx = float(
					FVector2D::Distance(ScreenPosition, EdgePosition)
					* AStarMapCameraPawn::SilhouetteRadiusScale(CameraDistanceCm, WorldRadiusCm));
			}
		}

		Candidates.Add({ Body, ScreenPosition, ScreenRadiusPx });
	}

	double MouseX = 0.0;
	double MouseY = 0.0;
	if (!GetMousePosition(MouseX, MouseY))
	{
		return nullptr;
	}

	return AStarMapCameraPawn::PickBodyAtScreenPosition(
		Candidates, FVector2D(MouseX, MouseY), SelectionPixelRadius);
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
