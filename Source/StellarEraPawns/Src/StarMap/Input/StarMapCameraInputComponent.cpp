// Copyright Voxel Plugin SAS. All Rights Reserved.

#include "StarMapCameraInputComponent.h"

#include "CosmosSimulationSubsystem.h"
#include "EnhancedInputComponent.h"
#include "Engine/World.h"
#include "GameFramework/Controller.h"
#include "GameFramework/HUD.h"
#include "GameFramework/PlayerController.h"
#include "InputActionValue.h"
#include "InputCoreTypes.h"
#include "PlanetEditComponent.h"
#include "PlanetEditUI.h"
#include "PlanetMineralViewComponent.h"
#include "SessionCommands.h"
#include "StarMapCameraInputActions.h"
#include "StarMapCameraPawn.h"
#include "StarMapPanelLayout.h"
#include "StarMapPicking.h"
#include "StellarEraInputActionBuilder.h"
#include "StellarEraInputSource.h"
#include "StellarEraNoticeSink.h"

AStarMapCameraPawn* UStarMapCameraInputComponent::GetPawn() const
{
	return Cast<AStarMapCameraPawn>(GetOwner());
}

APlayerController* UStarMapCameraInputComponent::GetPlayerController() const
{
	const AStarMapCameraPawn* Pawn = GetPawn();
	return Pawn ? Cast<APlayerController>(Pawn->GetController()) : nullptr;
}

void UStarMapCameraInputComponent::ShowNotice(const FString& Text) const
{
	// HUD 在 UI 模块（本模块的下游），只能走接口。没有 HUD 时静默跳过。
	const APlayerController* PC = GetPlayerController();
	IStellarEraNoticeSink* Sink = PC ? Cast<IStellarEraNoticeSink>(PC->GetHUD()) : nullptr;
	if (Sink)
	{
		Sink->ShowNotice(Text);
	}
}

UPlanetEditComponent* UStarMapCameraInputComponent::GetEditComponentInEditMode() const
{
	const AStarMapCameraPawn* Pawn = GetPawn();
	UPlanetEditComponent* Edit = Pawn ? Pawn->GetPlanetEditComponent() : nullptr;
	return Edit && Edit->IsEditMode() ? Edit : nullptr;
}

EStarMapLookMode UStarMapCameraInputComponent::ResolveLookMode(
	const bool bRightHeld,
	const bool bLeftHeld,
	const bool bMiddleHeld,
	const bool bEditMode,
	const bool bContinuousDigging,
	const bool bActiveMineralView)
{
	// 连续挖期间一个键都不放行：挖掘点跟着光标走，视角一动路径就画歪了。
	// 转头也算 —— 视线一变，屏幕射线跟着变，挖掘点一样会漂。
	if (bContinuousDigging)
	{
		return EStarMapLookMode::None;
	}

	// 右键优先：它只转头、不挪相机，与同时按住的其它键不冲突。
	if (bRightHeld)
	{
		return EStarMapLookMode::Aim;
	}

	// 中键绕行不看模式：编辑 / 矿物透视下左键被借走时，它就是唯一的绕行键。
	if (bMiddleHeld)
	{
		return EStarMapLookMode::Orbit;
	}

	// 左键在编辑模式（挖掘 / 填充）与矿物透视（图例点击）里已经被借走，
	// 这两种状态下按下左键必须什么都不做。
	if (bLeftHeld && !bEditMode && !bActiveMineralView)
	{
		return EStarMapLookMode::Orbit;
	}

	return EStarMapLookMode::None;
}

