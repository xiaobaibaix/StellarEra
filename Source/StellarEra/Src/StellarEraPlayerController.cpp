// Copyright Voxel Plugin SAS. All Rights Reserved.

#include "StellarEraPlayerController.h"

#include "PlanetCharacter.h"
#include "StarMapUISystem.h"
#include "PlanetEditBrush.h"
#include "PlanetEditComponent.h"
#include "SpaceshipPawn.h"
#include "StellarEraHUD.h"
#include "CosmosSimulationSubsystem.h"
#include "StellarEraGameMode.h"
#include "Components/InputComponent.h"
#include "StarMapPicking.h"
#include "StarMapRigComponent.h"
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

	/** 任意 Pawn 上的编辑组件，不判模式。角色与飞船各有一个。 */
	UPlanetEditComponent* GetEditComponentForPawn(APawn* InPawn)
	{
		// 角色与飞船都用组件查：谁是当前 Pawn 就查谁。
		return InPawn ? InPawn->FindComponentByClass<UPlanetEditComponent>() : nullptr;
	}
}

AStellarEraPlayerController::AStellarEraPlayerController()
{
	// 输入组件由 DefaultInput.ini 指定为 EnhancedInput 版；缺失时 Cast 会失败。
	// 星图 rig 挂在飞船 / 角色上，乘员由飞船提供；本类只查询。
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

void AStellarEraPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();

	// 全局会话动作：与 Possess 的 Pawn 无关，直接绑在控制器自己的 InputComponent 上。
	// `=` 是 Shift+= 的同一个物理键；`-` 是 Hyphen。
	if (InputComponent)
	{
		InputComponent->BindKey(EKeys::Equals, IE_Pressed, this, &AStellarEraPlayerController::TimeScaleUp);
		InputComponent->BindKey(EKeys::Hyphen, IE_Pressed, this, &AStellarEraPlayerController::TimeScaleDown);
	}
}

void AStellarEraPlayerController::AdjustTimeScale(const int32 Direction)
{
	UCosmosSimulationSubsystem* Cosmos =
		GetWorld() ? GetWorld()->GetSubsystem<UCosmosSimulationSubsystem>() : nullptr;
	if (!Cosmos)
	{
		return;
	}

	// 倍率语义：1× = 实时（1 模拟年 = 1 真实年），所以基准取 1/年秒数。
	// 步长来自 GameMode（蓝图子类里调）。
	const AStellarEraGameMode* GameMode = GetWorld()->GetAuthGameMode<AStellarEraGameMode>();
	const float Step = GameMode ? GameMode->TimeScaleStep : 1.f;
	TimeScaleMultiplier = FMath::Clamp(TimeScaleMultiplier + Direction * Step, 0.125f, 100000.f);

	const double Base = 1.0 / CosmosUnits::YearToSecond;
	Cosmos->TimeScale = Base * double(TimeScaleMultiplier);
	UE_LOG(LogTemp, Display, TEXT("[Time] 时间倍率 %.2fx（TimeScale %.5f）"), TimeScaleMultiplier, Cosmos->TimeScale);
}

