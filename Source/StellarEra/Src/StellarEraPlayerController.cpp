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
	 * 屏幕上一行提示（每帧刷新，duration 0）。固定 key 让屏幕上永远只有一行，
	 * 两个调用点共用，所以留在文件内而不是各编一个。
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
	// 输入组件由 DefaultInput.ini 指定为 EnhancedInput 版；缺失时 Cast 会失败。
	// 星图相机由行星的 Child Actor Component 提供，乘员由飞船提供；本类只查询。
}

void AStellarEraPlayerController::BeginPlay()
{
	Super::BeginPlay();

	// 动作资产在 BeginPlay 构造好再塞 IMC；三个 getter 若被更早调用也会自己 Build（幂等）。
	Input.Build(this);

	if (UEnhancedInputLocalPlayerSubsystem* Subsystem =
		ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer()))
	{
		Subsystem->AddMappingContext(Input.MappingContext, /*Priority=*/0);
	}
}

void AStellarEraPlayerController::OnPossess(APawn* InPawn)
{
	// 先拍下旧 Pawn：Super 会 UnPossess 并清空 GetPawn()，之后找不回刚换下的星图相机。
	APawn* const PreviousPossessed = GetPawn();
	Super::OnPossess(InPawn);

	// 换 Pawn（登船 / 离船 / 进出星图）一律退出矿物透视；会话状态住在相机 Pawn 上。
	if (AStarMapCameraPawn* PreviousCamera = Cast<AStarMapCameraPawn>(PreviousPossessed))
	{
		PreviousCamera->ExitMineralViewIfActive();
	}

	// 换视点：只有新 Pawn 的体素调用器参与 LOD 与碰撞。所有换 Pawn 路径都收口在这里。
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
	// 输入状态（左 / 右 / 中键）全归各 Pawn 的输入组件：换 Pawn 时松开事件送到新 Pawn。

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
		// SetInputMode 一次做三件手写易漏的事：ReleaseMouseCapture（下船时视口还攥着
		// 永久捕获）、SetHideCursorDuringCapture(false)（引擎捕获时会藏光标）、把捕获
		// 模式改成右键版。LockOnCapture 只在按住右键时锁，那正是需要无限旋转的时候。
		SetInputMode(FInputModeGameAndUI()
			.SetLockMouseToViewportBehavior(EMouseLockMode::LockOnCapture)
			.SetHideCursorDuringCapture(false));

		// 捕获是必需的：OnMouseMove 只在视口有捕获时累加位移，否则右键「按住了也没反应」。
		// CaptureDuringRightMouseDown 让同一次右键既进 InputKey 又拿位移，松开自动放掉。
		ViewportClient->SetMouseCaptureMode(EMouseCaptureMode::CaptureDuringRightMouseDown);
	}
	else if (bStarMap)
	{
		// 星图必须有捕获：MouseDelta 累加整段包在 if (bViewportHasCapture) 里（SceneViewport.cpp:905）。
		// 角色用右键版，星图要任意键版 CaptureDuringMouseDown（EngineBaseTypes.h:49）。
		SetInputMode(FInputModeGameAndUI()
			.SetLockMouseToViewportBehavior(EMouseLockMode::LockOnCapture)
			.SetHideCursorDuringCapture(false));
		ViewportClient->SetMouseCaptureMode(EMouseCaptureMode::CaptureDuringMouseDown);
	}
	else
	{
		// 飞船：永久捕获 + 光标隐藏，鼠标是它的操纵杆。GameOnly 会设捕获模式但
		// 不管 HideCursorDuringCapture，这里显式写回 true，免得被角色模式留下的 false 带过去。
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

	// 矿物透视绑在选中的行星上，改选就命令相机退出；换 Pawn 的退出在 OnPossess 收口，
	// 相机不 Tick，这条巡检只能留在这里。
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
		// 落地就提示，不要求舱里有角色：开局操控的就是飞船，角色第一次按 E 才生成。
		if (Ship->IsLanded())
		{
			ShowPrompt(TEXT("按 E 离船"));
		}
	}

	// 星图模式不在这里刷操作提示：每帧一条黄字像调试输出，且与星图 UI 重复。
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

	// 与按 E 同一条路径：取飞船上的角色（舱里没人时从挂接的子 Actor 取），摆好后换控制对象。
	// 不检查 IsLanded —— 无头验证时飞船可能还在下落。
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
		// 真登船时不接管：角色隐藏、附着、MOVE_None，直接 possess 会让 BoardedCharacter
		// 仍指着它。这种情况请按 E 走正常离船流程。
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