void UStarMapCameraInputComponent::Bind(UInputComponent* PlayerInputComponent)
{
	// 每次 Possess 都会重绑。按着鼠标切进 / 切出星图时，松开事件会送到别的 Pawn
	// 身上，标志就永远停在 true —— 一进星图没按键也在转视角。在这里清一次。
	// 放在早退之前：绑失败时输入本来就全哑，但标志不该留着上一轮的旧值。
	bLookHeld = false;
	bMiddleHeld = false;
	bSelectHeld = false;

	AStarMapCameraPawn* Pawn = GetPawn();
	UEnhancedInputComponent* EnhancedInput = Cast<UEnhancedInputComponent>(PlayerInputComponent);
	IStellarEraInputSource* Source = Pawn ? Cast<IStellarEraInputSource>(Pawn->GetController()) : nullptr;
	if (!Pawn || !EnhancedInput || !Source)
	{
		// 静默解绑等于「星图对键鼠完全没反应」——留一条 Error，别让它变成哑故障。
		UE_LOG(LogTemp, Error, TEXT("[Input] AStarMapCameraPawn 绑定失败：Pawn=%s EnhancedInput=%s InputSource=%s Controller=%s"),
			*GetNameSafe(Pawn),
			EnhancedInput ? TEXT("是") : TEXT("否"),
			Source ? TEXT("是") : TEXT("否"),
			*GetNameSafe(Pawn ? Pawn->GetController() : nullptr));
		return;
	}

	// 只取星图相机自己那张表：别的 Pawn 的动作不归这里管，也就不可能绑错。
	// 左键选择 / M 也在这张表里 —— 同样的按键在角色与飞船上各有一条，语义不同。
	const FStarMapCameraInputActions& Input = Source->GetStarMapCameraInputActions();

	EnhancedInput->BindAction(Input.Look, ETriggerEvent::Triggered, this, &UStarMapCameraInputComponent::OnLookInput);
	EnhancedInput->BindAction(Input.LookActivate, ETriggerEvent::Started, this, &UStarMapCameraInputComponent::OnLookActivateStarted);
	EnhancedInput->BindAction(Input.LookActivate, ETriggerEvent::Completed, this, &UStarMapCameraInputComponent::OnLookActivateCompleted);
	EnhancedInput->BindAction(Input.LookMiddle, ETriggerEvent::Started, this, &UStarMapCameraInputComponent::OnLookMiddleStarted);
	EnhancedInput->BindAction(Input.LookMiddle, ETriggerEvent::Completed, this, &UStarMapCameraInputComponent::OnLookMiddleCompleted);

	// 只绑 Triggered：滚轮每格是一次独立冲量，不滚就不产生值，没有 Completed 可收。
	EnhancedInput->BindAction(Input.Zoom, ETriggerEvent::Triggered, this, &UStarMapCameraInputComponent::OnZoomInput);

	// 编辑模式是「这个 Pawn 自己的组件」，所以 G 与笔刷尺寸跟着相机走。
	// 只绑 Started：Axis1D 的 Triggered 在按住时每帧触发一次，笔刷会瞬间冲到上限。
	EnhancedInput->BindAction(Input.EditModeToggle, ETriggerEvent::Started, this, &UStarMapCameraInputComponent::OnEditModeToggle);
	EnhancedInput->BindAction(Input.BrushSize, ETriggerEvent::Started, this, &UStarMapCameraInputComponent::OnBrushSize);

	// 矿物透视（H）与数字键只在本相机被操控时有意义，会话状态也住在相机 Pawn 上。
	EnhancedInput->BindAction(Input.MineralViewToggle, ETriggerEvent::Started,
		this, &UStarMapCameraInputComponent::OnMineralViewToggle);
	for (int32 ElementIndex = 0; ElementIndex < Input.MineralFilter.Num(); ++ElementIndex)
	{
		EnhancedInput->BindAction(Input.MineralFilter[ElementIndex], ETriggerEvent::Started,
			this, &UStarMapCameraInputComponent::OnMineralFilter, ElementIndex);
	}
	EnhancedInput->BindAction(Input.MineralShowAll, ETriggerEvent::Started,
		this, &UStarMapCameraInputComponent::OnMineralShowAll);

	// 左键选择与 M 都在相机自己那张表里。M 必须也绑在这里，否则进了星图之后
	// 就没有「按 M 退出」的入口（此刻 Possess 的是本相机）。
	EnhancedInput->BindAction(Input.StarMapSelect, ETriggerEvent::Started,
		this, &UStarMapCameraInputComponent::OnStarMapSelectStarted);
	EnhancedInput->BindAction(Input.StarMapSelect, ETriggerEvent::Completed,
		this, &UStarMapCameraInputComponent::OnStarMapSelectCompleted);
	EnhancedInput->BindAction(Input.StarMapToggle, ETriggerEvent::Started,
		this, &UStarMapCameraInputComponent::OnStarMapToggle);

	// 绑定成功也给一条 Verbose：「没报错」不等于「绑上了」，无头跑时用它当正证据。
	UE_LOG(LogTemp, Verbose, TEXT("[Input] AStarMapCameraPawn(%s) 已绑定星图相机输入"), *Pawn->GetName());
}

