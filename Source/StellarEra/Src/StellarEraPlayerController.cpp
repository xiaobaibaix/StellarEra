// Copyright Voxel Plugin SAS. All Rights Reserved.

#include "StellarEraPlayerController.h"

#include "PlanetCharacter.h"
#include "PlanetMineralViewComponent.h"
#include "StarMapUISystem.h"
#include "PlanetEditBrush.h"
#include "PlanetEditComponent.h"
#include "SpaceshipPawn.h"
#include "StellarEraHUD.h"
#include "CosmosSimulationSubsystem.h"
#include "StarMapCameraPawn.h"
#include "StarMapPicking.h"
#include "VoxelPlanetActor.h"
#include "VoxelViewpointInvokers.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/Engine.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/GameModeBase.h"
#include "TimerManager.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "EngineUtils.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
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

	// 星图相机优先用蓝图子类：笔刷区间、默认尺寸、连续挖间隔都在它的
	// PlanetEdit 组件细节面板里，不重编译就能调。找不到时退回原生类。
	StarMapPawnClass = AStarMapCameraPawn::StaticClass();

	static ConstructorHelpers::FClassFinder<AStarMapCameraPawn> StarMapClassFinder(
		TEXT("/Game/Code/Pawns/BP_StarMapCameraPawn.BP_StarMapCameraPawn_C"));
	if (StarMapClassFinder.Succeeded())
	{
		StarMapPawnClass = StarMapClassFinder.Class;
	}
	else
	{
		UE_LOG(LogTemp, Warning,
			TEXT("AStellarEraPlayerController: 找不到 BP_StarMapCameraPawn，"
				 "将使用原生 AStarMapCameraPawn（编辑参数只能在 C++ 里改）。"));
	}

	// 下船时生成的角色。同样是"优先蓝图、缺了退回原生"的模式：
	// 角色的蓝图缺失等于玩家本体出问题，所以退回的是原生 APlanetCharacter
	// —— 移动、重力、相机全在 C++ 里，蓝图只负责模型。
	CrewCharacterClass = APlanetCharacter::StaticClass();

	static ConstructorHelpers::FClassFinder<APlanetCharacter> CrewClassFinder(
		TEXT("/Game/Code/Pawns/Character/BP_PlanetCharacter.BP_PlanetCharacter_C"));
	if (CrewClassFinder.Succeeded())
	{
		CrewCharacterClass = CrewClassFinder.Class;
	}
	else
	{
		UE_LOG(LogTemp, Warning,
			TEXT("AStellarEraPlayerController: 找不到 BP_PlanetCharacter，"
				 "下船时将使用原生 APlanetCharacter（占位骷髅方块）。"));
	}
}

void AStellarEraPlayerController::BeginPlay()
{
	Super::BeginPlay();

	// 动作资产在 BeginPlay 构造好再塞 IMC；Pawn 的 SetupPlayerInputComponent
	// 若更早来取，三个 getter 自己也会 Build（幂等）。
	Input.Build(this);

	if (UEnhancedInputLocalPlayerSubsystem* Subsystem =
		ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer()))
	{
		Subsystem->AddMappingContext(Input.MappingContext, /*Priority=*/0);
	}
}

const FPlanetCharacterInputActions& AStellarEraPlayerController::GetPlanetCharacterInputActions()
{
	// 幂等：Pawn 的 SetupPlayerInputComponent 可能早于本类 BeginPlay 来取，
	// Build 在这里兜底（已构造就直接返回）。
	Input.Build(this);
	return Input.Character;
}

const FSpaceshipInputActions& AStellarEraPlayerController::GetSpaceshipInputActions()
{
	Input.Build(this);
	return Input.Ship;
}

const FStarMapCameraInputActions& AStellarEraPlayerController::GetStarMapCameraInputActions()
{
	Input.Build(this);
	return Input.StarMap;
}

void AStellarEraPlayerController::OnPossess(APawn* InPawn)
{
	// 先拍下旧 Pawn：Super 会在内部 UnPossess 并清空 GetPawn()，
	// 之后再想找「刚被换下的星图相机」就找不到了。
	APawn* const PreviousPossessed = GetPawn();
	Super::OnPossess(InPawn);

	// 换 Pawn（登船 / 离船 / 进出星图 / DebugPossessShip）一律退出矿物透视。
	// 会话状态住在相机 Pawn 上，这里只是命令那台旧相机收尾。
	if (AStarMapCameraPawn* PreviousCamera = Cast<AStarMapCameraPawn>(PreviousPossessed))
	{
		PreviousCamera->ExitMineralViewIfActive();
	}

	// 换视点：只有新 Pawn 的体素调用器参与 LOD 与碰撞。这是所有换 Pawn 路径的
	// 唯一收口，所以门控挂在这里，而不是登船 / 离船 / 星图三处各写一遍。
	ApplyViewpointInvokerGate(GetWorld(), InPawn);

	ApplyMouseModeForPawn(InPawn);
}