void AStellarEraPlayerController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);

	// 换视点：只有新 Pawn 的体素调用器参与 LOD 与碰撞。所有换 Pawn 路径都收口在这里。
	ApplyViewpointInvokerGate(GetWorld(), InPawn);

	// 每次上船都从驾驶开始，免得离船前留下的自由视角让光标一上船就出现。
	if (ASpaceshipPawn* Ship = Cast<ASpaceshipPawn>(InPawn))
	{
		Ship->SetFreeLookActive(false);
	}

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

	const ASpaceshipPawn* Ship = Cast<ASpaceshipPawn>(InPawn);
	bAppliedShipFreeLook = Ship && Ship->IsFreeLookActive();

	// 飞船自由视角与走路同一套：光标常驻、按住右键环绕。
	const bool bWalking = Cast<APlanetCharacter>(InPawn) != nullptr || bAppliedShipFreeLook;
	const UStarMapRigComponent* Rig = InPawn ? InPawn->FindComponentByClass<UStarMapRigComponent>() : nullptr;
	const bool bStarMap = Rig && Rig->IsActive();

	// 驾驶时光标隐藏：鼠标就是操纵杆。星图里光标也要可见可点。
	bShowMouseCursor = bWalking || bStarMap;

	UGameViewportClient* ViewportClient = GetWorld() ? GetWorld()->GetGameViewport() : nullptr;
	if (!ViewportClient)
	{
		return;
	}

	if (bStarMap)
	{
		// 星图必须有捕获：MouseDelta 累加整段包在 if (bViewportHasCapture) 里（SceneViewport.cpp:905）。
		// 角色用右键版，星图要任意键版 CaptureDuringMouseDown（EngineBaseTypes.h:49）。
		// 星图现在挂在当前 Pawn 上，必须排在走路分支之前，否则角色进图后左键拖不动。
		SetInputMode(FInputModeGameAndUI()
			.SetLockMouseToViewportBehavior(EMouseLockMode::LockOnCapture)
			.SetHideCursorDuringCapture(false));
		ViewportClient->SetMouseCaptureMode(EMouseCaptureMode::CaptureDuringMouseDown);
	}
	else if (bWalking)
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

	// 太空背景球跟相机走：半径 1 万 km 固定在天球中心时，飞出那个距离就会穿出去。
	// 跟随后球心永远在相机，等效无限远；角色 / 飞船 / 星图相机切换都自动覆盖。
	if (!SpaceBackdropActor)
	{
		for (TActorIterator<AActor> It(GetWorld()); It; ++It)
		{
			if (It->ActorHasTag(TEXT("SpaceBackdrop"))
				|| It->GetName().StartsWith(TEXT("SpaceBackdrop")))
			{
				SpaceBackdropActor = *It;
				break;
			}
		}
	}
	if (SpaceBackdropActor && PlayerCameraManager)
	{
		SpaceBackdropActor->SetActorLocation(PlayerCameraManager->GetCameraLocation());
	}

	// 星图进出不再走 Possess，鼠标模式只能在这里跟上；只在状态变化那一帧重设。
	APawn* CurrentPawn = GetPawn();
	const UStarMapRigComponent* Rig = CurrentPawn
		? CurrentPawn->FindComponentByClass<UStarMapRigComponent>() : nullptr;
	const bool bStarMapActive = Rig && Rig->IsActive();
	if (bStarMapActive != bAppliedStarMapActive)
	{
		bAppliedStarMapActive = bStarMapActive;
		ApplyMouseModeForPawn(CurrentPawn);
	}

	// 星图期间不刷操作提示：星图 UI 自己会提示，E / Tab 那时也归星图输入。
	if (!bStarMapActive)
	{
		// 提示只在“站在飞船旁”时出现，所以每帧重算一次距离。
		if (const APlanetCharacter* PawnCharacter = Cast<APlanetCharacter>(CurrentPawn))
		{
			// 与按 E 的判定同源（FindBoardableShipFrom）：提示与执行不能各算一套。
			if (FindBoardableShipFrom(PawnCharacter))
			{
				ShowPrompt(TEXT("按 E 登船"));
			}
		}
		else if (ASpaceshipPawn* Ship = Cast<ASpaceshipPawn>(CurrentPawn))
		{
			// Tab 只改飞船状态，鼠标模式在这里跟上；只在变化那一帧重设。
			if (Ship->IsFreeLookActive() != bAppliedShipFreeLook)
			{
				ApplyMouseModeForPawn(Ship);
			}

			// 落地就提示，不要求舱里有角色：开局操控的就是飞船，角色第一次按 E 才生成。
			if (Ship->IsLanded())
			{
				ShowPrompt(TEXT("按 E 离船"));
			}
		}
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