void UStarMapCameraInputComponent::OnLookInput(const FInputActionValue& Value)
{
	AStarMapCameraPawn* Pawn = GetPawn();
	if (!Pawn)
	{
		return;
	}

	const FVector2D Axis = Value.Get<FVector2D>();
	const float DeltaYaw = Axis.X * LookSensitivity;
	const float DeltaPitch = (bInvertLookY ? -Axis.Y : Axis.Y) * LookSensitivity;

	const UPlanetEditComponent* Edit = GetEditComponentInEditMode();

	switch (ResolveLookMode(
		bLookHeld,
		bSelectHeld,
		bMiddleHeld,
		Edit != nullptr,
		Edit && Edit->IsContinuousDigging(),
		Pawn->IsMineralViewActive()))
	{
	case EStarMapLookMode::Orbit:
		Pawn->OrbitLook(DeltaYaw, DeltaPitch);
		break;
	case EStarMapLookMode::Aim:
		Pawn->AimLook(DeltaYaw, DeltaPitch);
		break;
	case EStarMapLookMode::None:
	default:
		break;
	}
}

void UStarMapCameraInputComponent::OnLookActivateStarted(const FInputActionValue& Value)
{
	// 「按住右键 = 转头」。角色的右键（视角开关 + 编辑点放）由
	// UPlanetCharacterInputComponent 自己绑同一个动作、自己处理。
	bLookHeld = true;
}

void UStarMapCameraInputComponent::OnLookActivateCompleted(const FInputActionValue& Value)
{
	bLookHeld = false;
}

void UStarMapCameraInputComponent::OnLookMiddleStarted(const FInputActionValue& Value)
{
	// 编辑模式下左键被借走时的绕行键，见 ResolveLookMode。
	bMiddleHeld = true;
}

void UStarMapCameraInputComponent::OnLookMiddleCompleted(const FInputActionValue& Value)
{
	bMiddleHeld = false;
}

void UStarMapCameraInputComponent::OnZoomInput(const FInputActionValue& Value)
{
	if (AStarMapCameraPawn* Pawn = GetPawn())
	{
		Pawn->AddZoom(Value.Get<float>());
	}
}

void UStarMapCameraInputComponent::OnEditModeToggle(const FInputActionValue& Value)
{
	AStarMapCameraPawn* Pawn = GetPawn();

	// 矿物透视下左键归图例：编辑模式会抢左键并改写体素，所以直接忽略切换，
	// 保持进入观察时强制退出的状态。
	if (!Pawn || Pawn->IsMineralViewActive())
	{
		return;
	}

	if (UPlanetEditComponent* Edit = Pawn->GetPlanetEditComponent())
	{
		Edit->ToggleEditMode();
	}
}

void UStarMapCameraInputComponent::OnBrushSize(const FInputActionValue& Value)
{
	AStarMapCameraPawn* Pawn = GetPawn();
	if (!Pawn)
	{
		return;
	}

	const float Axis = Value.Get<float>();
	if (FMath::IsNearlyZero(Axis))
	{
		return;
	}

	if (UPlanetEditComponent* Edit = GetEditComponentInEditMode())
	{
		// 星图区间跨到 1280，一档 10 体素要按一百多下 —— 按住 Ctrl 走 ×10 粗调。
		APlayerController* PC = Cast<APlayerController>(Pawn->GetController());
		const bool bCoarse = PC
			&& (PC->IsInputKeyDown(EKeys::LeftControl) || PC->IsInputKeyDown(EKeys::RightControl));
		Edit->AdjustBrushSizeByStep(Axis > 0.f, bCoarse);
	}
}

void UStarMapCameraInputComponent::OnMineralViewToggle(const FInputActionValue& Value)
{
	AStarMapCameraPawn* Pawn = GetPawn();
	if (!Pawn)
	{
		return;
	}

	// 进不去时原因由相机 Pawn 返回，提示走本组件已有的 HUD 通道（与笔刷提示同一条）。
	const FString Failure = Pawn->ToggleMineralView();
	if (!Failure.IsEmpty())
	{
		ShowNotice(Failure);
	}
}

