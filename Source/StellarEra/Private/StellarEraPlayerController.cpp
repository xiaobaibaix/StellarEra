// Copyright Voxel Plugin SAS. All Rights Reserved.

#include "StellarEraPlayerController.h"

#include "PlanetCharacter.h"
#include "SpaceshipPawn.h"
#include "StellarEraHUD.h"
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
	// 换 Pawn 时把开关清零。按着右键登船/离船的话，松开事件会送到新 Pawn 身上，
	// 这个标志就永远停在 true —— 角色一下船就自己转视角，不用按右键。
	bLookHeld = false;

	const bool bWalking = Cast<APlanetCharacter>(InPawn) != nullptr;

	// 走路时光标常驻。飞船里保持隐藏：鼠标就是它的操纵杆，藏起来才像在驾驶。
	bShowMouseCursor = bWalking;

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