void AStellarEraPlayerController::ApplyViewpointInvokerGate(
	UWorld* const World, const APawn* const InPawn)
{
	if (!World)
	{
		return;
	}

	// OnPossess 是低频事件，遍历全部 Pawn 的开销可忽略。
	for (TActorIterator<APawn> It(World); It; ++It)
	{
		APawn* const Pawn = *It;
		IVoxelViewpointInvokers* const Viewpoint = Cast<IVoxelViewpointInvokers>(Pawn);
		if (!Viewpoint)
		{
			continue;
		}

		Viewpoint->SetViewpointInvokersActive(Pawn == InPawn);
	}
}

void AStellarEraPlayerController::ApplyMouseModeForPawn(APawn* InPawn)
{
	// 输入状态（左键 / 右键 / 中键）全归各 Pawn 的输入组件，它们在 Bind 时自己清零 ——
	// 换 Pawn 时松开事件会送到新 Pawn 身上，不归本类管了。

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
		// 在右键按下时为真，于是同一次右键既被送进 InputKey（触发 Input.LookActivate）、
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

UPlanetEditComponent* AStellarEraPlayerController::GetEditComponentInEditMode() const
{
	UPlanetEditComponent* Edit = GetEditComponentForPawn(GetPawn());
	return Edit && Edit->IsEditMode() ? Edit : nullptr;
}

UPlanetMineralViewComponent* AStellarEraPlayerController::GetActiveMineralView() const
{
	// 会话状态住在星图相机上：只有相机被操控时才可能有激活的矿物透视。
	const AStarMapCameraPawn* Camera = Cast<AStarMapCameraPawn>(GetPawn());
	return Camera ? Camera->GetActiveMineralView() : nullptr;
}

void AStellarEraPlayerController::ShowStarMapPrompt(
	const FString& Text, const float Seconds)
{
	ShowPromptFor(Text, Seconds);
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

void AStellarEraPlayerController::DebugDisembark()
{
	ASpaceshipPawn* Ship = Cast<ASpaceshipPawn>(GetPawn());
	if (!Ship)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("AStellarEraPlayerController: DebugDisembark 失败：当前 Pawn = %s，不是飞船。"),
			*GetNameSafe(GetPawn()));
		return;
	}

	// 与按 E 同一条路径：落地判据之外不做任何额外检查（无头验证时飞船可能还在下落，
	// 但生成角色的逻辑与 IsLanded 无关）。
	LeaveShip(Ship);
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

void AStellarEraPlayerController::PlayerTick(float DeltaTime)
{
	Super::PlayerTick(DeltaTime);

	// 矿物透视绑在「选中的那颗行星」上。焦点被改选就立刻命令相机退出 ——
	// 否则球壳与矿体会跟着一颗已取消选中的行星。换 Pawn 的退出在 OnPossess 收口，
	// 相机不 Tick，所以「选中变了没有」这条巡检留在这里。
	if (AStarMapCameraPawn* Camera = Cast<AStarMapCameraPawn>(GetPawn()))
	{
		if (UPlanetMineralViewComponent* MineralView = Camera->GetActiveMineralView())
		{
			UCosmosSimulationSubsystem* ViewSubsystem = GetWorld()
				? GetWorld()->GetSubsystem<UCosmosSimulationSubsystem>()
				: nullptr;
			// 视图行星本身也要非空：两颗都为空时 == 会判成「同一颗」，视图就退不出去。
			const AVoxelPlanetActor* ViewPlanet = MineralView->GetViewPlanet();
			const bool bSamePlanet = ViewSubsystem && ViewPlanet != nullptr &&
				ViewSubsystem->GetSelectedBody() == ViewPlanet;
			if (!bSamePlanet)
			{
				Camera->ExitMineralViewIfActive();
			}
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
		// 落地就提示，不再要求「舱里有角色」：开局操控的就是飞船，角色要等第一次
		// 按 E 才生成；DebugPossessShip 同样如此。按 E 一定会生成一个，提示不会骗人。
		if (Ship->IsLanded())
		{
			ShowPrompt(TEXT("按 E 离船"));
		}
	}

	// 星图模式不在这里刷操作提示：那是每帧一条 AddOnScreenDebugMessage（黄字），
	// 看起来就是调试输出，而且和星图自己的 UI 重复。操作说明归星图 UI 管。
}

void AStellarEraPlayerController::InteractWithShip()
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
	if (!Ship)
	{
		return;
	}

	// 开局操控的是飞船，舱里本来就没有角色 —— 第一次下船时才生成一个。
	// 第二次以后复用同一个角色：登船时它只是被隐藏、附着在船上。
	APlanetCharacter* PawnCharacter = Ship->GetBoardedCharacter();
	if (PawnCharacter)
	{
		PawnCharacter->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
		PawnCharacter->SetBoarded(false);
		Ship->SetBoardedCharacter(nullptr);
	}
	else
	{
		PawnCharacter = SpawnCrewCharacter(Ship);
		if (!PawnCharacter)
		{
			return;
		}
	}

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

APlanetCharacter* AStellarEraPlayerController::SpawnCrewCharacter(ASpaceshipPawn* Ship)
{
	UWorld* World = GetWorld();
	if (!World || !Ship)
	{
		return nullptr;
	}

	// 角色类来自本控制器的 CrewCharacterClass（构造时优先 BP_PlanetCharacter）。
	// 这里只做类型兜底，防止有人在细节面板里填了非角色类。
	UClass* CharacterClass = CrewCharacterClass.Get();
	if (!CharacterClass || !CharacterClass->IsChildOf(APlanetCharacter::StaticClass()))
	{
		CharacterClass = APlanetCharacter::StaticClass();
	}

	// 与 LeaveShip 的落脚点同一个位置：飞船右侧（机身坐标系），不在船体正中的
	// 碰撞体里。AlwaysSpawn：这一点可能还在船壳重叠范围内，让引擎挑位置反而
	// 会失败或把角色挪到不知哪里去。
	const FVector CrewSpawnLocation = Ship->GetActorLocation() + Ship->GetActorRightVector() * 400.f;

	FActorSpawnParameters Params;
	Params.Owner = this;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	APawn* SpawnedPawn = World->SpawnActor<APawn>(
		CharacterClass, CrewSpawnLocation, Ship->GetActorRotation(), Params);
	APlanetCharacter* SpawnedCharacter = Cast<APlanetCharacter>(SpawnedPawn);
	if (!SpawnedCharacter)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("StellarEraPlayerController: 下船时生成角色失败（类 = %s），按 E 无法离船。"),
			*GetNameSafe(CharacterClass));
		return nullptr;
	}

	UE_LOG(LogTemp, Log,
		TEXT("StellarEraPlayerController: 下船时在 %s 生成角色 %s（类 = %s）。"),
		*CrewSpawnLocation.ToString(),
		*SpawnedCharacter->GetName(),
		*GetNameSafe(CharacterClass));
	return SpawnedCharacter;
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
		// 混合还没走完时又按了一次 M：忽略。此刻 GetPawn() 仍是星图 Pawn，
		// 不拦的话会把整段退出流程重跑一遍、再压一个定时器上去。
		if (StarMapExitTimerHandle.IsValid()
			&& GetWorldTimerManager().IsTimerActive(StarMapExitTimerHandle))
		{
			return;
		}

		// 先退矿物透视再换 Pawn：它隐藏了行星网格、关了碰撞与大气，必须在恢复视角之前还原。
		// 状态住在相机上，由那台正在被操控的相机自己收尾。
		CurrentStarMapPawn->ExitMineralViewIfActive();

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
		APawn* const ReturnPawn = PreviousPawn.Get();
		if (ReturnPawn && StarMapExitBlendSeconds > 0.f)
		{
			// 不要一帧切回去：交给引擎做视点混合。起点是仍被 Possess 的星图相机
			// （它的 CalcCamera 每帧还在产出画面），终点是返回 Pawn 的相机；引擎
			// 逐帧插两条 FMinimalViewInfo，位置 / 朝向 / FOV 一起过渡。
			SetViewTargetWithBlend(
				ReturnPawn,
				StarMapExitBlendSeconds,
				EViewTargetBlendFunction::VTBlend_Cubic,
				/*BlendExp=*/0.f,
				/*bLockOutgoing=*/false);

			// 混合结束再 Possess —— Possess 会立刻把视点设成返回 Pawn，提前调用
			// 等于把混合掐掉。多等 0.05 s，避开「定时器与混合在同一帧收尾」的边界。
			FTimerDelegate ExitBlendDone = FTimerDelegate::CreateWeakLambda(
				this,
				[this, WeakReturn = TWeakObjectPtr<APawn>(ReturnPawn)]()
				{
					FinishStarMapExit(WeakReturn.Get());
				});
			GetWorldTimerManager().SetTimer(
				StarMapExitTimerHandle,
				ExitBlendDone,
				StarMapExitBlendSeconds + 0.05f,
				/*bLoop=*/false);
		}
		else
		{
			// 没有可回的 Pawn（它已经没了），或混合被关掉：老路径，立刻收尾。
			FinishStarMapExit(ReturnPawn);
		}
		// 星图的编辑模式不跟着换 Pawn 走：Possess 回飞船后组件已不在手上，
		// 预览 Tick 与笔刷线都会留在原地。退出即关，重进再按 G。
		ForceExitEditMode(CurrentStarMapPawn);
		PreviousPawn.Reset();
		return;
	}

	// ── 进入 ──
	// 进入前的 Pawn 先记下：决定用哪台相机（AStarMapCameraPawn::ResolvePlanetStarMapCamera），
	// 也是「深空退路」里挑最近焦点天体的原点。原来这行写在 EnterFrom 之后，
	// 现在提前 —— EnterFrom 与选相机都不改 GetPawn()，语义不变、依赖先满足。
	PreviousPawn = GetPawn();

	// B2 路线（2026-11-01 用户选定）：BP_VoxelPlanet 挂一个 Child Actor Component
	// （子类 BP_StarMapCameraPawn），「星球 + 它的星图相机」成为一个可整体复制的
	// 组合，参数（缩放步长 / 高度区间 / 最大距离）按星球在组件细节面板里配。
	// 找得到就用它；找不到（行星没挂 CAC、或从深空进图）退回下面的懒创建单例，
	// 行为与旧版完全一致 —— 所以没挂 CAC 的存量关卡不用改也能跑。
	AStarMapCameraPawn* CameraToUse =
		AStarMapCameraPawn::ResolvePlanetStarMapCamera(PreviousPawn.Get());
	if (!CameraToUse)
	{
		if (!StarMapPawn)
		{
			// 懒创建，之后复用：每次进出都 Spawn/Destroy 没必要。
			FActorSpawnParameters SpawnParams;
			SpawnParams.Owner = this;
			SpawnParams.SpawnCollisionHandlingOverride =
				ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			UClass* const PawnClass = StarMapPawnClass
				? StarMapPawnClass.Get()
				: AStarMapCameraPawn::StaticClass();
			StarMapPawn = GetWorld()->SpawnActor<AStarMapCameraPawn>(
				PawnClass, FTransform::Identity, SpawnParams);
		}
		CameraToUse = StarMapPawn.Get();
	}
	if (!CameraToUse)
	{
		// 两条路都没拿到相机（Spawn 失败）。PreviousPawn 刚写、Possess 还没动，
		// 原样清掉，别给退出分支留「上一个 Pawn 是自己」的脏状态。
		PreviousPawn.Reset();
		return;
	}

	// 入场位置：行星相机挂在球心，直接原地接手会先看见星体内部，所以先沿
	// 「球心 → 进入前视点」把它搬到球面附近（ComputeEntryAnchorLocation）。
	// 深空单例相机没有宿主行星，函数内部退回它自己的位置，行为与旧版一致。
	//
	// 用 GetPlayerViewPoint 而不是读 Pawn 的变换 —— 相机在 tick 组之后才更新，
	// Pawn 的变换不等于玩家此刻看到的画面。朝向仍取进入前的视线，于是进场第一帧
	// 是「贴着球面、朝原方向看」，随后 FocusOn 的平滑拉远把画面退到焦点飞船。
	FVector ViewLocation;
	FRotator ViewRotation;
	GetPlayerViewPoint(ViewLocation, ViewRotation);
	CameraToUse->EnterFrom(
		CameraToUse->ComputeEntryAnchorLocation(ViewLocation),
		ViewRotation);

	// 进星图前退出编辑模式：星图里左键是拾取，不能同时保留笔刷状态与网格。
	ForceExitEditMode(PreviousPawn.Get());

	// 焦点天体：只有挂了星图相机的天体才能被聚焦，飞船没有相机 → 不让它当焦点。
	//
	// 行星相机就挂在行星身上，所以「借用谁的相机」与「聚焦谁」是同一颗：沿进场
	// 视线把它拉远到相机自己的 PlanetFocusRadiusScale 倍地表半径（默认 3），整颗
	// 行星进画面。
	// 用的是深空单例相机（没有宿主行星）时，焦点退回离进场 Pawn 最近的、带相机
	// 的已注册天体；一个都没有就不聚焦，相机停在场点。
	AActor* FocusBody = CameraToUse->GetAttachParentActor();
	if (!FocusBody)
	{
		double BestDistanceSq = TNumericLimits<double>::Max();
		TArray<AActor*> Bodies;
		Subsystem->GetRegisteredBodies(Bodies);
		const FVector From = PreviousPawn.IsValid()
			? PreviousPawn->GetActorLocation()
			: CameraToUse->GetActorLocation();
		for (AActor* Body : Bodies)
		{
			if (!AStarMapCameraPawn::HasStarMapCamera(Body))
			{
				continue;
			}
			const double DistanceSq = FVector::DistSquared(From, Body->GetActorLocation());
			if (DistanceSq < BestDistanceSq)
			{
				BestDistanceSq = DistanceSq;
				FocusBody = Body;
			}
		}
	}

	Subsystem->SetSelectedBody(nullptr);
	Subsystem->SetStarMapActive(true);
	Possess(CameraToUse);

	// 选中焦点并拉远。放在 Possess 之后 —— FocusOn 只改相机 Pawn 自己的状态，
	// 不依赖 Possess，但语义上「进入星图」这个动作应当整体先完成再调相机。
	//
	// **就算用的是行星相机，图内点选别的星球也只走 SetSelectedBody + FocusOn、
	// 不换相机**：换相机 = 换 Possess = 两台相机各 4 个 invoker 的 bUseForLOD
	// 翻转 = 体素八叉树全量重建，正是 2026-10-24 那个 chunk 簿记 ensure 的触发
	// 模式。单相机 + 换焦点没有这个成本，「每星球一台」的差异收敛到参数上。
	if (FocusBody)
	{
		Subsystem->SetSelectedBody(FocusBody);
		CameraToUse->FocusOn(
			FocusBody,
			CameraToUse->GetPlanetFocusRadiusScale() * StarMapPicking::GetBodyRadiusCm(FocusBody));
	}
}