void UStarMapCameraInputComponent::OnMineralFilter(
	const FInputActionValue& Value, const int32 ElementIndex)
{
	if (AStarMapCameraPawn* Pawn = GetPawn())
	{
		Pawn->ToggleMineralElement(ElementIndex);
	}
}

void UStarMapCameraInputComponent::OnMineralShowAll(const FInputActionValue& Value)
{
	if (AStarMapCameraPawn* Pawn = GetPawn())
	{
		Pawn->ShowAllMineralElements();
	}
}

void UStarMapCameraInputComponent::OnStarMapSelectStarted(const FInputActionValue& Value)
{
	AStarMapCameraPawn* Pawn = GetPawn();
	if (!Pawn)
	{
		return;
	}

	// 编辑模式：按钮优先，其次一次笔刷操作。按钮命中不能顺带挖到脚下的地形。
	// 矿物透视激活期间左键归图例：短路编辑分支，否则左键会挖 / 填体素。
	UPlanetEditComponent* Edit = Pawn->IsMineralViewActive() ? nullptr : GetEditComponentInEditMode();
	if (Edit)
	{
		APlayerController* PC = GetPlayerController();
		double MouseX = 0.0;
		double MouseY = 0.0;
		if (!PC || !PC->GetMousePosition(MouseX, MouseY))
		{
			return;
		}
		const FVector2D MousePosition(MouseX, MouseY);

		// AHUD::Canvas 只在 PostRender 期间有效，而输入回调跑在 tick 里 —— 问
		// HUD 要视口尺寸永远拿到 (0,0)，按钮命中会全部落空。所以从控制器自己查。
		int32 ViewportSizeX = 0;
		int32 ViewportSizeY = 0;
		PC->GetViewportSize(ViewportSizeX, ViewportSizeY);
		const FVector2D ViewportSize(
			static_cast<float>(ViewportSizeX), static_cast<float>(ViewportSizeY));

		int32 ButtonIndex = INDEX_NONE;
		if (ViewportSize.X > 0.0 && ViewportSize.Y > 0.0 &&
			PlanetEditUI::HitTestBrushButton(ViewportSize, MousePosition, ButtonIndex))
		{
			const bool bSphere = ButtonIndex == 0;
			Edit->SetBrushShape(bSphere ? EPlanetBrushShape::Sphere : EPlanetBrushShape::Cube);
			ShowNotice(bSphere ? TEXT("笔刷：球体") : TEXT("笔刷：正方体"));
			return;
		}

		// 这次点击归不归编辑，由「射线打没打到行星」回答。不能用 DigAtScreenPosition
		// 的返回值判断：打到了行星但没改到任何体素（比如往空气里挖）也会返回 false。
		FPlanetEditTarget Target;
		if (Edit->TryResolveTarget(MousePosition, Target))
		{
			// Shift+左键按住 = 连续挖（移动光标沿路径挖）；Ctrl+左键 = 填一次；
			// 左键 = 挖一次。
			const bool bShift =
				PC->IsInputKeyDown(EKeys::LeftShift) || PC->IsInputKeyDown(EKeys::RightShift);
			const bool bCtrl =
				PC->IsInputKeyDown(EKeys::LeftControl) || PC->IsInputKeyDown(EKeys::RightControl);

			if (bShift)
			{
				// 按下先挖一次，之后交给组件的 Tick 节流。
				Edit->SetContinuousDigging(true);
				Edit->DigAtScreenPosition(MousePosition);
			}
			else if (bCtrl)
			{
				Edit->PlaceAtScreenPosition(MousePosition);
			}
			else
			{
				Edit->DigAtScreenPosition(MousePosition);
			}
			return;
		}

		// 编辑模式下左键只归挖掘 / 填充：连「按下的位置」都不记。否则点在行星
		// 轮廓边上一圈（射线没打到地表，但落在拾取的像素半径内）时，松开那一下
		// 会把当前聚焦的天体取消选中 —— 挖着挖着焦点就没了。想换选中就退出
		// 编辑模式（G）。
		return;
	}

	if (UPlanetMineralViewComponent* MineralView = Pawn->GetActiveMineralView())
	{
		APlayerController* PC = GetPlayerController();
		double MouseX = 0.0;
		double MouseY = 0.0;
		if (!PC || !PC->GetMousePosition(MouseX, MouseY))
		{
			return;
		}

		// 与绘制同一份纯函数：视口尺寸从控制器查（Canvas 只在 PostRender 有效）。
		int32 ViewportSizeX = 0;
		int32 ViewportSizeY = 0;
		PC->GetViewportSize(ViewportSizeX, ViewportSizeY);
		const FVector2D ViewportSize(
			static_cast<float>(ViewportSizeX), static_cast<float>(ViewportSizeY));

		FStarMapPanelRect Panel;
		TArray<FStarMapPanelRect> RowRects;
		StarMapPanelLayout::ComputeMineralLegendLayout(
			ViewportSize, MineralView->NumElements(), Panel, RowRects);

		const int32 RowIndex = StarMapPanelLayout::HitTestMineralLegendRow(
			RowRects, FVector2D(MouseX, MouseY));
		if (RowRects.IsValidIndex(RowIndex))
		{
			MineralView->ToggleElement(RowIndex);
		}

		// 观察模式下左键不再拾取天体，也不转视角（转视角归右键）。
		return;
	}

	// 用 double 重载，是为了后面直接喂给 FVector2D（UE5 里是双精度）时少一次转换。
	// **但精度上并不占便宜**：引擎那个重载内部就是 `float X, Y; GetMousePosition(X, Y);`
	// —— 先过 float 再展宽，两个重载拿到的是同一个值。
	double MouseX = 0.0;
	double MouseY = 0.0;
	if (APlayerController* PC = GetPlayerController(); PC && PC->GetMousePosition(MouseX, MouseY))
	{
		SelectPressPosition = FVector2D(MouseX, MouseY);
		bSelectHeld = true;
	}
}

