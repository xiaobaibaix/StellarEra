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

namespace
{
	/**
	 * 屏幕上一行提示（每帧刷新，duration 0）。
	 *
	 * 固定 key 让屏幕上永远只有一行，不会堆积。两个调用点共用这一个 key，
	 * 所以留在文件内，而不是每个调用点各编一个。
	 */
	void ShowPrompt(const FString& Text)
	{
		if (!GEngine)
		{
			return;
		}

		static const uint64 PromptMessageKey = 0x5745'4C43'4F4D'4521ULL;
		GEngine->AddOnScreenDebugMessage(PromptMessageKey, 0.f, FColor::Yellow, Text);
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

	// 本类不生成星图相机：它由行星的 Child Actor Component 提供
	// （BP_VoxelPlanet → BP_StarMapCameraPawn），由 FindStarMapCameraFor 取用。

	// 本类也不生成下船用的角色：乘员由飞船自己提供（舱里的记账，或直接挂在
	// 飞船下的 APlanetCharacter），查找在基类的 GetOrSpawnCrewCharacter 里。
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

void AStellarEraPlayerController::OnPossess(APawn* InPawn)
{
	// 先拍下旧 Pawn：Super 会在内部 UnPossess 并清空 GetPawn()，
	// 之后再想找「刚被换下的星图相机」就找不到了。
	APawn* const PreviousPossessed = GetPawn();
	Super::OnPossess(InPawn);

	// 换 Pawn（登船 / 离船 / 进出星图）一律退出矿物透视。
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

void AStellarEraPlayerController::ForceExitEditMode(APawn* InPawn)
{
	// 形参不能叫 Pawn：AController 自己有个 Pawn 成员，遮蔽会被当成错误（C4458）。
	UPlanetEditComponent* Edit = GetEditComponentForPawn(InPawn);
	if (Edit && Edit->IsEditMode())
	{
		Edit->SetEditMode(false);
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
	if (const APlanetCharacter* PawnCharacter = Cast<APlanetCharacter>(GetPawn()))
	{
		// 与按 E 的判定同源（FindBoardableShipFrom）：提示与执行不能各算一套。
		if (FindBoardableShipFrom(PawnCharacter))
		{
			ShowPrompt(TEXT("按 E 登船"));
		}
	}
	else if (ASpaceshipPawn* Ship = Cast<ASpaceshipPawn>(GetPawn()))
	{
		// 落地就提示，不再要求「舱里有角色」：开局操控的就是飞船，角色要等第一次
		// 按 E 才生成。按 E 一定会生成一个，提示不会骗人。
		if (Ship->IsLanded())
		{
			ShowPrompt(TEXT("按 E 离船"));
		}
	}

	// 星图模式不在这里刷操作提示：那是每帧一条 AddOnScreenDebugMessage（黄字），
	// 看起来就是调试输出，而且和星图自己的 UI 重复。操作说明归星图 UI 管。
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

	// 与飞船的 E 同一条路径：取得飞船上的角色（舱里没人时从挂接的子 Actor 取），
	// 摆好，再换控制对象。落地判据之外不做任何额外检查（无头验证时飞船可能还
	// 在下落，但取角色的逻辑与 IsLanded 无关）。
	APlanetCharacter* Crew = GetOrSpawnCrewCharacter(Ship);
	if (!Crew)
	{
		return;
	}

	Ship->DisembarkCrew(Crew);
	Possess(Crew);
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