void AStellarEraPlayerController::FinishStarMapExit(APawn* ReturnPawn)
{
	// 先认下当前这台星图相机，Possess 之后就认不出来了。
	AStarMapCameraPawn* const Camera = Cast<AStarMapCameraPawn>(GetPawn());

	if (ReturnPawn)
	{
		Possess(ReturnPawn);
	}

	// 星图期间 CalcCamera 每帧把相机 Actor 拽到渲染位置，退出后它还留在半空。
	// 挂行星的那台必须搬回球心 —— 否则从外面看行星时，那台子相机飘在球外，
	// 下次进场又得从它此刻的位置重算。深空单例相机直接跳过（没有挂载父）。
	//
	// 放在 Possess 之后：换人以后 CalcCamera 不再跑，搬过去的相对变换才不会被
	// 下一帧的「Actor 跟着相机走」覆盖回来。
	if (Camera)
	{
		Camera->ResetToAttachOrigin();
	}
}

bool AStellarEraPlayerController::ProjectBodyToScreen(
	AActor* Body, FVector2D& OutCenter, float& OutRadiusPx) const
{
	FStarMapScreenCandidate Candidate;
	if (!StarMapPicking::MakeScreenCandidate(this, Body, Candidate))
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
	// 拾取本身已下沉到 Pawns（相机输入组件与 HUD 悬停共用同一份）。容差取相机上
	// 的值；相机不在手上（例如 HUD 在非星图帧查询）时退回默认值。
	const AStarMapCameraPawn* Camera = Cast<AStarMapCameraPawn>(GetPawn());
	const float PixelTolerance = Camera
		? Camera->GetSelectionPixelRadius()
		: StarMapPicking::DefaultPixelTolerance;
	return StarMapPicking::PickBodyUnderScreenPosition(
		GetWorld(), this, ScreenPosition, PixelTolerance);
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