void UStarMapCameraInputComponent::OnStarMapSelectCompleted(const FInputActionValue& Value)
{
	// 连续挖必须在所有早退之前关掉：按下时命中了行星走的是「消费点击」分支，
	// bSelectHeld 没被置位，下面的早退会把它跳过。
	// 按当前 Pawn 查组件不会漏：现有的每条退出路径（M、G、H、登船）都经过
	// SetEditMode(false)，它会顺手清掉连挖标志（见 UPlanetEditComponent::SetEditMode）。
	if (UPlanetEditComponent* Edit = GetEditComponentInEditMode())
	{
		Edit->SetContinuousDigging(false);

		// 编辑模式下按下时不会记位置，这里也不拾取：挖矿期间左键只挖，焦点不该
		// 被点没。顺手清掉可能残留的标志 —— 进编辑模式前就按着的左键，松开时
		// 不该补一次选中切换。
		bSelectHeld = false;
		return;
	}

	AStarMapCameraPawn* Pawn = GetPawn();
	if (!Pawn)
	{
		return;
	}

	if (Pawn->GetActiveMineralView())
	{
		// 观察模式下左键只用来点图例，松手不做任何事。
		return;
	}

	if (!bSelectHeld)
	{
		return;
	}
	bSelectHeld = false;

	APlayerController* PC = GetPlayerController();
	double MouseX = 0.0;
	double MouseY = 0.0;
	if (!PC || !PC->GetMousePosition(MouseX, MouseY))
	{
		return;
	}

	// 按下与松开的位移大于阈值 → 这是「拖拽转视角」的收尾，不改选中。
	const double MovedSq = FVector2D::DistSquared(
		SelectPressPosition, FVector2D(MouseX, MouseY));
	if (MovedSq > StellarEraInput::ClickMoveThresholdPx * StellarEraInput::ClickMoveThresholdPx)
	{
		return;
	}

	// 点空白什么都不做（spec §5.2：取消选中会让相机停止跟随、丢失目标，
	// 绑在误点很容易触发的动作上不划算）。要取消就明确地点回它自己。
	AActor* Picked = StarMapPicking::PickBodyUnderScreenPosition(
		GetWorld(), PC, FVector2D(MouseX, MouseY), Pawn->GetSelectionPixelRadius());
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

void UStarMapCameraInputComponent::OnStarMapToggle(const FInputActionValue& Value)
{
	// 退图完全不问控制器：相机自己记着是哪个 Pawn 把它叫出来的，退回去 Possess
	// 谁、混合多久、相机搬回哪里，全是它自己的事。
	if (AStarMapCameraPawn* Pawn = GetPawn())
	{
		Pawn->ExitStarMap();
	}
}
